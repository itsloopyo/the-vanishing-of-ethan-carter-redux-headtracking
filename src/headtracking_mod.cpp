// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <windows.h>
#include <psapi.h>
#include <intrin.h>

#include "aim_projection.h"
#include "builds/build_registry.h"
#include "caller_gate.h"
#include "camera_pose.h"
#include "config.h"
#include "dev_probe.h"
#include "engine_trace.h"
#include "fov_override.h"
#include "hook_diagnostics.h"
#include "hud_reticle.h"
#include "log_throttle.h"
#include "logging.h"
#include "ue_abi.h"
#include "window_centering.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/os/module_paths.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"
#include "cameraunlock/unreal/ue_math.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace ecr_ht {

namespace {

namespace ue = ::cameraunlock::unreal;
namespace hooks = ::cameraunlock::hooks;

using cameraunlock::TrackingMode;
using cameraunlock::input::ChordGuarded;
using cameraunlock::input::NavGuarded;
using cameraunlock::time::FrameClock;

using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;

// The session picks between LocalSmoothing and RemoteSmoothing from the
// receiver's source-address check. That wiring is compile-time detected, so a
// receiver without IsRemoteConnection() would silently pin every session to the
// local value instead of failing to build.
static_assert(Session::kHasRemoteConnection,
              "receiver must expose IsRemoteConnection() for per-connection smoothing");

// Virtual-key codes for the AGENTS.md default bindings. The nav-cluster keys are
// the primary bindings; the Ctrl+Shift chords are the alternative for keyboards
// without a nav cluster.
namespace vk {
constexpr int kEnd    = 0x23;
constexpr int kPageUp = 0x21;
constexpr int kY = 0x59;
constexpr int kG = 0x47;
constexpr int kH = 0x48;
#if ECR_DEV_HOTKEYS
constexpr int kU = 0x55;
constexpr int kJ = 0x4A;
constexpr int kB = 0x42;
constexpr int kN = 0x4E;
constexpr int kF = 0x46;
#endif
}

// ---- state ---------------------------------------------------------------

Config g_config;

std::unique_ptr<cameraunlock::UdpReceiver> g_receiver;
std::unique_ptr<Session> g_session;
std::unique_ptr<cameraunlock::input::HotkeyPoller> g_hotkeys;

std::atomic<bool> g_trackingEnabled{true};
// true = world-space yaw (horizon-locked, FRotator addition); false = camera-
// local yaw (quaternion post-multiply, leans on pitched turns).
std::atomic<bool> g_worldSpaceYaw{true};
std::atomic<int>  g_injectMode{inject::kModeFirstCaller};

// Non-zero when the module could not be pinned. Recorded rather than logged at
// the point of failure, because the log is opened on the bootstrap thread.
DWORD g_pinFailure = 0;

GetPlayerViewPoint_t g_origGetPlayerViewPoint = nullptr;
std::atomic<std::uint64_t> g_hookCallCount{0};

// Ticked only by the injected render-path caller, so the session sees one dt per
// rendered frame.
FrameClock g_frameClock;

// Keeps a lean from putting the eye inside the level. Core owns the policy; the
// engine sweep behind it is ecr_ht::trace::QueryLean.
cameraunlock::camera::LeanClamp g_leanClamp;

// Previous frame's clean eye, for spotting a camera cut. The valley streams in
// and out and the story teleports the player, and an allowance carried across
// one of those is the previous room's wall rationing the lean in the new one.
ue::FVector g_lastCleanEye{0.0, 0.0, 0.0};
bool g_haveLastCleanEye = false;

// A jump larger than this between consecutive rendered frames is a cut, not
// walking: the player moves a few centimetres per frame at walking pace, so this
// is two orders of magnitude clear of ordinary movement.
constexpr double kCameraCutDistance = 300.0;

// How far the aim ray reaches, in UE units. Past this the parallax correction is
// smaller than a pixel, so a miss costs nothing and the ray is cheaper.
constexpr float kAimTraceRange = 20000.0f;

// Hotkey poll interval. Fast enough that a tap is never missed between polls,
// slow enough to stay off the profiler.
constexpr int kHotkeyPollIntervalMs = 16;

// kKnownCallerRvas[0] is the render caller (ULocalPlayer::GetViewPoint) by the
// profile's definition - see steam_offsets.cpp. The FOV work follows that caller
// specifically rather than whatever the inject mode currently points at, because
// a dev build can cycle the inject mode onto a caller whose out-params are two
// unrelated stack locals.
std::uintptr_t RenderCallerRva() { return Offsets().kKnownCallerRvas[0]; }

// Cursor visible -> menu / cutscene / pause -> suppress tracking. When the
// offset isn't known for this build the gate is disabled (always gameplay).
bool InGameplay(std::uintptr_t controller) {
    const OffsetTable& offsets = Offsets();
    if (offsets.kShowMouseCursorOffset == 0) return true;
    std::uint32_t flags = 0;
    if (!ue::SafeReadU32(controller + offsets.kShowMouseCursorOffset, flags)) return false;
    return (flags & offsets.kShowMouseCursorMask) == 0;
}

// ---- lean clamp and aim projection ---------------------------------------

// Drops the lean allowance and the published reticle offset on any frame that
// renders no head pose at all. Without it the last frame's allowance rations the
// first lean after a pause, and the crosshair stays parked where the aim used to
// be while the mod is no longer moving the camera.
void ReleaseCamera() {
    g_leanClamp.Reset();
    reticle::Publish(reticle::AimState::NotTracking, 0.0f, 0.0f);
}

void NoteCameraCut(const ue::FVector& cleanEye) {
    if (g_haveLastCleanEye) {
        const double dx = cleanEye.X - g_lastCleanEye.X;
        const double dy = cleanEye.Y - g_lastCleanEye.Y;
        const double dz = cleanEye.Z - g_lastCleanEye.Z;
        if (dx * dx + dy * dy + dz * dz > kCameraCutDistance * kCameraCutDistance)
            g_leanClamp.Reset();
    }
    g_lastCleanEye = cleanEye;
    g_haveLastCleanEye = true;
}

// Cuts the lean to whatever the level leaves room for, sweeping from the CLEAN
// eye - the position the game itself put the camera at. Clamping after the
// offset is applied would mean reading back a position already inside the wall.
ue::FVector ClampLean(std::uintptr_t controller, const ue::FVector& cleanEye,
                      const ue::FVector& desired, float dt) {
    if (!g_config.collision_enabled) return desired;

    const cameraunlock::math::Vec3 eye{static_cast<float>(cleanEye.X),
                                       static_cast<float>(cleanEye.Y),
                                       static_cast<float>(cleanEye.Z)};
    const cameraunlock::math::Vec3 offset{static_cast<float>(desired.X),
                                          static_cast<float>(desired.Y),
                                          static_cast<float>(desired.Z)};
    const cameraunlock::math::Vec3 allowed =
        g_leanClamp.Apply(eye, offset, dt, &trace::QueryLean,
                          reinterpret_cast<void*>(controller));
    diag::LogLeanState(g_leanClamp.InContact(), g_leanClamp.LastQueryFailed());
    return ue::FVector{allowed.x, allowed.y, allowed.z};
}

// Where the clean aim lands, in the frame that was just composed, and how far
// away that is - the distance is the pose line's rangefinder and comes out of
// the same trace, so the two cannot describe different frames.
struct AimResult {
    // NotTracking leaves the crosshair alone: the mod could not measure the aim,
    // and taking the crosshair away over that would turn a missing feature into
    // a missing crosshair for the whole session. Unknown is only for an aim that
    // WAS measured and came out behind the rendered eye, where the centre of the
    // frame is known not to be the aim.
    reticle::AimState state = reticle::AimState::NotTracking;
    AimNdc ndc;
    float distance = 0.0f;
};

// Returns an invalid result - and so publishes nothing - when the trace could
// not run or the point is behind the rendered eye, rather than leaving a stale
// mark standing where the aim is no longer pointing.
AimResult ProjectCleanAim(std::uintptr_t controller, const ue::FVector& cleanEye,
                          const ue::FQuat4d& cleanQ, const FVector3f& renderLocation,
                          const FRotator3f& renderRotation) {
    if (!g_config.move_crosshair) return AimResult{};

    // Without a readable field of view the tangents fall back to the identity,
    // which projects as though the frame were 90 degrees on both axes - a mark
    // placed confidently in the wrong place. Skipping here also spares the frame
    // the trace below, which is the expensive half.
    const float rendered = fov::RenderedFov();
    if (!fov::IsPlausible(rendered)) return AimResult{};

    float width = 0.0f, height = 0.0f;
    if (!reticle::ViewportSize(width, height)) return AimResult{};
    const HalfFieldTangents tangents = TangentsFromHorizontalFov(rendered, width, height);

    const ue::FVector aimDir = ue::QuatRotateVec(cleanQ, ue::FVector{1.0, 0.0, 0.0});
    const trace::AimHit hit = trace::TraceAim(reinterpret_cast<void*>(controller),
                                              cleanEye, aimDir, kAimTraceRange);
    if (!hit.queried) return AimResult{};

    // A definite no-hit is a target at infinity, so the aim direction is
    // projected for that frame. Never a magic fallback distance, and never the
    // last wall's depth: a fixed depth agrees with the aim at exactly one range
    // and crosses to the other side of it either way out from there.
    const ue::FVector aimPoint = hit.blocked
        ? hit.point
        : ue::FVector{cleanEye.X + aimDir.X * kAimTraceRange,
                      cleanEye.Y + aimDir.Y * kAimTraceRange,
                      cleanEye.Z + aimDir.Z * kAimTraceRange};

    const ue::FVector renderEye{renderLocation.X, renderLocation.Y, renderLocation.Z};
    const ue::FQuat4d renderQ = ue::QuatFromEulerDeg(renderRotation.Pitch, renderRotation.Yaw,
                                                     renderRotation.Roll);
    const AimNdc ndc = ProjectAimPoint(renderEye, renderQ, aimPoint, tangents);
    if (!ndc.valid) {
        // Measured, and behind the plane of the rendered eye. The crosshair is
        // taken off the frame rather than left at a centre the mod knows is not
        // the aim. Said once, because a crosshair that vanishes with nothing in
        // the log is untriageable.
        static BoundedLogThrottle s_hidden(0, 1);
        if (s_hidden.Due())
            Log::Line("reticle: the clean aim is behind the rendered view, so the "
                      "crosshair is taken off the frame while that lasts.");
        return AimResult{reticle::AimState::Unknown, ndc, 0.0f};
    }
    return AimResult{reticle::AimState::Known, ndc,
                     hit.blocked ? hit.distance : kAimTraceRange};
}

// ---- the hook ------------------------------------------------------------

// Writes the head pose over the view the game just produced, having first
// established that there is one to write: the session has to have advanced and
// carry a rotation. False means nothing was written and the camera was
// released, so the caller has nothing left to do for this frame.
//
// Everything downstream of the zoom scaling - the rotation, the lean and its
// clamp, the reticle projection - is derived from that one scaled pose, so the
// camera write, the sweep and the mark cannot describe different frames.
bool ApplyHeadPose(std::uintptr_t controller, const FRotator3f& clean, float dt,
                   FVector3f* outLocation, FRotator3f* outRotation,
                   diag::PoseSnapshot& outPose) {
    if (!g_session->Update(dt)) {
        ReleaseCamera();
        return false;
    }

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (!g_session->GetRotation(yaw, pitch, roll)) {
        ReleaseCamera();
        return false;
    }

    float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;
    const bool havePosition = g_session->GetPositionOffset(offsetX, offsetY, offsetZ);

    // Scaled here, before anything is composed, clamped or projected. The factor
    // is 1.0 unless the game is rendering at something other than its own
    // un-zoomed field of view - it widens the view while the player runs - and
    // it is what keeps the head worth the same amount of picture either way.
    const float zoom = fov::ZoomFactor();
    const ScaledPose pose = ScalePoseForZoom(yaw, pitch, roll, offsetX, offsetY, offsetZ, zoom);

    const ue::FVector cleanEye{outLocation->X, outLocation->Y, outLocation->Z};
    NoteCameraCut(cleanEye);

    const ue::FQuat4d cleanQ = CleanViewQuat(clean);
    *outRotation = ComposeViewRotation(clean, cleanQ, pose.yaw, pose.pitch, pose.roll,
                                       g_worldSpaceYaw.load(std::memory_order_relaxed));

    ue::FVector posOffset{0.0, 0.0, 0.0};
    if (havePosition) {
        posOffset = ClampLean(controller, cleanEye,
                              ComputePositionOffset(cleanQ, pose.x, pose.y, pose.z), dt);
        outLocation->X += static_cast<float>(posOffset.X);
        outLocation->Y += static_cast<float>(posOffset.Y);
        outLocation->Z += static_cast<float>(posOffset.Z);
    } else {
        g_leanClamp.Reset();
    }

    // The reticle marks where the CLEAN camera points, because that is the ray
    // the game's own interaction check runs, and it is projected through the
    // basis that was just written rather than re-derived from the tracker pose -
    // one derivation used twice cannot disagree with itself.
    const AimResult aim = ProjectCleanAim(controller, cleanEye, cleanQ, *outLocation,
                                          *outRotation);
    reticle::Publish(aim.state, aim.ndc.x, aim.ndc.y);

    outPose.clean = clean;
    outPose.applied_yaw = pose.yaw;
    outPose.applied_pitch = pose.pitch;
    outPose.applied_roll = pose.roll;
    outPose.zoom = zoom;
    outPose.result = *outRotation;
    outPose.position_offset = posOffset;
    outPose.aim_distance = aim.distance;
    outPose.aim = aim.ndc;
    outPose.lean_in_contact = g_leanClamp.InContact();
    outPose.lean_query_failed = g_leanClamp.LastQueryFailed();
    return true;
}

void __fastcall GetPlayerViewPoint_Hook(void* self, FVector3f* outLocation, FRotator3f* outRotation) {
    const void* retAddr = _ReturnAddress();
    const std::uintptr_t retRva = ue::ModuleBase() != 0
        ? reinterpret_cast<std::uintptr_t>(retAddr) - ue::ModuleBase()
        : reinterpret_cast<std::uintptr_t>(retAddr);

    const auto controller = reinterpret_cast<std::uintptr_t>(self);
    const bool inGameplay = InGameplay(controller);
#if ECR_DEV_HOTKEYS
    dev::ServiceRequest(controller);
#endif

    g_origGetPlayerViewPoint(self, outLocation, outRotation);
    const FRotator3f clean = *outRotation;

    const auto call = g_hookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const int mode = g_injectMode.load(std::memory_order_relaxed);

    if (mode == inject::kModeAll)
        inject::RecordCaller(retRva, call);

    // Field of view is a rendering preference, not head tracking: it follows the
    // render caller whether or not tracking is enabled and whether or not the
    // player is in gameplay, so the framing does not jump when they pause or
    // switch tracking off.
    const std::uintptr_t renderCaller = RenderCallerRva();
    if (renderCaller != 0 && retRva == renderCaller) {
        fov::Apply(controller, outLocation, outRotation, g_config.fov_offset);
        diag::LogFovBasis(g_config.fov_offset);
    }

    const bool trackingEnabled = g_trackingEnabled.load(std::memory_order_relaxed);
    diag::HeartbeatSnapshot beat{};
    beat.hook_calls = call;
    beat.caller_rva = retRva;
    beat.in_gameplay = inGameplay;
    beat.tracking_enabled = trackingEnabled;
    beat.world_space_yaw = g_worldSpaceYaw.load(std::memory_order_relaxed);
    beat.inject_mode = mode;
    diag::LogHeartbeat(g_receiver.get(), beat);

    if (!trackingEnabled || !g_session || !inGameplay) {
        ReleaseCamera();
        return;
    }

    // Decoupling: only the render-path caller(s) get the head pose written back.
    // Every other GetPlayerViewPoint caller (interaction traces, audio listener,
    // AI perception, replication) keeps the clean mouse/pad rotation.
    if (!inject::ShouldInject(Offsets().kKnownCallerRvas, retRva, mode))
        return;

    diag::PoseSnapshot pose{};
    pose.hook_calls = call;
    pose.caller_rva = retRva;
    if (!ApplyHeadPose(controller, clean, g_frameClock.Tick(), outLocation, outRotation, pose))
        return;

    if (diag::PoseDetailDue()) diag::LogPoseDetail(pose);
}

// ---- hotkeys -------------------------------------------------------------

void ToggleTracking() {
    const bool enabled = !g_trackingEnabled.load();
    g_trackingEnabled.store(enabled);
    Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
}

const char* TrackingModeName(TrackingMode mode) {
    switch (mode) {
        case TrackingMode::RotationAndPosition: return "rotation+position";
        case TrackingMode::RotationOnly:        return "rotation only";
        case TrackingMode::PositionOnly:        return "position only";
    }
    return "unknown";
}

// Three-state, not an on/off toggle: the session drives rotation and position
// independently, so a player who wants leaning without head turning can have it.
void CycleTrackingMode() {
    Log::Line("hotkey: tracking mode %s", TrackingModeName(g_session->CycleMode()));
}

void ToggleYawMode() {
    const bool worldSpace = !g_worldSpaceYaw.load();
    g_worldSpaceYaw.store(worldSpace);
    Log::Line("hotkey: yaw mode %s", worldSpace ? "world" : "local");
}

#if ECR_DEV_HOTKEYS
void CycleInjectMode(int direction) {
    const int mode = (g_injectMode.load() + direction + inject::kModeCount) % inject::kModeCount;
    g_injectMode.store(mode);
    const auto rva = (mode >= inject::kModeFirstCaller && mode <= inject::kModeLastCaller)
        ? Offsets().kKnownCallerRvas[static_cast<std::size_t>(mode - 1)]
        : 0;
    Log::Line("hotkey: inject mode -> %d (caller RVA 0x%08llx)",
        mode, static_cast<unsigned long long>(rva));
}
#endif

void RegisterHotkeys() {
    g_hotkeys = std::make_unique<cameraunlock::input::HotkeyPoller>();

    // Nav-cluster (AGENTS.md default bindings). Suppressed while Ctrl+Shift is
    // held so the chord path is the sole trigger for those combos.
    g_hotkeys->AddHotkey(vk::kEnd,              NavGuarded([] { ToggleTracking(); }));
    g_hotkeys->AddHotkey(vk::kPageUp,           NavGuarded([] { CycleTrackingMode(); }));
    g_hotkeys->AddHotkey(g_config.yaw_mode_key, NavGuarded([] { ToggleYawMode(); }));

    // Ctrl+Shift chord alternatives (Y/G/H cluster).
    g_hotkeys->AddHotkey(vk::kY, ChordGuarded([] { ToggleTracking(); }));
    g_hotkeys->AddHotkey(vk::kG, ChordGuarded([] { CycleTrackingMode(); }));
    g_hotkeys->AddHotkey(vk::kH, ChordGuarded([] { ToggleYawMode(); }));

#if ECR_DEV_HOTKEYS
    // Dev: re-confirm the render caller in-game (cycle which GPV caller is
    // injected) without a rebuild. Ctrl+Shift+U next / Ctrl+Shift+J prev.
    g_hotkeys->AddHotkey(vk::kU, ChordGuarded([] { CycleInjectMode(+1); }));
    g_hotkeys->AddHotkey(vk::kJ, ChordGuarded([] { CycleInjectMode(-1); }));
    // Controller field diff, for measuring a bitfield UPROPERTY's offset and mask.
    g_hotkeys->AddHotkey(vk::kB, ChordGuarded([] { dev::RequestBaseline(); }));
    g_hotkeys->AddHotkey(vk::kN, ChordGuarded([] { dev::RequestDiff(); }));
    // Field-of-view field scan, for re-deriving the player character's un-zoomed
    // FOV offset.
    g_hotkeys->AddHotkey(vk::kF, ChordGuarded([] { dev::RequestFovScan(); }));
    Log::Line("dev: inject-mode hotkeys enabled (Ctrl+Shift+U / Ctrl+Shift+J), "
              "controller diff (Ctrl+Shift+B baseline / Ctrl+Shift+N diff), "
              "field-of-view field scan (Ctrl+Shift+F)");
#endif

    g_hotkeys->Start(kHotkeyPollIntervalMs);
}

// ---- bootstrap -----------------------------------------------------------

void ApplyConfigToSession() {
    g_trackingEnabled.store(g_config.enable_on_startup);
    g_worldSpaceYaw.store(g_config.world_space_yaw);

    cameraunlock::SensitivitySettings sens;
    sens.yaw          = g_config.yaw_sensitivity;
    sens.pitch        = g_config.pitch_sensitivity;
    sens.roll         = g_config.roll_sensitivity;
    sens.invert_yaw   = g_config.invert_yaw;
    sens.invert_pitch = g_config.invert_pitch;
    sens.invert_roll  = g_config.invert_roll;
    g_session->GetProcessor().SetSensitivity(sens);
    // Both smoothing parameters cover rotation and position; the session picks
    // between them per connection from the receiver's source-address check, so a
    // switch from a local OpenTrack instance to a phone on WiFi mid-session needs
    // no restart.
    g_session->SetLocalSmoothing(g_config.local_smoothing);
    g_session->SetRemoteSmoothing(g_config.remote_smoothing);

    trace::SetAimChannel(g_config.aim_trace_channel);
    trace::SetLeanChannel(g_config.collision_channel);
    trace::SetLeanRadius(g_config.collision_radius);
    // The sphere sweep reports where its CENTRE stopped, so the radius IS the
    // standoff and the clamp's own skin must be zero or it is applied twice.
    cameraunlock::camera::LeanClampSettings lean;
    lean.skin = 0.0f;
    lean.release_smoothing = g_config.collision_release_smoothing;
    g_leanClamp.SetSettings(lean);

    auto& position = g_session->GetPositionProcessor().GetSettings();
    position.sensitivity_x = g_config.position_sensitivity_x;
    position.sensitivity_y = g_config.position_sensitivity_y;
    position.sensitivity_z = g_config.position_sensitivity_z;
    position.limit_x       = g_config.limit_x;
    // The clamp is [-limit_y_down, +limit_y] and limit_y_down carries its own
    // default, so mirror the one configured vertical limit the way
    // PositionSettings::Symmetric does. Left unset, raising LimitY widened the
    // upward budget only and downward travel stayed pinned at 0.20m.
    position.limit_y       = g_config.limit_y;
    position.limit_y_down  = g_config.limit_y;
    position.limit_z       = g_config.limit_z;
    position.limit_z_back  = g_config.limit_z_back;

    g_session->SetMode(g_config.position_enabled
        ? TrackingMode::RotationAndPosition
        : TrackingMode::RotationOnly);
}

// Log rotation is core's: Open() renames the outgoing generation to
// HeadTracking.prev.log and reports a failed rename into the fresh log. The
// crash handler asks the user to send this file, and they relaunch the game
// before going to look for it, which would otherwise truncate away the session
// being reported.
void OpenLog() {
    const std::wstring exeDir = cameraunlock::os::HostExeDirectory();
    // Empty only if the EXE path cannot be resolved at all. The log is the mod's
    // one diagnostic channel, so fall back to the working directory rather than
    // losing it.
    Log::Open((exeDir.empty() ? std::wstring(L".") : exeDir) + L"\\HeadTracking.log");
    Log::Line("=== The Vanishing of Ethan Carter Redux Head Tracking (UE4) ===");
    // Here rather than after the build-profile gate. Core's handler is a passive
    // unhandled-exception filter that logs and returns EXCEPTION_CONTINUE_SEARCH,
    // so the game's own crash flow still runs and a dormant build still behaves
    // exactly like an unmodded one. Installing it later would leave the config
    // read, the PE fingerprint walk and the module query - the startup chain a
    // crash report is actually wanted for, and the one the engine has not yet
    // installed its own filter over - running uncovered.
    cameraunlock::diagnostics::InstallCrashHandler();
}

void InitConfig() {
    const std::string exeDir = cameraunlock::os::HostExeDirectoryNarrow();
    if (exeDir.empty()) {
        // The INI layer is ANSI (GetPrivateProfile*A). A game directory with no
        // ANSI form has no path to read or write, and narrowing it best-fit would
        // address somebody else's folder.
        Log::Line("config: the game directory has no representable ANSI path, so "
                  "HeadTracking.ini cannot be read or written - built-in defaults "
                  "are used for this session.");
    } else {
        WriteDefaultConfigIfMissing(exeDir);
        LoadConfig(exeDir, g_config);
    }
    Log::Line("config: udp_port=%d enable=%d yaw_sens=%.2f local_smoothing=%.2f remote_smoothing=%.2f position=%d yaw_mode=%s fov_offset=%+.1f",
        g_config.udp_port, g_config.enable_on_startup ? 1 : 0,
        g_config.yaw_sensitivity, g_config.local_smoothing, g_config.remote_smoothing,
        g_config.position_enabled ? 1 : 0,
        g_config.world_space_yaw ? "world" : "local",
        g_config.fov_offset);
    Log::Line("config: move_crosshair=%d aim_channel=%d collision=%d radius=%.0f channel=%d release=%.2f",
        g_config.move_crosshair ? 1 : 0, g_config.aim_trace_channel,
        g_config.collision_enabled ? 1 : 0, g_config.collision_radius,
        g_config.collision_channel, g_config.collision_release_smoothing);
}

// False leaves the mod dormant: no hooks installed, game runs vanilla.
bool SelectBuildProfile(HMODULE host) {
    switch (builds::SelectProfile(host)) {
        case builds::MatchResult::Matched:
            g_injectMode.store(Offsets().kDefaultInjectMode);
            return true;
        case builds::MatchResult::HostNewer:
            Log::Line("build-check: this game build is NEWER than any profile this "
                      "mod knows about - check the releases page for an update. "
                      "Staying dormant; game runs vanilla.");
            return false;
        case builds::MatchResult::HostOlder:
            Log::Line("build-check: this game build is OLDER than the profile - let "
                      "Steam finish updating. Staying dormant; game runs vanilla.");
            return false;
        case builds::MatchResult::HostDiffers:
            Log::Line("build-check: this EXE has the build date this mod knows but a "
                      "different size or checksum, which means a tampered or repacked "
                      "binary. This mod will not engage on a modified EXE. Staying "
                      "dormant; game runs vanilla.");
            return false;
        case builds::MatchResult::ProfileIncomplete:
            Log::Line("build-check: this build is recognised but its profile did not "
                      "validate, so the mod will not hook against it. Staying dormant; "
                      "game runs vanilla.");
            return false;
        case builds::MatchResult::ReadFailed:
        default:
            Log::Line("build-check: could not read the game EXE's PE header, so the "
                      "build cannot be identified - staying dormant; game runs vanilla.");
            return false;
    }
}

bool InitUnrealRuntime(HMODULE host) {
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), host, &info, sizeof(info))) {
        Log::Line("FATAL: GetModuleInformation failed - cannot resolve RVAs");
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    // The lean hook does no UObject reflection, so the globals layout is left
    // zeroed - only the module range and SafeRead* guards are used.
    ue::SetRuntime(base, base + info.SizeOfImage, ue::UObjectGlobalsLayout{});
    Log::Line("module base=0x%llx size=0x%x",
        static_cast<unsigned long long>(base), info.SizeOfImage);
    return true;
}

