/*
 * poc2_validation.c — Validation predicates for H3-Extended (128-bit)
 *
 * Version: 1.0.0 — 2026-05-02
 * Companion to: CLAUDE.md v1.0.22, poc2.md, poc1_bit_layout.c
 *
 * PURPOSE:
 *   Validate the FIVE widened internal predicates used by isValidCell on
 *   128-bit H3 cells:
 *     1. _hasGoodTopBits        — high bit / reserved (56–58) / reserved (86–127)
 *     2. _firstOneIndex         — MSB position, must handle high half (no clz UB)
 *     3. _hasAny7UptoRes        — sentinel-7 detection within digits 1..res
 *     4. _hasAll7AfterRes       — sentinel-7 enforcement past res, through digit 22
 *     5. _hasDeletedSubsequence — pentagon K-axis (digit 1) first-traversal detection
 *
 *   Zero dependencies on the H3 source tree. All Group A/B/C macros are copied
 *   verbatim from poc1_bit_layout.c (proven correct by POC-1, 685/685).
 *
 * COMPILE:
 *   gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
 *       -o poc2_validation poc2_validation.c -lm && ./poc2_validation
 *
 * EXIT CODES:
 *   0  — all tests passed
 *   1  — one or more tests failed (failures printed to stderr)
 *
 * COVERAGE (VP-01 through VP-23):
 *   VP-01  _hasGoodTopBits on valid stock cells          (all res, several base cells)
 *   VP-02  _hasGoodTopBits on valid ext cells            (all ext res)
 *   VP-03  _hasGoodTopBits — high bit (bit 63) corruption
 *   VP-04  _hasGoodTopBits — reserved 56–58 corruption  (each bit individually)
 *   VP-05  _hasGoodTopBits — reserved 86–127 corruption (each bit, 42 positions)
 *   VP-06  _hasGoodTopBits — combined corruption
 *   VP-07  _firstOneIndex  — stock cells across all res
 *   VP-08  _firstOneIndex  — ext cells (would UB if implemented with bare clzll)
 *   VP-09  _firstOneIndex  — single-bit sweep across all 128 positions
 *   VP-10  _firstOneIndex  — H3_INIT and H3_INIT_EXT sentinels
 *   VP-11  _hasAny7UptoRes — clean stock cells (returns 0)
 *   VP-12  _hasAny7UptoRes — 7 placed at every position 1..res
 *   VP-13  _hasAny7UptoRes — clean ext cells (returns 0)
 *   VP-14  _hasAny7UptoRes — 7 placed in ext range (positions 16..22)
 *   VP-15  _hasAll7AfterRes — clean stock cells (returns 1)
 *   VP-16  _hasAll7AfterRes — clean ext cells (returns 1, must check through 22)
 *   VP-17  _hasAll7AfterRes — non-7 at every position past res (each → 0)
 *   VP-18  _hasAll7AfterRes — boundary res 15 vs res 16
 *   VP-19  _hasDeletedSubsequence — center-only (all zeros) returns 0
 *   VP-20  _hasDeletedSubsequence — first non-zero is non-K (returns 0)
 *   VP-21  _hasDeletedSubsequence — first non-zero is K (returns 1)
 *   VP-22  _hasDeletedSubsequence — K-axis discovered through ext digits
 *   VP-23  Combined / adversarial — wrong mode, dirty reserved, ext-flag mismatch
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef __SIZEOF_INT128__
#error "This preflight requires __uint128_t (GCC or Clang). MSVC is not supported."
#endif

typedef __uint128_t H3Index;

/* ═══════════════════════════════════════════════════════════════════════════
 * GROUP A — Stock bit-position constants (verbatim from poc1_bit_layout.c)
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

#define H3_INIT                ((H3Index)UINT64_C(35184372088831))

/* ═══════════════════════════════════════════════════════════════════════════
 * GROUP B — Stock getter/setter macros (verbatim from poc1_bit_layout.c)
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

#define H3_SET_RESERVED_BITS(h, v) (h) = (((h) & ~(H3_RESERVED_MASK << H3_RESERVED_OFFSET)) | \
                                          (((H3Index)(v)) << H3_RESERVED_OFFSET))

/* ═══════════════════════════════════════════════════════════════════════════
 * GROUP C — Group C macros (verbatim from poc1_bit_layout.c)
 * ═══════════════════════════════════════════════════════════════════════════ */

#define MAX_H3_EXT_RES         22
#define H3_EXT_FLAG_OFFSET     64
#define H3_EXT_FLAG_MASK       ((__uint128_t)1 << 64)
#define H3_EXT_DIGITS_OFFSET   65
#define H3_EXT_DIGITS_MASK     ((((__uint128_t)1 << 21) - 1) << 65)

