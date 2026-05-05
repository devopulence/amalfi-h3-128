/*
 * Copyright 2017-2018, 2020-2021 Uber Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/** @file test.h
 * @brief Test harness functions and macros.
 */

#ifndef TEST_H
#define TEST_H

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "h3api.h"
#include "latLng.h"

// H3-EXTENDED: libm tolerance helper for lat/lng round-trip tests at ext
// resolutions (16-22). Per playbook §9.2: round-trip tests cannot be
// byte-exact at high resolution because intermediate sin/cos/asin results
// vary across libm implementations (glibc vs musl vs Apple libm). Tighter
// tolerance for ext where cell edges drop to ~1cm (res 16) and below.
//
// Calibration (recalibrated 2026-05-05 — Session 7 item #3):
//
//   Single-platform measurement (macOS arm64, Apple libm 21.0.0):
//     1000 random g_in × 23 resolutions, cellToLatLng → latLngToCell →
//     cellToLatLng round-trip. Worst observed |Δlat| = |Δlng| = 0 rad
//     across every resolution including res 22 (cell edge ~1.1 cm).
//     Conclusion: round-trip is bit-deterministic on a single platform.
//     Existing ext tests rely on this (h1 == h2 byte-identity).
//
//   Cross-libm analytic bound (glibc / musl / Apple):
//     DBL_EPSILON ≈ 2.22e-16; a π-magnitude double has ULP ≈ 7e-16.
//     Pessimistic divergence at asin/atan2 across mainstream libms:
//     ≲ 50 ULPs → ≈ 3.5e-14 rad. 10× safety margin per playbook §9.2
//     gives ≈ 3.5e-13 rad. Use 1e-13 as a defensible cross-libm tolerance
//     for ext (rounds down for headroom — ~640 nm at Earth radius, well
//     below the ~1 cm res-22 cell edge).
//
//   Stock res: 1e-9 rad ≈ 6.4 mm — kept conservative per playbook §9.2.
//     Stock cell edges are ≥ 50 cm so this is comfortably loose.
//
// Cross-platform empirical calibration (against glibc/musl actuals via
// CI) is deferred. The helper is currently unused (round-trip ext tests
// use h1 == h2 bit-identity). When a future test first relies on it,
// recalibrate from observed CI output rather than this analytic bound.
static inline bool latlng_within_tolerance(LatLng a, LatLng b, int res) {
    double tolerance_rads = res >= 16 ? 1e-13 : 1e-9;
    return fabs(a.lat - b.lat) < tolerance_rads &&
           fabs(a.lng - b.lng) < tolerance_rads;
}

extern int globalTestCount;
extern const char *currentSuiteName;
extern const char *currentTestName;

#define t_assert(condition, msg)                                           \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "%s.%s: t_assert failed at %s:%d, %s, %s\n",   \
                    currentSuiteName, currentTestName, __FILE__, __LINE__, \
                    #condition, msg);                                      \
            exit(1);                                                       \
        }                                                                  \
        globalTestCount++;                                                 \
        printf(".");                                                       \
    } while (0)

#define t_assertSuccess(condition) t_assert(!(condition), "expected E_SUCCESS")

void t_assertBoundary(H3Index h3, const CellBoundary *b1);

#define SUITE(NAME)                                         \
    static void runTests(void);                             \
    int main(void) {                                        \
        currentSuiteName = #NAME;                           \
        printf("TEST %s\n", #NAME);                         \
        runTests();                                         \
        printf("\nDONE: %d assertions\n", globalTestCount); \
        return 0;                                           \
    }                                                       \
    void runTests(void)
#define TEST(NAME) currentTestName = #NAME;
#endif
