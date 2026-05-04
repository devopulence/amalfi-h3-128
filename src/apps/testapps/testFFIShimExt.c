/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 */
/** @file testFFIShimExt.c
 * @brief H3-Extended Phase D7 FFI shim gate test.
 *
 * Phase D7 gates from playbook §8.D / §7.1:
 *   D7-G1: All 10 shim functions linkable from a C test program.
 *   D7-G2: sizeof(H3Index) == 16 and _Alignof(H3Index) == 16 consistent
 *          across TUs (cross-TU consistency probe).
 *   D7-G3: 1,000 random ext cells round-trip through
 *          h3_ext_h3_to_string → h3_ext_string_to_h3 byte-identically
 *          (mirror of C2/C3 at the FFI boundary).
 *
 * Pattern 4 from POC_MANDATE.md: all H3Index values pass by pointer,
 * all returns are H3Error.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "h3ExtShim.h"
#include "h3Index.h"
#include "h3api.h"
#include "test.h"

// Deterministic LCG (Numerical Recipes constants) for reproducible coverage.
static uint64_t lcg_next(uint64_t *state) {
    *state = (*state) * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

// Fabricate an ext cell with given effective res, pseudo-random base cell
// and digits 1..res. Digits past res are sentinel 7 from H3_INIT_EXT.
static H3Index fabricate_ext_cell(uint64_t *lcg, int eff_res) {
    H3Index h = H3_INIT_EXT;
    H3_SET_MODE(h, H3_CELL_MODE);
    H3_SET_BASE_CELL(h, (int)(lcg_next(lcg) % NUM_BASE_CELLS));
    H3_SET_EFFECTIVE_RESOLUTION(h, eff_res);

    uint64_t bits = lcg_next(lcg);
    for (int r = 1; r <= eff_res; r++) {
        int d = (int)(bits % 7);
        bits /= 7;
        if (bits == 0) bits = lcg_next(lcg);
        H3_SET_DIGIT_AT_RES(h, r, d);
    }
    return h;
}

SUITE(ffiShimExt) {
    // D7-G1 — All 10 shim functions linkable + minimal smoke through each.
    TEST(d7g1_allTenFunctionsLinkable) {
        H3Index out = 0;
        H3Error err;

        // 1. lat_lng_to_cell — Sandy Hook NJ at res 19 (ext range).
        err = h3_ext_lat_lng_to_cell(0.7128 /* rad */, -1.2820 /* rad */, 19,
                                     &out);
        t_assertSuccess(err);
        t_assert(H3_GET_EXT_FLAG(out) == 1, "ext flag set on res 19 output");

        // 2. cell_to_lat_lng (round-trip the cell we just built).
        double lat = 0, lng = 0;
        err = h3_ext_cell_to_lat_lng(&out, &lat, &lng);
        t_assertSuccess(err);
        t_assert(!isnan(lat) && !isnan(lng), "lat/lng finite from shim");

        // 3. cell_to_parent — ext cell to stock parent (high-half scrub regression).
        H3Index parent = (H3Index)0xFFFFFFFFFFFFFFFFULL;  // poison high half
        err = h3_ext_cell_to_parent(&out, 5, &parent);
        t_assertSuccess(err);
        t_assert((uint64_t)(parent >> 64) == 0,
                 "ext→stock parent scrubs high half");

        // 4. cell_to_children_size — sized in advance for #5.
        int64_t childCount = 0;
        err = h3_ext_cell_to_children_size(&parent, 7, &childCount);
        t_assertSuccess(err);
        t_assert(childCount == 49,  // 7^(7-5) hexagon
                 "children size 7^2 for hexagon parent at res 5");

        // 5. cell_to_children — populates caller-allocated buffer.
        H3Index kids[64] = {0};
        int64_t emitted = 0;
        err = h3_ext_cell_to_children(&parent, 7, kids, &emitted);
        t_assertSuccess(err);
        t_assert(emitted == childCount, "cell_to_children count matches size");

        // 6. is_valid_cell — call links + returns success. NOTE: the ext-cell
        // validity result depends on Phase E predicate widening (playbook
        // §6.1/§6.2). D7's gate is linkability, not semantic validity.
        int valid = -1;
        err = h3_ext_is_valid_cell(&out, &valid);
        t_assertSuccess(err);
        t_assert(valid == 0 || valid == 1, "is_valid_cell returns 0 or 1");

        // 7. get_resolution — must report effective res 19, not stock-res field.
        int reportedRes = -1;
        err = h3_ext_get_resolution(&out, &reportedRes);
        t_assertSuccess(err);
        t_assert(reportedRes == 19,
                 "get_resolution returns effective res for ext cell");

        // 8. grid_distance — distance from cell to itself is 0.
        int64_t dist = 999;
        err = h3_ext_grid_distance(&out, &out, &dist);
        t_assertSuccess(err);
        t_assert(dist == 0, "self-distance is 0");

        // 9. h3_to_string — 32-char lowercase hex output.
        char buf[64] = {0};
        err = h3_ext_h3_to_string(&out, buf, sizeof(buf));
        t_assertSuccess(err);
        t_assert(strlen(buf) == 32, "ext cell serializes to 32 hex chars");

        // 10. string_to_h3 — round-trip back.
        H3Index parsed = 0;
        err = h3_ext_string_to_h3(buf, &parsed);
        t_assertSuccess(err);
        t_assert(parsed == out, "string round-trip byte-identical");
    }

    // D7-G2 — Cross-TU consistency: sizeof and alignof agree between
    // h3ExtShim.c (defines the probes) and this test TU (calls them).
    TEST(d7g2_crossTuSizeofAlignofConsistent) {
        // From this TU.
        size_t local_size = sizeof(H3Index);
        size_t local_align = __alignof__(H3Index);

        // From h3ExtShim.c TU.
        size_t shim_size = h3_ext_sizeof_h3index();
        size_t shim_align = h3_ext_alignof_h3index();

        t_assert(local_size == 16, "test TU sizeof(H3Index) == 16");
        t_assert(shim_size == 16, "shim TU sizeof(H3Index) == 16");
        t_assert(local_size == shim_size, "sizeof matches across TUs");

        t_assert(local_align == 16, "test TU _Alignof(H3Index) == 16");
        t_assert(shim_align == 16, "shim TU _Alignof(H3Index) == 16");
        t_assert(local_align == shim_align, "alignof matches across TUs");
    }

    // D7-G3 — 1,000 ext cells round-trip through the FFI string pair
    // byte-identically. Mirrors C2/C3 at the FFI boundary.
    TEST(d7g3_thousandExtCellStringRoundTrip) {
        uint64_t lcg = 0xD7E5BABEC0DEFEEDULL;
        const int N = 1000;
        int closed = 0;
        H3Index first_failed_in = 0, first_failed_back = 0;

        for (int i = 0; i < N; i++) {
            int eff_res = 16 + (int)(lcg_next(&lcg) % 7);  // 16..22
            H3Index in = fabricate_ext_cell(&lcg, eff_res);

            char buf[64] = {0};
            H3Error err = h3_ext_h3_to_string(&in, buf, sizeof(buf));
            if (err != E_SUCCESS) {
                if (closed == 0) {
                    first_failed_in = in;
                }
                continue;
            }
            t_assert(strlen(buf) == 32, "32-char output for every ext cell");

            H3Index back = 0;
            err = h3_ext_string_to_h3(buf, &back);
            if (err != E_SUCCESS || back != in) {
                if (closed == 0) {
                    first_failed_in = in;
                    first_failed_back = back;
                }
                continue;
            }
            closed++;
        }

        if (closed != N) {
            fprintf(stderr,
                    "D7-G3: %d/%d round-trips closed; first divergence "
                    "in.lo=%016llx in.hi=%016llx back.lo=%016llx back.hi=%016llx\n",
                    closed, N,
                    (unsigned long long)(uint64_t)first_failed_in,
                    (unsigned long long)(uint64_t)(first_failed_in >> 64),
                    (unsigned long long)(uint64_t)first_failed_back,
                    (unsigned long long)(uint64_t)(first_failed_back >> 64));
        }
        t_assert(closed == N, "1,000 ext cells string-round-trip byte-identical");
    }

    // Defensive coverage — NULL-pointer arguments return E_FAILED, not
    // segfault. Mirror of POC-4 ABI-15..ABI-21.
    TEST(d7nullPointerErrorContract) {
        H3Index dummy = 0;
        char strbuf[64] = {0};
        double d = 0;
        int64_t i64 = 0;
        int i = 0;

        t_assert(h3_ext_lat_lng_to_cell(0.0, 0.0, 5, NULL) == E_FAILED, "");
        t_assert(h3_ext_cell_to_lat_lng(NULL, &d, &d) == E_FAILED, "");
        t_assert(h3_ext_cell_to_lat_lng(&dummy, NULL, &d) == E_FAILED, "");
        t_assert(h3_ext_cell_to_parent(NULL, 0, &dummy) == E_FAILED, "");
        t_assert(h3_ext_cell_to_children_size(NULL, 0, &i64) == E_FAILED, "");
        t_assert(h3_ext_cell_to_children(NULL, 0, &dummy, &i64) == E_FAILED, "");
        t_assert(h3_ext_is_valid_cell(NULL, &i) == E_FAILED, "");
        t_assert(h3_ext_get_resolution(NULL, &i) == E_FAILED, "");
        t_assert(h3_ext_grid_distance(NULL, &dummy, &i64) == E_FAILED, "");
        t_assert(h3_ext_h3_to_string(NULL, strbuf, sizeof(strbuf)) == E_FAILED, "");
        t_assert(h3_ext_string_to_h3(NULL, &dummy) == E_FAILED, "");
    }
}
