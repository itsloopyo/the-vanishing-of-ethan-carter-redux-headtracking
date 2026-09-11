// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Pins the per-caller inject gate. This one predicate is the whole mod's
// look/aim decoupling: widen it and interaction traces, the audio listener and
// the streaming query all start reading the head-tracked rotation instead of
// the clean mouse-driven one.

#include "caller_gate.h"
#include "test_support.h"

namespace {

using namespace ecr_ht;
using namespace ecr_ht::tests;

// Shaped like a real profile: four known callers, the rest zero padding.
constexpr std::uintptr_t kRender    = 0x00b3705f;
constexpr std::uintptr_t kHud       = 0x00abc546;
constexpr std::uintptr_t kListener  = 0x00b77d42;
constexpr std::uintptr_t kStreaming = 0x00b3ad46;

CallerRvaTable MakeCallers() {
    CallerRvaTable callers{};
    callers[0] = kRender;
    callers[1] = kHud;
    callers[2] = kListener;
    callers[3] = kStreaming;
    return callers;
}

// The top of the mode range, with a slot that is actually populated. Every
// other case fills indices 0-3 only, so nothing else in the suite ever reads
// the last entry of the table, and a bound that was wrong only at the top of
// the range would go unnoticed.
void TestTheLastModeSelectsTheLastSlot(Suite& suite) {
    constexpr std::uintptr_t kLast = 0x00c0ffee;
    CallerRvaTable callers{};
    callers[0] = kRender;
    callers[kMaxKnownCallers - 1] = kLast;

    suite.Check(inject::ShouldInject(callers, kLast, inject::kModeLastCaller),
                "the last mode selects the last slot");
    suite.Check(!inject::ShouldInject(callers, kRender, inject::kModeLastCaller),
                "and only that slot");
    suite.Check(!inject::ShouldInject(callers, kLast, inject::kModeFirstCaller),
                "the first mode does not reach it");
}

// The shape a half-finished profile has: a hook target, and a caller table
// nobody filled in. It must inject nowhere rather than everywhere.
void TestAnEmptyTableInjectsNowhere(Suite& suite) {
    const CallerRvaTable empty{};
    suite.Check(!inject::ShouldInject(empty, kRender, inject::kModeFirstCaller),
                "an unpopulated table injects for no caller at the shipped default mode");
    suite.Check(!inject::ShouldInject(empty, 0, inject::kModeFirstCaller),
                "and a zero caller RVA does not match its zero padding");
}

void TestModeRangeIsDerivedFromTheTable(Suite& suite) {
    suite.Check(inject::kModeLastCaller == static_cast<int>(kMaxKnownCallers),
                "the last per-caller mode is the table size");
    suite.Check(inject::kModeNone == inject::kModeLastCaller + 1,
                "the none-mode sits one past the last caller");
    suite.Check(inject::kModeCount == inject::kModeNone + 1,
                "the cycle modulus covers every mode once");
}

void TestModeAllInjectsEverywhere(Suite& suite) {
    const CallerRvaTable callers = MakeCallers();
    suite.Check(inject::ShouldInject(callers, kRender, inject::kModeAll),
                "mode-all injects for the render caller");
    suite.Check(inject::ShouldInject(callers, kListener, inject::kModeAll),
                "mode-all injects for the listener caller");
    suite.Check(inject::ShouldInject(callers, 0xdeadbeef, inject::kModeAll),
                "mode-all injects for a caller the profile has never seen");
}

void TestDefaultModeInjectsOnlyForTheRenderCaller(Suite& suite) {
    const CallerRvaTable callers = MakeCallers();
    const int mode = inject::kModeFirstCaller;  // the shipped default

    suite.Check(inject::ShouldInject(callers, kRender, mode),
                "the default mode injects for the render caller");
    suite.Check(!inject::ShouldInject(callers, kHud, mode),
                "the default mode leaves the HUD caller clean");
    suite.Check(!inject::ShouldInject(callers, kListener, mode),
                "the default mode leaves the audio listener clean");
    suite.Check(!inject::ShouldInject(callers, kStreaming, mode),
                "the default mode leaves the streaming query clean");
}

void TestEachModeSelectsItsOwnCaller(Suite& suite) {
    const CallerRvaTable callers = MakeCallers();
    suite.Check(inject::ShouldInject(callers, kHud, inject::kModeFirstCaller + 1),
                "mode 2 selects the second caller");
    suite.Check(!inject::ShouldInject(callers, kRender, inject::kModeFirstCaller + 1),
                "mode 2 does not select the first caller");
}

void TestModeNoneInjectsNowhere(Suite& suite) {
    const CallerRvaTable callers = MakeCallers();
    suite.Check(!inject::ShouldInject(callers, kRender, inject::kModeNone),
                "the none-mode leaves even the render caller clean");
}

void TestPaddingSlotsNeverMatch(Suite& suite) {
    // Trailing zero entries are unused padding. A caller that genuinely returned
    // RVA 0 must not be treated as selecting one of them.
    const CallerRvaTable callers = MakeCallers();
    suite.Check(!inject::ShouldInject(callers, 0, inject::kModeLastCaller),
                "an empty table slot does not match a zero caller RVA");
}

void TestModesOutsideTheRangeAreRefused(Suite& suite) {
    const CallerRvaTable callers = MakeCallers();
    suite.Check(!inject::ShouldInject(callers, kRender, -1),
                "a negative mode injects nowhere");
    suite.Check(!inject::ShouldInject(callers, kRender, inject::kModeCount),
                "a mode past the cycle injects nowhere");
}

}

int RunCallerGateTests() {
    Suite suite("Inject caller gate");
    TestModeRangeIsDerivedFromTheTable(suite);
    TestModeAllInjectsEverywhere(suite);
    TestDefaultModeInjectsOnlyForTheRenderCaller(suite);
    TestEachModeSelectsItsOwnCaller(suite);
    TestModeNoneInjectsNowhere(suite);
    TestPaddingSlotsNeverMatch(suite);
    TestModesOutsideTheRangeAreRefused(suite);
    TestTheLastModeSelectsTheLastSlot(suite);
    TestAnEmptyTableInjectsNowhere(suite);
    return suite.failures();
}
