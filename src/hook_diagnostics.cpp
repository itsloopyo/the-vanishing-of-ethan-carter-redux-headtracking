// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hook_diagnostics.h"

#include <atomic>
#include <cmath>
#include <cstdio>

#include "fov_override.h"
#include "hud_reticle.h"
#include "log_throttle.h"
#include "logging.h"

namespace ecr_ht::diag {

namespace {

// Pose-detail cadence. A developer build streams it, because re-deriving the
// caller gate or an axis sign means reading these lines while driving the game.
#if ECR_DEV_HOTKEYS
constexpr int kPoseDetailLines = BoundedLogThrottle::kUnbounded;
// Fast enough to steer by. The line carries the clean view rotation and the aim
// ray's range, which together are a compass and a rangefinder, and a developer
// build is where the mod is driven around to check them.
constexpr std::uint64_t kPoseDetailIntervalMs = 250;
#else
constexpr int kPoseDetailLines = 20;
constexpr std::uint64_t kPoseDetailIntervalMs = 2000;
#endif

// Heartbeat cadence. The state is SAMPLED every 30s but only written when it
// changed, or every 5 minutes as a liveness keepalive. A fixed 30s line is 600
// of them across a five-hour playthrough, nearly all identical, and they bury
// the startup chain - which is the part the crash handler asks the player to
// send.
constexpr std::uint64_t kHeartbeatSampleIntervalMs = 30000;
constexpr std::uint64_t kHeartbeatKeepaliveMs = 300000;

// Slow enough that an unchanging lean state costs a line an hour, which is all
// it takes to tell a sweep that is running from one that is not.
constexpr std::uint64_t kLeanSampleIntervalMs = 60000;

// Port state alongside the data flag: "udpData=NO" on its own cannot tell a
// stalled tracker from a port another game is still holding, and only the
// second one resolves itself.
enum class UdpPortState { None = 0, WaitingForPort, Listening, Down };

UdpPortState PortState(const cameraunlock::UdpReceiver* receiver) {
    if (!receiver) return UdpPortState::None;
    if (receiver->IsRetrying()) return UdpPortState::WaitingForPort;
    return receiver->IsRunning() ? UdpPortState::Listening : UdpPortState::Down;
}

const char* PortStateName(UdpPortState state) {
    switch (state) {
        case UdpPortState::None:           return "none";
        case UdpPortState::WaitingForPort: return "waiting-for-port";
        case UdpPortState::Listening:      return "listening";
        case UdpPortState::Down:           return "down";
    }
    return "unknown";
}

const char* FormatAspect(float width, float height) {
    static char s_text[48];
    std::snprintf(s_text, sizeof(s_text), "%.0fx%.0f, %.4f", width, height, width / height);
    return s_text;
}

enum class LeanState { Clear = 0, InContact, QueryFailed };

const char* LeanStateText(LeanState state) {
    switch (state) {
        case LeanState::QueryFailed:
            return "the collision sweep could not run - the lean is passing through UNCLAMPED";
        case LeanState::InContact:
            return "held off a surface";
        case LeanState::Clear:
            return "clear, full lean available";
    }
    return "unknown";
}

}  // namespace

void LogFovBasis(float fov_offset) {
    // Read before the exchange: this runs on every render-caller frame for the
    // life of the process, and once the line is out the plain load is all it
    // costs, rather than a locked read-modify-write per frame forever.
    static std::atomic<bool> s_written{false};
    if (s_written.load(std::memory_order_relaxed)) return;
    // Waits for a reference rather than for a pose: behind the main menu there
    // is nothing to measure against yet. The first frame the player's own camera
    // renders still needs neither a tracker nor a head movement.
    if (fov::BaseFov() <= 0.0f) return;
    if (s_written.exchange(true, std::memory_order_relaxed)) return;

    float width = 0.0f, height = 0.0f;
    const bool haveViewport = reticle::ViewportSize(width, height);
    Log::Line("fov basis: rendering at %.2f deg horizontal (game %.2f + FovOffset %+.1f), "
              "un-zoomed reference %.2f deg horizontal (the player's own Options -> Graphics "
              "field of view plus the same offset), pose scaled by %.4f. Both angles are "
              "horizontal degrees of the same engine quantity, so the viewport aspect (%s) "
              "converts the vertical half-field for the reticle and never enters this ratio.",
              fov::RenderedFov(), fov::GameFov(), fov_offset, fov::BaseFov(),
              fov::ZoomFactor(),
              haveViewport ? FormatAspect(width, height) : "viewport not readable");
}

void LogHeartbeat(const cameraunlock::UdpReceiver* receiver, const HeartbeatSnapshot& state) {
    static LogThrottle s_sample(kHeartbeatSampleIntervalMs);
    static LogThrottle s_keepalive(kHeartbeatKeepaliveMs);
    // No state can encode to all-ones, so the first sample always writes.
    static std::atomic<std::uint32_t> s_lastState{0xffffffffu};

    if (!s_sample.Due()) return;

    float yaw = 0, pitch = 0, roll = 0;
    const bool data = receiver && receiver->GetRotation(yaw, pitch, roll);
    const UdpPortState port = PortState(receiver);

    // Only the categorical fields. The pose, the FOV and the call counter move
    // every frame, so including them would make every sample a "change" and put
    // the fixed cadence straight back.
    const std::uint32_t encoded =
        (state.tracking_enabled ? 1u : 0u) |
        (state.in_gameplay      ? 2u : 0u) |
        (data                   ? 4u : 0u) |
        (state.world_space_yaw  ? 8u : 0u) |
        (static_cast<std::uint32_t>(port) << 4) |
        (static_cast<std::uint32_t>(state.inject_mode) << 8);

    const bool changed = encoded != s_lastState.exchange(encoded, std::memory_order_relaxed);
    if (!changed && !s_keepalive.Due()) return;
    // A change-driven line restarts the keepalive too, so it never lands right
    // behind one.
    s_keepalive.Mark();

    Log::Line("heartbeat hook=%llu retRVA=0x%08llx enabled=%s gameplay=%s udpPort=%s udpData=%s raw=(Y=%.2f P=%.2f R=%.2f) yawMode=%s injectMode=%d fov=%.1f/%.1f hudDraw=%s reticleMoved=%llu",
        static_cast<unsigned long long>(state.hook_calls),
        static_cast<unsigned long long>(state.caller_rva),
        state.tracking_enabled ? "ON" : "OFF",
        state.in_gameplay ? "YES" : "NO",
        PortStateName(port),
        data ? "YES" : "NO", yaw, pitch, roll,
        state.world_space_yaw ? "world" : "local", state.inject_mode,
        fov::GameFov(), fov::RenderedFov(),
        reticle::SawDraw() ? "YES" : "NO", reticle::MovedCount());
}

bool PoseDetailDue() {
    static BoundedLogThrottle s_throttle(kPoseDetailIntervalMs, kPoseDetailLines);
    return s_throttle.Due();
}

void LogPoseDetail(const PoseSnapshot& pose) {
    const auto& offset = pose.position_offset;
    Log::Line("hook #%llu retRVA=0x%08llx rot_clean=(Y=%.2f P=%.2f) applied=(Y=%.2f P=%.2f R=%.2f) result=(Y=%.2f P=%.2f R=%.2f) posOff=(%.1f,%.1f,%.1f) lean=%.1f zoom=%.4f aimDist=%.0f ndc=(%.3f,%.3f)%s%s",
        static_cast<unsigned long long>(pose.hook_calls),
        static_cast<unsigned long long>(pose.caller_rva),
        pose.clean.Yaw, pose.clean.Pitch,
        pose.applied_yaw, pose.applied_pitch, pose.applied_roll,
        pose.result.Yaw, pose.result.Pitch, pose.result.Roll,
        offset.X, offset.Y, offset.Z,
        std::sqrt(offset.X * offset.X + offset.Y * offset.Y + offset.Z * offset.Z),
        pose.zoom, pose.aim_distance, pose.aim.x, pose.aim.y,
        pose.aim.valid ? "" : " (aim invalid)",
        pose.lean_in_contact ? " lean-clamped"
                             : (pose.lean_query_failed ? " lean-query-FAILED" : ""));
}

void LogLeanState(bool in_contact, bool query_failed) {
    static std::atomic<int> s_last{-1};
    static LogThrottle s_sample(kLeanSampleIntervalMs);

    const LeanState state = query_failed ? LeanState::QueryFailed
                          : (in_contact  ? LeanState::InContact : LeanState::Clear);
    const int encoded = static_cast<int>(state);
    const bool changed = s_last.exchange(encoded, std::memory_order_relaxed) != encoded;
    if (!changed && !s_sample.Due()) return;
    // A change-driven line restarts the periodic sample too, the way the
    // heartbeat's keepalive is restarted, so the next sample cannot land right
    // behind it repeating the state that line just reported.
    s_sample.Mark();
    Log::Line("lean: %s", LeanStateText(state));
}

}  // namespace ecr_ht::diag
