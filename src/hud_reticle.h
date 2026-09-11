// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// Moves the game's OWN crosshair onto the clean aim point.
//
// Ethan Carter Redux draws its crosshair from a HUD Blueprint over the native
// EthanCarterHUD - its description in the package reads "This HUD Blueprint
// draws a crosshair on the screen" - so the reticle reaches the frame through
// AHUD::DrawTexture or AHUD::DrawTextureSimple. Detouring those and shifting the
// quad is what moves the stock crosshair rather than drawing a second one beside
// it, which is the whole reason this mod ships no overlay of its own.
//
// Only a SMALL quad near the centre of the frame is moved. The HUD draws
// full-screen quads too (the vignette), and shifting one of those would drag the
// post-process across the screen; the crosshair is the one thing on the HUD that
// is both small and centred, and the size test is what separates them without
// needing UObject reflection to read the texture's name.
//
// The offset arrives from the camera hook, which is the only place that knows
// both cameras. Nothing here re-derives it.

namespace ecr_ht::reticle {

/// Detours the HUD texture draws named by the active build profile. False, with
/// a reason in the log, when the profile carries neither, or MinHook refuses.
bool Install();

/// What the camera hook knows about the aim on the frame about to be drawn.
/// Three states rather than two, because "the mod is not moving the camera" and
/// "the mod moved the camera and cannot say where the aim went" want opposite
/// treatment: the first leaves the crosshair alone, because a clean frame's
/// centre IS the aim; the second must not, because it is not.
enum class AimState {
    NotTracking,  ///< No head pose this frame. Leave the crosshair where it is.
    Known,        ///< Offset published; move the crosshair onto it.
    Unknown,      ///< Camera moved, aim unmeasurable. Take the crosshair away.
};

/// The aim in the frame about to be drawn, x right and y up in -1..1. The
/// state is the caller's per-frame decision and is never latched, so a stale
/// mark cannot outlive the frame that produced it.
void Publish(AimState state, float ndc_x, float ndc_y);

/// Whether a texture draw has been seen at all. The crosshair only appears when
/// the game decides to show it, so this separates "the hook never fired" from
/// "the hook fired and the offset was zero" in the log.
bool SawDraw();

/// Number of texture draws that were actually moved.
unsigned long long MovedCount();

/// The game window's client area in pixels. The canvas the HUD draws on is the
/// swap chain, so this is the same rectangle the reticle is positioned in and
/// the same one the aim projection has to divide by. False before the engine has
/// created its window.
bool ViewportSize(float& width, float& height);

}  // namespace ecr_ht::reticle
