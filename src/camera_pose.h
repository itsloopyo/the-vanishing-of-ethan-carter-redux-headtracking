// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/unreal/ue_math.h>

#include "ue_abi.h"

// Pure pose maths for the GetPlayerViewPoint hook: no globals, no logging, no
// engine memory. Every axis sign and composition order the camera depends on
// lives here, so it can be pinned by tests rather than re-derived from the
// hook body.

namespace ecr_ht {

/// Quaternion form of the game's own (clean) view rotation. The hook computes
/// this once per injected frame: both the rotation composition and the 6DOF
/// position basis need it.
cameraunlock::unreal::FQuat4d CleanViewQuat(const FRotator3f& clean);

/// The view rotation to render this frame, from the clean rotation and the
/// tracker pose in degrees. `cleanQ` must be CleanViewQuat(clean).
///
/// world_space_yaw true  = horizon-locked: FRotator addition, so yaw always
///                         turns about the world up-axis.
/// world_space_yaw false = camera-local: quaternion post-multiply, which leans
///                         the view on pitched turns. Roll is inverted on this
///                         path only, matching OpenTrack's convention against
///                         the engine's.
FRotator3f ComposeViewRotation(const FRotator3f& clean,
                               const cameraunlock::unreal::FQuat4d& cleanQ,
                               float yaw, float pitch, float roll,
                               bool world_space_yaw);

/// A tracker pose after zoom compensation: the rotation in degrees and the
/// position in the units it arrived in.
struct ScaledPose {
    float yaw;
    float pitch;
    float roll;
    float x;
    float y;
    float z;
};

/// Rescales a pose so it displaces the picture by as much at the field of view
/// the frame is being rendered at as it did at the game's un-zoomed one.
///
/// Yaw, pitch and the lean all translate the image across the frame, so they
/// scale. Roll is passed through: it ROTATES the image about the view
/// axis, and ten degrees of head roll rolls the picture ten degrees at every
/// field of view there is, so scaling it would flatten a tilt the player is
/// holding and buy nothing.
///
/// `zoom` of exactly 1.0 - ordinary play - returns the pose unchanged.
ScaledPose ScalePoseForZoom(float yaw, float pitch, float roll,
                            float x, float y, float z, float zoom);

/// World-space camera offset in UE units (cm) for the session's processed
/// offset in metres, expressed in the CLEAN camera basis so head sway follows
/// the body rather than the head-rotated view.
cameraunlock::unreal::FVector ComputePositionOffset(
    const cameraunlock::unreal::FQuat4d& cleanQ,
    float offset_x, float offset_y, float offset_z);

}
