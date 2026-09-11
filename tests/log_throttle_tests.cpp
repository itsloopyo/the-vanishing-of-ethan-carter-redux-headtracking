// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "log_throttle.h"

#include <windows.h>

#include "test_support.h"

// The cadence gates the hook's log lines run through. Both were duplicated per
// call site before, so what is pinned here is the shared behaviour every one of
// them relies on: the FIRST line is always written - it is the one that says
// the hook is alive at all - and a bounded gate stops for good once it has
// written its quota, whatever the interval does afterwards.
//
// A zero interval makes the time gate always due, so these run without sleeping
// and without depending on the machine's uptime.

namespace ecr_ht::tests {

namespace {

void TestFirstLineIsAlwaysWritten(Suite& suite) {
    LogThrottle immediate(0);
    suite.Check(immediate.Due(), "the first call is due even before any interval has elapsed");

    // An hour, so nothing but the never-fired sentinel can make the first call
    // due - the case a game launched shortly after boot lands on.
    LogThrottle hourly(3600000);
    suite.Check(hourly.Due(), "a long interval still writes the first line");
    suite.Check(!hourly.Due(), "the second call inside the interval is swallowed");
}

void TestMarkRestartsTheInterval(Suite& suite) {
    LogThrottle hourly(3600000);
    hourly.Mark();
    suite.Check(!hourly.Due(),
                "Mark() counts as a line, so a keepalive never lands right behind one");
}

void TestBoundedGateStopsAtItsQuota(Suite& suite) {
    BoundedLogThrottle bounded(0, 3);
    suite.Check(bounded.Due() && bounded.Due() && bounded.Due(),
                "a bounded gate writes its whole quota");
    suite.Check(!bounded.Due() && !bounded.Due(),
                "and nothing after it, however long the session runs");
}

void TestBoundedGateHonoursTheInterval(Suite& suite) {
    BoundedLogThrottle bounded(3600000, 5);
    suite.Check(bounded.Due(), "the first line is written before the interval has elapsed");
    suite.Check(!bounded.Due(), "the quota does not let a line past the interval gate");
}

void TestUnboundedGateNeverStops(Suite& suite) {
    BoundedLogThrottle unbounded(0, BoundedLogThrottle::kUnbounded);
    bool all = true;
    for (int i = 0; i < 1000; ++i) all = all && unbounded.Due();
    suite.Check(all, "kUnbounded streams the line, which is what a developer build wants");
}

// The half of the contract the rest of the suite cannot reach: every other case
// uses an interval of 0 (always due) or an hour (never due again), so nothing
// exercised the epoch being RESTARTED. A change that stopped refreshing it would
// leave the 5-minute heartbeat keepalive firing once at startup and never again
// - the one line that proves the hook is still alive - with the suite green.
void TestTheIntervalReopens(Suite& suite) {
    // 200ms against a ~15.6ms tick. A 1ms window would be straddled whenever the
    // Check between the two calls happened to cross a tick edge - the second
    // call would then see a full tick elapsed, the gate would reopen, and the
    // test would fail on CI a fraction of a percent of the time.
    LogThrottle brief(200);
    suite.Check(brief.Due(), "the first line is written");
    suite.Check(!brief.Due(), "an immediate second call is swallowed");
    Sleep(400);
    suite.Check(brief.Due(), "the gate reopens once the interval has elapsed");
    suite.Check(!brief.Due(), "and closes again behind that line");
}

void TestZeroQuotaWritesNothing(Suite& suite) {
    BoundedLogThrottle silent(0, 0);
    suite.Check(!silent.Due(), "a quota of zero writes no line at all, not one");
}

}  // namespace

int RunLogThrottleTests() {
    Suite suite("Log cadence gates");
    TestFirstLineIsAlwaysWritten(suite);
    TestMarkRestartsTheInterval(suite);
    TestBoundedGateStopsAtItsQuota(suite);
    TestBoundedGateHonoursTheInterval(suite);
    TestUnboundedGateNeverStops(suite);
    TestZeroQuotaWritesNothing(suite);
    TestTheIntervalReopens(suite);
    return suite.failures();
}

}  // namespace ecr_ht::tests

int RunLogThrottleTests() { return ecr_ht::tests::RunLogThrottleTests(); }
