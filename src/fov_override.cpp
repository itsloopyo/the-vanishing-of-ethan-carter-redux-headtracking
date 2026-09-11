// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "fov_override.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <cameraunlock/camera/zoom_compensation.h>
#include <cameraunlock/unreal/ue_runtime.h>

#include "builds/build_registry.h"
#include "log_throttle.h"
#include "logging.h"

namespace ecr_ht::fov {

namespace {

// Byte offset of FMinimalViewInfo::Rotation from ::Location.
//
// This is a cheap sanity check, NOT a proof that the out-params are one view
// info: FVector3f and FRotator3f are both 12 bytes, so two unrelated locals
// declared next to each other land 0x0c apart just as readily. What makes the
// write at +0x18 safe is that it follows the pinned render caller from the build
// profile; this catches a profile whose caller RVA has drifted onto something
// shaped differently, and the plausibility check on the value itself catches
// most of the rest.
constexpr std::ptrdiff_t kViewInfoRotationOffset = 0x0c;

// Minimum gap between "the game's FOV changed" log lines, so a slider drag or a
// scripted zoom is visible without streaming a line per frame.
constexpr std::uint64_t kChangeLogMinIntervalMs = 1000;

// Degrees the game's FOV must move before it is worth another log line.
constexpr float kChangeLogThresholdDeg = 0.5f;

// How close the frame's own field of view has to be to the player's setting for
// the frame to count as one the character's camera rendered. The two are equal
// to the third decimal at rest, so this only has to be tighter than the smallest
// thing the game does with the field of view - and it widens by six degrees to
// run.
constexpr float kAdoptToleranceDeg = 0.5f;

constexpr float kPi = 3.14159265358979323846f;

std::atomic<float> g_gameFov{0.0f};      // what the game asked to render at
std::atomic<float> g_renderedFov{0.0f};  // what the frame is rendered at
std::atomic<float> g_baseGameFov{0.0f};  // the reference as the game holds it
std::atomic<float> g_baseFov{0.0f};      // the same, with FovOffset applied
std::atomic<float> g_zoom{1.0f};         // what the pose is scaled by
std::atomic<bool>  g_usable{true};

float TanHalf(float fov_degrees) {
    return std::tan(fov_degrees * kPi / 360.0f);
}

template <typename... Args>
void Disable(const char* why_fmt, Args... args) {
    g_usable.store(false, std::memory_order_relaxed);
    char why[160];
    std::snprintf(why, sizeof(why), why_fmt, args...);
    Log::Line("fov: DISABLED - %s. The game's field of view is left untouched; "
              "head tracking is unaffected.", why);
}

void LogChange(float game_fov, float rendered_fov, float offset) {
    static std::atomic<float> s_lastLogged{0.0f};
    static LogThrottle s_throttle(kChangeLogMinIntervalMs);
    if (std::fabs(game_fov - s_lastLogged.load(std::memory_order_relaxed)) <
        kChangeLogThresholdDeg)
        return;
    if (!s_throttle.Due()) return;
    s_lastLogged.store(game_fov, std::memory_order_relaxed);
    Log::Line("fov: game=%.1f deg, FovOffset=%+.1f -> rendering at %.1f deg "
              "(un-zoomed base %.1f deg, pose scaled by %.4f)",
              game_fov, offset, rendered_fov,
              g_baseFov.load(std::memory_order_relaxed),
              g_zoom.load(std::memory_order_relaxed));
}

// The field of view the player set in Options -> Graphics, held on the player
// character. False - and no reference - rather than a guess when it cannot be
// read.
bool ReadCharacterFovSetting(std::uintptr_t controller, float& out) {
    const OffsetTable& offsets = Offsets();
    if (offsets.kPlayerCharacterOffset == 0 || offsets.kCharacterUnzoomedFovOffset == 0)
        return false;

    std::uintptr_t character = 0;
    if (!cameraunlock::unreal::SafeReadPtr(controller + offsets.kPlayerCharacterOffset,
                                           character) || character == 0)
        return false;

    float setting = 0.0f;
    if (!cameraunlock::unreal::SafeReadFloat(character + offsets.kCharacterUnzoomedFovOffset,
                                             setting))
        return false;
    if (!IsPlausible(setting)) return false;
    out = setting;
    return true;
}

// The character's setting is the reference only while the frame is actually
// being rendered through the character's own camera, and the way to know that
// is that the two agree. They do whenever the player is standing or walking;
// they part company while the game widens the view to run, which is the whole
// point, so the reference is ADOPTED on an agreeing frame and held through the
// disagreeing ones.
//
// The main menu is why this is not just "read the setting". It is a level with
// a player character of its own, whose field of view is 97 while the menu is
// rendered at 90 through a camera that is not the character's. Reading the
// setting alone there gives a reference the game never renders at, and a
// permanent 0.88 scale on a frame nothing is zooming. Never agreeing means
// never adopting, so nothing is scaled until the player is in the world.
void UpdateZoomFactor(std::uintptr_t controller, float game_fov, float rendered, float offset) {
    float setting = 0.0f;
    if (ReadCharacterFovSetting(controller, setting) &&
        std::fabs(game_fov - setting) <= kAdoptToleranceDeg)
        g_baseGameFov.store(setting, std::memory_order_relaxed);

    const float base = g_baseGameFov.load(std::memory_order_relaxed);
    if (base <= 0.0f) {
        g_baseFov.store(0.0f, std::memory_order_relaxed);
        g_zoom.store(1.0f, std::memory_order_relaxed);
        // Expected before the player reaches the world - the main menu renders
        // through a camera that is not the character's, so nothing agrees and
        // nothing is adopted. Worth one line all the same, because if it is
        // still true in gameplay the offsets are wrong for this build, and the
        // basis line that would say so is never written without a base.
        // One line for the whole session: a zero interval on a plain LogThrottle
        // is always due, which would stream this every menu frame.
        static BoundedLogThrottle s_once(0, 1);
        if (s_once.Due())
            Log::Line("fov: no un-zoomed reference adopted yet, so the head pose is "
                      "passed through unscaled. Expected until gameplay starts; if it "
                      "persists in the world, the character FOV offsets are wrong for "
                      "this build.");
        return;
    }

    // The offset is on both sides, so ordinary play - where the game renders at
    // the player's own setting - gives exactly 1.0 whatever FovOffset is.
    const float baseRendered = ApplyOffset(base, offset);
    g_baseFov.store(baseRendered, std::memory_order_relaxed);
    g_zoom.store(ZoomFactorBetween(rendered, baseRendered), std::memory_order_relaxed);
}

}

bool IsPlausible(float fov) {
    return std::isfinite(fov) && fov >= kMinPlausible && fov <= kMaxPlausible;
}

float ApplyOffset(float game_fov, float offset) {
    if (offset == 0.0f) return game_fov;
    const float rendered = game_fov + offset;
    if (rendered < kMinApplied) return kMinApplied;
    if (rendered > kMaxApplied) return kMaxApplied;
    return rendered;
}

float ZoomFactorBetween(float rendered_fov, float base_fov) {
    if (!IsPlausible(rendered_fov) || !IsPlausible(base_fov)) return 1.0f;
    return cameraunlock::camera::FovZoomFactor(TanHalf(rendered_fov), TanHalf(base_fov));
}

void Apply(std::uintptr_t controller, FVector3f* out_location,
           const FRotator3f* out_rotation, float offset) {
    if (!g_usable.load(std::memory_order_relaxed)) return;

    const std::size_t fovOffset = Offsets().kViewInfoFovOffset;
    if (fovOffset == 0) {
        // Said once rather than returning quietly forever. Not through Disable():
        // that line ends "head tracking is unaffected", and here it is - with no
        // readable field of view the pose is never scaled for zoom and the
        // crosshair never moves, which looks exactly like a mod that is not
        // working.
        g_usable.store(false, std::memory_order_relaxed);
        Log::Line("fov: this build profile carries no view-info FOV offset. The game's "
                  "field of view is left untouched, the head pose is not scaled when the "
                  "game zooms, and the crosshair stays where the game draws it rather "
                  "than following the aim.");
        return;
    }

    const auto location = reinterpret_cast<std::uintptr_t>(out_location);
    const auto rotation = reinterpret_cast<std::uintptr_t>(out_rotation);
    if (static_cast<std::ptrdiff_t>(rotation - location) != kViewInfoRotationOffset) {
        Disable("the render caller's out-params are %lld bytes apart, not the 0x%02x "
                "of FMinimalViewInfo",
                static_cast<long long>(rotation) - static_cast<long long>(location),
                static_cast<unsigned>(kViewInfoRotationOffset));
        return;
    }

    float* const fov = reinterpret_cast<float*>(location + fovOffset);
    const float gameFov = *fov;
    if (!IsPlausible(gameFov)) {
        Disable("+0x%02zx of the view info reads %.3f, which is not a field of view - "
                "kViewInfoFovOffset is wrong for this build",
                fovOffset, gameFov);
        return;
    }
    g_gameFov.store(gameFov, std::memory_order_relaxed);

    const float rendered = ApplyOffset(gameFov, offset);
    if (offset != 0.0f) *fov = rendered;
    g_renderedFov.store(rendered, std::memory_order_relaxed);
    UpdateZoomFactor(controller, gameFov, rendered, offset);
    LogChange(gameFov, rendered, offset);
}

float GameFov()     { return g_gameFov.load(std::memory_order_relaxed); }
float RenderedFov() { return g_renderedFov.load(std::memory_order_relaxed); }
float BaseFov()     { return g_baseFov.load(std::memory_order_relaxed); }
float ZoomFactor()  { return g_zoom.load(std::memory_order_relaxed); }

}
