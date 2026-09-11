// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "builds/build_profile.h"

// The per-caller inject gate - the mechanism the whole mod's look/aim
// decoupling rests on.
//
// GetPlayerViewPoint fires from several call sites per frame. Only the
// render-path caller (the FMinimalViewInfo builder) gets the head pose written
// back; interaction line-traces, the audio listener and the streaming/LOD query
// all read the clean mouse/pad rotation. Which callers are injected is chosen
// by the inject mode.

namespace ecr_ht::inject {

/// Inject for every caller. Entangles aim with view - diagnostic use only, and
/// the mode in which caller RVAs are recorded so the render caller can be
/// re-identified after a game patch.
inline constexpr int kModeAll = 0;

/// Modes kModeAll+1 .. kModeLastCaller select a single entry of
/// OffsetTable::kKnownCallerRvas (mode - 1).
inline constexpr int kModeFirstCaller = 1;
inline constexpr int kModeLastCaller  = static_cast<int>(kMaxKnownCallers);

/// Inject for no caller at all: tracking is off at the hook.
inline constexpr int kModeNone = kModeLastCaller + 1;

/// Total distinct modes, i.e. the modulus the dev cycle hotkeys wrap on.
inline constexpr int kModeCount = kModeNone + 1;

/// Whether the head pose should be written back for the call site that returned
/// to `caller_rva`. Pure; `callers` is the active profile's table.
bool ShouldInject(const CallerRvaTable& callers, std::uintptr_t caller_rva, int mode);

/// Record one call site for the kModeAll caller-distribution report, and emit
/// the periodic summary when enough calls have accumulated. `total_calls` is the
/// running hook call count. Only meaningful in kModeAll; that report is how the
/// render caller gets (re-)confirmed after a patch.
void RecordCaller(std::uintptr_t caller_rva, std::uint64_t total_calls);

}
