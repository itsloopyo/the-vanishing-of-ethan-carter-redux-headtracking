// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "caller_gate.h"

#include <mutex>
#include <unordered_map>

#include "logging.h"

namespace ecr_ht::inject {

namespace {

// Hook calls between caller-distribution reports. The hook runs several times
// per frame, so this is a few seconds of play.
constexpr std::uint64_t kSummaryEveryCalls = 1800;

std::mutex g_mutex;
std::unordered_map<std::uintptr_t, std::uint64_t> g_counts;
std::uint64_t g_lastSummaryAt = 0;

void DumpSummaryLocked(std::uint64_t total) {
    Log::Line("caller-summary @%llu calls: %zu unique return RVAs:",
        static_cast<unsigned long long>(total), g_counts.size());
    for (const auto& [rva, count] : g_counts)
        Log::Line("  ret RVA 0x%08llx  count=%llu",
            static_cast<unsigned long long>(rva),
            static_cast<unsigned long long>(count));
}

}

bool ShouldInject(const CallerRvaTable& callers, std::uintptr_t caller_rva, int mode) {
    if (mode == kModeAll) return true;
    if (mode < kModeFirstCaller || mode > kModeLastCaller) return false;
    const std::uintptr_t selected = callers[static_cast<std::size_t>(mode - 1)];
    return selected != 0 && caller_rva == selected;
}

void RecordCaller(std::uintptr_t caller_rva, std::uint64_t total_calls) {
    // The summary bookkeeping shares the counters' lock rather than reading
    // g_lastSummaryAt outside it: the hook is entered from more than one engine
    // thread, and an unsynchronised read there is a data race on a plain
    // uint64_t for no gain - this whole path only runs in kModeAll.
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_counts[caller_rva];
    if (total_calls - g_lastSummaryAt < kSummaryEveryCalls) return;
    g_lastSummaryAt = total_calls;
    DumpSummaryLocked(total_calls);
}

}
