/*
 * Copyright 2016-2021, 2024, 2026 Uber Technologies, Inc.
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
/** @file h3Index.c
 * @brief   H3Index utility functions
 *          (see h3api.h for the main library entry functions)
 */
#include "h3Index.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "baseCells.h"
#include "directedEdge.h"
#include "faceijk.h"
#include "h3Assert.h"
#include "iterators.h"
#include "mathExtensions.h"
#include "vertex.h"

// TODO: https://github.com/uber/h3/issues/984
static const bool isBaseCellPentagonArr[128] = {
    [4] = 1,  [14] = 1, [24] = 1, [38] = 1, [49] = 1,  [58] = 1,
    [63] = 1, [72] = 1, [83] = 1, [97] = 1, [107] = 1, [117] = 1};

/** @var H3ErrorDescriptions
 *  @brief An array of strings describing each of the H3ErrorCodes enum values
 */
static char *H3ErrorDescriptions[] = {
    /* E_SUCCESS */ "Success",
    /* E_FAILED */
    "The operation failed but a more specific error is not available",
    /* E_DOMAIN */ "Argument was outside of acceptable range",
    /* E_LATLNG_DOMAIN */
    "Latitude or longitude arguments were outside of acceptable range",
    /* E_RES_DOMAIN */ "Resolution argument was outside of acceptable range",
    /* E_CELL_INVALID */ "Cell argument was not valid",
    /* E_DIR_EDGE_INVALID */ "Directed edge argument was not valid",
    /* E_UNDIR_EDGE_INVALID */ "Undirected edge argument was not valid",
    /* E_VERTEX_INVALID */ "Vertex argument was not valid",
    /* E_PENTAGON */ "Pentagon distortion was encountered",
    /* E_DUPLICATE_INPUT */ "Duplicate input",
    /* E_NOT_NEIGHBORS */ "Cell arguments were not neighbors",
    /* E_RES_MISMATCH */ "Cell arguments had incompatible resolutions",
    /* E_MEMORY_ALLOC */ "Memory allocation failed",
    /* E_MEMORY_BOUNDS */ "Bounds of provided memory were insufficient",
    /* E_OPTION_INVALID */ "Mode or flags argument was not valid",
    /* E_INDEX_INVALID */ "Index argument was not valid",
    /* E_BASE_CELL_DOMAIN */ "Base cell number was outside of acceptable range",
    /* E_DIGIT_DOMAIN */ "Child digits invalid",
    /* E_DELETED_DIGIT */ "Deleted subsequence indicates invalid index"};

/**
 * Returns the string describing the H3Error. This string is internally
 * allocated and should not be `free`d.
 * @param err The H3 error.
 * @return The string describing the H3Error
 */
const char *H3_EXPORT(describeH3Error)(H3Error err) {
    // err is always non-negative because it is an unsigned integer
    if (err < H3_ERROR_END) {
        return H3ErrorDescriptions[err];
    }
    return "Invalid error code";
}

/**
 * Returns the H3 resolution of an H3 index.
 * @param h The H3 index.
 * @return The resolution of the H3 index argument.
 */
int H3_EXPORT(getResolution)(H3Index h) {
    // H3-EXTENDED: Rule LB (§5.1) — public API now reports the effective
    // resolution (0-22). For stock cells (ext flag = 0) this is identical
    // to H3_GET_RESOLUTION. For ext cells (ext flag = 1) it adds 16 so
    // callers see 16-22 instead of the raw 0-6 stock-res field.
    return H3_GET_EFFECTIVE_RESOLUTION(h);
}

/**
 * Returns the H3 base cell "number" of an H3 cell (hexagon or pentagon).
 *
 * Note: Technically works on H3 edges, but will return base cell of the
 * origin cell.
 *
 * @param h The H3 cell.
 * @return The base cell "number" of the H3 cell argument.
 */
int H3_EXPORT(getBaseCellNumber)(H3Index h) { return H3_GET_BASE_CELL(h); }

/**
 * Returns the index digit at `res`, which starts with 1 for resolution
 * 1, up to and including resolution 15.
 *
 * 0 is not a valid value for `res` because resolution 0 is specified by
 * the base cell number, not an indexing digit.
 *
 * `res` may exceed the actual resolution of the index, in which case
 * the actual digit stored in the index is returned. For valid cell indexes
 * this will be 7.
 *
 * @param h The H3 index (e.g. cell).
 * @param res Which indexing digit to retrieve, starting with 1.
 * @param out Receives the value of the indexing digit.
 * @return 0 (E_SUCCESS) on success, or another value otherwise.
 */
H3Error H3_EXPORT(getIndexDigit)(H3Index h, int res, int *out) {
    // H3-EXTENDED: Rule GR (§5.2) + Rule DR (§5.3). Public API now accepts
    // res 1-22 and dispatches digit access (stock vs ext bit positions).
    if (res < 1 || res > MAX_H3_EXT_RES) {
        return E_RES_DOMAIN;
    }
    *out = H3_GET_DIGIT_AT_RES(h, res);
    return E_SUCCESS;
}

/**
 * Create a cell from its components (resolution, base cell, children digits).
 * Only allows for constructing valid H3 cells.
 *
 * @param res  0--15
 * @param baseCellNumber  0--121
 * @param digits  Array of child digits (0--6) of length `res`.
 *                NULL allowed for `res=0`.
 * @param out  Created cell
 * @return 0 (E_SUCCESS) on success, otherwise some H3Error
 **/
H3Error H3_EXPORT(constructCell)(int res, int baseCellNumber, const int *digits,
                                 H3Index *out) {
    // H3-EXTENDED: Rule GR (§5.2) — accept ext resolutions 16-22.
    if (res < 0 || res > MAX_H3_EXT_RES) {
        return E_RES_DOMAIN;
    }
    if (baseCellNumber < 0 || baseCellNumber >= NUM_BASE_CELLS) {
        return E_BASE_CELL_DOMAIN;
    }

    // H3-EXTENDED: Rule INIT (§5.5) — ext path seeds digits 1-22 to sentinel 7
    // via H3_INIT_EXT; stock path keeps H3_INIT byte-identical.
    H3Index h = (res > MAX_H3_RES) ? H3_INIT_EXT : H3_INIT;
    H3_SET_MODE(h, H3_CELL_MODE);
    // H3-EXTENDED: Rule RW (§5.4) — atomic res field + ext flag write.
    H3_SET_EFFECTIVE_RESOLUTION(h, res);
    H3_SET_BASE_CELL(h, baseCellNumber);

    bool isPentagon = isBaseCellPentagonArr[baseCellNumber];

    for (int r = 1; r <= res; r++) {
        int d = digits[r - 1];
        if (d < CENTER_DIGIT || d >= INVALID_DIGIT) {  // (d < 0 || d >= 7)
            return E_DIGIT_DOMAIN;
        }
        if (isPentagon) {
            // check for deleted subsequences of pentagons
            if (d == CENTER_DIGIT) {  // d == 0
                // do nothing; still a pentagon
            } else if (d == K_AXES_DIGIT) {  // d == 1
                return E_DELETED_DIGIT;
            } else {
                isPentagon = false;
            }
        }
        // H3-EXTENDED: Rule DW (§5.3) — dispatching digit setter for r > 15.
        H3_SET_DIGIT_AT_RES(h, r, d);
    }

    *out = h;

    return E_SUCCESS;
}

/**
 * Converts a string representation of an H3 index into an H3 index.
 * @param str The string representation of an H3 index.
 * @param out Output: The H3 index corresponding to the string argument
 */
H3Error H3_EXPORT(stringToH3)(const char *str, H3Index *out) {
    // H3-EXTENDED: accepts 1-32 lowercase/uppercase hex chars. 16 chars or
    // fewer parse as legacy stock (low 64 bits, zero-extended). 17-32 chars
    // parse as canonical 128-bit (split at len-16). Trailing whitespace is
    // stripped (fgets-style line input compatibility); the remaining chars
    // must all be hex.
    if (str == NULL) return E_FAILED;
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' ||
                       str[len - 1] == ' ' || str[len - 1] == '\t')) {
        len--;
    }
    if (len == 0 || len > 32) return E_FAILED;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return E_FAILED;
        }
    }

    char buf[33];
    memcpy(buf, str, len);
    buf[len] = '\0';

    if (len <= 16) {
        uint64_t lo = 0;
        if (sscanf(buf, "%" PRIx64, &lo) != 1) return E_FAILED;
        *out = (H3Index)lo;
    } else {
        size_t high_len = len - 16;
        char high_buf[17];
        memcpy(high_buf, buf, high_len);
        high_buf[high_len] = '\0';

        uint64_t hi = 0, lo = 0;
        if (sscanf(high_buf, "%" PRIx64, &hi) != 1) return E_FAILED;
        if (sscanf(buf + high_len, "%" PRIx64, &lo) != 1) return E_FAILED;
        *out = ((H3Index)hi << 64) | (H3Index)lo;
    }
    return E_SUCCESS;
}

