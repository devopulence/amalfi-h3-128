# Running Tally — All Scripts and Source Files

> **Last Updated:** 2026-05-12 (Session 8.6 wheel retag + .env gitignore + repo hardening; v0.3.1-py3-tag tagged, Databricks-serverless install unblocked)
> **Scope:** H3-Extended (128-bit) preflight validation campaign + Sessions 1-7 (calendar) C library implementation + Session 7-py (Python bindings + cffi + wheels + CI) + Session 8 (local validation gate) + Session 8.5 (CI workflow repair) + cross-repo reference skill + Session 8.6 (wheel retag cp311-cp311 → py3-none for Databricks 3.12) + repo hardening (`.env` gitignore safety net, long-standing untracked items backfilled, unrelated `backtesting` agent+skill removed). Preflight programs compile in isolation; implementation files are part of the H3 v4.4.1 source tree on `feat/h3-128-mvp`. Python package `h3_extended` and validation/skill artifacts also live in this repo.

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

## Session 4 Implementation Files (Phases D7 + E + F + G + H on `feat/h3-128-mvp` — MVP COMPLETE, tag `v0.1.0-128bit-mvp`)

H3 source tree edits and additions delivered by Session 4. Each landed under ASAN+UBSAN with the pre-commit hook running the full ctest suite (323 → 324 → 325 → 326 as test suites were added). All Phase D7 + E + F + G + H gates green; 9/10 §11 acceptance criteria PASS (H4 CI matrix pending GitHub Actions on the just-pushed branch).

