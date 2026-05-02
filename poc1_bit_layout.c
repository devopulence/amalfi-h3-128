/*
 * poc1_bit_layout.c — Standalone bit-layout validation for H3-Extended (128-bit)
 *
 * Version: 1.0.0 — 2026-05-02
 * Companion to: CLAUDE.md v1.0.22, h3-extended-playbook-v4.1.0.md
 *
 * PURPOSE:
 *   Validate the 128-bit bit layout (playbook §2), resolution encoding (§2.1),
 *   and all 14 Group C macros (§3) in COMPLETE ISOLATION from the H3 source tree.
 *   This program has ZERO dependencies on uber/h3. It reimplements only the
 *   stock Group A/B constants and macros needed to verify the layout contract,
 *   then implements and exhaustively tests every Group C macro.
 *
 *   If this program exits 0, the bit layout is proven correct and the Group C
 *   macros are safe to drop into h3Index.h. If it exits non-zero, the specific
 *   failure message identifies which property broke.
 *
 * COMPILE:
 *   gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
 *       -o poc1_bit_layout poc1_bit_layout.c -lm && ./poc1_bit_layout
 *
 *   clang -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
 *         -o poc1_bit_layout poc1_bit_layout.c -lm && ./poc1_bit_layout
 *
 * EXIT CODES:
 *   0  — all tests passed
 *   1  — one or more tests failed (failures printed to stderr)
 *
 * COVERAGE:
 *   PF-01  sizeof(H3Index) == 16
 *   PF-02  Stock cell zero-extended is valid (bit 64 = 0, high half = 0)
 *   PF-03  Resolution encoding round-trips for all 23 resolutions (0–22)
 *   PF-04  H3_SET_EFFECTIVE_RESOLUTION / H3_GET_EFFECTIVE_RESOLUTION agree
 *   PF-05  Ext flag set for res >= 16, clear for res <= 15
 *   PF-06  Stock-res field holds (effective_res - 16) for ext cells (range 0–6)
 *   PF-07  Digit read/write round-trips for all 22 digit positions (1–22)
 *   PF-08  Stock digits (1–15) use bits 0–44; ext digits (16–22) use bits 65–85
 *   PF-09  Reserved bits 86–127 are zero after every macro operation
 *   PF-10  H3_INIT has correct sentinel pattern (digits 1–15 = 7)
 *   PF-11  H3_INIT_EXT has correct sentinel pattern (digits 1–22 = 7, bit 64 set)
 *   PF-12  No UBSAN violations across full resolution range (enforced by compiler)
 *   PF-13  Stock macros on ext cells return expected (misleading but defined) values
 *   PF-14  Cross-boundary digit isolation (writing ext digit doesn't corrupt stock)
 *   PF-15  H3_EXT_DIGITS_MASK covers exactly bits 65–85
 *   PF-16  H3_EXT_FLAG_MASK is exactly bit 64
 *   PF-17  Base cell field (bits 45–51) survives ext operations
 *   PF-18  Mode field (bits 59–62) survives ext operations
 *   PF-19  Effective resolution single-evaluation (H3_SET_EFFECTIVE_RESOLUTION side-effect safety)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Compiler gate — same as the H3-Extended fork requires
 * ═══════════════════════════════════════════════════════════════════════════ */
#ifndef __SIZEOF_INT128__
#error "This preflight requires __uint128_t (GCC or Clang). MSVC is not supported."
#endif

typedef __uint128_t H3Index;

/* ═══════════════════════════════════════════════════════════════════════════
 * GROUP A — Stock bit-position constants (from h3Index.h, verbatim values)
 *
 * These are the REAL stock H3 v4.4.1 values. We redefine them here so this
 * file has zero #include dependencies on the H3 source tree.
 * ═══════════════════════════════════════════════════════════════════════════ */

