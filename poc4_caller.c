/*
 * poc4_caller.c — Test caller for the POC-4 FFI ABI test (TU 2)
 *
 * Version: 1.0.0 — 2026-05-02
 *
 * This translation unit fabricates 128-bit cells, invokes every shim
 * function, and verifies that pointer-passed arguments survive the cross-TU
 * boundary intact. Group A/B/C macros are copied verbatim from
 * poc1_bit_layout.c so we can build cells without touching the H3 source.
 */
#include "poc4_shim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * GROUP A — verbatim from poc1_bit_layout.c (no typedef redeclaration)
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
 * GROUP B
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
 * GROUP C
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

static H3Index make_parent(int res, int bc) {
    return (res <= MAX_H3_RES) ? make_valid_stock_cell(res, bc)
                               : make_valid_ext_cell(res, bc);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-01: Caller-side sizeof(H3Index) == 16
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi01_caller_sizeof(void) {
    ASSERT_MSG(sizeof(H3Index) == 16,
        "ABI-01: caller sizeof(H3Index) = %zu, expected 16", sizeof(H3Index));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-02: Caller-side _Alignof(H3Index)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi02_caller_alignof(void) {
    /* GCC / Clang give __uint128_t a 16-byte alignment on amd64 / arm64. */
    ASSERT_MSG(_Alignof(H3Index) == 16,
        "ABI-02: caller _Alignof(H3Index) = %zu, expected 16",
        _Alignof(H3Index));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-03: Shim-side sizeof matches caller-side
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi03_cross_tu_sizeof(void) {
    size_t shim_sz = h3_ext_sizeof_h3index();
    ASSERT_MSG(shim_sz == sizeof(H3Index),
        "ABI-03: shim sizeof = %zu vs caller sizeof = %zu",
        shim_sz, sizeof(H3Index));
    ASSERT_MSG(shim_sz == 16,
        "ABI-03: shim sizeof = %zu, expected 16", shim_sz);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-04: Shim-side alignof matches caller-side
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi04_cross_tu_alignof(void) {
    size_t shim_a = h3_ext_alignof_h3index();
    ASSERT_MSG(shim_a == _Alignof(H3Index),
        "ABI-04: shim alignof = %zu vs caller alignof = %zu",
        shim_a, _Alignof(H3Index));
    ASSERT_MSG(shim_a == 16, "ABI-04: shim alignof = %zu, expected 16", shim_a);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-05: is_valid_cell returns 1 for clean stock cells (every res)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi05_valid_stock(void) {
    int bcs[] = {0, 5, 50, 121};
    for (int res = 0; res <= 15; res++) {
        for (size_t i = 0; i < sizeof(bcs)/sizeof(bcs[0]); i++) {
            H3Index cell = make_valid_stock_cell(res, bcs[i]);
            int valid = -1;
            H3Error err = h3_ext_is_valid_cell(&cell, &valid);
            ASSERT_MSG(err == H3_SUCCESS, "ABI-05: err=%d res=%d", err, res);
            ASSERT_MSG(valid == 1,
                "ABI-05: valid stock res=%d bc=%d returned valid=%d",
                res, bcs[i], valid);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-06: is_valid_cell returns 1 for clean ext cells (every res)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi06_valid_ext(void) {
    int bcs[] = {0, 5, 50, 121};
    for (int res = 16; res <= 22; res++) {
        for (size_t i = 0; i < sizeof(bcs)/sizeof(bcs[0]); i++) {
            H3Index cell = make_valid_ext_cell(res, bcs[i]);
            int valid = -1;
            H3Error err = h3_ext_is_valid_cell(&cell, &valid);
            ASSERT_MSG(err == H3_SUCCESS, "ABI-06: err=%d res=%d", err, res);
            ASSERT_MSG(valid == 1,
                "ABI-06: valid ext res=%d bc=%d returned valid=%d",
                res, bcs[i], valid);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-07: is_valid_cell returns 0 for cell with wrong mode
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi07_invalid_mode(void) {
    for (int mode = 0; mode <= 15; mode++) {
        if (mode == 1) continue;  /* mode=1 is the cell mode */
        H3Index cell = make_valid_ext_cell(20, 5);
        H3_SET_MODE(cell, mode);
        int valid = -1;
        H3Error err = h3_ext_is_valid_cell(&cell, &valid);
        ASSERT_MSG(err == H3_SUCCESS, "ABI-07: err = %d", err);
        ASSERT_MSG(valid == 0, "ABI-07: mode=%d should be invalid", mode);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-08: is_valid_cell returns 0 for cell with high bit / reserved set
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi08_invalid_top_bits(void) {
    /* High bit set */
    H3Index cell = make_valid_ext_cell(18, 5);
    H3_SET_HIGH_BIT(cell, 1);
    int valid = -1;
    H3Error err = h3_ext_is_valid_cell(&cell, &valid);
    ASSERT_MSG(err == H3_SUCCESS && valid == 0,
        "ABI-08: high-bit cell err=%d valid=%d", err, valid);

    /* Reserved 56-58 set */
    for (int v = 1; v <= 7; v++) {
        cell = make_valid_ext_cell(20, 5);
        H3_SET_RESERVED_BITS(cell, v);
        valid = -1;
        err = h3_ext_is_valid_cell(&cell, &valid);
        ASSERT_MSG(err == H3_SUCCESS && valid == 0,
            "ABI-08: reserved=%d valid=%d", v, valid);
    }

    /* Reserved 86–127: dirty bit at every position */
    for (int bit = 86; bit <= 127; bit++) {
        cell = make_valid_ext_cell(19, 5);
        cell |= ((__uint128_t)1) << bit;
        valid = -1;
        err = h3_ext_is_valid_cell(&cell, &valid);
        ASSERT_MSG(err == H3_SUCCESS && valid == 0,
            "ABI-08: bit %d valid=%d", bit, valid);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-09: get_resolution returns effective res for every cell
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi09_get_resolution(void) {
    for (int res = 0; res <= 22; res++) {
        H3Index cell = make_parent(res, 33);
        int got = -1;
        H3Error err = h3_ext_get_resolution(&cell, &got);
        ASSERT_MSG(err == H3_SUCCESS, "ABI-09: err=%d res=%d", err, res);
        ASSERT_MSG(got == res, "ABI-09: res=%d got=%d", res, got);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-10: cell_to_parent — basic and across boundary
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi10_cell_to_parent(void) {
    /* Same-res yields the same canonical cell. */
    for (int res = 0; res <= 22; res++) {
        H3Index cell = make_parent(res, 17);
        H3Index parent;
        H3Error err = h3_ext_cell_to_parent(&cell, res, &parent);
        ASSERT_MSG(err == H3_SUCCESS, "ABI-10: same-res err=%d res=%d", err, res);
        ASSERT_MSG(parent == cell,
            "ABI-10: same-res parent != cell (res=%d)", res);
    }

    /* Boundary: ext cell res 17 → stock parent res 14. */
    H3Index ext_cell = make_valid_ext_cell(17, 5);
    H3_SET_INDEX_DIGIT(ext_cell, 1, 2);
    H3_SET_INDEX_DIGIT(ext_cell, 14, 3);
    H3_SET_EXT_INDEX_DIGIT(ext_cell, 17, 4);
    H3Index parent;
    H3Error err = h3_ext_cell_to_parent(&ext_cell, 14, &parent);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-10: ext→stock err=%d", err);
    int pres = -1;
    h3_ext_get_resolution(&parent, &pres);
    ASSERT_MSG(pres == 14, "ABI-10: ext→stock parent res = %d", pres);
    ASSERT_MSG(H3_GET_EXT_FLAG(parent) == 0, "ABI-10: ext→stock parent has ext flag");
    ASSERT_MSG(H3_GET_BASE_CELL(parent) == 5, "ABI-10: ext→stock parent base cell wrong");
    /* High half should be cleared when stepping back to stock encoding. */
    ASSERT_MSG((parent >> 64) == 0, "ABI-10: ext→stock parent high half non-zero");

    /* Bad parentRes returns E_RES_DOMAIN. */
    err = h3_ext_cell_to_parent(&ext_cell, 18, &parent);  /* parent > child */
    ASSERT_MSG(err == E_RES_DOMAIN, "ABI-10: parent>child not E_RES_DOMAIN (err=%d)", err);
    err = h3_ext_cell_to_parent(&ext_cell, -1, &parent);
    ASSERT_MSG(err == E_RES_DOMAIN, "ABI-10: parent=-1 not E_RES_DOMAIN");
    err = h3_ext_cell_to_parent(&ext_cell, 23, &parent);
    ASSERT_MSG(err == E_RES_DOMAIN, "ABI-10: parent=23 not E_RES_DOMAIN");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-11: cell_to_children_size for many (parent_res, child_res) pairs
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi11_children_size(void) {
    for (int p = 0; p <= 22; p++) {
        for (int c = p; c <= 22; c++) {
            H3Index cell = make_parent(p, 5);
            int64_t sz = -1;
            H3Error err = h3_ext_cell_to_children_size(&cell, c, &sz);
            ASSERT_MSG(err == H3_SUCCESS, "ABI-11: err=%d p=%d c=%d", err, p, c);
            int64_t expected = 1;
            for (int i = 0; i < c - p; i++) expected *= 7;
            ASSERT_MSG(sz == expected,
                "ABI-11: p=%d c=%d sz=%lld expected=%lld",
                p, c, (long long)sz, (long long)expected);
        }
    }
    /* child < parent → E_RES_DOMAIN. */
    H3Index cell = make_parent(20, 5);
    int64_t sz;
    H3Error err = h3_ext_cell_to_children_size(&cell, 18, &sz);
    ASSERT_MSG(err == E_RES_DOMAIN, "ABI-11: child<parent not E_RES_DOMAIN");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-12: cell_to_children — array round-trip with 343 children
 * ═══════════════════════════════════════════════════════════════════════════ */
static int cmp_h3(const void *a, const void *b) {
    H3Index x = *(const H3Index *)a;
    H3Index y = *(const H3Index *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static void test_abi12_children_array(void) {
    H3Index parent = make_valid_stock_cell(15, 7);
    int64_t sz = 0;
    H3Error err = h3_ext_cell_to_children_size(&parent, 18, &sz);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-12: size err=%d", err);
    ASSERT_MSG(sz == 343, "ABI-12: expected 343 got %lld", (long long)sz);

    H3Index *buf = calloc((size_t)sz, sizeof(H3Index));
    ASSERT_MSG(buf != NULL, "ABI-12: calloc failed");

    int64_t actual = 0;
    err = h3_ext_cell_to_children(&parent, 18, buf, &actual);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-12: children err=%d", err);
    ASSERT_MSG(actual == sz,
        "ABI-12: actual=%lld expected=%lld", (long long)actual, (long long)sz);

    /* Verify each child has eff res 18, ext flag set, base cell preserved. */
    int violations = 0;
    for (int64_t i = 0; i < actual; i++) {
        if (H3_GET_EFFECTIVE_RESOLUTION(buf[i]) != 18) violations++;
        if (H3_GET_EXT_FLAG(buf[i]) != 1) violations++;
        if (H3_GET_BASE_CELL(buf[i]) != 7) violations++;
        if ((buf[i] >> 86) != 0) violations++;
    }
    ASSERT_MSG(violations == 0,
        "ABI-12: child invariant violations = %d", violations);

    /* Dedup. */
    qsort(buf, (size_t)actual, sizeof(H3Index), cmp_h3);
    int dup = 0;
    for (int64_t i = 1; i < actual; i++) if (buf[i] == buf[i-1]) dup++;
    ASSERT_MSG(dup == 0, "ABI-12: %d duplicates in array", dup);

    free(buf);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-13: lat_lng_to_cell pointer write
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi13_lat_lng_to_cell(void) {
    H3Index out = (H3Index)0x123456;
    H3Error err = h3_ext_lat_lng_to_cell(40.7128, -74.0060, 18, &out);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-13: err=%d", err);
    ASSERT_MSG(out != (H3Index)0x123456, "ABI-13: out not modified");
    /* Determinism: same inputs → same output. */
    H3Index out2 = (H3Index)0;
    err = h3_ext_lat_lng_to_cell(40.7128, -74.0060, 18, &out2);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-13: err2=%d", err);
    ASSERT_MSG(out == out2, "ABI-13: deterministic mapping broken");

    /* Different inputs → different outputs. */
    H3Index out3 = (H3Index)0;
    err = h3_ext_lat_lng_to_cell(40.7128, -74.0060, 19, &out3);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-13: err3=%d", err);
    ASSERT_MSG(out != out3, "ABI-13: different res produced same output");

    /* Bad res yields E_RES_DOMAIN. */
    err = h3_ext_lat_lng_to_cell(0, 0, -1, &out);
    ASSERT_MSG(err == E_RES_DOMAIN, "ABI-13: res=-1 not E_RES_DOMAIN");
    err = h3_ext_lat_lng_to_cell(0, 0, 23, &out);
    ASSERT_MSG(err == E_RES_DOMAIN, "ABI-13: res=23 not E_RES_DOMAIN");

    /* NaN yields E_DOMAIN. */
    err = h3_ext_lat_lng_to_cell(NAN, 0, 10, &out);
    ASSERT_MSG(err == E_DOMAIN, "ABI-13: NaN lat not E_DOMAIN");
    err = h3_ext_lat_lng_to_cell(0, NAN, 10, &out);
    ASSERT_MSG(err == E_DOMAIN, "ABI-13: NaN lng not E_DOMAIN");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-14: cell_to_lat_lng pointer writes
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi14_cell_to_lat_lng(void) {
    H3Index cell = make_valid_ext_cell(18, 42);
    double lat = NAN, lng = NAN;
    H3Error err = h3_ext_cell_to_lat_lng(&cell, &lat, &lng);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-14: err=%d", err);
    ASSERT_MSG(!isnan(lat), "ABI-14: lat not written");
    ASSERT_MSG(!isnan(lng), "ABI-14: lng not written");

    /* Different cells → different coordinates. */
    H3Index cell2 = make_valid_ext_cell(18, 99);
    H3_SET_EXT_INDEX_DIGIT(cell2, 18, 4);
    double lat2 = NAN, lng2 = NAN;
    err = h3_ext_cell_to_lat_lng(&cell2, &lat2, &lng2);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-14: err2=%d", err);
    ASSERT_MSG(lat != lat2 || lng != lng2,
        "ABI-14: distinct cells produced identical coordinates");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-15: grid_distance — same cell = 0, different cells > 0
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi15_grid_distance(void) {
    H3Index a = make_valid_ext_cell(20, 50);
    H3Index b = a;
    int64_t d = -1;
    H3Error err = h3_ext_grid_distance(&a, &b, &d);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-15: err=%d", err);
    ASSERT_MSG(d == 0, "ABI-15: same-cell distance = %lld", (long long)d);

    H3Index c = make_valid_ext_cell(20, 51);
    err = h3_ext_grid_distance(&a, &c, &d);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-15: err2=%d", err);
    ASSERT_MSG(d > 0, "ABI-15: distinct cells distance = %lld", (long long)d);

    /* Symmetry. */
    int64_t d_ba;
    err = h3_ext_grid_distance(&c, &a, &d_ba);
    ASSERT_MSG(err == H3_SUCCESS && d == d_ba,
        "ABI-15: distance not symmetric");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-16: h3_to_string — format and length
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi16_h3_to_string(void) {
    H3Index cell = make_valid_ext_cell(18, 7);
    char buf[64];
    memset(buf, 'X', sizeof(buf));
    H3Error err = h3_ext_h3_to_string(&cell, buf, sizeof(buf));
    ASSERT_MSG(err == H3_SUCCESS, "ABI-16: err=%d", err);
    ASSERT_MSG(strlen(buf) == 32, "ABI-16: strlen = %zu, expected 32", strlen(buf));
    /* Each character must be a hex digit. */
    int bad = 0;
    for (int i = 0; i < 32; i++) {
        char c = buf[i];
        int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) bad++;
    }
    ASSERT_MSG(bad == 0, "ABI-16: %d non-hex characters", bad);
    ASSERT_MSG(buf[32] == '\0', "ABI-16: string not null-terminated");

    /* Buffer too small → E_FAILED. */
    char tiny[16];
    err = h3_ext_h3_to_string(&cell, tiny, sizeof(tiny));
    ASSERT_MSG(err == E_FAILED, "ABI-16: tiny buf not E_FAILED");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-17: string_to_h3 — parse + bad-input rejection
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi17_string_to_h3(void) {
    /* All zeros */
    H3Index out;
    H3Error err = h3_ext_string_to_h3("00000000000000000000000000000000", &out);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-17: zero err=%d", err);
    ASSERT_MSG(out == (H3Index)0, "ABI-17: zero string did not give zero cell");

    /* All ones */
    err = h3_ext_string_to_h3("ffffffffffffffffffffffffffffffff", &out);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-17: ones err=%d", err);
    ASSERT_MSG(out == (~(H3Index)0), "ABI-17: ff string did not give all-ones cell");

    /* Mixed-case accepted */
    err = h3_ext_string_to_h3("0123456789ABCDEF0123456789abcdef", &out);
    ASSERT_MSG(err == H3_SUCCESS, "ABI-17: mixed-case err=%d", err);

    /* Bad length */
    err = h3_ext_string_to_h3("abc", &out);
    ASSERT_MSG(err == E_FAILED, "ABI-17: short string not E_FAILED");
    err = h3_ext_string_to_h3("000000000000000000000000000000000", &out);  /* 33 chars */
    ASSERT_MSG(err == E_FAILED, "ABI-17: long string not E_FAILED");
    err = h3_ext_string_to_h3("", &out);
    ASSERT_MSG(err == E_FAILED, "ABI-17: empty string not E_FAILED");

    /* Bad characters */
    err = h3_ext_string_to_h3("0000000000000000000000000000000g", &out);
    ASSERT_MSG(err == E_FAILED, "ABI-17: non-hex character not E_FAILED");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-18: String round-trip — 100 cells across all ext resolutions
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi18_string_roundtrip(void) {
    /* Deterministic LCG so the test is reproducible. */
    uint64_t s = 0xDEADBEEFCAFEBABEULL;
    for (int i = 0; i < 100; i++) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        int res = (int)(s % 23);  /* 0..22 */
        int bc  = (int)((s >> 16) % 122);
        H3Index cell = make_parent(res, bc);
        /* Sprinkle some random digits where applicable. */
        for (int r = 1; r <= res; r++) {
            int d = (int)((s >> (r % 30)) % 7);
            H3_SET_DIGIT_AT_RES(cell, r, d);
        }

        char buf[64];
        H3Error err = h3_ext_h3_to_string(&cell, buf, sizeof(buf));
        ASSERT_MSG(err == H3_SUCCESS, "ABI-18: i=%d to_string err=%d", i, err);

        H3Index parsed;
        err = h3_ext_string_to_h3(buf, &parsed);
        ASSERT_MSG(err == H3_SUCCESS, "ABI-18: i=%d from_string err=%d", i, err);

        /* Bit-identical round-trip — the decisive ABI test. */
        ASSERT_MSG(memcmp(&cell, &parsed, sizeof(H3Index)) == 0,
            "ABI-18: i=%d round-trip mismatch", i);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-19: NULL-pointer error contract — every shim function returns E_FAILED
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi19_null_contract(void) {
    H3Index dummy = (H3Index)0;
    int idummy = 0;
    int64_t i64dummy = 0;
    double ddummy = 0;
    char buf[64];
    char *cdummy = buf;

    ASSERT_MSG(h3_ext_lat_lng_to_cell(0, 0, 1, NULL) == E_FAILED,
        "ABI-19: lat_lng_to_cell(NULL)");
    ASSERT_MSG(h3_ext_cell_to_lat_lng(NULL, &ddummy, &ddummy) == E_FAILED,
        "ABI-19: cell_to_lat_lng(NULL cell)");
    ASSERT_MSG(h3_ext_cell_to_lat_lng(&dummy, NULL, &ddummy) == E_FAILED,
        "ABI-19: cell_to_lat_lng(NULL lat)");
    ASSERT_MSG(h3_ext_cell_to_lat_lng(&dummy, &ddummy, NULL) == E_FAILED,
        "ABI-19: cell_to_lat_lng(NULL lng)");
    ASSERT_MSG(h3_ext_cell_to_parent(NULL, 5, &dummy) == E_FAILED,
        "ABI-19: cell_to_parent(NULL cell)");
    ASSERT_MSG(h3_ext_cell_to_parent(&dummy, 5, NULL) == E_FAILED,
        "ABI-19: cell_to_parent(NULL out)");
    ASSERT_MSG(h3_ext_cell_to_children(NULL, 5, &dummy, &i64dummy) == E_FAILED,
        "ABI-19: cell_to_children(NULL cell)");
    ASSERT_MSG(h3_ext_cell_to_children(&dummy, 5, NULL, &i64dummy) == E_FAILED,
        "ABI-19: cell_to_children(NULL children)");
    ASSERT_MSG(h3_ext_cell_to_children(&dummy, 5, &dummy, NULL) == E_FAILED,
        "ABI-19: cell_to_children(NULL count)");
    ASSERT_MSG(h3_ext_cell_to_children_size(NULL, 5, &i64dummy) == E_FAILED,
        "ABI-19: children_size(NULL cell)");
    ASSERT_MSG(h3_ext_cell_to_children_size(&dummy, 5, NULL) == E_FAILED,
        "ABI-19: children_size(NULL out)");
    ASSERT_MSG(h3_ext_is_valid_cell(NULL, &idummy) == E_FAILED,
        "ABI-19: is_valid(NULL cell)");
    ASSERT_MSG(h3_ext_is_valid_cell(&dummy, NULL) == E_FAILED,
        "ABI-19: is_valid(NULL out)");
    ASSERT_MSG(h3_ext_get_resolution(NULL, &idummy) == E_FAILED,
        "ABI-19: get_resolution(NULL cell)");
    ASSERT_MSG(h3_ext_get_resolution(&dummy, NULL) == E_FAILED,
        "ABI-19: get_resolution(NULL out)");
    ASSERT_MSG(h3_ext_grid_distance(NULL, &dummy, &i64dummy) == E_FAILED,
        "ABI-19: grid_distance(NULL a)");
    ASSERT_MSG(h3_ext_grid_distance(&dummy, NULL, &i64dummy) == E_FAILED,
        "ABI-19: grid_distance(NULL b)");
    ASSERT_MSG(h3_ext_grid_distance(&dummy, &dummy, NULL) == E_FAILED,
        "ABI-19: grid_distance(NULL out)");
    ASSERT_MSG(h3_ext_h3_to_string(NULL, cdummy, 64) == E_FAILED,
        "ABI-19: h3_to_string(NULL cell)");
    ASSERT_MSG(h3_ext_h3_to_string(&dummy, NULL, 64) == E_FAILED,
        "ABI-19: h3_to_string(NULL out)");
    ASSERT_MSG(h3_ext_string_to_h3(NULL, &dummy) == E_FAILED,
        "ABI-19: string_to_h3(NULL str)");
    ASSERT_MSG(h3_ext_string_to_h3("00000000000000000000000000000000", NULL) == E_FAILED,
        "ABI-19: string_to_h3(NULL out)");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-20: Cross-TU pointer integrity over many calls (stress)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi20_stress_pointer(void) {
    /* Fabricate 500 ext cells, push each through is_valid_cell + get_resolution,
     * round-trip via string. Verify nothing corrupts the original cell. */
    uint64_t s = 0xC0FFEE12345ULL;
    int violations = 0;
    for (int i = 0; i < 500; i++) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        int res = 16 + (int)(s % 7);  /* 16..22 */
        int bc  = (int)((s >> 16) % 122);
        H3Index cell = make_valid_ext_cell(res, bc);
        H3Index original = cell;

        int valid = -1;
        if (h3_ext_is_valid_cell(&cell, &valid) != H3_SUCCESS) violations++;
        if (valid != 1) violations++;

        int got_res = -1;
        if (h3_ext_get_resolution(&cell, &got_res) != H3_SUCCESS) violations++;
        if (got_res != res) violations++;

        char buf[64];
        if (h3_ext_h3_to_string(&cell, buf, sizeof(buf)) != H3_SUCCESS) violations++;

        H3Index parsed;
        if (h3_ext_string_to_h3(buf, &parsed) != H3_SUCCESS) violations++;
        if (parsed != original) violations++;

        if (cell != original) violations++;  /* cell must be untouched */
    }
    ASSERT_MSG(violations == 0,
        "ABI-20: %d violations across 500-cell stress", violations);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-21: Adversarial inputs — boundary values and bit patterns
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi21_adversarial(void) {
    /* All zeros: not a valid cell (mode != 1). */
    H3Index zero = (H3Index)0;
    int valid = -1;
    H3Error err = h3_ext_is_valid_cell(&zero, &valid);
    ASSERT_MSG(err == H3_SUCCESS && valid == 0,
        "ABI-21: all-zero cell err=%d valid=%d", err, valid);

    /* All ones: high bit set + reserved set + bits 86–127 set + ... → invalid. */
    H3Index ones = ~(H3Index)0;
    err = h3_ext_is_valid_cell(&ones, &valid);
    ASSERT_MSG(err == H3_SUCCESS && valid == 0,
        "ABI-21: all-ones cell err=%d valid=%d", err, valid);

    /* String round-trip on edge values. */
    char buf[64];
    err = h3_ext_h3_to_string(&zero, buf, sizeof(buf));
    ASSERT_MSG(err == H3_SUCCESS, "ABI-21: to_string(zero) err=%d", err);
    H3Index parsed;
    err = h3_ext_string_to_h3(buf, &parsed);
    ASSERT_MSG(err == H3_SUCCESS && parsed == zero,
        "ABI-21: zero string round-trip mismatch");

    err = h3_ext_h3_to_string(&ones, buf, sizeof(buf));
    ASSERT_MSG(err == H3_SUCCESS, "ABI-21: to_string(ones) err=%d", err);
    err = h3_ext_string_to_h3(buf, &parsed);
    ASSERT_MSG(err == H3_SUCCESS && parsed == ones,
        "ABI-21: ones string round-trip mismatch");

    /* Cell with only the 128-bit MSB set — exercises high-half pointer transit. */
    H3Index msb = ((H3Index)1) << 127;
    err = h3_ext_h3_to_string(&msb, buf, sizeof(buf));
    ASSERT_MSG(err == H3_SUCCESS, "ABI-21: to_string(msb) err=%d", err);
    err = h3_ext_string_to_h3(buf, &parsed);
    ASSERT_MSG(err == H3_SUCCESS && parsed == msb,
        "ABI-21: msb round-trip mismatch (high-half transit broken)");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ABI-22: Heap allocation alignment + write-through-pointer
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test_abi22_heap_alignment(void) {
    /* Allocate an array of H3Index on the heap; verify alignment of the
     * pointer matches alignof, and that calls write through correctly. */
    H3Index *arr = calloc(8, sizeof(H3Index));
    ASSERT_MSG(arr != NULL, "ABI-22: calloc failed");
    /* Most malloc implementations give us at least 16-byte alignment. */
    uintptr_t addr = (uintptr_t)arr;
    ASSERT_MSG((addr % _Alignof(H3Index)) == 0,
        "ABI-22: heap pointer not aligned (addr=%llx, align=%zu)",
        (unsigned long long)addr, _Alignof(H3Index));

    /* Populate via shim. Use non-integer doubles so the inputs have
     * variation in low IEEE-754 mantissa bits — integer-valued doubles
     * differ only in their exponent fields, which the shim's hash bins
     * away. */
    for (int i = 0; i < 8; i++) {
        double lat = 40.7128 + (double)i * 0.001;
        double lng = -74.0060 - (double)i * 0.001;
        H3Error err = h3_ext_lat_lng_to_cell(lat, lng, 18, &arr[i]);
        ASSERT_MSG(err == H3_SUCCESS, "ABI-22: arr[%d] err=%d", i, err);
        ASSERT_MSG(arr[i] != (H3Index)0,
            "ABI-22: arr[%d] not written", i);
    }
    /* All elements must be distinct (different inputs). */
    int dup = 0;
    for (int i = 0; i < 8; i++) {
        for (int j = i + 1; j < 8; j++) {
            if (arr[i] == arr[j]) dup++;
        }
    }
    ASSERT_MSG(dup == 0, "ABI-22: %d duplicate heap-array entries", dup);
    free(arr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void) {
    printf("H3-Extended (128-bit) Preflight — FFI ABI (POC-4)\n");
    printf("=================================================\n");
    printf("Caller-side sizeof(H3Index)  = %zu\n", sizeof(H3Index));
    printf("Caller-side _Alignof(H3Index)= %zu\n", _Alignof(H3Index));
    printf("Shim-side  sizeof  (via fn)  = %zu\n", h3_ext_sizeof_h3index());
    printf("Shim-side  alignof (via fn)  = %zu\n", h3_ext_alignof_h3index());
    printf("\n");

    test_abi01_caller_sizeof();
    test_abi02_caller_alignof();
    test_abi03_cross_tu_sizeof();
    test_abi04_cross_tu_alignof();
    test_abi05_valid_stock();
    test_abi06_valid_ext();
    test_abi07_invalid_mode();
    test_abi08_invalid_top_bits();
    test_abi09_get_resolution();
    test_abi10_cell_to_parent();
    test_abi11_children_size();
    test_abi12_children_array();
    test_abi13_lat_lng_to_cell();
    test_abi14_cell_to_lat_lng();
    test_abi15_grid_distance();
    test_abi16_h3_to_string();
    test_abi17_string_to_h3();
    test_abi18_string_roundtrip();
    test_abi19_null_contract();
    test_abi20_stress_pointer();
    test_abi21_adversarial();
    test_abi22_heap_alignment();

    printf("\n=================================================\n");
    printf("Results: %d passed, %d failed\n", g_pass_count, g_fail_count);

    if (g_fail_count > 0) {
        printf("POC-4 FAILED — FFI ABI has %d issue(s).\n", g_fail_count);
        return 1;
    } else {
        printf("POC-4 PASSED — FFI ABI validated.\n");
        return 0;
    }
}
