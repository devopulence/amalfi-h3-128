/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 */
/** @file testAccessorExt.c
 * @brief H3-Extended Phase D3 accessor gate tests.
 *
 * Phase D3 gate from playbook §8.D:
 *   D3-G1: getResolution returns 16-22 for ext cells (verifies effective-res).
 *
 * Plus regression coverage for the four widened accessors:
 *   - getResolution       (Rule LB)
 *   - getIndexDigit       (Rule GR + DR)
 *   - constructCell       (Rule GR + INIT + RW + DW)
 *   - getBaseCellNumber   (no widening — base cell is res-independent;
 *                          test exists to confirm ext cells decode correctly)
 */

#include <stdint.h>

#include "constants.h"
#include "h3Index.h"
#include "h3api.h"
#include "test.h"

static uint64_t lcg_next(uint64_t *state) {
    *state = (*state) * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

SUITE(accessorExt) {
    // D3-G1: getResolution returns the effective res (16-22) for ext cells.
    TEST(d3g1_getResolution_returnsEffectiveResForExtCell) {
        for (int res = MAX_H3_RES + 1; res <= MAX_H3_EXT_RES; res++) {
            for (int baseCell = 0; baseCell < 5; baseCell++) {
                H3Index h = 0;
                setH3Index(&h, res, baseCell, CENTER_DIGIT);
                t_assert(H3_EXPORT(getResolution)(h) == res,
                         "getResolution returns effective res for ext cell");
            }
        }
    }

    // D3 stock byte-identity: getResolution unchanged for stock cells.
    TEST(d3_getResolution_stockResStillByteIdentical) {
        for (int res = 0; res <= MAX_H3_RES; res++) {
            H3Index h = 0;
            setH3Index(&h, res, 0, CENTER_DIGIT);
            t_assert(H3_EXPORT(getResolution)(h) == res,
                     "getResolution returns res for stock cell");
            t_assert((uint64_t)(h >> 64) == 0, "stock cell high half zero");
        }
    }

    // D3 getIndexDigit: accepts ext res, dispatches correctly.
    TEST(d3_getIndexDigit_extResDispatch) {
        H3Index h = 0;
        setH3Index(&h, 19, 7, K_AXES_DIGIT);
        // All digits 1..19 should equal K_AXES_DIGIT (initDigit).
        for (int r = 1; r <= 19; r++) {
            int digit = -1;
            t_assertSuccess(H3_EXPORT(getIndexDigit)(h, r, &digit));
            t_assert(digit == K_AXES_DIGIT,
                     "ext-res digit at r in [1, 19] equals initDigit");
        }
        // Digits 20-22 should be sentinel 7 (from H3_INIT_EXT past res).
        for (int r = 20; r <= MAX_H3_EXT_RES; r++) {
            int digit = -1;
            t_assertSuccess(H3_EXPORT(getIndexDigit)(h, r, &digit));
            t_assert(digit == INVALID_DIGIT,
                     "digit past effective res = sentinel 7");
        }
    }

    // D3 getIndexDigit: contract bounds preserved at the new upper bound.
    TEST(d3_getIndexDigit_outOfRangeRejected) {
        H3Index h = 0;
        setH3Index(&h, 5, 3, CENTER_DIGIT);
        int digit;
        t_assert(H3_EXPORT(getIndexDigit)(h, 0, &digit) == E_RES_DOMAIN,
                 "res 0 rejected");
        t_assert(H3_EXPORT(getIndexDigit)(h, -1, &digit) == E_RES_DOMAIN,
                 "negative res rejected");
        t_assert(H3_EXPORT(getIndexDigit)(h, MAX_H3_EXT_RES + 1, &digit) ==
                     E_RES_DOMAIN,
                 "above MAX_H3_EXT_RES rejected");
    }

    // D3 constructCell: builds ext cells with explicit digit arrays.
    TEST(d3_constructCell_extResRoundTrip) {
        // Build a hexagon ext cell at res 19 with all-zero digits.
        int digits[19] = {0};
        H3Index h = 0;
        t_assertSuccess(H3_EXPORT(constructCell)(19, 5, digits, &h));
        t_assert(H3_GET_EXT_FLAG(h) == 1, "constructCell sets ext flag");
        t_assert(H3_GET_EFFECTIVE_RESOLUTION(h) == 19, "effective res = 19");
        t_assert(H3_EXPORT(getBaseCellNumber)(h) == 5, "base cell preserved");
        t_assert(H3_EXPORT(getResolution)(h) == 19,
                 "getResolution returns 19 (effective)");

        // Verify all digits via getIndexDigit.
        for (int r = 1; r <= 19; r++) {
            int d;
            t_assertSuccess(H3_EXPORT(getIndexDigit)(h, r, &d));
            t_assert(d == 0, "digit r in [1, 19] = 0");
        }
    }

    // D3 constructCell: walk through several ext resolutions with random
    // digits to exercise the dispatching SET path for the ext range.
    TEST(d3_constructCell_extResVariousDigits) {
        uint64_t lcg = 0xD3CCC11C0DECCC1DULL;
        for (int res = MAX_H3_RES + 1; res <= MAX_H3_EXT_RES; res++) {
            int digits[MAX_H3_EXT_RES] = {0};
            uint64_t bits = lcg_next(&lcg);
            for (int r = 0; r < res; r++) {
                digits[r] = (int)(bits % 7);  // 0..6, never 7
                bits /= 7;
                if (bits == 0) bits = lcg_next(&lcg);
            }
            // Use a non-pentagon base cell to avoid deleted-subsequence issues.
            int baseCell = 5;
            H3Index h = 0;
            t_assertSuccess(
                H3_EXPORT(constructCell)(res, baseCell, digits, &h));
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(h) == res,
                     "constructCell effective res");
            for (int r = 1; r <= res; r++) {
                int d;
                t_assertSuccess(H3_EXPORT(getIndexDigit)(h, r, &d));
                t_assert(d == digits[r - 1], "digit round-trips");
            }
        }
    }

    // D3 getBaseCellNumber: ext cells return correct base cell.
    TEST(d3_getBaseCellNumber_extCell) {
        for (int baseCell = 0; baseCell < NUM_BASE_CELLS; baseCell++) {
            H3Index h = 0;
            setH3Index(&h, 19, baseCell, CENTER_DIGIT);
            t_assert(H3_EXPORT(getBaseCellNumber)(h) == baseCell,
                     "getBaseCellNumber returns correct base cell on ext");
        }
    }
}
