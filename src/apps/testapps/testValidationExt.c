/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 */
/** @file testValidationExt.c
 * @brief H3-Extended Phase E validation predicate gate test.
 *
 * Phase E gates from playbook §8.E (E1, E2 are CRITICAL — fail STOP):
 *   E1: _hasGoodTopBits accepts every valid ext cell (regression for §6.1).
 *   E2: _firstOneIndex returns correct bit position for cells with set
 *       bits in [64, 127] (regression for §6.2).
 *   E3: isValidCell returns true for 1,000 random valid ext cells, false
 *       for 1,000 corrupted ones.
 *   E4: _hasAny7UptoRes correctly flags an ext cell with a digit-7
 *       violation in the ext range (§6.3).
 *   E5: _hasAll7AfterRes correctly flags an ext cell with a non-7 digit
 *       past effective res (§6.4).
 *   E6: UBSAN gate for _zeroIndexDigits — call directly with the loop
 *       straddling the stock/ext boundary; must complete without
 *       negative-shift UB (regression for §6.6). The build's
 *       -fsanitize=undefined catches violations at runtime.
 *   E7: UBSAN gate for _incrementResDigit — exercised via the iterator
 *       (cellToChildren) at ext resolutions; must complete without
 *       negative-shift UB (regression for §6.10).
 *
 * The static-inline predicates _hasGoodTopBits / _hasAny7UptoRes /
 * _hasAll7AfterRes / _hasDeletedSubsequence are not directly linkable
 * from this TU; we exercise them through isValidCell, which calls all
 * four. _firstOneIndex was made externally linkable by Phase E5 (matches
 * the _h3Rotate60ccw convention).
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "constants.h"
#include "h3Index.h"
#include "h3api.h"
#include "iterators.h"
#include "test.h"

// _firstOneIndex (Phase E2 widening) — non-static so testValidationExt
// can probe it directly. See h3Index.c.
extern int _firstOneIndex(H3Index h);

// Pentagon base cells (mirrors isBaseCellPentagonArr in h3Index.c, which
// is file-local). Sourced from h3 v4.4.1 source-of-truth.
static const int kPentagonBaseCells[] = {
    4, 14, 24, 38, 49, 58, 63, 72, 83, 97, 107, 117,
};
static const int kPentagonBaseCellCount =
    (int)(sizeof(kPentagonBaseCells) / sizeof(kPentagonBaseCells[0]));

static int isPentagonBaseCell(int bc) {
    for (int i = 0; i < kPentagonBaseCellCount; i++) {
        if (kPentagonBaseCells[i] == bc) return 1;
    }
    return 0;
}

// Deterministic LCG (Numerical Recipes constants) for reproducible coverage.
static uint64_t lcg_next(uint64_t *state) {
    *state = (*state) * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

// Fabricate a clean ext cell at given effective res. Digits 1..res = 0..6,
// digits res+1..22 = sentinel 7 (from H3_INIT_EXT). Pentagon-skipped digit
// (1) avoided when the base cell is a pentagon to dodge spurious deleted-
// subsequence rejection.
static H3Index fabricate_valid_ext_cell(uint64_t *lcg, int eff_res,
                                        int base_cell) {
    H3Index h = H3_INIT_EXT;
    H3_SET_MODE(h, H3_CELL_MODE);
    H3_SET_BASE_CELL(h, base_cell);
    H3_SET_EFFECTIVE_RESOLUTION(h, eff_res);

    int isPent = isPentagonBaseCell(base_cell);
    uint64_t bits = lcg_next(lcg);
    for (int r = 1; r <= eff_res; r++) {
        int d = (int)(bits % 7);
        bits /= 7;
        if (bits == 0) bits = lcg_next(lcg);
        // For pentagon base cells, never emit the K_AXES digit (1) as the
        // first non-zero digit. Easiest: never emit 1 at all from this
        // fabricator; downstream tests pick a different digit if needed.
        if (isPent && d == 1) d = 2;
        H3_SET_DIGIT_AT_RES(h, r, d);
    }
    return h;
}

SUITE(validationExt) {
    // E1 (CRITICAL) — _hasGoodTopBits: clean ext cells across all ext res
    // and a representative spread of base cells must validate. Regression
    // for the §6.1 trap (stock right-shift collides ext flag with mode bit).
    TEST(e1_hasGoodTopBitsAcceptsExtCells) {
        uint64_t lcg = 0xE1AC1EBABE0FFC0EULL;
        const int kBases[] = {0, 1, 14, 50, 91, 121};
        const int nBases = (int)(sizeof(kBases) / sizeof(kBases[0]));

        for (int eff_res = 16; eff_res <= 22; eff_res++) {
            for (int i = 0; i < nBases; i++) {
                H3Index h = fabricate_valid_ext_cell(&lcg, eff_res, kBases[i]);
                int valid = H3_EXPORT(isValidCell)(h);
                t_assert(valid == 1,
                         "ext cell across all ext res must validate");
            }
        }
    }

    // E1 — corrupting any of the high-half "must be zero" reserved bits
    // (86-127) flips a clean ext cell to invalid.
    TEST(e1_hasGoodTopBitsRejectsReservedHighBits) {
        uint64_t lcg = 0xE1FACE0F8AD8AD00ULL;
        H3Index clean = fabricate_valid_ext_cell(&lcg, 19, 17);
        t_assert(H3_EXPORT(isValidCell)(clean) == 1, "baseline valid");

        // Set bit 86 (low edge of reserved area).
        H3Index dirty86 = clean | ((H3Index)1 << 86);
        t_assert(H3_EXPORT(isValidCell)(dirty86) == 0,
                 "bit 86 set rejects ext cell");

        // Set bit 127 (top edge).
        H3Index dirty127 = clean | ((H3Index)1 << 127);
        t_assert(H3_EXPORT(isValidCell)(dirty127) == 0,
                 "bit 127 set rejects ext cell");
    }

    // E1 — stock cells with non-zero high half must fail (§6.1: ext flag
    // clear ⇒ entire high half must be zero).
    TEST(e1_hasGoodTopBitsRejectsStockWithDirtyHighHalf) {
        // Build a clean stock cell (res 10), then poison the high half.
        H3Index stock = 0;
        H3_EXPORT(latLngToCell)(&((LatLng){.lat = 0.7, .lng = -1.3}), 10,
                                &stock);
        t_assert(H3_EXPORT(isValidCell)(stock) == 1, "baseline stock valid");
        t_assert((uint64_t)(stock >> 64) == 0, "baseline high half zero");

        // Set bit 64 (the ext flag) but leave stock-res field at 10 — that
        // would imply effective_res = 26 which is out of [16, 22], so
        // _hasGoodTopBits's "stock-res ≤ 6 for ext cells" rule rejects.
        H3Index halfState = stock | ((H3Index)1 << 64);
        t_assert(H3_EXPORT(isValidCell)(halfState) == 0,
                 "ext flag set with stock res > 6 must fail");

        // Set bit 90 with no ext flag: stock layout violation.
        H3Index dirtyHigh = stock | ((H3Index)1 << 90);
        t_assert(H3_EXPORT(isValidCell)(dirtyHigh) == 0,
                 "stock cell with bit 90 set must fail");
    }

    // E2 (CRITICAL) — _firstOneIndex single-bit sweep across all 128
    // positions. Regression for §6.2: stock used clzll on truncated low
    // half, UB when only set bit lives in [64, 127].
    TEST(e2_firstOneIndexSingleBitSweep) {
        for (int pos = 0; pos < 128; pos++) {
            H3Index h = (H3Index)1 << pos;
            int got = _firstOneIndex(h);
            t_assert(got == pos, "first-one matches the only set bit");
        }
        t_assert(_firstOneIndex((H3Index)0) == -1, "h == 0 returns -1");
    }

    // E2 — multiple-bit case: leading bit is the highest set position,
    // tested across stock and ext bit ranges.
    TEST(e2_firstOneIndexLeadingBitMixed) {
        // Bit 5 + bit 70 set → leading is 70 (high-half handled).
        H3Index h1 = ((H3Index)1 << 70) | ((H3Index)1 << 5);
        t_assert(_firstOneIndex(h1) == 70, "high-half leading bit");
        // Bit 30 + bit 50 set → leading is 50 (low-half only).
        H3Index h2 = ((H3Index)1 << 50) | ((H3Index)1 << 30);
        t_assert(_firstOneIndex(h2) == 50, "low-half leading bit");
        // Bit 127 (top) plus a low bit → leading is 127.
        H3Index h3 = ((H3Index)1 << 127) | ((H3Index)1 << 0);
        t_assert(_firstOneIndex(h3) == 127, "top-bit leading");
    }

    // E3 — full isValidCell: 1,000 random valid ext cells all validate;
    // 1,000 cells with one randomly-set reserved bit (86-127) all fail.
    TEST(e3_isValidCellRoundTripExtCells) {
        uint64_t lcg = 0xE3CAFE0FFC0FFEE0ULL;
        const int N = 1000;

        int validPass = 0;
        for (int i = 0; i < N; i++) {
            int eff_res = 16 + (int)(lcg_next(&lcg) % 7);
            int bc = (int)(lcg_next(&lcg) % NUM_BASE_CELLS);
            H3Index h = fabricate_valid_ext_cell(&lcg, eff_res, bc);
            if (H3_EXPORT(isValidCell)(h) == 1) validPass++;
        }
        t_assert(validPass == N,
                 "1000 valid ext cells all pass isValidCell");

        int corruptedRejected = 0;
        for (int i = 0; i < N; i++) {
            int eff_res = 16 + (int)(lcg_next(&lcg) % 7);
            int bc = (int)(lcg_next(&lcg) % NUM_BASE_CELLS);
            H3Index h = fabricate_valid_ext_cell(&lcg, eff_res, bc);
            // Set one random bit in 86..127 (reserved area).
            int bit = 86 + (int)(lcg_next(&lcg) % 42);
            h |= ((H3Index)1 << bit);
            if (H3_EXPORT(isValidCell)(h) == 0) corruptedRejected++;
        }
        t_assert(corruptedRejected == N,
                 "1000 cells with corrupted reserved bits all rejected");
    }

    // E4 — _hasAny7UptoRes (via isValidCell): place sentinel 7 at digit
    // position in the ext range. Stock impl missed bits 65-85 entirely.
    TEST(e4_hasAny7UptoResCatchesExtSentinel) {
        uint64_t lcg = 0xE4CAFEC0DEFACE00ULL;
        H3Index h = fabricate_valid_ext_cell(&lcg, 22, 17);
        t_assert(H3_EXPORT(isValidCell)(h) == 1, "baseline valid res 22");

        // Plant sentinel 7 at digit 18 (within effective res 22).
        H3_SET_DIGIT_AT_RES(h, 18, INVALID_DIGIT);
        t_assert(H3_EXPORT(isValidCell)(h) == 0,
                 "sentinel 7 at ext digit 18 fails validation");
    }

    // E5 — _hasAll7AfterRes (via isValidCell): plant a non-7 digit past
    // effective res. Stock impl returned true unconditionally for res>=15.
    TEST(e5_hasAll7AfterResCatchesNon7PastRes) {
        uint64_t lcg = 0xE5BADAB0DE5EAA00ULL;
        H3Index h = fabricate_valid_ext_cell(&lcg, 19, 17);
        t_assert(H3_EXPORT(isValidCell)(h) == 1, "baseline valid res 19");

        // Plant non-sentinel digit 3 at position 21 (effective_res=19).
        // Sentinel 7 is required there for a valid cell.
        H3_SET_DIGIT_AT_RES(h, 21, 3);
        t_assert(H3_EXPORT(isValidCell)(h) == 0,
                 "non-7 at digit 21 past res 19 fails validation");
    }

    // E5 — pentagon K-axis (digit 1) violation in ext range. Regression
    // for the §6.5 trap: stock _hasDeletedSubsequence windowed only the
    // low 45 bits and missed K-axis digits in positions 16-22.
    TEST(e5_pentagonKAxisInExtRange) {
        uint64_t lcg = 0xE5DEADC0FFEED4A1ULL;
        // Pentagon base cell 4. All stock digits 0 (so no K-axis
        // violation in 1-15), then plant K-axis at digit 17.
        H3Index h = H3_INIT_EXT;
        H3_SET_MODE(h, H3_CELL_MODE);
        H3_SET_BASE_CELL(h, 4);
        H3_SET_EFFECTIVE_RESOLUTION(h, 19);
        for (int r = 1; r <= 19; r++) H3_SET_DIGIT_AT_RES(h, r, 0);

        t_assert(H3_EXPORT(isValidCell)(h) == 1,
                 "all-zero pentagon ext cell baseline valid");

        H3_SET_DIGIT_AT_RES(h, 17, 1);  // K_AXES_DIGIT
        t_assert(H3_EXPORT(isValidCell)(h) == 0,
                 "K-axis at ext digit 17 of pentagon fails validation");
        (void)lcg;
    }

    // E6 — UBSAN gate for _zeroIndexDigits across the stock/ext boundary.
    // Stock body had `m <<= H3_PER_DIGIT_OFFSET * (MAX_H3_RES - end)` —
    // negative shift when end > 15. The Session 3 D4.1 widening replaced
    // that with a per-digit loop. Run the function with start=11..end=22
    // under the build's UBSAN; any negative-shift trip fails the test.
    TEST(e6_zeroIndexDigitsUBSANRange) {
        uint64_t lcg = 0xE6FEEDCAFEFADEEDULL;
        H3Index h = fabricate_valid_ext_cell(&lcg, 22, 17);
        // start straddles boundary, end at MAX_H3_EXT_RES.
        H3Index zeroed = _zeroIndexDigits(h, 11, MAX_H3_EXT_RES);
        // Verify: digits 11..22 are zero, ext flag preserved.
        for (int r = 11; r <= MAX_H3_EXT_RES; r++) {
            int d = H3_GET_DIGIT_AT_RES(zeroed, r);
            t_assert(d == 0, "_zeroIndexDigits sets digit to 0 across range");
        }
        // start == end (single position) — boundary condition.
        H3Index single = _zeroIndexDigits(h, 22, 22);
        t_assert(H3_GET_DIGIT_AT_RES(single, 22) == 0,
                 "single-position zero clears that digit");
        // start > end — degenerate; should be a no-op.
        H3Index noop = _zeroIndexDigits(h, 22, 11);
        t_assert(noop == h, "start > end returns input unchanged");
    }

    // E7 — UBSAN gate for _incrementResDigit. Indirect via iterator over
    // an ext parent: walk all 7 children at depth 1 (res 16 → 17), each
    // increment exercises _incrementResDigit at res 17 (ext range). Any
    // negative-shift UB in iterator increment trips here.
    TEST(e7_incrementResDigitUBSANViaIterator) {
        uint64_t lcg = 0xE7DEADC0FFEEC0DEULL;
        H3Index parent = fabricate_valid_ext_cell(&lcg, 16, 17);

        int64_t childCount = 0;
        t_assertSuccess(
            H3_EXPORT(cellToChildrenSize)(parent, 17, &childCount));
        t_assert(childCount == 7,
                 "ext res 16 → 7 res-17 children (hexagon)");

        H3Index kids[8] = {0};
        t_assertSuccess(H3_EXPORT(cellToChildren)(parent, 17, kids));

        for (int i = 0; i < (int)childCount; i++) {
            t_assert(H3_GET_EXT_FLAG(kids[i]) == 1, "child still ext");
            t_assert(H3_EXPORT(getResolution)(kids[i]) == 17,
                     "child at res 17");
        }
    }
}
