// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Frozen. See legacy_config.h.

#include "legacy_config.h"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"

namespace ecr_ht::legacy {

namespace {

namespace guards = ::cameraunlock::config;

const Config kDefaults{};

void GuardLog(const char* fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    if (written < 0) return;
    Log::Line("%s", message);
}

constexpr float kMaxFovOffset = 60.0f;

constexpr int kMinUdpPort = 1024;
constexpr int kMaxUdpPort = 65535;

int SanitizeUdpPort(int value) {
    if (value >= kMinUdpPort && value <= kMaxUdpPort) return value;
    Log::Line("config: [Network] UdpPort %d is outside %d-%d, using %d",
              value, kMinUdpPort, kMaxUdpPort, kDefaults.udp_port);
    return kDefaults.udp_port;
}

float ReadSensitivity(const cameraunlock::IniReader& ini, const char* section,
                      const char* key, float fallback) {
    return guards::ReadFloatChecked(ini, section, key, fallback,
                                    -guards::kMaxSensitivity, guards::kMaxSensitivity,
                                    &GuardLog);
}

float ReadPositionLimit(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    return guards::ReadFloatChecked(ini, "Position", key, fallback,
                                    0.0f, guards::kMaxPositionLimit, &GuardLog);
}

constexpr float kMinCollisionRadius = 11.0f;
constexpr float kMaxCollisionRadius = 200.0f;

constexpr int kMaxTraceChannel = 255;

int ReadChannel(const cameraunlock::IniReader& ini, const char* section, const char* key,
                const char* what, int fallback) {
    const std::string raw = guards::ReadRawValue(ini, section, key);
    if (raw.empty()) return fallback;

    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != '\0' || errno == ERANGE ||
        parsed < 0 || parsed > kMaxTraceChannel) {
        Log::Line("config: %s \"%s\" is not an ETraceTypeQuery index (0-%d), using %d",
                  what, raw.c_str(), kMaxTraceChannel, fallback);
        return fallback;
    }
    return static_cast<int>(parsed);
}

bool ReadBoolChecked(const cameraunlock::IniReader& ini, const char* section, const char* key,
                     const char* what, bool fallback) {
    const std::string raw = guards::ReadRawValue(ini, section, key);
    if (raw.empty()) return fallback;

    for (const char* yes : {"1", "true", "yes", "on"})
        if (_stricmp(raw.c_str(), yes) == 0) return true;
    for (const char* no : {"0", "false", "no", "off"})
        if (_stricmp(raw.c_str(), no) == 0) return false;

    Log::Line("config: %s \"%s\" is not a yes/no value (0/1, true/false, yes/no, "
              "on/off), using %d",
              what, raw.c_str(), fallback ? 1 : 0);
    return fallback;
}

int SanitizeYawModeKey(int vk) {
    if (guards::IsBindableVirtualKey(vk)) return vk;
    Log::Line("config: [Hotkeys] YawModeKey 0x%02X is not a bindable virtual-key code, using 0x%02X",
              vk, kDefaults.yaw_mode_key);
    return kDefaults.yaw_mode_key;
}

}  // namespace

