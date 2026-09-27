// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/hotkey_codec.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"

namespace ecr_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;
using ::cameraunlock::input::FormatKeyBindings;
using ::cameraunlock::input::KeyModifiers;

constexpr const wchar_t* kIniName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyIniName = L"HeadTracking.ini";

// data/games.json's display_name for ethan-carter-redux.
constexpr const char* kDisplayName = "The Vanishing of Ethan Carter Redux";

// A wider field of view than the game's own slider allows is the point of the
// row, so the bound is loose - but the offset lands in UE4's projection matrix,
// which divides by tan(FOV/2), and a typo of 600 for 60 would render a frame
// nobody can play through. Every earlier build held it to the same bounds.
constexpr double kMinFovOffset = -60.0;
constexpr double kMaxFovOffset = 60.0;

// ETraceTypeQuery is a TEnumAsByte the engine indexes into the project's trace
// channel array. A value outside a byte is not a channel at all, and the engine
// reads it without checking.
constexpr int kMaxTraceChannel = 255;

constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;

// The keys every build before the canonical format bound in code rather than in
// the file.
constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

void Save(const char* rows, const std::function<void(Config&)>& change) {
    // No owner when the bootstrap could not find the game's folder.
    if (!g_owner) {
        Log::Line("config: %s not saved: CameraUnlock.ini has no known folder this session", rows);
        return;
    }
    const cfg::ConfigSaveResult result = g_owner->Save(change);
    if (result.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("config: %s %s: %s", rows, cfg::ConfigSaveStatusName(result.status), result.reason.c_str());
    }
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    // Every earlier build opened HeadTracking.ini by its ANSI path, and the
    // frozen reader does the same. Where it finds no file, the published build
    // ran on its defaults.
    legacy::Config read;
    const bool present = legacy::Load(input.ansi_path, read);

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> pose_shaping;

    // Every sensitivity and inversion shipped at identity, and the axis
    // conversion to Unreal's frame is in camera_pose.cpp, so nothing folds: the
    // mod applies the pose as the tracker sends it, and a value the player
    // changed is dropped.
    const auto shaping = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, pose_shaping, dropped);
    };
    shaping(read.yaw_sensitivity, 1.0f, "Rotation", "YawSensitivity");
    shaping(read.pitch_sensitivity, 1.0f, "Rotation", "PitchSensitivity");
    shaping(read.roll_sensitivity, 1.0f, "Rotation", "RollSensitivity");
    shaping(read.invert_yaw, false, "Rotation", "InvertYaw");
    shaping(read.invert_pitch, false, "Rotation", "InvertPitch");
    shaping(read.invert_roll, false, "Rotation", "InvertRoll");
    shaping(read.position_sensitivity_x, 1.0f, "Position", "SensitivityX");
    shaping(read.position_sensitivity_y, 1.0f, "Position", "SensitivityY");
    shaping(read.position_sensitivity_z, 1.0f, "Position", "SensitivityZ");

    // The game's crosshair always follows the aim now, so MoveCrosshair=0 is a
    // reticle setting the approved change drops.
    if (!read.move_crosshair) dropped.push_back({cfg::DropRule::Reticle, "Reticle", "MoveCrosshair", "0"});

    // The reader keeps the port inside 1024-65535, every float finite and inside
    // a range the canonical rows hold, and both trace channels inside 0-255, so
    // each carries over as it is.
    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.fov_offset = read.fov_offset;
    out.aim_trace_channel = read.aim_trace_channel;
    out.collision_enabled = read.collision_enabled;
    out.collision_margin = read.collision_radius;
    out.collision_channel = read.collision_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;

    // [Position] Enabled chose the startup mode and nothing else: the cycle
    // reached every mode either way.
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(
        read.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                              : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = channels.rotation_enabled;
    out.position_enabled = channels.position_enabled;

    // The dev build had one vertical limit and bounded both directions with it.
    out.limit_x = read.limit_x;
    out.limit_y = read.limit_y;
    out.limit_y_down = read.limit_y;
    out.limit_z = read.limit_z;
    out.limit_z_back = read.limit_z_back;

    // End, Page Up and the Ctrl+Shift chords were bound in code; only the yaw
    // key was in the file, and the reader keeps it to a code IsBindableVirtualKey
    // accepts. A yaw key on Ctrl, Shift or Alt alone would be unbound (N3) and
    // keep the chord.
    out.toggle_key = FormatKeyBindings({{KeyModifiers::kNone, kVkEnd}, {kChord, kVkY}});
    out.cycle_tracking_mode_key = FormatKeyBindings({{KeyModifiers::kNone, kVkPageUp}, {kChord, kVkG}});
    const std::string yaw_key = cfg::LegacyVirtualKeyToBindings(read.yaw_mode_key, "Hotkeys", "YawModeKey", dropped);
    const std::string yaw_chord = FormatKeyBindings({{kChord, kVkH}});
    out.yaw_mode_key = yaw_key.empty() ? yaw_chord : yaw_key + ", " + yaw_chord;

    // A setting the player never changed from what the dev build shipped
    // follows Defaults.ini. The toggle and mode hotkeys were bound in code. The
    // collision margin and channel are the game's own and never follow it.
    const legacy::Config shipped;
    cfg::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, read.udp_port, shipped.udp_port);
    follows.Setting(Concept::EnableOnStartup, read.enable_on_startup, shipped.enable_on_startup);
    follows.Setting(Concept::WorldSpaceYaw, read.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(read.position_enabled, shipped.position_enabled);
    follows.Setting(Concept::LocalSmoothing, read.local_smoothing, shipped.local_smoothing);
    follows.Setting(Concept::RemoteSmoothing, read.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(Concept::PositionLimitX, read.limit_x, shipped.limit_x);
    follows.Setting(Concept::PositionLimitY, read.limit_y, shipped.limit_y);
    follows.Setting(Concept::PositionLimitYDown, read.limit_y, shipped.limit_y);
    follows.Setting(Concept::PositionLimitZ, read.limit_z, shipped.limit_z);
    follows.Setting(Concept::PositionLimitZBack, read.limit_z_back, shipped.limit_z_back);
    follows.Setting(Concept::CollisionEnabled, read.collision_enabled, shipped.collision_enabled);
    follows.Setting(Concept::CollisionReleaseSmoothing, read.collision_release_smoothing,
                    shipped.collision_release_smoothing);
    follows.NotInLegacy(Concept::ToggleKey);
    follows.NotInLegacy(Concept::CycleTrackingModeKey);
    follows.Setting(Concept::YawModeKey, read.yaw_mode_key, shipped.yaw_mode_key);

    return present ? cfg::ImportResult::Imported(std::move(dropped), std::move(pose_shaping), follows.Concepts())
                   : cfg::ImportResult::Absent(std::move(dropped), std::move(pose_shaping), follows.Concepts());
}

}  // namespace

