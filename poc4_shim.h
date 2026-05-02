/*
 * poc4_shim.h — Shared header for the POC-4 FFI ABI test
 *
 * Version: 1.0.0 — 2026-05-02
 *
 * This header is included by BOTH translation units (poc4_shim.c and
 * poc4_caller.c). It defines the typedef and the cross-TU function
 * signatures. All H3Index values are passed by pointer — never by value —
 * because __uint128_t lacks a stable register-passing ABI across compilers
 * and platforms.
 */
#ifndef POC4_SHIM_H
#define POC4_SHIM_H

#ifndef __SIZEOF_INT128__
#error "Requires __uint128_t (GCC or Clang). MSVC is not supported."
#endif

#include <stdint.h>
#include <stddef.h>

typedef __uint128_t H3Index;

typedef enum {
    H3_SUCCESS   = 0,
    E_FAILED     = 1,
    E_DOMAIN     = 2,
    E_RES_DOMAIN = 3
} H3Error;

/* All 10 shim functions — by pointer, never by value */
H3Error h3_ext_lat_lng_to_cell(double lat, double lng, int res, H3Index *out);
H3Error h3_ext_cell_to_lat_lng(const H3Index *cell, double *lat, double *lng);
H3Error h3_ext_cell_to_parent(const H3Index *cell, int parentRes, H3Index *out);
H3Error h3_ext_cell_to_children(const H3Index *cell, int childRes,
                                H3Index *children, int64_t *count);
H3Error h3_ext_cell_to_children_size(const H3Index *cell, int childRes,
                                     int64_t *out);
H3Error h3_ext_is_valid_cell(const H3Index *cell, int *out);
H3Error h3_ext_get_resolution(const H3Index *cell, int *out);
H3Error h3_ext_grid_distance(const H3Index *a, const H3Index *b, int64_t *out);
H3Error h3_ext_h3_to_string(const H3Index *cell, char *out, size_t sz);
H3Error h3_ext_string_to_h3(const char *str, H3Index *out);

/* Cross-TU consistency probes */
size_t h3_ext_sizeof_h3index(void);
size_t h3_ext_alignof_h3index(void);

#endif /* POC4_SHIM_H */