/**
 * Converts an H3 index into a string representation.
 * @param h The H3 index to convert.
 * @param str The string representation of the H3 index.
 * @param sz Size of the buffer `str`
 */
H3Error H3_EXPORT(h3ToString)(H3Index h, char *str, size_t sz) {
    // H3-EXTENDED: stock cells (high 64 bits = 0) emit 16-char legacy hex
    // (buffer >=17 bytes incl NUL) for byte-identical compat with stock H3
    // 4.4.1 output. Ext cells emit 32-char canonical 128-bit hex (buffer
    // >=33 bytes). Per playbook §9.3.
    uint64_t hi = (uint64_t)(h >> 64);
    uint64_t lo = (uint64_t)h;

    if (hi == 0) {
        if (sz < 17) return E_MEMORY_BOUNDS;
        snprintf(str, sz, "%" PRIx64, lo);
    } else {
        if (sz < 33) return E_MEMORY_BOUNDS;
        snprintf(str, sz, "%016" PRIx64 "%016" PRIx64, hi, lo);
    }
    return E_SUCCESS;
}

/*
The top 8 bits of the low 64-bit half of any cell should be a specific
constant:

- The 1 high bit should be `0`
- The 4 mode bits should be `0001` (H3_CELL_MODE)
- The 3 reserved bits should be `000`

In total, the top 8 bits of the low half should be `0_0001_000`.

H3-EXTENDED (playbook §6.1, POC-2): widened to 128-bit. Stock-half check
is preserved verbatim so res 0-15 cells validate byte-identical to stock.
High half is checked separately:
- ext flag clear: entire bits 64-127 must be zero (stock layout invariant).
- ext flag set:   bits 86-127 must be zero (reserved); stock-res field
                  must be in [0, 6] (effective res 16-22).
*/
static inline bool _hasGoodTopBits(H3Index h) {
    uint64_t low = (uint64_t)h;
    uint64_t high = (uint64_t)(h >> 64);

    // Stock low-half check — byte-identical to upstream.
    low >>= (64 - 8);
    if (low != 0b00001000) return false;

    if (H3_GET_EXT_FLAG(h)) {
        // Ext cell: bits 86-127 (high-half bits 22+) must be zero.
        if (high & ~(((uint64_t)1 << 22) - 1)) return false;
        // Stock-res field must encode an ext effective res in [16, 22],
        // i.e. raw value in [0, 6].
        if (H3_GET_RESOLUTION(h) > 6) return false;
    } else {
        // Stock cell: entire high half must be zero.
        if (high != 0) return false;
    }
    return true;
}

/* Check that no digit from 1 to `res` is 7 (INVALID_DIGIT).

MHI = 0b100100100100100100100100100100100100100100100;
MLO = MHI >> 2;

|  d  | d & MHI |  ~d | ~d - MLO | d & MHI & (~d - MLO) |  result |
|-----|---------|-----|----------|----------------------|---------|
| 000 |     000 |     |          |                  000 | OK      |
| 001 |     000 |     |          |                  000 | OK      |
| 010 |     000 |     |          |                  000 | OK      |
| 011 |     000 |     |          |                  000 | OK      |
| 100 |     100 | 011 | 010      |                  000 | OK      |
| 101 |     100 | 010 | 001      |                  000 | OK      |
| 110 |     100 | 001 | 000      |                  000 | OK      |
| 111 |     100 | 000 | 111*     |                  100 | invalid |

  *: carry happened


Note: only care about identifying the *lowest* 7.

Examples with multiple digits:

|    d    | d & MHI |    ~d   | ~d - MLO | d & MHI & (~d - MLO) |  result |
|---------|---------|---------|----------|----------------------|---------|
| 111.111 | 100.100 | 000.000 | 110.111* |              100.100 | invalid |
| 110.111 | 100.100 | 001.000 | 111.111* |              100.100 | invalid |
| 110.110 | 100.100 | 001.001 | 000.000  |              000.000 | OK      |

  *: carry happened

In the second example with 110.111, we "misidentify" the 110 as a 7, due
to a carry from the lower bits. But this is OK because we correctly
identify the lowest (only, in this example) 7 just before it.

We only have to worry about carries affecting higher bits in the case of
a 7; all other digits (0--6) don't cause a carry when computing ~d - MLO.
So even though a 7 can affect the results of higher bits, this is OK
because we will always correctly identify the lowest 7.

For further notes, see the discussion here:
https://github.com/uber/h3/pull/496#discussion_r795851046
*/
static inline bool _hasAny7UptoRes(H3Index h, int res) {
    // H3-EXTENDED (playbook §6.3, POC-2): stock bit-magic spans only digits
    // 1-15 (bits 0-44). For ext cells digits 16-22 live in bits 65-85 of
    // the high half; the bit-magic mask doesn't reach them. Widening:
    // run stock bit-magic over the low-64 digit window for digits 1..min(res, 15),
    // then loop-check ext digits 16..res for sentinel 7. Stock cells
    // (res <= 15) take the bit-magic path only — byte-identical to upstream.
    const uint64_t MHI = 0b100100100100100100100100100100100100100100100;
    const uint64_t MLO = MHI >> 2;

    int stockRes = res > MAX_H3_RES ? MAX_H3_RES : res;
    uint64_t lo = (uint64_t)h;
    int shift = 3 * (MAX_H3_RES - stockRes);
    lo >>= shift;
    lo <<= shift;
    if ((lo & MHI & (~lo - MLO)) != 0) return true;

    if (res > MAX_H3_RES) {
        for (int r = MAX_H3_RES + 1; r <= res; r++) {
            if (H3_GET_DIGIT_AT_RES(h, r) == INVALID_DIGIT) return true;
        }
    }
    return false;
}

/* Check that all unused digits after `res` are set to 7 (INVALID_DIGIT).

H3-EXTENDED (playbook §6.4, POC-2): stock unconditionally returned true
for res >= 15, masking ext-cell sentinel violations in digits 16-22.
Replaced with a per-digit loop dispatching through H3_GET_DIGIT_AT_RES.
The loop's upper bound is MAX_H3_EXT_RES when the cell carries the ext
flag, MAX_H3_RES otherwise — so ext cells get checked through digit 22
and stock cells stop at 15. Replaces the Phase A partial-guard cast.
At most 22-res iterations; correctness > microseconds (CLAUDE.md).
*/
static inline bool _hasAll7AfterRes(H3Index h, int res) {
    int maxPos = H3_GET_EXT_FLAG(h) ? MAX_H3_EXT_RES : MAX_H3_RES;
    for (int r = res + 1; r <= maxPos; r++) {
        if (H3_GET_DIGIT_AT_RES(h, r) != INVALID_DIGIT) return false;
    }
    return true;
}

/*
Get index of first nonzero bit of an H3Index, or -1 if h == 0.

When available, use compiler intrinsics, which should be fast. If not
available, fall back to a loop.

H3-EXTENDED (playbook §6.2, POC-2): widened to 128-bit. Stock
`__builtin_clzll(h)` truncates the high 64 bits — if the only set bit is
above bit 63, the cast lands at zero and clzll(0) is UB. The fix is to
test the high half first and only call clzll on a non-zero word.

External linkage (matches `_h3Rotate60ccw` etc.) so testValidationExt
can forward-declare and exercise the E2 CRITICAL gate directly. No
current in-library caller after the E5 widening of _hasDeletedSubsequence.
*/
int _firstOneIndex(H3Index h) {
    uint64_t hi = (uint64_t)(h >> 64);
    uint64_t lo = (uint64_t)h;
#if defined(__GNUC__) || defined(__clang__)
    if (hi != 0) return 127 - __builtin_clzll(hi);
    if (lo != 0) return 63 - __builtin_clzll(lo);
    return -1;
#elif defined(_MSC_VER) && defined(_M_X64)  // doesn't work on win32
    unsigned long index;
    if (hi != 0) {
        _BitScanReverse64(&index, hi);
        return 64 + (int)index;
    }
    if (lo != 0) {
        _BitScanReverse64(&index, lo);
        return (int)index;
    }
    return -1;
#else
    // Portable fallback
    if (hi != 0) {
        int pos = 63;
        while ((hi & ((uint64_t)1 << pos)) == 0) pos--;
        return 64 + pos;
    }
    if (lo != 0) {
        int pos = 63;
        while ((lo & ((uint64_t)1 << pos)) == 0) pos--;
        return pos;
    }
    return -1;
#endif
}

