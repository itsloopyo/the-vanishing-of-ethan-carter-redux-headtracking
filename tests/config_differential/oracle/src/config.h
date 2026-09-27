// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace ecr_ht {

struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;
    // true = yaw turns about the world up-axis (horizon-locked), false = about
    // the camera's own up-axis, which leans the view on pitched turns.
    bool world_space_yaw = true;

    int yaw_mode_key = 0x22;  // VK_NEXT (Page Down)

    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;

    // Two smoothing parameters, picked per connection from the packet source
    // address. Both cover rotation and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    // Degrees added to the game's own field of view, on the rendered view only.
    // The game ships an FOV slider of its own, so this is an offset rather than
    // an absolute: it stacks on whatever the player chose there (and so reaches
    // past that slider's range), and leaves any scripted FOV change intact.
    // 0 = the game's field of view is not touched at all.
    float fov_offset = 0.0f;

    // The game draws its own crosshair (Options -> Controls -> Display Dot
    // Crosshair, which cycles No / Smart / Always). Head tracking moves the
    // rendered eye off
    // the clean one that crosshair marks, so the mod puts it back over the aim
    // rather than drawing a second reticle beside it. 0 leaves the game's
    // crosshair exactly where the game draws it.
    bool move_crosshair = true;
    // ETraceTypeQuery index for the ray that finds what the aim is pointing at.
    // 0 is Visibility, which UE4's Pawn collision profile ignores, so the ray
    // leaves the player's own capsule without an ignore list.
    int aim_trace_channel = 0;

    bool position_enabled = true;
    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_z = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    // Lean collision: sweep the engine's own geometry from the clean eye toward
    // the lean target and cut the offset to whatever the level leaves room for.
    // Without it a 0.40m forward lean puts the rendered eye inside a doorframe
    // and the near plane culls the wood, so the player looks through the wall.
    bool collision_enabled = true;
    // Radius of the swept sphere, in UE units (cm). This IS the standoff held
    // off a surface, so it must exceed the camera's near clip distance (UE4's
    // default is 10) or the wall is culled before the eye reaches it.
    float collision_radius = 15.0f;
    // ETraceTypeQuery index for the sweep. Visibility for the same reason the
    // aim ray uses it: the Pawn profile ignores that channel, so the sweep does
    // not start inside the player's own capsule and report nowhere to go.
    int collision_channel = 0;
    // How quickly the allowance reopens once an obstruction clears. Tightening
    // is never smoothed - see cameraunlock::camera::LeanClamp.
    float collision_release_smoothing =
        cameraunlock::camera::LeanClampSettings{}.release_smoothing;
};

void LoadConfig(const std::string& exe_dir, Config& out);
void WriteDefaultConfigIfMissing(const std::string& exe_dir);

}
