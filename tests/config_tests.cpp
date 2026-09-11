// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Characterization tests over HeadTracking.ini: what the shipped defaults are,
// what a written file parses back to, and what happens to values the user got
// wrong. These pin the behaviour the mod had before boundary validation moved
// onto cameraunlock-core's shared guards.
//
// Each case gets its OWN directory. The INI name is fixed, and
// GetPrivateProfileString caches by path, so reusing one directory would serve a
// later case the earlier case's contents.

#include <cstdio>
#include <string>

#include <windows.h>

#include "config.h"
#include "test_support.h"

namespace {

using namespace ecr_ht;
using namespace ecr_ht::tests;

std::string MakeCaseDir(const char* case_name) {
    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    std::string dir = std::string(temp) + "ecr_ht_tests\\" +
                      std::to_string(GetCurrentProcessId()) + "_" + case_name;
    CreateDirectoryA((std::string(temp) + "ecr_ht_tests").c_str(), nullptr);
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

// Counts fixtures that did not reach disk. Every negative case here asserts
// "a bad value falls back to the default", and a fixture that was never written
// produces exactly that outcome with the whole validation layer removed - so a
// restrictive %TEMP% would turn the suite green while testing nothing. Checked
// once at the end of the run.
int g_iniWriteFailures = 0;

void WriteIni(const std::string& dir, const char* body) {
    const std::string path = dir + "\\HeadTracking.ini";
    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "w");
    if (!file) { ++g_iniWriteFailures; return; }
    std::fputs(body, file);
    std::fclose(file);

    // Read it back rather than trusting the write: this is the only thing
    // standing between a broken harness and a suite that cannot fail.
    FILE* check = nullptr;
    fopen_s(&check, path.c_str(), "r");
    if (!check) { ++g_iniWriteFailures; return; }
    char first = 0;
    const size_t read = std::fread(&first, 1, 1, check);
    std::fclose(check);
    if (read != 1 || first != body[0]) ++g_iniWriteFailures;
}

Config LoadFrom(const std::string& dir) {
    Config config;
    LoadConfig(dir, config);
    return config;
}

void TestShippedDefaults(Suite& suite) {
    const Config defaults;
    suite.Check(defaults.udp_port == 4242, "the default UDP port is the OpenTrack standard 4242");
    suite.Check(defaults.enable_on_startup, "tracking is enabled on startup by default");
    suite.Check(defaults.world_space_yaw, "world-space yaw is the default yaw mode");
    suite.Check(defaults.yaw_mode_key == 0x22, "the yaw-mode key defaults to Page Down");
    suite.Check(defaults.local_smoothing == 0.0f, "LocalSmoothing defaults to 0");
    suite.Check(Near(defaults.remote_smoothing, 0.15f, 1e-6), "RemoteSmoothing defaults to 0.15");
    suite.Check(defaults.fov_offset == 0.0f, "FovOffset defaults to 0, leaving the game's FOV alone");
    suite.Check(defaults.position_enabled, "position tracking is on by default");
    suite.Check(Near(defaults.limit_z, 0.40f, 1e-6), "the forward lean budget is the generous 0.40m");
    suite.Check(Near(defaults.limit_z_back, 0.10f, 1e-6), "the backward lean budget is the tighter 0.10m");
    suite.Check(defaults.limit_z > defaults.limit_z_back,
                "leaning in has more travel than leaning back");
}

void TestAbsentFileKeepsDefaults(Suite& suite) {
    const std::string dir = MakeCaseDir("absent");
    const Config config = LoadFrom(dir);
    suite.Check(config.udp_port == 4242, "a missing INI leaves the defaults in place");
}

void TestWrittenDefaultsRoundTrip(Suite& suite) {
    // The generated file is the one almost every player actually runs with, so
    // it has to parse back to exactly the built-in defaults.
    const std::string dir = MakeCaseDir("roundtrip");
    WriteDefaultConfigIfMissing(dir);

    // Every assertion below compares against the SAME defaults the mod falls
    // back to, so a file that was never written passes all of them with the
    // whole config layer removed. g_iniWriteFailures cannot catch that - this
    // case does not go through WriteIni - so the file is checked directly.
    FILE* generated = nullptr;
    fopen_s(&generated, (dir + "\\HeadTracking.ini").c_str(), "r");
    long generated_size = 0;
    if (generated) {
        std::fseek(generated, 0, SEEK_END);
        generated_size = std::ftell(generated);
        std::fclose(generated);
    }
    suite.Check(generated_size > 500,
                "the default config was actually generated, so the round-trip means something");

    const Config defaults;
    const Config config = LoadFrom(dir);

    suite.Check(config.udp_port == defaults.udp_port, "written UdpPort round-trips");
    suite.Check(config.yaw_mode_key == defaults.yaw_mode_key, "written YawModeKey round-trips");
    suite.Check(config.local_smoothing == defaults.local_smoothing,
                "written LocalSmoothing round-trips");
    suite.Check(Near(config.remote_smoothing, defaults.remote_smoothing, 1e-6),
                "written RemoteSmoothing round-trips");
    suite.Check(config.fov_offset == defaults.fov_offset, "written FovOffset round-trips");
    suite.Check(Near(config.limit_x, defaults.limit_x, 1e-6), "written LimitX round-trips");
    suite.Check(Near(config.limit_y, defaults.limit_y, 1e-6), "written LimitY round-trips");
    suite.Check(Near(config.limit_z, defaults.limit_z, 1e-6), "written LimitZ round-trips");
    suite.Check(Near(config.limit_z_back, defaults.limit_z_back, 1e-6),
                "written LimitZBack round-trips");
    suite.Check(config.world_space_yaw == defaults.world_space_yaw,
                "written WorldSpaceYaw round-trips");
    suite.Check(config.position_enabled == defaults.position_enabled,
                "written Position Enabled round-trips");
    // The keys the checked readers now handle. Nothing else asserts that the
    // generated file parses back through them.
    suite.Check(config.move_crosshair == defaults.move_crosshair,
                "written MoveCrosshair round-trips");
    suite.Check(config.enable_on_startup == defaults.enable_on_startup,
                "written EnableOnStartup round-trips");
    suite.Check(config.aim_trace_channel == defaults.aim_trace_channel,
                "written AimTraceChannel round-trips");
    suite.Check(config.collision_enabled == defaults.collision_enabled,
                "written CollisionEnabled round-trips");
    suite.Check(config.collision_channel == defaults.collision_channel,
                "written CollisionChannel round-trips");
    suite.Check(Near(config.collision_radius, defaults.collision_radius, 1e-6),
                "written CollisionRadius round-trips");
    suite.Check(Near(config.collision_release_smoothing, defaults.collision_release_smoothing, 1e-6),
                "written CollisionReleaseSmoothing round-trips");
}

void TestExistingFileIsNotOverwritten(Suite& suite) {
    const std::string dir = MakeCaseDir("preserve");
    WriteIni(dir, "[Network]\nUdpPort=5000\n");
    WriteDefaultConfigIfMissing(dir);
    suite.Check(LoadFrom(dir).udp_port == 5000,
                "an existing config is left alone rather than reset to defaults");
}

void TestValuesAreRead(Suite& suite) {
    const std::string dir = MakeCaseDir("values");
    WriteIni(dir,
        "[Network]\nUdpPort=4999\n"
        "[General]\nEnableOnStartup=0\nWorldSpaceYaw=0\n"
        "[Hotkeys]\nYawModeKey=0x24\n"
        "[Rotation]\nYawSensitivity=1.5\nInvertPitch=1\n"
        "LocalSmoothing=0.5\nRemoteSmoothing=0.25\n"
        "[Camera]\nFovOffset=15\n"
        "[Position]\nEnabled=0\nLimitZ=0.35\n");
    const Config config = LoadFrom(dir);

    suite.Check(config.udp_port == 4999, "UdpPort is read");
    suite.Check(!config.enable_on_startup, "EnableOnStartup=0 is read");
    suite.Check(!config.world_space_yaw, "WorldSpaceYaw=0 is read");
    suite.Check(config.yaw_mode_key == 0x24, "a valid YawModeKey is honoured");
    suite.Check(Near(config.yaw_sensitivity, 1.5f, 1e-6), "YawSensitivity is read");
    suite.Check(config.invert_pitch, "InvertPitch=1 is read");
    suite.Check(Near(config.local_smoothing, 0.5f, 1e-6), "LocalSmoothing is read");
    suite.Check(Near(config.remote_smoothing, 0.25f, 1e-6), "RemoteSmoothing is read");
    suite.Check(Near(config.fov_offset, 15.0f, 1e-6), "FovOffset is read");
    suite.Check(!config.position_enabled, "Position Enabled=0 is read");
    suite.Check(Near(config.limit_z, 0.35f, 1e-6), "LimitZ is read");
}

void TestSmoothingIsValidatedNotFloored(Suite& suite) {
    const std::string dir = MakeCaseDir("smoothing");
    WriteIni(dir,
        "[Rotation]\n"
        "LocalSmoothing=0.0\n"
        "RemoteSmoothing=5.0\n");
    const Config config = LoadFrom(dir);

    // The whole point of the two-parameter model: a configured 0 stays 0. A
    // floor here would silently override the user.
    suite.Check(config.local_smoothing == 0.0f, "a configured LocalSmoothing of 0 stays 0");
    suite.Check(config.remote_smoothing == 1.0f, "a smoothing above 1 is clamped to 1");
}

void TestNonFiniteSmoothingFallsBackPerKey(Suite& suite) {
    // A malformed RemoteSmoothing must NOT land on the local default, which
    // would leave a phone on WiFi with no smoothing at all.
    const std::string dir = MakeCaseDir("smoothing_nan");
    WriteIni(dir,
        "[Rotation]\n"
        "LocalSmoothing=nan\n"
        "RemoteSmoothing=nan\n");
    const Config config = LoadFrom(dir);

    const Config defaults;
    suite.Check(config.local_smoothing == defaults.local_smoothing,
                "a non-finite LocalSmoothing falls back to the local default");
    suite.Check(Near(config.remote_smoothing, defaults.remote_smoothing, 1e-6),
                "a non-finite RemoteSmoothing falls back to the REMOTE default");
}

void TestNegativeSmoothingIsClamped(Suite& suite) {
    // RemoteSmoothing rather than LocalSmoothing: its default is 0.15, so a
    // clamp to 0 is distinguishable from a fallback to the default. Asserting
    // this on LocalSmoothing cannot fail - the clamp target and the fallback
    // are both 0.0.
    const std::string dir = MakeCaseDir("smoothing_neg");
    WriteIni(dir, "[Rotation]\nRemoteSmoothing=-1.0\n");
    const Config config = LoadFrom(dir);
    suite.Check(config.remote_smoothing == 0.0f,
                "a negative smoothing is clamped to 0, not replaced by the key's default");
    suite.Check(config.remote_smoothing != Config{}.remote_smoothing,
                "and the clamp is telling itself apart from the 0.15 fallback");
}

// A bool key with a trailing comment. GetPrivateProfileString does not treat
// ';' as a comment introducer, so the whole of "0 ; off while testing" reaches
// the parser; IniReader::ReadBool matches the entire string against a fixed set
// and hands back the DEFAULT for anything else. Six of the eight bool keys here
// default to true, so the user's 0 was read as the opposite of what they wrote,
// and the startup log then reported the value they had not chosen. The
// generated ini is full of ';' comment lines teaching exactly this syntax.
void TestTrailingCommentOnABoolIsHonoured(Suite& suite) {
    const std::string dir = MakeCaseDir("bool_comment");
    WriteIni(dir,
        "[Position]\nCollisionEnabled=0 ; off while I test the lean\n"
        "[Reticle]\nMoveCrosshair=0 # leave the crosshair alone\n");
    const Config config = LoadFrom(dir);
    suite.Check(!config.collision_enabled,
                "CollisionEnabled=0 with a trailing comment turns collision OFF");
    suite.Check(!config.move_crosshair,
                "MoveCrosshair=0 with a trailing comment turns the crosshair move OFF");
}

void TestUnreadableBoolFallsBackToTheDefault(Suite& suite) {
    const std::string dir = MakeCaseDir("bool_garbage");
    WriteIni(dir, "[Position]\nCollisionEnabled=maybe\n");
    suite.Check(LoadFrom(dir).collision_enabled == Config{}.collision_enabled,
                "a bool the mod cannot read falls back to the shipped default");

    const std::string yes = MakeCaseDir("bool_words");
    WriteIni(yes, "[Position]\nEnabled=off\n[General]\nEnableOnStartup=FALSE\n");
    const Config config = LoadFrom(yes);
    suite.Check(!config.position_enabled, "\"off\" reads as false");
    suite.Check(!config.enable_on_startup, "\"FALSE\" reads as false, case-insensitively");
}

// The channels are the keys with the most direct engine consequence in the
// file, and both default to 0. ReadInt yields 0 on a present-but-unparseable
// value, which IS a valid channel, so a typo used to become Visibility with
// nothing in the log and the range check never reached.
void TestTraceChannelsAreWholeTokens(Suite& suite) {
    const std::string good = MakeCaseDir("channel_ok");
    WriteIni(good, "[Reticle]\nAimTraceChannel=3\n[Position]\nCollisionChannel=2\n");
    const Config config = LoadFrom(good);
    suite.Check(config.aim_trace_channel == 3, "a valid AimTraceChannel is read");
    suite.Check(config.collision_channel == 2, "a valid CollisionChannel is read");

    const std::string comment = MakeCaseDir("channel_comment");
    WriteIni(comment, "[Position]\nCollisionChannel=3 ; camera channel\n");
    suite.Check(LoadFrom(comment).collision_channel == 3,
                "a channel with a trailing comment is still read");

    const std::string partial = MakeCaseDir("channel_partial");
    WriteIni(partial, "[Position]\nCollisionChannel=0x03\n");
    suite.Check(LoadFrom(partial).collision_channel == Config{}.collision_channel,
                "a hex channel is refused rather than silently becoming something else");

    const std::string high = MakeCaseDir("channel_high");
    WriteIni(high, "[Reticle]\nAimTraceChannel=300\n");
    suite.Check(LoadFrom(high).aim_trace_channel == Config{}.aim_trace_channel,
                "a channel outside a byte is refused - the engine indexes it unchecked");

    const std::string negative = MakeCaseDir("channel_neg");
    WriteIni(negative, "[Reticle]\nAimTraceChannel=-1\n");
    suite.Check(LoadFrom(negative).aim_trace_channel == Config{}.aim_trace_channel,
                "a negative channel is refused");
}

// The standoff must clear UE4's 10-unit near plane or the wall it holds the eye
// off is culled anyway and the player still sees through it.
void TestCollisionValuesAreBounded(Suite& suite) {
    const std::string low = MakeCaseDir("radius_low");
    WriteIni(low, "[Position]\nCollisionRadius=5\n");
    suite.Check(Near(LoadFrom(low).collision_radius, 11.0f, 1e-6),
                "a radius inside the near clip plane is raised to clear it");

    const std::string high = MakeCaseDir("radius_high");
    WriteIni(high, "[Position]\nCollisionRadius=5000\n");
    suite.Check(Near(LoadFrom(high).collision_radius, 200.0f, 1e-6),
                "a radius wider than any corridor is held at the maximum");

    const std::string release = MakeCaseDir("release_high");
    WriteIni(release, "[Position]\nCollisionReleaseSmoothing=2\n");
    suite.Check(Near(LoadFrom(release).collision_release_smoothing, 1.0f, 1e-6),
                "the release smoothing is held inside 0-1");
}

void TestFovOffsetIsBounded(Suite& suite) {
    const std::string dir = MakeCaseDir("fov_high");
    WriteIni(dir, "[Camera]\nFovOffset=600\n");
    suite.Check(Near(LoadFrom(dir).fov_offset, 60.0f, 1e-6),
                "a mistyped FovOffset of 600 is held at +60");

    const std::string low = MakeCaseDir("fov_low");
    WriteIni(low, "[Camera]\nFovOffset=-600\n");
    suite.Check(Near(LoadFrom(low).fov_offset, -60.0f, 1e-6),
                "a mistyped FovOffset of -600 is held at -60");
}

void TestUnbindableYawModeKeyFallsBack(Suite& suite) {
    const Config defaults;

    const std::string outOfRange = MakeCaseDir("vk_range");
    WriteIni(outOfRange, "[Hotkeys]\nYawModeKey=0x230\n");
    suite.Check(LoadFrom(outOfRange).yaw_mode_key == defaults.yaw_mode_key,
                "a YawModeKey outside 0x01-0xFE falls back to the default");

    // A modifier can never fire as a binding: Ctrl and Shift are what the chord
    // guard itself tests.
    const std::string modifier = MakeCaseDir("vk_modifier");
    WriteIni(modifier, "[Hotkeys]\nYawModeKey=0x11\n");
    suite.Check(LoadFrom(modifier).yaw_mode_key == defaults.yaw_mode_key,
                "a YawModeKey on a modifier falls back to the default");
}

void TestUdpPortIsRangeChecked(Suite& suite) {
    // The port is handed to Start() as a uint16_t, so an out-of-range value does
    // not fail - it WRAPS. 70000 binds 4464 and the tracker aimed at 4242 is
    // never heard, with "listening" in the log.
    const Config defaults;

    const std::string high = MakeCaseDir("port_high");
    WriteIni(high, "[Network]\nUdpPort=70000\n");
    suite.Check(LoadFrom(high).udp_port == defaults.udp_port,
                "a UdpPort past 65535 falls back to the default rather than wrapping");

    // Port 0 binds an OS-assigned ephemeral port: bound, running, unreachable.
    const std::string zero = MakeCaseDir("port_zero");
    WriteIni(zero, "[Network]\nUdpPort=0\n");
    suite.Check(LoadFrom(zero).udp_port == defaults.udp_port,
                "UdpPort=0 falls back rather than binding an ephemeral port");

    // GetPrivateProfileIntA yields 0 for a present-but-unparseable value, so
    // garbage reaches the same ephemeral-port bind unless it is range-checked.
    const std::string garbage = MakeCaseDir("port_garbage");
    WriteIni(garbage, "[Network]\nUdpPort=not-a-port\n");
    suite.Check(LoadFrom(garbage).udp_port == defaults.udp_port,
                "an unparseable UdpPort falls back to the default");

    const std::string negative = MakeCaseDir("port_negative");
    WriteIni(negative, "[Network]\nUdpPort=-1\n");
    suite.Check(LoadFrom(negative).udp_port == defaults.udp_port,
                "a negative UdpPort falls back to the default");

    const std::string edge = MakeCaseDir("port_edge");
    WriteIni(edge, "[Network]\nUdpPort=1024\n");
    suite.Check(LoadFrom(edge).udp_port == 1024, "the lowest allowed port is honoured");
}

void TestNonFiniteSensitivityIsRefused(Suite& suite) {
    // A NaN sensitivity multiplies the processor's decomposition into a NaN
    // angle, which ComposeViewRotation writes straight into the engine's
    // FRotator every frame. Nothing downstream catches it: every comparison
    // against NaN is false, so the camera limits are skipped too.
    const Config defaults;

    const std::string nan = MakeCaseDir("sens_nan");
    WriteIni(nan, "[Rotation]\nYawSensitivity=nan\n");
    suite.Check(LoadFrom(nan).yaw_sensitivity == defaults.yaw_sensitivity,
                "a non-finite YawSensitivity falls back to the default");

    const std::string inf = MakeCaseDir("sens_inf");
    WriteIni(inf, "[Rotation]\nPitchSensitivity=inf\n");
    suite.Check(LoadFrom(inf).pitch_sensitivity == defaults.pitch_sensitivity,
                "an infinite PitchSensitivity falls back to the default");

    // strtod overflows a literal this large to +inf.
    const std::string overflow = MakeCaseDir("sens_overflow");
    WriteIni(overflow, "[Position]\nSensitivityZ=1e400\n");
    suite.Check(LoadFrom(overflow).position_sensitivity_z == defaults.position_sensitivity_z,
                "a SensitivityZ that overflows to infinity falls back to the default");
}

void TestDecimalCommaIsRefusedNotTruncated(Suite& suite) {
    // A European decimal comma is the expected user error, and a prefix parse
    // reads "2,5" as 2 - inside every valid range, so it passes silently and the
    // player gets a setting they did not ask for with nothing in the log.
    const Config defaults;

    const std::string dir = MakeCaseDir("sens_comma");
    WriteIni(dir, "[Rotation]\nPitchSensitivity=2,5\n");
    suite.Check(LoadFrom(dir).pitch_sensitivity == defaults.pitch_sensitivity,
                "a decimal-comma PitchSensitivity is refused, not truncated to 2");

    const std::string limit = MakeCaseDir("limit_comma");
    WriteIni(limit, "[Position]\nLimitZ=0,35\n");
    suite.Check(Near(LoadFrom(limit).limit_z, defaults.limit_z, 1e-6),
                "a decimal-comma LimitZ is refused, not truncated to 0");
}

void TestPositionLimitsAreBounded(Suite& suite) {
    // PositionProcessor clamps with Clamp(v, -limit, +limit). A negative limit
    // inverts the bounds, so every input returns the lower one and the lean is
    // pinned at a fixed offset instead of being freed.
    const std::string negative = MakeCaseDir("limit_negative");
    WriteIni(negative, "[Position]\nLimitZ=-0.5\n");
    suite.Check(LoadFrom(negative).limit_z >= 0.0f,
                "a negative LimitZ never reaches the position clamp");

    const Config defaults;
    const std::string nan = MakeCaseDir("limit_nan");
    WriteIni(nan, "[Position]\nLimitX=nan\n");
    suite.Check(Near(LoadFrom(nan).limit_x, defaults.limit_x, 1e-6),
                "a non-finite LimitX falls back to the default");

    const std::string huge = MakeCaseDir("limit_huge");
    WriteIni(huge, "[Position]\nLimitY=10000\n");
    suite.Check(LoadFrom(huge).limit_y <= 10.0f,
                "a mistyped LimitY of 10000 metres is bounded");
}

void TestRetiredSmoothingKeyIsIgnored(Suite& suite) {
    // The pre-split single Smoothing key carried a hidden 0.15 floor, so its
    // value does not mean what it used to and is deliberately not migrated.
    const std::string dir = MakeCaseDir("retired");
    WriteIni(dir, "[Rotation]\nSmoothing=0.8\n");
    const Config config = LoadFrom(dir);

    const Config defaults;
    suite.Check(config.local_smoothing == defaults.local_smoothing,
                "a retired Smoothing key does not migrate into LocalSmoothing");
    suite.Check(Near(config.remote_smoothing, defaults.remote_smoothing, 1e-6),
                "a retired Smoothing key does not migrate into RemoteSmoothing");
}

}

