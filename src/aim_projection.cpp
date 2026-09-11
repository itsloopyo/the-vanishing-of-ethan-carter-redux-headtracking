// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "aim_projection.h"

#include <cmath>

namespace ecr_ht {

namespace {

namespace ue = ::cameraunlock::unreal;

constexpr double kPi = 3.14159265358979323846;

// Below this the aim point is level with the eye or behind it and the
// perspective divide has no answer worth drawing.
constexpr double kMinForward = 1.0;

double Dot(const ue::FVector& a, const ue::FVector& b) {
    return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}

}  // namespace

HalfFieldTangents TangentsFromHorizontalFov(float horizontal_fov_degrees,
                                            float width, float height) {
    HalfFieldTangents out;
    if (!(horizontal_fov_degrees > 0.0f) || !(width > 0.0f) || !(height > 0.0f)) return out;

    const double half = static_cast<double>(horizontal_fov_degrees) * kPi / 360.0;
    out.horizontal = static_cast<float>(std::tan(half));
    // UE4 derives the vertical field from the horizontal one and the viewport
    // aspect (AspectRatio_MaintainXFOV), so the vertical tangent is the
    // horizontal one scaled by the aspect rather than a second FOV to read.
    out.vertical = static_cast<float>(out.horizontal * (height / width));
    return out;
}

AimNdc ProjectAimPoint(const ue::FVector& render_eye,
                       const ue::FQuat4d& render_rotation,
                       const ue::FVector& aim_point,
                       const HalfFieldTangents& tangents) {
    AimNdc out;
    if (!(tangents.horizontal > 0.0f) || !(tangents.vertical > 0.0f)) return out;

    // UE's camera basis: local +X forward, +Y right, +Z up.
    const ue::FVector fwd   = ue::QuatRotateVec(render_rotation, ue::FVector{1.0, 0.0, 0.0});
    const ue::FVector right = ue::QuatRotateVec(render_rotation, ue::FVector{0.0, 1.0, 0.0});
    const ue::FVector up    = ue::QuatRotateVec(render_rotation, ue::FVector{0.0, 0.0, 1.0});

    const ue::FVector aim{aim_point.X - render_eye.X,
                          aim_point.Y - render_eye.Y,
                          aim_point.Z - render_eye.Z};

    const double forward = Dot(aim, fwd);
    if (!(forward > kMinForward)) return out;

    const double x = Dot(aim, right) / forward / static_cast<double>(tangents.horizontal);
    const double y = Dot(aim, up)    / forward / static_cast<double>(tangents.vertical);
    if (!std::isfinite(x) || !std::isfinite(y)) return out;

    out.valid = true;
    out.x = static_cast<float>(x);
    out.y = static_cast<float>(y);
    return out;
}

}  // namespace ecr_ht
