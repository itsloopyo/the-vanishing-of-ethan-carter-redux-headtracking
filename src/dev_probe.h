// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// Developer-only probe for measuring a bitfield UPROPERTY's byte offset and
// mask on the live APlayerController.
//
// UE4 registers a bool UPROPERTY by probing a live object, so a bitfield's
// offset and mask are never immediates in the binary the way a pointer member's
// offset is. They have to be measured instead: snapshot the controller in
// gameplay, snapshot it again with the state of interest showing, and diff.
// kShowMouseCursorOffset / Mask in the build profile are that measurement.
//
// Compiled out entirely unless ECR_DEV_HOTKEYS is on. CMake defines the macro
// for the mod target either way; the fallback covers any other consumer.

#ifndef ECR_DEV_HOTKEYS
#define ECR_DEV_HOTKEYS 0
#endif

#if ECR_DEV_HOTKEYS

#include <cstdint>

namespace ecr_ht::dev {

/// Capture the controller's bytes as the comparison baseline (Ctrl+Shift+B).
void RequestBaseline();

/// Diff the controller's bytes against the baseline (Ctrl+Shift+N).
void RequestDiff();

/// Service a pending baseline/diff request. Called from the hook, where a live
/// controller pointer is in hand.
void ServiceRequest(std::uintptr_t controller);

/// Ask for a scan of every float in the pawn that could be a field of view
/// (Ctrl+Shift+F). Pressing it while walking and again while running is how the
/// pawn's un-zoomed FOV was told apart from the value the game animates: the
/// reference holds still, the animated one moves.
void RequestFovScan();

}

#endif  // ECR_DEV_HOTKEYS
