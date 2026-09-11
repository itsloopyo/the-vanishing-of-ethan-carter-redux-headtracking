// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Steam Win64 build of The Vanishing of Ethan Carter Redux
// (EthanCarter-Win64-Shipping.exe, UE 4.x, PE build date 2015-09-15). Every
// address below is an RVA from the module base of that exact binary, and the
// PE fingerprint in the profile is what identifies it.
//
// To add support for a new Steam build: do NOT edit kSteamProfile_<date> in
// place. Append a new `extern const BuildProfile kSteamProfile_YYYYMMDD = {...}`
// below, register it at the top of kKnownProfiles in build_registry.cpp, and
// keep older profiles forever (the PE fingerprint routes each user to theirs).

namespace ecr_ht::builds {

extern const BuildProfile kSteamProfile_20150915;

// ---- Steam Win64 build (PE TimeDateStamp 0x55F84E76) ----
const BuildProfile kSteamProfile_20150915 = {
    /* Name        */ "steam-win64-20150915",
    /* Fingerprint */ { 0x55F84E76u, 0x0221E000u, 0x020E42DBu },
    /* Offsets     */ {
        // APlayerController::GetPlayerViewPoint @ RVA 0x00b7c9f0. It
        // null-checks PlayerCameraManager (this[0x75] = +0x3a8) and the
        // camera-cache timestamp (pcm+0x388 > 0), then tail-calls
        // APlayerCameraManager::GetCameraViewPoint (0x00b4f270), which
        // copies POV.Location (pcm+0x390) and POV.Rotation (pcm+0x39c) -
        // both written as floats (UE4, pre-LWC). Otherwise it falls back to
        // the pawn-eyes path. It is reached only through the vtable, with no
        // static call sites, which is why the caller table below is a list of
        // return addresses rather than anything the binary names.
        /* kGetPlayerViewPointRva */ 0x00b7c9f0ULL,
        // The four distinct call sites, and which one is the render path:
        //   [0] 0x00b3705f - FMinimalViewInfo builder (fn 0x00b36fd0): fills
        //       Location/Rotation/FOV for the scene view, called by
        //       GetProjectionData / CalcSceneView. THE render caller.
        //   [1] 0x00abc546 - HUD/canvas view query (fn 0x00abb090).
        //   [2] 0x00b77d42 - view-axes/listener basis, sin/cos (fn 0x00b77b90).
        //   [3] 0x00b3ad46 - streaming/LOD view query, frame-throttled
        //       (fn 0x00b3aaa0).
        // Injecting only [0] keeps interaction traces / streaming / audio on
        // the clean mouse-driven rotation - that per-caller gate IS the
        // look/aim decoupling. The others are listed so a -DECR_DEV_HOTKEYS=ON
        // build can re-confirm the render caller in-game with Ctrl+Shift+U/J,
        // without a rebuild per candidate.
        /* kKnownCallerRvas */ {{
            0x00b3705fULL,  // 1: render (DEFAULT)
            0x00abc546ULL,  // 2: HUD/canvas
            0x00b77d42ULL,  // 3: view-axes/listener
            0x00b3ad46ULL,  // 4: streaming/LOD
            0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
        }},
        // Inject only for the render caller (kKnownCallerRvas[0]).
        /* kDefaultInjectMode     */ 1,
        // FMinimalViewInfo::FOV. The render caller (fn 0x00b36fd0,
        // ULocalPlayer::GetViewPoint) builds ONE FMinimalViewInfo, copies
        // the camera cache into it, writes the camera manager's field of
        // view angle (vtable +0x568) into that struct at +0x18, and only
        // THEN calls GetPlayerViewPoint, passing the struct's own address
        // as OutLocation and that address + 0xc as OutRotation. So the
        // OutLocation pointer our hook receives on that caller IS the
        // FMinimalViewInfo, Location being its first member, and the FOV
        // the frame will be rendered with sits 0x18 bytes into it, already
        // resolved. Reading it there is what makes the number the mod
        // reports the real one: the game has its own Field of View slider
        // (Options -> Graphics), so the value is the player's, not a
        // constant.
        /* kViewInfoFovOffset     */ 0x18,
        // AController::Character @ +0x330, read off AController's own
        // UPROPERTY registration (fn 0x00fa9350): UE4 bakes each property's
        // byte offset as an immediate beside the LEA of its name string,
        // and that function registers, in order, StateName 0x360,
        // ControlRotation 0x348, TransformComponent 0x340, PlayerState
        // 0x338, Character 0x330, Pawn 0x320.
        //
        // The character's un-zoomed field of view @ +0x668 is a Blueprint
        // variable of the player character, so no registration code names
        // it and the offset is a measurement rather than a reading
        // (Ctrl+Shift+F in a developer build lists the character's floats
        // that could be a field of view). Walking and then running
        // separates three neighbours: +0x668 holds 90.000 throughout, +0x670 is the live
        // value the game animates (90.000 -> 96.000 over about a second of
        // running, and back), and +0x674 holds 120.000, the slider's own
        // maximum. Clicking the Options -> Graphics "Field Of View" slider
        // to 115 moved +0x668 to 115.000 and left +0x670 at 90.000 until
        // the setting was applied, which is what identifies +0x668 as the
        // player's setting rather than anything the game is animating.
        //
        // Not APlayerCameraManager::DefaultFOV, which +0x670 is copied
        // into: the game drives DefaultFOV through the whole run animation,
        // so measuring against it gives a ratio of one value against the
        // same value a frame earlier - noise on the interpolation lag, not
        // compensation.
        /* kPlayerCharacterOffset       */ 0x330,
        /* kCharacterUnzoomedFovOffset  */ 0x668,
        // APlayerController::bShowMouseCursor, a measurement rather than a
        // reading: UE4 registers a bool UPROPERTY by probing a live object,
        // so a bitfield's byte offset and mask are never immediates in the
        // registration code. Snapshotting the controller in gameplay and
        // again with the pause menu up (the Ctrl+Shift+B / Ctrl+Shift+N diff
        // in a -DECR_DEV_HOTKEYS=ON build) moves exactly one flag byte:
        // +0x4f8 goes 0x24 -> 0x25. The
        // untouched bits corroborate the identification - 0x24 is bit 2 and
        // bit 5 of UE4's MouseInterface bitfield, i.e. bEnableTouchEvents
        // and bForceFeedbackEnabled at their engine defaults, which puts
        // bShowMouseCursor at bit 0 exactly where PlayerController.h
        // declares it first. The same bit is raised by the pause menu, the
        // options screen and the confirmation dialogs.
        /* kShowMouseCursorOffset */ 0x4f8,
        /* kShowMouseCursorMask   */ 0x1u,
        // UKismetSystemLibrary's Blueprint world queries. Located from the
        // reflected native names, which carry UE4's _NEW suffix -
        // "LineTraceSingle_NEW" @ .rdata 0x01ba12b8 and
        // "SphereTraceSingle_NEW" @ 0x01ba93d8. Nothing points at either by
        // qword, so there is no {name, fn} table to walk: the class's
        // StaticRegisterNatives body loads the name into rdx and the exec
        // thunk into r8 seven bytes earlier. Thunks 0x010c1e10 / 0x010e3500;
        // each is the VM parameter fetches followed by one call to the
        // static below, in the argument order the signatures in
        // build_profile.h record.
        /* kLineTraceSingleRva     */ 0x00b1c590ULL,
        /* kSphereTraceSingleRva   */ 0x00b26f70ULL,
        // Both thunks memset exactly 0x7c bytes of their FHitResult before
        // the call and then write 1.0f at +0x04, which is Time. That is the
        // size the engine writes, so it is the size of our buffer.
        //
        // Location sits at +0x08, NOT the +0x0c a later UE4 puts it at:
        // FHitResult::Distance does not exist in this engine version, so
        // Location follows Time directly. 0x7c is exactly the size that
        // layout adds up to - bitfield 4, Time 4, six FVectors of 12,
        // PenetrationDepth and Item 4 each, four 8-byte handles. Reading
        // +0x0c instead yields (Location.Y, Location.Z, ImpactPoint.X) as a
        // position, which is not a point on the ray at all: it measured 405m
        // away on a 200m trace, and put the reticle off the frame.
        /* kHitResultSize              */ 0x7c,
        /* kHitResultBlockingHitOffset */ 0x00,
        /* kHitResultLocationOffset    */ 0x08,
        // AHUD::DrawTexture / ::DrawTextureSimple, found the same way from
        // "DrawTexture" @ 0x01bbf3a8 and "DrawTextureSimple" @ 0x01bbf6f8
        // (thunks 0x010a4d00 / 0x010a5110). The crosshair belongs to the HUD
        // Blueprint at /Game/Gameplay/UI/HUD, which derives from the game's
        // own native HUD class and describes itself as the one that draws it,
        // and it reaches the screen through one of these two.
        /* kHudDrawTextureRva       */ 0x00ae7450ULL,
        /* kHudDrawTextureSimpleRva */ 0x00ae75e0ULL,
    },
};

}  // namespace ecr_ht::builds
