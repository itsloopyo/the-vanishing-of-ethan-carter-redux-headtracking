// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/unreal/ue_math.h>

#include "aim_projection.h"
#include "ue_abi.h"

// Everything the camera hook writes to the log, and nothing that decides what
// the camera does.
//
// These lines are what a session is triaged from after the fact, so they are
// kept together rather than threaded through the hook body: the hook reads as
// the camera path, and the cadence rules that keep a five-hour playthrough's
// log readable live in one place. Nothing here reads mod state - every value a
// line reports is handed in, so a line can only describe the frame the hook
// actually composed.

namespace ecr_ht::diag {

/// The whole basis the zoom compensation rests on, written once as soon as the
/// camera updates rather than once a pose arrives - so it is readable with no
/// tracker connected and without a head movement, and a patch that moves the
/// aspect or the units shows up in it. A factor that is wrong by a constant
/// renders exactly like a factor that is right, so the numbers have to be on a
/// line a human can check: the gate is that it reads 1.0000 in ordinary play.
void LogFovBasis(float fov_offset);

/// Categorical hook state, sampled on a fixed cadence but written only when it
/// changed (plus a slow keepalive). Distinguishes "hook never fired" from "no
/// tracker data", and timestamps the tracker dropping out or the gameplay gate
/// closing rather than leaving the reader to diff consecutive lines for it.
struct HeartbeatSnapshot {
    std::uint64_t hook_calls = 0;
    std::uintptr_t caller_rva = 0;
    bool in_gameplay = false;
    bool tracking_enabled = false;
    bool world_space_yaw = false;
    int inject_mode = 0;
};

/// `receiver` may be null (tracking never started); the line then reports the
/// port as absent rather than as stalled.
void LogHeartbeat(const cameraunlock::UdpReceiver* receiver, const HeartbeatSnapshot& state);

/// Rotation, lean, aim distance and the projected reticle, for ONE line. Read
/// apart they each fit two different faults equally well; together the
/// arithmetic settles which one it is.
struct PoseSnapshot {
    std::uint64_t hook_calls = 0;
    std::uintptr_t caller_rva = 0;
    FRotator3f clean{};
    // The pose AS APPLIED - after the zoom rescale - rather than as it left the
    // tracker, so the line describes the camera the player is looking through.
    // Reporting the raw angle beside a result derived from the scaled one reads
    // as a stray sensitivity multiplier on exactly the frames where the
    // difference is the compensation working.
    float applied_yaw = 0.0f;
    float applied_pitch = 0.0f;
    float applied_roll = 0.0f;
    // The factor those three were scaled by, handed in with them so the line
    // cannot pair a pose with a zoom read at some other moment.
    float zoom = 1.0f;
    FRotator3f result{};
    cameraunlock::unreal::FVector position_offset{};
    float aim_distance = 0.0f;
    AimNdc aim;
    bool lean_in_contact = false;
    bool lean_query_failed = false;
};

/// Whether the pose-detail line is due. Time-gated rather than call-gated,
/// because it runs on the render caller and a call interval would log at the
/// player's frame rate; bounded on top of that because the composition
/// evidence is all in the first minute.
bool PoseDetailDue();

void LogPoseDetail(const PoseSnapshot& pose);

/// Contact and query failure look identical from the player's seat - the lean
/// simply stops growing - so both transitions are logged, and a periodic sample
/// on top of them separates "the sweep runs and the room is open" from "the
/// sweep is not running at all".
void LogLeanState(bool in_contact, bool query_failed);

}  // namespace ecr_ht::diag
