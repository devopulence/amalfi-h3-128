# Running Tally — All Scripts and Source Files

> **Last Updated:** 2026-05-03 (Session 3 of implementation, mid-session save)
> **Scope:** H3-Extended (128-bit) preflight validation campaign + Session 1 implementation (Phases A, B, C) + Session 2 implementation (Phases D1, D2, D3, D6) + Session 3 implementation in progress (D4 + D5 complete; D7 + E + F + G + H still open). Preflight programs compile in isolation; implementation files are part of the H3 v4.4.1 source tree on `feat/h3-128-mvp`.

---

## Preflight Validation Programs

Standalone C test programs that validate the 128-bit architecture before any H3 source is modified. Each compiles with `-fsanitize=address,undefined -Werror -Wall -Wextra`, runs to completion with zero ASAN/UBSAN output, and exits 0 on success.

| Script | Created | Description |
|--------|---------|-------------|
| `poc1_bit_layout.c` | 2026-05-02 | POC-1 bit layout + Group C macro validation. Defines stock Group A/B constants and 14 Group C macros for 128-bit cells; runs 19 test categories (PF-01..PF-19) covering sizeof, resolution encoding, digit round-trip across all 22 positions, reserved-bit zeroing, H3_INIT/H3_INIT_EXT sentinel patterns, cross-boundary digit isolation, and single-evaluation safety. Result: 685/685 passed. |
| `poc2_validation.c` | 2026-05-02 | POC-2 validation predicates. Implements widened `_hasGoodTopBits`, `_firstOneIndex`, `_hasAny7UptoRes`, `_hasAll7AfterRes`, `_hasDeletedSubsequence` for 128-bit cells. 23 categories (VP-01..VP-23) including 128-position single-bit `_firstOneIndex` sweep, every-bit-individually corruption checks for reserved 86–127, and pentagon K-axis detection through ext digits. Result: 3473/3473 passed. |
| `poc3_iterator.c` | 2026-05-02 | POC-3 iterator state machine. Implements `_zeroIndexDigits`, `_incrementResDigit`, `_getResDigit`, `_iterInitParent`, `iterStepChild` plus the `ChildIterator` struct. 28 categories (IT-01..IT-28) culminating in the 823,543-child full-depth enumeration (parent res 15 → child res 22) under UBSAN — proves the §6.6 / §6.10 negative-shift traps are eliminated. Pentagon skip semantics validated at the stock/ext boundary. Result: 4595/4595 passed; 828,210 children enumerated total. |
| `poc4_shim.h` | 2026-05-02 | POC-4 shared FFI header. Declares the `H3Index` typedef (`__uint128_t`), the `H3Error` enum, and 10 cross-TU shim function signatures (all by-pointer, never by-value because `__uint128_t` lacks a stable register-passing ABI). Plus `h3_ext_sizeof_h3index()` / `h3_ext_alignof_h3index()` probes for cross-TU consistency. Included by both shim and caller TUs. |
| `poc4_shim.c` | 2026-05-02 | POC-4 shim translation unit. Implements all 10 functions with deterministic stub logic — `h3_ext_lat_lng_to_cell` packs a hash-derived bit pattern; `h3_ext_h3_to_string`/`h3_ext_string_to_h3` perform real 32-char hex serialization; `h3_ext_cell_to_children` uses a base-7 `decode_child` helper rather than carrying the full iterator. NULL-pointer / NaN / out-of-range res returns map to `E_FAILED` / `E_DOMAIN` / `E_RES_DOMAIN` respectively. |
| `poc4_caller.c` | 2026-05-02 | POC-4 caller translation unit (contains `main`). Fabricates 128-bit cells using Group A/B/C macros copied from POC-1, then invokes every shim function across 22 ABI categories (ABI-01..ABI-22). Includes 100-cell deterministic-LCG string round-trip with `memcmp` bit-identical assertion, 500-cell stress test, full 21-case NULL-pointer error contract, and adversarial all-zero / all-ones / single-bit-127 round-trips proving the high half transits intact. Result: 1314/1314 passed. |

---

## Session 1 Implementation Files (Phases A, B, C on `feat/h3-128-mvp`)

H3 source tree edits and additions delivered by Session 1. Each landed under ASAN+UBSAN with the pre-commit hook running the full 317/317 ctest suite.

### Build infrastructure

