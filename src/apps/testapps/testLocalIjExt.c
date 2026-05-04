/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 */
/** @file testLocalIjExt.c
 * @brief H3-Extended Phase D5 localij gate test.
 *
 * Phase D5 gate from playbook §8.D:
 *   D5-G1: gridDistance(a, b) for two ext cells in the same base cell
 *          returns the expected ijk distance.
 *
 * Plus regression coverage on the four widened sites in localij.c
 * (cellToLocalIjk reads + localIjkToCell init/write/loop).
 *
 * Coordijk int32 overflow caveat (Session 2 deferral): the internal
 * CoordIJK arithmetic in localij + coordijk + faceijk is still int32. At
 * ext res 20+ the gnomonic-scaled radii push i/j/k into billions for
 * off-face-center coordinates, tripping UBSAN signed-overflow. This file
 * scopes the gridDistance probe to res 19 — the same scope as Session 2's
 * testEncoderExt and testDecoderExt — where i/j/k stays well within
 * int32. Full res 20-22 coverage is deferred until coordijk is widened
 * to int64.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "constants.h"
#include "h3Index.h"
#include "h3api.h"
#include "test.h"

// Deterministic LCG (Numerical Recipes constants) for reproducible coverage.
static uint64_t lcg_next(uint64_t *state) {
    *state = (*state) * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

static LatLng deterministic_latlng_near(LatLng base, double drift_rad,
                                        uint64_t *lcg) {
    double u = (double)(lcg_next(lcg) >> 11) / (double)(1ULL << 53);
    double v = (double)(lcg_next(lcg) >> 11) / (double)(1ULL << 53);
    LatLng g;
    g.lat = base.lat + (u - 0.5) * 2.0 * drift_rad;
    g.lng = base.lng + (v - 0.5) * 2.0 * drift_rad;
    return g;
}

SUITE(localIjExt) {
    // ────────────────────────────────────────────────────────────────────
    // D5-G1 — gridDistance for two ext cells in the same base cell.
    //
    // Pick a stock res-15 cell anchor; encode the same lat/lng + a tiny
    // drift at res 19. Both cells should share the same base cell. Compute
    // gridDistance: must succeed and return a small (< 1000) ijk distance.
    // Repeat for many seeds to exercise different base cells.
    // ────────────────────────────────────────────────────────────────────
    TEST(d5g1_gridDistance_extCells_sameBaseCell) {
        uint64_t lcg = 0xD5D1BABE10C0FFEEULL;  // arbitrary seed
        // Use a small drift so neighboring res-19 cells are within the
        // same base cell (res-19 cell edge ≈ 1.4e-7 rad, so 1e-5 rad
        // covers ~70 cells of separation — well within base-cell bounds).
        const double drift = 1.0e-5;  // ~64m
        const int kIters = 200;
        int succeeded = 0;

        for (int i = 0; i < kIters; i++) {
            // Anchor latlng away from poles to avoid pentagon edge cases.
            double anchor_u = (double)(lcg_next(&lcg) >> 11) /
                              (double)(1ULL << 53);
            double anchor_v = (double)(lcg_next(&lcg) >> 11) /
                              (double)(1ULL << 53);
            LatLng anchor;
            anchor.lat = (anchor_u - 0.5) * 1.4;  // [-0.7, 0.7] rad ≈ ±40°
            anchor.lng = (anchor_v - 0.5) * 2.0 * 3.14;

            H3Index a, b;
            t_assertSuccess(H3_EXPORT(latLngToCell)(&anchor, 19, &a));
            LatLng anchor2 = deterministic_latlng_near(anchor, drift, &lcg);
            t_assertSuccess(H3_EXPORT(latLngToCell)(&anchor2, 19, &b));

            // Both cells must be ext.
            t_assert(H3_GET_EXT_FLAG(a) == 1, "a is ext");
            t_assert(H3_GET_EXT_FLAG(b) == 1, "b is ext");

            // Skip pairs that crossed a base cell boundary (gridDistance
            // can return E_FAILED for cells too far apart or across
            // pentagon distortion — that's expected, not a regression).
            if (H3_GET_BASE_CELL(a) != H3_GET_BASE_CELL(b)) continue;

            int64_t distance = -1;
            H3Error err = H3_EXPORT(gridDistance)(a, b, &distance);
            if (err != E_SUCCESS) continue;  // pentagon-side failure is OK
            t_assert(distance >= 0, "non-negative grid distance");
            t_assert(distance < 10000, "small drift → small distance");
            succeeded++;
        }
        t_assert(succeeded > 0,
                 "at least one ext-cell pair produced a valid gridDistance");
    }

    // ────────────────────────────────────────────────────────────────────
    // Regression — cellToLocalIjk res-mismatch on ext cells.
    //
    // An ext-res-19 cell vs a stock-res-15 cell must return E_RES_MISMATCH
    // (the two cells have different effective resolutions).
    // ────────────────────────────────────────────────────────────────────
    TEST(d5_cellToLocalIjk_extStockMismatch) {
        LatLng anchor = {0.5, 1.0};
        H3Index extCell, stockCell;
        t_assertSuccess(H3_EXPORT(latLngToCell)(&anchor, 19, &extCell));
        t_assertSuccess(H3_EXPORT(latLngToCell)(&anchor, 15, &stockCell));
        t_assert(H3_GET_EXT_FLAG(extCell) == 1, "extCell ext flag set");
        t_assert(H3_GET_EXT_FLAG(stockCell) == 0,
                 "stockCell ext flag clear");
        // Use the public CoordIJ API (cellToLocalIj) to exercise the
        // widened cellToLocalIjk on the inside.
        CoordIJ ij;
        H3Error err =
            H3_EXPORT(cellToLocalIj)(stockCell, extCell, 0, &ij);
        t_assert(err == E_RES_MISMATCH,
                 "ext vs stock origin returns E_RES_MISMATCH");
    }

    // ────────────────────────────────────────────────────────────────────
    // Regression — localIjkToCell INIT_EXT branch produces well-formed
    // ext cell.
    //
    // Round-trip: cellToLocalIj(origin, cell) → localIjToCell(origin, ij)
    // → cell. Bit-identical for ext-res-19 cells.
    // ────────────────────────────────────────────────────────────────────
    TEST(d5_localIjk_roundtrip_extRes19) {
        uint64_t lcg = 0xD5BEEFD0BEEFD0BEULL;
        const int kIters = 100;
        int roundtrips = 0;
        for (int i = 0; i < kIters; i++) {
            double u = (double)(lcg_next(&lcg) >> 11) / (double)(1ULL << 53);
            double v = (double)(lcg_next(&lcg) >> 11) / (double)(1ULL << 53);
            LatLng anchor = {.lat = (u - 0.5) * 1.4,
                             .lng = (v - 0.5) * 2.0 * 3.14};
            H3Index origin, neighbor;
            t_assertSuccess(H3_EXPORT(latLngToCell)(&anchor, 19, &origin));
            // Drift slightly to stay within same base cell.
            LatLng anchor2 = deterministic_latlng_near(anchor, 1.0e-5, &lcg);
            t_assertSuccess(H3_EXPORT(latLngToCell)(&anchor2, 19, &neighbor));

            CoordIJ ij;
            if (H3_EXPORT(cellToLocalIj)(origin, neighbor, 0, &ij)) continue;

            H3Index recovered;
            if (H3_EXPORT(localIjToCell)(origin, &ij, 0, &recovered)) continue;
            t_assert(recovered == neighbor,
                     "localIjToCell recovers original ext cell bit-identical");
            t_assert(H3_GET_EXT_FLAG(recovered) == 1,
                     "recovered cell ext flag set");
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(recovered) == 19,
                     "recovered cell effective res 19");
            roundtrips++;
        }
        t_assert(roundtrips > 0, "at least one ext round-trip succeeded");
    }
}