/*
One final validation just for cells whose base cell (res 0)
is a pentagon.

Pentagon cells start with a sequence of 0's (CENTER_DIGIT's).
The first nonzero digit can't be a 1 (i.e., "deleted subsequence",
PENTAGON_SKIPPED_DIGIT, or K_AXES_DIGIT).

We can check that (in the lower 45 = 15*3 bits) the position of the
first 1 bit isn't divisible by 3.
*/
static inline bool _hasDeletedSubsequence(H3Index h, int base_cell) {
    if (!isBaseCellPentagonArr[base_cell]) return false;
    // H3-EXTENDED (playbook §6.5, POC-2): stock bit-magic windowed only the
    // lower 45 bits — pentagon K-axis violations in ext digits 16-22 slipped
    // past. Walk digits 1..effective_res via H3_GET_DIGIT_AT_RES dispatch and
    // return whether the first non-zero digit is K_AXES_DIGIT (= 1).
    // Replaces the Phase A partial-guard cast.
    int effRes = H3_GET_EFFECTIVE_RESOLUTION(h);
    for (int r = 1; r <= effRes; r++) {
        int d = H3_GET_DIGIT_AT_RES(h, r);
        if (d == 0) continue;
        return d == 1;  // K_AXES_DIGIT — pentagon-deleted subsequence
    }
    return false;
}

/**
 * Returns whether or not an H3 index is a valid cell (hexagon or pentagon).
 * @param h The H3 index to validate.
 * @return 1 if the H3 index if valid, and 0 if it is not.
 */
int H3_EXPORT(isValidCell)(H3Index h) {
    /*
    Look for bit patterns that would disqualify an H3Index from
    being valid. If identified, exit early.

    For reference the H3 index bit layout:

    |   Region   | # bits |
    |------------|--------|
    | High       |      1 |
    | Mode       |      4 |
    | Reserved   |      3 |
    | Resolution |      4 |
    | Base Cell  |      7 |
    | Digit 1    |      3 |
    | Digit 2    |      3 |
    | ...        |    ... |
    | Digit 15   |      3 |

    Speed benefits come from using bit manipulation instead of loops,
    whenever possible.
    */
    if (!_hasGoodTopBits(h)) return false;

    // H3-EXTENDED (Rule LB, playbook §5.1): use effective resolution so the
    // digit predicates check the full ext-digit range for ext cells. Stock
    // cells: H3_GET_EFFECTIVE_RESOLUTION == H3_GET_RESOLUTION (ext flag 0,
    // shift 0) — byte-identical for res 0-15.
    const int res = H3_GET_EFFECTIVE_RESOLUTION(h);

    // Get base cell number and check that it is valid.
    const int bc = H3_GET_BASE_CELL(h);
    if (bc >= NUM_BASE_CELLS) return false;

    if (_hasAny7UptoRes(h, res)) return false;
    if (!_hasAll7AfterRes(h, res)) return false;
    if (_hasDeletedSubsequence(h, bc)) return false;

    // If no disqualifications were identified, the index is a valid H3 cell.
    return true;
}

/**
 * Returns whether or not an H3 index is valid for any mode (cell, directed
 * edge, or vertex).
 * @param h The H3 index to validate.
 * @return 1 if the H3 index is valid for any supported type, 0 otherwise.
 */
int H3_EXPORT(isValidIndex)(H3Index h) {
    return H3_EXPORT(isValidCell)(h) || H3_EXPORT(isValidDirectedEdge)(h) ||
           H3_EXPORT(isValidVertex)(h);
}

/**
 * Initializes an H3 index.
 * @param hp The H3 index to initialize.
 * @param res The H3 resolution to initialize the index to.
 * @param baseCell The H3 base cell to initialize the index to.
 * @param initDigit The H3 digit (0-7) to initialize all of the index digits to.
 */
void setH3Index(H3Index *hp, int res, int baseCell, Direction initDigit) {
    // H3-EXTENDED: Rule INIT (playbook §5.5). Ext path seeds digits 1-22 to
    // sentinel 7 via H3_INIT_EXT; stock path keeps H3_INIT byte-identical.
    H3Index h = (res > MAX_H3_RES) ? H3_INIT_EXT : H3_INIT;
    H3_SET_MODE(h, H3_CELL_MODE);
    // H3-EXTENDED: Rule RW (§5.4) — atomic write of low-4-bit res field + ext flag.
    H3_SET_EFFECTIVE_RESOLUTION(h, res);
    H3_SET_BASE_CELL(h, baseCell);
    // H3-EXTENDED: Rule DW (§5.3) — dispatch to ext-digit setter for r > 15.
    for (int r = 1; r <= res; r++) H3_SET_DIGIT_AT_RES(h, r, initDigit);
    *hp = h;
}

/**
 * cellToParent produces the parent index for a given H3 index
 *
 * @param h H3Index to find parent of
 * @param parentRes The resolution to switch to (parent, grandparent, etc)
 * @param out Output: H3Index of the parent
 */
H3Error H3_EXPORT(cellToParent)(H3Index h, int parentRes, H3Index *out) {
    // H3-EXTENDED: Rule LB on childRes capture; Rule GR on the upper-bound
    // guard; Rule RW on the resolution write; Rule DW on the per-digit
    // sentinel writes (trap §6.7). For ext children whose parent is a stock
    // cell (parentRes ≤ 15), the high half must be cleared entirely — bit 64
    // (ext flag) is cleared by H3_SET_EFFECTIVE_RESOLUTION but the ext digit
    // bits 65-85 retain the child's path digits, which is invalid layout for
    // a stock cell (see playbook §2 / §6.1: ext flag clear ⇒ entire high
    // half must be zero). Mask to low 64 bits in that case.
    int childRes = H3_GET_EFFECTIVE_RESOLUTION(h);
    if (parentRes < 0 || parentRes > MAX_H3_EXT_RES) {
        return E_RES_DOMAIN;
    } else if (parentRes > childRes) {
        return E_RES_MISMATCH;
    } else if (parentRes == childRes) {
        *out = h;
        return E_SUCCESS;
    }
    H3Index parentH = h;
    H3_SET_EFFECTIVE_RESOLUTION(parentH, parentRes);
    if (parentRes <= MAX_H3_RES) {
        // Stock parent: scrub the entire high half (ext flag + ext digits +
        // reserved). The Rule DW loop below then re-establishes the
        // sentinel-7 low-half digits parentRes+1..15.
        parentH &= 0xFFFFFFFFFFFFFFFFULL;
    }
    int sentinelEnd = (parentRes <= MAX_H3_RES) ? MAX_H3_RES : MAX_H3_EXT_RES;
    for (int i = parentRes + 1; i <= sentinelEnd; i++) {
        H3_SET_DIGIT_AT_RES(parentH, i, H3_DIGIT_MASK);
    }
    *out = parentH;
    return E_SUCCESS;
}

/**
 * Determines whether one resolution is a valid child resolution for a cell.
 * Each resolution is considered a valid child resolution of itself.
 *
 * @param h         h3Index  parent cell
 * @param childRes  int      resolution of the child
 *
 * @return The validity of the child resolution
 */
static bool _hasChildAtRes(H3Index h, int childRes) {
    // H3-EXTENDED: Rule LB on parentRes capture; Rule GR on the upper-bound.
    // Without these, an ext parent would compare childRes against the 4-bit
    // stock-res field (0-6) and reject most legitimate child requests; an
    // ext childRes (16-22) would also be rejected by the stock MAX_H3_RES
    // bound.
    int parentRes = H3_GET_EFFECTIVE_RESOLUTION(h);
    if (childRes < parentRes || childRes > MAX_H3_EXT_RES) {
        return false;
    }
    return true;
}

/**
 * cellToChildrenSize returns the exact number of children for a cell at a
 * given child resolution.
 *
 * @param h         H3Index to find the number of children of
 * @param childRes  The child resolution you're interested in
 * @param out      Output: exact number of children (handles hexagons and
 * pentagons correctly)
 */
H3Error H3_EXPORT(cellToChildrenSize)(H3Index h, int childRes, int64_t *out) {
    if (!_hasChildAtRes(h, childRes)) return E_RES_DOMAIN;

    // H3-EXTENDED: Rule LB on the depth arithmetic (trap §6.17). Without
    // this, an ext parent at effective res 18 with childRes 22 would compute
    // n = 22 - 2 = 20 (using the 4-bit stock-res field), returning ~7^20
    // children — allocation explodes. With the effective-res getter, n =
    // 22 - 18 = 4, returning the correct 7^4 = 2401.
    int n = childRes - H3_GET_EFFECTIVE_RESOLUTION(h);

    if (H3_EXPORT(isPentagon)(h)) {
        *out = 1 + 5 * (_ipow(7, n) - 1) / 6;
    } else {
        *out = _ipow(7, n);
    }
    return E_SUCCESS;
}

