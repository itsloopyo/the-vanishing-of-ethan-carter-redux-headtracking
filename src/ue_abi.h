// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <type_traits>

// The UE4 (not UE5) ABI at the GetPlayerViewPoint boundary.
//
// Large World Coordinates do not exist before UE5, so this engine's FVector and
// FRotator are 3-FLOAT PODs (12 bytes each). These are the exact types
// APlayerController::GetPlayerViewPoint(self, &OutLocation, &OutRotation)
// writes through. Declaring them as doubles (24B) would overflow the engine's
// stack out-params - the UE5 LWC trap in reverse.
//
// The names carry the COMPONENT COUNT, matching UE's own FVector3f. An earlier
// FVector4f here read as UE5's four-component float vector, which is 16 bytes,
// in the one file whose whole subject is getting the width right.
//
// Quaternion math stays in core's double types; conversion happens only here,
// at this boundary.

namespace ecr_ht {

struct FVector3f  { float X, Y, Z; };
struct FRotator3f { float Pitch, Yaw, Roll; };

// The 12 bytes are the contract, not an implementation detail: these are
// written through pointers into the engine's own stack frame, and a fourth
// member would have the hook overrun it and the FHitResult reads in
// engine_trace.cpp - which size their bounds check with sizeof - silently widen
// into the neighbouring member.
static_assert(sizeof(FVector3f) == 12, "GetPlayerViewPoint's out-param is a 3-float FVector");
static_assert(sizeof(FRotator3f) == 12, "GetPlayerViewPoint's out-param is a 3-float FRotator");
static_assert(alignof(FVector3f) == 4 && alignof(FRotator3f) == 4, "UE4 packs these to 4");
static_assert(std::is_trivially_copyable_v<FVector3f> &&
              std::is_trivially_copyable_v<FRotator3f>,
              "both are memcpy'd out of the engine's own buffers");

using GetPlayerViewPoint_t =
    void(__fastcall*)(void* self, FVector3f* outLocation, FRotator3f* outRotation);

}
