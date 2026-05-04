/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 */
/** @file h3ExtShim.c
 * @brief H3-Extended FFI shim — Phase D7 Pattern 4 (POC-4 contract).
 *
 * Each shim dereferences its by-pointer H3Index inputs, calls the real
 * (widened, ext-aware) H3 entry point, and writes outputs through the
 * caller-allocated pointers. NULL-pointer arguments return E_FAILED.
 * H3Error values flow through unchanged from the underlying call.
 */
#include "h3ExtShim.h"

#include <math.h>

#include "h3api.h"

H3Error h3_ext_lat_lng_to_cell(double lat, double lng, int res, H3Index *out) {
    if (out == NULL) return E_FAILED;
    if (isnan(lat) || isnan(lng)) return E_LATLNG_DOMAIN;
    LatLng g = {.lat = lat, .lng = lng};
    return H3_EXPORT(latLngToCell)(&g, res, out);
}

H3Error h3_ext_cell_to_lat_lng(const H3Index *cell, double *lat, double *lng) {
    if (cell == NULL || lat == NULL || lng == NULL) return E_FAILED;
    LatLng g;
    H3Error err = H3_EXPORT(cellToLatLng)(*cell, &g);
    if (err != E_SUCCESS) return err;
    *lat = g.lat;
    *lng = g.lng;
    return E_SUCCESS;
}

H3Error h3_ext_cell_to_parent(const H3Index *cell, int parentRes,
                              H3Index *out) {
    if (cell == NULL || out == NULL) return E_FAILED;
    return H3_EXPORT(cellToParent)(*cell, parentRes, out);
}

H3Error h3_ext_cell_to_children_size(const H3Index *cell, int childRes,
                                     int64_t *out) {
    if (cell == NULL || out == NULL) return E_FAILED;
    return H3_EXPORT(cellToChildrenSize)(*cell, childRes, out);
}

H3Error h3_ext_cell_to_children(const H3Index *cell, int childRes,
                                H3Index *children, int64_t *count) {
    if (cell == NULL || children == NULL || count == NULL) return E_FAILED;
    int64_t sz = 0;
    H3Error err = H3_EXPORT(cellToChildrenSize)(*cell, childRes, &sz);
    if (err != E_SUCCESS) return err;
    err = H3_EXPORT(cellToChildren)(*cell, childRes, children);
    if (err != E_SUCCESS) return err;
    *count = sz;
    return E_SUCCESS;
}

H3Error h3_ext_is_valid_cell(const H3Index *cell, int *out) {
    if (cell == NULL || out == NULL) return E_FAILED;
    *out = H3_EXPORT(isValidCell)(*cell);
    return E_SUCCESS;
}

H3Error h3_ext_get_resolution(const H3Index *cell, int *out) {
    if (cell == NULL || out == NULL) return E_FAILED;
    *out = H3_EXPORT(getResolution)(*cell);
    return E_SUCCESS;
}

H3Error h3_ext_grid_distance(const H3Index *a, const H3Index *b,
                             int64_t *out) {
    if (a == NULL || b == NULL || out == NULL) return E_FAILED;
    return H3_EXPORT(gridDistance)(*a, *b, out);
}

H3Error h3_ext_h3_to_string(const H3Index *cell, char *out, size_t sz) {
    if (cell == NULL || out == NULL) return E_FAILED;
    return H3_EXPORT(h3ToString)(*cell, out, sz);
}

H3Error h3_ext_string_to_h3(const char *str, H3Index *out) {
    if (str == NULL || out == NULL) return E_FAILED;
    return H3_EXPORT(stringToH3)(str, out);
}

size_t h3_ext_sizeof_h3index(void) { return sizeof(H3Index); }

size_t h3_ext_alignof_h3index(void) { return __alignof__(H3Index); }
