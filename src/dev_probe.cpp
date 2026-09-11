// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "dev_probe.h"

#if ECR_DEV_HOTKEYS

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <cameraunlock/unreal/ue_runtime.h>

#include "builds/build_registry.h"
#include "logging.h"

namespace ecr_ht::dev {

namespace {

// Enough of the controller to cover the engine's own UPROPERTY block without
// running off the end of a smaller derived object.
constexpr std::size_t kDumpBytes = 0x800;

enum class Request { None = 0, Baseline = 1, Diff = 2, FovScan = 3 };

// Past the native ACharacter block, far enough into the Blueprint
// variables to reach the field of view the player set.
constexpr std::size_t kFovScanBytes = 0x1400;

std::mutex g_mutex;
std::uint8_t g_baseline[kDumpBytes];
bool g_baselineValid = false;
std::atomic<Request> g_request{Request::None};

bool ReadControllerBytes(std::uintptr_t controller, std::uint8_t* out) {
    for (std::size_t i = 0; i < kDumpBytes; i += sizeof(std::uint32_t)) {
        std::uint32_t word = 0;
        if (!cameraunlock::unreal::SafeReadU32(controller + i, word)) return false;
        std::memcpy(out + i, &word, sizeof(word));
    }
    return true;
}

void ReportDiffLocked(const std::uint8_t* now) {
    Log::Line("dev: controller diff vs baseline:");
    int changed = 0;
    for (std::size_t i = 0; i < kDumpBytes; ++i) {
        if (g_baseline[i] == now[i]) continue;
        ++changed;
        Log::Line("  +0x%03zx  %02x -> %02x  (xor %02x)",
            i, g_baseline[i], now[i], g_baseline[i] ^ now[i]);
    }
    Log::Line("dev: %d byte(s) changed", changed);
}

void ScanRange(const char* what, std::uintptr_t base, std::size_t bytes) {
    if (base == 0) {
        Log::Line("dev: no %s to scan", what);
        return;
    }
    char line[512];
    int used = std::snprintf(line, sizeof(line), "dev: fov candidates in %s @0x%llx:", what,
                             static_cast<unsigned long long>(base));
    for (std::size_t i = 0; i + sizeof(float) <= bytes; i += sizeof(float)) {
        float value = 0.0f;
        if (!cameraunlock::unreal::SafeReadFloat(base + i, value)) continue;
        if (!(value >= 40.0f && value <= 160.0f)) continue;
        // Formatted apart from the line so a candidate that does not fit is
        // carried onto the next one. Appending straight into `line` truncated
        // it in place, flushed the truncation, and then dropped the candidate.
        char entry[32];
        const int n = std::snprintf(entry, sizeof(entry), " +0x%03zx=%.3f", i, value);
        if (n <= 0 || n >= static_cast<int>(sizeof(entry))) continue;
        if (used + n >= static_cast<int>(sizeof(line))) {
            Log::Line("%s", line);
            used = std::snprintf(line, sizeof(line), "dev: fov candidates in %s (cont):", what);
        }
        std::memcpy(line + used, entry, static_cast<std::size_t>(n) + 1);
        used += n;
    }
    Log::Line("%s", line);
}

void ScanFovFields(std::uintptr_t controller) {
    const OffsetTable& offsets = Offsets();
    std::uintptr_t character = 0;
    if (offsets.kPlayerCharacterOffset != 0)
        cameraunlock::unreal::SafeReadPtr(controller + offsets.kPlayerCharacterOffset, character);
    ScanRange("player character", character, kFovScanBytes);
}

}

void RequestBaseline() { g_request.store(Request::Baseline, std::memory_order_relaxed); }
void RequestDiff()     { g_request.store(Request::Diff,     std::memory_order_relaxed); }
void RequestFovScan()  { g_request.store(Request::FovScan,  std::memory_order_relaxed); }

void ServiceRequest(std::uintptr_t controller) {
    const Request request = g_request.exchange(Request::None, std::memory_order_relaxed);
    if (request == Request::None) return;

    if (request == Request::FovScan) {
        ScanFovFields(controller);
        return;
    }

    std::uint8_t now[kDumpBytes];
    if (!ReadControllerBytes(controller, now)) {
        Log::Line("dev: controller snapshot failed - object not fully readable");
        return;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (request == Request::Baseline) {
        std::memcpy(g_baseline, now, sizeof(now));
        g_baselineValid = true;
        Log::Line("dev: controller baseline captured at 0x%llx",
            static_cast<unsigned long long>(controller));
        return;
    }
    if (!g_baselineValid) {
        Log::Line("dev: no baseline captured - press Ctrl+Shift+B first");
        return;
    }
    ReportDiffLocked(now);
}

}

#endif  // ECR_DEV_HOTKEYS