/**
 * makeDirectChild takes an index and immediately returns the immediate child
 * index based on the specified cell number. Bit operations only, could generate
 * invalid indexes if not careful (deleted cell under a pentagon).
 *
 * @param h H3Index to find the direct child of
 * @param cellNumber int id of the direct child (0-6)
 *
 * @return The new H3Index for the child
 */
H3Index makeDirectChild(H3Index h, int cellNumber) {
    // H3-EXTENDED: Rule LB on the resolution arithmetic (trap §6.17); Rule
    // RW for the resolution write so the ext flag is set atomically when
    // the new childRes crosses into the ext range; Rule DW so the digit
    // write dispatches correctly for childRes > 15.
    int childRes = H3_GET_EFFECTIVE_RESOLUTION(h) + 1;
    H3Index childH = h;
    H3_SET_EFFECTIVE_RESOLUTION(childH, childRes);
    H3_SET_DIGIT_AT_RES(childH, childRes, cellNumber);
    return childH;
}

/**
 * cellToChildren takes the given hexagon id and generates all of the children
 * at the specified resolution storing them into the provided memory pointer.
 * It's assumed that cellToChildrenSize was used to determine the allocation.
 *
 * @param h H3Index to find the children of
 * @param childRes int the child level to produce
 * @param children H3Index* the memory to store the resulting addresses in
 */
H3Error H3_EXPORT(cellToChildren)(H3Index h, int childRes, H3Index *children) {
    int64_t i = 0;
    for (IterCellsChildren iter = iterInitParent(h, childRes); iter.h;
         iterStepChild(&iter)) {
        children[i] = iter.h;
        i++;
    }
    return E_SUCCESS;
}

/**
 * Zero out index digits from start to end, inclusive.
 * No-op if start > end.
 */
H3Index _zeroIndexDigits(H3Index h, int start, int end) {
    if (start > end) return h;
    // H3-EXTENDED: stock bit-magic shifts by `H3_PER_DIGIT_OFFSET * (MAX_H3_RES - end)`
    // which is negative when end > 15 — UB (playbook §6.6, POC-3 I1). Replace with
    // a per-digit loop dispatched through H3_SET_DIGIT_AT_RES; for stock-only
    // ranges the loop produces a byte-identical result.
    for (int r = start; r <= end; r++) {
        H3_SET_DIGIT_AT_RES(h, r, 0);
    }
    return h;
}

/**
 * cellToCenterChild produces the center child index for a given H3 index at
 * the specified resolution
 *
 * @param h H3Index to find center child of
 * @param childRes The resolution to switch to
 * @param child H3Index of the center child
 * @return 0 (E_SUCCESS) on success
 */
H3Error H3_EXPORT(cellToCenterChild)(H3Index h, int childRes, H3Index *child) {
    if (!_hasChildAtRes(h, childRes)) return E_RES_DOMAIN;

    // H3-EXTENDED: trap §6.8 — stock implementation reads parent res via
    // H3_GET_RESOLUTION (returns 4-bit stock-res field; for ext parent at
    // effective res 18 returns 2, so digit-zero window is wrong by 16) and
    // writes via H3_SET_RESOLUTION (truncates childRes to 4 bits and leaves
    // the ext flag clear, producing a malformed "looks like stock res-0"
    // cell when childRes ≥ 16). Use Group C dispatch on both reads/writes.
    h = _zeroIndexDigits(h, H3_GET_EFFECTIVE_RESOLUTION(h) + 1, childRes);
    H3_SET_EFFECTIVE_RESOLUTION(h, childRes);
    // For ext children, populate the trailing INVALID_DIGIT sentinels at
    // digits childRes+1..MAX_H3_EXT_RES so the result satisfies
    // _hasAll7AfterRes. (Stock children: parent's H3_INIT-derived sentinels
    // at digits 11..15 already cover this; high half stays zero.)
    if (childRes > MAX_H3_RES) {
        for (int r = childRes + 1; r <= MAX_H3_EXT_RES; r++) {
            H3_SET_DIGIT_AT_RES(h, r, INVALID_DIGIT);
        }
    }
    *child = h;
    return E_SUCCESS;
}

/**
 * compactCells takes a set of hexagons all at the same resolution and
 * compresses them by pruning full child branches to the parent level. This is
 * also done for all parents recursively to get the minimum number of hex
 * addresses that perfectly cover the defined space.
 * @param h3Set Set of hexagons
 * @param compactedSet The output array of compressed hexagons (preallocated)
 * @param numHexes The size of the input and output arrays (possible that no
 * contiguous regions exist in the set at all and no compression possible)
 * @return an error code on bad input data
 */
