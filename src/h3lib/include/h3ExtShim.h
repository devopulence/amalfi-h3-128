/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 */
/** @file h3ExtShim.h
 * @brief H3-Extended FFI shim layer — Phase D7 deliverable.
 *
 * Cross-TU FFI surface for Python/PySpark/Scala consumers. Per playbook
 * §7 (and POC-4 ABI proof, 1314/1314 assertions ASAN+UBSAN clean):
 *
 *   - All H3Index values pass BY POINTER, never by value. __uint128_t
 *     lacks a stable register-passing ABI on Windows MSVC and is
 *     fragile on other platforms.
 *   - All shims return H3Error.
 *   - Caller pre-allocates output buffers; the shim never malloc's on
 *     the caller's behalf.
 *   - h3_ext_ prefix to avoid collision with stock H3 if the consumer
 *     links both libraries.
 *
 * Signatures here are the validated POC-4 prototypes (poc4_shim.h),
 * with H3Index typedef and H3Error sourced from h3api.h instead of
 * being redeclared.
 */

#ifndef H3EXTSHIM_H
#define H3EXTSHIM_H

#include "h3api.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The 10 shim functions — all pass H3Index by pointer. */

DECLSPEC H3Error h3_ext_lat_lng_to_cell(double lat, double lng, int res,
                                        H3Index *out);
DECLSPEC H3Error h3_ext_cell_to_lat_lng(const H3Index *cell, double *lat,
                                        double *lng);
DECLSPEC H3Error h3_ext_cell_to_parent(const H3Index *cell, int parentRes,
                                       H3Index *out);
DECLSPEC H3Error h3_ext_cell_to_children(const H3Index *cell, int childRes,
                                         H3Index *children, int64_t *count);
DECLSPEC H3Error h3_ext_cell_to_children_size(const H3Index *cell, int childRes,
                                              int64_t *out);
DECLSPEC H3Error h3_ext_is_valid_cell(const H3Index *cell, int *out);
DECLSPEC H3Error h3_ext_get_resolution(const H3Index *cell, int *out);
DECLSPEC H3Error h3_ext_grid_distance(const H3Index *a, const H3Index *b,
                                      int64_t *out);
DECLSPEC H3Error h3_ext_h3_to_string(const H3Index *cell, char *out, size_t sz);
DECLSPEC H3Error h3_ext_string_to_h3(const char *str, H3Index *out);

/* Cross-TU consistency probes (D7-G2 — must report 16/16 from any TU). */
DECLSPEC size_t h3_ext_sizeof_h3index(void);
DECLSPEC size_t h3_ext_alignof_h3index(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* H3EXTSHIM_H */
