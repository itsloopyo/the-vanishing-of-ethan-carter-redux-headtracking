// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Pins the FOV offset arithmetic. The important property is the one an "obvious
// simplification" would break: an offset of exactly 0 must return the game's own
// value UNTOUCHED, never pulled into the applied range. That is what makes
// FovOffset=0 mean "do not touch the field of view" rather than "clamp it to
// 40-150".

#include <cmath>
#include <limits>

#include "fov_override.h"
#include "test_support.h"

namespace {

using namespace ecr_ht;
using namespace ecr_ht::tests;

void TestPlausibilityBounds(Suite& suite) {
    suite.Check(fov::IsPlausible(90.0f), "the measured 90 degree default is plausible");
    suite.Check(fov::IsPlausible(fov::kMinPlausible), "the lower bound is inclusive");
    suite.Check(fov::IsPlausible(fov::kMaxPlausible), "the upper bound is inclusive");
    suite.Check(!fov::IsPlausible(0.0f), "zero is not a field of view");
    suite.Check(!fov::IsPlausible(180.0f), "180 degrees is not a field of view");
    suite.Check(!fov::IsPlausible(std::numeric_limits<float>::quiet_NaN()),
                "NaN is not a field of view");
    suite.Check(!fov::IsPlausible(std::numeric_limits<float>::infinity()),
                "infinity is not a field of view");
}

void TestZeroOffsetLeavesTheGameValueAlone(Suite& suite) {
    suite.Check(fov::ApplyOffset(90.0f, 0.0f) == 90.0f,
                "an offset of zero returns the game's own field of view");
    // Below kMinApplied, so a clamp applied unconditionally would change it.
    suite.Check(fov::ApplyOffset(30.0f, 0.0f) == 30.0f,
                "an offset of zero does not clamp a narrow game field of view");
    suite.Check(fov::ApplyOffset(160.0f, 0.0f) == 160.0f,
                "an offset of zero does not clamp a wide game field of view");
}

void TestOffsetIsAdditive(Suite& suite) {
    suite.Check(Near(fov::ApplyOffset(90.0f, 30.0f), 120.0f, 1e-4),
                "the offset adds to the game's value");
    suite.Check(Near(fov::ApplyOffset(90.0f, -20.0f), 70.0f, 1e-4),
                "a negative offset narrows the view");
}

void TestOffsetResultIsBounded(Suite& suite) {
    suite.Check(Near(fov::ApplyOffset(140.0f, 60.0f), fov::kMaxApplied, 1e-4),
                "an offset result past the upper bound is clamped");
    suite.Check(Near(fov::ApplyOffset(50.0f, -40.0f), fov::kMinApplied, 1e-4),
                "an offset result past the lower bound is clamped");
    suite.Check(fov::kMinApplied < fov::kMaxApplied, "the applied bounds are ordered");
}

void TestZoomFactorIsOneWhenNothingIsZoomed(Suite& suite) {
    suite.Check(fov::ZoomFactorBetween(90.0f, 90.0f) == 1.0f,
                "a view rendered at its own un-zoomed field of view scales the pose by "
                "exactly 1.0");
    suite.Check(fov::ZoomFactorBetween(110.0f, 110.0f) == 1.0f,
                "the same holds at a field of view the player widened");
}

void TestZoomFactorFollowsTheTangentRatio(Suite& suite) {
    // tan(22.5) / tan(45): a scope at half the angle moves the picture 2.4x as
    // far, so the pose has to come down to 0.414 of itself.
    suite.Check(Near(fov::ZoomFactorBetween(45.0f, 90.0f), 0.414213562, 1e-6),
                "halving the field of view scales the pose down by the tangent ratio");
    // tan(60) / tan(45): the game widening the view moves the picture less, so
    // the pose goes up.
    suite.Check(Near(fov::ZoomFactorBetween(120.0f, 90.0f), 1.732050808, 1e-6),
                "widening the field of view scales the pose up by the tangent ratio");
}

void TestZoomFactorRefusesAnImplausibleField(Suite& suite) {
    suite.Check(fov::ZoomFactorBetween(0.0f, 90.0f) == 1.0f,
                "an unreadable rendered field of view means no compensation, not a guess");
    suite.Check(fov::ZoomFactorBetween(90.0f, 0.0f) == 1.0f,
                "an unreadable base does the same");
    suite.Check(fov::ZoomFactorBetween(std::numeric_limits<float>::quiet_NaN(), 90.0f) == 1.0f,
                "NaN does the same");
}

}

int RunFovTests() {
    Suite suite("Field of view offset");
    TestPlausibilityBounds(suite);
    TestZeroOffsetLeavesTheGameValueAlone(suite);
    TestOffsetIsAdditive(suite);
    TestOffsetResultIsBounded(suite);
    TestZoomFactorIsOneWhenNothingIsZoomed(suite);
    TestZoomFactorFollowsTheTangentRatio(suite);
    TestZoomFactorRefusesAnImplausibleField(suite);
    return suite.failures();
}
