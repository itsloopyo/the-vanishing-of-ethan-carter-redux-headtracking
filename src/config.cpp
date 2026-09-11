// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "fov_override.h"
#include "logging.h"

namespace ecr_ht {

namespace {

namespace guards = ::cameraunlock::config;

constexpr const char* kIniName = "HeadTracking.ini";

// The shipped defaults are the fallbacks a rejected value lands on, so the two
// cannot drift apart.
const Config kDefaults{};

// Boundary validation is core's: guards::ReadFloatChecked and the retired-key
// warning are shared with every other mod, and this sink is what puts the
// diagnostics in this mod's log.
void GuardLog(const char* fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    if (written < 0) return;
    Log::Line("%s", message);
}

std::string IniPath(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

// A wider field of view than the game's own slider allows is the point of the
// key, so the bound is loose - but the offset lands in UE4's projection matrix,
// which divides by tan(FOV/2), and a typo of 600 for 60 would render a frame
// nobody can play through, with no way back except editing this file again.
constexpr float kMaxFovOffset = 60.0f;

// Ports below 1024 need privilege to bind on some configurations and no tracker
// app offers one; 65535 is the end of the space. Outside that the value is not a
// port at all, and it must not reach Start(): the argument there is a uint16_t,
// so 70000 WRAPS to 4464 - bound, "listening", and deaf to a tracker aimed at
// 4242. Zero is worse than a wrap: bind(0) succeeds on an OS-assigned ephemeral
// port, so the log reports a healthy receiver that nothing can ever reach.
constexpr int kMinUdpPort = 1024;
constexpr int kMaxUdpPort = 65535;

// GetPrivateProfileIntA yields 0 rather than the supplied default for a
// present-but-unparseable value (ini_reader.h rule 4) and for a negative one, so
// garbage lands on exactly that ephemeral-port bind unless it is range-checked.
int SanitizeUdpPort(int value) {
    if (value >= kMinUdpPort && value <= kMaxUdpPort) return value;
    Log::Line("config: [Network] UdpPort %d is outside %d-%d, using %d",
              value, kMinUdpPort, kMaxUdpPort, kDefaults.udp_port);
    return kDefaults.udp_port;
}

// Every remaining numeric key goes through core's ReadFloatChecked rather than
// IniReader::ReadFloat. Two hazards, both reachable from one typo and neither
// caught anywhere downstream:
//
//   - strtod accepts "nan" and "inf" and overflows 1e400 to +inf. A non-finite
//     sensitivity multiplies through TrackingProcessor into a non-finite angle
//     that ComposeViewRotation writes into the engine's FRotator every frame,
//     and every comparison against NaN is false, so the camera limits are
//     skipped on the way past.
//   - ReadFloat parses a PREFIX, so "2,5" - a European decimal comma, which is
//     the expected user error - reads as 2. That is inside every valid range, so
//     it passes silently and the player gets a setting they never chose.
//
// A negative position limit is the third: PositionProcessor clamps with
// Clamp(v, -limit, +limit), and with limit < 0 the bounds invert and every input
// returns the lower one, pinning the lean at a fixed offset.
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

// The standoff has to clear UE4's 10-unit near clip plane or the wall it holds
// the eye off is culled anyway and the player still sees through it; past a
// couple of metres the sphere is wider than the corridors it sweeps and the lean
// never opens at all.
constexpr float kMinCollisionRadius = 11.0f;
constexpr float kMaxCollisionRadius = 200.0f;

// ETraceTypeQuery is a TEnumAsByte the engine indexes into the project's trace
// channel array. A value outside a byte is not a channel at all, and the engine
// reads it without checking.
constexpr int kMaxTraceChannel = 255;

// A whole-token integer, so a value the parser cannot read is refused out loud
// instead of becoming a number that happens to be valid.
//
// IniReader::ReadInt cannot do this: it yields 0 on a present-but-unparseable
// value (ini_reader.h rule 4), and 0 is Visibility - a real channel, and the
// default for both of the keys below. So `CollisionChannel=0x03` (hex, which
// GetPrivateProfileIntA does not accept, and which the generated file teaches by
// writing YawModeKey=0x22) silently kept sweeping on Visibility with nothing in
// the log, and the sanitizer's rejection branch was unreachable for every
// mistyped input.
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

// A bool that says so when it cannot read the value.
//
// IniReader::ReadBool matches the WHOLE value string against a fixed set and
// returns the DEFAULT for anything else (ini_reader.h rule 2), with no
// diagnostic. Six of the eight bool keys here default to true, so
// `CollisionEnabled=0 ; off while testing` - and the generated file is full of
// comment lines teaching that syntax - was read as the opposite of what the
// player wrote, and the startup line then reported the value they did not
// choose. ReadRawValue truncates at ';' or '#' and trims, which is what makes
// the commented form work at all.
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

// GetAsyncKeyState only defines 0x01..0xFE, and the modifiers are what the
// chord guard itself tests, so a binding on one either never fires or fires on
// every chord. Either way the key would silently do nothing.
int SanitizeYawModeKey(int vk) {
    if (guards::IsBindableVirtualKey(vk)) return vk;
    Log::Line("config: [Hotkeys] YawModeKey 0x%02X is not a bindable virtual-key code, using 0x%02X",
              vk, kDefaults.yaw_mode_key);
    return kDefaults.yaw_mode_key;
}

}

void LoadConfig(const std::string& exe_dir, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(IniPath(exe_dir))) {
        Log::Line("config: %s could not be opened - built-in defaults are used.", kIniName);
        return;
    }

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

    // The two fallbacks differ on purpose: a malformed RemoteSmoothing must not
    // drop back to the LOCAL default, which would leave a phone on WiFi running
    // with no smoothing at all on raw network jitter. [0, 1] is the whole
    // meaningful domain of CalculateSmoothingFactor - validation, never a floor,
    // so a configured 0 stays 0.
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
    // No position smoothing key: position uses the same LocalSmoothing /
    // RemoteSmoothing pair as rotation.
    guards::WarnRetiredSmoothingKey(ini, "Position", "Smoothing", &GuardLog);
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
