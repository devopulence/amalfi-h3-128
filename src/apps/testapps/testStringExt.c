/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 */
/** @file testStringExt.c
 * @brief H3-Extended Phase C string I/O tests for ext-resolution cells.
 *
 * Phase C gates from playbook §8.C:
 *   C1: stringToH3 round-trips a known res-15 stock cell byte-identically.
 *   C2: h3ToString produces a 32-char lowercase hex string for an ext cell.
 *   C3: round-trip 1,000 random ext cells through h3ToString → stringToH3
 *       byte-identically.
 *
 * Ext cells are fabricated via Group C macros (POC-1 pattern) since the
 * encoder isn't widened until Phase D1.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "h3Index.h"
#include "h3api.h"
#include "test.h"

// Deterministic linear-congruential generator (Numerical Recipes constants)
// for reproducible 1,000-cell coverage. Seeded once per test.
static uint64_t lcg_next(uint64_t *state) {
    *state = (*state) * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

// Fabricate an ext cell with the given effective resolution (16-22), pseudo-
// random base cell, and pseudo-random digits 1..res. All digits past res are
// initialized to sentinel 7 via H3_INIT_EXT.
static H3Index fabricate_ext_cell(uint64_t *lcg, int eff_res) {
    H3Index h = H3_INIT_EXT;  // mode=0, all digits 1-22 = sentinel 7, ext flag set
    H3_SET_MODE(h, H3_CELL_MODE);
    H3_SET_BASE_CELL(h, (int)(lcg_next(lcg) % NUM_BASE_CELLS));
    H3_SET_EFFECTIVE_RESOLUTION(h, eff_res);

    uint64_t bits = lcg_next(lcg);
    for (int r = 1; r <= eff_res; r++) {
        int d = (int)(bits % 7);  // 0..6 (never sentinel 7 — those go past res)
        bits /= 7;
        if (bits == 0) bits = lcg_next(lcg);
        H3_SET_DIGIT_AT_RES(h, r, d);
    }
    return h;
}

SUITE(stringExt) {
    // C1: stock res-15 cell round-trips identically.
    TEST(c1_stockRes15RoundTrip) {
        const char *str = "8f283470080808a";  // res 15 stock cell
        H3Index parsed = 0;
        t_assertSuccess(H3_EXPORT(stringToH3)(str, &parsed));

        char buf[64] = {0};
        t_assertSuccess(H3_EXPORT(h3ToString)(parsed, buf, sizeof(buf)));
        t_assert(strcmp(buf, str) == 0, "stock res-15 round-trips byte-identical");

        H3Index back = 0;
        t_assertSuccess(H3_EXPORT(stringToH3)(buf, &back));
        t_assert(back == parsed, "value identity preserved");
    }

    // C2: h3ToString produces 32-char lowercase hex output for an ext cell.
    TEST(c2_extProduces32CharOutput) {
        uint64_t lcg = 0xDEADBEEFCAFEBABEULL;
        H3Index ext = fabricate_ext_cell(&lcg, 16);

        // Sanity: ext flag must be set (high half non-zero).
        t_assert(H3_GET_EXT_FLAG(ext) == 1, "fabricated cell has ext flag set");
        t_assert((uint64_t)(ext >> 64) != 0, "ext cell has non-zero high half");

        char buf[64] = {0};
        t_assertSuccess(H3_EXPORT(h3ToString)(ext, buf, sizeof(buf)));
        t_assert(strlen(buf) == 32, "ext cell produces 32-char hex");

        // All chars must be lowercase hex.
        for (size_t i = 0; i < 32; i++) {
            char c = buf[i];
            t_assert(
                (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'),
                "all 32 chars are lowercase hex");
        }

        // Buffer < 33 must fail.
        char small[20] = {0};
        H3Error err = H3_EXPORT(h3ToString)(ext, small, sizeof(small));
        t_assert(err == E_MEMORY_BOUNDS,
                 "ext cell to undersized buffer returns E_MEMORY_BOUNDS");
    }

    // C3: 1,000 random ext cells round-trip via h3ToString → stringToH3
    // byte-identically. This is the architecture gate per the Session 1
    // kickoff prompt — fail = STOP.
    TEST(c3_thousandRandomExtCellsRoundTrip) {
        uint64_t lcg = 0x123456789ABCDEF0ULL;
        for (int i = 0; i < 1000; i++) {
            int eff_res = 16 + (int)(lcg_next(&lcg) % 7);  // 16..22
            H3Index original = fabricate_ext_cell(&lcg, eff_res);

            char buf[64] = {0};
            t_assertSuccess(H3_EXPORT(h3ToString)(original, buf, sizeof(buf)));
            t_assert(strlen(buf) == 32, "ext cell always produces 32-char hex");

            H3Index recovered = 0;
            t_assertSuccess(H3_EXPORT(stringToH3)(buf, &recovered));
            t_assert(recovered == original,
                     "round-trip byte-identical for ext cell");
        }
    }

    // Stock cells with high half = 0 emit 16-char (or shorter for leading-zero
    // forms — sprintf %PRIx64 omits leading zeros). Verifies non-negotiable #3
    // string compatibility.
    TEST(stockProducesShortForm) {
        H3Index h = (H3Index)0x85283473fffffffULL;  // 15-char value
        char buf[64] = {0};
        t_assertSuccess(H3_EXPORT(h3ToString)(h, buf, sizeof(buf)));
        t_assert(strcmp(buf, "85283473fffffff") == 0,
                 "stock cell produces legacy 15-char form");
    }

    // Both 16-char legacy and 32-char canonical inputs must produce identical
    // H3Index values when high half is zero.
    TEST(stockAcceptedInBothFormats) {
        const char *short_str = "85283473fffffff";  // 15 chars
        const char *canonical = "0000000000000000085283473fffffff";  // 32 chars
        t_assert(strlen(canonical) == 32, "canonical fixture is 32 chars");

        H3Index a = 0, b = 0;
        t_assertSuccess(H3_EXPORT(stringToH3)(short_str, &a));
        t_assertSuccess(H3_EXPORT(stringToH3)(canonical, &b));
        t_assert(a == b, "16-char and 32-char-with-zero-high parse to same value");
    }

    // Reject obviously invalid inputs.
    TEST(rejectsBadInputs) {
        H3Index out = 0;
        t_assert(H3_EXPORT(stringToH3)("", &out) == E_FAILED,
                 "empty string rejected");
        t_assert(H3_EXPORT(stringToH3)("xyz", &out) == E_FAILED,
                 "non-hex chars rejected");
        t_assert(
            H3_EXPORT(stringToH3)("123456789abcdef0123456789abcdef01", &out) ==
                E_FAILED,
            "33-char input rejected (too long)");
    }
}
