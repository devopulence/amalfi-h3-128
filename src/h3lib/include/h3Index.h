/*
 * Copyright 2016-2018, 2020, 2026 Uber Technologies, Inc.
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
/** @file h3Index.h
 * @brief   H3Index functions.
 */

#ifndef H3INDEX_H
#define H3INDEX_H

#include "faceijk.h"
#include "h3api.h"

// define's of constants and macros for bitwise manipulation of H3Index's.

/** The number of bits in an H3 index. */
#define H3_NUM_BITS 64

/** The bit offset of the max resolution digit in an H3 index. */
#define H3_MAX_OFFSET 63

/** The bit offset of the mode in an H3 index. */
#define H3_MODE_OFFSET 59

/** The bit offset of the base cell in an H3 index. */
#define H3_BC_OFFSET 45

/** The bit offset of the resolution in an H3 index. */
#define H3_RES_OFFSET 52

/** The bit offset of the reserved bits in an H3 index. */
#define H3_RESERVED_OFFSET 56

/** The number of bits in a single H3 resolution digit. */
#define H3_PER_DIGIT_OFFSET 3

// H3-EXTENDED: masks retyped from uint64_t to H3Index (__uint128_t) so that
// the negated forms (~MASK) and ANDs with H3Index preserve the high 64 bits.
// Bit values are unchanged — only the C type widens.

/** 1 in the highest bit, 0's everywhere else. */
#define H3_HIGH_BIT_MASK ((H3Index)(1) << H3_MAX_OFFSET)

/** 0 in the highest bit, 1's everywhere else. */
#define H3_HIGH_BIT_MASK_NEGATIVE (~H3_HIGH_BIT_MASK)

/** 1's in the 4 mode bits, 0's everywhere else. */
#define H3_MODE_MASK ((H3Index)(15) << H3_MODE_OFFSET)

/** 0's in the 4 mode bits, 1's everywhere else. */
#define H3_MODE_MASK_NEGATIVE (~H3_MODE_MASK)

/** 1's in the 7 base cell bits, 0's everywhere else. */
#define H3_BC_MASK ((H3Index)(127) << H3_BC_OFFSET)

/** 0's in the 7 base cell bits, 1's everywhere else. */
#define H3_BC_MASK_NEGATIVE (~H3_BC_MASK)

/** 1's in the 4 resolution bits, 0's everywhere else. */
#define H3_RES_MASK ((H3Index)(15) << H3_RES_OFFSET)

/** 0's in the 4 resolution bits, 1's everywhere else. */
#define H3_RES_MASK_NEGATIVE (~H3_RES_MASK)

/** 1's in the 3 reserved bits, 0's everywhere else. */
#define H3_RESERVED_MASK ((H3Index)(7) << H3_RESERVED_OFFSET)

/** 0's in the 3 reserved bits, 1's everywhere else. */
#define H3_RESERVED_MASK_NEGATIVE (~H3_RESERVED_MASK)

/** 1's in the 3 bits of res 15 digit bits, 0's everywhere else. */
#define H3_DIGIT_MASK ((H3Index)(7))

/** 0's in the 7 base cell bits, 1's everywhere else. */
#define H3_DIGIT_MASK_NEGATIVE (~H3_DIGIT_MASK)

/**
 * H3 index with mode 0, res 0, base cell 0, and 7 for all index digits.
 * Typically used to initialize the creation of an H3 cell index, which
 * expects all direction digits to be 7 beyond the cell's resolution.
 */
#define H3_INIT ((H3Index)UINT64_C(35184372088831))

/**
 * Gets the highest bit of the H3 index.
 */
#define H3_GET_HIGH_BIT(h3) ((int)((((h3)&H3_HIGH_BIT_MASK) >> H3_MAX_OFFSET)))

/**
 * Sets the highest bit of the h3 to v.
 */
#define H3_SET_HIGH_BIT(h3, v)                 \
    (h3) = (((h3)&H3_HIGH_BIT_MASK_NEGATIVE) | \
            (((uint64_t)(v)) << H3_MAX_OFFSET))

/**
 * Gets the integer mode of h3.
 */
#define H3_GET_MODE(h3) ((int)((((h3)&H3_MODE_MASK) >> H3_MODE_OFFSET)))

/**
 * Sets the integer mode of h3 to v.
 */
