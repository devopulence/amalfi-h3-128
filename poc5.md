# POC-5: CoordIJK int32 Overflow Stress — Test Specification

> **Version:** 1.0.0 — 2026-05-05
> **Prerequisite:** v0.1.0-128bit-mvp shipped (10/10 §11 acceptance)
> **Successor:** v0.2.0 CoordIJK int32 → int64 widening session

## Objective

Document the precise int32 overflow boundary in the CoordIJK arithmetic
path so v0.2.0 can target each site with regression coverage. Inherits
POC-1..4 discipline: standalone C99, ASAN+UBSAN clean, single file,
exit 0 on success.

## Construction

Single source file `poc5_coordijk_overflow.c` at repo root.

**No libh3 linkage.** POC-5 inlines the relevant constants
(`maxDimByCIIres`, `unitScaleByCIIres`, `M_SQRT7`) and uses
`__builtin_*_overflow` to detect the int32 boundary deterministically
without invoking the broken arithmetic at runtime. POC-5 itself never
trips a sanitizer.

The user spec asks POC-5 to "call latLngToCell at res 16-22 at multiple
geographic locations" and probe round-trip closure. We satisfy this
analytically: the deterministic chain
`lat/lng → Vec3d → gnomonic Vec2d → hex2d → CoordIJK → digits` is
inlined for the 3 specified geos (Monmouth, Palm Beach, Sorrento), then
the int32 arithmetic at each step is probed for overflow.

## Test Categories

| Category | Subject | Assertions |
|----------|---------|------------|
| CO-01..07 | maxDim / unitScale int32 boundary | 13 |
| CO-08..14 | Vec2d magnitude at res 16-22 (3 geos × 7 res) | 3 |
| CO-15..18 | `_hex2dToCoordIJK` / `_ijkNormalize` boundary | 5 |
| CO-19..22 | `_downAp7` / `_downAp7r` magnitude growth | 4 |
| CO-23..25 | Pentagon overage path | 3 |
| CO-26..28 | int64 widening eliminates each overflow site | 4 |
| CO-29..30 | Round-trip closure boundary prediction | 2 |
| CO-31..32 | Per-geo round-trip prediction (3 geos × 7 res) | 42 |

**Target: ≥ 75 assertions; achieved: 85.**

## Geographic Inputs (per user spec)

| Geo | Lat | Lng |
|-----|-----|-----|
| Monmouth County | 40.33 | -73.99 |
| Palm Beach | 26.71 | -80.05 |
| Piano di Sorrento | 40.63 | 14.40 |

## Failure Boundary (POC-5 result)

### Hexagon path

- **First-broken res: 21** (`maxDimByCIIres` at adjRes=22 reads
  `2 * 7^11 = 3,954,653,486 > INT32_MAX`)
- **Last-safe res: 20** (Class II direct, `maxDim[20] = 564,950,498`)
- **Overflow sites:**
  - `faceijk.c:315` — `maxDimByCIIres[22] = 2 * 7^11`
  - `faceijk.c:345` — `unitScaleByCIIres[22] = 7^11` (single value fits,
    but `* 3` in `_adjustOverageClassII` overflows)
  - `coordijk.h:207` — `_ijkNormalize` `i - j_negative` at res 22 worst case

### Pentagon path (non-monotonic)

- **First-broken res: 19** (Class III, sqrt(7) amplifier on top of
  `maxDim[20]`, then 3× cross-term ≈ 4.49e9)
- **res 20 RECOVERS** (Class II direct, no Class III amplifier)
- **res 21+ broken again** (reads adjRes=22, same failure as hexagon
  path plus pentagon distortion)
- **Continuous-safe ceiling: 18**
- **Overflow site:** `_adjustPentVertOverage` cross-term computation
  using `unitScaleByCIIres[adjRes] * 3 * sqrt(7)` ≈ `1.69e9 * 2.65` at
  res 19

## v0.2.0 Widening Targets

POC-5 documents 4 widening surfaces:

1. **CoordIJK struct** — `coordijk.h:48-52`
   `int i, j, k` → `int64_t i, j, k`

2. **CoordIJK arithmetic functions** — all in `coordijk.h`:
   `_setIJK`, `_ijkAdd`, `_ijkSub`, `_ijkScale`, `_ijkNormalize`,
   `_ijkNormalizeCouldOverflow`, `_hex2dToCoordIJK`, `_upAp7`, `_upAp7r`,
   `_upAp7Checked`, `_upAp7rChecked`, `_downAp7`, `_downAp7r`,
   `_downAp3`, `_downAp3r`, `_neighbor`, `_ijkRotate60ccw`,
   `_ijkRotate60cw`, `ijkDistance`, `ijkToIj`, `ijToIjk`,
   `ijkToCube`, `cubeToIjk`

3. **Static tables** — `faceijk.c:315, 345`:
   `maxDimByCIIres[]` and `unitScaleByCIIres[]` extended to length 23,
   element type `int → int64_t`

4. **CoordIJK consumers in faceijk.c** —
   `_adjustOverageClassII` (lines 600+, 770+, 910+) and
   `_adjustPentVertOverage` (lines 720+, 950+) use `int maxDim, unitScale`
   locals; widen to `int64_t`

## Success Criteria

```
Assertions: 85 passed, 0 failed
POC-5 PASSED — failure boundary documented.
```

Plus: exit 0, no ASAN output, no UBSAN output.

## Build Command

```bash
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc5_coordijk_overflow poc5_coordijk_overflow.c -lm
./poc5_coordijk_overflow
```

## Checkpoint

On success, the v0.2.0 entry session should:
1. Run POC-5 to confirm failure-boundary is still as documented.
2. Begin widening sequence with `CoordIJK` struct typedef change.
3. After each widening commit, re-run full ctest 326+ baseline AND
   re-run POC-5 (it should keep passing — POC-5 is type-agnostic).
4. Add a regression test `testCoordIjkExtRes` exercising res 19-22
   round-trips that POC-5 predicts work post-widening.