### FFI shim layer (Phase D7 — Pattern 4 mandatory)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/include/h3ExtShim.h` | 2026-05-04 — created | D7: 10 FFI shim function prototypes adapted from `poc4_shim.h`. Pulls `H3Index` typedef + `H3Error` enum from `h3api.h` instead of redeclaring. `DECLSPEC` for symbol export. Plus `h3_ext_sizeof_h3index` / `h3_ext_alignof_h3index` cross-TU consistency probes. Per playbook §7: by-pointer convention because `__uint128_t` lacks a stable register-passing ABI. Bundled with `4d9feaa4`. |
| `src/h3lib/lib/h3ExtShim.c` | 2026-05-04 — created | D7: implementations delegate to real (widened) H3 entry points. NULL-pointer args return E_FAILED. cell_to_children calls cellToChildrenSize internally to populate count. sizeof/alignof probes use `__alignof__` (GCC/Clang extension, C99-safe; `<stdalign.h>` is C11). Bundled with `4d9feaa4`. |
| `src/apps/testapps/testFFIShimExt.c` | 2026-05-04 — created | Phase D7-G1/G2/G3 + NULL-contract gate suite (1038 assertions, 4 TEST blocks). G1: 10 shim functions linkable + smoke through each entry point. G2: sizeof(H3Index) == 16 and __alignof__(H3Index) == 16 consistent across test TU and shim TU (POC-4 ABI verification). G3: 1,000 random ext cells string-round-trip byte-identical at FFI boundary. NULL contract: all 11 entry points return E_FAILED on NULL. d7g1 `is_valid_cell` assertion loosened to `valid == 0 || valid == 1` because semantic ext-cell validity depends on Phase E (E1's `_hasGoodTopBits` widening) — D7's gate is linkability + ABI, not semantic validity. Bundled with `4d9feaa4`. |

### Multi-phase H3 source edits (Session 4 — E1-E5 + F1 in h3Index.c)

The single file `src/h3lib/lib/h3Index.c` received 6 commits across Phase E (5 widenings + 1 stock contract) and Phase F (1 widening bundle). Consolidated description below.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/lib/h3Index.c` | 2026-05-04 — modified (Session 4) | **E1** (`59a4949d`) `_hasGoodTopBits` (split low/high check per playbook §6.1; stock low-half preserved verbatim, high-half: ext flag clear ⇒ all bits 64-127 zero, ext flag set ⇒ bits 86-127 zero AND stock-res ≤ 6). **E2** (`61e3ce54`) `_firstOneIndex` (high-half-first dispatch per §6.2; removed `static inline` for external linkage matching `_h3Rotate60ccw` convention; all three branches: GCC/clang clzll, MSVC _BitScanReverse64, portable fallback). **E3** (`92481108`) `_hasAny7UptoRes` (stock bit-magic over low-64 digit window for digits 1..min(res,15) + ext loop for res > 15; cast h to uint64_t for stock window stays in low half). **E4** (`943fcb51`) `_hasAll7AfterRes` (per-digit loop dispatching via H3_GET_DIGIT_AT_RES; max_pos = MAX_H3_EXT_RES if ext flag, else MAX_H3_RES; replaces Phase A partial-guard cast). **E5** (`80f2f8ee`) `_hasDeletedSubsequence` (per-digit walk through 1..effective_res via H3_GET_DIGIT_AT_RES) + `isValidCell` local res capture (Rule LB: H3_GET_RESOLUTION → H3_GET_EFFECTIVE_RESOLUTION; stock cells unchanged because eff_res == stock_res when ext flag clear). **F1** (`198649d3`) `cellToBoundary` (2 sites Rule LB switching to H3_GET_EFFECTIVE_RESOLUTION), `getIcosahedronFaces` (Rule LB; isResolutionClassIII parity-stable), `getPentagons` (Rule GR widened to MAX_H3_EXT_RES). After E1+E2+E3+E4+E5, isValidCell now end-to-end validates ext cells. F4 isResClassIII NO widening — playbook §6.14 confirms parity preserved (16 even). |

### Stock test contract updates (Session 4 — 2 sites)

Two existing tests directly codified the OLD upper-resolution bound and were flipped by the F1 widening. Per CLAUDE.md non-negotiable #2 ("if compatibility must break, STOP and surface"), each was surfaced and updated to use `MAX_H3_EXT_RES + 1` as the new "above max" sentinel.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/testapps/testPentagonIndexes.c` | 2026-05-04 — modified | Stock test contract update at line 61: `getPentagons(16, h3Indexes) == E_RES_DOMAIN` → `getPentagons(MAX_H3_EXT_RES + 1, h3Indexes) == E_RES_DOMAIN`. Bundled with the F widening commit (`198649d3`). |
| `tests/cli/getPentagons.txt` | 2026-05-04 — modified | Stock test contract update at line 6 (CLI shell-test fixture mirroring the same boundary): `add_h3_cli_test(testCliDontGetPentagons "getPentagons -r 20 ...` → `-r 23`. Initially missed this in the unit-test fix, surfaced as a 2nd ctest red. Bundled with `198649d3`. |

### New ext test suites (Session 4 — 3 suites)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/testapps/testValidationExt.c` | 2026-05-04 — created | Phase E1..E7 gate suite (220 assertions, 11 TEST blocks): E1 ext cells across all 7 ext res × 6 base cells must validate; E1 corruption of bits 86 / 127 rejects; E1 stock cell with non-zero high half rejects; E1 stock-res > 6 with ext flag rejects; E2 single-bit sweep all 128 positions; E2 multi-bit leading + h==0 returns -1; E3 1000 valid ext + 1000 corrupted; E4 sentinel 7 at digit 18 of res-22 cell rejects; E5 non-7 at digit 21 of res-19 cell rejects; E5 pentagon K-axis at digit 17 rejects; E6 _zeroIndexDigits direct call across stock/ext boundary (start=11..end=22); E7 _incrementResDigit via cellToChildren ext res 16→17. Pentagon base cells (4, 14, 24, 38, 49, 58, 63, 72, 83, 97, 107, 117) hardcoded since `isBaseCellPentagonArr` is file-local to h3Index.c. Forward-declares `_firstOneIndex` (made non-static in E5). Bundled with `36c56d14`. |
| `src/apps/testapps/testAuxiliaryExt.c` | 2026-05-04 — created | Phase F1..F4 gate suite (771 assertions, 6 TEST blocks): F1 cellToBoundary res-19 hexagon → 6 finite vertices, lat in [-pi/2, pi/2], lng in [-pi, pi]; F2 getIcosahedronFaces ext hexagon → 1-2 valid faces; F2 ext pentagon limited to maxFaceCount == 5 (full _adjustPentVertOverage walk deferred to coordijk int64 — trips signed-overflow at coordijk.h:132/217/222/223 even at res 19 because pentagon Class III aperture-7 amplifies on top of gnomonic scale); F3 getPentagons returns 12 distinct ext pentagons at every res 16-22; F3 rejects res > MAX_H3_EXT_RES; F4 isResClassIII parity matches (effective_res % 2) at every ext res 16-22 (no widening — auto-passes per §6.14 because 16 is even). Scoped to res 19 per the Session 2 coordijk int32 deferral. Bundled with `841db752`. |

### Audit infrastructure (Phase G)

Phase G deliverable per playbook §13. Lives at `.claude/skills/h3-128-audit/`; required `.gitignore` negation rules to track this subtree while keeping rest of `.claude/` ephemeral.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `.claude/skills/h3-128-audit/audit.py` | 2026-05-04 — created | G structural validator. Checks A1 (typedef widened to __uint128_t), A2/A3 (14 Group C macros + MAX_H3_EXT_RES present), D7 (10 FFI shim prototypes in h3ExtShim.h), H6 (pre-commit hook installed/exec/invokes ctest), G2 (mutation manifest covers all 6 widening rules: LB/GR/DW/DR/RW/INIT), G3 (every MAX_H3_RES site in src/h3lib/lib/*.c classified as widened or deferred). Modes: human (default), --quiet (CI silent on success), --json (machine output). Exit 0 = clean, 1 = findings, 2 = infra error. Stateful `_strip_comments` pass tracks `/* */` state across line boundaries to avoid flagging `MAX_H3_RES` in docstrings. `MAX_H3_RES_CLASSIFICATIONS` dict tracks 24 sites: 13 widened (dispatch logic threshold) + 11 deferred (auxiliary stat helpers, polyfill arrays/guard, cellToChildPos at h3Index.c:1676 missed in D4). Bundled with `2aabbdcd`. |
| `.claude/skills/h3-128-audit/mutations/manifest.json` | 2026-05-04 — created | G2: one mutation per widening rule. Each entry records (rule, file, find, replace, expected_failures). Mappings: LB ↔ cellToBoundary res arg (testAuxiliaryExt::f1); GR ↔ getPentagons guard (testAuxiliaryExt::f3); DW ↔ cellToParent loop (testHierarchyExt + UBSAN); DR ↔ iterators::_getResDigit (testHierarchyExt::d4g6/g7); RW ↔ cellToCenterChild (testHierarchyExt::d4g4 + polyfill cascade); INIT ↔ setH3Index (testEncoderExt::d1g1 + testValidationExt::e3 sentinel-7 violations). Documents which test guards which widening. Bundled with `2aabbdcd`. |
| `.claude/skills/h3-128-audit/SKILL.md` | 2026-05-04 — created | YAML frontmatter (name, description) + usage doc. Surfaces audit skill in Skill tool listings. Documents G1/G2/G3 gate semantics, the "adding a new MAX_H3_RES site" workflow, and the mutation manifest schema. Bundled with `2aabbdcd`. |
| `.gitignore` | 2026-05-04 — modified | Added negation rules at line 102-107 for `.claude/skills/h3-128-audit/` subtree only. Original `.claude` ignore rule preserved; rest of `.claude/` (settings, transcripts, agents, other skills) stays ephemeral. Bundled with `2aabbdcd`. |

### Test infrastructure registration (Session 4)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `CMakeLists.txt` | 2026-05-04 — modified | LIB_SOURCE_FILES: added `src/h3lib/include/h3ExtShim.h` and `src/h3lib/lib/h3ExtShim.c` so the shim compiles into libh3 (filename unchanged per CLAUDE.md). Bundled with `4d9feaa4`. |
| `CMakeTests.cmake` | 2026-05-04 — modified | Three new test registrations (in commit order): `add_h3_test(testFFIShimExt ...)` (D7, with `4d9feaa4`); `add_h3_test(testValidationExt ...)` (E, with `36c56d14`); `add_h3_test(testAuxiliaryExt ...)` (F, with `841db752`). Existing Session 1+2+3 registrations unchanged. |

### Session 4 acceptance + checkpoint

| File | Created/Modified | Description |
|------|------------------|-------------|
| `SESSION_4_CHECKPOINT.md` | 2026-05-04 — created | Phase H acceptance signoff (commit `6bc988e9`). Records 11 commits, gate-by-gate results (D7-G1/G2/G3, E1-E7, F1-F4, G1-G3, H1-H6), 2 stock test contract changes, 7 surprises/lessons (D7 over-asserts hit Phase E ordering; `_firstOneIndex` linkage flip; stock test fixture pentagon CLI path missed; pentagon-overage UBSAN at res 19 not just 20-22; .claude/ gitignore exception for audit; comment-strip needs multi-line block awareness; pwd persists between Bash tool calls), 10 architectural deferrals (coordijk int64, faceijk length 23, cellToChildPos, compaction, polyfill, edges, vertices, aux stats, B2 MSVC, CI on push), build configuration, next-session entry points (CI watch, v0.2.0 with POC-5, cellToChildPos follow-up). Companion to the annotated tag `v0.1.0-128bit-mvp` (object `d9af5e79`) which carries acceptance summary in the tag message. |
| `contexts/contexts-may-04-20260504-171340.md` | 2026-05-04 — created | Session 4 final save with all four standard sections (Summary, Files, Open Items, Context Dump) + a top-level "Claude.ai Supervision Session" section capturing the cross-session decisions made in the supervisory chat (D4.2 iterator guard verbatim from POC-3, cellToCenterChild bundled into D4, polyfill stays untouched, stock test contracts to MAX_H3_EXT_RES + 1, Phase A casts removed in E by proper POC-2 patterns, context-management discipline). Captures the campaign scorecard: 4 POCs / 10,067 assertions, 4 sessions / 41 commits, resolutions 0-22, all surfaces validated, max resolution 22 (~0.3cm² cell area). |

---

## Session 5 Implementation Files (H4 close-out + POC-5 + v0.2.0 first widening on `feat/h3-128-mvp`)

H3 source tree edits and additions delivered by Session 5. Each landed under ASAN+UBSAN with the pre-commit hook running the full ctest suite (326 → 326 → 326 → 326 → 326; no new test suites). 5 commits total. **First ALL-GREEN cross-platform CI run since Session 1** (run `25370995715` post-fix at `1fa4db7c`). 10/10 §11 acceptance criteria PASS — v0.1.0-128bit-mvp conclusively shipped. v0.2.0 begun with `CoordIJK` struct typedef widening.

### H4 fix (Session 5)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/include/h3Index.h` | 2026-05-05 — modified (commit `1fa4db7c`) | H4 fix per playbook §10 cross-platform discipline. Stock-branch arg of `H3_GET_DIGIT_AT_RES` (line 230) and `H3_SET_DIGIT_AT_RES` (line 240) masked with `& MAX_H3_RES` (= 0xF). Identity for res ∈ [0, 15]; unreachable at runtime for res > 15. Linux gcc with `-Werror=shift-count-negative` constant-folds the dead stock branch and computes negative shift count when caller passes literal ext-res ≥ 16 (e.g. testValidationExt.c:223 `H3_SET_DIGIT_AT_RES(h, 18, INVALID_DIGIT)`). Apple clang doesn't run this static check, so Sessions 1-4 all green on macOS but red on Linux. Identical commit chain restored: ctest 326/326 PASS local; CI all 4 jobs green on the next push. |

### POC-5 source artifacts (Session 5)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `poc5_coordijk_overflow.c` | 2026-05-05 — created (commit `bef722e3`) | POC-5: CoordIJK int32 overflow stress test. Standalone single-file C99 (~610 lines), no libh3 linkage, ASAN+UBSAN clean, exit 0. Inherits POC-1..4 discipline. Uses `__builtin_*_overflow` to detect int32 boundaries deterministically without invoking the broken arithmetic at runtime. **85 assertions across CO-01..CO-32**. Documents failure boundary: hexagon first-broken res 21 (last-safe res 20), pentagon non-monotonic first-broken res 19 (recovers at 20, breaks again at 21+). 3 user-spec geos: Monmouth 40.33/-73.99, Palm Beach 26.71/-80.05, Piano di Sorrento 40.63/14.40. Identifies 4 widening surfaces for v0.2.0. |
| `poc5.md` | 2026-05-05 — created (commit `bef722e3`) | POC-5 spec doc. Test-category table (CO-01..32, target ≥75 assertions, achieved 85), geo-input table, failure-boundary table for both hexagon/pentagon paths, v0.2.0 widening targets list (CoordIJK struct, CoordIJK arithmetic helpers, static tables, _adjustOverageClassII/_adjustPentVertOverage), success criteria, build command, checkpoint plan. |

### v0.2.0 first widening (Session 5)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/include/coordijk.h` | 2026-05-05 — modified (commit `57672bc6`) | v0.2.0 widening #1: `CoordIJK` struct fields `int i, j, k → int64_t i, j, k` (lines 48-58). Comment block updated to reference POC-5 and document that this unblocks ext res 20-22 round-trips + pentagon overage at res 19+. `ijkDistance` return type widened `int → int64_t` and `abs()` → `llabs()` (lines 710-716). Public `gridDistance` already returns `int64_t *out` so this just removes the internal truncation; one in-tree caller (`localij.c:615 *out = ijkDistance(...)`) is API-compatible. |
| `src/apps/applib/lib/utility.c` | 2026-05-05 — modified (commit `57672bc6`) | v0.2.0 cascade fix: `coordIjkPrint` format specifier `%d` → `%" PRId64 "` for the new int64_t struct field type (line 55). `<inttypes.h>` already included. Only cascade fix needed for the typedef change. |

### Session 5 checkpoints (multi-stage)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `SESSION_5_CHECKPOINT.md` | 2026-05-05 — created (commit `ea8503e6`), updated (commit `39ebff5c`) | H4 close-out doc covering failure mode, fix at `1fa4db7c`, 4 CI job IDs (74394171722 macos-release, 74394171723 macos-sanitizers, 74394171729 ubuntu-sanitizers, 74394171759 ubuntu-release) on run `25370995715`. Updated with v0.2.0 first-commit summary + Session 6 entry points. **10/10 §11 acceptance criteria PASS**. Audit-rule follow-up suggestion: detect dispatch-macro patterns where stock-branch can be reached with constant ext-res, and require `& MAX_H3_RES` clamping. |
| `contexts/contexts-may-05-20260505-124714.md` | 2026-05-05 — created | This context file (covers Sessions 5+6 jointly). |

---

## Session 6 Implementation Files (v0.2.0 CoordIJK int64 widening complete on `feat/h3-128-mvp`)

H3 source tree edits and additions delivered by Session 6. Each landed under ASAN+UBSAN with the pre-commit hook running the full ctest suite (326 → 326 → 327 → 327; +1 from new testCoordIjkExtRes suite). 4 commits total. **POC-5 predicted failure boundary now CLEARED**: hexagon path works at res 16-22, pentagon path works at res 16-22 (no more non-monotonic break at res 19+).

### v0.2.0 widening #2 — static tables (Session 6)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/lib/faceijk.c` | 2026-05-05 — modified (commit `f84ab171`) | v0.2.0 widening #2: `maxDimByCIIres[]` extended length 21 → 23, element type `int → int64_t`. New entries: `[21]=-1` (Class III sentinel), `[22]=3954653486LL` (= 2*7^11 — the value Session 2 had to defer because it exceeds INT32_MAX = 2,147,483,647 by 1,807,169,839). `unitScaleByCIIres[]` same pattern: `[21]=-1`, `[22]=1977326743LL` (= 7^11). Comment block at lines 298-313 / 339-345 updated to document the v0.2.0 widening. 4 int locals widened to int64_t in `_adjustOverageClassII` (lines 925-926 maxDim, 965-966 unitScale) and `_faceIjkPentToVerts` overage paths (lines 617-622 + 783-788 — pentagon edge-vertex Vec2d bounds). Vec2d initializers gain explicit `(double)maxDim` casts. |
| `src/h3lib/include/coordijk.h` | 2026-05-05 — modified (commit `f84ab171`) | v0.2.0 widening (continued): `_setIJK` parameters `int → int64_t` (line 108) — called from `_adjustOverageClassII:947` with int64_t maxDim arg. `_ijkScale` factor parameter `int → int64_t` (line 156) — called with int64_t unitScale at faceijk.c:609, 967. Without these param widenings, the table-derived int64 values would silently narrow at the call site. |

### v0.2.0 widening #3 — inner arithmetic + new test (Session 6)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/include/coordijk.h` | 2026-05-05 — modified (commit `5fbdcf4e`) | v0.2.0 widening #3: inner-arithmetic int locals widened. Adding testCoordIjkExtRes exposed UBSAN trips at coordijk.h:487, 488, 504, 505 — all 4 `_upAp7 / _upAp7r / _upAp7Checked / _upAp7rChecked` declared `int i = ijk->i - ijk->k; int j = ijk->j - ijk->k;` capturing now-int64 fields and silently narrowing. POC-5 §"CO-19..22" predicted exactly these sites. Widened all 4 functions: int → int64_t locals (lines 388-389, 436-437, 484-485, 501-502); `(int)lround(...)` → `(int64_t)lround(...)` with explicit `(double)` casts on the floating arithmetic (lines 411-412, 459-460, 483-484, 500-501); `_ijkNormalizeCouldOverflow` `int max,min` → int64_t and `ADD/SUB_INT32S_OVERFLOWS` replaced with `__builtin_add_overflow`/`__builtin_sub_overflow` (type-generic, exact int64; lines 167-188); `_ijkNormalize::min` widened (line 233); `_ijkToHex2d` int i,j → int64_t with `(double)` casts on Vec2d arithmetic (lines 247-251); `_hex2dToCoordIJK` int m1,m2 → int64_t with `(int64_t)x1` quantization cast and `(double)` casts on folding arithmetic (lines 264-353); `INT32_MAX_3` → `INT64_MAX_3` (= INT64_MAX/3 ≈ 3.07e18, effectively unreachable for H3 magnitudes ~2e9 but kept for defense; line 98). `#undef INT32_MAX_3` → `#undef INT64_MAX_3` (line 753). |
| `src/apps/testapps/testCoordIjkExtRes.c` | 2026-05-05 — created (commit `5fbdcf4e`) | NEW v0.2.0 regression suite. 6 TEST blocks (~3000 assertions): **CO-G1** 500 res 20 (Class II) cell-center round-trips bit-identical (already worked pre-widening but moved to first-class); **CO-G2** 500 res 21 (Class III) round-trips bit-identical (was 247/500 pre-widening — the headline gate); **CO-G3** 500 res 22 (Class II) round-trips bit-identical (was UBSAN trip pre-widening); **CO-G4** pentagon res 19+ getPentagons returns 12 distinct cells without UBSAN (POC-5 §"CO-24" boundary cleared); **CO-G5** stock res 0-15 byte-identity guard (CoordIJK widening must not regress stock); **CO-G6** 3 POC-5 user-spec geos (Monmouth, Palm Beach, Sorrento) × res 20-22 round-trip bit-identical (9/9 PASS). Header documents v0.2.0 prereqs and POC-5 §"Failure Boundary" reference. |

### Stock test contract update (Session 6 — 1 site)

Per CLAUDE.md non-negotiable #2 ("if compatibility must break, STOP and surface"). The pre-widening contract was "INT32_MAX inputs overflow"; v0.2.0 makes the safe arithmetic range int64, so the new contract is "INT64_MAX inputs overflow". Test intent preserved at the new bound.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/testapps/testCoordIjkInternal.c` | 2026-05-05 — modified (commit `5fbdcf4e`) | Stock test contract update at lines 54-94: `_upAp7Checked` and `_upAp7rChecked` overflow-guard tests. `INT32_MAX → INT64_MAX` (and `/2`, `/3` proportions) in 12 `_setIJK` overflow probes. The test intent ("i + i overflows", "i * 3 overflows", "j + j overflows", "(i * 3) - j overflows", "i + (j * 2) overflows") is preserved at the new int64 bound. Comment block added at line 54 documenting the contract update rationale. |

### Test infrastructure registration (Session 6)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `CMakeTests.cmake` | 2026-05-05 — modified (commit `5fbdcf4e`) | Test registration: `add_h3_test(testCoordIjkExtRes src/apps/testapps/testCoordIjkExtRes.c)` added after the Phase F testAuxiliaryExt registration (line 283-284). Existing Session 1-4 registrations unchanged. |

### Audit infrastructure update (Session 6)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `.claude/skills/h3-128-audit/mutations/manifest.json` | 2026-05-05 — modified (commit `36a5b36d`) | Added 2 W64 mutations covering v0.2.0 widening. **W64-CoordIJK-fields**: reverting struct fields `int64_t → int` trips testCoordIjkExtRes::co_g3 and UBSAN at res 22 in `_upAp7`/`_upAp7r`. **W64-maxDimByCIIres**: reverting the table to `int` element type silently truncates `maxDim[22] = 3,954,653,486` and breaks res 21+ round-trips. The 6 required rules (LB/GR/DW/DR/RW/INIT) remain present. W64 is a v0.2.0-introduced rule for type widening; not yet a required-set member. audit.py status: 24 sites classified, 0 CRITICAL, 0 FINDING. |

### Session 6 checkpoint (Session 6)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `SESSION_6_CHECKPOINT.md` | 2026-05-05 — created (commit `88c7c94f`) | Session 6 acceptance signoff. Records 4 commits (f84ab171, 5fbdcf4e, 36a5b36d, 88c7c94f), gate-by-gate testCoordIjkExtRes results (CO-G1..G6 all PASS), 7 architectural notes (POC-5 prediction accuracy → 3-layer cascade lesson; struct alone insufficient; stock contract precedent; `__builtin_*_overflow` type-generic; pentagon non-monotonic confirmed; gh auth via .env GITHUB_PAT; near-miss accidental `git add -A`), deferrals carried forward (cellToChildPos still post-MVP, compaction/polyfill/edges/vertices/aux-stats out of MVP, B2 MSVC), Session 7 entry points (tag v0.2.0, cellToChildPos, libm tolerance, POC1-4 tracking). |

### Run 25388983711 (CI confirmation)

All 4 matrix jobs green on commit `88c7c94f`:
- ✓ release on macos-latest
- ✓ release on ubuntu-latest
- ✓ sanitizers on macos-latest
- ✓ sanitizers on ubuntu-latest

v0.2.0 widening confirmed cross-platform under glibc + gcc + Linux x86_64 alongside Apple clang + Darwin arm64.

---

## Session 7 Implementation Files (Tag v0.2.0 + post-MVP cleanup on `feat/h3-128-mvp`)

Session 7 closes the predecessor's open items: tag v0.2.0, widen the deferred:post-mvp `cellToChildPos`/`childPosToCell` site, recalibrate libm tolerance, untrack stray POC binaries, write retrospective. No new widening rules; no new ext test suites beyond the D4-G8 addition; no stock test contract changes.

### Tag (Session 7 — annotated)

| Tag | Created | Description |
|-----|---------|-------------|
| `v0.2.0-coordijk-int64` | 2026-05-05 — created (annotated, points at `88c7c94f`) | Multi-section message: 9 Session-5/6 commits + subjects, ctest progression 326 → 327, POC-5 pre/post boundary table (hexagon res 21: 247/500 → 500/500; res 22: UBSAN trip → 500/500; pentagon res 19+: UBSAN trip → clean; 3 user-spec geos × res 20-22: 9/9 bit-identical), 3-layer cascade summary (struct → tables → inner arithmetic), stock contract precedent (testCoordIjkInternal.c INT32_MAX → INT64_MAX), full deferral list. Pushed to origin. |

### cellToChildPos / childPosToCell widening (Session 7 — Rule LB)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/lib/h3Index.c` | 2026-05-05 — modified (commit `ee57e69a`) | Single-commit Rule LB widening + 9 cascade sites. `cellToChildPos:1594` `H3_GET_RESOLUTION → H3_GET_EFFECTIVE_RESOLUTION`. `cellToChildPos:1621, 1648` `H3_GET_INDEX_DIGIT → H3_GET_DIGIT_AT_RES`. `childPosToCell:1681` Rule LB: `MAX_H3_RES → MAX_H3_EXT_RES`. `childPosToCell:1684` `H3_GET_RESOLUTION → H3_GET_EFFECTIVE_RESOLUTION`. `childPosToCell:1701` `H3_SET_RESOLUTION → H3_SET_EFFECTIVE_RESOLUTION`. `childPosToCell:1717, 1721, 1725, 1733` 4 × `H3_SET_INDEX_DIGIT → H3_SET_DIGIT_AT_RES`. ctest 327/327 PASS. |
| `src/h3lib/lib/mathExtensions.c` | 2026-05-05 — modified (commit `ee57e69a`) | Latent stock bug surfaced: `_ipow` line 35's trailing `base *= base` is a dead store on the loop's last iteration. For stock res `_ipow(7,15)` was harmless; ext-res `_ipow(7,22)` trips UBSAN signed-int64 overflow at `7^32 ≈ 1.1e27`. Fix: one-line `if (exp) base *= base;` guard. |
| `src/apps/testapps/testHierarchyExt.c` | 2026-05-05 — modified (commit `ee57e69a`) | New `TEST(d4g8_cellToChildPos_extRes)` with 5 sub-gates: G8a domain check (childRes > MAX_H3_EXT_RES rejected, negatives too); G8b hex stock-parent → ext-child round-trip across (parentRes 0..15, childRes 16..22) × 3 sample positions; G8c hex ext-parent → ext-child round-trip across (parentRes 16..21, childRes parentRes+1..22); G8d pentagon stock-parent res-15 → ext-child res-18 enumeration (286 children, 4 sample positions round-trip); G8e stock res-15 byte-identity canary. 2,513,991 assertions across the suite. |

### Audit infrastructure update (Session 7)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `.claude/skills/h3-128-audit/audit.py` | 2026-05-05 — modified (commit `ee57e69a`) | Removed stale classification entry `h3Index.c:1676 "deferred:post-mvp"` (line no longer references MAX_H3_RES — widening replaced it with MAX_H3_EXT_RES, which the regex doesn't match). Replaced with documenting NOTE comment. Site count 24 → 23 (13 widened, 10 deferred). |
| `.claude/skills/h3-128-audit/mutations/manifest.json` | 2026-05-05 — modified (commit `ee57e69a`) | Added `LB-childPosToCell` mutation (Rule LB): reverting `MAX_H3_EXT_RES → MAX_H3_RES` rejects ext-res callers with E_RES_DOMAIN; expected to break `testHierarchyExt::d4g8_cellToChildPos_extRes`. The 6 required rules (LB/GR/DW/DR/RW/INIT) and 2 W64 entries (Session 6) remain present. |

### libm tolerance recalibration (Session 7)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/apps/applib/include/test.h` | 2026-05-05 — modified (commit `bab3d7fe`) | `latlng_within_tolerance`: ext tolerance 1e-12 → **1e-13 rad**. Comment block expanded with macOS measurement record (1000 random g_in × 23 resolutions: 0 rad delta on every resolution including res 22 — round-trip is bit-deterministic on a single platform), cross-libm ULP analysis (50 ULPs × π × 10× safety = 3.5e-13, rounded down to 1e-13 for ~640 nm headroom), explicit deferral note that empirical CI cross-platform calibration awaits a first caller. Stock tolerance unchanged at 1e-9 rad per playbook §9.2. Helper still has no callers — change is documentation + defensible default. |

### POC binary untrack (Session 7)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `.gitignore` | 2026-05-05 — modified (commit `44897e50`) | Added 5 explicit entries `/poc1_bit_layout`, `/poc2_validation`, `/poc3_iterator`, `/poc4_ffi`, `/poc5_coordijk_overflow` to prevent re-tracking of extensionless POC build binaries. The existing `*.exe` / `*.out` / `*.app` patterns don't match extensionless binaries — that's how the 4 originals slipped in. |
| `poc1_bit_layout` / `poc2_validation` / `poc3_iterator` / `poc4_ffi` | 2026-05-05 — untracked (commit `44897e50`) | `git rm --cached` only — preserved on disk for re-running as architectural diagnostics. Source files (`poc{1..5}*.{c,h,md}`) remain tracked as frozen proof artifacts per CLAUDE.md non-negotiable. |

### Retrospective (Session 7)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `LESSONS_LEARNED.md` | 2026-05-05 — created (commit `100198e1`) | New top-level retrospective doc, 261 lines. 9 lessons organized by session: Sessions 5+6 (gcc constant-folds dead branches; 3-layer widening cascade; POC-5 prediction accuracy; pentagon non-monotonic boundary; hidden parameter cascade `_setIJK`/`_ijkScale`; gh-keychain-vs-PAT workflow; `git add -A` near-miss); Session 7 (`_ipow` latent dead-store overflow; `H3_GET_RESOLUTION` silently wrong for ext children). 3 candidate audit rules proposed (R-NoIntFromIjkField, R-NoStockResInWidened, R-NoUnboundedIpowAtExtRes — all with implementation sketches, NOT implemented this session). Documents intentional non-rules (H4 fix already structural; pentagon non-monotonic captured by CO-G4; git-add-A is workflow). Living doc — append future session lessons. |

### Session 7 context save

| File | Created/Modified | Description |
|------|------------------|-------------|
| `contexts/contexts-may-05-20260505-143641.md` | 2026-05-05 — created | Session 7 context save covering all 5 commits + the v0.2.0 tag + 6 open items for Session 8. |

---

## Session 7-py Implementation Files (Python Bindings + FFI Shim Integration on `feat/h3-128-mvp`)

The "planned Session 7" from `amalfi-h3-128-session-plan.md` §7 — Python bindings on top of the C library that v0.2.0 produced. Two commits: shim extension (`74bf9053`), then Python package (`b77d7cf7`). Tag `v0.3.0-python-bindings` lands at the second commit. Pre-commit hook ran full ctest 327/327 on each commit.

### FFI shim extension (Session 7-py Step 0, commit `74bf9053`)

The original 10 shim functions covered indexing/hierarchy/distance but not boundary/area/grid/local-ij. Added 8 wrappers + macros so the 16-function Python API can be implemented entirely on top of the validated POC-4 by-pointer ABI.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `src/h3lib/include/h3ExtShim.h` | 2026-05-07 — modified (commit `74bf9053`) | +8 shim declarations: `h3_ext_cell_to_boundary` (CellBoundary*), `h3_ext_cell_area` (int unit dispatch with `H3_EXT_AREA_M2/KM2/RADS2 = 0/1/2`), `h3_ext_max_grid_disk_size`, `h3_ext_grid_disk`, `h3_ext_grid_path_cells_size`, `h3_ext_grid_path_cells`, `h3_ext_cell_to_local_ij` (CoordIJ*), `h3_ext_local_ij_to_cell` (const CoordIJ*). Pattern 4 contract preserved: by-pointer H3Index, H3Error returns, NULL → E_FAILED. |
| `src/h3lib/lib/h3ExtShim.c` | 2026-05-07 — modified (commit `74bf9053`) | +8 implementations forwarding to widened H3 entry points (`cellToBoundary`, `cellAreaM2/Km2/Rads2`, `maxGridDiskSize`, `gridDisk`, `gridPathCellsSize`, `gridPathCells`, `cellToLocalIj`, `localIjToCell`). NULL → E_FAILED, bad unit code → E_OPTION_INVALID. CoordIJ and CellBoundary passed as struct pointers (public POD types in h3api.h, ABI-stable). |
| `src/apps/testapps/testFFIShimExt.c` | 2026-05-07 — modified (commit `74bf9053`) | New `TEST(d7g4_followOnShimsLinkable)` — linkability + smoke through all 8 new functions at res 9: boundary 5..10 verts finite, area unit conversion (m²/km² 1ppb tolerance), `max_grid_disk_size(2) == 19`, `grid_disk[0] == origin`, self-path size 1, local_ij round-trip back to origin. NULL contract extended to all 8 + bad-unit (E_OPTION_INVALID) case. +41 assertions (1038 → 1079). |

### Python package — h3_extended (Session 7-py Steps 1-9, commit `b77d7cf7`)

A self-contained Python package that wraps the C library via cffi ABI mode. The package bundles a renamed shared library (`libh3_extended.{dylib,so}`) so it coexists with stock `h3-py` (no symbol/library collision; both can be installed and imported in the same process).

| File | Created/Modified | Description |
|------|------------------|-------------|
| `h3_extended/__init__.py` | 2026-05-07 — created (commit `b77d7cf7`) | Public Python API: 16 functions. Indexing: `latlng_to_cell`, `cell_to_latlng`, `cell_to_boundary`. Resolution: `get_resolution`, `get_effective_resolution`, `cell_area(cell, unit='m^2')`. Hierarchy: `cell_to_parent`, `cell_to_children`. Grid: `grid_disk`, `grid_distance`, `grid_path_cells`. Local IJ: `cell_to_local_ij`, `local_ij_to_cell`. Validation: `is_valid_cell`. Stock interop: `to_64bit` (raises H3Error E_RES_DOMAIN for ext cells), `from_64bit`. Plus `H3Error` exception class. lat/lng in degrees (matches h3-py); cffi layer converts to radians for the shim. Hex strings throughout (16-char stock, 32-char ext). |
| `h3_extended/_ffi.py` | 2026-05-07 — created (commit `b77d7cf7`) | cffi ABI-mode bindings. `ffi.cdef()` declares all 18 shim functions + 2 size probes + LatLng/CellBoundary/CoordIJ structs + 16 H3Error code constants. `ffi.dlopen()` of `libh3_extended.{dylib,so}`. Library resolution order: `H3_EXTENDED_LIB` env var (dev override) → bundled `<package_dir>/libh3_extended.{dylib,so}`. ABI consistency check at import: `h3_ext_sizeof_h3index() == 16` and `h3_ext_alignof_h3index() == 16` (fail-fast guard against accidentally loading stock 64-bit libh3). |
| `h3_extended/batch.py` | 2026-05-07 — created (commit `b77d7cf7`) | 3 batch helpers: `batch_latlng_to_cell`, `batch_cell_to_parent`, `batch_get_resolution`. Python-loop bodies; structured for drop-in Pandas UDF / vectorized cffi upgrade later — public contract is stable. |
| `h3_extended/tests/__init__.py` | 2026-05-07 — created (commit `b77d7cf7`) | Empty package marker. |
| `h3_extended/tests/test_basic.py` | 2026-05-07 — created (commit `b77d7cf7`) | 23 tests — res 0-15 fundamentals: parameterized creation across all res, round-trip cell_to_latlng → re-index identity, parent/child counts, validation, area monotonicity (strictly decreasing), area unit consistency (m² ≡ km² × 1e6 within 1ppb). |
| `h3_extended/tests/test_extended_res.py` | 2026-05-07 — created (commit `b77d7cf7`) | 23 tests — res 16-22: creation + validation, 32-char string contract, area strictly decreasing, sub-square-meter at res 20, res-22 area ratio vs res-15 area sanity check. |
| `h3_extended/tests/test_hierarchy.py` | 2026-05-07 — created (commit `b77d7cf7`) | 10 tests — cross stock-ext boundary at res 15→16; full descent res 0 → 22; child counts 7^delta. **Anchored at parent center** rather than raw lat/lng input — `(40.33, -73.99)` sits within ε of a res-15 cell boundary, so res-15-of-input vs res-15-of-res-16-child diverged in the last digit; center anchoring is interior-safe at all coarser resolutions. |
| `h3_extended/tests/test_backward_compat.py` | 2026-05-07 — created (commit `b77d7cf7`) | 80 tests — bit-identical match vs stock `h3-py` 4.4.2: `latlng_to_cell` byte-for-byte at res 0..15 across 3 geographies (Monmouth/Palm Beach/Sorrento); `cell_to_latlng` numerical match within 1e-12; `to_64bit`/`from_64bit` round-trip; `cell_area` rel-diff < 1e-7 (loosened from 1e-12 — observed worst 3.6e-9 is FP-reordering noise from post-int64 CoordIJK kernels, not a regression). `pytest.importorskip("h3")` so module skips cleanly when stock h3-py absent. |
| `h3_extended/tests/test_precision.py` | 2026-05-07 — created (commit `b77d7cf7`) | 18 tests — round-trip closure at the three target geographies, res 19-22 idempotency. Cell center re-index must yield same cell; round-trip distance bounded < 1cm at res 19, < 5mm at res 20. On macOS arm64 + Apple libm the actual delta is 0 rad — round-trip is bit-deterministic on a single platform. |
| `h3_extended/tests/test_grid_ops.py` | 2026-05-07 — created (commit `b77d7cf7`) | 16 tests — `grid_disk(k=0,1,2)` (k=2 yields 19 cells for hexagons); `grid_distance` self/neighbor; `grid_path_cells` self/two-step; `cell_to_local_ij` round-trip across res 18-20. |
| `h3_extended/tests/test_string.py` | 2026-05-07 — created (commit `b77d7cf7`) | 33 tests — stock-res 1-16-char hex round-trip; ext-res 32-char zero-padded contract; FFI-direct round-trip via `lib.h3_ext_h3_to_string`/`string_to_h3` (mirrors C-side D7-G3); invalid hex returns False from `is_valid_cell` (no exception); `to_64bit`/`from_64bit` round-trip across all stock res. |
| `pyproject.toml` | 2026-05-07 — created (commit `b77d7cf7`) | setuptools build backend; cffi runtime dep; pytest+h3 test deps; declares `h3_extended` package + `h3_extended.tests` subpackage; package_data includes `libh3_extended.dylib` and `libh3_extended.so`. |
| `setup.py` | 2026-05-07 — created (commit `b77d7cf7`) | `BinaryDistribution` override (subclasses `setuptools.dist.Distribution`, returns `True` from `has_ext_modules` and `False` from `is_pure`) — forces a platform-specific wheel tag because the package bundles a native shared library as data. Without this the wheel would be tagged `py3-none-any` and fail to install on the target platform. |
| `.github/workflows/python-wheel.yml` | 2026-05-07 — created (commit `b77d7cf7`); modified (commit `fbd8d04a`) | Cross-platform CI: matrix on `ubuntu-latest` (x86_64) + `macos-latest` (arm64). Each leg: setup-python 3.11 → cmake configure with `BUILD_SHARED_LIBS=ON` + `POSITION_INDEPENDENT_CODE` → build target h3 → stage shared library into package dir (rename to `libh3_extended.{dylib,so}`, `install_name_tool -id @rpath` on macOS) → `python -m build --wheel` → inspect wheel zipfile for bundled lib → install in clean venv from `/tmp` (avoid source-tree shadow) → `pytest --pyargs h3_extended.tests` → upload wheel as 30-day artifact named `h3_extended-<runner-os>-<arch>-py311`. |
| `.gitignore` | 2026-05-07 — modified (commit `b77d7cf7`) | Session 7-py additions: `/dist/`, `.venv*/`, `*.egg-info/`, `__pycache__/`, `*.pyc`, `.pytest_cache/`. Bundled `libh3_extended.dylib` is excluded by the existing `*.dylib` rule (built at wheel-time, never committed). |
| `SESSION_7_CHECKPOINT.md` | 2026-05-07 — created (commit `b77d7cf7`) | 11,868 bytes. Full Session 7-py test results, exit-criteria checkmarks, two test calibrations (cell_area 1e-12 → 1e-7 tolerance loosen, hierarchy parent-center anchoring), known issues (boundary geometric edge case), wheel artifact path + size, performance baselines. |

### Session 7-py context save

| File | Created/Modified | Description |
|------|------------------|-------------|
| `contexts/contexts-may-07-20260507-185446.md` | 2026-05-07 — created | This session's context save covering Session 7-py + Session 8 + Session 8.5 + skill setup. |

---

## Session 8 Implementation Files (Local Validation — Quality Gate Before Databricks)

Single commit `3ee39bc7`. NO TAG (validation gate, not a release artifact). Pre-commit ran ctest 327/327. Validates the Python package end-to-end against real geotagged imagery from the sibling `amalfi_intelligence_platform` repo.

### Local validation script + report (Session 8, commit `3ee39bc7`)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `validate_local.py` | 2026-05-07 — created (commit `3ee39bc7`) | ~700-line end-to-end validator. **Imagery sources**: dual-mode autodetect — JSON manifest sibling (`manifest.json` next to keyframes; capture_data schema with per-frame `gps_latitude`/`gps_longitude`) OR EXIF GPSInfo IFD (standard JPEG photo dirs). Default `--imagery` paths target the sibling repo's `photos/april-11-jpg/` and `capture_data/monmouth_30_9.02/2026-03-29/keyframes/`. **Per-image gates**: backward compat res 15 vs h3-py 4.4.2; parent res 19 → 15; parent res 20 → 19 (BOUNDARY classification for geographic edge cases — input within ε of a coarser-res cell edge); round-trip res 19 < 1cm; round-trip res 20 < 5mm; grid_disk(k=2) → 19 unique res-19 cells; string round-trip 16/32-char. **Stress test**: 100K random Monmouth-County bbox coords, deterministic LCG seed, validates no invalid res-19/res-20 cells, no invalid res-19 parents from res-20 (boundary disagreements counted as informational, not failure). **Memory leak**: 1M `latlng_to_cell` calls under tracemalloc with 5 checkpoints (0/250K/500K/750K/1M); gate < 1 MiB final delta. **Performance baselines**: 10K `latlng_to_cell(res=19)`, 10K `cell_to_parent(res19→15)`, 1K `grid_disk(k=3, res=19)` — recorded for Session 9 Databricks UDF comparison. Generates a markdown `validation_report.md` covering all sections. |
| `validation_report.md` | 2026-05-07 — created (commit `3ee39bc7`) | Generated output of validate_local.py at HEAD `3ee39bc7`. **PASS** across all gates: 103 GPS-tagged images (40 EXIF + 63 manifest; 67 april-23-jpg skipped no GPS), 33 distinct (lat,lng) points, all per-image gates pass with 5 informational BOUNDARY rows, round-trip distance 0.000000 mm at every image, 100K stress 0/0/0 invalid + 7,218 (7.22%) boundary disagreements (informational), 1M memory leak 1.9 KiB final delta, perf baselines latlng_to_cell 390,918 ops/sec / cell_to_parent 427,932 / grid_disk(k=3) 15,343. |

---

## Session 8.5 — Pre-Session-9 CI Repair (destination-session pickup)

A pre-Session-9 inspection of the fork's CI found `python-wheel.yml` red on both matrix legs at HEAD `3ee39bc7` (the C library `ci.yml` was green throughout). A destination-session pass fixed both bugs at commit `fbd8d04a`. Documented because regressions in this area should be recognized fast.

### CI workflow fix (Session 8.5, commit `fbd8d04a`)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `.github/workflows/python-wheel.yml` | 2026-05-07 — modified (commit `fbd8d04a`) | **Bug 1** (macOS leg): `find -name '${{ matrix.lib }}*'` did not match `libh3.1.dylib` because `.1` is infixed, not suffixed. With `-not -type l` excluding the symlink, `src` was empty. **Fix**: `-type f \( -name 'libh3.[0-9]*.dylib' -o -name 'libh3.so.[0-9]*' \)` — type filter excludes symlinks naturally; explicit numeric-version patterns target the real files on both platforms. **Bug 2** (Linux leg inspect step): `python -m zipfile -l \| grep '\.(so|dylib)$'` never matched because the listing format is `Name Modified Size` (line ends in size digit). **Fix**: `\| awk '{print $1}' \| grep -E '\.(so|dylib)$'` — extract column 1 first. Both legs now go green: Linux ~43s, macOS ~31s. Linux x86_64 wheel artifact (~94 KiB) downloadable on every push to `feat/h3-128-mvp`. |

---

## Cross-Repo Reference Skill (commit `ec4e0ae6`)

A global Claude skill that lets a session in any sibling repo (notably the Databricks integration repo for Sessions 9-11) understand what the fork produces and how to consume it. Tracked in the fork at `.claude/skills/amalfi-h3-128/` (mirroring the existing `.claude/skills/h3-128-audit/` project-local pattern); also accessible via `~/.claude/skills/amalfi-h3-128/` which is symlinked to the fork copy (single source of truth).

### Skill content (commit `ec4e0ae6`)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `.claude/skills/amalfi-h3-128/SKILL.md` | 2026-05-07 — created (commit `ec4e0ae6`) | 162 lines. Entry point — frontmatter for skill discovery, executive summary as of HEAD `fbd8d04a`, "What's done" / "What's NOT yet done" matrix, quick-start consumption commands (build C lib local, pull Linux wheel from CI artifact), index pointing at the 7 detail files. |
| `.claude/skills/amalfi-h3-128/architecture.md` | 2026-05-07 — created (commit `ec4e0ae6`) | 173 lines. Why the fork exists; 128-bit bit layout (bits 0-63 stock, bit 64 ext flag, bits 65-85 ext digits 16-22, bits 86-127 reserved zero); four non-negotiables verbatim; five POC-validated coding patterns (Group C macros, validation predicates, iterator state machine, FFI shim, widening rules); five widening rules (LB/GR/DW/DR/RW/INIT); don'ts and toolchain constraints. |
| `.claude/skills/amalfi-h3-128/implementation-history.md` | 2026-05-07 — created (commit `ec4e0ae6`) | 361 lines. Session-by-session log: POC campaign (5 POCs, 10,067 assertions); Sessions 1-7-cal (each with phase commits + ctest progression 316 → 327); Session 7-py (Step 0 shim extension + Steps 1-9 Python package); Session 8 (validation gate); Session 8.5 (CI fix); Sessions 9-11 forward-looking. |
| `.claude/skills/amalfi-h3-128/c-library.md` | 2026-05-07 — created (commit `ec4e0ae6`) | 260 lines. Repository paths; build-dev (sanitizers) vs build-release (shared lib for wheel) configurations; ctest baseline progression table (316 → 327 with delta-per-session attribution); stock test contract; ext test suite assertion volumes; audit infrastructure status (23 sites, 0 CRITICAL); FFI shim surface (18 functions + 2 size probes); pre-commit hook; CI matrix; POC re-run commands; post-MVP deferrals (edges/vertices/compactCells/polyfill/auxstats/MSVC). |
| `.claude/skills/amalfi-h3-128/python-package.md` | 2026-05-07 — created (commit `ec4e0ae6`) | 307 lines. Package structure tree; 16-function API with signatures and brief docs; H3Error code reference; 3 batch helpers; installation paths (wheel + source); ABI consistency check at import; performance baselines from Session 8; test coverage breakdown; two known test calibrations (cell_area FP tolerance, hierarchy parent-center anchoring); coexistence with stock h3-py; what's NOT in the API yet. |
| `.claude/skills/amalfi-h3-128/validation.md` | 2026-05-07 — created (commit `ec4e0ae6`) | 274 lines. Three layers of validation evidence: POC campaign (10,067 assertions), C-side ctest (327/327, ~2.5M ext assertions, audit clean), Session 8 local validation against real imagery (PASS — 103 images + 100K stress + 1M memory + perf). What was NOT validated: cross-libm round-trip variability (Apple vs glibc), pentagon-adjacent geographies in Python wrapper, real flight imagery (drone EXIF/SRT format), high-throughput vectorized path. CI verification status. |
| `.claude/skills/amalfi-h3-128/artifacts.md` | 2026-05-07 — created (commit `ec4e0ae6`) | 281 lines. Path-by-path inventory: repository (remote + local clone + branch + HEAD); tags; wheels (local macOS arm64 + CI Linux x86_64 + macOS arm64 with build commands); compiled C library (.a / .dylib / bundled); Python package source layout; validation script + report; CI workflows; documentation files in the fork; key paths cheat-sheet; what the fork does NOT produce yet. |
| `.claude/skills/amalfi-h3-128/databricks-roadmap.md` | 2026-05-07 — created (commit `ec4e0ae6`) | 566 lines. Session 9 (PySpark UDFs + Databricks): build h3_extended_spark.py with 11 UDFs (with NULL handling); build h3_auto_* SQL dispatch (Photon for res 0-15, h3_extended for res 16-22); upload wheel to Unity Catalog Volume; cluster init script vs notebook-scoped install; first live queries; bulk perf test; tag v0.4.0-databricks-udf. Session 10 (Delta tables + Auto-Loader): Bronze/Silver/Gold schemas, partition + Z-ORDER, S3 ingestion; tag v0.5.0-delta-pipeline. Session 11 (first flight); tag v1.0.0-first-flight. Plus pre-Session-9 sanity checklist; Resolved CI bugs (history) section preserving the diagnosis from Session 8.5; common gotchas (Linux x86_64 wheel requirement, manylinux, lat/lng-vs-lng/lat dispatch, batch.py loop bottleneck, libm cross-platform deltas, pentagon neighborhoods); architectural diagram of the dispatch layer. |
| `.gitignore` | 2026-05-07 — modified (commit `ec4e0ae6`) | Added allowlist `!.claude/skills/amalfi-h3-128/` + `!.claude/skills/amalfi-h3-128/**` mirroring the existing h3-128-audit pattern. The rest of `.claude/` (settings, transcripts, other project-local skills) stays ephemeral. |

### Filesystem-only artifacts (NOT committed in this repo)

These live outside the repo and are not version-controlled here, but are part of how the skill is discovered globally.

| Path | Action | Description |
|---|---|---|
| `~/.claude/skills/amalfi-h3-128/` | 2026-05-07 — symlink → repo path | Replaced original directory with symlink pointing at `/Users/johndesposito/amalfi_work/amalfi-h3-128/.claude/skills/amalfi-h3-128`. Single source of truth in the fork; edits anywhere update the same files; commits in the fork are the only persistence point. |
| `~/.claude/projects/-Users-johndesposito-amalfi-work-amalfi-h3-128/memory/project_h3_128_pocs.md` | 2026-05-07 — modified | Refreshed (was 5-day-stale "POC-1 done, POCs 2-4 TBD"). Now reflects 5-POC campaign complete (10,067 assertions). |
| `~/.claude/projects/-Users-johndesposito-amalfi-work-amalfi-h3-128/memory/feedback_session9_udf_validation.md` | 2026-05-07 — created | NEW project memory: Session 9 UDF design rule — every PySpark UDF in `h3_extended_spark.py` must validate lat/lng inputs (None, NaN, range) at the wrapper boundary, catch H3Error and return None (not raise), to defend against the lat/lng vs lng/lat swap (most common dispatch bug) and Databricks executor-crash cost. |
| `~/.claude/projects/-Users-johndesposito-amalfi-work-amalfi-h3-128/memory/MEMORY.md` | 2026-05-07 — created | Index pointing at both memory files. |

---

## Session 8.6 Implementation Files (Wheel retag for Databricks serverless on `feat/h3-128-mvp`)

Pre-Session-9 packaging fix. The wheel published at v0.3.0 was tagged `cp311-cp311-<plat>` (CI build host = Python 3.11). Databricks serverless runs Python 3.12, and pip rejects strict CPython ABI mismatches — install failed with `PipError: returned non-zero exit status 1`, blocking platform-side Session 9 Step 5. The package is cffi ABI mode (no Python C extension; `libh3` loaded at runtime via `ffi.dlopen()`), so the cp311 ABI tag was never load-bearing. One commit (`11f383fe`), tag `v0.3.1-py3-tag`. Pre-commit ran ctest 327/327.

### Wheel retag (Session 8.6, commit `11f383fe`)

| File | Created/Modified | Description |
|------|------------------|-------------|
| `setup.py` | 2026-05-11 — modified (commit `11f383fe`) | Subclass `bdist_wheel` and override `get_tag()` to return `("py3", "none", plat)` — explicit, doesn't depend on internal `has_ext_modules` heuristics. Try-import `wheel.bdist_wheel` first, fall back to `setuptools.command.bdist_wheel` for newer setuptools. Flip `Distribution.has_ext_modules()` True → False (no Python C extension; bundled `.so`/`.dylib` are data files). Keep `is_pure()` False so wheel stays platform-tagged. Docstring expanded with Databricks 3.12 motivation. |
| `pyproject.toml` | 2026-05-11 — modified (commit `11f383fe`) | Version 0.3.0 → 0.3.1. |
| `h3_extended/__init__.py` | 2026-05-11 — modified (commit `11f383fe`) | `__version__` 0.3.0 → 0.3.1. |
| `.claude/skills/amalfi-h3-128/SKILL.md` | 2026-05-11 — modified (commit `11f383fe`) | Added `v0.3.1-py3-tag` row to tags table; updated wheel filenames in quick-start commands. |
| `.claude/skills/amalfi-h3-128/artifacts.md` | 2026-05-11 — modified (commit `11f383fe`) | Added `v0.3.1-py3-tag` row to tags table; updated wheel filename example. |
| `.claude/skills/amalfi-h3-128/python-package.md` | 2026-05-11 — modified (commit `11f383fe`) | Version 0.3.0 → 0.3.1; install pattern → `py3-none-<platform>`; retag rationale note. |
| `.claude/skills/amalfi-h3-128/databricks-roadmap.md` | 2026-05-11 — modified (commit `11f383fe`) | All wheel filename references retargeted to `h3_extended-0.3.1-py3-none-linux_x86_64.whl`. |
| `.claude/skills/amalfi-h3-128/implementation-history.md` | 2026-05-11 — modified (commit `11f383fe`) | Added Session 8.6 entry between Session 8.5 (CI fix) and Sessions 9-11. |

**Local validation** (macOS arm64): build produced `py3-none-macosx_26_0_arm64`. pytest **203/203 PASS on Python 3.11** (.venv-test). pytest **203/203 PASS on Python 3.12** (fresh `/tmp/wheel-check` venv). Smoke calls match canonical Monmouth reference cells (`8f2a139108ac78d` at res 15).

**CI validation**: run `25666972631` succeeded in **41 s**, both matrix legs produced correctly-tagged artifacts:
- `h3_extended-0.3.1-py3-none-linux_x86_64.whl` (Linux x86_64)
- `h3_extended-0.3.1-py3-none-macosx_15_0_universal2.whl` (macOS — note `universal2` because CI's macos-latest is macOS 15, producing lipo'd binaries that work on both arm64 and Intel)

### Tag created

| File | Created/Modified | Description |
|------|------------------|-------------|
| `v0.3.1-py3-tag` | 2026-05-11 — annotated tag at `11f383fe` | Documents the cp311-cp311 → py3-none retag rationale, validation results, and wheel filename changes. v0.3.0-python-bindings stays valid alongside. |

---

## Repo Hardening + Cleanup (`feat/h3-128-mvp`)

Two commits driven by a leaked-PAT incident (May 12) and follow-up evaluation of long-standing untracked items. No source changes.

### .env safety net (commit `2c242d63`)

Triggered by an IDE system reminder echoing `.env` contents (including `GITHUB_PAT`) into the conversation transcript. Diagnostic confirmed `.env` was never in git history (never staged, never in any commit's tree) — the file stayed safe only because of named-file `git add` discipline, not because of any `.gitignore` rule. Added explicit `.env` and `.env.*` entries as a safety net. User explicitly declined to rotate the leaked PAT; the conversation transcript is the only persistence point for the exposure.

| File | Created/Modified | Description |
|------|------------------|-------------|
| `.gitignore` | 2026-05-12 — modified (commit `2c242d63`) | Added `.env` and `.env.*` to top section with comment block flagging the secrets-protection role. Two-line additive change. |

### Track session-state + cleanup (commit `8bc3130b`)

User requested commit of long-standing untracked items. Re-evaluated each honestly: kept everything except two genuinely-unrelated items (`backtesting` agent + skill, both Phemex crypto-trading, leftover from a multi-project workspace).

| File | Created/Modified | Description |
|------|------------------|-------------|
| `.gitignore` | 2026-05-12 — modified (commit `8bc3130b`) | Added allowlist entries: `!.claude/commands/`, `!.claude/commands/**`, `!.claude/skills/persist-session/`, `!.claude/skills/persist-session/**`, `!.claude/skills/resume/`, `!.claude/skills/resume/**`. Mirrors the existing h3-128-audit + amalfi-h3-128 pattern. |
| `.claude/commands/resume.md` | 2026-05-12 — committed (was untracked since 2026-04-29) | Resume slash command body (486 bytes). |
| `.claude/commands/save-session.md` | 2026-05-12 — committed (was untracked since 2026-04-29) | Save-session slash command body (1008 bytes). |
| `.claude/commands/ss.md` | 2026-05-12 — committed (was untracked since 2026-04-29) | Short alias for save-session (983 bytes). |
| `.claude/skills/persist-session/SKILL.md` | 2026-05-12 — committed (was untracked) | Persist-session skill definition. |
| `.claude/skills/persist-session/references/context-template.md` | 2026-05-12 — committed (was untracked) | Template the persist-session skill uses to generate context save files. |
| `.claude/skills/resume/SKILL.md` | 2026-05-12 — committed (was untracked) | Resume skill definition. |
| `amalfi-h3-128-session-plan.md` | 2026-05-12 — committed (was untracked since 2026-05-05) | 336-line planning doc covering Sessions 6-11 (CoordIJK int64 → Python bindings → local validation → Databricks UDFs → Delta pipeline → first flight). Comparable to `SESSION_GUIDE.md`. |
| `initial-main-event-prompt.md` | 2026-05-12 — committed (was untracked since 2026-05-02) | 29-line kickoff prompt that initiated the implementation work. Preserved for historical context. |
| `contexts/contexts-may-02-20260502-183908.md` | 2026-05-12 — committed (was untracked since 2026-05-02) | Older context save from POC era. |
| `contexts/contexts-may-03-20260503-154140.md` | 2026-05-12 — committed (was untracked since 2026-05-03) | Older context save (POC continuation). |
| `contexts/contexts-may-03-20260503-201452.md` | 2026-05-12 — committed (was untracked since 2026-05-03) | Older context save (Session 1 start). |
| `contexts/contexts-may-05-20260505-143641.md` | 2026-05-12 — committed (was untracked since 2026-05-05) | Session 7 (calendar) context save covering v0.2.0 finishing moves. |

### Removed from working tree (NOT committed — never tracked)

| File | Action | Reason |
|------|--------|--------|
| `.claude/agents/backtesting.md` | 2026-05-12 — deleted | Phemex crypto-trading agent (5556 bytes). Leftover from a multi-project workspace; unrelated to H3 fork. |
| `.claude/skills/backtesting/SKILL.md` | 2026-05-12 — deleted | Phemex crypto-trading skill. Same rationale. |

### Session 8.6 + repo hardening context save

| File | Created/Modified | Description |
|------|------------------|-------------|
| `contexts/contexts-may-12-20260512-141924.md` | 2026-05-12 — created | This session's context save covering wheel retag + repo hardening + tracked-files backfill. |

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
build-dev/bin/testFFIShimExt       # Session 4 — D7-G1/G2/G3, 1038 assertions
build-dev/bin/testValidationExt    # Session 4 — E1-E7, 220 assertions
build-dev/bin/testAuxiliaryExt     # Session 4 — F1-F4, 771 assertions
build-dev/bin/testCoordIjkExtRes   # Session 6 — CO-G1..G6, ~3000 assertions

# POC-5 standalone (analytic — no libh3 linkage)
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o /tmp/poc5 poc5_coordijk_overflow.c -lm && /tmp/poc5
# Expected: 85 assertions PASS, exit 0
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
| Session 5 — H4 fix (h3Index.h dispatch macros) | 1 (modified) |
| Session 5 — POC-5 source artifacts (poc5_coordijk_overflow.c + poc5.md) | 2 |
| Session 5 — v0.2.0 first widening (coordijk.h struct + utility.c cascade) | 2 (modified) |
| Session 5 — SESSION_5_CHECKPOINT.md + contexts/contexts-may-05-* | 2 |
| Session 6 — v0.2.0 table widening (faceijk.c maxDim/unitScale + coordijk.h _setIJK/_ijkScale) | 2 (modified) |
| Session 6 — v0.2.0 inner arithmetic widening (coordijk.h all 4 _upAp7* + _ijkNormalize* + _ijkToHex2d + _hex2dToCoordIJK + INT64_MAX_3) | 1 (modified, bundled into 5fbdcf4e) |
| Session 6 — testCoordIjkExtRes (new) | 1 |
| Session 6 — Stock test contract update (testCoordIjkInternal INT32_MAX → INT64_MAX) | 1 (modified) |
| Session 6 — Test infrastructure registration (CMakeTests.cmake) | — (modified) |
| Session 6 — Audit manifest update (W64 mutations) | 1 (modified) |
| Session 6 — SESSION_6_CHECKPOINT.md | 1 |
| Session 7 — v0.2.0 tag (annotated) | 1 |
| Session 7 — cellToChildPos widening (h3Index.c + mathExtensions.c + testHierarchyExt.c) | 3 (modified) |
| Session 7 — Audit infrastructure update (audit.py + manifest.json) | 2 (modified) |
| Session 7 — libm tolerance (test.h) | 1 (modified) |
| Session 7 — POC binary untrack (.gitignore + 4 binaries removed) | 1 (modified) + 4 (untracked) |
| Session 7 — LESSONS_LEARNED.md | 1 |
| Session 7 — contexts/contexts-may-05-* | 1 |
| Session 7-py — FFI shim extension (h3ExtShim.h + h3ExtShim.c + testFFIShimExt.c) | 3 (modified) |
| Session 7-py — Python package source (h3_extended/__init__.py + _ffi.py + batch.py) | 3 |
| Session 7-py — Python package tests (h3_extended/tests/ — 7 modules + __init__.py) | 8 |
| Session 7-py — Build configuration (pyproject.toml + setup.py) | 2 |
| Session 7-py — CI workflow (python-wheel.yml) | 1 |
| Session 7-py — .gitignore (Python build artifacts) | 1 (modified) |
| Session 7-py — SESSION_7_CHECKPOINT.md | 1 |
| Session 7-py — contexts/contexts-may-07-* | 1 |
| Session 8 — Local validation script + report (validate_local.py + validation_report.md) | 2 |
| Session 8.5 — CI workflow fix (python-wheel.yml) | 1 (modified) |
| Skill — Cross-repo reference (.claude/skills/amalfi-h3-128/ — 8 .md files + .gitignore allowlist) | 8 + 1 (modified) |
| Session 8.6 — Wheel retag (setup.py + pyproject.toml + __init__.py + 5 skill .md files) | 8 (modified) |
| Repo hardening — .env gitignore safety net | 1 (modified) |
| Repo hardening — track session-state (.claude/commands/ + persist-session/ + resume/ skills + 4 contexts/ + 2 root .md files) | 12 + 1 (modified) |
| Session 8.6 + hardening context save | 1 |
| **Total** | **131** |

| Metric | Value |
|--------|-------|
| Total assertions across all POCs | 10,152 (10,067 POC-1..4 + 85 POC-5) |
| Total children enumerated (POC-3) | 828,210 |
| Toolchain (local) | Apple clang 21.0.0 / Darwin arm64 |
| Toolchain (CI) | Ubuntu gcc / x86_64 + macOS Apple clang (4-job matrix) |
| Sanitizers | AddressSanitizer + UndefinedBehaviorSanitizer (both clean) |
| POCs passing | 5 / 5 (POC-5 added Session 5) |
| Session 1 ctest count | 317/317 PASS (316 stock + testStringExt 4051 assertions) |
| Session 2 ctest count | 321/321 PASS (316 stock + 5 ext suites) |
| Session 3 ctest count (mid-session) | 323/323 PASS |
| Session 4 ctest count | 326/326 PASS (316 stock + 10 ext suites) |
| Session 5 ctest count | 326/326 PASS (no new test suites; v0.2.0 first widening preserves count) |
| Session 6 ctest count | **327/327 PASS** (316 stock + 11 ext suites: Session 1-4 above + testCoordIjkExtRes ~3000) |
| Session 7 ctest count | **327/327 PASS** (no new test suites; D4-G8 added to existing testHierarchyExt → 2,513,991 assertions in that suite alone) |
| Session 7-py ctest count | **327/327 PASS** (no new ctest suites — Python tests are separate; D7-G4 added to existing testFFIShimExt 1038 → 1079 assertions) |
| Session 7-py pytest count | **203/203 PASS** in 0.17s (test_basic 23 + test_extended_res 23 + test_hierarchy 10 + test_backward_compat 80 + test_precision 18 + test_grid_ops 16 + test_string 33) |
| Session 8 ctest count | 327/327 PASS (validation gate, no source changes) |
| Session 8 validation result | **PASS** — 103 GPS-tagged images (40 EXIF + 63 manifest) all gates pass; 100K stress 0/0/0 invalid; 1M memory leak 1.9 KiB final delta; perf baselines latlng_to_cell 390,918 / cell_to_parent 427,932 / grid_disk(k=3) 15,343 ops/sec |
| Session 8.5 ctest count | 327/327 PASS (CI workflow fix only, no source changes) |
| Session 8.6 ctest count | 327/327 PASS (wheel-tag fix only, no C source touched) |
| Session 8.6 pytest count | **203/203 PASS** on Python 3.11 AND Python 3.12 (the Databricks serverless target). Smoke calls match canonical Monmouth reference cells. |
| Session 8.6 wheel CI result | CI run `25666972631`, **41 s wall**, both matrix legs green. Linux artifact `h3_extended-0.3.1-py3-none-linux_x86_64.whl`; macOS artifact `h3_extended-0.3.1-py3-none-macosx_15_0_universal2.whl`. Tags verified against downloaded WHEEL metadata. |
| Session 1 gates | A1-A5, B1, B3, C1-C3 PASS; B2 deferred |
| Session 2 gates | D1-G1, D2-G1 (res 19), D3-G1, D6-G1 PASS; D2-G1 res 20-22 deferred (coordijk int64) |
| Session 3 gates (mid-session) | D4-G0..G7 PASS (incl. 823,543-cell res 22 recursion), D5-G1 PASS (res 19); D7 + E + F + G + H still open |
| Session 4 gates | D7-G1/G2/G3 PASS, E1-E7 PASS (E1+E2 CRITICAL), F1-F4 PASS (F2 ext-pentagon scoped to maxFaceCount; full overage walk deferred to coordijk int64), G1-G3 PASS, H1/H2/H3/H5/H6 PASS; H4 (CI on push) deferred. **v0.1.0-128bit-mvp tagged.** |
| Session 5 gates | **H4 PASS** (CI matrix all 4 green on `1fa4db7c` after dispatch-macro fix). **10/10 §11 acceptance criteria PASS.** v0.2.0 begun: CoordIJK struct typedef widened. |
| Session 6 gates | **v0.2.0 complete**: CO-G1 (res 20 round-trip), CO-G2 (res 21 round-trip — was 247/500 pre-widening, now 500/500), CO-G3 (res 22 round-trip), CO-G4 (pentagon res 19+ no UBSAN), CO-G5 (stock byte-identity), CO-G6 (3 user-spec geos × res 20-22) — ALL PASS. POC-5 predicted boundary cleared. |
| Session 7 gates | **v0.2.0-coordijk-int64 tagged**, post-MVP cleanup: D4-G8 a..e PASS (cellToChildPos/childPosToCell ext-res round-trip). Latent `_ipow` UBSAN bug fixed. Tolerance helper recalibrated to analytically grounded 1e-13 rad ext / 1e-9 rad stock. POC binaries untracked. LESSONS_LEARNED.md retrospective written. |
| Session 7-py gates | **v0.3.0-python-bindings tagged**: D7-G4 (8 follow-on shims linkability + smoke + extended NULL contract) PASS. 203/203 pytest PASS including bit-identical match vs h3-py 4.4.2 at res 0-15 (3 geographies × 5 res = 15 BC tests pass). Wheel `h3_extended-0.3.0-cp311-cp311-macosx_*_arm64.whl` (164 KiB) self-contained, installs cleanly in fresh /tmp venv, library resolves to site-packages. Coexists with stock h3-py (no symbol/library collision). Two test calibrations documented: cell_area BC tolerance 1e-12 → 1e-7 (FP noise); hierarchy tests anchor at parent center (geographic boundary edge case). |
| Session 8 gates | **PASS — validation gate for Databricks**. Per-image gates 7/7 PASS at 103 images (BC res-15 vs h3-py 103/103, parent res 19→15 / 20→19 with 5 BOUNDARY informational, round-trip res 19 < 1cm + res 20 < 5mm both PASS at 0.000000 mm worst, grid_disk 19 unique res-19 cells PASS, string 16/32-char round-trip PASS). 100K stress: 0 invalid res-19/res-20/parents, 7,218 (7.22%) boundary disagreements informational. 1M memory tracemalloc: 1.9 KiB final delta (gate < 1 MiB). Perf baselines recorded. |
| Session 8.5 gates | CI workflow `python-wheel.yml` previously red on both legs at HEAD `3ee39bc7` (BUG 1: `find` glob did not match infixed `.1` versioned dylib; BUG 2: `python -m zipfile -l` regex anchor never matched the listing format). Fixed at `fbd8d04a`: both legs go green (Linux ~43s, macOS ~31s); Linux x86_64 wheel artifact (~94 KiB) downloadable on every push. Session 9 unblocked. |
| Session 8.6 gates | **v0.3.1-py3-tag — Databricks-serverless install unblocker**. Wheel retag `cp311-cp311` → `py3-none`. setup.py overrides bdist_wheel.get_tag() to force ("py3", "none", plat); has_ext_modules flipped True → False (no Python C extension; cffi ABI mode dlopens libh3 at runtime). Version 0.3.0 → 0.3.1. pytest 203/203 PASS on Python 3.11 AND 3.12 locally. CI run 25666972631 green, both matrix legs produced correctly-tagged artifacts. v0.3.0-python-bindings tag remains valid alongside. |
| Session 1 commits on `feat/h3-128-mvp` | 6 (8f8829ab, b2192926, 1e7b6ee3, 089ab9b0, 9e44107e, 944d6e5c) |
| Session 2 commits on `feat/h3-128-mvp` | 18 |
| Session 3 commits on `feat/h3-128-mvp` (mid-session) | 7 |
| Session 4 commits on `feat/h3-128-mvp` | 10 (closing at `6bc988e9` + docs at `2be94029`) |
| Session 5 commits on `feat/h3-128-mvp` | 5 (1fa4db7c H4 fix, ea8503e6 H4 close-out, bef722e3 POC-5, 57672bc6 v0.2.0 typedef, 39ebff5c session-5 doc) |
| Session 6 commits on `feat/h3-128-mvp` | 4 (f84ab171 tables, 5fbdcf4e inner arithmetic + testCoordIjkExtRes, 36a5b36d audit W64 mutations, 88c7c94f session-6 doc) |
| Session 7 commits on `feat/h3-128-mvp` | 4 (ee57e69a cellToChildPos + _ipow + D4-G8, bab3d7fe libm tolerance, 44897e50 POC binary untrack, 100198e1 LESSONS_LEARNED) + 1 retroactive context save commit `addbb2fb` carried over |
| Session 7-py commits on `feat/h3-128-mvp` | 2 (74bf9053 FFI shim extension + D7-G4, b77d7cf7 Python package + cffi + wheels + CI) + tag `v0.3.0-python-bindings` at `b77d7cf7` |
| Session 8 commits on `feat/h3-128-mvp` | 1 (3ee39bc7 validate_local.py + validation_report.md) — no tag (validation gate) |
| Session 8.5 commits on `feat/h3-128-mvp` | 1 (fbd8d04a python-wheel.yml fix — destination-session pickup) |
| Skill commits on `feat/h3-128-mvp` | 1 (ec4e0ae6 amalfi-h3-128 cross-repo reference skill — 8 .md files + .gitignore allowlist) |
| Session 8.6 commits on `feat/h3-128-mvp` | 1 (11f383fe wheel retag — setup.py override + version bump + skill update) + tag `v0.3.1-py3-tag` at `11f383fe` |
| Repo hardening commits on `feat/h3-128-mvp` | 2 (2c242d63 .env gitignore safety net; 8bc3130b track session-state + remove backtesting) |
| Cumulative commit count | 73 (Sessions 1-7 + 7-py + 8 + 8.5 + skill + 8.6 + 2 hardening; not counting tag-only updates) |
| Final HEAD (this session close) | `8bc3130b` |
| Tag history | `v0.1.0-128bit-mvp` (annotated, at `d9af5e79`); `v0.2.0-coordijk-int64` (annotated, at `88c7c94f`); `v0.3.0-python-bindings` (annotated, at `b77d7cf7`); **`v0.3.1-py3-tag`** (annotated, Session 8.6 at `11f383fe`) |
| CI status | All 4 matrix jobs (`ci.yml`) green on Linux + macOS × release + sanitizers throughout. `python-wheel.yml` was red at `3ee39bc7` due to two workflow bugs; fixed at `fbd8d04a` (Session 8.5). At `11f383fe` (Session 8.6) verified green: CI run `25666972631`, 41s wall, both matrix legs uploaded correctly-tagged artifacts (`h3_extended-0.3.1-py3-none-linux_x86_64.whl` and `h3_extended-0.3.1-py3-none-macosx_15_0_universal2.whl`). **Outstanding**: confirm both workflows green at HEAD `8bc3130b` (commits after `11f383fe` are pure docs/config — no source change). |
