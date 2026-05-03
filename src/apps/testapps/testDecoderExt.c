/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 */
/** @file testDecoderExt.c
 * @brief H3-Extended Phase D2 decoder gate tests.
 *
 * Phase D2 gate from playbook §8.D:
 *   D2-G1: cellToLatLng round-trips 1,000 res-19 cells within libm tolerance.
 *
 * Round-trip pattern (mirrors stock testCellToLatLng):
 *   1. Encode an arbitrary lat/lng → h1 at res 19.
 *   2. cellToLatLng(h1) → g_center.
 *   3. latLngToCell(g_center, 19) → h2.
 *   4. Assert h1 == h2 (the cell's center must round-trip to itself).
 *
 * This is bit-identity at the cell level (the same contract the stock
 * testCellToLatLng uses). The libm tolerance helper is reserved for
 * defensive center-vs-center checks where two valid encodings may
 * diverge by a sub-bit in lat/lng but still resolve to the same cell.
 *
 * Prerequisites for the gate to close at res 19:
 *   - D1 encoder widening (latLngToCell, vec3ToCell, _faceIjkToH3, setH3Index).
 *   - D2 decoder widening (_h3ToFaceIjk{,WithInitializedFijk}, cellToVec3).
 *   - D6 rotation widening (_h3LeadingNonZeroDigit, _h3Rotate60{ccw,cw},
 *     _h3RotatePent60{ccw,cw}) so encoder and decoder agree on canonical
 *     orientation across all 22 digit positions.
 *   - faceijk.c maxDimByCIIres + unitScaleByCIIres extended to length 21
 *     so the post-Class-III increment to res 20 is in-bounds for ext res 19.
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

SUITE(decoderExt) {
    // D2 stock guard: stock res 0-15 cell-center round-trip is bit-identical
    // (regression for the D2 widening commits — must keep stock byte-identity).
    TEST(d2_stockResCellCenterRoundTripIdentical) {
        uint64_t lcg = 0xD25710CFE17ECADEULL;
        for (int res = 0; res <= MAX_H3_RES; res++) {
            for (int i = 0; i < 5; i++) {
                LatLng g_in = deterministic_latlng(&lcg);
                H3Index h1 = 0;
                t_assertSuccess(H3_EXPORT(latLngToCell)(&g_in, res, &h1));
                LatLng g_center;
                t_assertSuccess(H3_EXPORT(cellToLatLng)(h1, &g_center));
                H3Index h2 = 0;
                t_assertSuccess(H3_EXPORT(latLngToCell)(&g_center, res, &h2));
                t_assert(h1 == h2,
                         "stock res cell center re-encodes to same cell");
                t_assert((uint64_t)(h1 >> 64) == 0, "stock cell high half zero");
            }
        }
    }

    // D2-G1: 1,000 res-19 cell-center round-trips. Reports closure rate;
    // gate passes only if all 1,000 round-trip bit-identically.
    TEST(d2g1_res19_cellCenterRoundTripIdentical) {
        uint64_t lcg = 0xD2671EE5DA7AAEFEULL;
        const int N = 1000;
        int closed = 0;
        int failed = 0;
        H3Index first_fail_h1 = 0;
        H3Index first_fail_h2 = 0;
        LatLng first_fail_in = {0, 0};
        LatLng first_fail_center = {0, 0};
        int first_fail_base_cell = -1;
        for (int i = 0; i < N; i++) {
            LatLng g_in = deterministic_latlng(&lcg);
            H3Index h1 = 0;
            t_assertSuccess(H3_EXPORT(latLngToCell)(&g_in, 19, &h1));
            LatLng g_center;
            t_assertSuccess(H3_EXPORT(cellToLatLng)(h1, &g_center));
            H3Index h2 = 0;
            t_assertSuccess(H3_EXPORT(latLngToCell)(&g_center, 19, &h2));
            t_assert(H3_GET_EXT_FLAG(h1) == 1, "h1 is ext cell");
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(h1) == 19, "h1 res 19");
            if (h1 == h2) {
                closed++;
            } else {
                if (failed == 0) {
                    first_fail_h1 = h1;
                    first_fail_h2 = h2;
                    first_fail_in = g_in;
                    first_fail_center = g_center;
                    first_fail_base_cell = H3_GET_BASE_CELL(h1);
                }
                failed++;
            }
        }
        if (failed > 0) {
            fprintf(stderr,
                    "\n[D2-G1] cell-center round-trip closed: %d/%d "
                    "(%d failed)\n"
                    "[D2-G1] first fail: in=(%.6f, %.6f) center=(%.6f, %.6f)\n"
                    "[D2-G1] first fail: h1 hi=0x%016llx lo=0x%016llx\n"
                    "[D2-G1] first fail: h2 hi=0x%016llx lo=0x%016llx\n"
                    "[D2-G1] first fail: base cell %d\n",
                    closed, N, failed, first_fail_in.lat, first_fail_in.lng,
                    first_fail_center.lat, first_fail_center.lng,
                    (unsigned long long)(first_fail_h1 >> 64),
                    (unsigned long long)(uint64_t)first_fail_h1,
                    (unsigned long long)(first_fail_h2 >> 64),
                    (unsigned long long)(uint64_t)first_fail_h2,
                    first_fail_base_cell);
        }
        t_assert(closed == N,
                 "all 1,000 res-19 cell centers re-encode bit-identical");
    }
}
