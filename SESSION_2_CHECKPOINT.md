# Session 2 Checkpoint

- **Date:** 2026-05-03 (Apple clang 21.0.0 / Darwin arm64)
- **Branch:** `feat/h3-128-mvp`
- **Commit:** `63cb7b6b` (phase-d3: add testAccessorExt — D3-G1 gate + accessor regression)
- **Stock ctest:** 316/316 PASS (byte-identical baseline preserved)
- **Ext tests added this session:** 4 suites — `testEncoderExt`, `testDecoderExt`, `testRotationExt`, `testAccessorExt`
- **Total ctest:** 321/321 PASS under ASAN+UBSAN (was 317 at Session 1 end; +4 ext suites)
- **ASAN:** clean
- **UBSAN:** clean

## Commits this session (17, in order)

| SHA | Subject |
|-----|---------|
| `a36f68f1` | phase-d1: widen setH3Index (Rule INIT + RW + DW) |
| `7bd3ef69` | phase-d1: widen _faceIjkToH3 (Rule INIT + RW + DW) |
| `569da6b2` | phase-d1: relax encoder guards to MAX_H3_EXT_RES (Rule GR) |
| `d15f6668` | phase-d1: add testEncoderExt — D1-G1 gate + encoder regression coverage |
| `16491e7f` | phase-d2: widen _h3ToFaceIjkWithInitializedFijk (Rule LB + DR) |
| `a98a2096` | phase-d2: widen _h3ToFaceIjk local res reads (Rule LB) |
| `c18b505f` | phase-d2: widen cellToVec3 res arg to _faceIjkToVec3 (Rule LB) |
| `e75e840f` | phase-d2/d6-prep: extend maxDimByCIIres + unitScaleByCIIres to length 21 |
| `d09221c8` | phase-d6: widen _h3LeadingNonZeroDigit (Rule LB + DR, trap §6.13) |
| `437278c0` | phase-d6: widen _h3Rotate60ccw + _h3Rotate60cw (Pattern 3, trap §6.12) |
| `bf22c42f` | phase-d6: widen _h3RotatePent60ccw + _h3RotatePent60cw (Pattern 3) |
| `f8c318ab` | phase-d2: add testDecoderExt — D2-G1 gate (1,000 res-19 round-trips) |
| `9fe7f529` | phase-d6: add testRotationExt — D6-G1 gate (six rotations 6-cycle) |
| `5a64861e` | phase-d3: widen getResolution to return effective res (Rule LB) |
| `5a1311ca` | phase-d3: widen getIndexDigit (Rule GR + DR) |
| `bdf36196` | phase-d3: widen constructCell (Rule GR + INIT + RW + DW) |
| `63cb7b6b` | phase-d3: add testAccessorExt — D3-G1 gate + accessor regression |

## Gates passed

### Phase D1 — Encoder
- **D1-G1** `latLngToCell` at res 19 produces ext cells with bit 64 set, effective res = 19, mode = H3_CELL_MODE: PASS — `testEncoderExt::d1g1_latLngToCell_res19_extFlagSet` (100 deterministic coords).
- D1 stock byte-identity: PASS — stock res 0-15 cells keep high 64 bits zero across `latLngToCell`, `vec3ToCell`, `setH3Index`.
- D1 vec3ToCell parallel path: PASS — `vec3ToCell` matches `latLngToCell` at res 19 byte-identical.
- D1 contract bound: PASS — res > MAX_H3_EXT_RES rejected with E_RES_DOMAIN.

### Phase D2 — Decoder
- **D2-G1** 1,000 res-19 cell-center round-trips bit-identical (`latLngToCell → cellToLatLng → latLngToCell` returns same cell): PASS — `testDecoderExt::d2g1_res19_cellCenterRoundTripIdentical`.
- D2 stock guard: PASS — stock res 0-15 cell-center round-trip stays bit-identical.

### Phase D3 — Accessors
- **D3-G1** `getResolution` returns 16-22 for ext cells: PASS — `testAccessorExt::d3g1_getResolution_returnsEffectiveResForExtCell`.
- D3 stock byte-identity: PASS — `getResolution` unchanged for stock cells.
- D3 `getIndexDigit` ext dispatch: PASS — digits 1-19 = initDigit, digits 20-22 = sentinel 7.
- D3 `getIndexDigit` bound: PASS — res > MAX_H3_EXT_RES rejected.
- D3 `constructCell` ext round-trip: PASS — 16 ≤ res ≤ 22, all base cells, pseudo-random digits round-trip via getIndexDigit.
- D3 `getBaseCellNumber` on ext cells: PASS — all 122 base cells.

### Phase D6 — Rotation (executed early to unblock D2-G1)
- **D6-G1** `_h3Rotate60ccw` six rotations restore the original ext cell: PASS — `testRotationExt::d6g1_rotate60ccw_sixCycle_extCell`.
- D6 mirror: `_h3Rotate60cw` 6-cycle: PASS.
- D6 inverse: ccw then cw is identity on ext cells: PASS.
- D6 pentagon: `_h3RotatePent60ccw` 5-cycle on a pentagon center cell at res 19: PASS.
- D6 leading-non-zero finds ext digit when stock-range digits all zero: PASS.