#define H3_INIT_EXT            (H3_INIT | H3_EXT_FLAG_MASK | H3_EXT_DIGITS_MASK)

#define H3_GET_EXT_FLAG(h)     ((int)(((h) & H3_EXT_FLAG_MASK) >> H3_EXT_FLAG_OFFSET))
#define H3_SET_EXT_FLAG(h, v)  (h) = (((h) & ~H3_EXT_FLAG_MASK) | \
                                      (((__uint128_t)(v)) << H3_EXT_FLAG_OFFSET))

#define H3_GET_EFFECTIVE_RESOLUTION(h) \
    (H3_GET_RESOLUTION(h) + (H3_GET_EXT_FLAG(h) << 4))

#define H3_SET_EFFECTIVE_RESOLUTION(h, res) ({  \
    int _r = (res);                              \
    H3_SET_RESOLUTION((h), (_r) & 0xF);          \
    H3_SET_EXT_FLAG((h), (_r) >= 16 ? 1 : 0);    \
})

#define H3_GET_EXT_INDEX_DIGIT(h, res) \
    ((int)(((h) >> (H3_EXT_DIGITS_OFFSET + ((res) - 16) * 3)) & H3_DIGIT_MASK))

#define H3_SET_EXT_INDEX_DIGIT(h, res, digit) \
    (h) = (((h) & ~((__uint128_t)H3_DIGIT_MASK << (H3_EXT_DIGITS_OFFSET + ((res) - 16) * 3))) | \
           (((__uint128_t)(digit)) << (H3_EXT_DIGITS_OFFSET + ((res) - 16) * 3)))

#define H3_GET_DIGIT_AT_RES(h, res) \
    ((res) <= MAX_H3_RES ? H3_GET_INDEX_DIGIT((h), (res)) : H3_GET_EXT_INDEX_DIGIT((h), (res)))

#define H3_SET_DIGIT_AT_RES(h, res, digit) do { \
    if ((res) <= MAX_H3_RES)                      \
        H3_SET_INDEX_DIGIT((h), (res), (digit));  \
    else                                          \
        H3_SET_EXT_INDEX_DIGIT((h), (res), (digit)); \
} while (0)

/* ═══════════════════════════════════════════════════════════════════════════
 * THE FIVE PREDICATES (widened for 128-bit)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* P1: _hasGoodTopBits
 *
 * Stock semantics: the top bits area (high bit + reserved 56–58) must be all
 * zero. For 128-bit, we ALSO require bits 86–127 to be zero (the new reserved
 * area above the ext-digit field).
 *
 * Why this is a structural break vs. stock: the stock implementation may use
 * a `(h >> N)` shift-and-compare that, on a 128-bit value with ext flag /
 * digits / dirty high reserved bits set, produces a different residual. We
 * replace it with explicit field checks.
 */
static inline int _hasGoodTopBits(H3Index h) {
    if (H3_GET_HIGH_BIT(h) != 0) return 0;
    if (H3_GET_RESERVED_BITS(h) != 0) return 0;
    /* Bits 86–127 must be zero. Equivalent to ((h >> 86) == 0). */
    if ((h >> 86) != (H3Index)0) return 0;
    return 1;
}

/* P2: _firstOneIndex
 *
 * Returns the bit index (0..127) of the most-significant set bit, or -1 if h
 * is zero. Stock's `__builtin_clzll` only operates on a 64-bit word; calling
 * it on a truncated low half is UB when the actual MSB lives in the high
 * half. We split the 128-bit value into two 64-bit halves and only call
 * clzll on a non-zero word.
 */
static inline int _firstOneIndex(H3Index h) {
    uint64_t hi = (uint64_t)(h >> 64);
    uint64_t lo = (uint64_t)h;
    if (hi != 0) {
        return 127 - __builtin_clzll(hi);
    }
    if (lo != 0) {
        return 63 - __builtin_clzll(lo);
    }
    return -1;
}

/* P3: _hasAny7UptoRes
 *
 * Returns 1 iff any digit at positions 1..res equals INVALID_DIGIT (sentinel 7).
 * For res > 15, must dispatch through H3_GET_DIGIT_AT_RES so it reads ext digits.
 */
static inline int _hasAny7UptoRes(H3Index h, int res) {
    for (int r = 1; r <= res; r++) {
        if (H3_GET_DIGIT_AT_RES(h, r) == INVALID_DIGIT) return 1;
    }
    return 0;
}

/* P4: _hasAll7AfterRes
 *
 * Returns 1 iff every digit position after res (up through the cell's max
 * digit position) holds sentinel 7. Stock checked through MAX_H3_RES (=15);
 * for ext cells we MUST check through MAX_H3_EXT_RES (=22) — otherwise
 * non-sentinel garbage in the ext-digit field would not be caught.
 */
