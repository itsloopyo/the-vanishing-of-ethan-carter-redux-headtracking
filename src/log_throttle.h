// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <atomic>
#include <cstdint>

#include <windows.h>

// Cadence gate for a log line written from the camera hook, which runs several
// times per rendered frame and is entered from more than one engine thread.
//
// The first call is always due. GetTickCount64() counts milliseconds since the
// machine booted, so a game launched shortly after boot has a tick smaller than
// the interval, and a gate that only compared elapsed time would swallow the
// first line - the one that says the hook is alive at all.

namespace ecr_ht {

class LogThrottle {
public:
    explicit constexpr LogThrottle(std::uint64_t interval_ms) : interval_ms_(interval_ms) {}

    /// True on the first call and once per interval after that, restarting the
    /// interval whenever it returns true.
    ///
    /// At most one caller wins each interval. A plain load-test-store let two
    /// engine threads crossing the boundary together both see the old epoch and
    /// both return true, which is how a bounded throttle overruns its ceiling.
    ///
    /// The clock is re-read inside the loop: a losing thread that kept its old
    /// reading would compare it against the winner's NEWER epoch, and now - last
    /// underflows in unsigned arithmetic to a huge number that passes the
    /// interval test - firing a second line and putting the epoch back.
    bool Due() {
        std::uint64_t last = last_.load(std::memory_order_relaxed);
        for (;;) {
            const std::uint64_t now = GetTickCount64();
            // now < last means another thread has already stamped this instant
            // or later, so it has taken the slot.
            if (last != 0 && (now < last || now - last < interval_ms_)) return false;
            const std::uint64_t stamp = now != 0 ? now : 1;
            if (last_.compare_exchange_weak(last, stamp, std::memory_order_relaxed))
                return true;
        }
    }

    /// Restart the interval for a line written for some other reason, so a
    /// keepalive does not follow one line with another.
    void Mark() { Store(GetTickCount64()); }

private:
    // Zero is the never-fired sentinel, so it must never be stored as a time.
    void Store(std::uint64_t now) {
        last_.store(now != 0 ? now : 1, std::memory_order_relaxed);
    }

    std::atomic<std::uint64_t> last_{0};
    std::uint64_t interval_ms_;
};

// A LogThrottle with a ceiling on how many lines it will ever let through.
//
// The evidence a diagnostic line carries is settled in the first few of them,
// and a fixed cadence past that point buries the startup chain - which is the
// part the crash handler asks the player to send. A developer build passes
// kUnbounded to stream the same line while driving the game.
class BoundedLogThrottle {
public:
    /// A negative bound means no ceiling.
    static constexpr int kUnbounded = -1;

    constexpr BoundedLogThrottle(std::uint64_t interval_ms, int max_lines)
        : throttle_(interval_ms), max_lines_(max_lines) {}

    /// True on the first call and once per interval after that, until
    /// `max_lines` calls have returned true.
    ///
    /// The quota is taken by the increment rather than tested before it, so N
    /// threads cannot each pass a check with one slot left between them. The
    /// cheap read first keeps a long-exhausted throttle off the atomic.
    bool Due() {
        if (Exhausted()) return false;
        if (!throttle_.Due()) return false;
        if (max_lines_ < 0) return true;
        if (written_.fetch_add(1, std::memory_order_relaxed) < max_lines_) return true;
        // Lost the race for the last slot. Put it back so a long-running session
        // cannot wrap the counter.
        written_.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }

private:
    bool Exhausted() const {
        return max_lines_ >= 0 && written_.load(std::memory_order_relaxed) >= max_lines_;
    }

    LogThrottle throttle_;
    std::atomic<int> written_{0};
    int max_lines_;
};

}
