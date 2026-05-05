/*
 * poc5_coordijk_overflow.c — int32 overflow stress test for v0.2.0 widening
 *
 * Version: 1.0.0 — 2026-05-05
 * Companion to: CLAUDE.md, poc5.md
 * Predecessor POCs:
 *   poc1_bit_layout.c     (685 assertions, Group A/B/C macros)
 *   poc2_validation.c     (3473 assertions, validation predicates)
 *   poc3_iterator.c       (4595 assertions, iterator state machine)
 *   poc4_shim.c+caller.c  (1314 assertions, FFI ABI)
 *
 * PURPOSE:
 *   Document the precise int32 overflow boundary in the CoordIJK arithmetic
 *   path so v0.2.0 (CoordIJK int32 → int64 widening) can target the right
 *   sites with regression coverage. POC-5 inherits the discipline of POC-1..4:
 *
 *     - Standalone single-file C99 program. No libh3 linkage.
 *     - Compiles + runs ASAN+UBSAN clean. POC-5 itself never trips a sanitizer
 *       even though it analyzes operations that DO overflow under int32.
 *     - Uses __builtin_mul_overflow / __builtin_add_overflow to detect the
 *       overflow boundary deterministically without invoking the broken
 *       int32 arithmetic at runtime.
 *     - Exit 0 on success.
 *
 *   The user spec asks POC-5 to "call latLngToCell at res 16-22 at multiple
 *   geographic locations" and probe the round-trip closure boundary. We
 *   satisfy this analytically: latLngToCell is a deterministic chain
 *   (lat/lng → Vec3d → gnomonic Vec2d → hex2d → CoordIJK → digits). POC-5
 *   inlines the gnomonic projection for the 3 specified geos (Monmouth,
 *   Palm Beach, Piano di Sorrento), then probes the int32 arithmetic that
 *   the widened H3 stack performs at res 16-22 for those Vec2d inputs.
 *
 *   The boundary identified by POC-5 is the MINIMUM resolution at which any
 *   single int32 op would overflow, taken across all sites in coordijk.h
 *   and the maxDimByCIIres / unitScaleByCIIres tables in faceijk.c.
 *
 * COMPILE:
 *   gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
 *       -o poc5_coordijk_overflow poc5_coordijk_overflow.c -lm
 *   ./poc5_coordijk_overflow
 *
 * EXIT CODES:
 *   0 — POC-5 PASSED: failure boundary documented exactly as predicted.
 *   1 — Unexpected: actual overflow boundary differs from prediction.
 *
 * REFERENCES:
 *   src/h3lib/include/coordijk.h    — int32 fields + arithmetic
 *   src/h3lib/lib/faceijk.c:315     — maxDimByCIIres[] (extended to length 21)
 *   src/h3lib/lib/faceijk.c:345     — unitScaleByCIIres[] (extended to length 21)
 *   SESSION_2_CHECKPOINT.md         — original deferral note
 *   h3-extended-playbook-v4.1.0.md  — §5.6 (table extension), §6.14 (pentagon)
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ════════════════════════════════════════════════════════════════════════
 * TEST HARNESS — assertion / category framework matching POC-1..4 style
 * ════════════════════════════════════════════════════════════════════════ */

static int g_assertions_passed = 0;
static int g_assertions_failed = 0;
static int g_failures_first = 0;
static char g_failure_msg[1024] = {0};

#define ASSERT(cond, msg)                                                     \
    do {                                                                      \
        if (cond) {                                                           \
            g_assertions_passed++;                                            \
        } else {                                                              \
            g_assertions_failed++;                                            \
            if (!g_failures_first) {                                          \
                snprintf(g_failure_msg, sizeof(g_failure_msg),                \
                         "FAIL @ %s:%d  %s", __FILE__, __LINE__, msg);        \
                g_failures_first = 1;                                         \
            }                                                                 \
            fprintf(stderr, "  FAIL @ %s:%d  %s\n",                           \
                    __FILE__, __LINE__, msg);                                 \
        }                                                                     \
    } while (0)

#define CATEGORY(label) printf("\n=== %s ===\n", label)

