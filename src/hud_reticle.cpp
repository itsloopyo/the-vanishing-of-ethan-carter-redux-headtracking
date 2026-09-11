// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hud_reticle.h"

#include <atomic>
#include <cmath>
#include <cstdint>

#include <windows.h>

#include "builds/build_registry.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/os/game_window.h"
#include "cameraunlock/unreal/ue_runtime.h"
#include "log_throttle.h"
#include "logging.h"

namespace ecr_ht::reticle {

namespace {

namespace hooks = ::cameraunlock::hooks;

struct FVector2f { float X, Y; };
struct FLinearColorF { float R, G, B, A; };

// The two AHUD statics, with the arguments their exec thunks fetch. A shipping
// build carries no debug colour or duration, so these are the whole signatures.
using DrawTexture_t = void(__fastcall*)(void* self, void* texture,
                                        float screen_x, float screen_y,
                                        float screen_w, float screen_h,
                                        float u, float v, float u_width, float v_height,
                                        const FLinearColorF* tint, int blend_mode,
                                        float scale, bool scale_position,
                                        float rotation, FVector2f rot_pivot);

using DrawTextureSimple_t = void(__fastcall*)(void* self, void* texture,
                                             float screen_x, float screen_y,
                                             float scale, bool scale_position);

DrawTexture_t g_origDrawTexture = nullptr;
DrawTextureSimple_t g_origDrawTextureSimple = nullptr;

std::atomic<int> g_aimState{static_cast<int>(AimState::NotTracking)};
std::atomic<float> g_ndcX{0.0f};
std::atomic<float> g_ndcY{0.0f};
std::atomic<bool> g_sawDraw{false};
std::atomic<unsigned long long> g_moved{0};

// A quad wider than this fraction of the frame is not a reticle. The vignette
// the same HUD draws covers the whole frame; the crosshair is a few dozen
// pixels.
constexpr float kMaxReticleFraction = 0.2f;

// How far from the centre of the frame a quad may start and still be the
// crosshair, as a fraction of the frame. Generous, because the draw position is
// the quad's top-left corner and the texture's own size is unknown here.
constexpr float kCentreFraction = 0.12f;

// Crosshair-move cadence. The heartbeat already carries the running count as
// reticleMoved=, so what this line adds is the evidence the count cannot give:
// the quad the filter matched and the viewport it matched it in. Those are
// settled in the first few lines. A fixed cadence past them writes 3,600
// near-identical lines across a five-hour playthrough - six times the fixed
// heartbeat this mod already rejected for burying the startup chain, which is
// the part the crash handler asks the player to send. A developer build streams
// it, because re-deriving the quad filter means watching it match while driving
// the game.
#if ECR_DEV_HOTKEYS
constexpr int kMoveLogLines = BoundedLogThrottle::kUnbounded;
constexpr std::uint64_t kMoveLogIntervalMs = 1000;
#else
constexpr int kMoveLogLines = 5;
constexpr std::uint64_t kMoveLogIntervalMs = 5000;
#endif

// First call is due, so the move that proves the compensation engaged is always
// written.
bool MoveLogDue() {
    static BoundedLogThrottle s_throttle(kMoveLogIntervalMs, kMoveLogLines);
    return s_throttle.Due();
}

// Shifts a draw that looks like the crosshair onto the published aim. Leaves
// the quad alone when it is something else, or when the mod is not moving the
// camera at all.
//
// `scale` and `scale_position` are the engine's own post-processing of these
// arguments: AHUD builds the tile at (ScreenW, ScreenH) * Scale, and multiplies
// the POSITION by Scale as well when bScalePosition is set. So the quad the
// player sees is not the one this function is handed, and both the size filter
// and the offset have to be expressed in what actually reaches the canvas.
void OffsetIfReticle(float& screen_x, float& screen_y, float screen_w, float screen_h,
                     float scale, bool scale_position) {
    g_sawDraw.store(true, std::memory_order_relaxed);

    const auto state = static_cast<AimState>(g_aimState.load(std::memory_order_relaxed));
    if (state == AimState::NotTracking) return;

    float width = 0.0f, height = 0.0f;
    if (!ViewportSize(width, height)) return;

    // A degenerate scale would divide by zero below, and the engine draws
    // nothing useful with it either.
    if (!std::isfinite(scale) || scale <= 0.0f) return;

    const float drawn_w = screen_w * scale;
    const float drawn_h = screen_h * scale;
    if (drawn_w > width * kMaxReticleFraction || drawn_h > height * kMaxReticleFraction)
        return;

    // Where the engine will actually put the top-left corner.
    const float position_scale = scale_position ? scale : 1.0f;
    const float drawn_x = screen_x * position_scale;
    const float drawn_y = screen_y * position_scale;

    const float centre_x = width * 0.5f;
    const float centre_y = height * 0.5f;
    if (std::fabs(drawn_x - centre_x) > width * kCentreFraction ||
        std::fabs(drawn_y - centre_y) > height * kCentreFraction)
        return;

    // The aim was measured and landed behind the rendered eye. Only that case
    // reaches here: an aim the mod could not measure at all publishes
    // NotTracking above, so a build that cannot read its field of view or run a
    // world query leaves the crosshair alone rather than removing it for the
    // whole session.
    // The crosshair marks where the player's look and interaction ray goes, and
    // the centre of a head-tracked frame is NOT that point, so leaving it there
    // would state a target the mod knows is wrong. Push it off the canvas
    // instead, where the rasteriser clips it away.
    if (state == AimState::Unknown) {
        screen_x -= width * 4.0f / position_scale;
        screen_y -= height * 4.0f / position_scale;
        return;
    }

    // NDC y runs up, screen y runs down. The delta is in drawn pixels, so it is
    // divided back out of the engine's position scaling before being handed on.
    const float dx = g_ndcX.load(std::memory_order_relaxed) * centre_x;
    const float dy = -g_ndcY.load(std::memory_order_relaxed) * centre_y;
    screen_x += dx / position_scale;
    screen_y += dy / position_scale;

    const auto moved = g_moved.fetch_add(1, std::memory_order_relaxed) + 1;
    if (MoveLogDue())
        Log::Line("reticle: moved the game's crosshair to the clean aim - "
                  "ndc=(%.3f,%.3f) quad=%.0fx%.0f at (%.0f,%.0f) scale=%.2f%s in %.0fx%.0f "
                  "(%llu moved)",
                  g_ndcX.load(std::memory_order_relaxed), g_ndcY.load(std::memory_order_relaxed),
                  drawn_w, drawn_h, drawn_x + dx, drawn_y + dy, scale,
                  scale_position ? " (position scaled)" : "", width, height,
                  static_cast<unsigned long long>(moved));
}

void __fastcall DrawTexture_Hook(void* self, void* texture,
                                 float screen_x, float screen_y,
                                 float screen_w, float screen_h,
                                 float u, float v, float u_width, float v_height,
                                 const FLinearColorF* tint, int blend_mode,
                                 float scale, bool scale_position,
                                 float rotation, FVector2f rot_pivot) {
    OffsetIfReticle(screen_x, screen_y, screen_w, screen_h, scale, scale_position);
    g_origDrawTexture(self, texture, screen_x, screen_y, screen_w, screen_h,
                      u, v, u_width, v_height, tint, blend_mode, scale, scale_position,
                      rotation, rot_pivot);
}

void __fastcall DrawTextureSimple_Hook(void* self, void* texture,
                                       float screen_x, float screen_y,
                                       float scale, bool scale_position) {
    // The simple form takes no size: the quad is the texture's own dimensions
    // times `scale`, which is not readable here without UObject reflection. A
    // HUD only reaches for it for a small badge, so the size test is skipped and
    // the centre test carries the identification on its own.
    OffsetIfReticle(screen_x, screen_y, 0.0f, 0.0f, scale, scale_position);
    g_origDrawTextureSimple(self, texture, screen_x, screen_y, scale, scale_position);
}

bool Detour(std::uintptr_t rva, void* detour, void** original, const char* what) {
    if (rva == 0) return false;
    auto& manager = hooks::HookManager::Instance();
    void* target = reinterpret_cast<void*>(cameraunlock::unreal::ModuleBase() + rva);
    if (auto status = manager.CreateHook(target, detour, original);
        status != hooks::HookStatus::Ok) {
        Log::Line("reticle: CreateHook(%s) failed: %s", what, hooks::HookStatusToString(status));
        return false;
    }
    if (auto status = manager.EnableHook(target); status != hooks::HookStatus::Ok) {
        Log::Line("reticle: EnableHook(%s) failed: %s", what, hooks::HookStatusToString(status));
        return false;
    }
    return true;
}

}  // namespace

bool ViewportSize(float& width, float& height) {
    // Runs per HUD quad per frame, so the cached window is tested by the read
    // that has to happen anyway: GetClientRect fails on a handle that is no
    // longer a window, which makes a separate IsWindow a second call buying
    // nothing. The window scan behind FindGameWindow stays on the failure path.
    static std::atomic<HWND> s_window{nullptr};
    HWND window = s_window.load(std::memory_order_relaxed);
    RECT client{};
    if (window == nullptr || !GetClientRect(window, &client)) {
        window = cameraunlock::os::FindGameWindow();
        s_window.store(window, std::memory_order_relaxed);
        if (window == nullptr || !GetClientRect(window, &client)) return false;
    }
    width = static_cast<float>(client.right - client.left);
    height = static_cast<float>(client.bottom - client.top);
    return width > 0.0f && height > 0.0f;
}

bool Install() {
    const OffsetTable& offsets = Offsets();
    // Both paths are hooked because which node the HUD Blueprint used is not
    // visible from outside the package, and detouring the one it did not use
    // costs a function that never fires.
    const bool full = Detour(offsets.kHudDrawTextureRva,
                             reinterpret_cast<void*>(&DrawTexture_Hook),
                             reinterpret_cast<void**>(&g_origDrawTexture), "AHUD::DrawTexture");
    const bool simple = Detour(offsets.kHudDrawTextureSimpleRva,
                               reinterpret_cast<void*>(&DrawTextureSimple_Hook),
                               reinterpret_cast<void**>(&g_origDrawTextureSimple),
                               "AHUD::DrawTextureSimple");
    if (!full && !simple) {
        Log::Line("reticle: no HUD texture draw available on this build - the game's "
                  "crosshair stays at the centre of the frame and marks the clean aim "
                  "only while the head is centred.");
        return false;
    }
    Log::Line("reticle: HUD crosshair follows the clean aim (DrawTexture=%s DrawTextureSimple=%s)",
              full ? "hooked" : "unavailable", simple ? "hooked" : "unavailable");
    return true;
}

void Publish(AimState state, float ndc_x, float ndc_y) {
    if (state == AimState::Known) {
        g_ndcX.store(ndc_x, std::memory_order_relaxed);
        g_ndcY.store(ndc_y, std::memory_order_relaxed);
    }
    g_aimState.store(static_cast<int>(state), std::memory_order_relaxed);
}

bool SawDraw() { return g_sawDraw.load(std::memory_order_relaxed); }

unsigned long long MovedCount() { return g_moved.load(std::memory_order_relaxed); }

}  // namespace ecr_ht::reticle
