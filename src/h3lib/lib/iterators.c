/*
 * Copyright 2021 Uber Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** @file IterCellsChildren.c
 * @brief Iterator structs and functions for the children of a cell,
 * or cells at a given resolution.
 */

#include "iterators.h"

#include "h3Index.h"

// extract the `res` digit (0--7) of the current cell
// H3-EXTENDED: Rule DR (POC-3 I3) — dispatch via H3_GET_DIGIT_AT_RES so res
// values 16-22 read from the ext bits in the high half.
static int _getResDigit(IterCellsChildren *it, int res) {
    return H3_GET_DIGIT_AT_RES(it->h, res);
}

// increment the digit (0--7) at location `res`
// H3-EXTENDED: trap §6.10. Stock body computes `val <<= H3_PER_DIGIT_OFFSET *
// (MAX_H3_RES - res)`, which is a negative shift count when res > 15 — UB.
// POC-3 I2 pattern: read d, write d+1 via dispatching macros. Caller (the
// iterStepChild carry loop) explicitly zeros the overflowed digit before
// recursing; the bit-add carry semantic that the stock body relied on is
// replaced by that explicit zero+recurse.
static void _incrementResDigit(IterCellsChildren *it, int res) {
    int d = H3_GET_DIGIT_AT_RES(it->h, res);
    H3_SET_DIGIT_AT_RES(it->h, res, d + 1);
}

/**
 * Create a fully nulled-out child iterator for when an iterator is exhausted.
 * This helps minimize the chance that a user will depend on the iterator
 * internal state after it's exhausted, like the child resolution, for
 * example.
 */
static IterCellsChildren _null_iter(void) {
    return (IterCellsChildren){
        .h = H3_NULL, ._parentRes = -1, ._skipDigit = -1};
}

/**

## Logic for iterating through the children of a cell

We'll describe the logic for ....

- normal (non pentagon iteration)
- pentagon iteration. define "pentagon digit"


### Cell Index Component Diagrams

The lower 56 bits of an H3 Cell Index describe the following index components:

- the cell resolution (4 bits)
- the base cell number (7 bits)
- the child cell digit for each resolution from 1 to 15 (3*15 = 45 bits)

These are the bits we'll be focused on when iterating through child cells.
To help describe the iteration logic, we'll use diagrams displaying the
(decimal) values for each component like:

                            child digit for resolution 2
                           /
| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 | ... |
|-----|-------------|---|---|---|---|---|---|-----|
|   9 |          17 | 5 | 3 | 0 | 6 | 2 | 1 | ... |


### Iteration through children of a hexagon (but not a pentagon)

Iteration through the children of a *hexagon* (but not a pentagon)
simply involves iterating through all the children values (0--6)
for each child digit (up to the child's resolution).

For example, suppose a resolution 3 hexagon index has the following
components:
                                parent resolution
                               /
| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 | ... |
|-----|-------------|---|---|---|---|---|---|-----|
|   3 |          17 | 3 | 5 | 1 | 7 | 7 | 7 | ... |

The iteration through all children of resolution 6 would look like:


                                parent res  child res
                               /           /
| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | ... |
|-----|-------------|---|---|---|---|---|---|---|---|-----|
| 6   |          17 | 3 | 5 | 1 | 0 | 0 | 0 | 7 | 7 | ... |
| 6   |          17 | 3 | 5 | 1 | 0 | 0 | 1 | 7 | 7 | ... |
| ... |             |   |   |   |   |   |   |   |   |     |
| 6   |          17 | 3 | 5 | 1 | 0 | 0 | 6 | 7 | 7 | ... |
| 6   |          17 | 3 | 5 | 1 | 0 | 1 | 0 | 7 | 7 | ... |
| 6   |          17 | 3 | 5 | 1 | 0 | 1 | 1 | 7 | 7 | ... |
| ... |             |   |   |   |   |   |   |   |   |     |
| 6   |          17 | 3 | 5 | 1 | 6 | 6 | 6 | 7 | 7 | ... |


### Step sequence on a *pentagon* cell

Pentagon cells have a base cell number (e.g., 97) corresponding to a
resolution 0 pentagon, and have all zeros from digit 1 to the digit
corresponding to the cell's resolution.
(We'll drop the ellipses from now on, knowing that digits should contain
7's beyond the cell resolution.)

                            parent res      child res
                           /               /
| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|-------------|---|---|---|---|---|---|
|   6 |          97 | 0 | 0 | 0 | 0 | 0 | 0 |

Iteration through children of a *pentagon* is almost the same
as *hexagon* iteration, except that we skip the *first* 1 value
that appears in the "skip digit". This corresponds to the fact
that a pentagon only has 6 children, which are denoted with
the numbers {0,2,3,4,5,6}.

The skip digit starts at the child resolution position.
When iterating through children more than one resolution below
the parent, we move the skip digit to the left
(up to the next coarser resolution) each time we skip the 1 value
in that digit.

Iteration would start like:

                            parent res      child res
                           /               /
| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|-------------|---|---|---|---|---|---|
|   6 |          97 | 0 | 0 | 0 | 0 | 0 | 0 |
                                           \
                                            skip digit

Noticing we skip the 1 value and move the skip digit,
the next iterate would be:


| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|-------------|---|---|---|---|---|---|
|   6 |          97 | 0 | 0 | 0 | 0 | 0 | 2 |
                                       \
                                        skip digit

Iteration continues normally until we get to:


| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|-------------|---|---|---|---|---|---|
|   6 |          97 | 0 | 0 | 0 | 0 | 0 | 6 |
                                       \
                                        skip digit

which is followed by (skipping the 1):


| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|-------------|---|---|---|---|---|---|
|   6 |          97 | 0 | 0 | 0 | 0 | 2 | 0 |
                                   \
                                    skip digit

For the next iterate, we won't skip the `1` in the previous digit
because it is no longer the skip digit:

| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|-------------|---|---|---|---|---|---|
|   6 |          97 | 0 | 0 | 0 | 0 | 2 | 1 |
                                   \
                                    skip digit

Iteration continues normally until we're right before the next skip
digit:

| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|-------------|---|---|---|---|---|---|
|   6 |          97 | 0 | 0 | 0 | 0 | 6 | 6 |
                                   \
                                    skip digit

Which is followed by

| res | base cell # | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|-------------|---|---|---|---|---|---|
|   6 |          97 | 0 | 0 | 0 | 2 | 0 | 0 |
                               \
                                skip digit

and so on.

 */

