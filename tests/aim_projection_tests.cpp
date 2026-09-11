// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "aim_projection.h"

#include <cmath>

#include "test_support.h"

// The reticle litmus tests, run against the projection rather than against a
// running game. Each one pins a case the fleet has shipped wrong at least once:
// a roll that drags the mark sideways, a pitch that also moves it horizontally,
// and - the one 6DOF adds - a lean that has to move the mark by parallax, by an
// amount that depends on how far away the thing being aimed at is.

namespace ecr_ht::tests {

namespace {

namespace ue = ::cameraunlock::unreal;

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

// 1280x720 at the 90 degree horizontal field the game renders. Measured, not
// assumed: a 25 degree head yaw moves the frame 292px, which is that field.
HalfFieldTangents Tangents() {
    return TangentsFromHorizontalFov(90.0f, 1280.0f, 720.0f);
}

ue::FQuat4d Rot(double pitch, double yaw, double roll) {
    return ue::QuatFromEulerDeg(pitch, yaw, roll);
}

// A point straight ahead of an un-rotated camera at the origin, `distance` UE
// units away, offset by (right, up) in world terms.
ue::FVector Ahead(double distance, double right = 0.0, double up = 0.0) {
    return ue::FVector{distance, right, up};
}

}  // namespace

int RunAimProjectionTests() {
    Suite s("aim projection");
    const HalfFieldTangents t = Tangents();

    s.Check(Near(t.horizontal, std::tan(45.0 * kDegToRad), 1e-4),
            "a 90 degree horizontal field gives a half-field tangent of 1");
    s.Check(Near(t.vertical, t.horizontal * 720.0 / 1280.0, 1e-6),
            "the vertical half-field is the horizontal one scaled by the viewport aspect");

    // Centred camera, centred aim: the reticle belongs at the centre of the frame.
    {
        const AimNdc ndc = ProjectAimPoint(ue::FVector{0, 0, 0}, Rot(0, 0, 0), Ahead(500.0), t);
        s.Check(ndc.valid && Near(ndc.x, 0.0, 1e-6) && Near(ndc.y, 0.0, 1e-6),
                "aim dead ahead of an un-rotated camera projects to the centre");
    }

    // Litmus 1: pure roll, no lean. The clean aim point does not move, and a
    // point on the view axis stays on the view axis however the frame is rolled.
    {
        const AimNdc ndc = ProjectAimPoint(ue::FVector{0, 0, 0}, Rot(0, 0, 25), Ahead(500.0), t);
        s.Check(ndc.valid && Near(ndc.x, 0.0, 1e-6) && Near(ndc.y, 0.0, 1e-6),
                "pure roll leaves the reticle at the centre");
    }

    // Litmus 2: pure pitch. The mark moves purely vertically, and by the tangent
    // of the pitch over the vertical half-field.
    {
        const AimNdc ndc = ProjectAimPoint(ue::FVector{0, 0, 0}, Rot(20, 0, 0), Ahead(500.0), t);
        const double expected = -std::tan(20.0 * kDegToRad) / t.vertical;
        s.Check(ndc.valid && Near(ndc.x, 0.0, 1e-9), "pure pitch does not move the reticle sideways");
        s.Check(ndc.valid && Near(ndc.y, expected, 1e-6),
                "pitching the view up puts the aim below the centre by tan(pitch)/tan(vFOV/2)");
    }

    // Litmus 3: pitch and roll together. The camera composes roll about the final
    // view axis, so the offset ROTATES with roll rather than staying vertical -
    // and it must rotate by exactly the roll the camera was written with, which
    // is what a basis-to-basis projection gives for free.
    {
        const AimNdc pitchOnly =
            ProjectAimPoint(ue::FVector{0, 0, 0}, Rot(20, 0, 0), Ahead(500.0), t);
        const AimNdc both =
            ProjectAimPoint(ue::FVector{0, 0, 0}, Rot(20, 0, 25), Ahead(500.0), t);
        // Compare in a square space: NDC is stretched by the aspect, so the
        // rotation is only a rotation once that stretch is undone.
        const double px = pitchOnly.x * t.horizontal, py = pitchOnly.y * t.vertical;
        const double bx = both.x * t.horizontal, by = both.y * t.vertical;
        s.Check(both.valid && Near(std::hypot(bx, by), std::hypot(px, py), 1e-6),
                "adding roll to pitch rotates the aim offset without changing its length");
        s.Check(both.valid && std::fabs(bx) > 1e-3,
                "roll over pitch moves the aim off the vertical, matching the rolled frame");
    }

    // Litmus 5: pure lateral lean, rotation centred. This is the one a
    // direction-only projection gets wrong, and the one the release gate tests
    // at two distances: the correction is parallax, so it MUST be larger for a
    // near target than a far one.
    {
        const double lean = 30.0;  // 0.30m of sway, the shipped LimitX
        const ue::FVector eye{0.0, lean, 0.0};
        const AimNdc near_ =
            ProjectAimPoint(eye, Rot(0, 0, 0), Ahead(100.0), t);
        const AimNdc far_ =
            ProjectAimPoint(eye, Rot(0, 0, 0), Ahead(1000.0), t);
        s.Check(near_.valid && Near(near_.x, -lean / 100.0 / t.horizontal, 1e-6),
                "a lateral lean moves the aim by lean/distance at arm's reach");
        s.Check(far_.valid && Near(far_.x, -lean / 1000.0 / t.horizontal, 1e-6),
                "the same lean moves it a tenth as far across the room");
        s.Check(near_.valid && far_.valid && std::fabs(near_.x) > std::fabs(far_.x) * 5.0,
                "the correction shrinks with distance, which a fixed depth cannot reproduce");
        s.Check(near_.valid && Near(near_.y, 0.0, 1e-9),
                "a lateral lean does not move the aim vertically");
    }

    // Litmus 6: pure vertical lean, same near/far pair.
    {
        const double lean = 20.0;  // 0.20m of heave, the shipped LimitY
        const ue::FVector eye{0.0, 0.0, lean};
        const AimNdc near_ = ProjectAimPoint(eye, Rot(0, 0, 0), Ahead(100.0), t);
        const AimNdc far_ = ProjectAimPoint(eye, Rot(0, 0, 0), Ahead(1000.0), t);
        s.Check(near_.valid && Near(near_.y, -lean / 100.0 / t.vertical, 1e-6),
                "rising in the seat moves the aim down the frame by lean/distance");
        s.Check(far_.valid && Near(far_.y, -lean / 1000.0 / t.vertical, 1e-6),
                "and a tenth as far for a target ten times further away");
        s.Check(near_.valid && Near(near_.x, 0.0, 1e-9),
                "a vertical lean does not move the aim sideways");
    }

    // Opposite leans land on opposite sides of centre by the same amount: the
    // aim point itself never moves, so the mark must be symmetric about it.
    {
        const AimNdc left =
            ProjectAimPoint(ue::FVector{0.0, -30.0, 0.0}, Rot(0, 0, 0), Ahead(200.0), t);
        const AimNdc right =
            ProjectAimPoint(ue::FVector{0.0, 30.0, 0.0}, Rot(0, 0, 0), Ahead(200.0), t);
        s.Check(left.valid && right.valid && Near(left.x, -right.x, 1e-9),
                "opposite leans put the reticle symmetrically either side of the aim point");
    }

    // A point level with or behind the eye has no projection worth drawing.
    {
        const AimNdc behind =
            ProjectAimPoint(ue::FVector{0, 0, 0}, Rot(0, 0, 0), ue::FVector{-500.0, 0, 0}, t);
        const AimNdc level =
            ProjectAimPoint(ue::FVector{0, 0, 0}, Rot(0, 0, 0), ue::FVector{0.0, 500.0, 0}, t);
        s.Check(!behind.valid, "an aim point behind the eye is rejected, not clamped");
        s.Check(!level.valid, "an aim point level with the eye is rejected before the divide");
    }

    // A viewport the engine has not sized yet must not reach the divide.
    {
        const HalfFieldTangents none = TangentsFromHorizontalFov(90.0f, 0.0f, 0.0f);
        s.Check(Near(none.horizontal, 1.0, 1e-9) && Near(none.vertical, 1.0, 1e-9),
                "an unsized viewport falls back to identity tangents rather than dividing by zero");
        // Deliberately NOT asserted as a usable mark. Identity tangents project
        // as though the frame were 90 degrees on BOTH axes, so on this game's
        // 90x58.7 field a 20 degree pitch lands 131px above centre of a 1280x720
        // frame instead of the measured 233px - a mark placed confidently in the
        // wrong place. The caller is required to skip the projection instead,
        // which is what ProjectCleanAim's IsPlausible / ViewportSize guards do.
        const HalfFieldTangents bad = TangentsFromHorizontalFov(0.0f, 1280.0f, 720.0f);
        s.Check(Near(bad.horizontal, 1.0, 1e-9) && Near(bad.vertical, 1.0, 1e-9),
                "an unreadable field of view falls back to identity tangents rather than a divide by zero");
    }

    return s.failures();
}

}  // namespace ecr_ht::tests

int RunAimProjectionTests() { return ecr_ht::tests::RunAimProjectionTests(); }
