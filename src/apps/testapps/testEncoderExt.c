/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 */
/** @file testEncoderExt.c
 * @brief H3-Extended Phase D1 encoder gate tests.
 *
 * Phase D1 gate from playbook §8.D:
 *   D1-G1: latLngToCell at res 19 for 100 known coordinates produces ext
 *          cells with bit 64 set.
 *
 * Plus regression coverage on the four widened encoder sites:
 *   - latLngToCell  (Rule GR — guard relaxation)
 *   - vec3ToCell    (Rule GR — guard relaxation)
 *   - _faceIjkToH3  (Rules INIT + RW + DW)
 *   - setH3Index    (Rules INIT + RW + DW)
 *
 * Known issue (Session 2 / out of D1 scope): stock coordijk.h arithmetic
 * uses 32-bit int. At ext res 20-22 the intermediate i/j/k values from
 * `_hex2dToCoordIJK` can exceed INT_MAX for points far from a face center,
 * triggering UBSAN signed-overflow findings. This is independent of the
 * Group C widening — it lives in stock geometry and needs coordijk
 * int64-widening or a bounded-coordinate contract. This file therefore
 * exercises the encoder at res 19 (the D1-G1 gate) where i/j/k stays
 * comfortably within int32 (~2.16e8 max). The all-res-up-to-22 sweep is
 * deferred until coordijk is widened.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "constants.h"
#include "h3Index.h"
#include "h3api.h"
#include "test.h"
#include "vec3d.h"

// Deterministic LCG (Numerical Recipes constants) for reproducible coverage.
static uint64_t lcg_next(uint64_t *state) {
    *state = (*state) * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

// Pseudo-random latitude in (-pi/2, pi/2), longitude in (-pi, pi).
static LatLng deterministic_latlng(uint64_t *lcg) {
    double u = (double)(lcg_next(lcg) >> 11) / (double)(1ULL << 53);
    double v = (double)(lcg_next(lcg) >> 11) / (double)(1ULL << 53);
    LatLng g;
    g.lat = (u - 0.5) * (M_PI - 1e-9);
    g.lng = (v - 0.5) * (2.0 * M_PI - 1e-9);
    return g;
}

SUITE(encoderExt) {
    // D1-G1: latLngToCell at res 19 for 100 deterministic coordinates →
    // ext cells with bit 64 set, effective res = 19, mode = H3_CELL_MODE.
    TEST(d1g1_latLngToCell_res19_extFlagSet) {
        uint64_t lcg = 0xD1A1B0F2C3D4E5F6ULL;
        for (int i = 0; i < 100; i++) {
            LatLng g = deterministic_latlng(&lcg);
            H3Index h = 0;
            t_assertSuccess(H3_EXPORT(latLngToCell)(&g, 19, &h));
            t_assert(H3_GET_EXT_FLAG(h) == 1,
                     "ext flag must be set for res-19 cell");
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(h) == 19,
                     "effective res must be 19");
            t_assert(H3_GET_MODE(h) == H3_CELL_MODE,
                     "cell mode must be H3_CELL_MODE");
            t_assert(H3_GET_BASE_CELL(h) >= 0 &&
                         H3_GET_BASE_CELL(h) < NUM_BASE_CELLS,
                     "base cell must be in [0, NUM_BASE_CELLS)");
        }
    }

    // D1 stock byte-identity: res 0-15 must keep high 64 bits zero. Stock
    // coordijk arithmetic stays well within int32 across res 0-15.
    TEST(d1_latLngToCell_stockResHighHalfZero) {
        uint64_t lcg = 0x57DCBA7E1DEFAB17ULL;
        for (int res = 0; res <= MAX_H3_RES; res++) {
            for (int i = 0; i < 5; i++) {
                LatLng g = deterministic_latlng(&lcg);
                H3Index h = 0;
                t_assertSuccess(H3_EXPORT(latLngToCell)(&g, res, &h));
                t_assert(H3_GET_EXT_FLAG(h) == 0,
                         "ext flag must be clear for stock res");
                t_assert((uint64_t)(h >> 64) == 0,
                         "high 64 bits must be zero for stock cell");
                t_assert(H3_GET_EFFECTIVE_RESOLUTION(h) == res,
                         "effective res equals stock res");
            }
        }
    }

    // D1 vec3ToCell: parallel path to latLngToCell — must produce identical
    // ext cells when given the corresponding vec3.
    TEST(d1_vec3ToCell_matchesLatLngToCell) {
        uint64_t lcg = 0xC0DECAFEBABEFADEULL;
        for (int i = 0; i < 50; i++) {
            LatLng g = deterministic_latlng(&lcg);
            H3Index hLatLng = 0, hVec = 0;
            t_assertSuccess(H3_EXPORT(latLngToCell)(&g, 19, &hLatLng));
            Vec3d v = latLngToVec3(g);
            t_assertSuccess(vec3ToCell(&v, 19, &hVec));
            t_assert(hLatLng == hVec,
                     "vec3ToCell must match latLngToCell at res 19");
            t_assert(H3_GET_EXT_FLAG(hVec) == 1, "vec3ToCell sets ext flag");
        }
    }

    // D1 setH3Index: ext-res init produces well-formed cell — bit 64 set,
    // stock-res field correct, ext digits written, digits past res = 7.
    // No geometric path; pure bit ops, safe at all ext resolutions.
    TEST(d1_setH3Index_extResWellFormed) {
        for (int res = MAX_H3_RES + 1; res <= MAX_H3_EXT_RES; res++) {
            for (int baseCell = 0; baseCell < 5; baseCell++) {
                for (int digit = CENTER_DIGIT; digit < INVALID_DIGIT; digit++) {
                    H3Index h = 0;
                    setH3Index(&h, res, baseCell, (Direction)digit);
                    t_assert(H3_GET_EXT_FLAG(h) == 1, "ext flag set");
                    t_assert(H3_GET_EFFECTIVE_RESOLUTION(h) == res,
                             "effective res matches");
                    t_assert(H3_GET_BASE_CELL(h) == baseCell,
                             "base cell preserved");
                    t_assert(H3_GET_MODE(h) == H3_CELL_MODE, "cell mode");
                    for (int r = 1; r <= res; r++) {
                        t_assert(H3_GET_DIGIT_AT_RES(h, r) == digit,
                                 "digit r in [1, res] equals initDigit");
                    }
                    for (int r = res + 1; r <= MAX_H3_EXT_RES; r++) {
                        t_assert(H3_GET_DIGIT_AT_RES(h, r) == INVALID_DIGIT,
                                 "digit r > res equals sentinel 7");
                    }
                }
            }
        }
    }

    // D1 setH3Index stock: byte-identical to original (high half zero).
    TEST(d1_setH3Index_stockResByteIdentical) {
        for (int res = 0; res <= MAX_H3_RES; res++) {
            for (int baseCell = 0; baseCell < 3; baseCell++) {
                H3Index h = 0;
                setH3Index(&h, res, baseCell, CENTER_DIGIT);
                t_assert(H3_GET_EXT_FLAG(h) == 0, "stock: ext flag clear");
                t_assert((uint64_t)(h >> 64) == 0, "stock: high half zero");
                t_assert(H3_GET_RESOLUTION(h) == res, "stock res matches");
                t_assert(H3_GET_BASE_CELL(h) == baseCell, "base cell");
                for (int r = 1; r <= res; r++) {
                    t_assert(H3_GET_DIGIT_AT_RES(h, r) == CENTER_DIGIT,
                             "digit equals initDigit");
                }
                for (int r = res + 1; r <= MAX_H3_RES; r++) {
                    t_assert(H3_GET_DIGIT_AT_RES(h, r) == INVALID_DIGIT,
                             "digit past res = sentinel 7 (from H3_INIT)");
                }
            }
        }
    }

    // D1 contract: res > MAX_H3_EXT_RES still rejected.
    TEST(d1_latLngToCell_aboveMaxExtRejected) {
        LatLng g = {0.5, -1.3};
        H3Index h = 0;
        t_assert(H3_EXPORT(latLngToCell)(&g, MAX_H3_EXT_RES + 1, &h) ==
                     E_RES_DOMAIN,
                 "res 23 rejected");
        t_assert(H3_EXPORT(latLngToCell)(&g, -1, &h) == E_RES_DOMAIN,
                 "res -1 rejected");
        Vec3d v = {1.0, 0.0, 0.0};
        t_assert(vec3ToCell(&v, MAX_H3_EXT_RES + 1, &h) == E_RES_DOMAIN,
                 "vec3 res 23 rejected");
    }
}