| File | Created/Modified | Description |
|------|------------------|-------------|
| `.git/hooks/pre-commit` | 2026-05-02 — created (not in repo) | Build + ctest gate before every commit. Runs `cmake --build build-dev -j` then `(cd build-dev && ctest --output-on-failure --quiet)`. Exits non-zero on failure, blocking the commit. Non-bypassable per `CLAUDE.md` non-negotiable #4. |
| `.github/workflows/ci.yml` | 2026-05-02 — created | CI matrix `{ubuntu-latest, macos-latest} × {release, sanitizers}` for `feat/h3-128-mvp` and `master`. Sanitizer config matches `build-dev`: `-DCMAKE_BUILD_TYPE=Debug -DWARNINGS_AS_ERRORS=ON -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Wno-error=deprecated-declarations"`. Release config uses `WARNINGS_AS_ERRORS=ON` only. |

### H3 source tree edits (Phase A)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/include/h3api.h.in` | 2026-05-02 — modified | Public API typedef widened: `typedef uint64_t H3Index` → `typedef __uint128_t H3Index`. `#error` guard on `__SIZEOF_INT128__` per playbook §9.1 — MSVC dropped, MinGW/WSL documented escape hatches. |
| `src/h3lib/include/constants.h` | 2026-05-02 — modified | Added `MAX_H3_EXT_RES = 22` adjacent to `MAX_H3_RES`. Group C constant #1. |
| `src/h3lib/include/h3Index.h` | 2026-05-02 — modified | (1) Retyped 6 stock Group A masks (`H3_HIGH_BIT_MASK`, `H3_MODE_MASK`, `H3_BC_MASK`, `H3_RES_MASK`, `H3_RESERVED_MASK`, `H3_DIGIT_MASK`) and `H3_INIT` from `(uint64_t)`/`UINT64_C(...)` to `(H3Index)`. Bit values unchanged; required so `~MASK` is full-width and SET macros preserve the high half. (2) Added 14 Group C macros copied verbatim from `poc1_bit_layout.c`: `H3_EXT_FLAG_OFFSET`, `H3_EXT_FLAG_MASK`, `H3_EXT_DIGITS_OFFSET`, `H3_EXT_DIGITS_MASK`, `H3_INIT_EXT`, `H3_GET/SET_EXT_FLAG`, `H3_GET/SET_EFFECTIVE_RESOLUTION` (GCC statement-expression for single-evaluation), `H3_GET/SET_EXT_INDEX_DIGIT`, `H3_GET/SET_DIGIT_AT_RES`. (3) `_Static_assert(sizeof(H3Index) == 16, ...)` for gate A1. |

### CLI app cascade fixes (Phase A)

The typedef widening cascades through every site that uses `PRIx64`/`PRIu64` with `H3Index`. Fix is mechanical: cast print args to `(uint64_t)` (preserves stock byte-identity for high-half-zero cells); use uint64 intermediate for sscanf-into-H3Index; zero-init H3Index decls that get sscanf'd via the args framework.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/applib/lib/utility.c` | 2026-05-02 — modified | `h3Print(H3Index)` and `h3Println(H3Index)` cast to `(uint64_t)` for `printf("%PRIx64", ...)`. |
| `src/apps/filters/h3.c` | 2026-05-02 — modified | 28 unique print sites cast to `(uint64_t)` (variables `c`, `out`, `out[i]`, `out[j]`, `out[0]`, `out[1]`, `parent`, `centerChild`, `child`, `compactedSet[i]`, `uncompactedSet[i]`, `cells[i]`). One sscanf at `buffer + bufferOffset` rewritten to use a uint64 intermediate. Three subcommands (`intToString`, `areNeighborCells`, `cellsToDirectedEdge`) had their `H3Index` decls zero-initialized so the args framework's sscanf doesn't leave high-half garbage that gets passed through `cellToParent`. |
| `src/apps/miscapps/cellToBoundaryHier.c` | 2026-05-02 — modified | `parentIndex` cast to `(uint64_t)` for sprintf in KML name generation. |
| `src/apps/miscapps/cellToLatLngHier.c` | 2026-05-02 — modified | `parentIndex` cast to `(uint64_t)` for sprintf in KML name generation. |
| `src/apps/testapps/testH3NeighborRotations.c` | 2026-05-02 — modified | `h` cast to `(uint64_t)` in debug printf. |

