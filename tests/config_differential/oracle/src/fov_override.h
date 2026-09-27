// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "ue_abi.h"

// Field of view, read from - and optionally offset in - the scene view the
// frame is about to be rendered with.
//
// ULocalPlayer::GetViewPoint builds ONE FMinimalViewInfo per rendered frame,
// writes ViewInfo.FOV = PlayerCameraManager->GetFOVAngle() into it, and only
// then calls GetPlayerViewPoint(&ViewInfo.Location, &ViewInfo.Rotation). So on
// the render caller the two out-params the hook already receives are interior
// pointers into that FMinimalViewInfo, and the FOV sits kViewInfoFovOffset from
// OutLocation, already resolved. Reading it needs no second hook and no extra
// RVA.
//
// Reading rather than assuming is the point: the game ships its own Field of
// View slider (Options -> Graphics), so a constant would be wrong for any
// player who moved it, and GetFOVAngle() also carries whatever a scripted
// moment has done to the FOV.
//
// Two things downstream consume it. The reticle projection divides by the
// half-field tangents of the view being drawn, and the tracker pose is scaled
// so its displacement on screen is the same whatever the game is doing with its
// field of view: the game widens the view while the player runs, and a wider
// view moves the picture less for the same head angle, which without the
// correction reads as head tracking going soft the moment they start running.
//
// The reference that scaling is measured against is the player's own Options ->
// Graphics field of view, read live off the player character and adopted on a
// frame where the two agree - see UpdateZoomFactor for why agreement is the
// condition. That is what the game renders at when nothing is zooming, so the
// factor is exactly 1.0 in ordinary play whatever the player set, which is also
// the check that the two numbers are in the same units. Both are horizontal
// degrees: the rendered one is FMinimalViewInfo::FOV, measured horizontal (a 25
// degree head yaw moves the 1280-wide frame 292px, which is a 90 degree
// horizontal field), and the setting is the number that ends up in it. No
// aspect conversion enters the ratio.

namespace ecr_ht::fov {

// UE4 divides by tan(FOV/2), so a frame is never rendered at 0 or 180. A value
// outside this says the read did not land on FMinimalViewInfo::FOV at all.
inline constexpr float kMinPlausible = 1.0f;
inline constexpr float kMaxPlausible = 179.0f;

// Bounds on the OFFSET result only, so FovOffset=0 leaves the game's own value
// untouched whatever it is.
inline constexpr float kMinApplied = 40.0f;
inline constexpr float kMaxApplied = 150.0f;

/// Whether a value read out of the view info can be a field of view at all.
bool IsPlausible(float fov);

/// The field of view to render at. Pure. An offset of exactly 0 returns the
/// game's own value untouched - it is not pulled into [kMinApplied, kMaxApplied].
float ApplyOffset(float game_fov, float offset);

/// The factor a tracked angle or lean scales by so its displacement on screen at
/// `rendered_fov` is what it would have been at `base_fov`. Pure. Both fields
/// must be in the same axis - here both are horizontal - and plausible;
/// anything else returns 1.0, which is "do not scale".
float ZoomFactorBetween(float rendered_fov, float base_fov);

/// Read the render caller's field of view, apply `offset`, write it back when
/// the offset is non-zero, and work out the zoom factor against the player's
/// un-zoomed setting. Call only for the render caller: the guards
/// below assume the two out-params are the Location and Rotation members of one
/// FMinimalViewInfo.
///
/// Either guard failing disables FOV handling for the rest of the session with
/// a log line, and leaves the game's own value untouched. Until a reference has
/// been adopted the zoom factor stays 1.0, so the pose is passed through
/// unscaled rather than scaled against a guess.
void Apply(std::uintptr_t controller, FVector3f* out_location,
           const FRotator3f* out_rotation, float offset);

/// Last values seen by Apply, for the hook's heartbeat and basis lines. Zero
/// until the render caller has been observed.
float GameFov();
float RenderedFov();
/// The un-zoomed reference, with the same FovOffset applied to it as the
/// rendered value. Zero when it has not been read.
float BaseFov();
/// What the pose is scaled by this frame. Exactly 1.0 in ordinary play.
float ZoomFactor();

}
