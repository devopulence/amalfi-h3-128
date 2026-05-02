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
// Tolerance values are empirical — the playbook calls for calibration by
// running glibc/musl/darwin and taking 10x the worst observed delta.
// Initial pre-calibration values: 1e-12 rad (~6.4 um) for ext, 1e-9 rad
// (~6.4 mm) for stock. Used by Phase D-G1 / D-G2 round-trip tests.
static inline bool latlng_within_tolerance(LatLng a, LatLng b, int res) {
    double tolerance_rads = res >= 16 ? 1e-12 : 1e-9;
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