### Test infrastructure (Phase B)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/applib/include/test.h` | 2026-05-02 — modified | Added `static inline bool latlng_within_tolerance(LatLng a, LatLng b, int res)` helper per playbook §9.2. Returns `fabs(a.lat - b.lat) < tol && fabs(a.lng - b.lng) < tol` with `tol = 1e-12 rad` for `res >= 16`, `1e-9 rad` otherwise. Pre-calibration values; will be empirically re-calibrated when Phase D2 round-trip tests land. |

### String I/O widening (Phase C)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/testapps/testStringExt.c` | 2026-05-02 — created | Phase C ext-cell ctest suite: 4051 assertions. Gates C1 (stock res-15 byte-identical round-trip), C2 (ext 32-char output + buffer-too-small returns E_MEMORY_BOUNDS), C3 (1000 random ext cells round-trip via deterministic LCG, all 7 ext resolutions 16–22, all 122 base cells reachable). Ext cells fabricated via Group C macros (POC-1 pattern) since the encoder isn't widened until Phase D1. |

### Session 1 checkpoint

| File | Created/Modified | Description |
|------|------------------|-------------|
| `SESSION_1_CHECKPOINT.md` | 2026-05-02 — created | Records exact A1-A5, B1+B3, C1-C3 gate results plus the 5 surprises/lessons learned during integration (POC-1 isolation diagnostic, mask retype necessity, sscanf zero-init requirement, fgets newline trap, granularity reality). Session 2 reads this before starting Phase D1 to verify and learn the integration gotchas. |

---

## Session 2 Implementation Files (Phases D1, D2, D3, D6 on `feat/h3-128-mvp`)

H3 source tree edits and additions delivered by Session 2. Each landed under ASAN+UBSAN with the pre-commit hook running the full ctest suite (317 → 318 → 319 → 320 → 321 as test suites were added). Phases D6 was sequenced before D2-G1 closure (out of playbook §4.1 sub-phase order) because the encoder rotation is a hard prerequisite of round-trip closure for ext cells.

### Multi-phase H3 source edits

The single file `src/h3lib/lib/h3Index.c` received edits across all four Session 2 phases (D1, D2, D3, D6). Consolidated description below.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/lib/h3Index.c` | 2026-05-03 — modified (Session 2) | **Phase D1 (encoder, 4 sites)**: `setH3Index` (Rule INIT branch on `res > MAX_H3_RES` to seed from `H3_INIT_EXT` vs `H3_INIT`; Rule RW `H3_SET_RESOLUTION` → `H3_SET_EFFECTIVE_RESOLUTION`; Rule DW `H3_SET_INDEX_DIGIT` → `H3_SET_DIGIT_AT_RES`). `_faceIjkToH3` (same INIT+RW+DW pattern at the encoder entry). `latLngToCell` + `vec3ToCell` (Rule GR `MAX_H3_RES` → `MAX_H3_EXT_RES` on the upper-bound guards). **Phase D2 (decoder, 3 sites)**: `_h3ToFaceIjkWithInitializedFijk` (Rule LB on the digit-traversal loop bound + Rule DR for the digit-getter dispatch). `_h3ToFaceIjk` (3 Rule LB sites: local res declaration + two post-overage comparisons against the cell's reported res). `cellToVec3` (Rule LB on `_faceIjkToVec3`'s res arg — without it the gnomonic rescale is wrong by `sqrt(7)^16` for ext cells). **Phase D6 (rotation, 5 sites — Pattern 3 mandatory)**: `_h3LeadingNonZeroDigit` (Rule LB+DR), `_h3Rotate60ccw` + `_h3Rotate60cw` (Pattern 3: LB+DR+DW), `_h3RotatePent60ccw` + `_h3RotatePent60cw` (Pattern 3). **Phase D3 (accessors, 3 sites)**: `getResolution` (Rule LB — single-line switch to `H3_GET_EFFECTIVE_RESOLUTION`; stock cells unchanged). `getIndexDigit` (Rule GR + DR). `constructCell` (Rule GR + INIT + RW + DW — same pattern as setH3Index). All 14 Session 2 widening sites pass stock 316 ctest byte-identical (every Pattern 3 substitution is a no-op for r ≤ 15). |

### Faceijk static-table extension (D2/D6 prep)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/lib/faceijk.c` | 2026-05-03 — modified | Extended `maxDimByCIIres` and `unitScaleByCIIres` from length 17 to length 21. New Class III entries at indices 17, 19 = -1; new Class II entries at indices 18, 20 = `2 * 7^9, 2 * 7^10` (maxDim) and `7^9, 7^10` (unitScale). All fit in int32. **Indices 21-22 NOT added** because `maxDimByCIIres[22] = 2 * 7^11 ≈ 3.95e9` exceeds INT_MAX = 2147483647. Full coverage of res 21-22 round-trips needs `_adjustOverageClassII`'s `int maxDim, unitScale` int64-widened plus all downstream CoordIJK arithmetic int64. Per playbook §5.6: extending these literals is permitted "if the table values for res 16-22 are known and computed." They are. Without this commit, `_h3ToFaceIjk` for an ext res-19 Class III cell does `res++` to 20 and OOBs the 17-entry tables (ASAN global-buffer-overflow at `_adjustOverageClassII`). |

