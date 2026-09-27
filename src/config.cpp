// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cerrno>
#include <cstdio>
#include <string>
#include <windows.h>

#include "fov_override.h"
#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace ecr_ht {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

const Config kDefaults{};

std::string IniPath(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

// The bounds the frozen reader in src/legacy_config/ holds each key to, named
// again here for the comments of the file WriteDefaultConfigIfMissing writes.
constexpr float kMaxFovOffset = 60.0f;
constexpr int kMinUdpPort = 1024;
constexpr int kMaxUdpPort = 65535;
constexpr float kMinCollisionRadius = 11.0f;
constexpr float kMaxCollisionRadius = 200.0f;

}

void LoadConfig(const std::string& exe_dir, Config& out) {
    legacy::Config read;
    if (!legacy::Load(IniPath(exe_dir), read)) {
        Log::Line("config: %s could not be opened - built-in defaults are used.", kIniName);
        return;
    }

    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.yaw_mode_key = read.yaw_mode_key;
    out.yaw_sensitivity = read.yaw_sensitivity;
    out.pitch_sensitivity = read.pitch_sensitivity;
    out.roll_sensitivity = read.roll_sensitivity;
    out.invert_yaw = read.invert_yaw;
    out.invert_pitch = read.invert_pitch;
    out.invert_roll = read.invert_roll;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.fov_offset = read.fov_offset;
    out.move_crosshair = read.move_crosshair;
    out.aim_trace_channel = read.aim_trace_channel;
    out.position_enabled = read.position_enabled;
    out.position_sensitivity_x = read.position_sensitivity_x;
    out.position_sensitivity_y = read.position_sensitivity_y;
    out.position_sensitivity_z = read.position_sensitivity_z;
    out.limit_x = read.limit_x;
    out.limit_y = read.limit_y;
    out.limit_z = read.limit_z;
    out.limit_z_back = read.limit_z_back;
    out.collision_enabled = read.collision_enabled;
    out.collision_radius = read.collision_radius;
    out.collision_channel = read.collision_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;
}

void WriteDefaultConfigIfMissing(const std::string& exe_dir) {
    const std::string path = IniPath(exe_dir);
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    FILE* file = nullptr;
    // "wx" - create, never truncate. The attribute check above is a separate
    // syscall, so between it and this open a second launch of the game (or the
    // player) can have written the file, and a plain "w" would truncate away the
    // settings they are about to be told to edit.
    const errno_t opened = fopen_s(&file, path.c_str(), "wx");
    if (!file) {
        if (opened == EEXIST) return;
        Log::Line("config: could not create %s - built-in defaults are used, and "
                  "settings changed in that file will not be picked up.", kIniName);
        return;
    }
    std::fprintf(file,
        "; The Vanishing of Ethan Carter Redux Head Tracking - configuration\n"
        "; Edit values, restart the game to apply.\n\n"
        "[Network]\n"
        "; UDP port the tracker sends to. Accepted %d to %d.\n"
        "UdpPort=%d\n\n"
        "[General]\n"
        "EnableOnStartup=1\n"
        "; Yaw mode: 1 = horizon-locked yaw (default), 0 = camera-local yaw.\n"
        "; Toggled in game with Page Down or Ctrl+Shift+H.\n"
        "WorldSpaceYaw=1\n\n"
        "[Hotkeys]\n"
        "; Virtual-key code for the yaw-mode toggle. 0x%02X = Page Down.\n"
        "YawModeKey=0x%02X\n\n"
        "[Rotation]\n"
        "YawSensitivity=1.0\n"
        "PitchSensitivity=1.0\n"
        "RollSensitivity=1.0\n"
        "InvertYaw=0\n"
        "InvertPitch=0\n"
        "InvertRoll=0\n"
        "; Smoothing applied when the tracker runs on this machine (loopback).\n"
        "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n"
        "LocalSmoothing=%.1f\n"
        "; Smoothing applied when the tracker is a remote device on the network.\n"
        "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n"
        "RemoteSmoothing=%.2f\n\n"
        "[Camera]\n"
        "; Degrees added to the game's own field of view. The game has a Field of\n"
        "; View slider in Options -> Graphics, and this adds to whatever you set\n"
        "; there, so it can reach past that slider's range. 0 = leave the game's\n"
        "; field of view exactly as it is. Accepted -%.0f to +%.0f; the result is held\n"
        "; between %.0f and %.0f degrees. The rendered view only - interaction traces,\n"
        "; audio and streaming keep the game's own value.\n"
        "FovOffset=0\n\n"
        "[Position]\n"
        "Enabled=1\n"
        "SensitivityX=1.0\n"
        "SensitivityY=1.0\n"
        "SensitivityZ=1.0\n"
        "LimitX=%.2f\n"
        "LimitY=%.2f\n"
        "LimitZ=%.2f\n"
        "LimitZBack=%.2f\n"
        "; Lean collision. The mod sweeps the level from the camera the game put\n"
        "; there toward where your head wants to go and cuts the lean to whatever\n"
        "; the room leaves, so leaning into a wall stops at the wall instead of\n"
        "; putting the view inside it. 0 turns that off.\n"
        "CollisionEnabled=1\n"
        "; How far off a surface the view is held, in centimetres. Accepted %.0f to %.0f.\n"
        "CollisionRadius=%.0f\n"
        "; Which collision channel the sweep runs on. 0 is Visibility.\n"
        "CollisionChannel=%d\n"
        "; How quickly the lean opens back up once you step clear of something.\n"
        "; 0 = instantly, 1 = very slowly. Leaning INTO something always stops at once.\n"
        "CollisionReleaseSmoothing=%.2f\n\n"
        "[Reticle]\n"
        "; The game draws its own crosshair (Options -> Controls -> Display Dot\n"
        "; Crosshair). Head tracking moves the view off the direction you are\n"
        "; actually pointing, so the mod moves that crosshair onto the point your\n"
        "; look and interaction ray really hits. 0 leaves the crosshair fixed at\n"
        "; the centre of the frame.\n"
        "MoveCrosshair=1\n"
        "; Which collision channel the aim ray runs on. 0 is Visibility.\n"
        "AimTraceChannel=%d\n",
        kMinUdpPort, kMaxUdpPort, kDefaults.udp_port,
        kDefaults.yaw_mode_key, kDefaults.yaw_mode_key,
        kDefaults.local_smoothing, kDefaults.remote_smoothing,
        kMaxFovOffset, kMaxFovOffset, fov::kMinApplied, fov::kMaxApplied,
        kDefaults.limit_x, kDefaults.limit_y, kDefaults.limit_z, kDefaults.limit_z_back,
        kMinCollisionRadius, kMaxCollisionRadius, kDefaults.collision_radius,
        kDefaults.collision_channel, kDefaults.collision_release_smoothing,
        kDefaults.aim_trace_channel);
    // A write that fails part way (a full disk, a read-only game directory that
    // only refused at write time) leaves a TRUNCATED file behind, and a truncated
    // INI is the worst outcome available: it parses, the keys that survived are
    // honoured, and every key past the cut silently takes its built-in default.
    // The player then edits a file that never held what they think it held.
    const bool write_failed = std::ferror(file) != 0;
    const bool close_failed = std::fclose(file) != 0;
    if (write_failed || close_failed) {
        Log::Line("config: %s was only partly written - it is INCOMPLETE and keys "
                  "missing from it fall back to built-in defaults. Delete it and "
                  "relaunch to have it written again.", kIniName);
    }
}

}
