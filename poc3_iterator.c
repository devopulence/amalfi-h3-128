/*
 * poc3_iterator.c — Child-iterator state machine for H3-Extended (128-bit)
 *
 * Version: 1.0.0 — 2026-05-02
 * Companion to: CLAUDE.md v1.0.22, poc3.md, poc1_bit_layout.c
 *
 * PURPOSE:
 *   Validate the FIVE widened iterator functions for child-cell enumeration:
 *     1. _zeroIndexDigits   — zero a [start,end] range of digits
 *     2. _incrementResDigit — increment the digit at a given res
 *     3. _getResDigit       — read the digit at a given res
 *     4. _iterInitParent    — set up iterator for first child
 *     5. iterStepChild      — advance iterator with carry + pentagon skip
 *
 *   Zero dependencies on the H3 source tree. Group A/B/C macros are copied
 *   verbatim from poc1_bit_layout.c (proven correct by POC-1, 685/685).
 *
 *   The two PRIMARY UBSAN traps from §6.6 / §6.10 are:
 *     - Negative-shift in stock _incrementResDigit when res > 15
 *     - Negative-shift in stock _zeroIndexDigits when range exceeds 15
 *   Our widened versions dispatch through H3_GET/SET_DIGIT_AT_RES, which
 *   selects stock or ext encoding by res. The full-depth test (res 15 → 22,
 *   823,543 children) under UBSAN proves no UB shift remains.
 *
 * COMPILE:
 *   gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
 *       -o poc3_iterator poc3_iterator.c -lm && ./poc3_iterator
 *
 * EXIT CODES:
 *   0  — all tests passed
 *   1  — one or more tests failed (failures printed to stderr)
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
#define H3_NULL                ((H3Index)0)

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
 * THE FIVE WIDENED ITERATOR FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

/* I1: _zeroIndexDigits — zero a contiguous [startRes, endRes] digit range.
 *
 * Stock implementation uses bitmask shifts; on 128-bit values that span the
 * stock/ext boundary, those shifts can hit UB (shift count > word width or
 * negative offset). We dispatch per-digit through H3_SET_DIGIT_AT_RES.
 */
static H3Index _zeroIndexDigits(H3Index h, int startRes, int endRes) {
    if (startRes > endRes) return h;
    if (startRes < 1) startRes = 1;
    if (endRes > MAX_H3_EXT_RES) endRes = MAX_H3_EXT_RES;
    for (int r = startRes; r <= endRes; r++) {
        H3_SET_DIGIT_AT_RES(h, r, 0);
    }
    return h;
}

/* I2: _incrementResDigit — increment the digit at the given res.
 *
 * Stock implementation does `h += 1ULL << ((MAX_H3_RES - res) * 3)`; that
 * computation is UB when res > 15 (negative shift) or res < 0. Our version
 * routes through H3_GET/SET_DIGIT_AT_RES.
 *
 * Note: incrementing a digit holding 6 → 7 (sentinel). The caller (carry
 * loop in iterStepChild) treats 7 as the "done" / "reset and carry" signal.
 */
static void _incrementResDigit(H3Index *h, int res) {
    int d = H3_GET_DIGIT_AT_RES(*h, res);
    H3_SET_DIGIT_AT_RES(*h, res, d + 1);
}

/* I3: _getResDigit — read the digit at the given res. */
static inline int _getResDigit(H3Index h, int res) {
    return H3_GET_DIGIT_AT_RES(h, res);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Iterator struct + state machine
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    H3Index h;          /* current child cell, H3_NULL once exhausted */
    int _parentRes;     /* effective resolution of parent (0–22) */
    int _skipDigit;     /* digit to skip (-1 = hexagon, 1 = pentagon K_AXES) */
} ChildIterator;

/* I4: _iterInitParent — set iterator to the FIRST child.
 *
 * The first child has the parent's path and digits parent_res+1..child_res
 * all zero (center-child trail). The pentagon skip is applied by the caller
 * via the `isPentagon` param.
 */
static void _iterInitParent(ChildIterator *it, H3Index parent, int childRes, int isPentagon) {
    int parentRes = H3_GET_EFFECTIVE_RESOLUTION(parent);
    it->_parentRes = parentRes;
    it->_skipDigit = isPentagon ? 1 : -1;

    if (childRes < parentRes) {
        it->h = H3_NULL;
        return;
    }

    H3Index h = parent;
    H3_SET_EFFECTIVE_RESOLUTION(h, childRes);
    if (parentRes + 1 <= childRes) {
        h = _zeroIndexDigits(h, parentRes + 1, childRes);
    }
    /* Populate canonical sentinels at digits childRes+1..max:
     *   - ext child: max = 22 (must explicitly set bits 65–85 because a stock
     *     parent has zero high half).
     *   - stock child: max = 15 (parent already has these as 7).
     * The trailing sentinels are required for the result to pass isValidCell;
     * the iterator step logic does not touch them. */
    int max_pos = (childRes >= 16) ? MAX_H3_EXT_RES : MAX_H3_RES;
    for (int r = childRes + 1; r <= max_pos; r++) {
        H3_SET_DIGIT_AT_RES(h, r, INVALID_DIGIT);
    }
    it->h = h;
}

