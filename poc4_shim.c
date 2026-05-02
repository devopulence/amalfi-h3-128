/*
 * poc4_shim.c — Shim implementation for the POC-4 FFI ABI test (TU 1)
 *
 * Version: 1.0.0 — 2026-05-02
 *
 * This translation unit implements the 10 cross-TU shim functions plus the
 * sizeof/alignof probes. Group A/B/C macros are copied verbatim from
 * poc1_bit_layout.c. Functions read/write 128-bit values exclusively through
 * pointer arguments.
 */
#include "poc4_shim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * GROUP A — Stock bit-position constants (verbatim from poc1_bit_layout.c).
 * Note: H3Index typedef comes from poc4_shim.h.
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
 * GROUP B — Stock getter/setter macros (verbatim).
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
 * GROUP C — Group C macros (verbatim).
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
 * Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/* Build a child cell at a given (parent, parentRes, childRes, base-7 index).
 * The child's encoding is canonical: digits 1..parentRes inherited from parent,
 * digits parentRes+1..childRes derived from the base-7 decomposition of `i`,
 * digits childRes+1..max set to sentinel 7. */
static H3Index decode_child(H3Index parent, int parentRes, int childRes, int64_t i) {
    H3Index h = parent;
    H3_SET_EFFECTIVE_RESOLUTION(h, childRes);
    int max_pos = (childRes >= 16) ? MAX_H3_EXT_RES : MAX_H3_RES;
    for (int r = childRes + 1; r <= max_pos; r++) {
        H3_SET_DIGIT_AT_RES(h, r, INVALID_DIGIT);
    }
    /* Decode i base-7 from least-significant-digit (deepest position) up. */
    for (int r = childRes; r >= parentRes + 1; r--) {
        int d = (int)(i % 7);
        i /= 7;
        H3_SET_DIGIT_AT_RES(h, r, d);
    }
    return h;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Cross-TU consistency probes
 * ═══════════════════════════════════════════════════════════════════════════ */

size_t h3_ext_sizeof_h3index(void) {
    return sizeof(H3Index);
}

size_t h3_ext_alignof_h3index(void) {
    return _Alignof(H3Index);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * The 10 shim functions
 * ═══════════════════════════════════════════════════════════════════════════ */

H3Error h3_ext_lat_lng_to_cell(double lat, double lng, int res, H3Index *out) {
    if (out == NULL) return E_FAILED;
    if (isnan(lat) || isnan(lng)) return E_DOMAIN;
    if (res < 0 || res > MAX_H3_EXT_RES) return E_RES_DOMAIN;

    /* Pack a deterministic value derived from inputs. This is NOT a real
     * H3 index — it is a stable mapping that exercises pointer-write
     * across the TU boundary. */
    uint64_t lat_bits, lng_bits;
    memcpy(&lat_bits, &lat, 8);
    memcpy(&lng_bits, &lng, 8);

    /* Use only the digit/data area for derived bits — leave reserved zero. */
    H3Index h = (H3Index)0;
    /* Stock digit area: bits 0–44 ← low 45 bits of lat ^ lng ^ res. */
    uint64_t mix = (lat_bits ^ lng_bits ^ ((uint64_t)res * 0x9E3779B97F4A7C15ULL));
    h |= (H3Index)(mix & ((UINT64_C(1) << 45) - 1));
    /* Base cell field (bits 45–51): low 7 bits of an additional mix. */
    H3_SET_BASE_CELL(h, (int)((mix >> 45) & 0x7F));
    /* Mode = 1 (cell). */
    H3_SET_MODE(h, 1);
    /* Effective resolution. */
    H3_SET_EFFECTIVE_RESOLUTION(h, res);
    /* Ext digits: derived from the high half of the lat/lng mix. */
    if (res >= 16) {
        for (int r = 16; r <= res; r++) {
            int d = (int)((lng_bits >> ((r - 16) * 3)) & 0x7);
            if (d == 7) d = 0;
            H3_SET_EXT_INDEX_DIGIT(h, r, d);
        }
        for (int r = res + 1; r <= MAX_H3_EXT_RES; r++) {
            H3_SET_EXT_INDEX_DIGIT(h, r, INVALID_DIGIT);
        }
    }
    *out = h;
    return H3_SUCCESS;
}

H3Error h3_ext_cell_to_lat_lng(const H3Index *cell, double *lat, double *lng) {
    if (cell == NULL || lat == NULL || lng == NULL) return E_FAILED;
    /* Deterministic outputs derived from the cell's bits. Range chosen to be
     * non-degenerate and easy to verify. */
    uint64_t lo = (uint64_t)*cell;
    uint64_t hi = (uint64_t)(*cell >> 64);
    *lat = (double)((lo >> 7) & 0xFFFFFFFFu) / 1e8;
    *lng = (double)((hi >> 7) & 0xFFFFFFFFu) / 1e8;
    return H3_SUCCESS;
}

H3Error h3_ext_cell_to_parent(const H3Index *cell, int parentRes, H3Index *out) {
    if (cell == NULL || out == NULL) return E_FAILED;
    if (parentRes < 0 || parentRes > MAX_H3_EXT_RES) return E_RES_DOMAIN;
    int childRes = H3_GET_EFFECTIVE_RESOLUTION(*cell);
    if (parentRes > childRes) return E_RES_DOMAIN;

    H3Index h = *cell;
    H3_SET_EFFECTIVE_RESOLUTION(h, parentRes);
    int max_pos = (parentRes >= 16) ? MAX_H3_EXT_RES : MAX_H3_RES;
    for (int r = parentRes + 1; r <= max_pos; r++) {
        H3_SET_DIGIT_AT_RES(h, r, INVALID_DIGIT);
    }
    /* When stepping from ext encoding down to stock encoding, the high half's
     * bits (ext flag + ext digit field) must be cleared. */
    if (parentRes < 16) {
        h &= (((H3Index)1 << 64) - 1);
    }
    *out = h;
    return H3_SUCCESS;
}

H3Error h3_ext_cell_to_children_size(const H3Index *cell, int childRes,
                                     int64_t *out) {
    if (cell == NULL || out == NULL) return E_FAILED;
    if (childRes < 0 || childRes > MAX_H3_EXT_RES) return E_RES_DOMAIN;
    int parentRes = H3_GET_EFFECTIVE_RESOLUTION(*cell);
    if (childRes < parentRes) return E_RES_DOMAIN;
    int64_t n = 1;
    for (int i = 0; i < childRes - parentRes; i++) n *= 7;
    *out = n;
    return H3_SUCCESS;
}

H3Error h3_ext_cell_to_children(const H3Index *cell, int childRes,
                                H3Index *children, int64_t *count) {
    if (cell == NULL || children == NULL || count == NULL) return E_FAILED;
    if (childRes < 0 || childRes > MAX_H3_EXT_RES) return E_RES_DOMAIN;
    int parentRes = H3_GET_EFFECTIVE_RESOLUTION(*cell);
    if (childRes < parentRes) return E_RES_DOMAIN;

    int64_t n = 1;
    for (int i = 0; i < childRes - parentRes; i++) n *= 7;

    H3Index parent = *cell;
    for (int64_t i = 0; i < n; i++) {
        children[i] = decode_child(parent, parentRes, childRes, i);
    }
    *count = n;
    return H3_SUCCESS;
}

H3Error h3_ext_is_valid_cell(const H3Index *cell, int *out) {
    if (cell == NULL || out == NULL) return E_FAILED;
    int valid = 1;
    H3Index c = *cell;
    if (H3_GET_MODE(c) != 1) valid = 0;
    if (H3_GET_HIGH_BIT(c) != 0) valid = 0;
    if (H3_GET_RESERVED_BITS(c) != 0) valid = 0;
    if ((c >> 86) != 0) valid = 0;
    *out = valid;
    return H3_SUCCESS;
}

H3Error h3_ext_get_resolution(const H3Index *cell, int *out) {
    if (cell == NULL || out == NULL) return E_FAILED;
    *out = H3_GET_EFFECTIVE_RESOLUTION(*cell);
    return H3_SUCCESS;
}

H3Error h3_ext_grid_distance(const H3Index *a, const H3Index *b, int64_t *out) {
    if (a == NULL || b == NULL || out == NULL) return E_FAILED;
    /* Deterministic stub: hamming distance of the two 128-bit values.
     * Real grid distance is far more complex; this is sufficient for ABI
     * verification (it only requires the bytes round-trip cleanly). */
    H3Index x = (*a) ^ (*b);
    int64_t c = 0;
    for (int i = 0; i < 128; i++) {
        if ((x >> i) & (H3Index)1) c++;
    }
    *out = c;
    return H3_SUCCESS;
}

H3Error h3_ext_h3_to_string(const H3Index *cell, char *out, size_t sz) {
    if (cell == NULL || out == NULL) return E_FAILED;
    if (sz < 33) return E_FAILED;  /* 32 hex chars + NUL */
    uint64_t hi = (uint64_t)(*cell >> 64);
    uint64_t lo = (uint64_t)*cell;
    int n = snprintf(out, sz, "%016llx%016llx",
                     (unsigned long long)hi, (unsigned long long)lo);
    if (n < 0 || n >= (int)sz) return E_FAILED;
    return H3_SUCCESS;
}

H3Error h3_ext_string_to_h3(const char *str, H3Index *out) {
    if (str == NULL || out == NULL) return E_FAILED;
    size_t len = strlen(str);
    if (len != 32) return E_FAILED;
    uint64_t hi = 0, lo = 0;
    for (int i = 0; i < 16; i++) {
        int v = hex_nibble(str[i]);
        if (v < 0) return E_FAILED;
        hi = (hi << 4) | (uint64_t)v;
    }
    for (int i = 16; i < 32; i++) {
        int v = hex_nibble(str[i]);
        if (v < 0) return E_FAILED;
        lo = (lo << 4) | (uint64_t)v;
    }
    *out = ((H3Index)hi << 64) | (H3Index)lo;
    return H3_SUCCESS;
}
