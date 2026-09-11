// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>
#include <iostream>

// Shared assertions for the behaviour-locking tests, in the runner style
// cameraunlock-core's own tests use: no framework, failures reported by name
// and counted per suite.

namespace ecr_ht::tests {

class Suite {
public:
    explicit Suite(const char* name) { std::cout << name << ":\n"; }

    void Check(bool condition, const char* what) {
        if (condition) {
            std::cout << "  [PASS] " << what << "\n";
        } else {
            std::cout << "  [FAIL] " << what << "\n";
            ++failures_;
        }
    }

    int failures() const { return failures_; }

private:
    int failures_ = 0;
};

inline bool Near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

}