#define H3_SET_MODE(h3, v) \
    (h3) = (((h3)&H3_MODE_MASK_NEGATIVE) | (((uint64_t)(v)) << H3_MODE_OFFSET))

/**
 * Gets the integer base cell of h3.
 */
#define H3_GET_BASE_CELL(h3) ((int)((((h3)&H3_BC_MASK) >> H3_BC_OFFSET)))

/**
 * Sets the integer base cell of h3 to bc.
 */
#define H3_SET_BASE_CELL(h3, bc) \
    (h3) = (((h3)&H3_BC_MASK_NEGATIVE) | (((uint64_t)(bc)) << H3_BC_OFFSET))

/**
 * Gets the integer resolution of h3.
 */
#define H3_GET_RESOLUTION(h3) ((int)((((h3)&H3_RES_MASK) >> H3_RES_OFFSET)))

/**
 * Sets the integer resolution of h3.
 */
#define H3_SET_RESOLUTION(h3, res) \
    (h3) = (((h3)&H3_RES_MASK_NEGATIVE) | (((uint64_t)(res)) << H3_RES_OFFSET))

/**
 * Gets the resolution res integer digit (0-7) of h3.
 */
#define H3_GET_INDEX_DIGIT(h3, res)                                        \
    ((Direction)((((h3) >> ((MAX_H3_RES - (res)) * H3_PER_DIGIT_OFFSET)) & \
                  H3_DIGIT_MASK)))

/**
 * Sets a value in the reserved space. Setting to non-zero may produce invalid
 * indexes.
 */
#define H3_SET_RESERVED_BITS(h3, v)            \
    (h3) = (((h3)&H3_RESERVED_MASK_NEGATIVE) | \
            (((uint64_t)(v)) << H3_RESERVED_OFFSET))

/**
 * Gets a value in the reserved space. Should always be zero for valid indexes.
 */
#define H3_GET_RESERVED_BITS(h3) \
    ((int)((((h3)&H3_RESERVED_MASK) >> H3_RESERVED_OFFSET)))

/**
 * Sets the resolution res digit of h3 to the integer digit (0-7)
 */
#define H3_SET_INDEX_DIGIT(h3, res, digit)                                  \
    (h3) = (((h3) & ~((H3_DIGIT_MASK                                        \
                       << ((MAX_H3_RES - (res)) * H3_PER_DIGIT_OFFSET)))) | \
            (((uint64_t)(digit))                                            \
             << ((MAX_H3_RES - (res)) * H3_PER_DIGIT_OFFSET)))

// H3-EXTENDED: Group C macros for ext bits 64-127 (playbook §3, validated by
// POC-1, 685/685 assertions ASAN+UBSAN clean). Copied verbatim from
// poc1_bit_layout.c. MAX_H3_EXT_RES is defined in constants.h (Group C #1).

/* C2: H3_EXT_FLAG_OFFSET — bit position of ext flag */
#define H3_EXT_FLAG_OFFSET 64

/* C3: H3_EXT_FLAG_MASK — single-bit mask at position 64 */
#define H3_EXT_FLAG_MASK ((__uint128_t)1 << 64)

/* C4: H3_EXT_DIGITS_OFFSET — bit position of ext digit 16 */
#define H3_EXT_DIGITS_OFFSET 65

/* C5: H3_EXT_DIGITS_MASK — 7 ext digits × 3 bits = 21 bits starting at bit 65 */
#define H3_EXT_DIGITS_MASK ((((__uint128_t)1 << 21) - 1) << 65)

/* C6: H3_INIT_EXT — init pattern with ext flag set, all digits (1-22) = sentinel 7.
 *     Low 64 bits = H3_INIT (digits 1-15 = 7).
 *     Bit 64 = 1 (ext flag).
 *     Bits 65-85 = all 1s (ext digits 16-22 = 7).
 *     Bits 86-127 = 0 (reserved). */
#define H3_INIT_EXT (H3_INIT | H3_EXT_FLAG_MASK | H3_EXT_DIGITS_MASK)

/* C7: H3_GET_EXT_FLAG — returns 0 or 1 */
#define H3_GET_EXT_FLAG(h) \
    ((int)(((h) & H3_EXT_FLAG_MASK) >> H3_EXT_FLAG_OFFSET))