bool Load(const std::string& ini_path, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(ini_path)) return false;

    out.udp_port           = SanitizeUdpPort(
        ini.ReadInt("Network", "UdpPort", out.udp_port));
    out.enable_on_startup  = ReadBoolChecked(ini, "General", "EnableOnStartup",
        "[General] EnableOnStartup", out.enable_on_startup);
    out.world_space_yaw    = ReadBoolChecked(ini, "General", "WorldSpaceYaw",
        "[General] WorldSpaceYaw", out.world_space_yaw);

    out.yaw_mode_key       = SanitizeYawModeKey(
        ini.ReadHex("Hotkeys", "YawModeKey", out.yaw_mode_key));

    out.yaw_sensitivity    = ReadSensitivity(ini, "Rotation", "YawSensitivity",   out.yaw_sensitivity);
    out.pitch_sensitivity  = ReadSensitivity(ini, "Rotation", "PitchSensitivity", out.pitch_sensitivity);
    out.roll_sensitivity   = ReadSensitivity(ini, "Rotation", "RollSensitivity",  out.roll_sensitivity);
    out.invert_yaw         = ReadBoolChecked(ini, "Rotation", "InvertYaw",
        "[Rotation] InvertYaw", out.invert_yaw);
    out.invert_pitch       = ReadBoolChecked(ini, "Rotation", "InvertPitch",
        "[Rotation] InvertPitch", out.invert_pitch);
    out.invert_roll        = ReadBoolChecked(ini, "Rotation", "InvertRoll",
        "[Rotation] InvertRoll", out.invert_roll);

    out.local_smoothing    = guards::ReadFloatChecked(ini, "Rotation", "LocalSmoothing",
        kDefaults.local_smoothing, 0.0f, 1.0f, &GuardLog);
    out.remote_smoothing   = guards::ReadFloatChecked(ini, "Rotation", "RemoteSmoothing",
        kDefaults.remote_smoothing, 0.0f, 1.0f, &GuardLog);

    guards::WarnRetiredSmoothingKey(ini, "Rotation", "Smoothing", &GuardLog);

    out.fov_offset         = guards::ReadFloatChecked(ini, "Camera", "FovOffset",
        kDefaults.fov_offset, -kMaxFovOffset, kMaxFovOffset, &GuardLog);

    out.move_crosshair     = ReadBoolChecked(ini, "Reticle", "MoveCrosshair",
        "[Reticle] MoveCrosshair", out.move_crosshair);
    out.aim_trace_channel  = ReadChannel(ini, "Reticle", "AimTraceChannel",
        "[Reticle] AimTraceChannel", kDefaults.aim_trace_channel);

    out.position_enabled   = ReadBoolChecked(ini, "Position", "Enabled",
        "[Position] Enabled", out.position_enabled);
    out.position_sensitivity_x = ReadSensitivity(ini, "Position", "SensitivityX", out.position_sensitivity_x);
    out.position_sensitivity_y = ReadSensitivity(ini, "Position", "SensitivityY", out.position_sensitivity_y);
    out.position_sensitivity_z = ReadSensitivity(ini, "Position", "SensitivityZ", out.position_sensitivity_z);
    out.limit_x            = ReadPositionLimit(ini, "LimitX",     out.limit_x);
    out.limit_y            = ReadPositionLimit(ini, "LimitY",     out.limit_y);
    out.limit_z            = ReadPositionLimit(ini, "LimitZ",     out.limit_z);
    out.limit_z_back       = ReadPositionLimit(ini, "LimitZBack", out.limit_z_back);
    out.collision_enabled  = ReadBoolChecked(ini, "Position", "CollisionEnabled",
        "[Position] CollisionEnabled", out.collision_enabled);
    out.collision_radius   = guards::ReadFloatChecked(ini, "Position", "CollisionRadius",
        kDefaults.collision_radius, kMinCollisionRadius, kMaxCollisionRadius, &GuardLog);
    out.collision_channel  = ReadChannel(ini, "Position", "CollisionChannel",
        "[Position] CollisionChannel", kDefaults.collision_channel);
    out.collision_release_smoothing = guards::ReadFloatChecked(ini, "Position",
        "CollisionReleaseSmoothing", kDefaults.collision_release_smoothing, 0.0f, 1.0f, &GuardLog);
    guards::WarnRetiredSmoothingKey(ini, "Position", "Smoothing", &GuardLog);
    return true;
}

std::vector<Key> ReadKeys() {
    return {
        {"Network", "UdpPort"},
        {"General", "EnableOnStartup"},
        {"General", "WorldSpaceYaw"},
        {"Hotkeys", "YawModeKey"},
        {"Rotation", "YawSensitivity"},
        {"Rotation", "PitchSensitivity"},
        {"Rotation", "RollSensitivity"},
        {"Rotation", "InvertYaw"},
        {"Rotation", "InvertPitch"},
        {"Rotation", "InvertRoll"},
        {"Rotation", "LocalSmoothing"},
        {"Rotation", "RemoteSmoothing"},
        {"Camera", "FovOffset"},
        {"Reticle", "MoveCrosshair"},
        {"Reticle", "AimTraceChannel"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "CollisionEnabled"},
        {"Position", "CollisionRadius"},
        {"Position", "CollisionChannel"},
        {"Position", "CollisionReleaseSmoothing"},
    };
}

}  // namespace ecr_ht::legacy
