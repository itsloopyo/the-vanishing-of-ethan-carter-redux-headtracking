// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/config/config_concepts.g.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace ecr_ht {

// The settings CameraUnlock.ini holds, at their defaults.
struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;
    // true = yaw turns about the world up-axis (horizon-locked), false = about
    // the camera's own up-axis, which leans the view on pitched turns.
    bool world_space_yaw = true;

    // The tracking mode at startup, the pair the mode hotkey saves.
    bool rotation_enabled = true;
    bool position_enabled = true;

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

    // ETraceTypeQuery index for the ray that finds what the aim is pointing at,
    // which puts the game's crosshair back over the aim. 0 is Visibility, which
    // UE4's Pawn collision profile ignores, so the ray leaves the player's own
    // capsule without an ignore list.
    int aim_trace_channel = 0;

    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
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
    float collision_margin = 15.0f;
    // ETraceTypeQuery index for the sweep. Visibility for the same reason the
    // aim ray uses it: the Pawn profile ignores that channel, so the sweep does
    // not start inside the player's own capsule and report nowhere to go.
    int collision_channel = 0;
    // How quickly the allowance reopens once an obstruction clears. Tightening
    // is never smoothed - see cameraunlock::camera::LeanClamp.
    float collision_release_smoothing =
        cameraunlock::camera::LeanClampSettings{}.release_smoothing;

    std::string toggle_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault;
    std::string cycle_tracking_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::CycleTrackingModeKey>::kCanonicalDefault;
    std::string yaw_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::YawModeKey>::kCanonicalDefault;
};

}  // namespace ecr_ht

// CameraUnlock.ini, beside the game exe, in cameraunlock-core's canonical config
// format. One ConfigOwner reads and writes it; nothing else in the mod touches
// it. HeadTracking.ini, the file every earlier build read, is imported once
// while CameraUnlock.ini is absent and is never written.
namespace ecr_ht::config {

cameraunlock::config::ConfigTable<Config> Table();

cameraunlock::config::RenderHeader Header();

// HeadTracking.ini through the frozen reader in src/legacy_config/, mapped into
// Config.
cameraunlock::config::LegacyImport<Config> Import();

// The owner's options for CameraUnlock.ini in `exe_dir`, a full path, with
// HeadTracking.ini beside it as the legacy file and Defaults.ini where
// `defaults` says.
cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir,
                                                              cameraunlock::config::DefaultsFile defaults);

// Reads, imports or creates CameraUnlock.ini in `exe_dir`, logs what the owner
// reports, and returns the settings the session runs on. Call once, from the
// bootstrap thread, with the log open. `defaults` is DefaultsFile::PerUser() in
// the mod.
Config Load(const std::wstring& exe_dir, cameraunlock::config::DefaultsFile defaults);

// The tracking mode the settings start in. The table never gives both rows
// false.
cameraunlock::TrackingMode StartupTrackingMode(const Config& config);

// Saves the value a hotkey has just applied. The session keeps it whether or
// not the save succeeds; a failed save is logged. Called on the hotkey thread.
void SaveWorldSpaceYaw(bool world_space_yaw);
void SaveTrackingMode(cameraunlock::TrackingMode mode);

}  // namespace ecr_ht::config