/* C8: H3_SET_EXT_FLAG */
#define H3_SET_EXT_FLAG(h, v)             \
    (h) = (((h) & ~H3_EXT_FLAG_MASK) |    \
           (((__uint128_t)(v)) << H3_EXT_FLAG_OFFSET))

/* C9: H3_GET_EFFECTIVE_RESOLUTION — returns 0-22 */
#define H3_GET_EFFECTIVE_RESOLUTION(h) \
    (H3_GET_RESOLUTION(h) + (H3_GET_EXT_FLAG(h) << 4))

/* C10: H3_SET_EFFECTIVE_RESOLUTION — atomic write of stock-res field + ext flag.
 *      GCC statement-expression for single evaluation of `res` (POC-1 PF-19). */
#define H3_SET_EFFECTIVE_RESOLUTION(h, res)         \
    ({                                              \
        int _r = (res);                             \
        H3_SET_RESOLUTION((h), (_r) & 0xF);         \
        H3_SET_EXT_FLAG((h), (_r) >= 16 ? 1 : 0);   \
    })

/* C11: H3_GET_EXT_INDEX_DIGIT — UB if res not in [16, 22]. Caller's responsibility. */
#define H3_GET_EXT_INDEX_DIGIT(h, res)                                       \
    ((int)(((h) >> (H3_EXT_DIGITS_OFFSET + ((res) - 16) * 3)) & H3_DIGIT_MASK))

/* C12: H3_SET_EXT_INDEX_DIGIT — UB if res not in [16, 22]. */
#define H3_SET_EXT_INDEX_DIGIT(h, res, digit)                                       \
    (h) = (((h) & ~((__uint128_t)H3_DIGIT_MASK                                      \
                    << (H3_EXT_DIGITS_OFFSET + ((res) - 16) * 3))) |                \
           (((__uint128_t)(digit))                                                  \
            << (H3_EXT_DIGITS_OFFSET + ((res) - 16) * 3)))

/* C13: H3_GET_DIGIT_AT_RES — dispatching getter. Use when res is unknown-range.
 *      Stock branch arg masked with MAX_H3_RES so gcc does not constant-fold
 *      a negative shift count when callers pass a literal ext-res (>= 16).
 *      The mask is a no-op for res in [0, 15] and unreachable for res > 15. */
#define H3_GET_DIGIT_AT_RES(h, res)                                          \
    ((res) <= MAX_H3_RES ? H3_GET_INDEX_DIGIT((h), ((res) & MAX_H3_RES))     \
                         : H3_GET_EXT_INDEX_DIGIT((h), (res)))

/* C14: H3_SET_DIGIT_AT_RES — dispatching setter. Replaces H3_SET_INDEX_DIGIT
 *      at every site whose res argument may exceed 15 at runtime.
 *      Stock branch arg masked with MAX_H3_RES (see C13 rationale). */
#define H3_SET_DIGIT_AT_RES(h, res, digit)                                \
    do {                                                                  \
        if ((res) <= MAX_H3_RES)                                          \
            H3_SET_INDEX_DIGIT((h), ((res) & MAX_H3_RES), (digit));       \
        else                                                              \
            H3_SET_EXT_INDEX_DIGIT((h), (res), (digit));                  \
    } while (0)

// H3-EXTENDED: gate A1 — sizeof(H3Index) must be 16 bytes (128-bit).
_Static_assert(sizeof(H3Index) == 16,
               "H3-Extended: H3Index must be 16 bytes (__uint128_t)");

void setH3Index(H3Index *h, int res, int baseCell, Direction initDigit);
int isResolutionClassIII(int r);

// Internal functions

int _h3ToFaceIjkWithInitializedFijk(H3Index h, FaceIJK *fijk);
H3Error _h3ToFaceIjk(H3Index h, FaceIJK *fijk);
H3Index _faceIjkToH3(const FaceIJK *fijk, int res);
Direction _h3LeadingNonZeroDigit(H3Index h);
H3Index _h3RotatePent60ccw(H3Index h);
H3Index _h3RotatePent60cw(H3Index h);
H3Index _h3Rotate60ccw(H3Index h);
H3Index _h3Rotate60cw(H3Index h);
DECLSPEC H3Index _zeroIndexDigits(H3Index h, int start, int end);

H3Error vec3ToCell(const Vec3d *v, int res, H3Index *out);
H3Error cellToVec3(H3Index h3, Vec3d *v);

#endif