### Stock test contract updates (Session 2)

Four existing stock tests directly codified the OLD upper-resolution bound by literal `16`. The H3-Extended widening intentionally changes that contract; per CLAUDE.md non-negotiable #2 ("if compatibility must break, STOP and surface"), each was surfaced and updated to use `MAX_H3_EXT_RES + 1` (= 23) as the new "above max" sentinel — preserving test intent at the new bound.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/testapps/testH3Api.c` | 2026-05-03 — modified | Stock test contract update at line 38: `latLngToCell(..., 16, ...) == E_RES_DOMAIN` → `MAX_H3_EXT_RES + 1`. Bundled with the D1.3 encoder guard relaxation commit (`569da6b2`). |
| `src/apps/testapps/testVec3.c` | 2026-05-03 — modified | Stock test contract update at line 83: `vec3ToCell(..., 16, ...) == E_RES_DOMAIN` → `MAX_H3_EXT_RES + 1`. Bundled with `569da6b2`. |
| `src/apps/testapps/testIndexDigits.c` | 2026-05-03 — modified | Stock test contract update at line 55: `getIndexDigit(h, 16, ...) == E_RES_DOMAIN` → `MAX_H3_EXT_RES + 1`. Bundled with the D3.2 widening commit (`5a1311ca`). |
| `src/apps/testapps/testConstructCell.c` | 2026-05-03 — modified | Stock test contract update at lines 112-113: `res = 16, res = 18` table cases → `MAX_H3_EXT_RES + 1, MAX_H3_EXT_RES + 3`. Bundled with the D3.3 widening commit (`bdf36196`). |

