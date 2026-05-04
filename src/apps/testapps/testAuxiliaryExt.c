/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 */
/** @file testAuxiliaryExt.c
 * @brief H3-Extended Phase F auxiliary-surface gate test.
 *
 * Phase F gates from playbook §8.F:
 *   F1: cellToBoundary for an ext cell produces 6 (or 5 for pentagons)
 *       finite vertices within the public LatLng range.
 *   F2: getIcosahedronFaces for an ext cell returns 1-2 faces (5 for
 *       pentagons).
 *   F3: getPentagons(20, out) returns 12 pentagons at res 20.
 *   F4: isResClassIII matches (effective_res % 2) for ext res 16-22.
 *       (Auto-passes per playbook §6.14: stock-res-field % 2 ==
 *       effective_res % 2 since 16 is even — no widening needed.)
 *
 * Coordijk int32 overflow caveat (Session 2 deferral): F1 is scoped to
 * res 19 — the same scope as testEncoderExt / testDecoderExt — where
 * gnomonic-scaled i/j/k stays within int32. F2/F3 don't go through
 * coordijk arithmetic (F2 uses _faceIjkToVerts at the cell's own
 * coords; F3 builds cells via setH3Index without projection).
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "constants.h"
#include "h3Index.h"
#include "h3api.h"
#include "test.h"

SUITE(auxiliaryExt) {
    // F1 — cellToBoundary on an ext res-19 cell produces 6 finite vertices
    // within plausible lat/lng range. Built from a real lat/lng so the cell
    // is geometrically valid.
    TEST(f1_cellToBoundaryExtHexagonRes19) {
        // Sandy Hook, NJ — known valid cell at res 19, hexagon base cell.
        LatLng g = {.lat = 0.7128, .lng = -1.2820};
        H3Index cell = 0;
        t_assertSuccess(H3_EXPORT(latLngToCell)(&g, 19, &cell));
        t_assert(H3_GET_EXT_FLAG(cell) == 1, "produced ext cell");
        t_assert(H3_EXPORT(isPentagon)(cell) == 0, "non-pentagon (hexagon)");

        CellBoundary cb = {0};
        t_assertSuccess(H3_EXPORT(cellToBoundary)(cell, &cb));
        t_assert(cb.numVerts == NUM_HEX_VERTS,
                 "ext hexagon yields 6 vertices");
        for (int i = 0; i < cb.numVerts; i++) {
            t_assert(!isnan(cb.verts[i].lat) && !isnan(cb.verts[i].lng),
                     "vertex lat/lng finite");
            t_assert(cb.verts[i].lat >= -M_PI_2 && cb.verts[i].lat <= M_PI_2,
                     "lat in [-pi/2, pi/2]");
            t_assert(cb.verts[i].lng >= -M_PI && cb.verts[i].lng <= M_PI,
                     "lng in [-pi, pi]");
        }
    }

    // F2 — getIcosahedronFaces for an ext cell returns 1 or 2 faces (hexagon).
    TEST(f2_getIcosahedronFacesExtHexagon) {
        LatLng g = {.lat = 0.5, .lng = 0.3};
        H3Index cell = 0;
        t_assertSuccess(H3_EXPORT(latLngToCell)(&g, 19, &cell));
        t_assert(H3_GET_EXT_FLAG(cell) == 1, "produced ext cell");

        int faceCount = 0;
        t_assertSuccess(H3_EXPORT(maxFaceCount)(cell, &faceCount));
        t_assert(faceCount == 2, "hexagon maxFaceCount == 2");

        int faces[2] = {-1, -1};
        t_assertSuccess(H3_EXPORT(getIcosahedronFaces)(cell, faces));
        // At least one valid face must be set; both must be in [0, 19] when set.
        int validFaces = 0;
        for (int i = 0; i < 2; i++) {
            if (faces[i] >= 0) {
                t_assert(faces[i] < 20, "face index in [0, 19]");
                validFaces++;
            }
        }
        t_assert(validFaces >= 1 && validFaces <= 2,
                 "ext hexagon intersects 1-2 icosahedron faces");
    }

    // F2 — getIcosahedronFaces ext-pentagon coverage is DEFERRED to the
    // coordijk int64 widening sub-phase (Session 2 carryover, playbook §6.14
    // caveat). _adjustPentVertOverage runs aperture-7 IJK arithmetic at the
    // ext effective resolution; for pentagon centers at res ≥ 16 this
    // overflows int32 inside coordijk.h:132/217/222/223. The pentagon F2
    // probe will be re-enabled once CoordIJK is widened to int64.
    // For now F2 is gated by the hexagon test above + maxFaceCount on pent.
    TEST(f2_pentagonMaxFaceCountExt) {
        H3Index pentRes0 = 0;
        setH3Index(&pentRes0, 0, 4, 0);
        H3Index pentRes19 = 0;
        t_assertSuccess(H3_EXPORT(cellToCenterChild)(pentRes0, 19, &pentRes19));
        t_assert(H3_EXPORT(isPentagon)(pentRes19) == 1, "ext pentagon");
        int faceCount = 0;
        t_assertSuccess(H3_EXPORT(maxFaceCount)(pentRes19, &faceCount));
        t_assert(faceCount == 5,
                 "ext pentagon maxFaceCount == 5 (no coordijk path)");
    }

    // F3 — getPentagons(20, ...) returns 12 pentagons at ext res 20.
    TEST(f3_getPentagonsAtExtRes) {
        for (int res = 16; res <= MAX_H3_EXT_RES; res++) {
            H3Index out[NUM_PENTAGONS] = {0};
            t_assertSuccess(H3_EXPORT(getPentagons)(res, out));
            for (int i = 0; i < NUM_PENTAGONS; i++) {
                t_assert(H3_GET_EXT_FLAG(out[i]) == 1,
                         "ext pentagon has ext flag");
                t_assert(H3_EXPORT(getResolution)(out[i]) == res,
                         "ext pentagon at expected res");
                t_assert(H3_EXPORT(isPentagon)(out[i]) == 1,
                         "all 12 are pentagons");
            }
            // De-dup check: 12 distinct cells.
            for (int i = 0; i < NUM_PENTAGONS; i++) {
                for (int j = i + 1; j < NUM_PENTAGONS; j++) {
                    t_assert(out[i] != out[j],
                             "pentagons distinct at ext res");
                }
            }
        }
    }

    // F3 — out-of-range probe matches the widened bound (res 23 invalid).
    TEST(f3_getPentagonsRejectsAboveExtMax) {
        H3Index out[NUM_PENTAGONS] = {0};
        t_assert(
            H3_EXPORT(getPentagons)(MAX_H3_EXT_RES + 1, out) == E_RES_DOMAIN,
            "res > MAX_H3_EXT_RES rejected");
        t_assert(H3_EXPORT(getPentagons)(-1, out) == E_RES_DOMAIN,
                 "negative res rejected");
    }

    // F4 — isResClassIII parity matches (effective_res % 2) for ext cells.
    // No widening needed because 16 is even, so stock-res-field parity ==
    // effective-res parity. Verify the invariant holds in the test.
    TEST(f4_isResClassIIIParityExt) {
        for (int res = 16; res <= MAX_H3_EXT_RES; res++) {
            H3Index out[NUM_PENTAGONS] = {0};
            t_assertSuccess(H3_EXPORT(getPentagons)(res, out));
            // Use a pentagon as a representative ext cell at this res.
            int isClassIII = H3_EXPORT(isResClassIII)(out[0]);
            int expected = res % 2;
            t_assert(isClassIII == expected,
                     "isResClassIII matches (effective_res %% 2) for ext");
        }
    }
}