static inline int _hasAll7AfterRes(H3Index h, int res) {
    int max_pos = H3_GET_EXT_FLAG(h) ? MAX_H3_EXT_RES : MAX_H3_RES;
    for (int r = res + 1; r <= max_pos; r++) {
        if (H3_GET_DIGIT_AT_RES(h, r) != INVALID_DIGIT) return 0;
    }
    return 1;
}

/* P5: _hasDeletedSubsequence
 *
 * Pentagon "deleted subsequence" check: pentagons skip the K-axis (digit 1).
 * If the FIRST non-zero digit at positions 1..res is digit 1 (K_AXES_DIGIT),
 * the cell encodes a deleted subsequence and is invalid for a pentagon parent.
 *
 * Returns 1 if the path through digits 1..res traverses the K-axis as its
 * first non-center step; 0 otherwise (all-center, or first non-center is not K).
 *
 * For res > 15, dispatch reads ext digits — required to detect deleted
 * subsequences that first appear past the stock boundary.
 */
static inline int _hasDeletedSubsequence(H3Index h, int res) {
    for (int r = 1; r <= res; r++) {
        int d = H3_GET_DIGIT_AT_RES(h, r);
        if (d == 0) continue;
        return (d == 1) ? 1 : 0;
    }
    return 0;
}

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

/* ───── Cell builders ───── */

/* A clean stock cell at the given res with mode=cell (1).
 * Stock digits 1..res = 0; stock digits res+1..15 = 7 (sentinel).
 * High half is zero. Reserved bits zero.
 */
static H3Index make_valid_stock_cell(int res, int base_cell) {
    H3Index h = H3_INIT;       /* digits 1-15 = 7 */
    H3_SET_MODE(h, 1);
    H3_SET_RESOLUTION(h, res); /* low res, ext flag stays 0 */
    H3_SET_BASE_CELL(h, base_cell);
    for (int r = 1; r <= res; r++) {
        H3_SET_INDEX_DIGIT(h, r, 0);
    }
    return h;
}

/* A clean ext cell at the given effective res with mode=cell (1).
 * All digits 1..res = 0; digits res+1..22 = 7 (sentinel).
 * Bit 64 set, bits 65-85 set as appropriate, bits 86-127 zero.
 */
