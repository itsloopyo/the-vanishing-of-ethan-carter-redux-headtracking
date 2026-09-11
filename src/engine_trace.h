// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/camera/lean_clamp.h>
#include <cameraunlock/unreal/ue_math.h>

// The two world queries this mod asks UE4 for, and nothing else.
//
//   - a LINE trace along the clean aim, so the reticle can be drawn on the
//     surface the player is actually pointing at rather than at a guessed depth;
//   - a SPHERE sweep along the lean, so a 6DOF offset cannot put the eye inside
//     the level. Core owns what to do with that answer
//     (cameraunlock/camera/lean_clamp.h); this owns getting one.
//
// Both go through UKismetSystemLibrary rather than UWorld, so there is no
// FCollisionQueryParams ABI to reconstruct and no second physics query in this
// mod to disagree with the game's: every argument is a scalar, a pointer, or a
// TArray we build.
//
// Both run on the Visibility channel by default. That is not a convenience: UE4
// ships the "Pawn" collision profile with Visibility set to Ignore, so a trace
// starting at the eye - which sits inside the player's own capsule - passes out
// of the character without needing the Pawn pointer in an ignore list.

namespace ecr_ht::trace {

namespace ue = ::cameraunlock::unreal;

/// Result of the aim line trace. `queried` false means the trace could not run
/// at all, which is different from a clear path and is reported rather than
/// absorbed - a reticle drawn from a failed trace is a reticle pointing nowhere.
struct AimHit {
    bool queried = false;
    bool blocked = false;
    ue::FVector point{0.0, 0.0, 0.0};
    float distance = 0.0f;
};

/// ETraceTypeQuery index the queries run on. Which channel a level's geometry
/// blocks is a project setting, not an engine constant, so both are
/// configurable and the only way to know is to run them and read the log.
void SetAimChannel(int trace_type_query);
void SetLeanChannel(int trace_type_query);

/// Radius of the swept sphere, in UE units (cm). This IS the standoff from the
/// surface - the sweep reports where the sphere's CENTRE stopped - so it must
/// exceed the camera's near clip distance, or geometry is culled before the eye
/// reaches it and the wall goes transparent anyway.
void SetLeanRadius(float centimetres);

/// Resolves both statics against the active build profile. False, having logged
/// which part was missing, when the profile does not carry them; callers then
/// run with no aim point and no clamp rather than with broken ones. Safe to call
/// repeatedly.
bool Ready();

/// Line trace from `start` along unit `direction` for `max_distance` UE units.
/// `context` is the APlayerController the hook was called on: it is a UObject in
/// the level, which is all WorldContextObject has to be. Game thread only.
AimHit TraceAim(void* context, const ue::FVector& start, const ue::FVector& direction,
                float max_distance);

/// The lean sweep, in the shape core's clamp takes. Game thread only.
cameraunlock::camera::LeanObstruction QueryLean(void* context,
                                                const cameraunlock::math::Vec3& start,
                                                const cameraunlock::math::Vec3& direction,
                                                float max_distance);

}  // namespace ecr_ht::trace
