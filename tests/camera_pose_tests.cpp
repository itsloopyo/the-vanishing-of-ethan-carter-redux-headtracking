// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Pins the camera maths the player actually sees: the two yaw modes, the roll
// sign on each, and the 6DOF axis mapping.
//
// AGENTS.md is emphatic that these signs are not derivable from the engine's
// handedness - they were confirmed in a running game, and the shipped UE
// siblings agree with them. A "simplification" that flips one would send the
// view the wrong way with nothing in the build to object, so each one is
// asserted here against a hand-worked expectation rather than against whatever
// the code currently returns.

#include "camera_pose.h"

#include <cmath>
#include <limits>

#include "test_support.h"

namespace {

using namespace ecr_ht;
using namespace ecr_ht::tests;

namespace ue = ::cameraunlock::unreal;

constexpr float kMetresToUE = 100.0f;

FRotator3f Compose(const FRotator3f& clean, float yaw, float pitch, float roll, bool worldYaw) {
    return ComposeViewRotation(clean, CleanViewQuat(clean), yaw, pitch, roll, worldYaw);
}

void TestWorldYawIsRotatorAddition(Suite& suite) {
    const FRotator3f clean{10.0f, 61.70f, 0.0f};
    const FRotator3f out = Compose(clean, 15.0f, -5.0f, 3.0f, true);

    suite.Check(Near(out.Yaw, 76.70f, 1e-4), "world yaw adds tracker yaw to the clean yaw");
    suite.Check(Near(out.Pitch, 5.0f, 1e-4), "world yaw adds tracker pitch");
    suite.Check(Near(out.Roll, -3.0f, 1e-4), "world yaw SUBTRACTS tracker roll");
}

void TestWorldYawIsHorizonLocked(Suite& suite) {
    // Whatever the camera's pitch, a head yaw moves only the yaw component: that
    // is what horizon-locked means, and it is why this is the default mode.
    const FRotator3f pitchedDown{-70.0f, 20.0f, 0.0f};
    const FRotator3f out = Compose(pitchedDown, 30.0f, 0.0f, 0.0f, true);

    suite.Check(Near(out.Yaw, 50.0f, 1e-4), "head yaw while pitched down moves yaw only");
    suite.Check(Near(out.Pitch, -70.0f, 1e-4), "head yaw while pitched down leaves pitch alone");
    suite.Check(Near(out.Roll, 0.0f, 1e-4), "head yaw while pitched down introduces no roll");
}

void TestLocalYawMatchesWorldYawAtTheHorizon(Suite& suite) {
    // With the camera level and no roll, camera-local yaw and world yaw are the
    // same rotation. They diverge only once the base is pitched.
    const FRotator3f level{0.0f, 45.0f, 0.0f};
    const FRotator3f world = Compose(level, 20.0f, 0.0f, 0.0f, true);
    const FRotator3f local = Compose(level, 20.0f, 0.0f, 0.0f, false);

    suite.Check(Near(world.Yaw, local.Yaw, 1e-4), "local and world yaw agree at the horizon");
    suite.Check(Near(local.Pitch, 0.0f, 1e-4), "local yaw at the horizon adds no pitch");
    suite.Check(Near(local.Roll, 0.0f, 1e-4), "local yaw at the horizon adds no roll");
}

void TestLocalYawLeansOnPitchedTurns(Suite& suite) {
    // The documented difference between the modes: camera-local yaw about a
    // pitched view tilts the horizon. If this ever stops being true the two
    // modes have silently become one.
    const FRotator3f pitchedDown{-40.0f, 0.0f, 0.0f};
    const FRotator3f local = Compose(pitchedDown, 30.0f, 0.0f, 0.0f, false);

    suite.Check(std::fabs(local.Roll) > 1.0, "local yaw about a pitched view introduces roll");
}

void TestLocalYawRollIsInverted(Suite& suite) {
    // Roll is negated on the local path too, matching the world path's `- roll`.
    const FRotator3f level{0.0f, 0.0f, 0.0f};
    const FRotator3f out = Compose(level, 0.0f, 0.0f, 12.0f, false);

    suite.Check(Near(out.Roll, -12.0f, 1e-3), "local yaw negates tracker roll");
}

void TestIdentityBasisPositionAxes(Suite& suite) {
    // With no camera rotation, UE camera-local +x is forward, +y right, +z up.
    const ue::FQuat4d identity = CleanViewQuat(FRotator3f{0.0f, 0.0f, 0.0f});

    // The processor's NEGATIVE z is the forward lean, so it must land on +x.
    const ue::FVector forward = ComputePositionOffset(identity, 0.0f, 0.0f, -0.40f);
    suite.Check(Near(forward.X, 0.40 * kMetresToUE, 1e-3), "processor -z leans the camera forward (+x)");
    suite.Check(Near(forward.Y, 0.0, 1e-6), "a pure forward lean does not sway");
    suite.Check(Near(forward.Z, 0.0, 1e-6), "a pure forward lean does not heave");

    // Sway is mirrored too: a positive tracker x must move the camera LEFT.
    const ue::FVector sway = ComputePositionOffset(identity, 0.30f, 0.0f, 0.0f);
    suite.Check(Near(sway.Y, -0.30 * kMetresToUE, 1e-3), "processor +x sways the camera left (-y)");

    // Heave is the one axis that is NOT mirrored.
    const ue::FVector heave = ComputePositionOffset(identity, 0.0f, 0.20f, 0.0f);
    suite.Check(Near(heave.Z, 0.20 * kMetresToUE, 1e-3), "processor +y raises the camera (+z)");
}

void TestPositionOffsetIsMetresToCentimetres(Suite& suite) {
    const ue::FQuat4d identity = CleanViewQuat(FRotator3f{0.0f, 0.0f, 0.0f});
    const ue::FVector out = ComputePositionOffset(identity, 0.0f, 0.0f, -1.0f);
    suite.Check(Near(out.X, 100.0, 1e-6), "one metre of lean is 100 UE units");
}

void TestPositionOffsetFollowsTheBody(Suite& suite) {
    // The offset is built in the CLEAN camera basis, so a body facing +y turns
    // the forward lean onto the world +y axis. This is what keeps head sway
    // attached to the body rather than to the head-rotated view.
    const ue::FQuat4d facingY = CleanViewQuat(FRotator3f{0.0f, 90.0f, 0.0f});
    const ue::FVector out = ComputePositionOffset(facingY, 0.0f, 0.0f, -0.40f);

    suite.Check(Near(out.X, 0.0, 1e-3), "a lean while facing +y does not move along x");
    suite.Check(Near(out.Y, 0.40 * kMetresToUE, 1e-3), "a forward lean while facing +y moves along +y");
}

void TestZoomScalingLeavesOrdinaryPlayAlone(Suite& suite) {
    const ScaledPose pose = ScalePoseForZoom(12.0f, -7.0f, 4.0f, 0.1f, -0.2f, 0.3f, 1.0f);
    suite.Check(pose.yaw == 12.0f && pose.pitch == -7.0f && pose.roll == 4.0f,
                "a factor of 1.0 passes the rotation through untouched");
    suite.Check(pose.x == 0.1f && pose.y == -0.2f && pose.z == 0.3f,
                "a factor of 1.0 passes the lean through untouched");
}

void TestZoomScalingHoldsTheScreenDisplacement(Suite& suite) {
    // The image displacement of an angle goes as tan(angle) / tan(fov/2), so
    // holding it fixed means tan(out) = tan(in) * factor.
    const float factor = 0.5f;
    const ScaledPose pose = ScalePoseForZoom(20.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, factor);
    suite.Check(Near(std::tan(pose.yaw * 3.14159265358979323846 / 180.0),
                     std::tan(20.0 * 3.14159265358979323846 / 180.0) * factor, 1e-6),
                "yaw is rescaled through the tangent, not multiplied");
    suite.Check(Near(std::tan(pose.pitch * 3.14159265358979323846 / 180.0),
                     std::tan(10.0 * 3.14159265358979323846 / 180.0) * factor, 1e-6),
                "pitch is rescaled the same way");
}

void TestZoomScalingLeavesRollAlone(Suite& suite) {
    const ScaledPose pose = ScalePoseForZoom(0.0f, 0.0f, 25.0f, 0.0f, 0.0f, 0.0f, 0.4f);
    suite.Check(pose.roll == 25.0f,
                "roll rotates the picture rather than translating it, so a zoom does not "
                "touch it");
}

// ScaleAngleForZoom is atan(tan(angle) * factor), which is only defined inside
// +/-90: tan is discontinuous there and the round trip changes SIGN past it. The
// pipeline can reach past it - BoundAngle admits +/-720 by design, and the pose
// is scaled BEFORE it is bounded - and this game animates its field of view by
// six degrees whenever the player runs, so the factor is not 1.0 for the whole
// of every sprint. Unguarded, a 90.5 degree yaw came back as -89.6: the view
// snapped to the opposite side, only while running, and snapped back on
// release.
void TestZoomScalingDoesNotWrapPastTheTangentPole(Suite& suite) {
    const float factor = 1.1104f;  // 96 degrees rendered against a 90 degree base

    const ScaledPose past = ScalePoseForZoom(90.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, factor);
    suite.Check(past.yaw > 0.0f,
                "a yaw past 90 degrees keeps its sign instead of flipping to the far side");
    suite.Check(Near(past.yaw, 90.5, 1e-4),
                "and is passed through unscaled, the tangent being useless there");

    const ScaledPose half_turn = ScalePoseForZoom(0.0f, 179.5f, 0.0f, 0.0f, 0.0f, 0.0f, factor);
    suite.Check(Near(half_turn.pitch, 179.5, 1e-4),
                "the same for a pitch most of the way round");

    // The step across the boundary has to be small, or the pass-through is a
    // visible jump of its own. Inside the domain the tangent is already so
    // steep that scaling barely moves the angle.
    // The measured step at this zoom is 0.093 degrees. The bound is 0.15 rather
    // than a round half-degree because a looser one is also satisfied by the
    // UNGUARDED function, and would pin nothing.
    const ScaledPose inside = ScalePoseForZoom(88.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, factor);
    const ScaledPose outside = ScalePoseForZoom(89.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, factor);
    suite.Check(outside.yaw - inside.yaw < 0.15f && outside.yaw > inside.yaw,
                "and the pass-through is continuous with the scaled side of the boundary");

    // Ordinary head angles must still be scaled - the guard must not have
    // turned the compensation off.
    const ScaledPose ordinary = ScalePoseForZoom(15.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, factor);
    suite.Check(ordinary.yaw > 15.0f && ordinary.yaw < 17.0f,
                "a head-sized angle is still rescaled");
}

void TestZoomScalingIsLinearInTheLean(Suite& suite) {
    const ScaledPose pose = ScalePoseForZoom(0.0f, 0.0f, 0.0f, 0.30f, -0.20f, 0.40f, 0.5f);
    suite.Check(Near(pose.x, 0.15, 1e-6) && Near(pose.y, -0.10, 1e-6) &&
                Near(pose.z, 0.20, 1e-6),
                "a lean scales linearly - a head offset lands at d / (2 D tan(fov/2)) of "
                "the frame");
}

// A tracker angle arrives over a socket bound to every interface, and core's
// parser only rejects non-finite values - a finite 3e38 degrees reaches the
// composition intact, and one sensitivity multiply or zoom scale past that is
// an infinity the engine turns into a NaN view matrix.
void TestExtremeTrackerAnglesStayFinite(Suite& suite) {
    const FRotator3f clean{0.0f, 0.0f, 0.0f};
    const float huge = 3.0e38f;

    const FRotator3f world = Compose(clean, huge, -huge, huge, true);
    suite.Check(std::isfinite(world.Yaw) && std::isfinite(world.Pitch) &&
                std::isfinite(world.Roll),
                "an enormous tracker angle cannot reach the engine's rotator as infinity");
    suite.Check(std::fabs(world.Yaw) <= 720.0f && std::fabs(world.Pitch) <= 720.0f &&
                std::fabs(world.Roll) <= 720.0f,
                "an enormous tracker angle is bounded to two turns, not passed through");

    const FRotator3f local = Compose(clean, huge, -huge, huge, false);
    suite.Check(std::isfinite(local.Yaw) && std::isfinite(local.Pitch) &&
                std::isfinite(local.Roll),
                "the camera-local yaw path bounds the same way the world path does");
}

// Nothing upstream can emit one, but a zoom scale of an already-huge angle can,
// and a NaN written into the rotator would take the whole frame's projection
// with it.
void TestNonFiniteTrackerAnglesCollapseToNoRotation(Suite& suite) {
    const FRotator3f clean{10.0f, 20.0f, 0.0f};
    const float inf = std::numeric_limits<float>::infinity();
    const FRotator3f out = Compose(clean, inf, -inf,
                                   std::numeric_limits<float>::quiet_NaN(), true);

    suite.Check(Near(out.Yaw, 20.0f, 1e-4) && Near(out.Pitch, 10.0f, 1e-4) &&
                Near(out.Roll, 0.0f, 1e-4),
                "a non-finite tracker angle leaves the clean rotation exactly as it was");
}

// The bound is a failsafe on garbage, not a limit on the head: every angle a
// tracker really produces has to pass through it untouched.
void TestOrdinaryAnglesAreUntouchedByTheBound(Suite& suite) {
    const FRotator3f clean{0.0f, 0.0f, 0.0f};
    const FRotator3f out = Compose(clean, 179.5f, -89.5f, 179.5f, true);

    suite.Check(Near(out.Yaw, 179.5f, 1e-4), "a full-range tracker yaw is passed through");
    suite.Check(Near(out.Pitch, -89.5f, 1e-4), "a full-range tracker pitch is passed through");
    suite.Check(Near(out.Roll, -179.5f, 1e-4), "a full-range tracker roll is passed through");
}

}

int RunCameraPoseTests() {
    Suite suite("Camera pose composition");
    TestWorldYawIsRotatorAddition(suite);
    TestWorldYawIsHorizonLocked(suite);
    TestLocalYawMatchesWorldYawAtTheHorizon(suite);
    TestLocalYawLeansOnPitchedTurns(suite);
    TestLocalYawRollIsInverted(suite);
    TestIdentityBasisPositionAxes(suite);
    TestPositionOffsetIsMetresToCentimetres(suite);
    TestPositionOffsetFollowsTheBody(suite);
    TestZoomScalingLeavesOrdinaryPlayAlone(suite);
    TestZoomScalingHoldsTheScreenDisplacement(suite);
    TestZoomScalingLeavesRollAlone(suite);
    TestZoomScalingIsLinearInTheLean(suite);
    TestZoomScalingDoesNotWrapPastTheTangentPole(suite);
    TestExtremeTrackerAnglesStayFinite(suite);
    TestNonFiniteTrackerAnglesCollapseToNoRotation(suite);
    TestOrdinaryAnglesAreUntouchedByTheBound(suite);
    return suite.failures();
}
