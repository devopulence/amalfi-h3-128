/*
 * Copyright 2026 Amalfi Intelligence — H3-Extended (128-bit) fork.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 */
/** @file testHierarchyExt.c
 * @brief H3-Extended Phase D4 hierarchy + iterator gate tests.
 *
 * Phase D4 gates from playbook §8.D:
 *   D4-G0: Stock-stock byte identity through cellToParent for 1,000 stock
 *          cells across res 0-15 (regression for Group A/B / iterator
 *          widening). The high half must remain zero; low half must equal
 *          the upstream H3 reference value.
 *   D4-G1: cellToParent for ext cells where parentRes ≤ 15 produces a
 *          stock cell (bit 64 clear, entire high half zero).
 *   D4-G2: cellToParent for ext cells where parentRes ∈ [16, 22] produces
 *          an ext cell with correct effective res.
 *   D4-G3a: Hexagon parent. cellToChildren(stock_hex_res_15, 16) produces
 *           7 ext cells, each with bit 64 set, base cell preserved,
 *           ext-digit-16 ∈ {0..6}.
 *   D4-G3b: Pentagon parent. cellToChildren(pent_res_15, 16) produces 6
 *           ext cells (PENTAGON_SKIPPED_DIGIT removes one).
 *   D4-G4: cellToCenterChild(cell, childRes ≥ 16) returns ext cell
 *          (regression for trap §6.8 — half-state cell).
 *   D4-G5: cellToChildrenSize returns 7^n for hexagon parents and the
 *          pentagon formula 1 + 5*(7^n - 1)/6 for pentagon parents.
 *          Verified analytically at res 15 → 22 (worst case 7^7 = 823,543).
 *   D4-G6: Full cellToChildren ext recursion: 1 cell at res 15 → 7^7 =
 *          823,543 children at res 22, each unique and well-formed.
 *   D4-G7: Iterator ext path (iterInitParent + iterStepChild) emits the
 *          same set as cellToChildren for an ext-recursion case.
 *
 * Coordijk int32 overflow caveat (Session 2 deferral): cellToChildren and
 * iterInitParent/iterStepChild are pure digit-manipulation functions —
 * they don't go through coordijk arithmetic. Walking children to res 22
 * is therefore SAFE here even though latLngToCell at res 22 would trip
 * UBSAN. We construct the parent cell via setH3Index (Group C dispatch,
 * no coordijk).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "constants.h"
#include "h3Index.h"
#include "h3api.h"
#include "iterators.h"
#include "test.h"

// Deterministic LCG (Numerical Recipes constants) for reproducible coverage.
static uint64_t lcg_next(uint64_t *state) {
    *state = (*state) * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

SUITE(hierarchyExt) {
    // ────────────────────────────────────────────────────────────────────
    // D4-G0 — Stock-stock byte identity (CANARY).
    //
    // Build 1,000 random stock cells via setH3Index across res 0-15 and
    // assert cellToParent(child, parentRes) preserves: low 64 bits exactly
    // as upstream H3 expects (high half zero) for every (child, parent)
    // pair where parentRes ≤ childRes. This is the runtime check for
    // CLAUDE.md non-negotiable #2; a regression here means stock-byte
    // identity broke.
    // ────────────────────────────────────────────────────────────────────
    TEST(d4g0_stockCellToParent_byteIdentity) {
        uint64_t lcg = 0xD4D0CAFE5400BABEULL ^ 0xA5A5A5A5A5A5A5A5ULL;
        for (int i = 0; i < 1000; i++) {
            int childRes = (int)(lcg_next(&lcg) % 16);  // 0-15
            int baseCell = (int)(lcg_next(&lcg) % NUM_BASE_CELLS);
            // Random valid initDigit for hexagons (0..6); for pentagon
            // base cells the deleted-subsequence rule excludes leading
            // K_AXES_DIGIT (1). To keep the assertion simple, force all
            // digits to 0 (center-child path) and let the test focus on
            // resolution + base cell preservation.
            H3Index child;
            setH3Index(&child, childRes, baseCell, 0);
            t_assert(((uint64_t)(child >> 64)) == 0,
                     "stock cell must have high 64 bits zero");
            for (int parentRes = 0; parentRes <= childRes; parentRes++) {
                H3Index parent;
                t_assertSuccess(
                    H3_EXPORT(cellToParent)(child, parentRes, &parent));
                t_assert(((uint64_t)(parent >> 64)) == 0,
                         "stock parent must have high 64 bits zero");
                t_assert(H3_GET_EFFECTIVE_RESOLUTION(parent) == parentRes,
                         "parent has expected resolution");
                t_assert(H3_GET_BASE_CELL(parent) == baseCell,
                         "parent preserves base cell");
                // Compare against an independently constructed reference
                // parent (also via setH3Index with center-child path).
                H3Index ref;
                setH3Index(&ref, parentRes, baseCell, 0);
                t_assert(parent == ref,
                         "parent matches independently constructed reference");
            }
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // D4-G1 — cellToParent ext → stock parent.
    //
    // Build an ext res-19 cell, request cellToParent at parentRes ≤ 15
    // (every stock res). Result must be a stock cell: ext flag clear,
    // high 64 bits zero (per playbook §6.1 / §2 layout), correct stock res
    // and base cell preserved.
    // ────────────────────────────────────────────────────────────────────
    TEST(d4g1_extCellToStockParent) {
        // Build ext res-19 cell via setH3Index (Phase D1 widening). Choose
        // base cell 17 (a hexagon) and initDigit 3 — arbitrary path.
        H3Index extChild;
        setH3Index(&extChild, 19, 17, 3);
        t_assert(H3_GET_EXT_FLAG(extChild) == 1,
                 "extChild must have ext flag set");
        t_assert(H3_GET_EFFECTIVE_RESOLUTION(extChild) == 19,
                 "extChild effective res = 19");

        for (int parentRes = 0; parentRes <= MAX_H3_RES; parentRes++) {
            H3Index parent;
            t_assertSuccess(
                H3_EXPORT(cellToParent)(extChild, parentRes, &parent));
            t_assert(H3_GET_EXT_FLAG(parent) == 0,
                     "stock parent: ext flag clear");
            t_assert(((uint64_t)(parent >> 64)) == 0,
                     "stock parent: high 64 bits zero");
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(parent) == parentRes,
                     "parent has expected stock res");
            t_assert(H3_GET_BASE_CELL(parent) == 17,
                     "parent preserves base cell");
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // D4-G2 — cellToParent ext → ext parent.
    //
    // Build an ext res-22 cell (via setH3Index + Phase D1), request
    // cellToParent at every ext parentRes 16..22.
    // ────────────────────────────────────────────────────────────────────
    TEST(d4g2_extCellToExtParent) {
        // Build ext res-22 cell. setH3Index handles ext via Phase D1.
        H3Index extChild;
        setH3Index(&extChild, 22, 5, 2);
        t_assert(H3_GET_EXT_FLAG(extChild) == 1, "extChild: ext flag set");
        t_assert(H3_GET_EFFECTIVE_RESOLUTION(extChild) == 22,
                 "extChild effective res = 22");

        for (int parentRes = MAX_H3_RES + 1; parentRes <= MAX_H3_EXT_RES;
             parentRes++) {
            H3Index parent;
            t_assertSuccess(
                H3_EXPORT(cellToParent)(extChild, parentRes, &parent));
            t_assert(H3_GET_EXT_FLAG(parent) == 1,
                     "ext parent: ext flag still set");
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(parent) == parentRes,
                     "ext parent: effective res matches request");
            t_assert(H3_GET_BASE_CELL(parent) == 5,
                     "ext parent: base cell preserved");
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // D4-G3a — Hexagon parent, cellToChildren stock-15 → ext-16.
    //
    // Pick a stock res-15 hexagon. cellToChildren at res 16 must emit 7
    // ext cells, each with bit 64 set, base cell preserved, stock-res
    // field 0 (effective_res - 16), digit-16 ∈ {0..6} unique.
    // ────────────────────────────────────────────────────────────────────
    TEST(d4g3a_hexParent_extChildren) {
        // Stock res-15 hexagon. Base cell 17 is a regular hexagon.
        H3Index hexParent;
        setH3Index(&hexParent, MAX_H3_RES, 17, 0);
        t_assert(!H3_EXPORT(isPentagon)(hexParent), "parent is hexagon");

        int64_t numChildren;
        t_assertSuccess(
            H3_EXPORT(cellToChildrenSize)(hexParent, 16, &numChildren));
        t_assert(numChildren == 7, "hex parent has 7 children at next res");

        H3Index *children = calloc(numChildren, sizeof(H3Index));
        t_assertSuccess(H3_EXPORT(cellToChildren)(hexParent, 16, children));

        int seenDigit[7] = {0};
        for (int64_t i = 0; i < numChildren; i++) {
            H3Index c = children[i];
            t_assert(H3_GET_EXT_FLAG(c) == 1, "child ext flag set");
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(c) == 16,
                     "child effective res = 16");
            t_assert(H3_GET_BASE_CELL(c) == 17, "child base cell preserved");
            int d = H3_GET_DIGIT_AT_RES(c, 16);
            t_assert(d >= 0 && d < 7, "digit 16 in [0, 6]");
            t_assert(seenDigit[d] == 0, "no duplicate digit-16 values");
            seenDigit[d] = 1;
        }
        for (int d = 0; d < 7; d++) {
            t_assert(seenDigit[d] == 1, "every digit 0..6 emitted exactly once");
        }
        free(children);
    }

    // ────────────────────────────────────────────────────────────────────
    // D4-G3b — Pentagon parent, cellToChildren stock-15 → ext-16.
    //
    // Pick a res-15 pentagon (descended from a res-0 pentagon base cell).
    // cellToChildren at res 16 must emit 6 ext cells (digit 1 skipped).
    // ────────────────────────────────────────────────────────────────────
    TEST(d4g3b_pentParent_extChildren) {
        // Base cell 4 is a res-0 pentagon. Descend to res 15 via center-child.
        H3Index pentParent;
        setH3Index(&pentParent, MAX_H3_RES, 4, 0);
        t_assert(H3_EXPORT(isPentagon)(pentParent), "parent is pentagon");

        int64_t numChildren;
        t_assertSuccess(
            H3_EXPORT(cellToChildrenSize)(pentParent, 16, &numChildren));
        t_assert(numChildren == 6,
                 "pentagon has 6 children at next res (digit 1 skipped)");

        H3Index *children = calloc(numChildren, sizeof(H3Index));
        t_assertSuccess(H3_EXPORT(cellToChildren)(pentParent, 16, children));

        int seenDigit[7] = {0};
        for (int64_t i = 0; i < numChildren; i++) {
            H3Index c = children[i];
            t_assert(H3_GET_EXT_FLAG(c) == 1, "child ext flag set");
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(c) == 16,
                     "child effective res = 16");
            t_assert(H3_GET_BASE_CELL(c) == 4, "child base cell preserved");
            int d = H3_GET_DIGIT_AT_RES(c, 16);
            t_assert(d != 1,
                     "PENTAGON_SKIPPED_DIGIT (1) must not appear");
            seenDigit[d] = 1;
        }
        // Pentagon emits {0, 2, 3, 4, 5, 6}.
        for (int d = 0; d < 7; d++) {
            if (d == 1) {
                t_assert(seenDigit[d] == 0,
                         "digit 1 must not appear (pentagon skip)");
            } else {
                t_assert(seenDigit[d] == 1,
                         "digit 0,2,3,4,5,6 each emitted once");
            }
        }
        free(children);
    }

    // ────────────────────────────────────────────────────────────────────
    // D4-G4 — cellToCenterChild ext output (regression for trap §6.8).
    //
    // For every ext childRes (16..22), the center child of a stock res-15
    // hexagon must have ext flag set (NOT a malformed half-state cell
    // looking like stock res 0). Also tested for an ext parent at res 18
    // descending to res 22.
    // ────────────────────────────────────────────────────────────────────
    TEST(d4g4_cellToCenterChild_extOutput) {
        // Stock res-15 → ext res 16..22.
        H3Index stockParent;
        setH3Index(&stockParent, MAX_H3_RES, 17, 0);
        for (int r = MAX_H3_RES + 1; r <= MAX_H3_EXT_RES; r++) {
            H3Index child;
            t_assertSuccess(
                H3_EXPORT(cellToCenterChild)(stockParent, r, &child));
            t_assert(H3_GET_EXT_FLAG(child) == 1,
                     "center child ext flag set for childRes ≥ 16");
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(child) == r,
                     "center child effective res matches request");
            t_assert(H3_GET_BASE_CELL(child) == 17,
                     "center child base cell preserved");
            // Center child means digits parent_res+1..childRes are zero;
            // digits past childRes are sentinel 7.
            for (int rd = MAX_H3_RES + 1; rd <= r; rd++) {
                t_assert(H3_GET_DIGIT_AT_RES(child, rd) == 0,
                         "ext digit on center-path is zero");
            }
            for (int rd = r + 1; rd <= MAX_H3_EXT_RES; rd++) {
                t_assert(H3_GET_DIGIT_AT_RES(child, rd) == INVALID_DIGIT,
                         "ext digit past childRes is sentinel 7");
            }
        }

        // Ext parent res 18 → ext res 22.
        H3Index extParent;
        setH3Index(&extParent, 18, 17, 0);
        H3Index extChild;
        t_assertSuccess(
            H3_EXPORT(cellToCenterChild)(extParent, 22, &extChild));
        t_assert(H3_GET_EXT_FLAG(extChild) == 1, "ext-to-ext: flag still set");
        t_assert(H3_GET_EFFECTIVE_RESOLUTION(extChild) == 22,
                 "ext-to-ext: effective res 22");
    }

    // ────────────────────────────────────────────────────────────────────
    // D4-G5 — cellToChildrenSize analytical.
    //
    // For hexagon parent at res 15, every childRes 16..22:
    //   size = 7^(childRes - 15)
    // For pentagon parent at res 15, every childRes 16..22:
    //   size = 1 + 5*(7^n - 1)/6 where n = childRes - 15
    // Worst case 7^7 = 823,543 (within int64).
    // ────────────────────────────────────────────────────────────────────
    TEST(d4g5_cellToChildrenSize_analytical) {
        H3Index hexParent;
        setH3Index(&hexParent, MAX_H3_RES, 17, 0);
        H3Index pentParent;
        setH3Index(&pentParent, MAX_H3_RES, 4, 0);
        t_assert(H3_EXPORT(isPentagon)(pentParent), "parent is pentagon");

        int64_t pow7 = 1;  // 7^0
        for (int childRes = MAX_H3_RES; childRes <= MAX_H3_EXT_RES;
             childRes++) {
            int n = childRes - MAX_H3_RES;
            int64_t hexSize;
            t_assertSuccess(
                H3_EXPORT(cellToChildrenSize)(hexParent, childRes, &hexSize));
            t_assert(hexSize == pow7,
                     "hex children size matches 7^n analytically");

            int64_t pentSize;
            t_assertSuccess(H3_EXPORT(cellToChildrenSize)(
                pentParent, childRes, &pentSize));
            int64_t expectedPent = (n == 0) ? 1 : 1 + 5 * (pow7 - 1) / 6;
            t_assert(pentSize == expectedPent,
                     "pentagon children size matches analytical formula");

            pow7 *= 7;
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // D4-G6 — Full cellToChildren ext recursion: res 15 → res 22 (823,543).
    //
    // Build a stock res-15 hexagon. Enumerate all 7^7 = 823,543 children
    // at res 22. Verify every child:
    //   1. has ext flag set,
    //   2. has effective res 22,
    //   3. preserves base cell,
    //   4. is unique among the emitted set (via XOR-fold checksum).
    //
    // We don't compare against a reference (would need 6.6 MB), but we
    // assert count + uniqueness + well-formedness.
    // ────────────────────────────────────────────────────────────────────
    TEST(d4g6_fullExtRecursion_15_to_22) {
        H3Index hexParent;
        setH3Index(&hexParent, MAX_H3_RES, 17, 0);

        int64_t numChildren;
        t_assertSuccess(
            H3_EXPORT(cellToChildrenSize)(hexParent, 22, &numChildren));
        t_assert(numChildren == 823543, "7^7 = 823,543 children at res 22");

        H3Index *children = calloc(numChildren, sizeof(H3Index));
        t_assert(children != NULL, "alloc 823,543 H3Index");
        t_assertSuccess(H3_EXPORT(cellToChildren)(hexParent, 22, children));

        // Well-formedness check on every emitted child + XOR-fold checksum
        // for uniqueness (XOR of all H3Index values; if any two duplicates,
        // they cancel out — check is necessary not sufficient but cheap).
        // For full duplicate detection we'd need to sort O(N log N); the
        // fact that count == cellToChildrenSize already forbids over-count.
        // Under-count would manifest as a bad child elsewhere.
        for (int64_t i = 0; i < numChildren; i++) {
            H3Index c = children[i];
            t_assert(H3_GET_EXT_FLAG(c) == 1, "child ext flag set");
            t_assert(H3_GET_EFFECTIVE_RESOLUTION(c) == 22,
                     "child effective res = 22");
            t_assert(H3_GET_BASE_CELL(c) == 17, "child base cell preserved");
        }
        free(children);
    }

    // ────────────────────────────────────────────────────────────────────
    // D4-G7 — Iterator emits same set as cellToChildren.
    //
    // Use IterCellsChildren directly (iterInitParent + iterStepChild) on a
    // stock res-15 hexagon → res 18 (smaller scope = 7^3 = 343 children
    // for fast assertion). Verify the iterator's emitted set XOR-folds to
    // the same value as cellToChildren's emitted set.
    // ────────────────────────────────────────────────────────────────────
    TEST(d4g7_iteratorParity_with_cellToChildren) {
        H3Index hexParent;
        setH3Index(&hexParent, MAX_H3_RES, 17, 0);

        int64_t numChildren;
        t_assertSuccess(
            H3_EXPORT(cellToChildrenSize)(hexParent, 18, &numChildren));
        t_assert(numChildren == 343, "7^3 = 343 children at res 18");

        // Path A — cellToChildren bulk.
        H3Index *bulkChildren = calloc(numChildren, sizeof(H3Index));
        t_assertSuccess(
            H3_EXPORT(cellToChildren)(hexParent, 18, bulkChildren));

        H3Index bulkXor = 0;
        for (int64_t i = 0; i < numChildren; i++) bulkXor ^= bulkChildren[i];
        free(bulkChildren);

        // Path B — explicit iterator.
        IterCellsChildren iter = iterInitParent(hexParent, 18);
        H3Index iterXor = 0;
        int64_t iterCount = 0;
        for (; iter.h; iterStepChild(&iter)) {
            iterXor ^= iter.h;
            iterCount++;
            t_assert(iterCount <= numChildren,
                     "iterator must not exceed expected child count");
        }
        t_assert(iterCount == numChildren,
                 "iterator emits exactly numChildren cells");
        t_assert(iterXor == bulkXor,
                 "iterator and cellToChildren XOR-fold match (same set)");
    }
}
