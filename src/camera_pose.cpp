// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_pose.h"

#include <cmath>

#include <cameraunlock/camera/zoom_compensation.h>

namespace ecr_ht {

namespace {

namespace ue = ::cameraunlock::unreal;

// The session hands out metres; UE works in centimetres.
constexpr double kMetresToUE = 100.0;

// A tracker angle is untrusted input: the socket binds every interface, and
// core's packet parser rejects only NON-FINITE values, so a finite 3e38 degrees
// from anything that can reach the port arrives here intact. One sensitivity
// multiply or zoom scale past that overflows to infinity, and the engine turns
// the FRotator it is handed into radians and through SinCos, so an infinite
// angle is a NaN view matrix for the frame. Two full turns is past anything a
// head or a tracker produces, so the bound costs a real pose nothing.
constexpr float kMaxTrackedAngleDeg = 720.0f;

// Widest angle the zoom rescale is defined for. ScaleAngleForZoom is
// atan(tan(angle) * factor), so it needs |angle| < 90: tan is discontinuous
// there and the round trip changes SIGN past it. At a zoom of 1.11 - what this
// game reaches when it widens the view to run - a 90.5 degree yaw would come
// back as -89.6, snapping the view to the opposite side for as long as the
// player held the turn and the sprint. 89 degrees keeps clear of the pole while
// costing a real pose nothing: the largest step across the boundary is about a
// tenth of a degree, because tan is already so steep there that scaling barely
// moves the angle.
constexpr float kMaxZoomScalableDeg = 89.0f;

float BoundAngle(float degrees) {
    if (!std::isfinite(degrees)) return 0.0f;
    if (degrees < -kMaxTrackedAngleDeg) return -kMaxTrackedAngleDeg;
    if (degrees > kMaxTrackedAngleDeg) return kMaxTrackedAngleDeg;
    return degrees;
}

// Passes an angle the rescale is not defined for straight through rather than
// letting it wrap. Outside the domain the tangent is so steep that the scaled
// and unscaled angles differ by a fraction of a degree anyway, so the
// pass-through is very nearly continuous with the scaled side of it.
float ScaleAngle(float degrees, float zoom) {
    if (!std::isfinite(degrees) || std::fabs(degrees) >= kMaxZoomScalableDeg) return degrees;
    return ::cameraunlock::camera::ScaleAngleForZoom(degrees, zoom);
}

}

ue::FQuat4d CleanViewQuat(const FRotator3f& clean) {
    return ue::QuatFromEulerDeg(clean.Pitch, clean.Yaw, clean.Roll);
}

FRotator3f ComposeViewRotation(const FRotator3f& clean, const ue::FQuat4d& cleanQ,
                               float yaw, float pitch, float roll,
                               bool world_space_yaw) {
    // Bounded here rather than at either call site: this is the one point where
    // a tracker angle becomes an engine rotation, so neither yaw mode can be
    // reached with a value the engine's rotation maths cannot survive.
    yaw = BoundAngle(yaw);
    pitch = BoundAngle(pitch);
    roll = BoundAngle(roll);

    if (world_space_yaw) {
        return FRotator3f{clean.Pitch + pitch, clean.Yaw + yaw, clean.Roll - roll};
    }

    const ue::FQuat4d headLocalQ = ue::QuatFromEulerDeg(
        static_cast<double>(pitch), static_cast<double>(yaw), -static_cast<double>(roll));
    const ue::FRotator composed = ue::QuatToRotator(ue::QuatMul(cleanQ, headLocalQ));
    return FRotator3f{
        static_cast<float>(composed.Pitch),
        static_cast<float>(composed.Yaw),
        static_cast<float>(composed.Roll),
    };
}

ScaledPose ScalePoseForZoom(float yaw, float pitch, float roll,
                            float x, float y, float z, float zoom) {
    if (zoom == 1.0f) return ScaledPose{yaw, pitch, roll, x, y, z};

    return ScaledPose{
        ScaleAngle(yaw, zoom),
        ScaleAngle(pitch, zoom),
        roll,
        x * zoom,
        y * zoom,
        z * zoom,
    };
}

ue::FVector ComputePositionOffset(const ue::FQuat4d& cleanQ,
                                  float offset_x, float offset_y, float offset_z) {
    const ue::FVector camFwd   = ue::QuatRotateVec(cleanQ, ue::FVector{1.0, 0.0, 0.0});
    const ue::FVector camRight = ue::QuatRotateVec(cleanQ, ue::FVector{0.0, 1.0, 0.0});
    const ue::FVector camUp    = ue::QuatRotateVec(cleanQ, ue::FVector{0.0, 0.0, 1.0});

    // Surge and sway are mirrored between the processor and UE's camera basis,
    // and both are negated here, at the engine boundary, rather than through the
    // processor's invert_x / invert_z - inversion happens BEFORE the clamp, so
    // flipping z there would move the generous 0.40m budget onto leaning back
    // and leave 0.10m for leaning in.
    //
    // z: the processor clamps to [-limit_z, +limit_z_back], i.e. NEGATIVE z is
    // the forward lean, while UE's camera-local +x is forward.
    // x: the tracker's sway runs opposite to UE's camera-right; left un-negated
    // the camera slides right as the player leans left. Identical to the three
    // shipped UE siblings, whose signs were confirmed in game.
    const double surge = -static_cast<double>(offset_z) * kMetresToUE;  // -> forward
    const double sway  = -static_cast<double>(offset_x) * kMetresToUE;  // -> right
    const double heave =  static_cast<double>(offset_y) * kMetresToUE;  // -> up

    return ue::FVector{
        camFwd.X * surge + camRight.X * sway + camUp.X * heave,
        camFwd.Y * surge + camRight.Y * sway + camUp.Y * heave,
        camFwd.Z * surge + camRight.Z * sway + camUp.Z * heave,
    };
}

}