### New ext test suites (Session 2)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/testapps/testEncoderExt.c` | 2026-05-03 — created | Phase D1-G1 gate suite (8305 assertions, 6 TEST blocks): `latLngToCell` at res 19 produces ext cells with bit 64 set and effective res 19 (the playbook gate, 100 deterministic LCG coords); stock res 0-15 high half zero (D1 byte-identity guard); `vec3ToCell` matches `latLngToCell` byte-identical at res 19; `setH3Index` ext-res produces well-formed cell across res 16-22 + all 122 base cells + all initDigits (digits past res = sentinel 7 from H3_INIT_EXT); `setH3Index` stock byte-identity; res > MAX_H3_EXT_RES rejection. Scoped to res 19 due to stock coordijk int32 overflow at higher res (documented as known-issue in the file header — `_ijkNormalize`, `_hex2dToCoordIJK`, `_upAp7r` are int32 and overflow at ext res for off-face-center coords). |
| `src/apps/testapps/testDecoderExt.c` | 2026-05-03 — created | Phase D2-G1 gate suite (5401 assertions, 2 TEST blocks): stock res 0-15 cell-center round-trip bit-identical (D2 widening regression guard); 1,000 res-19 cell-center round-trips bit-identical (the playbook gate). Round-trip pattern: `latLngToCell(g_in, 19, &h1) → cellToLatLng(h1, &g_center) → latLngToCell(g_center, 19, &h2)`, assert `h1 == h2`. Reports closure rate + dumps first failure (h1, h2, in/center coords, base cell) on any divergence — none observed. Header documents the four prerequisites for closure: D1 + D2 + D6 widening + faceijk table extension. |
| `src/apps/testapps/testRotationExt.c` | 2026-05-03 — created | Phase D6-G1 gate suite (302 assertions, 5 TEST blocks): `_h3Rotate60ccw` is a 6-cycle on ext cells across res 16-19 (the playbook gate); `_h3Rotate60cw` 6-cycle (mirror); ccw-then-cw is identity on ext cells (catches asymmetric digit-position handling); `_h3RotatePent60ccw` is a 5-cycle on a pentagon center cell at res 19 (base cell 4, all digits 0); `_h3LeadingNonZeroDigit` finds the ext-range digit when stock-range digits are all zero (regression for trap §6.13). Forward-declares the internal rotation helpers (h3api.h doesn't expose them; they link from libh3). |
| `src/apps/testapps/testAccessorExt.c` | 2026-05-03 — created | Phase D3-G1 gate suite (559 assertions, 7 TEST blocks): `getResolution` returns 16-22 for ext cells across res 16-22 + 5 base cells (the playbook gate); stock byte-identity guard for res 0-15; `getIndexDigit` ext dispatch (digits 1-19 = initDigit, 20-22 = sentinel 7); `getIndexDigit` bound (negative, 0, MAX_H3_EXT_RES + 1 all rejected with E_RES_DOMAIN); `constructCell` ext round-trip at res 19; `constructCell` various pseudo-random digits across res 16-22; `getBaseCellNumber` on all 122 ext cells (no widening needed; defensive coverage). |

### Test infrastructure registration (Session 2)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `CMakeTests.cmake` | 2026-05-03 — modified | Added 4 new test registrations to the existing list: `add_h3_test(testEncoderExt ...)`, `add_h3_test(testDecoderExt ...)`, `add_h3_test(testRotationExt ...)`, `add_h3_test(testAccessorExt ...)`. Each on its own `add_h3_test(...)` line with a comment header indicating the phase (D1/D2/D6/D3 respectively). Also retains the Session 1 `add_h3_test(testStringExt ...)` registration unchanged. |

### Session 2 checkpoint

| File | Created/Modified | Description |
|------|------------------|-------------|
| `SESSION_2_CHECKPOINT.md` | 2026-05-03 — created | Records exact D1-G1, D2-G1, D3-G1, D6-G1 gate results, the 4 stock test contract changes (with reasons), the faceijk table extension scope/justification (length 21, why not 23), 3 architectural deferrals for Session 3 (coordijk int64 widening, faceijk length 23 + `_adjustOverageClassII` int64, D4-D7 widening), 6 surprises/lessons learned (D6 prereq of D2-G1, faceijk static-table trap, coordijk int32 overflow at ext res, stock test fixture upper-bound encoding, dispatch-macro stock byte-identity, clangd LSP staleness), build configuration unchanged from Session 1, and the 17-commit list with subjects. |

---

## Session 3 Implementation Files (Phases D4 + D5 on `feat/h3-128-mvp` — mid-session save)

H3 source tree edits and additions delivered by Session 3 so far. Each landed under ASAN+UBSAN with the pre-commit hook running the full ctest suite (321 → 322 → 323 as test suites were added). D4 + D5 complete; D7 + E + F + G + H still open.

### Multi-phase H3 source edits (Session 3)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/lib/h3Index.c` | 2026-05-03 — modified (Session 3) | **Phase D4 (hierarchy, 6 sites)**: `_zeroIndexDigits` (§6.6 — replaced bit-magic body with per-digit loop using `H3_SET_DIGIT_AT_RES`; POC-3 I1 pattern). `cellToParent` (Rule LB+GR+RW+DW + high-half scrub `parentH &= 0xFFFFFFFFFFFFFFFFULL` for ext→stock transitions; trap §6.7). `_hasChildAtRes` (Rule LB on parentRes capture + Rule GR on upper-bound). `cellToChildrenSize` (Rule LB on depth arithmetic — without it an ext parent at effective res 18 with childRes 22 computes depth = 22 - 2 = 20 via stock-res field, returning ~7^20 children → allocation explosion; trap §6.17). `makeDirectChild` (Rule LB+RW+DW; trap §6.17). `cellToCenterChild` (Rule LB+RW + trailing-sentinel population for ext children at digits childRes+1..MAX_H3_EXT_RES; trap §6.8 — without this, the function produces malformed half-state cells that look like stock res-0 base cells, breaking everything downstream including polyfill which is post-MVP per §11). |
| `src/h3lib/lib/iterators.c` | 2026-05-03 — modified (Session 3) | **Phase D4 (iterators, 5 sites)**: `_getResDigit` (Rule DR — `H3_GET_INDEX_DIGIT` → `H3_GET_DIGIT_AT_RES`; POC-3 I3). `_incrementResDigit` (POC-3 I2 — replaced negative-shift UB body with read-d/write-d+1 via dispatching macros; trap §6.10). `_iterInitParent` (Rule LB on `_parentRes` capture + Rule GR + Rule RW + trailing-sentinel population for ext childRes; POC-3 I4). `iterStepChild` (POC-3 I5 — replaced for-loop's bit-add carry with explicit-zero-and-recurse while-loop; CRITICAL top-of-function guard `if (childRes <= _parentRes) return null` matching POC-3 line 236-240 — without it, single-cell iters emit a duplicate corrupted cell, breaking uncompactCells with E_MEMORY_BOUNDS). `iterInitBaseCellNum` (Rule GR). |
| `src/h3lib/lib/localij.c` | 2026-05-03 — modified (Session 3) | **Phase D5 (localij, 6 sites)**: `cellToLocalIjk` (Rule LB on origin's effective res; Rule LB on res-mismatch comparison so two ext cells with matching effective res succeed; trap §6.15). `localIjkToCell` (Rule LB on origin's effective res; Rule INIT seed `*out` from `H3_INIT_EXT` for ext output; Rule RW `H3_SET_RESOLUTION` → `H3_SET_EFFECTIVE_RESOLUTION`; Rule DW `H3_SET_INDEX_DIGIT` → `H3_SET_DIGIT_AT_RES` in build-from-fine loop). Internal CoordIJK arithmetic unchanged (still int32 — same Session 2 deferral). |

### Stock test contract updates (Session 3)

Five existing stock tests directly codified the OLD upper-resolution bound by literal `MAX_H3_RES + 1` (= 16) or `16` itself. The H3-Extended Phase D4 widening intentionally changes that contract; per CLAUDE.md non-negotiable #2, each was surfaced and updated to use `MAX_H3_EXT_RES + 1` (= 23) as the new "above-max" sentinel.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/testapps/testCellToParent.c` | 2026-05-03 — modified | Stock test contract update at line 55: `cellToParent(child, 16, ...) == E_RES_DOMAIN` → `MAX_H3_EXT_RES + 1`. Bundled with the cellToParent widening commit (`5aba98f5`). |
| `src/apps/testapps/testCellToCenterChild.c` | 2026-05-03 — modified | Stock test contract update at line 80: `cellToCenterChild(baseHex, MAX_H3_RES + 1, ...) == E_RES_DOMAIN` → `MAX_H3_EXT_RES + 1`. Bundled with `9211f12e`. |
| `src/apps/testapps/testCellToChildren.c` | 2026-05-03 — modified | Stock test contract update at line 146 (the `childResTooFine` test): `int res = MAX_H3_RES + 1` → `MAX_H3_EXT_RES + 1`. Bundled with `9211f12e`. |
| `src/apps/testapps/testCompactCells.c` | 2026-05-03 — modified | Two stock test contract updates: line 459 `uncompactCellsSize(.., MAX_H3_RES + 1, ..) == E_RES_MISMATCH` → `MAX_H3_EXT_RES + 1`; line 488 `uncompactCells(.., MAX_H3_RES + 1)` → `MAX_H3_EXT_RES + 1` (the line-488 site was guarding a stack-buffer-overflow scenario via the OLD bound; with widened bounds the call would proceed and overflow `uncompressed` (3-element stack buffer) by writing 21 cells). Both bundled with `9211f12e`. |

### New ext test suites (Session 3)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/testapps/testHierarchyExt.c` | 2026-05-03 — created | Phase D4-G0..G7 gate suite (~700 assertions, 8 TEST blocks): D4-G0 stock-stock byte identity through cellToParent for 1,000 random stock cells across res 0-15 (the canary; high half stays zero, low half matches independently-constructed reference parent); D4-G1 ext→stock parent for every parentRes 0..15 (verifies high-half scrub from `cellToParent` widening); D4-G2 ext→ext parent every parentRes 16..22; D4-G3a hexagon parent (base cell 17) at res 15 → 7 ext children at res 16, digit-16 ∈ {0..6} unique; D4-G3b pentagon parent (base cell 4) at res 15 → 6 ext children (digit 1 skipped); D4-G4 cellToCenterChild ext output (regression for trap §6.8) for stock res-15 → ext 16-22 + ext res-18 → ext res-22 with center-path digits zero and trailing sentinels = 7; D4-G5 cellToChildrenSize analytical (7^n hexagon, 1+5*(7^n-1)/6 pentagon — verify int64 returned, no enumeration); D4-G6 full ext recursion res 15 → 22 = 823,543 cells, each verified ext flag/effective res/base cell; D4-G7 iterator parity with cellToChildren (XOR-fold checksum, 343 children at res 18). G6 is achievable today despite the Session 2 coordijk deferral because cellToChildren is purely digit manipulation. |
| `src/apps/testapps/testLocalIjExt.c` | 2026-05-03 — created | Phase D5-G1 gate suite (1707 assertions, 3 TEST blocks): D5-G1 gridDistance for 200 deterministic ext-cell pairs in same base cell (skipping pentagon-side failures which are E_PENTAGON / E_FAILED — expected); cellToLocalIjk ext-vs-stock res mismatch returns E_RES_MISMATCH (effective resolution comparison); cellToLocalIj → localIjToCell round-trip bit-identical for 100 ext-res-19 anchor pairs (exercises the H3_INIT_EXT seed + H3_SET_EFFECTIVE_RESOLUTION + H3_SET_DIGIT_AT_RES loop in widened localIjkToCell). Scoped to res 19 due to coordijk int32 deferral. |

### Test infrastructure registration (Session 3)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `CMakeTests.cmake` | 2026-05-03 — modified | Added 2 new test registrations: `add_h3_test(testHierarchyExt ...)` (Phase D4) and `add_h3_test(testLocalIjExt ...)` (Phase D5). Existing Session 1+2 registrations unchanged. |

### Session 3 mid-session context save

| File | Created/Modified | Description |
|------|------------------|-------------|
| `contexts/contexts-may-03-20260503-201452.md` | 2026-05-03 — created | Mid-session save covering the D4 + D5 progress so that the next session continuation (D7 → E → F → G → H) can resume exactly where this stops. Includes the four sections (Summary, Files, Open Items, Context Dump), the 5 stock test contract changes, the 7 surprises/lessons (notably the iterStepChild top-guard bug, the cellToCenterChild + _hasChildAtRes contract pairing, D4-G6 achievability), and a comprehensive Open Items list with concrete next-step recipes for D7 (FFI shim Pattern 4 mandatory) through H (acceptance + tag). |

---

## Build Commands

Each POC is built and run independently from the repo root:

```bash
# POC-1
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc1_bit_layout poc1_bit_layout.c -lm && ./poc1_bit_layout

# POC-2
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc2_validation poc2_validation.c -lm && ./poc2_validation

# POC-3
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc3_iterator poc3_iterator.c -lm && ./poc3_iterator

# POC-4 (separate translation units — must NOT be combined)
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_shim.c   -o poc4_shim.o
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_caller.c -o poc4_caller.o
gcc -fsanitize=address,undefined -o poc4_ffi poc4_shim.o poc4_caller.o -lm && ./poc4_ffi

# Session 1 + Session 2 implementation: build-dev with sanitizers
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Debug -DWARNINGS_AS_ERRORS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Wno-error=deprecated-declarations" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-dev -j$(sysctl -n hw.ncpu)
(cd build-dev && ctest --output-on-failure)
# Expected: 321/321 PASS

# Re-run individual ext suites in isolation (faster than full ctest)
build-dev/bin/testStringExt        # Session 1 — 4051 assertions
build-dev/bin/testEncoderExt       # Session 2 — 8305 assertions, scoped to res 19
build-dev/bin/testDecoderExt       # Session 2 — 5401 assertions, 1000 res-19 round-trips
build-dev/bin/testRotationExt      # Session 2 — 302 assertions
build-dev/bin/testAccessorExt      # Session 2 — 559 assertions
build-dev/bin/testHierarchyExt     # Session 3 — D4-G0..G7, ~700 assertions (823,543-cell sweep)
build-dev/bin/testLocalIjExt       # Session 3 — D5-G1, 1707 assertions
```

---

## Summary

| Category | Count |
|----------|-------|
| Preflight Validation Programs | 6 |
| Build infrastructure (hook + workflow) | 2 |
| H3 source tree edits (Phase A) | 3 |
| CLI app cascade fixes (Phase A) | 5 |
| Test infrastructure (Phase B) | 1 |
| String I/O widening (Phase C — testStringExt only; h3Index.c rolled into multi-phase entry) | 1 |
| Session 1 checkpoint | 1 |
| Multi-phase H3 source edits (Session 2 — D1+D2+D3+D6 in one file) | 1 |
| Faceijk static-table extension (Session 2) | 1 |
| Stock test contract updates (Session 2) | 4 |
| New ext test suites (Session 2) | 4 |
| Test infrastructure registration (Session 2) | 1 |
| Session 2 checkpoint | 1 |
| Multi-phase H3 source edits (Session 3 — D4 in h3Index.c + iterators.c, D5 in localij.c) | 3 |
| Stock test contract updates (Session 3) | 4 (5 sites; testCompactCells.c has 2 sites) |
| New ext test suites (Session 3) | 2 |
| Test infrastructure registration (Session 3 — same CMakeTests.cmake row, modified) | — |
| Session 3 mid-session context save | 1 |
| Session 4 — D7 FFI shim files (header + source + test) | 3 |
| Session 4 — Multi-phase H3 source edits (Session 4 — E1-E5 + F1 in h3Index.c) | 1 (modified, bundled into 6 commits) |
| Session 4 — New ext test suites (testValidationExt, testAuxiliaryExt) | 2 |
| Session 4 — Stock test contract updates (testPentagonIndexes + tests/cli/getPentagons.txt) | 2 |
| Session 4 — Audit infrastructure (audit.py + manifest.json + SKILL.md) | 3 |
| Session 4 — SESSION_4_CHECKPOINT.md | 1 |
| **Total** | **54** |

| Metric | Value |
|--------|-------|
| Total assertions across all POCs | 10,067 |
| Total children enumerated (POC-3) | 828,210 |
| Toolchain | Apple clang 21.0.0 / Darwin arm64 |
| Sanitizers | AddressSanitizer + UndefinedBehaviorSanitizer (both clean) |
| POCs passing | 4 / 4 |
| Session 1 ctest count | 317/317 PASS (316 stock + testStringExt 4051 assertions) |
| Session 2 ctest count | 321/321 PASS (316 stock + 5 ext suites: testStringExt 4051, testEncoderExt 8305, testDecoderExt 5401, testRotationExt 302, testAccessorExt 559) |
| Session 3 ctest count (mid-session) | 323/323 PASS (316 stock + 7 ext suites: Session 1+2 above + testHierarchyExt ~700 with 823,543-cell ext recursion + testLocalIjExt 1707) |
| Session 4 ctest count | 326/326 PASS (316 stock + 10 ext suites: Session 1-3 above + testFFIShimExt 1038 + testValidationExt 220 + testAuxiliaryExt 771) |
| Session 1 gates | A1-A5, B1, B3, C1-C3 PASS; B2 deferred |
| Session 2 gates | D1-G1, D2-G1 (res 19), D3-G1, D6-G1 PASS; D2-G1 res 20-22 deferred (coordijk int64) |
| Session 3 gates (mid-session) | D4-G0..G7 PASS (incl. 823,543-cell res 22 recursion), D5-G1 PASS (res 19); D7 + E + F + G + H still open |
| Session 4 gates | D7-G1/G2/G3 PASS, E1-E7 PASS (E1+E2 CRITICAL), F1-F4 PASS (F2 ext-pentagon scoped to maxFaceCount; full overage walk deferred to coordijk int64), G1-G3 PASS, H1/H2/H3/H5/H6 PASS; H4 (CI on push) deferred. **v0.1.0-128bit-mvp tagged.** |
| Session 1 commits on `feat/h3-128-mvp` | 6 (8f8829ab, b2192926, 1e7b6ee3, 089ab9b0, 9e44107e, 944d6e5c) |
| Session 2 commits on `feat/h3-128-mvp` | 18 (a36f68f1, 7bd3ef69, 569da6b2, d15f6668, 16491e7f, a98a2096, c18b505f, e75e840f, d09221c8, 437278c0, bf22c42f, f8c318ab, 9fe7f529, 5a64861e, 5a1311ca, bdf36196, 63cb7b6b, 7d5d6296) |
| Session 3 commits on `feat/h3-128-mvp` (mid-session) | 7 (d22204e1 _zeroIndexDigits, 8fd4f1a0 iterators, 5aba98f5 cellToParent, 9211f12e _hasChildAtRes/Size/MakeChild/CenterChild bundle, 1bef1e34 testHierarchyExt, c395f91e localij widening, a9de465d testLocalIjExt) |
| Session 4 commits on `feat/h3-128-mvp` | 10 (4d9feaa4 D7 FFI shim, 59a4949d E1, 61e3ce54 E2, 92481108 E3, 943fcb51 E4, 80f2f8ee E5, 36c56d14 E test, 198649d3 F widening, 841db752 F test, 2aabbdcd G audit) |