int RunConfigTests() {
    Suite suite("Configuration");
    TestShippedDefaults(suite);
    TestAbsentFileKeepsDefaults(suite);
    TestWrittenDefaultsRoundTrip(suite);
    TestExistingFileIsNotOverwritten(suite);
    TestValuesAreRead(suite);
    TestSmoothingIsValidatedNotFloored(suite);
    TestNonFiniteSmoothingFallsBackPerKey(suite);
    TestNegativeSmoothingIsClamped(suite);
    TestFovOffsetIsBounded(suite);
    TestUnbindableYawModeKeyFallsBack(suite);
    TestUdpPortIsRangeChecked(suite);
    TestNonFiniteSensitivityIsRefused(suite);
    TestDecimalCommaIsRefusedNotTruncated(suite);
    TestPositionLimitsAreBounded(suite);
    TestRetiredSmoothingKeyIsIgnored(suite);
    TestTrailingCommentOnABoolIsHonoured(suite);
    TestUnreadableBoolFallsBackToTheDefault(suite);
    TestTraceChannelsAreWholeTokens(suite);
    TestCollisionValuesAreBounded(suite);

    // Last, and it guards every negative case above: if the fixtures never
    // reached disk they all "passed" against an untouched Config.
    suite.Check(g_iniWriteFailures == 0,
                "every test fixture was written and read back, so the negative cases meant something");
    return suite.failures();
}