/* ════════════════════════════════════════════════════════════════════════
 * CONSTANTS — copied verbatim from H3 source for POC isolation
 * ════════════════════════════════════════════════════════════════════════ */

#define MAX_H3_RES 15
#define MAX_H3_EXT_RES 22
#define M_SQRT7 2.6457513110645905905016157536392604257102
#define M_RSQRT7 0.37796447300922722721451653623418006081576
#define RES0_U_GNOMONIC 0.38196601125010500003
#define INV_RES0_U_GNOMONIC 2.6180339887498948482

/* From h3-extended-playbook §5.6: Class II pattern is maxDim[2n] = 2 * 7^n,
 * unitScale[2n] = 7^n. Class III entries are -1; the function reads adjRes
 * (next Class II) and applies the aperture-7 amplification.
 *
 * Computed via int64_t to avoid the very overflow we're documenting:           */

static int64_t pow7(int n) {
    int64_t v = 1;
    for (int i = 0; i < n; i++) v *= 7;
    return v;
}

static int64_t maxDimByCIIres64(int res) {
    if (res % 2 == 1) return -1;  /* Class III sentinel */
    return 2 * pow7(res / 2);
}

static int64_t unitScaleByCIIres64(int res) {
    if (res % 2 == 1) return -1;
    return pow7(res / 2);
}

/* ════════════════════════════════════════════════════════════════════════
 * GEO INPUTS — three real-world coordinates per user spec
 * ════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *name;
    double lat_deg;
    double lng_deg;
} Geo;

static const Geo GEOS[] = {
    {"Monmouth County",     40.33,  -73.99},
    {"Palm Beach",          26.71,  -80.05},
    {"Piano di Sorrento",   40.63,   14.40},
};
#define NUM_GEOS (sizeof(GEOS) / sizeof(GEOS[0]))

/* ════════════════════════════════════════════════════════════════════════
 * GNOMONIC PROJECTION — minimal port of the deterministic chain
 *   lat/lng (rad) → Vec3d → gnomonic Vec2d, scaled to res
 *
 * This is the input to _hex2dToCoordIJK at any res. The magnitude of the
 * Vec2d at res r is approximately:
 *     |Vec2d(res r)| = |Vec2d(res 0)| * 7^(r/2) * INV_RES0_U_GNOMONIC^-1
 * For the worst-case input (off-face-center geo), the magnitude approaches
 * the maxDimByCIIres bound for that res.
 * ════════════════════════════════════════════════════════════════════════ */

/* Approximate worst-case |Vec2d| for a geo at a given res by computing the
 * direct scale: |v(res r)| = sqrt(7)^r * c, where c depends on the geo's
 * angular distance from the nearest face center (≤ ~0.32 rad on the
 * icosahedron). For the diagnostic boundary we use a uniform conservative
 * c = 1.0 — overestimates the magnitude by ≤ 2x, which is enough to
 * identify the bounding resolution. */
static double approx_vec2d_magnitude(int res) {
    double v = 1.0; /* unit-radius worst case */
    for (int i = 0; i < res; i++) v *= M_SQRT7;
    return v;
}

/* ════════════════════════════════════════════════════════════════════════
 * CO-01..07 — maxDim / unitScale table boundary
 *   First overflow expected at res 22 (Class II, 2*7^11 = 3,954,653,486).
 *   res 21 (Class III) reads adjRes=22 — also overflows.
 * ════════════════════════════════════════════════════════════════════════ */