H3Error H3_EXPORT(compactCells)(const H3Index *h3Set, H3Index *compactedSet,
                                const int64_t numHexes) {
    if (numHexes == 0) {
        return E_SUCCESS;
    }
    int res = H3_GET_RESOLUTION(h3Set[0]);
    if (res == 0) {
        // No compaction possible, just copy the set to output
        for (int64_t i = 0; i < numHexes; i++) {
            compactedSet[i] = h3Set[i];
        }
        return E_SUCCESS;
    }
    H3Index *remainingHexes = H3_MEMORY(malloc)(numHexes * sizeof(H3Index));
    if (!remainingHexes) {
        return E_MEMORY_ALLOC;
    }
    memcpy(remainingHexes, h3Set, numHexes * sizeof(H3Index));
    H3Index *hashSetArray = H3_MEMORY(calloc)(numHexes, sizeof(H3Index));
    if (!hashSetArray) {
        H3_MEMORY(free)(remainingHexes);
        return E_MEMORY_ALLOC;
    }
    H3Index *compactedSetOffset = compactedSet;
    int64_t numRemainingHexes = numHexes;
    while (numRemainingHexes) {
        res = H3_GET_RESOLUTION(remainingHexes[0]);
        int parentRes = res - 1;

        // If parentRes is less than zero, we've compacted all the way up to the
        // base cells. Time to process the remaining cells.
        if (parentRes >= 0) {
            // Put the parents of the hexagons into the temp array
            // via a hashing mechanism, and use the reserved bits
            // to track how many times a parent is duplicated
            for (int64_t i = 0; i < numRemainingHexes; i++) {
                H3Index currIndex = remainingHexes[i];
                // TODO: This case is coverable (reachable by fuzzer)
                if (currIndex != 0) {
                    // If the reserved bits were set by the caller, the
                    // algorithm below may encounter undefined behavior
                    // because it expects to have set the reserved bits
                    // itself.
                    if (H3_GET_RESERVED_BITS(currIndex) != 0) {
                        H3_MEMORY(free)(remainingHexes);
                        H3_MEMORY(free)(hashSetArray);
                        return E_CELL_INVALID;
                    }

                    H3Index parent;
                    H3Error parentError =
                        H3_EXPORT(cellToParent)(currIndex, parentRes, &parent);
                    // Should never be reachable as a result of the compact
                    // algorithm. Can happen if cellToParent errors e.g.
                    // because of incompatible resolutions.
                    if (parentError) {
                        H3_MEMORY(free)(remainingHexes);
                        H3_MEMORY(free)(hashSetArray);
                        return parentError;
                    }
                    // Modulus hash the parent into the temp array
                    int64_t loc = (int64_t)(parent % numRemainingHexes);
                    DEFENSEONLY(int64_t loopCount = 0);
                    while (hashSetArray[loc] != 0) {
                        if (NEVER(loopCount > numRemainingHexes)) {
                            // This case should not be possible because at
                            // most one index is placed into hashSetArray
                            // per numRemainingHexes.
                            H3_MEMORY(free)(remainingHexes);
                            H3_MEMORY(free)(hashSetArray);
                            return E_FAILED;
                        }
                        H3Index tempIndex =
                            hashSetArray[loc] & H3_RESERVED_MASK_NEGATIVE;
                        if (tempIndex == parent) {
                            int count =
                                H3_GET_RESERVED_BITS(hashSetArray[loc]) + 1;
                            int limitCount = 7;
                            if (H3_EXPORT(isPentagon)(
                                    tempIndex & H3_RESERVED_MASK_NEGATIVE)) {
                                limitCount--;
                            }
                            // One is added to count for this check to match
                            // one being added to count later in this
                            // function when checking for all children being
                            // present.
                            if (count + 1 > limitCount) {
                                // Only possible on duplicate input
                                H3_MEMORY(free)(remainingHexes);
                                H3_MEMORY(free)(hashSetArray);
                                return E_DUPLICATE_INPUT;
                            }
                            H3_SET_RESERVED_BITS(parent, count);
                            hashSetArray[loc] = H3_NULL;
                        } else {
                            loc = (loc + 1) % numRemainingHexes;
                        }
                        DEFENSEONLY(loopCount++);
                    }
                    hashSetArray[loc] = parent;
                }
            }
        }

        // Determine which parent hexagons have a complete set
        // of children and put them in the compactableHexes array
        int64_t compactableCount = 0;
        int64_t maxCompactableCount =
            numRemainingHexes / 6;  // Somehow all pentagons; conservative
        if (maxCompactableCount == 0) {
            memcpy(compactedSetOffset, remainingHexes,
                   numRemainingHexes * sizeof(remainingHexes[0]));
            break;
        }
        H3Index *compactableHexes =
            H3_MEMORY(calloc)(maxCompactableCount, sizeof(H3Index));
        if (!compactableHexes) {
            H3_MEMORY(free)(remainingHexes);
            H3_MEMORY(free)(hashSetArray);
            return E_MEMORY_ALLOC;
        }
        for (int64_t i = 0; i < numRemainingHexes; i++) {
            if (hashSetArray[i] == 0) continue;
            int count = H3_GET_RESERVED_BITS(hashSetArray[i]) + 1;
            // Include the deleted direction for pentagons as implicitly "there"
            if (H3_EXPORT(isPentagon)(hashSetArray[i] &
                                      H3_RESERVED_MASK_NEGATIVE)) {
                // We need this later on, no need to recalculate
                H3_SET_RESERVED_BITS(hashSetArray[i], count);
                // Increment count after setting the reserved bits,
                // since count is already incremented above, so it
                // will be the expected value for a complete hexagon.
                count++;
            }
            if (count == 7) {
                // Bingo! Full set!
                compactableHexes[compactableCount] =
                    hashSetArray[i] & H3_RESERVED_MASK_NEGATIVE;
                compactableCount++;
            }
        }
        // Uncompactable hexes are immediately copied into the
        // output compactedSetOffset
        int64_t uncompactableCount = 0;
        for (int64_t i = 0; i < numRemainingHexes; i++) {
            H3Index currIndex = remainingHexes[i];
            // TODO: This case is coverable (reachable by fuzzer)
            if (currIndex != H3_NULL) {
                bool isUncompactable = true;
                // Resolution 0 cells always uncompactable, and trying to take
                // the res -1 parent of a cell is invalid.
                if (parentRes >= 0) {
                    H3Index parent;
                    H3Error parentError =
                        H3_EXPORT(cellToParent)(currIndex, parentRes, &parent);
                    if (NEVER(parentError)) {
                        H3_MEMORY(free)(compactableHexes);
                        H3_MEMORY(free)(remainingHexes);
                        H3_MEMORY(free)(hashSetArray);
                        return parentError;
                    }
                    // Modulus hash the parent into the temp array
                    // to determine if this index was included in
                    // the compactableHexes array
                    int64_t loc = (int64_t)(parent % numRemainingHexes);
                    DEFENSEONLY(int64_t loopCount = 0);
                    do {
                        if (NEVER(loopCount > numRemainingHexes)) {
                            // This case should not be possible because at most
                            // one index is placed into hashSetArray per input
                            // hexagon.
                            H3_MEMORY(free)(compactableHexes);
                            H3_MEMORY(free)(remainingHexes);
                            H3_MEMORY(free)(hashSetArray);
                            return E_FAILED;
                        }
                        H3Index tempIndex =
                            hashSetArray[loc] & H3_RESERVED_MASK_NEGATIVE;
                        if (tempIndex == parent) {
                            int count =
                                H3_GET_RESERVED_BITS(hashSetArray[loc]) + 1;
                            if (count == 7) {
                                isUncompactable = false;
                            }
                            break;
                        } else {
                            loc = (loc + 1) % numRemainingHexes;
                        }
                        DEFENSEONLY(loopCount++;)
                    } while (hashSetArray[loc] != parent);
                }
                if (isUncompactable) {
                    compactedSetOffset[uncompactableCount] = remainingHexes[i];
                    uncompactableCount++;
                }
            }
        }
        // Set up for the next loop
        memset(hashSetArray, 0, numHexes * sizeof(H3Index));
        compactedSetOffset += uncompactableCount;
        memcpy(remainingHexes, compactableHexes,
               compactableCount * sizeof(H3Index));
        numRemainingHexes = compactableCount;
        H3_MEMORY(free)(compactableHexes);
    }
    H3_MEMORY(free)(remainingHexes);
    H3_MEMORY(free)(hashSetArray);
    return E_SUCCESS;
}

/**
 * uncompactCells takes a compressed set of cells and expands back to the
 * original set of cells.
 *
 * Skips elements that are H3_NULL (i.e., 0).
 *
 * @param   compactedSet  Set of compacted cells
 * @param   numCompacted  The number of cells in the input compacted set
 * @param   outSet      Output array for decompressed cells (preallocated)
 * @param   numOut      The size of the output array to bound check against
 * @param   res         The H3 resolution to decompress to
 * @return              An error code if output array is too small or any cell
 *                      is smaller than the output resolution.
 */
H3Error H3_EXPORT(uncompactCells)(const H3Index *compactedSet,
                                  const int64_t numCompacted, H3Index *outSet,
                                  const int64_t numOut, const int res) {
    int64_t i = 0;

    for (int64_t j = 0; j < numCompacted; j++) {
        if (!_hasChildAtRes(compactedSet[j], res)) return E_RES_MISMATCH;

        for (IterCellsChildren iter = iterInitParent(compactedSet[j], res);
             iter.h; i++, iterStepChild(&iter)) {
            if (i >= numOut) return E_MEMORY_BOUNDS;  // went too far; abort!
            outSet[i] = iter.h;
        }
    }
    return E_SUCCESS;
}

/**
 * uncompactCellsSize takes a compacted set of hexagons and provides
 * the exact size of the uncompacted set of hexagons.
 *
 * @param   compactedSet  Set of hexagons
 * @param   numCompacted  The number of hexes in the input set
 * @param   res           The hexagon resolution to decompress to
 * @param   out           The number of hexagons to allocate memory for
 * @returns E_SUCCESS on success, or another value on error
 */
H3Error H3_EXPORT(uncompactCellsSize)(const H3Index *compactedSet,
                                      const int64_t numCompacted, const int res,
                                      int64_t *out) {
    int64_t numOut = 0;
    for (int64_t i = 0; i < numCompacted; i++) {
        if (compactedSet[i] == H3_NULL) continue;

        int64_t childrenSize;
        H3Error childrenError =
            H3_EXPORT(cellToChildrenSize)(compactedSet[i], res, &childrenSize);
        if (childrenError) {
            // The parent res does not contain `res`.
            return E_RES_MISMATCH;
        }
        numOut += childrenSize;
    }
    *out = numOut;
    return E_SUCCESS;
}

/**
 * isResClassIII takes a hexagon ID and determines if it is in a
 * Class III resolution (rotated versus the icosahedron and subject
 * to shape distortion adding extra points on icosahedron edges, making
 * them not true hexagons).
 * @param h The H3Index to check.
 * @return Returns 1 if the hexagon is class III, otherwise 0.
 */
int H3_EXPORT(isResClassIII)(H3Index h) { return H3_GET_RESOLUTION(h) % 2; }

/**
 * isPentagon takes an H3Index and determines if it is actually a pentagon.
 * @param h The H3Index to check.
 * @return Returns 1 if it is a pentagon, otherwise 0.
 */
int H3_EXPORT(isPentagon)(H3Index h) {
    return _isBaseCellPentagon(H3_GET_BASE_CELL(h)) &&
           !_h3LeadingNonZeroDigit(h);
}

/**
 * Returns the highest resolution non-zero digit in an H3Index.
 * @param h The H3Index.
 * @return The highest resolution non-zero digit in the H3Index.
 */
Direction _h3LeadingNonZeroDigit(H3Index h) {
    // H3-EXTENDED: Rule LB (§5.1) + Rule DR (§5.3 / trap §6.13) — walk
    // digits up to effective res so an ext cell whose first non-zero
    // digit lives in the ext range is detected.
    for (int r = 1; r <= H3_GET_EFFECTIVE_RESOLUTION(h); r++)
        if (H3_GET_DIGIT_AT_RES(h, r)) return H3_GET_DIGIT_AT_RES(h, r);

    // if we're here it's all 0's
    return CENTER_DIGIT;
}

/**
 * Rotate an H3Index 60 degrees counter-clockwise about a pentagonal center.
 * @param h The H3Index.
 */