void StartTracking() {
    g_receiver = std::make_unique<cameraunlock::UdpReceiver>();
    g_receiver->SetLog([](const std::string& m) { Log::Line("udp: %s", m.c_str()); });
    // A busy port is not an error here. The receiver's supervisor keeps retrying
    // the bind and takes the port the moment whatever held it (the game the user
    // forgot to close) exits, so the return value is ignored and the sink above
    // carries both the wait and the reclaim into the log.
    g_receiver->Start(static_cast<uint16_t>(g_config.udp_port));

    g_session = std::make_unique<Session>(*g_receiver);
    ApplyConfigToSession();
}

bool InstallCameraHook() {
    auto& manager = hooks::HookManager::Instance();
    if (auto status = manager.Initialize(); status != hooks::HookStatus::Ok) {
        Log::Line("FATAL: MinHook init failed: %s", hooks::HookStatusToString(status));
        return false;
    }

    void* target = reinterpret_cast<void*>(
        ue::ModuleBase() + Offsets().kGetPlayerViewPointRva);
    if (auto status = manager.CreateHook(target,
                                         reinterpret_cast<void*>(&GetPlayerViewPoint_Hook),
                                         reinterpret_cast<void**>(&g_origGetPlayerViewPoint));
        status != hooks::HookStatus::Ok) {
        Log::Line("FATAL: CreateHook(GetPlayerViewPoint) failed: %s",
                  hooks::HookStatusToString(status));
        return false;
    }
    if (auto status = manager.EnableHook(target); status != hooks::HookStatus::Ok) {
        Log::Line("FATAL: EnableHook failed: %s", hooks::HookStatusToString(status));
        return false;
    }

    Log::Line("GetPlayerViewPoint hooked at RVA 0x%08llx (default inject mode %d)",
        static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva),
        g_injectMode.load());
    return true;
}