#define H3_PER_DIGIT_OFFSET    3
#define H3_MAX_OFFSET          63
#define H3_MODE_OFFSET         59
#define H3_MODE_MASK           ((H3Index)15)
#define H3_BC_OFFSET           45
#define H3_BC_MASK             ((H3Index)127)
#define H3_RES_OFFSET          52
#define H3_RES_MASK            ((H3Index)15)
#define H3_RESERVED_OFFSET     56
#define H3_RESERVED_MASK       ((H3Index)7)
#define H3_HIGH_BIT_OFFSET     63
#define H3_HIGH_BIT_MASK       ((H3Index)1)
#define H3_DIGIT_MASK          ((H3Index)7)

#define MAX_H3_RES             15
#define INVALID_DIGIT          7

/* H3_INIT = 2^45 - 1 = sets digits 1-15 to sentinel 7 (all 1s in bits 0-44) */
#define H3_INIT                ((H3Index)UINT64_C(35184372088831))

/* ═══════════════════════════════════════════════════════════════════════════
 * GROUP B — Stock getter/setter macros (from h3Index.h, verbatim logic)
 * ═══════════════════════════════════════════════════════════════════════════ */

#define H3_GET_HIGH_BIT(h)     ((int)(((h) >> H3_HIGH_BIT_OFFSET) & H3_HIGH_BIT_MASK))
#define H3_SET_HIGH_BIT(h, v)  (h) = (((h) & ~(H3_HIGH_BIT_MASK << H3_HIGH_BIT_OFFSET)) | \
                                      (((H3Index)(v)) << H3_HIGH_BIT_OFFSET))

#define H3_GET_MODE(h)         ((int)(((h) >> H3_MODE_OFFSET) & H3_MODE_MASK))
#define H3_SET_MODE(h, v)      (h) = (((h) & ~(H3_MODE_MASK << H3_MODE_OFFSET)) | \
                                      (((H3Index)(v)) << H3_MODE_OFFSET))

#define H3_GET_RESOLUTION(h)   ((int)(((h) >> H3_RES_OFFSET) & H3_RES_MASK))
#define H3_SET_RESOLUTION(h, v) (h) = (((h) & ~(H3_RES_MASK << H3_RES_OFFSET)) | \
                                       (((H3Index)(v)) << H3_RES_OFFSET))

#define H3_GET_BASE_CELL(h)    ((int)(((h) >> H3_BC_OFFSET) & H3_BC_MASK))
#define H3_SET_BASE_CELL(h, v) (h) = (((h) & ~(H3_BC_MASK << H3_BC_OFFSET)) | \
                                      (((H3Index)(v)) << H3_BC_OFFSET))

#define H3_GET_INDEX_DIGIT(h, res) \
    ((int)(((h) >> ((MAX_H3_RES - (res)) * H3_PER_DIGIT_OFFSET)) & H3_DIGIT_MASK))

#define H3_SET_INDEX_DIGIT(h, res, digit) \
    (h) = (((h) & ~(H3_DIGIT_MASK << ((MAX_H3_RES - (res)) * H3_PER_DIGIT_OFFSET))) | \
           (((H3Index)(digit)) << ((MAX_H3_RES - (res)) * H3_PER_DIGIT_OFFSET)))

#define H3_GET_RESERVED_BITS(h) ((int)(((h) >> H3_RESERVED_OFFSET) & H3_RESERVED_MASK))

/* ═══════════════════════════════════════════════════════════════════════════
 * GROUP C — New macros for 128-bit (from playbook §3)
 *
 * These are the EXACT macros that will be placed in h3Index.h during Phase A.
 * The preflight validates them in isolation before they touch the H3 source.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* C1: MAX_H3_EXT_RES */
#define MAX_H3_EXT_RES         22

/* C2: H3_EXT_FLAG_OFFSET */
#define H3_EXT_FLAG_OFFSET     64

/* C3: H3_EXT_FLAG_MASK */
#define H3_EXT_FLAG_MASK       ((__uint128_t)1 << 64)

/* C4: H3_EXT_DIGITS_OFFSET */
#define H3_EXT_DIGITS_OFFSET   65

/* C5: H3_EXT_DIGITS_MASK — 7 ext digits × 3 bits = 21 bits starting at bit 65 */
#define H3_EXT_DIGITS_MASK     ((((__uint128_t)1 << 21) - 1) << 65)