/**
 * Initialize a IterCellsChildren struct representing the sequence giving
 * the children of cell `h` at resolution `childRes`.
 *
 * At any point in the iteration, starting once
 * the struct is initialized, IterCellsChildren.h gives the current child.
 *
 * Also, IterCellsChildren.h == H3_NULL when all the children have been iterated
 * through, or if the input to `iterInitParent` was invalid.
 */
IterCellsChildren iterInitParent(H3Index h, int childRes) {
    IterCellsChildren it;
    _iterInitParent(h, childRes, &it);
    return it;
}

/**
 * Internal function - initialize a parent iterator in-place
 */
void _iterInitParent(H3Index h, int childRes, IterCellsChildren *iter) {
    // H3-EXTENDED: Rule LB on _parentRes capture (POC-3 I4) — for ext parents
    // _parentRes must be the effective resolution, not the 4-bit stock-res
    // field; Rule GR on the upper-bound guard (MAX_H3_RES → MAX_H3_EXT_RES);
    // Rule RW on the resolution write so the ext flag is set atomically with
    // the stock-res field for ext childRes.
    iter->_parentRes = H3_GET_EFFECTIVE_RESOLUTION(h);

    if (childRes < iter->_parentRes || childRes > MAX_H3_EXT_RES ||
        h == H3_NULL) {
        *iter = _null_iter();
        return;
    }

    iter->h = _zeroIndexDigits(h, iter->_parentRes + 1, childRes);
    H3_SET_EFFECTIVE_RESOLUTION(iter->h, childRes);

    // H3-EXTENDED: when childRes is in the ext range, populate trailing
    // INVALID_DIGIT sentinels at digits childRes+1..MAX_H3_EXT_RES so the
    // emitted cell satisfies _hasAll7AfterRes. For stock parents the high
    // half is zero (no ext digits to clear); for ext parents, _zeroIndexDigits
    // only zeros up to childRes — digits childRes+1..22 retain the parent's
    // sentinels (already 7), which is also the desired state. The loop below
    // is therefore safe in both ext sub-cases (idempotent when sentinels are
    // already in place). For stock children (childRes ≤ 15) we MUST NOT
    // touch the high half — leave the stock-cell layout as it is.
    if (childRes > MAX_H3_RES) {
        for (int r = childRes + 1; r <= MAX_H3_EXT_RES; r++) {
            H3_SET_DIGIT_AT_RES(iter->h, r, INVALID_DIGIT);
        }
    }

    if (H3_EXPORT(isPentagon)(iter->h)) {
        // The skip digit skips `1` for pentagons.
        // The "_skipDigit" moves to the left as we count up from the
        // child resolution to the parent resolution.
        iter->_skipDigit = childRes;
    } else {
        // if not a pentagon, we can ignore "skip digit" logic
        iter->_skipDigit = -1;
    }
}