## Stock test contract changes (4 sites)

These existing stock tests directly codified the OLD upper bound (`res = 16`
returns E_RES_DOMAIN). The H3-Extended widening intentionally changes that
contract; per CLAUDE.md non-negotiable #2 ("if compatibility must break,
STOP and surface"), each was surfaced and updated to use `MAX_H3_EXT_RES + 1`
as the new "above max" sentinel:

| File | Line | Change |
|------|------|--------|
| `src/apps/testapps/testH3Api.c` | 38 | `latLngToCell(..., 16, ...)` → `latLngToCell(..., MAX_H3_EXT_RES + 1, ...)` |
| `src/apps/testapps/testVec3.c` | 83 | `vec3ToCell(..., 16, ...)` → `vec3ToCell(..., MAX_H3_EXT_RES + 1, ...)` |
| `src/apps/testapps/testIndexDigits.c` | 55 | `getIndexDigit(h, 16, ...)` → `getIndexDigit(h, MAX_H3_EXT_RES + 1, ...)` |
| `src/apps/testapps/testConstructCell.c` | 112-113 | `res = 16, res = 18` → `res = MAX_H3_EXT_RES + 1, MAX_H3_EXT_RES + 3` |

## Faceijk static-table extension (commit `e75e840f`)

`_h3ToFaceIjk` for a Class III cell does `res++` before passing res to
`_adjustOverageClassII`, which indexes `maxDimByCIIres[res]` and
`unitScaleByCIIres[res]`. For an ext res-19 cell (Class III) the increment
lands at index 20; the stock 17-entry tables (sized for res 0-16) OOB and
trip ASAN global-buffer-overflow.

Both tables follow the aperture-7 grid pattern (Class II at index `2n`):
`maxDimByCIIres[2n] = 2 * 7^n`, `unitScaleByCIIres[2n] = 7^n`. Class III
entries are -1.

Indices 0-16 stock byte-identical. New entries at 17-20 (computed):
- maxDim:    `[17] = -1, [18] = 80707214, [19] = -1, [20] = 564950498`
- unitScale: `[17] = -1, [18] = 40353607, [19] = -1, [20] = 282475249`

All fit in int32 (max 564950498 = 2 * 7^10).

**Indices 21-22 NOT added.** At index 22 the maxDim value is 2 * 7^11 ≈
3.95e9, exceeding INT_MAX = 2147483647. Full coverage of res 20-22 needs:
1. `_adjustOverageClassII`'s `int maxDim, unitScale` widened to `int64_t`.
2. All downstream CoordIJK arithmetic widened to `int64_t`.
3. The same-class trap in stock coordijk `_ijkNormalize`, `_hex2dToCoordIJK`,
   `_upAp7r` (signed-int overflow at extreme i/j/k values for high res).

**Until those land**, the public API does not reject res 20-22 input but
downstream decode of an overage-bearing res-21 cell (or any res-22 cell
with non-trivial IJK) will OOB the table or trip UBSAN signed-overflow.
This is documented but not fixed in this session.

## Phases sequenced out of playbook order

The playbook §4.1 lists D1 → D2 → D3 → D4 → D5 → D6 → D7. This session
executed D6 before D3 / D4 / D5 because:

- D2-G1 (1,000 res-19 round-trips) cannot close without D6 rotation
  widening. The encoder applies `_h3Rotate60ccw` numRots times after
  building digits; stock rotation loops on `H3_GET_RESOLUTION` which for
  ext cells reports the 4-bit stock-res field (≤ 6). Without D6, ext
  digits 4-22 stay in the home-face frame, and the decoder reads them
  as if canonical-orientation. Round-trip diverges.
- The faceijk table extension (commit `e75e840f`) was prerequisite for
  the same gate (Class III post-increment OOBs at res 20).

Both detours surfaced to the user mid-session and authorized as
"Option B: extend tables + do D6 rotation widening BEFORE finishing
D2-G1." See conversation context. D4 / D5 / D7 follow the playbook order.

## Gates deferred

- **B2 MSVC `#error` path** — still deferred from Session 1 (macOS arm64
  cannot exercise; needs Windows runner or manual MSVC test).
- **D2-G1 at res 20-22** — covered only at res 19 in this session because
  faceijk tables stop at res 20 and coordijk has int32 overflow at higher
  res. Scope to res 19 satisfies the playbook's explicit gate text.

## Incomplete phases

Per CONTEXT PRESSURE VALVE in Session 2 kickoff prompt: D3 green, context
heavy, stopping. **Session 3 picks up D4 → D5 → D7 → E → F → G → H**:

- **D4** — Hierarchy + iterators (Pattern 3 mandatory: copy
  `_zeroIndexDigits`, `_incrementResDigit`, `_getResDigit`,
  `_iterInitParent`, `iterStepChild` patterns from `poc3_iterator.c`).
  Gates D4-G0 (canary stock-stock byte identity), D4-G1 through D4-G7.