static H3Index make_valid_ext_cell(int res, int base_cell) {
    H3Index h = H3_INIT_EXT;
    H3_SET_MODE(h, 1);
    H3_SET_EFFECTIVE_RESOLUTION(h, res);
    H3_SET_BASE_CELL(h, base_cell);
    for (int r = 1; r <= res; r++) {
        H3_SET_DIGIT_AT_RES(h, r, 0);
    }
    return h;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * VP-01: _hasGoodTopBits on valid stock cells (all res × several base cells)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp01_good_top_bits_stock(void) {
    int base_cells[] = {0, 1, 14, 50, 91, 121};
    int nbc = (int)(sizeof(base_cells)/sizeof(base_cells[0]));
    for (int res = 0; res <= 15; res++) {
        for (int i = 0; i < nbc; i++) {
            H3Index h = make_valid_stock_cell(res, base_cells[i]);
            ASSERT_MSG(_hasGoodTopBits(h) == 1,
                "VP-01: clean stock cell res %d, bc %d failed _hasGoodTopBits",
                res, base_cells[i]);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-02: _hasGoodTopBits on valid ext cells (ext res × several base cells)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp02_good_top_bits_ext(void) {
    int base_cells[] = {0, 1, 14, 50, 91, 121};
    int nbc = (int)(sizeof(base_cells)/sizeof(base_cells[0]));
    for (int res = 16; res <= 22; res++) {
        for (int i = 0; i < nbc; i++) {
            H3Index h = make_valid_ext_cell(res, base_cells[i]);
            ASSERT_MSG(_hasGoodTopBits(h) == 1,
                "VP-02: clean ext cell res %d, bc %d failed _hasGoodTopBits",
                res, base_cells[i]);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-03: _hasGoodTopBits — high bit (bit 63) corruption
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp03_good_top_bits_high_bit(void) {
    /* On both stock and ext cells, setting the high bit must cause failure. */
    for (int res = 0; res <= 15; res++) {
        H3Index h = make_valid_stock_cell(res, 7);
        ASSERT_MSG(_hasGoodTopBits(h) == 1, "VP-03: pre-corruption stock res %d", res);
        H3_SET_HIGH_BIT(h, 1);
        ASSERT_MSG(_hasGoodTopBits(h) == 0,
            "VP-03: stock res %d with high bit set passed _hasGoodTopBits", res);
    }
    for (int res = 16; res <= 22; res++) {
        H3Index h = make_valid_ext_cell(res, 7);
        ASSERT_MSG(_hasGoodTopBits(h) == 1, "VP-03: pre-corruption ext res %d", res);
        H3_SET_HIGH_BIT(h, 1);
        ASSERT_MSG(_hasGoodTopBits(h) == 0,
            "VP-03: ext res %d with high bit set passed _hasGoodTopBits", res);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-04: _hasGoodTopBits — reserved bits 56–58 corruption (each individually)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp04_good_top_bits_reserved_lo(void) {
    /* Each of the 7 non-zero values for the 3-bit reserved field must fail. */
    for (int v = 1; v <= 7; v++) {
        H3Index hs = make_valid_stock_cell(7, 30);
        H3_SET_RESERVED_BITS(hs, v);
        ASSERT_MSG(_hasGoodTopBits(hs) == 0,
            "VP-04: stock cell with reserved=%d passed _hasGoodTopBits", v);

        H3Index he = make_valid_ext_cell(20, 30);
        H3_SET_RESERVED_BITS(he, v);
        ASSERT_MSG(_hasGoodTopBits(he) == 0,
            "VP-04: ext cell with reserved=%d passed _hasGoodTopBits", v);
    }
    /* And reserved=0 still passes for sanity */
    H3Index ok = make_valid_ext_cell(18, 30);
    H3_SET_RESERVED_BITS(ok, 0);
    ASSERT_MSG(_hasGoodTopBits(ok) == 1,
        "VP-04: cleanup cell with reserved=0 failed _hasGoodTopBits");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-05: _hasGoodTopBits — reserved bits 86–127 corruption (each bit)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp05_good_top_bits_reserved_hi(void) {
    for (int bit = 86; bit <= 127; bit++) {
        H3Index h = make_valid_ext_cell(19, 42);
        ASSERT_MSG(_hasGoodTopBits(h) == 1, "VP-05: precondition res 19 bit %d", bit);
        h |= ((__uint128_t)1) << bit;
        ASSERT_MSG(_hasGoodTopBits(h) == 0,
            "VP-05: ext cell with bit %d set passed _hasGoodTopBits", bit);
    }
    /* Same for stock cells (reserved-high should still be zero on stock too) */
    for (int bit = 86; bit <= 127; bit++) {
        H3Index h = make_valid_stock_cell(10, 42);
        ASSERT_MSG(_hasGoodTopBits(h) == 1, "VP-05: precondition stock res 10 bit %d", bit);
        h |= ((__uint128_t)1) << bit;
        ASSERT_MSG(_hasGoodTopBits(h) == 0,
            "VP-05: stock cell with bit %d set passed _hasGoodTopBits", bit);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-06: _hasGoodTopBits — combined corruption (multiple defects at once)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp06_good_top_bits_combined(void) {
    H3Index h;

    /* High bit + reserved-lo */
    h = make_valid_ext_cell(17, 5);
    H3_SET_HIGH_BIT(h, 1);
    H3_SET_RESERVED_BITS(h, 4);
    ASSERT_MSG(_hasGoodTopBits(h) == 0, "VP-06: high+reserved-lo passed");

    /* High bit + reserved-hi */
    h = make_valid_stock_cell(8, 5);
    H3_SET_HIGH_BIT(h, 1);
    h |= ((__uint128_t)1) << 100;
    ASSERT_MSG(_hasGoodTopBits(h) == 0, "VP-06: high+reserved-hi passed");

    /* Reserved-lo + reserved-hi */
    h = make_valid_ext_cell(22, 5);
    H3_SET_RESERVED_BITS(h, 7);
    h |= ((__uint128_t)1) << 127;
    ASSERT_MSG(_hasGoodTopBits(h) == 0, "VP-06: reserved-lo+reserved-hi passed");

    /* All three corruptions */
    h = make_valid_stock_cell(0, 5);
    H3_SET_HIGH_BIT(h, 1);
    H3_SET_RESERVED_BITS(h, 5);
    h |= ((__uint128_t)1) << 95;
    ASSERT_MSG(_hasGoodTopBits(h) == 0, "VP-06: triple corruption passed");

    /* Sanity: clean cells still pass */
    ASSERT_MSG(_hasGoodTopBits(make_valid_stock_cell(15, 100)) == 1, "VP-06 sanity stock");
    ASSERT_MSG(_hasGoodTopBits(make_valid_ext_cell(16, 100)) == 1, "VP-06 sanity ext lo");
    ASSERT_MSG(_hasGoodTopBits(make_valid_ext_cell(22, 100)) == 1, "VP-06 sanity ext hi");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-07: _firstOneIndex — stock cells across all res
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp07_first_one_index_stock(void) {
    /* For a clean stock cell, mode=1 means bit 59 is set. So MSB is bit 59
     * (since bit 63 is high bit = 0, mode bits 59–62 hold value 1 → bit 59 set).
     */
    for (int res = 0; res <= 15; res++) {
        H3Index h = make_valid_stock_cell(res, 0);
        int idx = _firstOneIndex(h);
        ASSERT_MSG(idx == 59,
            "VP-07: stock res %d _firstOneIndex = %d, expected 59", res, idx);
    }
    /* Setting a higher bit (e.g. by writing reserved) should change the answer */
    H3Index h = make_valid_stock_cell(5, 0);
    H3_SET_RESERVED_BITS(h, 4);  /* sets bit 58 — still below 59, stays 59 */
    ASSERT_MSG(_firstOneIndex(h) == 59, "VP-07: reserved=4 expected MSB=59");
    H3_SET_HIGH_BIT(h, 1);  /* sets bit 63 — new MSB */
    ASSERT_MSG(_firstOneIndex(h) == 63, "VP-07: high bit set expected MSB=63");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-08: _firstOneIndex — ext cells (would UB without high-half handling)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp08_first_one_index_ext(void) {
    /* For a clean ext cell at res 16: digit 16 = 7 (bits 65-67 set), digits
     * 17-22 = 7 (bits 68-85 set). MSB sits at the high end of the ext-digit
     * field — bit 85 if all ext digits 16..22 = 7.
     */
    for (int res = 16; res <= 22; res++) {
        H3Index h = make_valid_ext_cell(res, 0);
        int idx = _firstOneIndex(h);
        /* Highest sentinel ext digit is at position 22 → bits 65+18..65+20 = 83..85.
         * If res < 22, that highest sentinel exists. If res == 22, no sentinels
         * remain in ext range, but stock sentinel residue may exist. We expect
         * the MSB to land in the ext-digit area for res < 22, or in the
         * mode/high area when ext digits are all zero.
         */
        if (res < 22) {
            ASSERT_MSG(idx == 85,
                "VP-08: ext res %d _firstOneIndex = %d, expected 85 (sentinel digit 22 hi bit)",
                res, idx);
        } else {
            /* res == 22: digits 1-22 all zero, only ext flag (64) and mode (59) set.
             * MSB is bit 64.
             */
            ASSERT_MSG(idx == 64,
                "VP-08: ext res 22 _firstOneIndex = %d, expected 64 (ext flag)", idx);
        }
    }
    /* Set bit 127 — MSB must move to 127 */
    H3Index h = make_valid_ext_cell(20, 5);
    h |= ((__uint128_t)1) << 127;
    ASSERT_MSG(_firstOneIndex(h) == 127, "VP-08: bit 127 set expected MSB=127");

    /* Set bit 100 (in reserved-hi) on a stock cell — would UB if implemented
     * with bare clzll on low half */
    H3Index hs = make_valid_stock_cell(0, 0);
    hs |= ((__uint128_t)1) << 100;
    ASSERT_MSG(_firstOneIndex(hs) == 100, "VP-08: bit 100 on stock cell expected MSB=100");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-09: _firstOneIndex — single-bit sweep across all 128 positions
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp09_first_one_index_sweep(void) {
    for (int bit = 0; bit < 128; bit++) {
        H3Index h = ((__uint128_t)1) << bit;
        int idx = _firstOneIndex(h);
        ASSERT_MSG(idx == bit,
            "VP-09: single bit %d → _firstOneIndex = %d", bit, idx);
    }
    /* Zero input must return -1 */
    ASSERT_MSG(_firstOneIndex((H3Index)0) == -1,
        "VP-09: _firstOneIndex(0) must be -1");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-10: _firstOneIndex — H3_INIT and H3_INIT_EXT sentinels
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp10_first_one_index_init(void) {
    /* H3_INIT = digits 1-15 = 7 → bits 0..44 all set → MSB = 44 */
    int idx = _firstOneIndex(H3_INIT);
    ASSERT_MSG(idx == 44,
        "VP-10: _firstOneIndex(H3_INIT) = %d, expected 44", idx);

    /* H3_INIT_EXT = H3_INIT | bit64 | bits65..85 set → MSB = 85 */
    idx = _firstOneIndex(H3_INIT_EXT);
    ASSERT_MSG(idx == 85,
        "VP-10: _firstOneIndex(H3_INIT_EXT) = %d, expected 85", idx);

    /* And the ext-flag-only case: bit 64 alone */
    H3Index just_flag = H3_EXT_FLAG_MASK;
    ASSERT_MSG(_firstOneIndex(just_flag) == 64,
        "VP-10: _firstOneIndex(H3_EXT_FLAG_MASK) expected 64");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-11: _hasAny7UptoRes — clean stock cells (all digits 1..res are 0 → 0)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp11_any7_clean_stock(void) {
    for (int res = 0; res <= 15; res++) {
        H3Index h = make_valid_stock_cell(res, 11);
        int got = _hasAny7UptoRes(h, res);
        ASSERT_MSG(got == 0,
            "VP-11: clean stock res %d → _hasAny7UptoRes = %d", res, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-12: _hasAny7UptoRes — 7 placed at every position 1..res
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp12_any7_seeded_stock(void) {
    for (int res = 1; res <= 15; res++) {
        for (int p = 1; p <= res; p++) {
            H3Index h = make_valid_stock_cell(res, 5);
            H3_SET_INDEX_DIGIT(h, p, INVALID_DIGIT);
            int got = _hasAny7UptoRes(h, res);
            ASSERT_MSG(got == 1,
                "VP-12: stock res %d, 7 at pos %d → _hasAny7UptoRes = %d",
                res, p, got);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-13: _hasAny7UptoRes — clean ext cells (returns 0 for both stock+ext positions)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp13_any7_clean_ext(void) {
    for (int res = 16; res <= 22; res++) {
        H3Index h = make_valid_ext_cell(res, 11);
        int got = _hasAny7UptoRes(h, res);
        ASSERT_MSG(got == 0,
            "VP-13: clean ext res %d → _hasAny7UptoRes = %d", res, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-14: _hasAny7UptoRes — 7 placed at every ext digit position
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp14_any7_seeded_ext(void) {
    for (int res = 16; res <= 22; res++) {
        for (int p = 1; p <= res; p++) {
            H3Index h = make_valid_ext_cell(res, 7);
            H3_SET_DIGIT_AT_RES(h, p, INVALID_DIGIT);
            int got = _hasAny7UptoRes(h, res);
            ASSERT_MSG(got == 1,
                "VP-14: ext res %d, 7 at pos %d → _hasAny7UptoRes = %d",
                res, p, got);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-15: _hasAll7AfterRes — clean stock cells (sentinels past res → 1)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp15_all7_clean_stock(void) {
    for (int res = 0; res <= 15; res++) {
        H3Index h = make_valid_stock_cell(res, 0);
        int got = _hasAll7AfterRes(h, res);
        ASSERT_MSG(got == 1,
            "VP-15: clean stock res %d → _hasAll7AfterRes = %d", res, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-16: _hasAll7AfterRes — clean ext cells (must check through 22)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp16_all7_clean_ext(void) {
    for (int res = 16; res <= 22; res++) {
        H3Index h = make_valid_ext_cell(res, 0);
        int got = _hasAll7AfterRes(h, res);
        ASSERT_MSG(got == 1,
            "VP-16: clean ext res %d → _hasAll7AfterRes = %d", res, got);
    }
    /* Boundary edge: res=22 means no positions after — must still return 1. */
    H3Index h22 = make_valid_ext_cell(22, 5);
    ASSERT_MSG(_hasAll7AfterRes(h22, 22) == 1,
        "VP-16: ext res=22 (empty range) must return 1");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-17: _hasAll7AfterRes — non-7 at every position past res (each → 0)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp17_all7_violation(void) {
    /* Stock cell at low res, place a non-7 anywhere past res */
    for (int res = 0; res <= 14; res++) {
        for (int p = res + 1; p <= 15; p++) {
            for (int badval = 0; badval <= 6; badval++) {
                H3Index h = make_valid_stock_cell(res, 5);
                H3_SET_INDEX_DIGIT(h, p, badval);
                int got = _hasAll7AfterRes(h, res);
                ASSERT_MSG(got == 0,
                    "VP-17: stock res %d, digit %d=%d (non-7) → got %d",
                    res, p, badval, got);
            }
        }
    }
    /* Ext cell, place non-7 at any position past res in ext range */
    for (int res = 16; res <= 21; res++) {
        for (int p = res + 1; p <= 22; p++) {
            for (int badval = 0; badval <= 6; badval++) {
                H3Index h = make_valid_ext_cell(res, 5);
                H3_SET_DIGIT_AT_RES(h, p, badval);
                int got = _hasAll7AfterRes(h, res);
                ASSERT_MSG(got == 0,
                    "VP-17: ext res %d, digit %d=%d (non-7) → got %d",
                    res, p, badval, got);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-18: _hasAll7AfterRes — boundary res 15 vs res 16
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp18_all7_boundary(void) {
    /* Stock cell res=15: no positions past res → must return 1 */
    H3Index h15s = make_valid_stock_cell(15, 0);
    ASSERT_MSG(_hasAll7AfterRes(h15s, 15) == 1,
        "VP-18: stock res 15 with empty after-range must return 1");

    /* Ext cell res=15 — represented as ext flag clear, all 16-22 don't exist.
     * But our builder for res<=15 uses make_valid_stock_cell. To test ext-flag
     * behaviour, build an ext cell whose effective res happens to equal 16
     * (boundary): digit 16 = 0, digits 17-22 = 7.
     */
    H3Index h16e = make_valid_ext_cell(16, 0);
    ASSERT_MSG(_hasAll7AfterRes(h16e, 16) == 1,
        "VP-18: ext res 16 with sentinels at 17-22 must return 1");

    /* Now corrupt digit 22 of an ext res=16 cell: must fail. This is the bug
     * stock would miss because stock loops only to MAX_H3_RES=15. */
    H3Index hbad = make_valid_ext_cell(16, 0);
    H3_SET_EXT_INDEX_DIGIT(hbad, 22, 3);
    ASSERT_MSG(_hasAll7AfterRes(hbad, 16) == 0,
        "VP-18: ext res 16 with non-7 at digit 22 must return 0 (stock bug surface)");

    /* Same construction at digit 17 (immediately after res) */
    H3Index hbad2 = make_valid_ext_cell(16, 0);
    H3_SET_EXT_INDEX_DIGIT(hbad2, 17, 1);
    ASSERT_MSG(_hasAll7AfterRes(hbad2, 16) == 0,
        "VP-18: ext res 16 with non-7 at digit 17 must return 0");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-19: _hasDeletedSubsequence — center-only path returns 0
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp19_deleted_center(void) {
    for (int res = 1; res <= 22; res++) {
        H3Index h = (res <= 15) ? make_valid_stock_cell(res, 14)
                                : make_valid_ext_cell(res, 14);
        /* All digits 1..res = 0 (center). */
        int got = _hasDeletedSubsequence(h, res);
        ASSERT_MSG(got == 0,
            "VP-19: all-center res %d → _hasDeletedSubsequence = %d", res, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-20: _hasDeletedSubsequence — first non-zero is non-K (returns 0)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp20_deleted_non_k(void) {
    for (int res = 1; res <= 22; res++) {
        for (int first = 1; first <= res; first++) {
            for (int d = 2; d <= 6; d++) {
                H3Index h = (res <= 15) ? make_valid_stock_cell(res, 14)
                                        : make_valid_ext_cell(res, 14);
                /* Place first non-zero at position `first` with non-K digit d. */
                H3_SET_DIGIT_AT_RES(h, first, d);
                int got = _hasDeletedSubsequence(h, res);
                ASSERT_MSG(got == 0,
                    "VP-20: res %d first=%d d=%d → got %d", res, first, d, got);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-21: _hasDeletedSubsequence — first non-zero is K-axis (returns 1)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp21_deleted_k_axis(void) {
    for (int res = 1; res <= 22; res++) {
        for (int first = 1; first <= res; first++) {
            H3Index h = (res <= 15) ? make_valid_stock_cell(res, 14)
                                    : make_valid_ext_cell(res, 14);
            H3_SET_DIGIT_AT_RES(h, first, 1);  /* K_AXES_DIGIT */
            int got = _hasDeletedSubsequence(h, res);
            ASSERT_MSG(got == 1,
                "VP-21: res %d first-K at %d → _hasDeletedSubsequence = %d",
                res, first, got);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-22: _hasDeletedSubsequence — K-axis discovered through ext digits
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp22_deleted_through_ext(void) {
    /* The K-axis traversal first appears at an ext digit position (16..22).
     * All stock digits 1..15 = 0; ext digit p = 1; everything past p = 7. */
    for (int p = 16; p <= 22; p++) {
        H3Index h = make_valid_ext_cell(p, 14);
        H3_SET_EXT_INDEX_DIGIT(h, p, 1);
        int got = _hasDeletedSubsequence(h, p);
        ASSERT_MSG(got == 1,
            "VP-22: K at ext digit %d → _hasDeletedSubsequence = %d", p, got);
    }
    /* And: place K at ext digit but a non-K stock digit earlier — must return 0. */
    for (int p = 16; p <= 22; p++) {
        H3Index h = make_valid_ext_cell(p, 14);
        H3_SET_INDEX_DIGIT(h, 5, 3);              /* first non-zero is non-K */
        H3_SET_EXT_INDEX_DIGIT(h, p, 1);          /* K at ext, but later */
        int got = _hasDeletedSubsequence(h, p);
        ASSERT_MSG(got == 0,
            "VP-22: non-K at stock 5, K at ext %d → got %d (expected 0)",
            p, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * VP-23: Combined / adversarial — wrong mode, dirty reserved, ext-flag mismatch
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_vp23_adversarial(void) {
    /* Wrong mode does NOT affect _hasGoodTopBits / _hasAny7 / _hasAll7 / etc.
     * — those operate on the digit and reserved fields. We assert that the
     * predicates remain stable when mode changes. */
    for (int mode = 0; mode <= 15; mode++) {
        H3Index h = make_valid_ext_cell(20, 50);
        H3_SET_MODE(h, mode);
        ASSERT_MSG(_hasGoodTopBits(h) == 1,
            "VP-23: mode=%d should not affect _hasGoodTopBits", mode);
        ASSERT_MSG(_hasAny7UptoRes(h, 20) == 0,
            "VP-23: mode=%d clean digits → _hasAny7UptoRes 0", mode);
        ASSERT_MSG(_hasAll7AfterRes(h, 20) == 1,
            "VP-23: mode=%d clean digits → _hasAll7AfterRes 1", mode);
    }

    /* Adversarial: every bit in [86,127] in isolation breaks _hasGoodTopBits */
    for (int bit = 86; bit <= 127; bit++) {
        H3Index h = (H3Index)0;
        h |= ((__uint128_t)1) << bit;
        ASSERT_MSG(_hasGoodTopBits(h) == 0,
            "VP-23: lone bit %d must break _hasGoodTopBits", bit);
    }

    /* Adversarial: ext flag set with garbage in low res field — _hasGoodTopBits
     * still passes (low-res field is allowed to be anything 0–6 per ext encoding) */
    H3Index garbage = H3_EXT_FLAG_MASK | ((H3Index)1 << 59);  /* mode=1, ext flag set */
    ASSERT_MSG(_hasGoodTopBits(garbage) == 1,
        "VP-23: ext-flag-only with mode=1 should pass _hasGoodTopBits");

    /* Ext flag set + dirty bit 86 — fails */
    H3Index dirty = garbage | ((H3Index)1 << 86);
    ASSERT_MSG(_hasGoodTopBits(dirty) == 0,
        "VP-23: ext flag + dirty bit 86 must fail _hasGoodTopBits");

    /* _hasDeletedSubsequence with a sentinel-7 digit: 7 != 1 and 7 != 0, so
     * the predicate sees a non-K non-center digit and returns 0. (That's the
     * literal contract; isValidCell catches sentinel violations separately.) */
    H3Index hsent = make_valid_ext_cell(20, 5);
    H3_SET_DIGIT_AT_RES(hsent, 3, INVALID_DIGIT);
    ASSERT_MSG(_hasDeletedSubsequence(hsent, 20) == 0,
        "VP-23: sentinel-7 first non-zero is non-K (treated as non-K) → 0");

    /* Combined: _hasAny7UptoRes catches the sentinel */
    ASSERT_MSG(_hasAny7UptoRes(hsent, 20) == 1,
        "VP-23: sentinel-7 in res range caught by _hasAny7UptoRes");

    /* Sweep all (res, first-non-zero-position) tuples with digit=2 to confirm
     * non-K never triggers deleted-subsequence regardless of position. */
    int hits = 0;
    for (int res = 5; res <= 22; res++) {
        for (int p = 1; p <= res; p++) {
            H3Index h = (res <= 15) ? make_valid_stock_cell(res, 5)
                                    : make_valid_ext_cell(res, 5);
            H3_SET_DIGIT_AT_RES(h, p, 2);
            if (_hasDeletedSubsequence(h, res) != 0) hits++;
        }
    }
    ASSERT_MSG(hits == 0, "VP-23: digit=2 must never trigger deleted-sub, hits=%d", hits);
}


/* ═══════════════════════════════════════════════════════════════════════════
 * Main — run all tests, report summary
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void) {
    printf("H3-Extended (128-bit) Preflight — Validation Predicates (POC-2)\n");
    printf("================================================================\n\n");

    test_vp01_good_top_bits_stock();
    test_vp02_good_top_bits_ext();
    test_vp03_good_top_bits_high_bit();
    test_vp04_good_top_bits_reserved_lo();
    test_vp05_good_top_bits_reserved_hi();
    test_vp06_good_top_bits_combined();

    test_vp07_first_one_index_stock();
    test_vp08_first_one_index_ext();
    test_vp09_first_one_index_sweep();
    test_vp10_first_one_index_init();

    test_vp11_any7_clean_stock();
    test_vp12_any7_seeded_stock();
    test_vp13_any7_clean_ext();
    test_vp14_any7_seeded_ext();

    test_vp15_all7_clean_stock();
    test_vp16_all7_clean_ext();
    test_vp17_all7_violation();
    test_vp18_all7_boundary();

    test_vp19_deleted_center();
    test_vp20_deleted_non_k();
    test_vp21_deleted_k_axis();
    test_vp22_deleted_through_ext();
    test_vp23_adversarial();

    printf("\n================================================================\n");
    printf("Results: %d passed, %d failed\n", g_pass_count, g_fail_count);

    if (g_fail_count > 0) {
        printf("POC-2 FAILED — validation predicates have %d issue(s).\n", g_fail_count);
        return 1;
    } else {
        printf("POC-2 PASSED — validation predicates validated.\n");
        return 0;
    }
}