H3Index _h3RotatePent60ccw(H3Index h) {
    // rotate in place; skips any leading 1 digits (k-axis)

    int foundFirstNonZeroDigit = 0;
    // H3-EXTENDED: Pattern 3 (Rule LB + DR + DW, trap §6.12).
    for (int r = 1, res = H3_GET_EFFECTIVE_RESOLUTION(h); r <= res; r++) {
        // rotate this digit
        H3_SET_DIGIT_AT_RES(h, r, _rotate60ccw(H3_GET_DIGIT_AT_RES(h, r)));

        // look for the first non-zero digit so we
        // can adjust for deleted k-axes sequence
        // if necessary
        if (!foundFirstNonZeroDigit && H3_GET_DIGIT_AT_RES(h, r) != 0) {
            foundFirstNonZeroDigit = 1;

            // adjust for deleted k-axes sequence
            if (_h3LeadingNonZeroDigit(h) == K_AXES_DIGIT)
                h = _h3Rotate60ccw(h);
        }
    }
    return h;
}

/**
 * Rotate an H3Index 60 degrees clockwise about a pentagonal center.
 * @param h The H3Index.
 */
H3Index _h3RotatePent60cw(H3Index h) {
    // rotate in place; skips any leading 1 digits (k-axis)

    int foundFirstNonZeroDigit = 0;
    // H3-EXTENDED: Pattern 3 (Rule LB + DR + DW, trap §6.12).
    for (int r = 1, res = H3_GET_EFFECTIVE_RESOLUTION(h); r <= res; r++) {
        // rotate this digit
        H3_SET_DIGIT_AT_RES(h, r, _rotate60cw(H3_GET_DIGIT_AT_RES(h, r)));

        // look for the first non-zero digit so we
        // can adjust for deleted k-axes sequence
        // if necessary
        if (!foundFirstNonZeroDigit && H3_GET_DIGIT_AT_RES(h, r) != 0) {
            foundFirstNonZeroDigit = 1;

            // adjust for deleted k-axes sequence
            if (_h3LeadingNonZeroDigit(h) == K_AXES_DIGIT) h = _h3Rotate60cw(h);
        }
    }
    return h;
}

/**
 * Rotate an H3Index 60 degrees counter-clockwise.
 * @param h The H3Index.
 */
H3Index _h3Rotate60ccw(H3Index h) {
    // H3-EXTENDED: Pattern 3 (Rule LB + DR + DW, trap §6.12) — bound on
    // effective res; dispatching digit getter/setter for r > 15.
    for (int r = 1, res = H3_GET_EFFECTIVE_RESOLUTION(h); r <= res; r++) {
        Direction oldDigit = H3_GET_DIGIT_AT_RES(h, r);
        H3_SET_DIGIT_AT_RES(h, r, _rotate60ccw(oldDigit));
    }

    return h;
}

/**
 * Rotate an H3Index 60 degrees clockwise.
 * @param h The H3Index.
 */
H3Index _h3Rotate60cw(H3Index h) {
    // H3-EXTENDED: Pattern 3 (Rule LB + DR + DW, trap §6.12).
    for (int r = 1, res = H3_GET_EFFECTIVE_RESOLUTION(h); r <= res; r++) {
        Direction oldDigit = H3_GET_DIGIT_AT_RES(h, r);
        H3_SET_DIGIT_AT_RES(h, r, _rotate60cw(oldDigit));
    }

    return h;
}

/**
 * Convert an FaceIJK address to the corresponding H3Index.
 * @param fijk The FaceIJK address.
 * @param res The cell resolution.
 * @return The encoded H3Index (or H3_NULL on failure).
 */
H3Index _faceIjkToH3(const FaceIJK *fijk, int res) {
    // initialize the index
    // H3-EXTENDED: Rule INIT (§5.5 / §6.16) — ext path seeds digits 1-22 to
    // sentinel 7. Stock path keeps H3_INIT byte-identical.
    H3Index h = (res > MAX_H3_RES) ? H3_INIT_EXT : H3_INIT;
    H3_SET_MODE(h, H3_CELL_MODE);
    // H3-EXTENDED: Rule RW (§5.4 / §6.16) — atomic res field + ext flag write.
    H3_SET_EFFECTIVE_RESOLUTION(h, res);

    // check for res 0/base cell
    if (res == 0) {
        if (fijk->coord.i > MAX_FACE_COORD || fijk->coord.j > MAX_FACE_COORD ||
            fijk->coord.k > MAX_FACE_COORD) {
            // out of range input
            return H3_NULL;
        }

        H3_SET_BASE_CELL(h, _faceIjkToBaseCell(fijk));
        return h;
    }

    // we need to find the correct base cell FaceIJK for this H3 index;
    // start with the passed in face and resolution res ijk coordinates
    // in that face's coordinate system
    FaceIJK fijkBC = *fijk;

    // build the H3Index from finest res up
    // adjust r for the fact that the res 0 base cell offsets the indexing
    // digits
    CoordIJK *ijk = &fijkBC.coord;
    for (int r = res - 1; r >= 0; r--) {
        CoordIJK lastIJK = *ijk;
        CoordIJK lastCenter;
        if (isResolutionClassIII(r + 1)) {
            // rotate ccw
            _upAp7(ijk);
            lastCenter = *ijk;
            _downAp7(&lastCenter);
        } else {
            // rotate cw
            _upAp7r(ijk);
            lastCenter = *ijk;
            _downAp7r(&lastCenter);
        }

        CoordIJK diff;
        _ijkSub(&lastIJK, &lastCenter, &diff);
        _ijkNormalize(&diff);

        // H3-EXTENDED: Rule DW (§5.3) — dispatching digit setter for r+1 > 15.
        H3_SET_DIGIT_AT_RES(h, r + 1, _unitIjkToDigit(&diff));
    }

    // fijkBC should now hold the IJK of the base cell in the
    // coordinate system of the current face

    if (fijkBC.coord.i > MAX_FACE_COORD || fijkBC.coord.j > MAX_FACE_COORD ||
        fijkBC.coord.k > MAX_FACE_COORD) {
        // out of range input
        return H3_NULL;
    }

    // lookup the correct base cell
    int baseCell = _faceIjkToBaseCell(&fijkBC);
    H3_SET_BASE_CELL(h, baseCell);

    // rotate if necessary to get canonical base cell orientation
    // for this base cell
    int numRots = _faceIjkToBaseCellCCWrot60(&fijkBC);
    if (_isBaseCellPentagon(baseCell)) {
        // force rotation out of missing k-axes sub-sequence
        if (_h3LeadingNonZeroDigit(h) == K_AXES_DIGIT) {
            // check for a cw/ccw offset face; default is ccw
            if (_baseCellIsCwOffset(baseCell, fijkBC.face)) {
                h = _h3Rotate60cw(h);
            } else {
                h = _h3Rotate60ccw(h);
            }
        }

        for (int i = 0; i < numRots; i++) h = _h3RotatePent60ccw(h);
    } else {
        for (int i = 0; i < numRots; i++) {
            h = _h3Rotate60ccw(h);
        }
    }

    return h;
}

/**
 * Encodes a coordinate on the sphere to the H3 index of the containing cell at
 * the specified resolution.
 *
 * Returns 0 on invalid input.
 *
 * @param g The spherical coordinates to encode.
 * @param res The desired H3 resolution for the encoding.
 * @param out The encoded H3Index.
 * @returns E_SUCCESS (0) on success, another value otherwise
 */
H3Error H3_EXPORT(latLngToCell)(const LatLng *g, int res, H3Index *out) {
    // H3-EXTENDED: Rule GR (§5.2) — accept ext resolutions 16-22.
    if (res < 0 || res > MAX_H3_EXT_RES) {
        return E_RES_DOMAIN;
    }
    if (!isfinite(g->lat) || !isfinite(g->lng)) {
        return E_LATLNG_DOMAIN;
    }

    Vec3d v = latLngToVec3(*g);
    return vec3ToCell(&v, res, out);
}

/**
 * Encodes a coordinate on the sphere to the H3 index of the containing cell at
 * the specified resolution.
 *
 * Vec3d v is expected to be on the unit sphere.
 *
 * @param v The 3D cartesian coordinates to encode.
 * @param res The desired H3 resolution for the encoding.
 * @param out The encoded H3Index.
 * @returns E_SUCCESS on success, another value otherwise
 */
H3Error vec3ToCell(const Vec3d *v, int res, H3Index *out) {
    // H3-EXTENDED: Rule GR (§5.2) — accept ext resolutions 16-22.
    if (res < 0 || res > MAX_H3_EXT_RES) {
        return E_RES_DOMAIN;
    }
    if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z)) {
        return E_DOMAIN;
    }

    FaceIJK fijk;
    _vec3ToFaceIjk(*v, res, &fijk);
    *out = _faceIjkToH3(&fijk, res);
    if (ALWAYS(*out)) {
        return E_SUCCESS;
    } else {
        return E_FAILED;
    }
}