- **D5** — localij surface (`cellToLocalIj`, `localIjToCell`,
  `gridDistance`, `gridPathCells`). Gate D5-G1.
- **D7** — FFI shim (Pattern 4 mandatory: copy signatures from
  `poc4_shim.h`, replace stubs with real H3 calls). Gates D7-G1, G2, G3.
- E / F / G / H from playbook §4.

## Build configuration (unchanged from Session 1)

```bash
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Debug -DWARNINGS_AS_ERRORS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Wno-error=deprecated-declarations" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

Pre-commit hook (validated every commit this session): builds + runs full
ctest suite under sanitizers; exits non-zero on any failure.

Each pre-commit run is ~9 minutes wall time on this hardware. 17 commits
this session = ~150 minutes of pure ctest time + edit time.

## Surprises / non-obvious lessons

1. **D6 rotation widening is a hard prerequisite of D2-G1**, despite the
   playbook §4.1 listing them as separate sub-phases in the order
   D2 → D6. The encoder-side rotation in `_faceIjkToH3` (called by every
   `latLngToCell` whose base cell needs reorientation) loops on stock res;
   for ext cells this leaves digits 4-22 unrotated. Round-trip diverges
   for all such cells (most random res-19 coords land off-home-face). The
   playbook's sub-phase ordering reflects sub-phase scope, not gate
   prerequisites. Session 3 should treat the playbook order as advisory
   for sub-phase scope and architectural-prerequisites order for gate
   evaluation.

2. **`_adjustOverageClassII` indexes static tables sized for stock res
   only.** This is a §5.6 trap class — not flagged in the playbook
   trap catalog but real. Must be extended for any decoder-side ext-res
   work to OOB-clean. Pattern is deterministic (`2 * 7^n` and `7^n`),
   but at res 22 the values overflow int32 — the function itself needs
   int64 widening for full res 20-22 coverage. For Session 2 the
   length-21 extension covered the D2-G1 gate at res 19 only.

3. **Stock H3 coordijk arithmetic overflows int32 at ext res for points
   far from face centers.** ASAN-flagged signed-overflow in
   `_ijkNormalize` (line 211), `_hex2dToCoordIJK` (line 345), `_upAp7r`
   (line 496). Stock H3 never exposed this because res ≤ 15 keeps the
   gnomonic-scaled radii well below INT_MAX. Encoder at res 22 multiplies
   r by sqrt(7)^22 ≈ 1.97e9, and points off-center push i/j/k into
   billions. Tests at res ≥ 20 will trip UBSAN until coordijk is
   int64-widened. testEncoderExt was scoped to res 19 to avoid this.

4. **Stock test fixtures encode the old MAX_H3_RES = 15 upper bound.**
   Four sites this session needed updates to use `MAX_H3_EXT_RES + 1` as
   the "above max" sentinel. This is the unavoidable cost of widening
   the public contract; each was surfaced per CLAUDE.md non-negotiable
   #2 and committed alongside the corresponding widening. Session 3 may
   hit a few more sites in D4 / D5 / D7 widening — check
   `grep -n 'res.*16\|res.*MAX_H3_RES' src/apps/testapps/` before each
   widening commit.

5. **`H3_GET_DIGIT_AT_RES` and `H3_SET_DIGIT_AT_RES` collapse to the
   stock macros for r ≤ 15.** This is what makes Pattern 3 transfers
   stock byte-identical: every Rule LB / DR / DW switch is a no-op for
   stock cells. Verified across 17 commits — stock 316 ctest stayed
   green at every step.

6. **clangd LSP diagnostics are noisy and stale throughout.** The
   editor reports `static_assert sizeof(H3Index) == 16 failed` because
   it can't resolve `__SIZEOF_INT128__` from the build. The actual
   cmake build under clang has the macro and the assert passes. Ignore
   LSP warnings unless they correlate with cmake build failures.

## Next session

Session 3 may proceed with phases D4 → D5 → D7 → E → F → G → H. Read this
checkpoint plus `POC_TRANSFER_GUIDE.md` (Pattern 3 / Pattern 4 transfers).
The Pattern 3 iterator copies for D4 are the next architectural unit;
POC-3 has the validated implementations (`_zeroIndexDigits` loop body,
`_incrementResDigit` dispatch, `_getResDigit` wrapper).

D4-G0 canary (1,000 stock cells through `cellToParent`, byte identity)
must run BEFORE any D4 changes to confirm the stock baseline is still
clean — and AFTER all D4 changes to verify no regression. Same canary
discipline as Session 1's stock test stays green.

Architectural follow-ups before D4 can fully gate:
- coordijk int64 widening (signed-overflow at ext res in
  `_ijkNormalize`, `_hex2dToCoordIJK`, `_upAp7r`).
- faceijk table extension to length 23 + `_adjustOverageClassII`
  int64-widened for res 21-22 round-trip coverage.

Both are deferred; testEncoderExt and testDecoderExt scope themselves
to res 19 to avoid them. D4 hierarchy gates (cellToChildren at
res 15 → 22 = 823,543 children per playbook §8.D D4-G6) will need them.