DWORD WINAPI BootstrapThread(LPVOID) {
    OpenLog();
    if (g_pinFailure != 0) {
        // The pin is what makes the absent DllMain teardown safe. Without it an
        // unload while the detours or this thread are live runs unmapped code.
        Log::Line("WARNING: could not pin the module (error %lu). Nothing unloads an "
                  "ASI plugin in normal use, but an unload now would be unsafe.",
                  g_pinFailure);
    }
    InitConfig();

    HMODULE host = GetModuleHandleW(nullptr);
    if (!SelectBuildProfile(host)) return 0;
    if (!InitUnrealRuntime(host)) return 0;

    StartTracking();
    if (!InstallCameraHook()) {
        // The receiver is already bound to the tracker port at this point. Left
        // running it would hold UDP 4242 for the whole life of a process that has
        // given up on head tracking, and the next game the player starts would sit
        // in its own bind-retry loop until they quit this one.
        g_receiver->Stop();
        return 0;
    }

    // Both of these depend on the camera hook being live, and both degrade to
    // nothing on their own: without the world queries there is no lean clamp and
    // no aim point, and without the HUD detour the crosshair simply stays put.
    trace::Ready();
    if (g_config.move_crosshair) reticle::Install();

    RegisterHotkeys();
    Log::Line("init complete. End=toggle PageUp=tracking mode VK 0x%02X=yawmode "
              "(chords Ctrl+Shift+Y/G/H). Waiting for OpenTrack on UDP %d.",
        g_config.yaw_mode_key, g_config.udp_port);

    // Last, because it blocks until the game's window has stopped moving, which
    // is tens of seconds into a cold start. Everything above is what tracking
    // needs to be live the moment the player reaches gameplay.
    CenterWindowWhenReady();
    return 0;
}

}  // namespace

void Initialize(HMODULE self) {
    // Pin the module before anything else starts. The detours are entered from
    // engine threads, and the bootstrap thread below blocks for tens of seconds
    // inside CenterWindowWhenReady; an unload while either is live would run
    // unmapped code, and the teardown that would have prevented it cannot run
    // safely from DllMain (see the comment there). An ASI loader never frees a
    // plugin, so this only closes the door a third party could open.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(self), &pinned)) {
        g_pinFailure = GetLastError();
    }

    // Nothing joins the bootstrap thread - it runs once and exits - so the
    // handle is closed straight away rather than held for the process lifetime.
    if (HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr))
        CloseHandle(thread);
}

}  // namespace ecr_ht
