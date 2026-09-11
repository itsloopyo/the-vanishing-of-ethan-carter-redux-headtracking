// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <windows.h>

namespace ecr_ht {

// The single entry point, called from DllMain. Initialize pins the module and
// spins up a bootstrap thread, so the heavy work (config, fingerprinting, UDP,
// MinHook) never runs under the loader lock and the module cannot be unmapped
// out from under the threads it starts.
void Initialize(HMODULE self);

}
