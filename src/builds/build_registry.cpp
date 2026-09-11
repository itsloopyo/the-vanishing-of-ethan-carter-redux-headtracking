// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_registry.h"

#include <array>
#include <cstddef>

#include "caller_gate.h"

#include <cameraunlock/memory/pe_fingerprint.h>

#include "logging.h"

namespace ecr_ht::builds {

// One extern per known build. Never-delete policy: when a game patch breaks
// the current build, derive new RVAs and ADD a new profile here (newest at
// the top of kKnownProfiles) without removing the old one. Users on the
// un-patched build still match their old profile by PE fingerprint.
extern const BuildProfile kSteamProfile_20150915;

namespace {
// Newest-first. The first entry is the "primary" used to label
// newer/older when no profile matches.
constexpr std::array<const BuildProfile*, 1> kKnownProfiles = {
    &kSteamProfile_20150915,
};

const BuildProfile* g_active = nullptr;

// Every RVA the mod turns into a code pointer. A profile is a hand-written
// table appended by a human after a patch, so these are checked against the
// image the fingerprint describes before any of them is called.
struct NamedRva {
    const char* what;
    std::uintptr_t rva;
};

// The bound is the profile's OWN SizeOfImage rather than the running module's,
// which is the same number by the time this runs: the fingerprint comparison
// that got us here includes SizeOfImage, so a profile only reaches validation
// when the two agree.
bool RvasFitTheImage(const BuildProfile& profile) {
    const OffsetTable& o = profile.Offsets;
    const std::uintptr_t limit = profile.Fingerprint.SizeOfImage;
    const NamedRva rvas[] = {
        {"GetPlayerViewPoint", o.kGetPlayerViewPointRva},
        {"LineTraceSingle", o.kLineTraceSingleRva},
        {"SphereTraceSingle", o.kSphereTraceSingleRva},
        {"AHUD::DrawTexture", o.kHudDrawTextureRva},
        {"AHUD::DrawTextureSimple", o.kHudDrawTextureSimpleRva},
    };
    for (const NamedRva& r : rvas) {
        // Zero is "this build profile does not carry that address", which each
        // consumer already handles by turning its own feature off.
        if (r.rva != 0 && r.rva >= limit) {
            Log::Line("build-check: profile %s puts %s at RVA 0x%08llx, past the "
                      "0x%08x-byte image - staying dormant rather than calling it",
                      profile.Name, r.what,
                      static_cast<unsigned long long>(r.rva), profile.Fingerprint.SizeOfImage);
            return false;
        }
    }
    return true;
}

// The inject mode decides which GetPlayerViewPoint callers get the head pose,
// and it is the look/aim decoupling. Both ways it can be wrong are silent: a
// mode of kModeAll ships the pose to the interaction trace and the audio
// listener as well as the render path, and a mode selecting an empty slot
// hooks successfully and then never injects anything, which reads in the log
// exactly like a working install.
bool InjectModeIsUsable(const BuildProfile& profile) {
    const int mode = profile.Offsets.kDefaultInjectMode;
    if (mode < inject::kModeFirstCaller || mode > inject::kModeLastCaller) {
        Log::Line("build-check: profile %s defaults to inject mode %d, which is not a "
                  "single caller - staying dormant", profile.Name, mode);
        return false;
    }
    if (profile.Offsets.kKnownCallerRvas[static_cast<std::size_t>(mode - 1)] == 0) {
        Log::Line("build-check: profile %s defaults to inject mode %d but names no caller "
                  "there - staying dormant rather than hooking and never injecting",
                  profile.Name, mode);
        return false;
    }
    return true;
}

// A profile is usable iff it names a hook target, every address it does name
// lies inside the image, and its default inject mode selects a caller that
// exists. Anything else leaves the mod dormant rather than activating against
// an address that is not there.
bool ProfileHasHookTarget(const BuildProfile& profile) {
    return profile.Offsets.kGetPlayerViewPointRva != 0;
}

// Run only for the profile whose fingerprint matched. Doing it for every
// profile would put one bad entry's rejection lines in every user's log on
// every launch, matched or not, and the registry only ever grows.
bool ProfileValidates(const BuildProfile& profile) {
    return RvasFitTheImage(profile) && InjectModeIsUsable(profile);
}
}  // namespace


MatchResult SelectProfile(HMODULE host) {
    PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(host, running)) {
        Log::Line("build-check: failed to read PE header from host module");
        return MatchResult::ReadFailed;
    }

    Log::Line("build-check: running  ts=0x%08x size=0x%08x csum=0x%08x",
        running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

    for (const BuildProfile* p : kKnownProfiles) {
        const bool hasTarget = ProfileHasHookTarget(*p);
        Log::Line("build-check: profile=%s ts=0x%08x size=0x%08x csum=0x%08x%s",
            p->Name, p->Fingerprint.TimeDateStamp,
            p->Fingerprint.SizeOfImage, p->Fingerprint.CheckSum,
            hasTarget ? "" : " (offsets TBD)");
        if (running.Matches(p->Fingerprint)) {
            if (!hasTarget || !ProfileValidates(*p)) {
                Log::Line("build-check: fingerprint matches %s but the profile did not "
                          "validate - staying dormant", p->Name);
                return MatchResult::ProfileIncomplete;
            }
            g_active = p;
            Log::Line("build-check: matched profile %s", p->Name);
            return MatchResult::Matched;
        }
    }

    // No match. Classify against the primary profile so the log explains
    // direction ("patched newer", "older", or "tampered").
    switch (cameraunlock::memory::ClassifyMismatch(
                running, kKnownProfiles.front()->Fingerprint)) {
        case cameraunlock::memory::FingerprintMismatch::Newer:
            return MatchResult::HostNewer;
        case cameraunlock::memory::FingerprintMismatch::Older:
            return MatchResult::HostOlder;
        case cameraunlock::memory::FingerprintMismatch::Differs:
        default:
            return MatchResult::HostDiffers;
    }
}

const BuildProfile& ActiveProfile() { return *g_active; }

}  // namespace ecr_ht::builds