/**
 * Convert an H3Index to the FaceIJK address on a specified icosahedral face.
 * @param h The H3Index.
 * @param fijk The FaceIJK address, initialized with the desired face
 *        and normalized base cell coordinates.
 * @return Returns 1 if the possibility of overage exists, otherwise 0.
 */
int _h3ToFaceIjkWithInitializedFijk(H3Index h, FaceIJK *fijk) {
    CoordIJK *ijk = &fijk->coord;
    // H3-EXTENDED: Rule LB (§5.1) — walk digits up to effective res, not
    // stock res. For an ext cell at res 19 the stock-res field holds 3;
    // without the dispatch the loop terminates at digit 3 and ext digits
    // 4-19 are silently dropped from the IJK traversal.
    int res = H3_GET_EFFECTIVE_RESOLUTION(h);

    // center base cell hierarchy is entirely on this face
    int possibleOverage = 1;
    if (!_isBaseCellPentagon(H3_GET_BASE_CELL(h)) &&
        (res == 0 ||
         (fijk->coord.i == 0 && fijk->coord.j == 0 && fijk->coord.k == 0)))
        possibleOverage = 0;

    for (int r = 1; r <= res; r++) {
        if (isResolutionClassIII(r)) {
            // Class III == rotate ccw
            _downAp7(ijk);
        } else {
            // Class II == rotate cw
            _downAp7r(ijk);
        }

        // H3-EXTENDED: Rule DR (§5.3) — dispatching digit getter for r > 15
        // (stock H3_GET_INDEX_DIGIT does a negative shift, UB).
        _neighbor(ijk, H3_GET_DIGIT_AT_RES(h, r));
    }

    return possibleOverage;
}

/**
 * Convert an H3Index to a FaceIJK address.
 * @param h The H3Index.
 * @param fijk The corresponding FaceIJK address.
 */
H3Error _h3ToFaceIjk(H3Index h, FaceIJK *fijk) {
    int baseCell = H3_GET_BASE_CELL(h);
    if (NEVER(baseCell < 0) || baseCell >= NUM_BASE_CELLS) {
        // Base cells less than zero can not be represented in an index
        // To prevent reading uninitialized memory, we zero the output.
        fijk->face = 0;
        fijk->coord.i = fijk->coord.j = fijk->coord.k = 0;
        return E_CELL_INVALID;
    }
    // adjust for the pentagonal missing sequence; all of sub-sequence 5 needs
    // to be adjusted (and some of sub-sequence 4 below)
    if (_isBaseCellPentagon(baseCell) && _h3LeadingNonZeroDigit(h) == 5)
        h = _h3Rotate60cw(h);

    // start with the "home" face and ijk+ coordinates for the base cell of c
    *fijk = baseCellData[baseCell].homeFijk;
    if (!_h3ToFaceIjkWithInitializedFijk(h, fijk))
        return E_SUCCESS;  // no overage is possible; h lies on this face

    // if we're here we have the potential for an "overage"; i.e., it is
    // possible that c lies on an adjacent face

    CoordIJK origIJK = fijk->coord;

    // if we're in Class III, drop into the next finer Class II grid
    // H3-EXTENDED: Rule LB (§5.1) — local res must be the effective res
    // (0-22) so the Class III dispatch is correct for ext cells. The two
    // post-overage comparisons below also widen for symmetry.
    int res = H3_GET_EFFECTIVE_RESOLUTION(h);
    if (isResolutionClassIII(res)) {
        // Class III
        _downAp7r(&fijk->coord);
        res++;
    }

    // adjust for overage if needed
    // a pentagon base cell with a leading 4 digit requires special handling
    int pentLeading4 =
        (_isBaseCellPentagon(baseCell) && _h3LeadingNonZeroDigit(h) == 4);
    if (_adjustOverageClassII(fijk, res, pentLeading4, 0) != NO_OVERAGE) {
        // if the base cell is a pentagon we have the potential for secondary
        // overages
        if (_isBaseCellPentagon(baseCell)) {
            while (_adjustOverageClassII(fijk, res, 0, 0) != NO_OVERAGE)
                continue;
        }

        if (res != H3_GET_EFFECTIVE_RESOLUTION(h)) _upAp7r(&fijk->coord);
    } else if (res != H3_GET_EFFECTIVE_RESOLUTION(h)) {
        fijk->coord = origIJK;
    }
    return E_SUCCESS;
}

/**
 * Determines the 3D cartesian coordinates of the center of an H3 cell.
 *
 * @param h3 The H3 index.
 * @param v The 3D cartesian coordinates of the H3 cell center.
 * @return E_SUCCESS on success, or another H3Error code on failure.
 */
H3Error cellToVec3(H3Index h3, Vec3d *v) {
    FaceIJK fijk;
    H3Error e = _h3ToFaceIjk(h3, &fijk);
    if (e) {
        return e;
    }
    // H3-EXTENDED: Rule LB (§5.1) — _hex2dToVec3 rescales radius by
    // sqrt(7)^-res. Passing stock res (e.g. 3) for an ext cell at res 19
    // would underweight the rescale by sqrt(7)^16 and place the point
    // far from its true location.
    _faceIjkToVec3(&fijk, H3_GET_EFFECTIVE_RESOLUTION(h3), v);
    return E_SUCCESS;
}

/**
 * Determines the spherical coordinates of the center point of an H3 index.
 *
 * @param h3 The H3 index.
 * @param g The spherical coordinates of the H3 cell center.
 */
H3Error H3_EXPORT(cellToLatLng)(H3Index h3, LatLng *g) {
    Vec3d v;
    H3Error e = cellToVec3(h3, &v);
    if (e) {
        return e;
    }
    *g = vec3ToLatLng(v);
    return E_SUCCESS;
}

/**
 * Determines the cell boundary in spherical coordinates for an H3 index.
 *
 * @param h3 The H3 index.
 * @param cb The boundary of the H3 cell in spherical coordinates.
 */
H3Error H3_EXPORT(cellToBoundary)(H3Index h3, CellBoundary *cb) {
    FaceIJK fijk;
    H3Error e = _h3ToFaceIjk(h3, &fijk);
    if (e) {
        return e;
    }
    if (H3_EXPORT(isPentagon)(h3)) {
        _faceIjkPentToCellBoundary(&fijk, H3_GET_RESOLUTION(h3), 0,
                                   NUM_PENT_VERTS, cb);
    } else {
        _faceIjkToCellBoundary(&fijk, H3_GET_RESOLUTION(h3), 0, NUM_HEX_VERTS,
                               cb);
    }
    return E_SUCCESS;
}

/**
 * Returns the max number of possible icosahedron faces an H3 index
 * may intersect.
 *
 * @return int count of faces
 */
H3Error H3_EXPORT(maxFaceCount)(H3Index h3, int *out) {
    // a pentagon always intersects 5 faces, a hexagon never intersects more
    // than 2 (but may only intersect 1)
    *out = H3_EXPORT(isPentagon)(h3) ? 5 : 2;
    return E_SUCCESS;
}

/**
 * Find all icosahedron faces intersected by a given H3 index, represented
 * as integers from 0-19. The array is sparse; since 0 is a valid value,
 * invalid array values are represented as -1. It is the responsibility of
 * the caller to filter out invalid values.
 *
 * @param h3 The H3 index
 * @param out Output array. Must be of size maxFaceCount(h3).
 */