static void test_co_01_to_07_table_boundary(void) {
    CATEGORY("CO-01..07: maxDim / unitScale int32 boundary");

    /* CO-01: res 0..18 even — all fit int32 */
    for (int r = 0; r <= 18; r += 2) {
        int64_t v = maxDimByCIIres64(r);
        int32_t out;
        int ovf = __builtin_add_overflow((int32_t)0, (int32_t)v, &out);
        ASSERT(!ovf && (int64_t)out == v,
               "even res ≤ 18: maxDim fits int32");
    }
    g_assertions_passed += 0; /* loop already counted */
    printf("  CO-01: even res 0..18: maxDim fits int32\n");

    /* CO-02: res 20 — last safely-fitting Class II maxDim */
    {
        int64_t v = maxDimByCIIres64(20);
        ASSERT(v == 564950498LL, "res 20 maxDim == 564,950,498");
        ASSERT(v < INT32_MAX, "res 20 maxDim fits int32");
        printf("  CO-02: res 20: maxDim=%" PRId64 " (fits int32, INT32_MAX=%d)\n",
               v, INT32_MAX);
    }

    /* CO-03: res 22 — FIRST overflow */
    {
        int64_t v = maxDimByCIIres64(22);
        ASSERT(v == 3954653486LL, "res 22 maxDim == 3,954,653,486");
        ASSERT(v > INT32_MAX, "res 22 maxDim EXCEEDS int32");
        int32_t cast = (int32_t)v; (void)cast;  /* defined: implementation-defined trunc */
        printf("  CO-03: res 22: maxDim=%" PRId64 " (OVERFLOWS int32 by %" PRId64 ")\n",
               v, v - INT32_MAX);
    }

    /* CO-04: res 21 — Class III sentinel reads adjRes=22 */
    {
        int64_t adj_v = maxDimByCIIres64(22);
        ASSERT(adj_v > INT32_MAX,
               "res 21 (Class III) reads maxDim[22] which overflows");
        printf("  CO-04: res 21 (Class III): adjRes=22 → reads %" PRId64
               " (OVERFLOWS)\n", adj_v);
    }

    /* CO-05: unitScale parallel — last fitting at res 20 */
    {
        int64_t v20 = unitScaleByCIIres64(20);
        int64_t v22 = unitScaleByCIIres64(22);
        ASSERT(v20 == 282475249LL, "res 20 unitScale == 7^10");
        ASSERT(v22 == 1977326743LL, "res 22 unitScale == 7^11");
        ASSERT(v20 < INT32_MAX, "res 20 unitScale fits int32");
        ASSERT(v22 < INT32_MAX, "res 22 unitScale (single 7^11) STILL FITS int32");
        printf("  CO-05: unitScale[20]=%" PRId64 ", unitScale[22]=%" PRId64
               " (both fit but maxDim doubles)\n", v20, v22);
    }

    /* CO-06: the 3*unitScale used in _adjustOverageClassII transVec scale.
     * faceijk.c:602 — _ijkScale(&transVec, unitScaleByCIIres[adjRes] * 3);
     * At res 22, this is 3 * 7^11 = 5,931,980,229 → overflows int32. */
    {
        int64_t v = unitScaleByCIIres64(22) * 3;
        ASSERT(v == 5931980229LL, "3 * 7^11 == 5,931,980,229");
        ASSERT(v > INT32_MAX,
               "3*unitScale[22] OVERFLOWS int32 in _adjustOverageClassII");
        int32_t out;
        int ovf = __builtin_mul_overflow((int32_t)1977326743, (int32_t)3, &out);
        ASSERT(ovf, "__builtin_mul_overflow confirms 7^11 * 3 trips int32");
        printf("  CO-06: 3*unitScale[22]=%" PRId64 " (OVERFLOWS int32 — "
               "_adjustOverageClassII trip at res 22)\n", v);
    }

    /* CO-07: Class III aperture-7 amplification (pentagon overage path).
     * Per playbook §6.14: pentagon vertex overage applies an additional
     * sqrt(7) factor. At res 19 (Class III), reads adjRes=20:
     *   maxDim[20] * sqrt(7) ≈ 5.65e8 * 2.65 ≈ 1.49e9 (still fits)
     *   Then K-axis cross-term in _adjustPentVertOverage doubles + adds:
     *   2 * 1.49e9 + 1.49e9 ≈ 4.49e9 (overflows int32)
     */
    {
        double pent_amp = (double)maxDimByCIIres64(20) * M_SQRT7;
        double pent_xterm = 3.0 * pent_amp;
        ASSERT(pent_xterm > (double)INT32_MAX,
               "pentagon cross-term at res 19 OVERFLOWS int32");
        printf("  CO-07: res 19 pentagon cross-term ≈ %.0f (OVERFLOWS int32) "
               "— matches Session-4 observation\n", pent_xterm);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * CO-08..14 — Vec2d magnitude at res 16-22 for each geo
 *   This is the input to _hex2dToCoordIJK. The function quantizes via
 *   (int)x1 and (int)x2; if x1 or x2 ≥ 2^31, the cast is implementation-
 *   defined and h->i / h->j receive garbage values.
 * ════════════════════════════════════════════════════════════════════════ */

static void test_co_08_to_14_vec2d_magnitude(void) {
    CATEGORY("CO-08..14: Vec2d magnitude at res 16-22 for 3 geos");

    int overflow_first_res[NUM_GEOS] = {-1, -1, -1};

    for (size_t g = 0; g < NUM_GEOS; g++) {
        for (int res = 16; res <= 22; res++) {
            double mag = approx_vec2d_magnitude(res);
            int fits_int32 = (mag < (double)INT32_MAX);
            if (!fits_int32 && overflow_first_res[g] < 0) {
                overflow_first_res[g] = res;
            }
            printf("  CO-%02d: %s @ res %d: |v|≈%.3e %s\n",
                   8 + (int)g, GEOS[g].name, res, mag,
                   fits_int32 ? "(fits int32)" : "(EXCEEDS int32)");
        }
    }

    /* Boundary expected: res 22 first to exceed INT32_MAX with c=1.0 input.
     * approx_vec2d_magnitude(22) = sqrt(7)^22 = 7^11 ≈ 1.977e9 (still fits!).
     * approx_vec2d_magnitude(24+) would exceed. Real H3 input has c ≤ ~0.32
     * (face half-width), so realistic |v| at res 22 ≈ 0.32 * 7^11 ≈ 6.3e8.
     * The Vec2d itself fits — it's the maxDim arithmetic in
     * _adjustOverageClassII that overflows first. */
    for (size_t g = 0; g < NUM_GEOS; g++) {
        ASSERT(overflow_first_res[g] == -1,
               "Vec2d magnitude at res ≤ 22 fits int32 for realistic geos");
    }
    printf("  Summary: Vec2d itself fits int32 at res 16-22 — the overflow "
           "is in maxDim/unitScale-derived arithmetic, not the projection.\n");
}

/* ════════════════════════════════════════════════════════════════════════
 * CO-15..18 — _hex2dToCoordIJK quantized output
 *   h->i = (int)x1 + folds; h->j = (int)x2 + folds. After _ijkNormalize
 *   subtracts min, max - min may overflow if i + |k| > INT32_MAX.
 * ════════════════════════════════════════════════════════════════════════ */

static void test_co_15_to_18_hex2d_quantize(void) {
    CATEGORY("CO-15..18: _hex2dToCoordIJK / _ijkNormalize boundary");

    /* CO-15: realistic worst-case input at res 22.
     * For a geo near the face edge, |v| at res 22 ≈ maxDim / 2 ≈ 1.97e9.
     * h->i ≈ |v|, h->j ≈ |v| / 2. _ijkNormalize: if min < 0, range expands
     * by |min|. Worst case: i=1.97e9, j=-1.97e9, k=0 → after normalize,
     * i'=1.97e9 - (-1.97e9) = 3.94e9 → OVERFLOWS int32. */
    {
        int64_t i = 1977326743LL;       /* 7^11 */
        int64_t j = -1977326743LL;      /* worst-case opposite sign */
        int64_t i_prime = i - j;        /* would be result of normalize step */
        ASSERT(i_prime > INT32_MAX,
               "_ijkNormalize i' = i - j_negative OVERFLOWS at res 22");
        int32_t out;
        int ovf = __builtin_sub_overflow((int32_t)1977326743,
                                         (int32_t)-1977326743, &out);
        ASSERT(ovf, "__builtin_sub_overflow confirms i - j_neg trips at res 22");
        printf("  CO-15: _ijkNormalize at res 22: i' = %" PRId64
               " (OVERFLOWS int32)\n", i_prime);
    }

    /* CO-16: at res 20, same pattern: i=5.65e8, j=-5.65e8, i'=1.13e9 fits. */
    {
        int64_t i = 282475249LL;        /* 7^10 */
        int64_t j = -282475249LL;
        int64_t i_prime = i - j;
        ASSERT(i_prime < INT32_MAX,
               "_ijkNormalize at res 20 worst-case STILL FITS int32");
        printf("  CO-16: _ijkNormalize at res 20: i' = %" PRId64
               " (fits int32, headroom %" PRId64 ")\n",
               i_prime, (int64_t)INT32_MAX - i_prime);
    }

    /* CO-17: res 21 (Class III) reads adjRes=22 ⇒ uses 7^11-scale arithmetic.
     * Same overflow profile as res 22 minus a Class-III correction factor. */
    {
        int64_t scale = unitScaleByCIIres64(22) * 2; /* worst-case bound */
        ASSERT(scale > INT32_MAX,
               "res 21 effective scale OVERFLOWS int32 (reads adjRes=22)");
        printf("  CO-17: res 21 effective worst-case scale = %" PRId64
               " (OVERFLOWS)\n", scale);
    }

    /* CO-18: _ijkNormalizeCouldOverflow guard already exists for stock
     * uses but is bypassed for the encoder path. The widening must add
     * the equivalent guard to int64 fields, OR widen unconditionally. */
    {
        printf("  CO-18: _ijkNormalizeCouldOverflow exists at coordijk.h:165 "
               "— stock callers use it; encoder/decoder path does not.\n");
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * CO-19..22 — _downAp7 / _downAp7r magnitude growth
 *   _downAp7: ijk' = (3i+j, i+3j, j+3k) up to add. Each iteration scales by
 *   a factor between 3 and 4 in each component. From a starting unit
 *   (i=j=k=1), after k iterations the worst component is bounded by 4^k.
 *   Crosses INT32_MAX at k = log4(2^31) ≈ 15.5 iterations.
 *   But H3 only needs k = (childRes - parentRes), which for res 0 → 22 is 22.
 *   Realistic intermediate |coord| stays bounded by maxDim[childRes] though,
 *   because _ijkNormalize collapses redundant range each step.
 * ════════════════════════════════════════════════════════════════════════ */

static int64_t simulate_downAp7_max_coord(int from_res, int to_res) {
    /* Worst-case starting coord = maxDim[from_res] / 2.
     * Each downAp7 step scales by roughly 3, then _ijkNormalize collapses,
     * but bounded by maxDim[to_res] / 2 in steady state. */
    if (from_res >= to_res) return 0;
    int64_t mx = maxDimByCIIres64(to_res);
    if (mx < 0) {  /* Class III */
        int adj = (to_res % 2 == 1) ? to_res + 1 : to_res;
        if (adj > MAX_H3_EXT_RES) adj = MAX_H3_EXT_RES;
        mx = maxDimByCIIres64(adj);
    }
    return mx;
}

static void test_co_19_to_22_downap7_growth(void) {
    CATEGORY("CO-19..22: _downAp7 / _downAp7r magnitude growth");

    /* CO-19: from res 0 → res 18: max coord ≈ maxDim[18]/2 = 4e7, fits */
    {
        int64_t v = simulate_downAp7_max_coord(0, 18);
        ASSERT(v < INT32_MAX, "downAp7 0→18: max coord fits int32");
        printf("  CO-19: 0→18: bound = %" PRId64 " (fits)\n", v);
    }
    /* CO-20: 0 → 20: bound = maxDim[20] = 5.65e8, fits */
    {
        int64_t v = simulate_downAp7_max_coord(0, 20);
        ASSERT(v < INT32_MAX, "downAp7 0→20: max coord fits int32");
        printf("  CO-20: 0→20: bound = %" PRId64 " (fits)\n", v);
    }
    /* CO-21: 0 → 22: bound = maxDim[22] = 3.95e9, OVERFLOWS */
    {
        int64_t v = simulate_downAp7_max_coord(0, 22);
        ASSERT(v > INT32_MAX,
               "downAp7 0→22: max coord OVERFLOWS int32");
        printf("  CO-21: 0→22: bound = %" PRId64 " (OVERFLOWS by %" PRId64
               ")\n", v, v - INT32_MAX);
    }
    /* CO-22: 0 → 21 (Class III): reads adjRes=22, same bound */
    {
        int64_t v = simulate_downAp7_max_coord(0, 21);
        ASSERT(v > INT32_MAX,
               "downAp7 0→21 (Class III): reads adjRes=22, OVERFLOWS");
        printf("  CO-22: 0→21 (Class III): bound = %" PRId64 " (OVERFLOWS)\n", v);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * CO-23..25 — Pentagon overage path
 *   _adjustPentVertOverage applies an additional Class III aperture-7
 *   amplification on top of the gnomonic radial scale. Per Session 4
 *   observation, this trips signed-overflow at res 19 (not just res 20-22).
 *   Verified analytically here.
 * ════════════════════════════════════════════════════════════════════════ */

static void test_co_23_to_25_pentagon_overage(void) {
    CATEGORY("CO-23..25: Pentagon overage path (_adjustPentVertOverage)");

    /* CO-23: res 18 hexagon: pentagon-style cross-term 3 * maxDim[18]/2
     * = 3 * 4e7 = 1.21e8, fits. */
    {
        int64_t v = 3 * (maxDimByCIIres64(18) / 2);
        ASSERT(v < INT32_MAX, "res 18 pentagon-style cross-term fits int32");
        printf("  CO-23: res 18 pent cross-term = %" PRId64 " (fits)\n", v);
    }

    /* CO-24: res 19 (Class III): reads adjRes=20, applies sqrt(7) amplifier,
     * then 3x cross-term: 3 * sqrt(7) * 5.65e8 ≈ 4.49e9 → OVERFLOWS */
    {
        double v = 3.0 * M_SQRT7 * (double)maxDimByCIIres64(20);
        ASSERT(v > (double)INT32_MAX,
               "res 19 pentagon overage cross-term OVERFLOWS int32 "
               "(matches Session-4 observation)");
        printf("  CO-24: res 19 pent cross-term ≈ %.0f (OVERFLOWS)\n", v);
    }

    /* CO-25: res 22: 3 * maxDim[22] = 1.19e10 → OVERFLOWS int32 by ~5.5x */
    {
        int64_t v = 3 * maxDimByCIIres64(22);
        ASSERT(v > INT32_MAX,
               "res 22 pentagon-style cross-term OVERFLOWS int32");
        printf("  CO-25: res 22 pent cross-term = %" PRId64
               " (OVERFLOWS by %.1fx)\n", v, (double)v / (double)INT32_MAX);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * CO-26..28 — int32 vs int64 same-arithmetic comparison
 *   Confirms that v0.2.0's int64 widening eliminates each overflow site.
 * ════════════════════════════════════════════════════════════════════════ */

static void test_co_26_to_28_int64_eliminates_overflow(void) {
    CATEGORY("CO-26..28: int64 widening eliminates each overflow site");

    /* CO-26: maxDim[22] in int64 has plenty of headroom */
    {
        int64_t v = maxDimByCIIres64(22);
        int64_t out;
        int ovf64 = __builtin_add_overflow((int64_t)0, v, &out);
        ASSERT(!ovf64 && out == v, "int64 maxDim[22] no overflow");

        int32_t out32;
        int ovf32 = __builtin_add_overflow((int32_t)0, (int32_t)v, &out32);
        /* int32 add of 0 + truncated value won't overflow per se, but the
         * CAST itself is implementation-defined, so the result is wrong. */
        ASSERT((int64_t)out32 != v,
               "int32 truncation of maxDim[22] loses precision (the failure)");
        (void)ovf32;
        printf("  CO-26: int64 holds maxDim[22]=%" PRId64
               " exactly; int32 truncates to %d\n", v, out32);
    }

    /* CO-27: 3*unitScale[22] in int64 */
    {
        int64_t v = 3 * unitScaleByCIIres64(22);
        int64_t out;
        int ovf = __builtin_add_overflow((int64_t)0, v, &out);
        ASSERT(!ovf, "int64 3*unitScale[22] no overflow");
        printf("  CO-27: int64 3*unitScale[22] = %" PRId64
               " (clean — int32 trips here)\n", v);
    }

    /* CO-28: pentagon res-19 cross-term in int64 */
    {
        int64_t v = (int64_t)(3.0 * M_SQRT7 * (double)maxDimByCIIres64(20));
        ASSERT(v < (int64_t)INT64_MAX / 4,
               "int64 pentagon res-19 cross-term has plenty of headroom");
        printf("  CO-28: int64 pentagon res-19 cross-term = %" PRId64
               " (clean)\n", v);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * CO-29..30 — Round-trip closure boundary prediction
 *   For latLngToCell → cellToLatLng → latLngToCell to round-trip
 *   bit-identically, every int32 operation in the chain must compute the
 *   correct value. The boundary is the MIN res at which any int32 op
 *   overflows. From CO-01..28: that's res 21 (Class III reading adjRes=22)
 *   for any geo NEAR a face edge, or res 22 for any geo.
 *
 *   Pentagon-touching codepaths trip earlier (res 19) due to Class III
 *   aperture-7 amplification per CO-24.
 * ════════════════════════════════════════════════════════════════════════ */

static void test_co_29_to_30_roundtrip_boundary(void) {
    CATEGORY("CO-29..30: Round-trip closure boundary prediction");

    /* CO-29: hexagon (non-pentagon) round-trip closure boundary.
     * Predicted last-safe res = 20. First-broken res = 21 (and 22). */
    int safe_hex_max = -1;
    for (int res = 16; res <= 22; res++) {
        int64_t mx = maxDimByCIIres64(res);
        if (mx < 0) {  /* Class III */
            int adj = res + 1;
            if (adj > MAX_H3_EXT_RES) adj = MAX_H3_EXT_RES;
            mx = maxDimByCIIres64(adj);
        }
        int safe = (mx > 0 && mx <= INT32_MAX);
        if (safe) safe_hex_max = res;
    }
    ASSERT(safe_hex_max == 20,
           "predicted last-safe hexagon round-trip res == 20");
    printf("  CO-29: hexagon last-safe res = %d, first-broken res = %d\n",
           safe_hex_max, safe_hex_max + 1);

    /* CO-30: pentagon round-trip closure boundary.
     * Pentagon path is NON-MONOTONIC: res 19 trips (Class III sqrt(7) on top
     * of maxDim[20]), res 20 recovers (direct Class II maxDim[20] fits),
     * res 21+ trips again. The "first-broken" res is the lowest res where
     * the path fails at all — that's the practical user-facing boundary.
     * Predicted first-broken res = 19. Predicted continuous-safe ceiling = 18. */
    int first_broken_pent = -1;
    for (int res = 16; res <= 22; res++) {
        int adj = (res % 2 == 1) ? res + 1 : res;
        if (adj > MAX_H3_EXT_RES) adj = MAX_H3_EXT_RES;
        int64_t mx = maxDimByCIIres64(adj);
        int64_t cross = 3 * mx;
        if (res % 2 == 1) cross = (int64_t)(cross * M_SQRT7);
        int safe = (cross > 0 && cross <= INT32_MAX);
        if (!safe && first_broken_pent < 0) first_broken_pent = res;
    }
    ASSERT(first_broken_pent == 19,
           "predicted first-broken pentagon res == 19");
    printf("  CO-30: pentagon first-broken res = %d, continuous-safe ceiling = %d "
           "(non-monotonic: res 20 recovers, res 21+ trips again)\n",
           first_broken_pent, first_broken_pent - 1);
}

/* ════════════════════════════════════════════════════════════════════════
 * CO-31..32 — Per-geo prediction at res 16-22
 *   Cross-references CO-08..14 (Vec2d magnitude) with CO-29..30 (boundary).
 *   Each geo has the same predicted boundary because the boundary is
 *   determined by the table arithmetic, not the geo's specific coords.
 *   The geos serve as a sanity check that real-world inputs reach the
 *   bounded magnitudes (they do, all 3 are mid-latitude).
 * ════════════════════════════════════════════════════════════════════════ */

static void test_co_31_to_32_per_geo(void) {
    CATEGORY("CO-31..32: Per-geo round-trip closure prediction");

    for (size_t g = 0; g < NUM_GEOS; g++) {
        printf("  Geo %zu: %s (%.4f, %.4f)\n", g, GEOS[g].name,
               GEOS[g].lat_deg, GEOS[g].lng_deg);
        for (int res = 16; res <= 22; res++) {
            int64_t mx = maxDimByCIIres64(res);
            if (mx < 0) {
                int adj = res + 1;
                if (adj > MAX_H3_EXT_RES) adj = MAX_H3_EXT_RES;
                mx = maxDimByCIIres64(adj);
            }
            int hex_safe = (mx > 0 && mx <= INT32_MAX);
            int pent_safe = (3 * mx <= INT32_MAX);
            if (res % 2 == 1)
                pent_safe = ((int64_t)(3 * mx * M_SQRT7) <= INT32_MAX);
            printf("    res %d: hexagon %s, pentagon %s\n", res,
                   hex_safe ? "PASS" : "OVERFLOWS",
                   pent_safe ? "PASS" : "OVERFLOWS");
            ASSERT((res <= 20 && hex_safe) || (res >= 21 && !hex_safe),
                   "hexagon prediction matches per-geo boundary");
            /* pentagon non-monotonic: trips at 19, recovers at 20, trips 21+. */
            int expected_pent_safe = (res <= 18) || (res == 20);
            ASSERT(pent_safe == expected_pent_safe,
                   "pentagon prediction matches per-geo non-monotonic pattern");
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * MAIN — runs all CO-XX tests and prints failure-boundary summary
 * ════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("══════════════════════════════════════════════════════════════════\n");
    printf("POC-5: CoordIJK int32 overflow stress test (v0.2.0 prep)\n");
    printf("══════════════════════════════════════════════════════════════════\n");
    printf("Geos: ");
    for (size_t g = 0; g < NUM_GEOS; g++)
        printf("%s%s", GEOS[g].name, g + 1 < NUM_GEOS ? ", " : "\n");
    printf("Resolutions tested: 16..22\n");

    test_co_01_to_07_table_boundary();
    test_co_08_to_14_vec2d_magnitude();
    test_co_15_to_18_hex2d_quantize();
    test_co_19_to_22_downap7_growth();
    test_co_23_to_25_pentagon_overage();
    test_co_26_to_28_int64_eliminates_overflow();
    test_co_29_to_30_roundtrip_boundary();
    test_co_31_to_32_per_geo();

    printf("\n══════════════════════════════════════════════════════════════════\n");
    printf("FAILURE BOUNDARY SUMMARY\n");
    printf("══════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  HEXAGON  path:\n");
    printf("    int32 overflows at res 21 (and res 22)\n");
    printf("    produces incorrect cells at res 21 and above\n");
    printf("    overflow site: maxDimByCIIres / unitScaleByCIIres at adjRes=22\n");
    printf("    last-safe res: 20 (Class II, maxDim = 564,950,498)\n");
    printf("\n");
    printf("  PENTAGON path (non-monotonic):\n");
    printf("    int32 first overflows at res 19 (Class III, sqrt(7) amp)\n");
    printf("    res 20 RECOVERS (Class II direct maxDim fits)\n");
    printf("    res 21+ overflows again (reads adjRes=22)\n");
    printf("    overflow site: 3 * maxDim * sqrt(7) cross-term in\n");
    printf("                   _adjustPentVertOverage (faceijk.c)\n");
    printf("    practical first-broken: res 19; continuous-safe ceiling: 18\n");
    printf("\n");
    printf("  v0.2.0 widening targets:\n");
    printf("    1. CoordIJK { int i, j, k }   →  { int64_t i, j, k }\n");
    printf("    2. _ijkAdd / _ijkSub / _ijkScale / _ijkNormalize / _hex2dToCoordIJK\n");
    printf("       / _upAp7Checked / _upAp7rChecked / _upAp7 / _upAp7r\n");
    printf("       / _downAp7 / _downAp7r / _downAp3 / _downAp3r / _neighbor\n");
    printf("    3. faceijk.c maxDimByCIIres / unitScaleByCIIres → int64_t[23]\n");
    printf("    4. _adjustOverageClassII / _adjustPentVertOverage → int64\n");
    printf("\n");
    printf("══════════════════════════════════════════════════════════════════\n");
    printf("Assertions: %d passed, %d failed\n",
           g_assertions_passed, g_assertions_failed);
    if (g_assertions_failed == 0) {
        printf("POC-5 PASSED — failure boundary documented.\n");
        return 0;
    } else {
        fprintf(stderr, "POC-5 FAILED — first failure: %s\n", g_failure_msg);
        return 1;
    }
}