/* I5: iterStepChild — advance to next child, or set h=H3_NULL at the end.
 *
 * Algorithm:
 *   1. Increment digit at childRes (deepest digit).
 *   2. If digit == 7 (sentinel = overflow): reset to 0, carry to res-1.
 *      Repeat at res-1, etc.
 *   3. The pentagon skip applies ONLY at digit position parent_res+1: if
 *      after incrementing, that digit equals _skipDigit, increment again.
 *   4. When the carry would propagate past parent_res+1, set h=H3_NULL.
 */
static void iterStepChild(ChildIterator *it) {
    if (it->h == H3_NULL) return;
    int childRes = H3_GET_EFFECTIVE_RESOLUTION(it->h);
    if (childRes <= it->_parentRes) {
        /* parent_res == child_res: only one "child" (the parent itself). */
        it->h = H3_NULL;
        return;
    }

    int r = childRes;
    _incrementResDigit(&it->h, r);

    while (1) {
        int d = _getResDigit(it->h, r);

        /* Pentagon skip — only at the parent_res+1 boundary digit. */
        if (r == it->_parentRes + 1 && d == it->_skipDigit) {
            _incrementResDigit(&it->h, r);
            d = _getResDigit(it->h, r);
        }

        if (d != INVALID_DIGIT) {
            return;  /* iterator stable, points at next child */
        }

        /* d == 7: must carry. */
        if (r == it->_parentRes + 1) {
            it->h = H3_NULL;  /* overflow at top level — done */
            return;
        }
        H3_SET_DIGIT_AT_RES(it->h, r, 0);
        r--;
        _incrementResDigit(&it->h, r);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test harness
 * ═══════════════════════════════════════════════════════════════════════════ */

static int g_fail_count = 0;
static int g_pass_count = 0;
static long g_children_total = 0;

#define ASSERT_MSG(cond, fmt, ...) do {                                     \
    if (!(cond)) {                                                          \
        fprintf(stderr, "FAIL [%s:%d]: " fmt "\n",                          \
                __FILE__, __LINE__, ##__VA_ARGS__);                         \
        g_fail_count++;                                                     \
    } else {                                                                \
        g_pass_count++;                                                     \
    }                                                                       \
} while (0)

static H3Index make_valid_stock_cell(int res, int base_cell) {
    H3Index h = H3_INIT;
    H3_SET_MODE(h, 1);
    H3_SET_RESOLUTION(h, res);
    H3_SET_BASE_CELL(h, base_cell);
    for (int r = 1; r <= res; r++) H3_SET_INDEX_DIGIT(h, r, 0);
    return h;
}

static H3Index make_valid_ext_cell(int res, int base_cell) {
    H3Index h = H3_INIT_EXT;
    H3_SET_MODE(h, 1);
    H3_SET_EFFECTIVE_RESOLUTION(h, res);
    H3_SET_BASE_CELL(h, base_cell);
    for (int r = 1; r <= res; r++) H3_SET_DIGIT_AT_RES(h, r, 0);
    return h;
}

static H3Index make_parent(int res, int base_cell) {
    return (res <= MAX_H3_RES) ? make_valid_stock_cell(res, base_cell)
                               : make_valid_ext_cell(res, base_cell);
}

static int cmp_h3(const void *a, const void *b) {
    H3Index x = *(const H3Index *)a;
    H3Index y = *(const H3Index *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-01: _zeroIndexDigits on stock-only range, all 7 sentinels → all 0
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it01_zero_stock_range(void) {
    H3Index h = H3_INIT;  /* digits 1-15 = 7 */
    h = _zeroIndexDigits(h, 1, 5);
    for (int r = 1; r <= 5; r++) {
        ASSERT_MSG(_getResDigit(h, r) == 0, "IT-01: digit %d not zeroed", r);
    }
    for (int r = 6; r <= 15; r++) {
        ASSERT_MSG(_getResDigit(h, r) == 7, "IT-01: digit %d corrupted to %d",
            r, _getResDigit(h, r));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-02: _zeroIndexDigits on full stock range
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it02_zero_full_stock(void) {
    H3Index h = H3_INIT;
    h = _zeroIndexDigits(h, 1, 15);
    for (int r = 1; r <= 15; r++) {
        ASSERT_MSG(_getResDigit(h, r) == 0, "IT-02: digit %d not zeroed", r);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-03: _zeroIndexDigits on ext-only range
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it03_zero_ext_range(void) {
    H3Index h = H3_INIT_EXT;
    h = _zeroIndexDigits(h, 16, 22);
    for (int r = 1; r <= 15; r++) {
        ASSERT_MSG(_getResDigit(h, r) == 7, "IT-03: stock digit %d corrupted", r);
    }
    for (int r = 16; r <= 22; r++) {
        ASSERT_MSG(_getResDigit(h, r) == 0, "IT-03: ext digit %d not zeroed", r);
    }
    /* ext flag must remain set (the operation only touches ext digits, not the flag) */
    ASSERT_MSG(H3_GET_EXT_FLAG(h) == 1, "IT-03: ext flag corrupted");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-04: _zeroIndexDigits crossing the stock/ext boundary
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it04_zero_cross_boundary(void) {
    H3Index h = H3_INIT_EXT;
    h = _zeroIndexDigits(h, 10, 20);
    for (int r = 1; r <= 9; r++) {
        ASSERT_MSG(_getResDigit(h, r) == 7, "IT-04: stock digit %d corrupted", r);
    }
    for (int r = 10; r <= 20; r++) {
        ASSERT_MSG(_getResDigit(h, r) == 0, "IT-04: digit %d in range not zeroed", r);
    }
    for (int r = 21; r <= 22; r++) {
        ASSERT_MSG(_getResDigit(h, r) == 7, "IT-04: ext digit %d corrupted", r);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-05: _zeroIndexDigits with empty range (start > end) is a no-op
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it05_zero_empty(void) {
    H3Index h0 = H3_INIT_EXT;
    H3Index h1 = _zeroIndexDigits(h0, 10, 5);
    ASSERT_MSG(h0 == h1, "IT-05: empty range modified the cell");

    H3Index h2 = _zeroIndexDigits(h0, 22, 16);
    ASSERT_MSG(h0 == h2, "IT-05: ext-empty range modified the cell");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-06: _zeroIndexDigits single-digit at every position 1..22
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it06_zero_single(void) {
    for (int p = 1; p <= 22; p++) {
        H3Index h = H3_INIT_EXT;
        h = _zeroIndexDigits(h, p, p);
        ASSERT_MSG(_getResDigit(h, p) == 0,
            "IT-06: zero at position %d failed (digit = %d)", p, _getResDigit(h, p));
        for (int q = 1; q <= 22; q++) {
            if (q == p) continue;
            ASSERT_MSG(_getResDigit(h, q) == 7,
                "IT-06: zeroing %d corrupted digit %d to %d", p, q, _getResDigit(h, q));
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-07: _zeroIndexDigits — clamp on out-of-range inputs (defensive)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it07_zero_clamp(void) {
    H3Index h0 = H3_INIT_EXT;

    /* startRes < 1 → clamped to 1 */
    H3Index h1 = _zeroIndexDigits(h0, 0, 3);
    for (int r = 1; r <= 3; r++) {
        ASSERT_MSG(_getResDigit(h1, r) == 0, "IT-07: digit %d not zeroed", r);
    }

    /* endRes > 22 → clamped to 22 */
    H3Index h2 = _zeroIndexDigits(h0, 18, 30);
    for (int r = 18; r <= 22; r++) {
        ASSERT_MSG(_getResDigit(h2, r) == 0, "IT-07: digit %d not zeroed", r);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-08: _zeroIndexDigits sweep — many (start, end) pairs
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it08_zero_sweep(void) {
    for (int s = 1; s <= 22; s++) {
        for (int e = s; e <= 22; e++) {
            H3Index h = H3_INIT_EXT;
            h = _zeroIndexDigits(h, s, e);
            int ok = 1;
            for (int r = 1; r <= 22; r++) {
                int d = _getResDigit(h, r);
                int expected = (r >= s && r <= e) ? 0 : 7;
                if (d != expected) { ok = 0; break; }
            }
            ASSERT_MSG(ok, "IT-08: sweep [%d,%d] failed", s, e);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-09: _incrementResDigit at every stock position, value 0..6
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it09_increment_stock(void) {
    for (int p = 1; p <= 15; p++) {
        for (int v = 0; v <= 6; v++) {
            H3Index h = make_valid_stock_cell(15, 0);
            H3_SET_INDEX_DIGIT(h, p, v);
            _incrementResDigit(&h, p);
            int got = _getResDigit(h, p);
            ASSERT_MSG(got == v + 1,
                "IT-09: stock pos %d, %d+1 → %d", p, v, got);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-10: _incrementResDigit at every ext position, value 0..6
 *        (Stock impl would produce a negative shift for res > 15 — UB.)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it10_increment_ext(void) {
    for (int p = 16; p <= 22; p++) {
        for (int v = 0; v <= 6; v++) {
            H3Index h = make_valid_ext_cell(22, 0);
            H3_SET_DIGIT_AT_RES(h, p, v);
            _incrementResDigit(&h, p);
            int got = _getResDigit(h, p);
            ASSERT_MSG(got == v + 1,
                "IT-10: ext pos %d, %d+1 → %d", p, v, got);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-11: _incrementResDigit at stock pos doesn't corrupt other digits
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it11_increment_isolation_stock(void) {
    for (int p = 1; p <= 15; p++) {
        H3Index h = make_valid_ext_cell(22, 30);
        /* Seed all digits with distinct values */
        for (int r = 1; r <= 22; r++) H3_SET_DIGIT_AT_RES(h, r, r % 6);

        H3Index before = h;
        _incrementResDigit(&h, p);

        for (int r = 1; r <= 22; r++) {
            int expected = r % 6 + (r == p ? 1 : 0);
            int got = _getResDigit(h, r);
            ASSERT_MSG(got == expected,
                "IT-11: increment pos %d, digit %d expected %d got %d",
                p, r, expected, got);
        }
        /* Mode and base cell preserved */
        ASSERT_MSG(H3_GET_MODE(h) == H3_GET_MODE(before),
            "IT-11: increment pos %d corrupted mode", p);
        ASSERT_MSG(H3_GET_BASE_CELL(h) == 30,
            "IT-11: increment pos %d corrupted base cell", p);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-12: _incrementResDigit at ext pos doesn't corrupt other digits
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it12_increment_isolation_ext(void) {
    for (int p = 16; p <= 22; p++) {
        H3Index h = make_valid_ext_cell(22, 77);
        for (int r = 1; r <= 22; r++) H3_SET_DIGIT_AT_RES(h, r, r % 6);

        _incrementResDigit(&h, p);

        for (int r = 1; r <= 22; r++) {
            int expected = r % 6 + (r == p ? 1 : 0);
            int got = _getResDigit(h, r);
            ASSERT_MSG(got == expected,
                "IT-12: increment ext pos %d, digit %d expected %d got %d",
                p, r, expected, got);
        }
        ASSERT_MSG(H3_GET_BASE_CELL(h) == 77,
            "IT-12: increment ext pos %d corrupted base cell", p);
        ASSERT_MSG(H3_GET_EXT_FLAG(h) == 1,
            "IT-12: increment ext pos %d corrupted ext flag", p);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-13: _incrementResDigit produces sentinel 7 at boundary 6 → 7
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it13_increment_to_sentinel(void) {
    for (int p = 1; p <= 22; p++) {
        H3Index h = make_valid_ext_cell(22, 0);
        H3_SET_DIGIT_AT_RES(h, p, 6);
        _incrementResDigit(&h, p);
        ASSERT_MSG(_getResDigit(h, p) == 7,
            "IT-13: increment 6→7 at pos %d failed", p);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-14: _getResDigit reads correctly across boundary
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it14_get_res_digit(void) {
    H3Index h = make_valid_ext_cell(22, 0);
    /* Place known pattern: digit r = r mod 7 */
    for (int r = 1; r <= 22; r++) H3_SET_DIGIT_AT_RES(h, r, r % 7);
    for (int r = 1; r <= 22; r++) {
        int got = _getResDigit(h, r);
        ASSERT_MSG(got == r % 7,
            "IT-14: digit %d expected %d got %d", r, r % 7, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-15: _iterInitParent — first child has center path (digits parent+1..child = 0)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it15_iter_init_first_child(void) {
    int parents[] = {0, 5, 10, 14, 15, 16, 18, 21};
    int childs[]  = {1, 8, 14, 16, 17, 19, 22, 22};
    for (size_t i = 0; i < sizeof(parents)/sizeof(parents[0]); i++) {
        int p = parents[i], c = childs[i];
        if (c < p) continue;
        H3Index parent = make_parent(p, 33);
        ChildIterator it;
        _iterInitParent(&it, parent, c, 0);

        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(it.h) == c,
            "IT-15: parent %d→%d first child eff res = %d",
            p, c, H3_GET_EFFECTIVE_RESOLUTION(it.h));
        ASSERT_MSG(H3_GET_BASE_CELL(it.h) == 33,
            "IT-15: parent %d→%d first child base cell wrong", p, c);
        ASSERT_MSG(it._parentRes == p,
            "IT-15: parent %d→%d _parentRes = %d", p, c, it._parentRes);
        for (int r = p + 1; r <= c; r++) {
            ASSERT_MSG(_getResDigit(it.h, r) == 0,
                "IT-15: parent %d→%d digit %d = %d (expected 0)",
                p, c, r, _getResDigit(it.h, r));
        }
        /* Sentinels past child_res — only up to the encoding boundary:
         *   ext child  → check through digit 22
         *   stock child → check through digit 15 (digits 16-22 are out of band) */
        int max_check = (c >= 16) ? MAX_H3_EXT_RES : MAX_H3_RES;
        for (int r = c + 1; r <= max_check; r++) {
            ASSERT_MSG(_getResDigit(it.h, r) == 7,
                "IT-15: parent %d→%d trailing digit %d = %d (expected 7)",
                p, c, r, _getResDigit(it.h, r));
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-16: _iterInitParent — child_res < parent_res yields H3_NULL
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it16_iter_init_invalid(void) {
    H3Index parent = make_parent(20, 5);
    ChildIterator it;
    _iterInitParent(&it, parent, 18, 0);
    ASSERT_MSG(it.h == H3_NULL,
        "IT-16: child_res < parent_res did not yield H3_NULL");

    /* And: parent at res 22, child at res 22 — single child (the parent) */
    H3Index p22 = make_parent(22, 5);
    _iterInitParent(&it, p22, 22, 0);
    ASSERT_MSG(it.h != H3_NULL,
        "IT-16: parent_res == child_res must produce a single child");
    ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(it.h) == 22,
        "IT-16: same-res first child eff res wrong");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-17: hexagon parent res 15 → child res 16 = exactly 7 children (BOUNDARY)
 *        Each child must have ext flag set, eff res 16, base cell preserved,
 *        unique digit 16 value, reserved bits clear.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it17_boundary_15_to_16(void) {
    H3Index parent = make_valid_stock_cell(15, 5);
    /* Make all stock digits distinct so we can verify they're preserved */
    for (int r = 1; r <= 15; r++) H3_SET_INDEX_DIGIT(parent, r, r % 6);

    ChildIterator it;
    _iterInitParent(&it, parent, 16, 0);

    int seen_digits[8] = {0};
    int count = 0;
    while (it.h != H3_NULL) {
        ASSERT_MSG(H3_GET_EXT_FLAG(it.h) == 1,
            "IT-17: child %d does not have ext flag set", count);
        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(it.h) == 16,
            "IT-17: child %d eff res != 16", count);
        ASSERT_MSG(H3_GET_BASE_CELL(it.h) == 5,
            "IT-17: child %d base cell wrong", count);
        ASSERT_MSG((it.h >> 86) == 0,
            "IT-17: child %d has dirty reserved bits", count);
        /* All stock digits 1..15 preserved */
        int stock_ok = 1;
        for (int r = 1; r <= 15; r++) {
            if (_getResDigit(it.h, r) != r % 6) { stock_ok = 0; break; }
        }
        ASSERT_MSG(stock_ok, "IT-17: child %d has corrupted stock digits", count);
        int d16 = _getResDigit(it.h, 16);
        ASSERT_MSG(d16 >= 0 && d16 <= 6, "IT-17: child %d digit 16 = %d out of range",
            count, d16);
        ASSERT_MSG(seen_digits[d16] == 0, "IT-17: duplicate digit 16 = %d", d16);
        seen_digits[d16] = 1;
        count++;
        iterStepChild(&it);
    }
    ASSERT_MSG(count == 7, "IT-17: hexagon parent res 15 yielded %d children, expected 7", count);
    g_children_total += count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-18: hexagon parent res 14 → child res 16 = 7^2 = 49 children
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it18_two_levels_to_ext(void) {
    H3Index parent = make_valid_stock_cell(14, 50);
    ChildIterator it;
    _iterInitParent(&it, parent, 16, 0);

    H3Index buf[64];
    int count = 0;
    while (it.h != H3_NULL && count < 64) {
        ASSERT_MSG(H3_GET_EXT_FLAG(it.h) == 1, "IT-18: child %d no ext flag", count);
        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(it.h) == 16, "IT-18: child %d wrong res", count);
        ASSERT_MSG(H3_GET_BASE_CELL(it.h) == 50, "IT-18: child %d wrong bc", count);
        buf[count++] = it.h;
        iterStepChild(&it);
    }
    ASSERT_MSG(count == 49, "IT-18: expected 49 children, got %d", count);

    /* Dedup */
    qsort(buf, count, sizeof(H3Index), cmp_h3);
    int dup = 0;
    for (int i = 1; i < count; i++) if (buf[i] == buf[i-1]) dup++;
    ASSERT_MSG(dup == 0, "IT-18: %d duplicate children", dup);
    g_children_total += count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-19: hexagon parent res 15 → child res 18 = 7^3 = 343 children
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it19_full_depth_343(void) {
    H3Index parent = make_valid_stock_cell(15, 99);
    ChildIterator it;
    _iterInitParent(&it, parent, 18, 0);

    static H3Index buf[400];
    int count = 0;
    while (it.h != H3_NULL && count < 400) {
        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(it.h) == 18,
            "IT-19: child %d wrong res", count);
        ASSERT_MSG(H3_GET_EXT_FLAG(it.h) == 1, "IT-19: child %d no ext flag", count);
        ASSERT_MSG((it.h >> 86) == 0, "IT-19: child %d dirty reserved", count);
        buf[count++] = it.h;
        iterStepChild(&it);
    }
    ASSERT_MSG(count == 343, "IT-19: expected 343 children, got %d", count);

    qsort(buf, count, sizeof(H3Index), cmp_h3);
    int dup = 0;
    for (int i = 1; i < count; i++) if (buf[i] == buf[i-1]) dup++;
    ASSERT_MSG(dup == 0, "IT-19: %d duplicate children", dup);
    g_children_total += count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-20: stock-only enumeration res 5 → res 8 = 7^3 = 343 children
 *        (verifies the iterator works entirely below the boundary too)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it20_stock_only(void) {
    H3Index parent = make_valid_stock_cell(5, 17);
    ChildIterator it;
    _iterInitParent(&it, parent, 8, 0);

    static H3Index buf[400];
    int count = 0;
    while (it.h != H3_NULL && count < 400) {
        ASSERT_MSG(H3_GET_EXT_FLAG(it.h) == 0, "IT-20: stock-only child has ext flag");
        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(it.h) == 8, "IT-20: child wrong res");
        buf[count++] = it.h;
        iterStepChild(&it);
    }
    ASSERT_MSG(count == 343, "IT-20: expected 343, got %d", count);

    qsort(buf, count, sizeof(H3Index), cmp_h3);
    int dup = 0;
    for (int i = 1; i < count; i++) if (buf[i] == buf[i-1]) dup++;
    ASSERT_MSG(dup == 0, "IT-20: %d duplicates", dup);
    g_children_total += count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-21: pentagon parent res 0 → child res 1 = 6 children (skip K_AXES)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it21_pentagon_one_level(void) {
    H3Index parent = make_valid_stock_cell(0, 4);  /* base cell 4 in stock H3 */
    ChildIterator it;
    _iterInitParent(&it, parent, 1, 1);

    int count = 0;
    int seen_k = 0;
    while (it.h != H3_NULL) {
        int d1 = _getResDigit(it.h, 1);
        ASSERT_MSG(d1 != 1, "IT-21: pentagon child has K_AXES (digit 1)");
        if (d1 == 1) seen_k = 1;
        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(it.h) == 1, "IT-21: child wrong res");
        ASSERT_MSG(H3_GET_BASE_CELL(it.h) == 4, "IT-21: child wrong base cell");
        count++;
        iterStepChild(&it);
    }
    ASSERT_MSG(seen_k == 0, "IT-21: K-axis was not skipped");
    ASSERT_MSG(count == 6, "IT-21: pentagon expected 6 children, got %d", count);
    g_children_total += count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-22: pentagon parent res 15 → child res 16 = 6 children
 *        (PENTAGON SKIP AT THE STOCK/EXT BOUNDARY — critical case)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it22_pentagon_at_boundary(void) {
    H3Index parent = make_valid_stock_cell(15, 4);
    ChildIterator it;
    _iterInitParent(&it, parent, 16, 1);

    int count = 0;
    int digits_seen[8] = {0};
    while (it.h != H3_NULL) {
        int d16 = _getResDigit(it.h, 16);
        ASSERT_MSG(d16 != 1, "IT-22: K_AXES at digit 16 not skipped");
        ASSERT_MSG(d16 >= 0 && d16 <= 6, "IT-22: digit 16 = %d out of range", d16);
        ASSERT_MSG(digits_seen[d16] == 0, "IT-22: duplicate digit 16 = %d", d16);
        digits_seen[d16] = 1;
        ASSERT_MSG(H3_GET_EXT_FLAG(it.h) == 1, "IT-22: pentagon ext child no ext flag");
        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(it.h) == 16, "IT-22: wrong res");
        count++;
        iterStepChild(&it);
    }
    ASSERT_MSG(count == 6, "IT-22: pentagon→ext expected 6, got %d", count);
    /* Specifically: digit values seen = {0, 2, 3, 4, 5, 6}, NOT 1 */
    ASSERT_MSG(digits_seen[0] && !digits_seen[1] && digits_seen[2] && digits_seen[3]
            && digits_seen[4] && digits_seen[5] && digits_seen[6],
        "IT-22: wrong digit set seen");
    g_children_total += count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-23: pentagon parent res 14 → child res 17 = 6×7×7 = 294 children
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it23_pentagon_multilevel(void) {
    H3Index parent = make_valid_stock_cell(14, 4);
    ChildIterator it;
    _iterInitParent(&it, parent, 17, 1);

    static H3Index buf[400];
    int count = 0;
    while (it.h != H3_NULL && count < 400) {
        int d15 = _getResDigit(it.h, 15);
        ASSERT_MSG(d15 != 1, "IT-23: K_AXES at digit 15 (parent_res+1) not skipped");
        ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(it.h) == 17, "IT-23: wrong res");
        ASSERT_MSG(H3_GET_EXT_FLAG(it.h) == 1, "IT-23: ext flag missing");
        buf[count++] = it.h;
        iterStepChild(&it);
    }
    ASSERT_MSG(count == 294, "IT-23: pentagon res 14→17 expected 294, got %d", count);

    qsort(buf, count, sizeof(H3Index), cmp_h3);
    int dup = 0;
    for (int i = 1; i < count; i++) if (buf[i] == buf[i-1]) dup++;
    ASSERT_MSG(dup == 0, "IT-23: %d duplicates", dup);
    g_children_total += count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-24: dedup test — combined enumeration from a different parent
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it24_dedup_mixed(void) {
    /* Enumerate res 12 → 16 = 7^4 = 2401 children */
    H3Index parent = make_valid_stock_cell(12, 60);
    ChildIterator it;
    _iterInitParent(&it, parent, 16, 0);

    static H3Index buf[2500];
    int count = 0;
    while (it.h != H3_NULL && count < 2500) {
        buf[count++] = it.h;
        iterStepChild(&it);
    }
    ASSERT_MSG(count == 2401, "IT-24: 12→16 expected 2401, got %d", count);

    qsort(buf, count, sizeof(H3Index), cmp_h3);
    int dup = 0;
    for (int i = 1; i < count; i++) if (buf[i] == buf[i-1]) dup++;
    ASSERT_MSG(dup == 0, "IT-24: %d duplicates", dup);
    g_children_total += count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-25: enumeration ordering — first child is all-zero digit path,
 *        last child is all-6 path
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it25_ordering(void) {
    H3Index parent = make_valid_stock_cell(15, 5);
    ChildIterator it;
    _iterInitParent(&it, parent, 18, 0);

    /* First child: digits 16,17,18 all zero */
    ASSERT_MSG(_getResDigit(it.h, 16) == 0, "IT-25: first child d16 != 0");
    ASSERT_MSG(_getResDigit(it.h, 17) == 0, "IT-25: first child d17 != 0");
    ASSERT_MSG(_getResDigit(it.h, 18) == 0, "IT-25: first child d18 != 0");

    H3Index last = it.h;
    while (it.h != H3_NULL) {
        last = it.h;
        iterStepChild(&it);
    }
    /* Last child: digits 16, 17, 18 all 6 */
    ASSERT_MSG(_getResDigit(last, 16) == 6, "IT-25: last child d16 != 6");
    ASSERT_MSG(_getResDigit(last, 17) == 6, "IT-25: last child d17 != 6");
    ASSERT_MSG(_getResDigit(last, 18) == 6, "IT-25: last child d18 != 6");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-26: FULL-DEPTH UBSAN trap — res 15 → res 22 = 7^7 = 823,543 children.
 *        Under UBSAN, any negative shift in _incrementResDigit or
 *        _zeroIndexDigits aborts. Completing proves the UB is eliminated.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it26_full_depth(void) {
    H3Index parent = make_valid_stock_cell(15, 1);
    /* Set stock digits to a non-trivial pattern so they must be preserved */
    for (int r = 1; r <= 15; r++) H3_SET_INDEX_DIGIT(parent, r, (r * 3) % 6);

    ChildIterator it;
    _iterInitParent(&it, parent, 22, 0);

    long count = 0;
    H3Index last = it.h;
    while (it.h != H3_NULL) {
        last = it.h;
        count++;
        iterStepChild(&it);
        /* Sanity check periodic invariants without per-child assertions */
        if ((count & 0x3FFFF) == 0) {
            ASSERT_MSG(H3_GET_EFFECTIVE_RESOLUTION(last) == 22,
                "IT-26: child %ld eff res != 22", count);
            ASSERT_MSG(H3_GET_BASE_CELL(last) == 1,
                "IT-26: child %ld base cell wrong", count);
            ASSERT_MSG((last >> 86) == 0,
                "IT-26: child %ld reserved bits dirty", count);
        }
    }
    ASSERT_MSG(count == 823543L, "IT-26: full-depth expected 823543, got %ld", count);
    /* Last child has all-6 ext digits */
    for (int r = 16; r <= 22; r++) {
        ASSERT_MSG(_getResDigit(last, r) == 6,
            "IT-26: last child digit %d != 6 (got %d)", r, _getResDigit(last, r));
    }
    /* And stock digits preserved */
    for (int r = 1; r <= 15; r++) {
        ASSERT_MSG(_getResDigit(last, r) == (r * 3) % 6,
            "IT-26: last child stock digit %d corrupted", r);
    }
    g_children_total += count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-27: parent_res == child_res produces a single child (the parent itself)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it27_same_res(void) {
    for (int res = 0; res <= 22; res++) {
        H3Index parent = make_parent(res, 11);
        ChildIterator it;
        _iterInitParent(&it, parent, res, 0);
        int count = 0;
        H3Index first_child = it.h;
        while (it.h != H3_NULL) {
            count++;
            iterStepChild(&it);
        }
        ASSERT_MSG(count == 1, "IT-27: same-res %d expected 1 child, got %d", res, count);
        ASSERT_MSG(first_child == parent, "IT-27: same-res %d child != parent", res);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IT-28: cross-cutting invariants — every child of a hex parent at every
 *        boundary-crossing pair has correct base cell, mode, ext flag, and
 *        clean reserved bits.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_it28_invariants(void) {
    int parent_resos[] = {0, 5, 13, 15};
    int child_resos[]  = {1, 6, 16, 17};
    int bcs[] = {0, 50, 121};
    for (size_t i = 0; i < sizeof(parent_resos)/sizeof(parent_resos[0]); i++) {
        for (size_t j = 0; j < sizeof(bcs)/sizeof(bcs[0]); j++) {
            int p = parent_resos[i], c = child_resos[i], bc = bcs[j];
            H3Index parent = make_parent(p, bc);
            ChildIterator it;
            _iterInitParent(&it, parent, c, 0);

            int count = 0;
            int violations = 0;
            while (it.h != H3_NULL) {
                if (H3_GET_BASE_CELL(it.h) != bc) violations++;
                if (H3_GET_MODE(it.h) != 1) violations++;
                if ((it.h >> 86) != 0) violations++;
                int expected_flag = (c >= 16) ? 1 : 0;
                if (H3_GET_EXT_FLAG(it.h) != expected_flag) violations++;
                if (H3_GET_EFFECTIVE_RESOLUTION(it.h) != c) violations++;
                count++;
                iterStepChild(&it);
            }
            ASSERT_MSG(violations == 0,
                "IT-28: parent res %d→%d bc %d had %d invariant violations",
                p, c, bc, violations);
            ASSERT_MSG(count > 0,
                "IT-28: parent res %d→%d bc %d produced no children", p, c, bc);
            g_children_total += count;
        }
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 * Main — run all tests, report summary
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void) {
    printf("H3-Extended (128-bit) Preflight — Iterator State Machine (POC-3)\n");
    printf("================================================================\n\n");

    test_it01_zero_stock_range();
    test_it02_zero_full_stock();
    test_it03_zero_ext_range();
    test_it04_zero_cross_boundary();
    test_it05_zero_empty();
    test_it06_zero_single();
    test_it07_zero_clamp();
    test_it08_zero_sweep();

    test_it09_increment_stock();
    test_it10_increment_ext();
    test_it11_increment_isolation_stock();
    test_it12_increment_isolation_ext();
    test_it13_increment_to_sentinel();

    test_it14_get_res_digit();
    test_it15_iter_init_first_child();
    test_it16_iter_init_invalid();

    test_it17_boundary_15_to_16();
    test_it18_two_levels_to_ext();
    test_it19_full_depth_343();
    test_it20_stock_only();

    test_it21_pentagon_one_level();
    test_it22_pentagon_at_boundary();
    test_it23_pentagon_multilevel();

    test_it24_dedup_mixed();
    test_it25_ordering();
    test_it26_full_depth();
    test_it27_same_res();
    test_it28_invariants();

    printf("\n================================================================\n");
    printf("Results: %d passed, %d failed (children enumerated: %ld)\n",
        g_pass_count, g_fail_count, g_children_total);

    if (g_fail_count > 0) {
        printf("POC-3 FAILED — iterator state machine has %d issue(s).\n", g_fail_count);
        return 1;
    } else {
        printf("POC-3 PASSED — iterator state machine validated.\n");
        return 0;
    }
}