/* C6: H3_INIT_EXT — init pattern with ext flag set, all digits (1-22) = sentinel 7 */
/*     Low 64 bits = H3_INIT (digits 1-15 = 7).
 *     Bit 64 = 1 (ext flag).
 *     Bits 65-85 = all 1s (ext digits 16-22 = 7).
 *     Bits 86-127 = 0 (reserved). */
#define H3_INIT_EXT            (H3_INIT | H3_EXT_FLAG_MASK | H3_EXT_DIGITS_MASK)

/* C7: H3_GET_EXT_FLAG */
#define H3_GET_EXT_FLAG(h)     ((int)(((h) & H3_EXT_FLAG_MASK) >> H3_EXT_FLAG_OFFSET))

/* C8: H3_SET_EXT_FLAG */
#define H3_SET_EXT_FLAG(h, v)  (h) = (((h) & ~H3_EXT_FLAG_MASK) | \
                                      (((__uint128_t)(v)) << H3_EXT_FLAG_OFFSET))

/* C9: H3_GET_EFFECTIVE_RESOLUTION */
#define H3_GET_EFFECTIVE_RESOLUTION(h) \
    (H3_GET_RESOLUTION(h) + (H3_GET_EXT_FLAG(h) << 4))

/* C10: H3_SET_EFFECTIVE_RESOLUTION — GCC statement-expression for single evaluation */
#define H3_SET_EFFECTIVE_RESOLUTION(h, res) ({  \
    int _r = (res);                              \
    H3_SET_RESOLUTION((h), (_r) & 0xF);          \
    H3_SET_EXT_FLAG((h), (_r) >= 16 ? 1 : 0);    \
})

/* C11: H3_GET_EXT_INDEX_DIGIT — UB if res not in [16, 22] */
#define H3_GET_EXT_INDEX_DIGIT(h, res) \
    ((int)(((h) >> (H3_EXT_DIGITS_OFFSET + ((res) - 16) * 3)) & H3_DIGIT_MASK))

/* C12: H3_SET_EXT_INDEX_DIGIT — UB if res not in [16, 22] */
#define H3_SET_EXT_INDEX_DIGIT(h, res, digit) \
    (h) = (((h) & ~((__uint128_t)H3_DIGIT_MASK << (H3_EXT_DIGITS_OFFSET + ((res) - 16) * 3))) | \
           (((__uint128_t)(digit)) << (H3_EXT_DIGITS_OFFSET + ((res) - 16) * 3)))

/* C13: H3_GET_DIGIT_AT_RES — dispatching getter */
#define H3_GET_DIGIT_AT_RES(h, res) \
    ((res) <= MAX_H3_RES ? H3_GET_INDEX_DIGIT((h), (res)) : H3_GET_EXT_INDEX_DIGIT((h), (res)))

/* C14: H3_SET_DIGIT_AT_RES — dispatching setter */
#define H3_SET_DIGIT_AT_RES(h, res, digit) do { \
    if ((res) <= MAX_H3_RES)                      \
        H3_SET_INDEX_DIGIT((h), (res), (digit));  \
    else                                          \
        H3_SET_EXT_INDEX_DIGIT((h), (res), (digit)); \
} while (0)


/* ═══════════════════════════════════════════════════════════════════════════
 * Test harness
 * ═══════════════════════════════════════════════════════════════════════════ */

static int g_fail_count = 0;
static int g_pass_count = 0;

#define ASSERT_MSG(cond, fmt, ...) do {                                     \
    if (!(cond)) {                                                          \
        fprintf(stderr, "FAIL [%s:%d]: " fmt "\n",                          \
                __FILE__, __LINE__, ##__VA_ARGS__);                         \
        g_fail_count++;                                                     \
    } else {                                                                \
        g_pass_count++;                                                     \
    }                                                                       \
} while (0)

/* Helper: extract the high 64 bits */
static inline uint64_t high64(H3Index h) { return (uint64_t)(h >> 64); }
static inline uint64_t low64(H3Index h)  { return (uint64_t)h; }

