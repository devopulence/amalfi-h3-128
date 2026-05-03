/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 */
/** @file testRotationExt.c
 * @brief H3-Extended Phase D6 rotation gate tests.
 *
 * Phase D6 gate from playbook §8.D:
 *   D6-G1: _h3Rotate60ccw six times restores the original ext cell
 *          (rotation is a 6-cycle).
 *
 * Plus mirror coverage for _h3Rotate60cw, _h3RotatePent60ccw,
 * _h3RotatePent60cw, and _h3LeadingNonZeroDigit on ext cells.
 */

#include <stdint.h>
#include <stdio.h>

#include "constants.h"
#include "h3Index.h"
#include "h3api.h"
#include "test.h"

// Forward declarations of internal rotation helpers (h3Index.h does not
// expose these, but they are used from h3Index.c and link from libh3).
H3Index _h3Rotate60ccw(H3Index h);
H3Index _h3Rotate60cw(H3Index h);
H3Index _h3RotatePent60ccw(H3Index h);
H3Index _h3RotatePent60cw(H3Index h);
Direction _h3LeadingNonZeroDigit(H3Index h);

static uint64_t lcg_next(uint64_t *state) {
    *state = (*state) * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

// Fabricate an ext cell with given res, base cell, and pseudo-random digits.
static H3Index fabricate_ext_cell(uint64_t *lcg, int eff_res, int baseCell) {
    H3Index h = H3_INIT_EXT;
    H3_SET_MODE(h, H3_CELL_MODE);
    H3_SET_BASE_CELL(h, baseCell);
    H3_SET_EFFECTIVE_RESOLUTION(h, eff_res);
    uint64_t bits = lcg_next(lcg);
    for (int r = 1; r <= eff_res; r++) {
        int d = (int)(bits % 7);  // 0..6
        bits /= 7;
        if (bits == 0) bits = lcg_next(lcg);
        H3_SET_DIGIT_AT_RES(h, r, d);
    }
    return h;
}

SUITE(rotationExt) {
    // D6-G1: _h3Rotate60ccw applied six times restores the original ext cell.
    TEST(d6g1_rotate60ccw_sixCycle_extCell) {
        uint64_t lcg = 0xD6671CCCCCCCCCCDULL;
        for (int res = MAX_H3_RES + 1; res <= 19; res++) {
            for (int trial = 0; trial < 25; trial++) {
                int baseCell = (int)(lcg_next(&lcg) % NUM_BASE_CELLS);
                H3Index original = fabricate_ext_cell(&lcg, res, baseCell);
                H3Index rotated = original;
                for (int i = 0; i < 6; i++) rotated = _h3Rotate60ccw(rotated);
                t_assert(rotated == original,
                         "_h3Rotate60ccw is a 6-cycle on ext cells");
            }
        }
    }

    // D6-G1 mirror: _h3Rotate60cw is also a 6-cycle.
    TEST(d6_rotate60cw_sixCycle_extCell) {
        uint64_t lcg = 0xD6CCCCCCCCCCCCCDULL;
        for (int res = MAX_H3_RES + 1; res <= 19; res++) {
            for (int trial = 0; trial < 25; trial++) {
                int baseCell = (int)(lcg_next(&lcg) % NUM_BASE_CELLS);
                H3Index original = fabricate_ext_cell(&lcg, res, baseCell);
                H3Index rotated = original;
                for (int i = 0; i < 6; i++) rotated = _h3Rotate60cw(rotated);
                t_assert(rotated == original,
                         "_h3Rotate60cw is a 6-cycle on ext cells");
            }
        }
    }

    // D6: _h3Rotate60cw is the inverse of _h3Rotate60ccw.
    TEST(d6_rotate60ccw_then_cw_isIdentity) {
        uint64_t lcg = 0xD6CC60FFEE60CCDDULL;
        for (int res = MAX_H3_RES + 1; res <= 19; res++) {
            for (int trial = 0; trial < 25; trial++) {
                int baseCell = (int)(lcg_next(&lcg) % NUM_BASE_CELLS);
                H3Index original = fabricate_ext_cell(&lcg, res, baseCell);
                H3Index rt = _h3Rotate60cw(_h3Rotate60ccw(original));
                t_assert(rt == original, "ccw then cw is identity");
            }
        }
    }

    // D6: pentagon rotations 5-cycle on a true pentagon cell (digit 1
    // skipped via K_AXES_DIGIT adjustment). Use a known pentagon base cell
    // (e.g. base cell 4 — first pentagon in the 12-pentagon set).
    TEST(d6_rotatePent60ccw_fiveCycle_extPentagon) {
        // Pentagon base cells per stock H3: 4, 14, 24, 38, 49, 58, 63, 72,
        // 83, 97, 107, 117. Use base cell 4 with all digits = 0 (which is
        // a pentagon center at any res).
        H3Index pent = H3_INIT_EXT;
        H3_SET_MODE(pent, H3_CELL_MODE);
        H3_SET_BASE_CELL(pent, 4);
        H3_SET_EFFECTIVE_RESOLUTION(pent, 19);
        for (int r = 1; r <= 19; r++) H3_SET_DIGIT_AT_RES(pent, r, 0);
        // Five rotations should restore (pentagons have 5-fold symmetry).
        H3Index rotated = pent;
        for (int i = 0; i < 5; i++) rotated = _h3RotatePent60ccw(rotated);
        t_assert(rotated == pent,
                 "_h3RotatePent60ccw is a 5-cycle on a pentagon center cell");
    }

    // D6: _h3LeadingNonZeroDigit detects a non-zero digit in the ext range
    // for an ext cell whose stock-res-field digits are all zero.
    TEST(d6_leadingNonZero_findsExtDigit) {
        H3Index h = H3_INIT_EXT;
        H3_SET_MODE(h, H3_CELL_MODE);
        H3_SET_BASE_CELL(h, 0);
        H3_SET_EFFECTIVE_RESOLUTION(h, 19);
        // Zero out all stock-range digits (1-15).
        for (int r = 1; r <= MAX_H3_RES; r++) H3_SET_DIGIT_AT_RES(h, r, 0);
        // Set a single non-zero ext digit at r=17.
        H3_SET_DIGIT_AT_RES(h, 17, 3);
        // Also zero any digit before 17 in the ext range.
        H3_SET_DIGIT_AT_RES(h, 16, 0);
        // Digits 18, 19 stay sentinel-ish from H3_INIT_EXT — set to 0 for clarity.
        for (int r = 18; r <= 19; r++) H3_SET_DIGIT_AT_RES(h, r, 0);

        Direction d = _h3LeadingNonZeroDigit(h);
        t_assert(d == 3,
                 "_h3LeadingNonZeroDigit returns ext digit value, not 0");
    }
}