cfg::ConfigTable<Config> Table() {
    cfg::ConfigTable<Config> table;
    table.Concept<Concept::UdpPort>(&Config::udp_port)
        .Concept<Concept::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<Concept::WorldSpaceYaw>(&Config::world_space_yaw)
        .Writable()
        .Concept<Concept::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Concept<Concept::LocalSmoothing>(&Config::local_smoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<Concept::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<Concept::PositionLimitX>(&Config::limit_x)
        .Concept<Concept::PositionLimitY>(&Config::limit_y)
        .Concept<Concept::PositionLimitYDown>(&Config::limit_y_down)
        .Concept<Concept::PositionLimitZ>(&Config::limit_z)
        .Concept<Concept::PositionLimitZBack>(&Config::limit_z_back)
        .Concept<Concept::CollisionEnabled>(&Config::collision_enabled)
        .Concept<Concept::CollisionMargin>(&Config::collision_margin)
        .Comment("How far the view is held off a wall when you lean into it, in centimetres. Keep it\n"
                 "above 10, the camera's near clip, or the wall is cut away before the view reaches it.")
        .Concept<Concept::CollisionChannel>(&Config::collision_channel)
        .Comment("Which of the game's collision channels the wall check tests against. 0 is Visibility.")
        .Engine()
        .Concept<Concept::CollisionReleaseSmoothing>(&Config::collision_release_smoothing)
        .Concept<Concept::ToggleKey>(&Config::toggle_key)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .Concept<Concept::YawModeKey>(&Config::yaw_mode_key)
        .Local("Camera", "FovOffset", &Config::fov_offset, cfg::FloatCodec(),
               "Degrees added to the game's field of view, -60 to 60. 0 leaves it as it is. The\n"
               "game has a Field of View slider in Options -> Graphics, and this adds to whatever\n"
               "you set there, so it can reach past that slider's range. The result is held\n"
               "between 40 and 150 degrees. The rendered view only: interaction, audio and\n"
               "streaming keep the game's own value.")
        .Range(kMinFovOffset, kMaxFovOffset)
        .Local("Camera", "AimTraceChannel", &Config::aim_trace_channel, cfg::IntCodec<int>(),
               "Which of the game's collision channels the aim ray runs on, to put the game's\n"
               "crosshair over what the aim is pointing at. 0 is Visibility.")
        .Range(0, kMaxTraceChannel)
        .Engine();
    return table;
}

cfg::RenderHeader Header() {
    cfg::RenderHeader header;
    header.display_name = kDisplayName;
    return header;
}

cfg::LegacyImport<Config> Import() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    for (const legacy::Key& key : legacy::ReadKeys()) import.keys.push_back({key.section, key.key});
    return import;
}

cfg::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = exe_dir + L"\\" + kIniName;
    options.table = Table();
    options.import = Import();
    options.legacy_path = exe_dir + L"\\" + kLegacyIniName;
    options.header = Header();
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    g_owner = std::make_unique<cfg::ConfigOwner<Config>>(OwnerOptions(exe_dir, std::move(defaults)));
    const cfg::ConfigLoadResult<Config> result = g_owner->Load();
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
    if (!result.reason.empty()) Log::Line("config: %s", result.reason.c_str());
    Log::Line("config: %s", cfg::ConfigLoadStatusName(result.status));
    return result.config;
}

cameraunlock::TrackingMode StartupTrackingMode(const Config& config) {
    const auto mode = cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled);
    if (!mode) throw std::logic_error("RotationEnabled and PositionEnabled are both false, which the table never gives");
    return *mode;
}

void SaveWorldSpaceYaw(bool world_space_yaw) {
    Save("[General] WorldSpaceYaw", [world_space_yaw](Config& c) { c.world_space_yaw = world_space_yaw; });
}

void SaveTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    Save("[General] RotationEnabled and [Position] PositionEnabled", [channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

}  // namespace ecr_ht::config
