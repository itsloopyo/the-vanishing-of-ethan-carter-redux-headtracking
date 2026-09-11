// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/unreal/ue_math.h>

// Where on the drawn frame the player's aim actually lands.
//
// The game's crosshair marks the centre of the CLEAN camera - the one the mouse
// drives and the one every interaction trace reads. Head tracking renders from a
// different camera, so the two stop agreeing the moment the head turns or leans,
// and the crosshair becomes a lie. This turns the clean aim into a position in
// the drawn frame so the stock crosshair can be moved onto it.
//
// It projects a POINT, not a direction. With the eye where the game put it the
// two are the same, which is why a direction is enough until 6DOF lands; a lean
// breaks it, because the frame is drawn from an eye up to 30cm away from the one
// the aim leaves. A reticle built on a direction then slides off the thing it
// marks, worse the closer the target.
//
// Basis-to-basis, never a per-axis Euler formula: the vectors handed in are the
// ones the camera hook wrote, so one derivation is used twice and cannot
// disagree with itself.

namespace ecr_ht {

/// Aim position in the drawn frame, x right, y up, -1..1 across the frame.
struct AimNdc {
    bool valid = false;
    float x = 0.0f;
    float y = 0.0f;
};

/// Half-field tangents of the view being DRAWN. UE4's FMinimalViewInfo::FOV is
/// the HORIZONTAL angle - measured, not assumed: a 25 degree head yaw moves the
/// 1280-wide frame 292px, which is a 90 degree horizontal field, and the 20
/// degree pitch that moves it 232px vertically is the 58.9 degrees that
/// horizontal field implies at 16:9.
struct HalfFieldTangents {
    float horizontal = 1.0f;
    float vertical = 1.0f;
};

/// Tangents for a horizontal field of view in degrees over a viewport of
/// `width` x `height` pixels. Zero or negative inputs return the identity
/// tangents and are the caller's cue to skip the projection.
HalfFieldTangents TangentsFromHorizontalFov(float horizontal_fov_degrees,
                                            float width, float height);

/// Projects `aim_point` - a world position on the surface the clean aim ray hit
/// - into the frame drawn from `render_eye` with `render_rotation`.
///
/// Invalid, rather than clamped, when the point is at or behind the plane of the
/// eye: the projection goes to infinity as the forward component goes to zero,
/// and a reticle at 1e30 is a NaN on its way to a draw call.
AimNdc ProjectAimPoint(const cameraunlock::unreal::FVector& render_eye,
                       const cameraunlock::unreal::FQuat4d& render_rotation,
                       const cameraunlock::unreal::FVector& aim_point,
                       const HalfFieldTangents& tangents);

}  // namespace ecr_ht
