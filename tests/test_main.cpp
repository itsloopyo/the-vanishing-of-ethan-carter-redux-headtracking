// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include <iostream>

int RunAimProjectionTests();
int RunCameraPoseTests();
int RunCallerGateTests();
int RunFovTests();
int RunLogThrottleTests();
int RunConfigTests();

int main() {
    std::cout << "Ethan Carter Redux Head Tracking Tests\n";
    std::cout << "=====================================\n";

    int failures = 0;
    failures += RunAimProjectionTests();
    failures += RunCameraPoseTests();
    failures += RunCallerGateTests();
    failures += RunFovTests();
    failures += RunLogThrottleTests();
    failures += RunConfigTests();

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
