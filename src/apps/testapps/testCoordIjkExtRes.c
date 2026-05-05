/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 */
/** @file testCoordIjkExtRes.c
 * @brief v0.2.0 CoordIJK int64 widening regression — res 20-22 round-trips.
 *
 * Pre-v0.2.0 status (Sessions 1-5): all ext suites scoped to res 19 because
 * `int` CoordIJK fields and `int` table entries / locals overflow at res
 * 20-22 (POC-5 §"Failure Boundary"). v0.2.0 widens:
 *   - CoordIJK { int → int64_t } (commit 57672bc6)
 *   - maxDimByCIIres / unitScaleByCIIres → int64_t[23] (commit f84ab171)
 *   - _adjustOverageClassII / _adjustPentVertOverage int locals → int64_t
 *   - _setIJK / _ijkScale params widened to int64_t for table-derived args
 *
 * This suite proves the widening eliminates the POC-5 failure boundary:
 *
 *   CO-G1: 500 random res 20 (Class II) cell-center round-trips bit-identical.
 *   CO-G2: 500 random res 21 (Class III) cell-center round-trips bit-identical.
 *   CO-G3: 500 random res 22 (Class II) cell-center round-trips bit-identical.
 *   CO-G4: pentagon-touching res 19+ cells do not trip UBSAN on overage.
 *   CO-G5: stock res 0-15 round-trip remains bit-identical (CoordIJK widening
 *          must not regress stock byte-identity).
 *
 * The 3 user-spec geos (Monmouth, Palm Beach, Piano di Sorrento) are also
 * exercised in CO-G6 to confirm POC-5's per-geo predictions are now FALSE.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "constants.h"
#include "h3Index.h"
#include "h3api.h"
#include "test.h"

static uint64_t lcg_next(uint64_t *state) {
    *state = (*state) * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

static LatLng deterministic_latlng(uint64_t *lcg) {
    double u = (double)(lcg_next(lcg) >> 11) / (double)(1ULL << 53);
    double v = (double)(lcg_next(lcg) >> 11) / (double)(1ULL << 53);
    LatLng g;
    g.lat = (u - 0.5) * (M_PI - 1e-9);
    g.lng = (v - 0.5) * (2.0 * M_PI - 1e-9);
    return g;
}

static int round_trip_count(uint64_t seed, int res, int n) {
    uint64_t lcg = seed;
    int closed = 0;
    for (int i = 0; i < n; i++) {
        LatLng g_in = deterministic_latlng(&lcg);
        H3Index h1 = 0;
        H3Error e1 = H3_EXPORT(latLngToCell)(&g_in, res, &h1);
        if (e1) continue;
        LatLng g_center;
        H3Error e2 = H3_EXPORT(cellToLatLng)(h1, &g_center);
        if (e2) continue;
        H3Index h2 = 0;
        H3Error e3 = H3_EXPORT(latLngToCell)(&g_center, res, &h2);
        if (e3) continue;
        if (h1 == h2) closed++;
    }
    return closed;
}

SUITE(coordIjkExtRes) {
    /* CO-G1: res 20 (Class II) — 500 round-trips bit-identical. Pre-widening
     * this would have already worked since maxDim[20] fit int32, but the
     * Session-2 deferral noted that "ext suites scope to res 19" for safety
     * margin; this confirms res 20 is now first-class. */
    TEST(co_g1_res20_classII_roundTrip) {
        const int N = 500;
        int closed = round_trip_count(0xC0DE2010CF1100AAULL, 20, N);
        if (closed != N) {
            fprintf(stderr, "\n[CO-G1] res 20 closed %d/%d\n", closed, N);
        }
        t_assert(closed == N,
                 "res 20 Class II cell centers re-encode bit-identical");
    }

    /* CO-G2: res 21 (Class III) — 500 round-trips bit-identical. POC-5
     * predicted overflow because adjRes=22 reads from the table (now
     * widened). This confirms the widening fixes the predicted boundary. */
    TEST(co_g2_res21_classIII_roundTrip) {
        const int N = 500;
        int closed = round_trip_count(0xC0DE2110CF11AABBULL, 21, N);
        if (closed != N) {
            fprintf(stderr, "\n[CO-G2] res 21 closed %d/%d\n", closed, N);
        }
        t_assert(closed == N,
                 "res 21 Class III cell centers re-encode bit-identical");
    }

    /* CO-G3: res 22 (Class II) — 500 round-trips bit-identical. THE
     * headline gate: maxDim[22]=2*7^11=3.95e9 > INT32_MAX, requires
     * int64_t tables + locals. */
    TEST(co_g3_res22_classII_roundTrip) {
        const int N = 500;
        int closed = round_trip_count(0xC0DE2210CF1122CCULL, 22, N);
        if (closed != N) {
            fprintf(stderr, "\n[CO-G3] res 22 closed %d/%d\n", closed, N);
        }
        t_assert(closed == N,
                 "res 22 Class II cell centers re-encode bit-identical");
    }

    /* CO-G4: pentagon-touching res 19+ cells via getPentagons probe. POC-5
     * §"CO-24" predicted UBSAN trip at res 19 due to Class III sqrt(7)
     * amplifier; widening removes the int32 limit so getPentagons returns
     * 12 distinct cells without overage UB. */
    TEST(co_g4_pentagon_res19_no_ubsan) {
        for (int res = 19; res <= MAX_H3_EXT_RES; res++) {
            H3Index pents[12];
            t_assertSuccess(H3_EXPORT(getPentagons)(res, pents));
            int distinct = 0;
            for (int i = 0; i < 12; i++) {
                int dup = 0;
                for (int j = 0; j < i; j++)
                    if (pents[i] == pents[j]) {
                        dup = 1;
                        break;
                    }
                if (!dup) distinct++;
                t_assert(H3_GET_EFFECTIVE_RESOLUTION(pents[i]) == res,
                         "pentagon at expected res");
            }
            t_assert(distinct == 12, "12 distinct pentagons at ext res");
        }
    }

    /* CO-G5: stock res 0-15 round-trip remains bit-identical (CoordIJK
     * widening must not regress stock byte-identity — guard for the
     * struct typedef + table widening). */
    TEST(co_g5_stockRes_byteIdentical) {
        uint64_t lcg = 0xC0DE5710CB711DEEULL;
        for (int res = 0; res <= MAX_H3_RES; res++) {
            for (int i = 0; i < 10; i++) {
                LatLng g_in = deterministic_latlng(&lcg);
                H3Index h1 = 0;
                t_assertSuccess(H3_EXPORT(latLngToCell)(&g_in, res, &h1));
                LatLng g_center;
                t_assertSuccess(H3_EXPORT(cellToLatLng)(h1, &g_center));
                H3Index h2 = 0;
                t_assertSuccess(H3_EXPORT(latLngToCell)(&g_center, res, &h2));
                t_assert(h1 == h2, "stock cell center re-encodes identically");
                t_assert((uint64_t)(h1 >> 64) == 0,
                         "stock cell high half stays zero");
            }
        }
    }

    /* CO-G6: 3 POC-5 geos × res 20-22. POC-5 predicted overflow at these
     * boundaries; v0.2.0 widening should eliminate it. Each geo is the
     * specific lat/lng the user named in the Session-5 brief. */
    TEST(co_g6_poc5_geos_at_ext_res) {
        const LatLng geos[3] = {
            {40.33 * M_PI / 180.0, -73.99 * M_PI / 180.0},  /* Monmouth */
            {26.71 * M_PI / 180.0, -80.05 * M_PI / 180.0},  /* Palm Beach */
            {40.63 * M_PI / 180.0,  14.40 * M_PI / 180.0},  /* Sorrento */
        };
        const char *names[3] = {"Monmouth", "Palm Beach", "Sorrento"};
        for (int g = 0; g < 3; g++) {
            for (int res = 20; res <= 22; res++) {
                H3Index h1 = 0;
                t_assertSuccess(H3_EXPORT(latLngToCell)(&geos[g], res, &h1));
                t_assert(H3_GET_EXT_FLAG(h1) == 1,
                         "POC-5 geo encodes to ext cell");
                t_assert(H3_GET_EFFECTIVE_RESOLUTION(h1) == res,
                         "effective res matches request");
                LatLng center;
                t_assertSuccess(H3_EXPORT(cellToLatLng)(h1, &center));
                H3Index h2 = 0;
                t_assertSuccess(H3_EXPORT(latLngToCell)(&center, res, &h2));
                if (h1 != h2) {
                    fprintf(stderr,
                            "\n[CO-G6] %s @ res %d: round-trip FAIL\n",
                            names[g], res);
                }
                t_assert(h1 == h2,
                         "POC-5 geo round-trip bit-identical post-widening");
            }
        }
    }
}