H3Error H3_EXPORT(getIcosahedronFaces)(H3Index h3, int *out) {
    int res = H3_GET_RESOLUTION(h3);
    int isPent = H3_EXPORT(isPentagon)(h3);

    // We can't use the vertex-based approach here for class II pentagons,
    // because all their vertices are on the icosahedron edges. Their
    // direct child pentagons cross the same faces, so use those instead.
    if (isPent && !isResolutionClassIII(res)) {
        // Note that this would not work for res 15, but this is only run on
        // Class II pentagons, it should never be invoked for a res 15 index.
        H3Index childPentagon = makeDirectChild(h3, 0);
        return H3_EXPORT(getIcosahedronFaces)(childPentagon, out);
    }

    // convert to FaceIJK
    FaceIJK fijk;
    H3Error err = _h3ToFaceIjk(h3, &fijk);
    if (err) {
        return err;
    }

    // Get all vertices as FaceIJK addresses. For simplicity, always
    // initialize the array with 6 verts, ignoring the last one for pentagons
    FaceIJK fijkVerts[NUM_HEX_VERTS];
    int vertexCount;

    if (isPent) {
        vertexCount = NUM_PENT_VERTS;
        _faceIjkPentToVerts(&fijk, &res, fijkVerts);
    } else {
        vertexCount = NUM_HEX_VERTS;
        _faceIjkToVerts(&fijk, &res, fijkVerts);
    }

    // We may not use all of the slots in the output array,
    // so fill with invalid values to indicate unused slots
    int faceCount;
    H3Error maxFaceCountError = H3_EXPORT(maxFaceCount)(h3, &faceCount);
    if (NEVER(maxFaceCountError != E_SUCCESS)) {
        return maxFaceCountError;
    }
    for (int i = 0; i < faceCount; i++) {
        out[i] = INVALID_FACE;
    }

    // add each vertex face, using the output array as a hash set
    for (int i = 0; i < vertexCount; i++) {
        FaceIJK *vert = &fijkVerts[i];

        // Adjust overage, determining whether this vertex is
        // on another face
        if (isPent) {
            _adjustPentVertOverage(vert, res);
        } else {
            _adjustOverageClassII(vert, res, 0, 1);
        }

        // Save the face to the output array
        int face = vert->face;
        int pos = 0;
        // Find the first empty output position, or the first position
        // matching the current face
        while (out[pos] != INVALID_FACE && out[pos] != face) {
            pos++;
            if (pos >= faceCount) {
                // Mismatch between the heuristic used in maxFaceCount and
                // calculation here - indicates an invalid index.
                return E_FAILED;
            }
        }
        out[pos] = face;
    }
    return E_SUCCESS;
}

/**
 * pentagonCount returns the number of pentagons (same at any resolution)
 *
 * @return int count of pentagon indexes
 */
int H3_EXPORT(pentagonCount)(void) { return NUM_PENTAGONS; }

/**
 * Generates all pentagons at the specified resolution
 *
 * @param res The resolution to produce pentagons at.
 * @param out Output array. Must be of size pentagonCount().
 */
H3Error H3_EXPORT(getPentagons)(int res, H3Index *out) {
    if (res < 0 || res > MAX_H3_RES) {
        return E_RES_DOMAIN;
    }
    int i = 0;
    for (int bc = 0; bc < NUM_BASE_CELLS; bc++) {
        if (_isBaseCellPentagon(bc)) {
            H3Index pentagon;
            setH3Index(&pentagon, res, bc, 0);
            out[i++] = pentagon;
        }
    }
    return E_SUCCESS;
}

/**
 * Returns whether or not a resolution is a Class III grid. Note that odd
 * resolutions are Class III and even resolutions are Class II.
 * @param res The H3 resolution.
 * @return 1 if the resolution is a Class III grid, and 0 if the resolution is
 *         a Class II grid.
 */
int isResolutionClassIII(int res) { return res % 2; }

/**
 * Validate a child position in the context of a given parent, returning
 * an error if validation fails.
 */
static H3Error validateChildPos(int64_t childPos, H3Index parent,
                                int childRes) {
    int64_t maxChildCount;
    H3Error sizeError =
        H3_EXPORT(cellToChildrenSize)(parent, childRes, &maxChildCount);
    if (NEVER(sizeError)) {
        return sizeError;
    }
    if (childPos < 0 || childPos >= maxChildCount) {
        return E_DOMAIN;
    }
    return E_SUCCESS;
}

/**
 * Returns the position of the cell within an ordered list of all children of
 * the cell's parent at the specified resolution
 * @param child Child cell index
 * @param parentRes Resolution of the parent cell to find the position within
 * @param out Output: The position of the child cell within its parents cell
 * list of children
 */
H3Error H3_EXPORT(cellToChildPos)(H3Index child, int parentRes, int64_t *out) {
    int childRes = H3_GET_RESOLUTION(child);
    // Get the parent at res. This will catch any resolution errors
    H3Index originalParent;
    H3Error parentError =
        H3_EXPORT(cellToParent(child, parentRes, &originalParent));
    if (parentError) {
        return parentError;
    }

    // Define the initial parent. Note that these variables are reassigned
    // within the loop.
    H3Index parent = originalParent;
    int parentIsPentagon = H3_EXPORT(isPentagon)(parent);

    // Walk up the resolution digits, incrementing the index
    *out = 0;
    if (parentIsPentagon) {
        // Pentagon logic. Pentagon parents skip the 1 digit, so the offsets are
        // different from hexagons
        for (int res = childRes; res > parentRes; res--) {
            H3Error parentError =
                H3_EXPORT(cellToParent(child, res - 1, &parent));
            if (NEVER(parentError)) {
                return parentError;
            }

            parentIsPentagon = H3_EXPORT(isPentagon)(parent);
            int rawDigit = H3_GET_INDEX_DIGIT(child, res);
            // Validate the digit before proceeding
            if (rawDigit == INVALID_DIGIT ||
                (parentIsPentagon && rawDigit == K_AXES_DIGIT)) {
                return E_CELL_INVALID;
            }
            int digit =
                parentIsPentagon && rawDigit > 0 ? rawDigit - 1 : rawDigit;
            if (digit != CENTER_DIGIT) {
                int64_t hexChildCount = _ipow(7, childRes - res);
                // The offset for the 0-digit slot depends on whether the
                // current index is the child of a pentagon. If so, the offset
                // is based on the count of pentagon children, otherwise,
                // hexagon children.
                *out += (parentIsPentagon
                             ?  // pentagon children. See the explanation
                                // for getNumCells in h3api.h.in
                             1 + (5 * (hexChildCount - 1)) / 6
                             :  // one hexagon's children
                             hexChildCount) +
                        // the other hexagon children
                        (digit - 1) * hexChildCount;
            }
        }
    } else {
        // Hexagon logic. Offsets are simple powers of 7
        for (int res = childRes; res > parentRes; res--) {
            int digit = H3_GET_INDEX_DIGIT(child, res);
            if (digit == INVALID_DIGIT) {
                return E_CELL_INVALID;
            }
            *out += digit * _ipow(7, childRes - res);
        }
    }

    if (NEVER(validateChildPos(*out, originalParent, childRes))) {
        // This is the result of an internal error, so return E_FAILED
        // instead of the validation error
        return E_FAILED;
    }

    return E_SUCCESS;
}

/**
 * Returns the child cell at a given position within an ordered list of all
 * children at the specified resolution
 * @param childPos Position within the ordered list
 * @param parent Parent cell of the cell index to find
 * @param childRes Resolution of the child cell index
 * @param child Output: child cell index
 */
H3Error H3_EXPORT(childPosToCell)(int64_t childPos, H3Index parent,
                                  int childRes, H3Index *child) {
    // Validate resolution
    if (childRes < 0 || childRes > MAX_H3_RES) {
        return E_RES_DOMAIN;
    }
    // Validate parent resolution
    int parentRes = H3_GET_RESOLUTION(parent);
    if (childRes < parentRes) {
        return E_RES_MISMATCH;
    }
    // Validate child pos
    H3Error childPosErr = validateChildPos(childPos, parent, childRes);
    if (childPosErr) {
        return childPosErr;
    }

    int resOffset = childRes - parentRes;

    *child = parent;
    int64_t idx = childPos;

    H3_SET_RESOLUTION(*child, childRes);

    if (H3_EXPORT(isPentagon)(parent)) {
        // Pentagon tile logic. Pentagon tiles skip the 1 digit, so the offsets
        // are different
        bool inPent = true;
        for (int res = 1; res <= resOffset; res++) {
            int64_t resWidth = _ipow(7, resOffset - res);
            if (inPent) {
                // While we are inside a parent pentagon, we need to check if
                // this cell is a pentagon, and if not, we need to offset its
                // digit to account for the skipped direction
                int64_t pentWidth = 1 + (5 * (resWidth - 1)) / 6;
                if (idx < pentWidth) {
                    H3_SET_INDEX_DIGIT(*child, parentRes + res, 0);
                } else {
                    idx -= pentWidth;
                    inPent = false;
                    H3_SET_INDEX_DIGIT(*child, parentRes + res,
                                       (idx / resWidth) + 2);
                    idx %= resWidth;
                }
            } else {
                // We're no longer inside a pentagon, continue as for hex
                H3_SET_INDEX_DIGIT(*child, parentRes + res, idx / resWidth);
                idx %= resWidth;
            }
        }
    } else {
        // Hexagon tile logic. Offsets are simple powers of 7
        for (int res = 1; res <= resOffset; res++) {
            int64_t resWidth = _ipow(7, resOffset - res);
            H3_SET_INDEX_DIGIT(*child, parentRes + res, idx / resWidth);
            idx %= resWidth;
        }
    }

    return E_SUCCESS;
}
