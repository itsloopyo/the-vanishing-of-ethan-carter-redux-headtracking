// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The dev pre-release's reader and startup code (tag dev, commit 3db7b5e, the
// only build this repo has published).
//
// src/config.cpp, src/config.h, src/logging.h, src/fov_override.h and
// src/ue_abi.h beside this file are byte copies of the dev build's, compiled
// here as they shipped, with their namespace renamed by the macro below so they
// can sit in one program beside this build's ecr_ht. Every cameraunlock-core
// source they include holds the same bytes at the dev build's pin (ee8cc72) and
// at this repo's (CMakeLists.txt checks both). What is transcribed is the
// startup code that consumed the settings, which cannot be compiled into a test
// because it hooks the game, all from src/headtracking_mod.cpp:
//
//   lines 432-444  RegisterHotkeys, the registrations as data
//   lines 467-513  ApplyConfigToSession
//   lines 537-549  InitConfig
//   lines 209, 681 the crosshair projection and HUD detour behind MoveCrosshair

#define ecr_ht ecr_ht_dev
#include "src/config.cpp"
#undef ecr_ht

#include "oracle_reader.h"

namespace ecr_oracle {

namespace {

constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;

constexpr unsigned kNav = 0;
constexpr unsigned kChord = 3;

}  // namespace

Published Read(const std::string& exe_dir) {
    ecr_ht_dev::Config g_config;
    ecr_ht_dev::WriteDefaultConfigIfMissing(exe_dir);
    ecr_ht_dev::LoadConfig(exe_dir, g_config);

    Published p;
    p.udp_port = g_config.udp_port;
    p.tracking_enabled = g_config.enable_on_startup;
    p.world_space_yaw = g_config.world_space_yaw;

    p.yaw_sens = g_config.yaw_sensitivity;
    p.pitch_sens = g_config.pitch_sensitivity;
    p.roll_sens = g_config.roll_sensitivity;
    p.invert_yaw = g_config.invert_yaw;
    p.invert_pitch = g_config.invert_pitch;
    p.invert_roll = g_config.invert_roll;
    p.local_smoothing = g_config.local_smoothing;
    p.remote_smoothing = g_config.remote_smoothing;

    p.aim_trace_channel = g_config.aim_trace_channel;
    p.collision_channel = g_config.collision_channel;
    p.collision_radius = g_config.collision_radius;
    p.collision_release_smoothing = g_config.collision_release_smoothing;
    p.collision_enabled = g_config.collision_enabled;

    p.pos_sens_x = g_config.position_sensitivity_x;
    p.pos_sens_y = g_config.position_sensitivity_y;
    p.pos_sens_z = g_config.position_sensitivity_z;
    p.limit_x = g_config.limit_x;
    p.limit_y = g_config.limit_y;
    p.limit_y_down = g_config.limit_y;
    p.limit_z = g_config.limit_z;
    p.limit_z_back = g_config.limit_z_back;

    p.tracking_mode = g_config.position_enabled ? 0 : 1;

    p.fov_offset = g_config.fov_offset;
    p.crosshair_follows_aim = g_config.move_crosshair;

    p.hotkeys.push_back({kToggle, kVkEnd, kNav});
    p.hotkeys.push_back({kCycleMode, kVkPageUp, kNav});
    p.hotkeys.push_back({kYawMode, g_config.yaw_mode_key, kNav});
    p.hotkeys.push_back({kToggle, kVkY, kChord});
    p.hotkeys.push_back({kCycleMode, kVkG, kChord});
    p.hotkeys.push_back({kYawMode, kVkH, kChord});
    return p;
}

}  // namespace ecr_oracle
