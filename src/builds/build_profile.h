// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include <cameraunlock/memory/pe_fingerprint.h>

// One BuildProfile describes a single shipped build of The Vanishing of Ethan
// Carter Redux: the PE-header fingerprint that uniquely identifies it, plus
// every per-build RVA / field offset the camera hook needs. The registry holds
// one profile per supported build; at startup the mod fingerprints the live
// module and selects the matching profile. No match leaves the mod fully
// dormant (no hooks installed, game runs vanilla) - see AGENTS.md "Maintain
// compatibility across new patches": never edit an existing profile's RVAs in
// place, ADD a new one.
//
// Ethan Carter Redux is UE 4.x (2015 build). UE4 predates Large World
// Coordinates, so FVector / FRotator are 3-float structs (12 bytes each), NOT
// the 3-double FVector3d / FRotator3d of UE5. The hook reads/writes the
// GetPlayerViewPoint out-params as floats - see ue_abi.h.
//
// This is a narrative walking-sim: no weapons, no crosshair, no helmet overlay,
// so the mod only needs to inject the head pose into the render-path view and
// leave every other GetPlayerViewPoint caller clean (the aim/interaction-trace
// decoupling).

namespace ecr_ht {

// PE-header build fingerprint (TimeDateStamp + SizeOfImage + CheckSum);
// the shared type keeps reading/matching/classification in core.
using PeFingerprint = ::cameraunlock::memory::PeFingerprint;

// How many distinct GetPlayerViewPoint call sites a profile can name. The
// inject-mode range in caller_gate.h is derived from this, so widening the
// table does not need a second constant kept in step by hand.
inline constexpr std::size_t kMaxKnownCallers = 16;
using CallerRvaTable = std::array<std::uintptr_t, kMaxKnownCallers>;

struct OffsetTable {
    // Hook target: APlayerController::GetPlayerViewPoint. RVA from the
    // module base. Zero = profile incomplete (mod stays dormant).
    std::uintptr_t kGetPlayerViewPointRva;

    // Return-address RVAs of the distinct GetPlayerViewPoint call sites.
    // Head tracking is injected ONLY for callers flagged here per the
    // active inject mode; every other caller reads the clean (mouse/pad)
    // rotation. That per-caller gate IS the look/aim decoupling.
    // 0-valued trailing entries are unused padding.
    CallerRvaTable kKnownCallerRvas;

    // Default inject mode at startup. See caller_gate.h for the encoding:
    // 0 = all callers (diagnostic only), 1..kMaxKnownCallers = inject only
    // for kKnownCallerRvas[mode-1] (the render-path caller /
    // FMinimalViewInfo builder), and one past that = none. A -DECR_DEV_HOTKEYS=ON
    // build adds Ctrl+Shift+U / J to cycle this live, so the render caller
    // can be re-confirmed in game after a patch without a rebuild.
    int kDefaultInjectMode;

    // Byte offset of FMinimalViewInfo::FOV within the struct, i.e. relative
    // to the OutLocation pointer the render caller hands to
    // GetPlayerViewPoint (UE4 lays Location at +0x0 and Rotation at +0xc, so
    // the same pointer addresses the whole view info). Zero = the layout is
    // not known for this build, and the mod neither reads nor writes FOV.
    std::size_t kViewInfoFovOffset;

    // AController::Character, and the player character's own un-zoomed
    // field of view within it - the number the Options -> Graphics slider
    // writes, which the game then animates away from and back to. It is the
    // reference the rendered FOV above is measured against when the pose is
    // scaled for zoom. Character rather than Pawn because the offset below
    // is a layout of the player character class, and Character is null when
    // the possessed pawn is not one. Either being zero means the base
    // cannot be read on this build, and the pose is then passed through
    // unscaled rather than scaled against a guess.
    std::size_t kPlayerCharacterOffset;
    std::size_t kCharacterUnzoomedFovOffset;

    // APlayerController::bShowMouseCursor bitfield, for the InGameplay
    // gate (cursor visible == menu/cutscene -> suppress tracking). Both
    // zero = gate disabled (always treat as gameplay).
    std::size_t   kShowMouseCursorOffset;
    std::uint32_t kShowMouseCursorMask;

    // UKismetSystemLibrary::LineTraceSingle and ::SphereTraceSingle, the
    // engine's own Blueprint world queries. Calling these rather than
    // UWorld::LineTraceSingleByChannel keeps FCollisionQueryParams - a
    // non-trivial struct with a TArray in it - out of this mod entirely:
    // every parameter is a scalar, a pointer, or a TArray we build.
    //
    // Shipping compiles out the debug colour and duration arguments the
    // UE4 headers carry, so the signatures are the exec thunks':
    //
    //   bool LineTraceSingle(UObject* WorldContext, const FVector* Start,
    //       const FVector* End, int TraceChannel, bool bTraceComplex,
    //       const TArray<AActor*>* ActorsToIgnore, int DrawDebugType,
    //       FHitResult* OutHit, bool bIgnoreSelf);
    //   bool SphereTraceSingle(...same, with float Radius after End);
    //
    // Zero leaves the matching feature off however it is configured: the
    // aim marker for the line trace, the lean collision clamp for the sweep.
    std::uintptr_t kLineTraceSingleRva;
    std::uintptr_t kSphereTraceSingleRva;

    // FHitResult, as the traces fill it. Size is what the exec thunk
    // memsets before the call, and it MUST be right: the engine writes the
    // whole struct, so a buffer sized from a different UE version overflows
    // our own frame.
    std::size_t   kHitResultSize;
    // Bit 0 of this byte is bBlockingHit.
    std::size_t   kHitResultBlockingHitOffset;
    // FHitResult::Location. For a SWEEP this is the centre of the sphere
    // where it stopped, so it is already backed off the surface by the
    // radius; for a line trace it is the contact itself.
    std::size_t   kHitResultLocationOffset;

    // AHUD::DrawTexture, the static behind the HUD Blueprint's "Draw
    // Texture" node. The game's crosshair is drawn through it, so this is
    // where the stock reticle gets moved onto the clean aim point rather
    // than a second one being drawn beside it. Zero = no reticle
    // compensation on this build.
    std::uintptr_t kHudDrawTextureRva;
    // AHUD::DrawTextureSimple, the one-line sibling of the above. Hooked
    // alongside it because which node a HUD Blueprint used is not visible
    // from outside, and the two are the only texture paths AHUD exposes.
    std::uintptr_t kHudDrawTextureSimpleRva;
};

struct BuildProfile {
    const char*   Name;
    PeFingerprint Fingerprint;
    OffsetTable   Offsets;
};

}  // namespace ecr_ht
