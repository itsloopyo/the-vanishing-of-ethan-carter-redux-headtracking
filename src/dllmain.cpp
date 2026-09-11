// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        ecr_ht::Initialize(module);
    }
    // There is deliberately no DLL_PROCESS_DETACH work. Initialize pins the
    // module, so the only detach left is process exit, where the kernel has
    // already killed every other thread without unwinding and the OS reclaims
    // the sockets, threads and detours anyway.
    //
    // The teardown this used to run on an explicit FreeLibrary could not be made
    // safe: it ran inside DllMain, holding the loader lock, and both the hotkey
    // poller and the UDP receiver join their own threads. An exiting thread
    // still needs the loader lock to run the DLL_THREAD_DETACH notifications of
    // every OTHER loaded module - DisableThreadLibraryCalls above only opts this
    // one out - so the join and the loader wait on each other and the game hangs
    // with no log line. Pinning removes the path rather than papering over it.
    return TRUE;
}