/**
 * Step a IterCellsChildren to the next child cell.
 * When the iteration is over, IterCellsChildren.h will be H3_NULL.
 * Handles iterating through hexagon and pentagon cells.
 */
void iterStepChild(IterCellsChildren *it) {
    // once h == H3_NULL, the iterator returns an infinite sequence of H3_NULL
    if (it->h == H3_NULL) return;

    // H3-EXTENDED: Rule LB on childRes capture; Pattern 3 (POC-3 I5) state
    // machine. The widened _incrementResDigit (POC-3 I2) is `set digit+1`
    // only — it does NOT do bit-add carry across digit boundaries (which
    // would not work cleanly across the stock/ext word boundary anyway).
    // Caller (this loop) performs explicit zero on overflow then recurses
    // with another _incrementResDigit on the next-higher digit. For stock
    // cells the produced enumeration is identical to the bit-add carry's
    // (verified by ctest stock 316 byte-identity).
    int childRes = H3_GET_EFFECTIVE_RESOLUTION(it->h);

    // H3-EXTENDED: when the iterator spans zero levels (childRes ==
    // _parentRes), the initial emission from _iterInitParent was the only
    // child (the parent itself). The stock loop happened to return null
    // immediately because the for-loop's first check is `i == _parentRes`;
    // our while-loop carries the carry-detection condition until after a
    // bump, so we need an explicit guard here. POC-3 I5 has the same guard
    // (poc3_iterator.c:236-240).
    if (childRes <= it->_parentRes) {
        *it = _null_iter();
        return;
    }

    int r = childRes;
    _incrementResDigit(it, r);

    while (1) {
        int d = _getResDigit(it, r);

        // PENTAGON_SKIPPED_DIGIT == 1
        if (r == it->_skipDigit && d == PENTAGON_SKIPPED_DIGIT) {
            // Iterating through the children of a pentagon cell — skip the
            // `1` digit at the current skip position. The skip position
            // moves up one digit each time we cross it via carry.
            _incrementResDigit(it, r);
            it->_skipDigit -= 1;
            return;
        }

        // d == valid digit (0..6) — current child found, return.
        if (d != INVALID_DIGIT) return;

        // d == INVALID_DIGIT (7): overflow at this digit. Carry to next-higher.
        if (r == it->_parentRes + 1) {
            // Carry would propagate past the iterator's domain — done.
            *it = _null_iter();
            return;
        }
        H3_SET_DIGIT_AT_RES(it->h, r, 0);
        r--;
        _incrementResDigit(it, r);
    }
}

// create iterator for children of base cell at given resolution
IterCellsChildren iterInitBaseCellNum(int baseCellNum, int childRes) {
    // H3-EXTENDED: Rule GR — relax the upper bound to MAX_H3_EXT_RES so that
    // base-cell-rooted iterators can descend into ext resolutions.
    if (baseCellNum < 0 || baseCellNum >= NUM_BASE_CELLS || childRes < 0 ||
        childRes > MAX_H3_EXT_RES) {
        return _null_iter();
    }

    H3Index baseCell;
    setH3Index(&baseCell, 0, baseCellNum, 0);

    return iterInitParent(baseCell, childRes);
}

// create iterator for all cells at given resolution
IterCellsResolution iterInitRes(int res) {
    IterCellsChildren itC = iterInitBaseCellNum(0, res);

    IterCellsResolution itR = {
        .h = itC.h, ._baseCellNum = 0, ._res = res, ._itC = itC};

    return itR;
}

void iterStepRes(IterCellsResolution *itR) {
    // reached the end of over iterator; emits H3_NULL from now on
    if (itR->h == H3_NULL) return;

    // step child iterator
    iterStepChild(&(itR->_itC));

    // If the child iterator is exhausted and there are still
    // base cells remaining, we initialize the next base cell child iterator
    if ((itR->_itC.h == H3_NULL) && (itR->_baseCellNum + 1 < NUM_BASE_CELLS)) {
        itR->_baseCellNum += 1;
        itR->_itC = iterInitBaseCellNum(itR->_baseCellNum, itR->_res);
    }

    // This overall iterator reflects the next cell in the child iterator.
    // Note: This sets itR->h = H3_NULL if the base cells were
    // exhausted in the check above.
    itR->h = itR->_itC.h;
}