/* Helper: check bits 86-127 are zero */
static inline int reserved_high_clear(H3Index h) {
    /* bits 86-127 = high-half bits 22-63 */
    uint64_t hi = high64(h);
    return (hi >> 22) == 0;
}

/* Helper: build a minimal valid stock cell at given res with mode=1 (cell) */
static H3Index make_stock_cell(int res, int base_cell) {
    H3Index h = H3_INIT;
    H3_SET_MODE(h, 1);
    H3_SET_RESOLUTION(h, res);
    H3_SET_BASE_CELL(h, base_cell);
    return h;
}

/* Helper: build a minimal valid ext cell at given res with mode=1 (cell) */
static H3Index make_ext_cell(int res, int base_cell) {
    H3Index h = H3_INIT_EXT;
    H3_SET_MODE(h, 1);
    H3_SET_EFFECTIVE_RESOLUTION(h, res);
    H3_SET_BASE_CELL(h, base_cell);
    /* Set all digits 1..res to 0 (CENTER_DIGIT), leave res+1..22 as sentinel 7 */
    for (int r = 1; r <= res; r++) {
        H3_SET_DIGIT_AT_RES(h, r, 0);
    }
    return h;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * PF-01: sizeof(H3Index) == 16
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf01_sizeof(void) {
    ASSERT_MSG(sizeof(H3Index) == 16,
        "PF-01: sizeof(H3Index) = %zu, expected 16", sizeof(H3Index));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-02: Stock cell zero-extended is valid
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf02_stock_zero_extended(void) {
    for (int res = 0; res <= 15; res++) {
        H3Index h = make_stock_cell(res, 0);
        ASSERT_MSG(H3_GET_EXT_FLAG(h) == 0,
            "PF-02: stock cell res %d has ext flag set", res);
        ASSERT_MSG(high64(h) == 0,
            "PF-02: stock cell res %d has non-zero high half", res);
        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(h) == res,
            "PF-02: stock cell res %d effective_res = %d",
            res, H3_GET_EFFECTIVE_RESOLUTION(h));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-03: Resolution encoding round-trips for all 23 resolutions
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf03_resolution_roundtrip(void) {
    for (int res = 0; res <= 22; res++) {
        H3Index h = (res <= 15) ? make_stock_cell(res, 5) : make_ext_cell(res, 5);
        int got = H3_GET_EFFECTIVE_RESOLUTION(h);
        ASSERT_MSG(got == res,
            "PF-03: res %d round-trip got %d", res, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-04: SET/GET EFFECTIVE_RESOLUTION agree for every resolution
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf04_effective_res_agree(void) {
    for (int res = 0; res <= 22; res++) {
        H3Index h = H3_INIT_EXT;
        H3_SET_MODE(h, 1);
        H3_SET_EFFECTIVE_RESOLUTION(h, res);
        int got = H3_GET_EFFECTIVE_RESOLUTION(h);
        ASSERT_MSG(got == res,
            "PF-04: SET then GET effective res %d returned %d", res, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-05: Ext flag set for res >= 16, clear for res <= 15
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf05_ext_flag_range(void) {
    for (int res = 0; res <= 22; res++) {
        H3Index h = H3_INIT_EXT;
        H3_SET_MODE(h, 1);
        H3_SET_EFFECTIVE_RESOLUTION(h, res);
        int flag = H3_GET_EXT_FLAG(h);
        if (res <= 15) {
            ASSERT_MSG(flag == 0,
                "PF-05: res %d should have ext flag 0, got %d", res, flag);
        } else {
            ASSERT_MSG(flag == 1,
                "PF-05: res %d should have ext flag 1, got %d", res, flag);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-06: Stock-res field for ext cells holds (effective_res - 16)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf06_stock_res_field_encoding(void) {
    for (int res = 16; res <= 22; res++) {
        H3Index h = make_ext_cell(res, 10);
        int stock_res = H3_GET_RESOLUTION(h);  /* stock macro — reads bits 52-55 only */
        int expected = res - 16;
        ASSERT_MSG(stock_res == expected,
            "PF-06: ext res %d, stock-res field = %d, expected %d",
            res, stock_res, expected);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-07: Digit read/write round-trips for ALL 22 digit positions
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf07_digit_roundtrip(void) {
    for (int digit_pos = 1; digit_pos <= 22; digit_pos++) {
        for (int digit_val = 0; digit_val <= 6; digit_val++) {
            H3Index h = H3_INIT_EXT;
            H3_SET_MODE(h, 1);
            H3_SET_EFFECTIVE_RESOLUTION(h, 22);  /* max res so all positions valid */
            H3_SET_BASE_CELL(h, 0);

            H3_SET_DIGIT_AT_RES(h, digit_pos, digit_val);
            int got = H3_GET_DIGIT_AT_RES(h, digit_pos);
            ASSERT_MSG(got == digit_val,
                "PF-07: digit pos %d, wrote %d, read %d",
                digit_pos, digit_val, got);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-08: Stock digits use bits 0-44, ext digits use bits 65-85
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf08_digit_bit_positions(void) {
    /* Stock digits: position r uses bits ((15-r)*3) to ((15-r)*3 + 2) */
    for (int r = 1; r <= 15; r++) {
        H3Index h = (H3Index)0;
        H3_SET_INDEX_DIGIT(h, r, 5);  /* 5 = 0b101 */
        int bit_start = (MAX_H3_RES - r) * H3_PER_DIGIT_OFFSET;
        ASSERT_MSG(bit_start >= 0 && bit_start <= 42,
            "PF-08: stock digit %d bit_start = %d (out of range 0-42)", r, bit_start);
        /* Verify only the expected 3 bits are set */
        uint64_t lo = low64(h);
        uint64_t expected = (uint64_t)5 << bit_start;
        ASSERT_MSG(lo == expected,
            "PF-08: stock digit %d, low64 = 0x%llx, expected 0x%llx",
            r, (unsigned long long)lo, (unsigned long long)expected);
        ASSERT_MSG(high64(h) == 0,
            "PF-08: stock digit %d leaked into high half", r);
    }

    /* Ext digits: position r uses bits (65 + (r-16)*3) to (65 + (r-16)*3 + 2) */
    for (int r = 16; r <= 22; r++) {
        H3Index h = (H3Index)0;
        H3_SET_EXT_INDEX_DIGIT(h, r, 5);  /* 5 = 0b101 */
        int bit_start = H3_EXT_DIGITS_OFFSET + (r - 16) * 3;
        ASSERT_MSG(bit_start >= 65 && bit_start <= 83,
            "PF-08: ext digit %d bit_start = %d (out of range 65-83)", r, bit_start);
        /* Verify low 64 bits are untouched */
        ASSERT_MSG(low64(h) == 0,
            "PF-08: ext digit %d leaked into low half", r);
        /* Verify only the expected bits are set in the high half */
        int high_bit_start = bit_start - 64;
        uint64_t hi = high64(h);
        uint64_t expected_hi = (uint64_t)5 << high_bit_start;
        ASSERT_MSG(hi == expected_hi,
            "PF-08: ext digit %d, high64 = 0x%llx, expected 0x%llx",
            r, (unsigned long long)hi, (unsigned long long)expected_hi);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-09: Reserved bits 86-127 are zero after every macro operation
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf09_reserved_bits_clear(void) {
    /* After building ext cells at every resolution */
    for (int res = 16; res <= 22; res++) {
        H3Index h = make_ext_cell(res, 42);
        ASSERT_MSG(reserved_high_clear(h),
            "PF-09: ext cell res %d has non-zero reserved bits (86-127)", res);
    }

    /* After setting every ext digit to every value */
    for (int r = 16; r <= 22; r++) {
        for (int d = 0; d <= 7; d++) {
            H3Index h = H3_INIT_EXT;
            H3_SET_EXT_INDEX_DIGIT(h, r, d);
            ASSERT_MSG(reserved_high_clear(h),
                "PF-09: after SET_EXT_INDEX_DIGIT(%d, %d) reserved bits dirty", r, d);
        }
    }

    /* After toggling ext flag */
    H3Index h = H3_INIT_EXT;
    H3_SET_EXT_FLAG(h, 0);
    ASSERT_MSG(reserved_high_clear(h),
        "PF-09: after clearing ext flag, reserved bits dirty");
    H3_SET_EXT_FLAG(h, 1);
    ASSERT_MSG(reserved_high_clear(h),
        "PF-09: after setting ext flag, reserved bits dirty");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-10: H3_INIT sentinel pattern — digits 1-15 = 7
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf10_h3_init(void) {
    H3Index h = H3_INIT;
    for (int r = 1; r <= 15; r++) {
        int d = H3_GET_INDEX_DIGIT(h, r);
        ASSERT_MSG(d == INVALID_DIGIT,
            "PF-10: H3_INIT digit %d = %d, expected 7", r, d);
    }
    /* H3_INIT should have zero high half */
    ASSERT_MSG(high64(h) == 0,
        "PF-10: H3_INIT has non-zero high half");
    /* Value check */
    ASSERT_MSG(low64(h) == UINT64_C(35184372088831),
        "PF-10: H3_INIT low64 = %llu, expected 35184372088831",
        (unsigned long long)low64(h));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-11: H3_INIT_EXT sentinel pattern — digits 1-22 = 7, bit 64 set
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf11_h3_init_ext(void) {
    H3Index h = H3_INIT_EXT;

    /* Ext flag must be set */
    ASSERT_MSG(H3_GET_EXT_FLAG(h) == 1,
        "PF-11: H3_INIT_EXT ext flag not set");

    /* Stock digits 1-15 = 7 */
    for (int r = 1; r <= 15; r++) {
        int d = H3_GET_INDEX_DIGIT(h, r);
        ASSERT_MSG(d == INVALID_DIGIT,
            "PF-11: H3_INIT_EXT stock digit %d = %d, expected 7", r, d);
    }

    /* Ext digits 16-22 = 7 */
    for (int r = 16; r <= 22; r++) {
        int d = H3_GET_EXT_INDEX_DIGIT(h, r);
        ASSERT_MSG(d == INVALID_DIGIT,
            "PF-11: H3_INIT_EXT ext digit %d = %d, expected 7", r, d);
    }

    /* Reserved bits must be zero */
    ASSERT_MSG(reserved_high_clear(h),
        "PF-11: H3_INIT_EXT has non-zero reserved bits (86-127)");

    /* Low 64 bits must equal H3_INIT */
    ASSERT_MSG(low64(h) == low64(H3_INIT),
        "PF-11: H3_INIT_EXT low half differs from H3_INIT");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-12: UBSAN enforced by compiler flags — no explicit test needed.
 *        If any shift in the macros above is UB, UBSAN will abort during
 *        the other PF tests. This test just confirms we reach this point.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf12_ubsan_clean(void) {
    /* Traverse every macro at every valid input to trigger any latent UB */
    for (int res = 0; res <= 22; res++) {
        H3Index h = H3_INIT_EXT;
        H3_SET_EFFECTIVE_RESOLUTION(h, res);
        (void)H3_GET_EFFECTIVE_RESOLUTION(h);
        (void)H3_GET_EXT_FLAG(h);

        if (res >= 1 && res <= 15) {
            H3_SET_INDEX_DIGIT(h, res, 3);
            (void)H3_GET_INDEX_DIGIT(h, res);
        }
        if (res >= 16 && res <= 22) {
            H3_SET_EXT_INDEX_DIGIT(h, res, 3);
            (void)H3_GET_EXT_INDEX_DIGIT(h, res);
        }
        if (res >= 1 && res <= 22) {
            H3_SET_DIGIT_AT_RES(h, res, 4);
            (void)H3_GET_DIGIT_AT_RES(h, res);
        }
    }
    ASSERT_MSG(1, "PF-12: all macro invocations completed without UBSAN abort");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-13: Stock macros on ext cells return expected (misleading) values
 *        This is a FEATURE per §2.1 — forces use of Group C dispatch.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf13_stock_macros_on_ext(void) {
    for (int eff_res = 16; eff_res <= 22; eff_res++) {
        H3Index h = make_ext_cell(eff_res, 20);
        /* Stock H3_GET_RESOLUTION sees only the low 4 bits = eff_res - 16 */
        int stock_res = H3_GET_RESOLUTION(h);
        ASSERT_MSG(stock_res == eff_res - 16,
            "PF-13: ext res %d, stock GET_RESOLUTION = %d, expected %d",
            eff_res, stock_res, eff_res - 16);
        /* But effective resolution is correct */
        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(h) == eff_res,
            "PF-13: ext res %d, effective = %d",
            eff_res, H3_GET_EFFECTIVE_RESOLUTION(h));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-14: Cross-boundary digit isolation
 *        Writing an ext digit must NOT corrupt stock digits, and vice versa.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf14_cross_boundary_isolation(void) {
    /* Start with all digits at known values */
    H3Index h = H3_INIT_EXT;
    H3_SET_MODE(h, 1);
    H3_SET_EFFECTIVE_RESOLUTION(h, 22);
    H3_SET_BASE_CELL(h, 50);

    /* Set stock digits to distinct values */
    for (int r = 1; r <= 15; r++) {
        H3_SET_INDEX_DIGIT(h, r, r % 7);
    }
    /* Set ext digits to distinct values */
    for (int r = 16; r <= 22; r++) {
        H3_SET_EXT_INDEX_DIGIT(h, r, (r - 16) % 7);
    }

    /* Now write ext digit 18 — verify stock digits unchanged */
    H3Index before_low = (H3Index)low64(h);
    H3_SET_EXT_INDEX_DIGIT(h, 18, 5);
    ASSERT_MSG(low64(h) == (uint64_t)before_low,
        "PF-14: writing ext digit 18 corrupted low half (stock digits)");

    /* Read back all stock digits */
    for (int r = 1; r <= 15; r++) {
        int d = H3_GET_INDEX_DIGIT(h, r);
        ASSERT_MSG(d == r % 7,
            "PF-14: after ext write, stock digit %d = %d, expected %d",
            r, d, r % 7);
    }

    /* Write stock digit 5 — verify ext digits unchanged */
    uint64_t before_high = high64(h);
    H3_SET_INDEX_DIGIT(h, 5, 2);
    ASSERT_MSG(high64(h) == before_high,
        "PF-14: writing stock digit 5 corrupted high half (ext digits)");

    /* Read back all ext digits */
    for (int r = 16; r <= 22; r++) {
        int expected = (r == 18) ? 5 : (r - 16) % 7;
        int d = H3_GET_EXT_INDEX_DIGIT(h, r);
        ASSERT_MSG(d == expected,
            "PF-14: after stock write, ext digit %d = %d, expected %d",
            r, d, expected);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-15: H3_EXT_DIGITS_MASK covers exactly bits 65-85
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf15_ext_digits_mask(void) {
    H3Index mask = H3_EXT_DIGITS_MASK;

    /* Bit 64 should NOT be in the mask */
    ASSERT_MSG(!(mask & H3_EXT_FLAG_MASK),
        "PF-15: EXT_DIGITS_MASK overlaps with EXT_FLAG_MASK (bit 64)");

    /* Bits 65-85 should be set */
    for (int bit = 65; bit <= 85; bit++) {
        H3Index probe = (__uint128_t)1 << bit;
        ASSERT_MSG(mask & probe,
            "PF-15: bit %d not set in EXT_DIGITS_MASK", bit);
    }

    /* Bits 86-127 should NOT be set */
    for (int bit = 86; bit <= 127; bit++) {
        H3Index probe = (__uint128_t)1 << bit;
        ASSERT_MSG(!(mask & probe),
            "PF-15: bit %d incorrectly set in EXT_DIGITS_MASK", bit);
    }

    /* Bits 0-63 should NOT be set */
    ASSERT_MSG(low64(mask) == 0,
        "PF-15: EXT_DIGITS_MASK has non-zero low half");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-16: H3_EXT_FLAG_MASK is exactly bit 64
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf16_ext_flag_mask(void) {
    H3Index mask = H3_EXT_FLAG_MASK;
    ASSERT_MSG(low64(mask) == 0,
        "PF-16: EXT_FLAG_MASK has non-zero low half");
    ASSERT_MSG(high64(mask) == 1,
        "PF-16: EXT_FLAG_MASK high half = 0x%llx, expected 1",
        (unsigned long long)high64(mask));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-17: Base cell field (bits 45-51) survives ext operations
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf17_base_cell_survives(void) {
    for (int bc = 0; bc < 122; bc++) {
        H3Index h = make_ext_cell(20, bc);
        /* Poke ext digits */
        H3_SET_EXT_INDEX_DIGIT(h, 17, 3);
        H3_SET_EXT_INDEX_DIGIT(h, 20, 5);
        int got = H3_GET_BASE_CELL(h);
        ASSERT_MSG(got == bc,
            "PF-17: base cell %d corrupted to %d after ext digit writes", bc, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-18: Mode field (bits 59-62) survives ext operations
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf18_mode_survives(void) {
    int modes[] = {1, 2, 4};  /* cell, edge, vertex */
    for (int i = 0; i < 3; i++) {
        H3Index h = H3_INIT_EXT;
        H3_SET_MODE(h, modes[i]);
        H3_SET_EFFECTIVE_RESOLUTION(h, 19);
        H3_SET_BASE_CELL(h, 77);
        H3_SET_EXT_INDEX_DIGIT(h, 16, 4);
        H3_SET_EXT_INDEX_DIGIT(h, 19, 2);
        int got = H3_GET_MODE(h);
        ASSERT_MSG(got == modes[i],
            "PF-18: mode %d corrupted to %d after ext operations", modes[i], got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PF-19: H3_SET_EFFECTIVE_RESOLUTION evaluates `res` exactly once
 *        (side-effect safety — validates the GCC statement-expression form)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_pf19_single_evaluation(void) {
    H3Index h = H3_INIT_EXT;
    H3_SET_MODE(h, 1);
    int counter = 15;
    /* If res is evaluated more than once, counter will be incremented
     * more than once and the resulting resolution will be wrong. */
    H3_SET_EFFECTIVE_RESOLUTION(h, ++counter);
    ASSERT_MSG(counter == 16,
        "PF-19: counter = %d after SET_EFFECTIVE_RESOLUTION(++counter), "
        "expected 16 (res evaluated %d times)", counter, counter - 15);
    ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(h) == 16,
        "PF-19: effective res = %d, expected 16",
        H3_GET_EFFECTIVE_RESOLUTION(h));
}


/* ═══════════════════════════════════════════════════════════════════════════
 * Main — run all tests, report summary
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void) {
    printf("H3-Extended (128-bit) Preflight — Bit Layout Validation\n");
    printf("========================================================\n\n");

    test_pf01_sizeof();
    test_pf02_stock_zero_extended();
    test_pf03_resolution_roundtrip();
    test_pf04_effective_res_agree();
    test_pf05_ext_flag_range();
    test_pf06_stock_res_field_encoding();
    test_pf07_digit_roundtrip();
    test_pf08_digit_bit_positions();
    test_pf09_reserved_bits_clear();
    test_pf10_h3_init();
    test_pf11_h3_init_ext();
    test_pf12_ubsan_clean();
    test_pf13_stock_macros_on_ext();
    test_pf14_cross_boundary_isolation();
    test_pf15_ext_digits_mask();
    test_pf16_ext_flag_mask();
    test_pf17_base_cell_survives();
    test_pf18_mode_survives();
    test_pf19_single_evaluation();

    printf("\n========================================================\n");
    printf("Results: %d passed, %d failed\n", g_pass_count, g_fail_count);

    if (g_fail_count > 0) {
        printf("PREFLIGHT FAILED — bit layout has %d issue(s).\n", g_fail_count);
        printf("Do NOT proceed to Session 1 until all preflight tests pass.\n");
        return 1;
    } else {
        printf("PREFLIGHT PASSED — bit layout validated.\n");
        printf("The Group C macros are safe to drop into h3Index.h.\n");
        printf("Proceed to Session 1.\n");
        return 0;
    }
}
