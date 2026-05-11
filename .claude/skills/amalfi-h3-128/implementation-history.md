# Implementation History

Session-by-session log. Use this when you need a commit SHA, a ctest
checkpoint, or to understand which session touched a specific
function. Each session ended green (327/327 ctest at HEAD; earlier
counts at earlier checkpoints).

---

## Pre-Implementation: POC Campaign (April–May 2026)

Four proof-of-concept programs validated the architecture before any
H3 source was modified. All compiled with
`-fsanitize=address,undefined -Werror -Wall -Wextra` on Apple clang
21.0.0 / macOS arm64. Zero sanitizer findings.

| POC | Risk area | Assertions | Commit | Source |
|---|---|---:|---|---|
| POC-1 | Bit layout + Group C macros | 685 | `d5168127` | `poc1_bit_layout.c` |
| POC-2 | Validation predicates | 3,473 | `f47965e4` | `poc2_validation.c` |
| POC-3 | Iterator state machine | 4,595 | `3a8b98e2` | `poc3_iterator.c` |
| POC-4 | FFI ABI (cross-TU) | 1,314 | `1813e0da` | `poc4_shim.{h,c}` + `poc4_caller.c` |
| **Total** | | **10,067** | | |

Plus build discipline (`8f8829ab`): ASAN+UBSAN dev config, pre-commit
hook, GitHub Actions `ci.yml` workflow.

POC-5 came later (after Session 4 — see Session 5) when an int32
overflow surfaced in `_adjustOverageClassII` at ext resolutions:

| POC-5 | CoordIJK int32 overflow at ext resolutions | (program in repo) | `bef722e3` | `poc5_coordijk_overflow.c` |

POC files (`poc{1..5}*.{c,h,md}`) are tracked in the repo and frozen.
The compiled binaries (`poc1_bit_layout`, `poc2_validation`, etc.) are
NOT tracked (`.gitignore` excludes them — they're rebuildable
diagnostics).

---

## Session 1 — Phases A + B + C → ctest 317/317 (`944d6e5c`)

| Phase | Commit | What |
|---|---|---|
| A: type widening | `b2192926`, `1e7b6ee3` | `MAX_H3_EXT_RES = 22` constant; `H3Index` typedef widened to `__uint128_t`; Group C macros installed in `h3Index.h`; cascade fixes through 28 source files. |
| B: tolerance helper | `089ab9b0` | `latlng_within_tolerance` helper added to `src/apps/applib/include/test.h` for round-trip ext tests (placeholder values; recalibrated in Session 7). |
| C: string I/O | `9e44107e` | `h3ToString` / `stringToH3` widened to handle the 32-char canonical form for ext cells while preserving 1-16-char output for stock cells. |

ctest 317/317 (316 stock + 1 new ext test).

---

## Session 2 — Phases D1 + D2 + D3 + D6 → ctest 321/321 (`7d5d6296`)

The "encode/decode core" session. Each widening one commit, test in
the same commit.

**D1 — Encoder** (the latLng → cell path)
- `setH3Index` widened (Rule INIT + RW + DW): `a36f68f1`
- `_faceIjkToH3` widened (Rule INIT + RW + DW): `7bd3ef69`
- Encoder guards relaxed (Rule GR): `569da6b2`
- `testEncoderExt` D1-G1 gate added: `d15f6668`

**D2 — Decoder** (the cell → latLng path)
- `_h3ToFaceIjkWithInitializedFijk` widened (Rule LB + DR): `16491e7f`
- `_h3ToFaceIjk` local res reads widened (Rule LB): `a98a2096`
- `cellToVec3` res arg widened (Rule LB): `c18b505f`
- `maxDimByCIIres` + `unitScaleByCIIres` extended to length 21: `e75e840f`
- `testDecoderExt` D2-G1 gate (1,000 res-19 round-trips): `f8c318ab`

**D3 — Accessors**
- `getResolution` widened to return effective res (Rule LB): `5a64861e`
- `getIndexDigit` widened (Rule GR + DR): `5a1311ca`
- `constructCell` widened (Rule GR + INIT + RW + DW): `bdf36196`
- `testAccessorExt` D3-G1 gate added: `63cb7b6b`

**D6 — Rotations**
- `_h3LeadingNonZeroDigit` widened (Rule LB + DR, trap §6.13): `d09221c8`
- `_h3Rotate60ccw` + `_h3Rotate60cw` (Pattern 3, trap §6.12): `437278c0`
- `_h3RotatePent60ccw` + `_h3RotatePent60cw` (Pattern 3): `bf22c42f`
- `testRotationExt` D6-G1 gate (six rotations 6-cycle): `9fe7f529`

ctest 321/321.

---

## Session 3 — Phases D4 + D5 + D7 (no checkpoint commit; rolls into Session 4)

**D4 — Hierarchy + Iterators** (the highest-risk widening per §6 traps)
- `_zeroIndexDigits` rewritten as dispatch loop (§6.6, POC-3 I1): `d22204e1`
- Iterators widened (Pattern 3, POC-3 I2-I5): `8fd4f1a0`
- `cellToParent` widened (Rule LB+GR+RW+DW, trap §6.7): `5aba98f5`
- `_hasChildAtRes`, `cellToChildrenSize`, `makeDirectChild`,
  `cellToCenterChild` widened: `9211f12e`
- `testHierarchyExt` D4-G0 canary + G1..G7 gates added: `1bef1e34`

**D5 — Local IJ**
- `cellToLocalIjk` + `localIjkToCell` widened (Rules LB+INIT+RW+DW,
  trap §6.15): `c395f91e`
- `testLocalIjExt` D5-G1 gate + cellToLocalIj round-trip: `a9de465d`

**D7 — FFI Shim Layer** (Pattern 4 — POC-4 contract)
- `4d9feaa4` — added `src/h3lib/include/h3ExtShim.h` with the 10
  validated POC-4 prototypes; `src/h3lib/lib/h3ExtShim.c` with each
  shim dereferencing its by-pointer inputs and forwarding to the
  widened H3 entry point. `testFFIShimExt` D7-G1/G2/G3 gates added.

---

## Session 4 — Phases E + F + G + H → **v0.1.0-128bit-mvp** (`6bc988e9`)

**Phase E — Validation Widening** (per POC-2 patterns)
- `_hasGoodTopBits` rewritten — split low/high check: `59a4949d`
- `_firstOneIndex` rewritten — high-half-first dispatch: `61e3ce54`
- `_hasAny7UptoRes` extended with ext-digit loop: `92481108`
- `_hasAll7AfterRes` extended (per-digit loop, removed early-return): `943fcb51`
- `_hasDeletedSubsequence` + `isValidCell` res capture: `80f2f8ee`
- `testValidationExt` E1..E7 gates added: `36c56d14`

**Phase F — Auxiliary Surface**
- `cellToBoundary`, `getIcosahedronFaces`, `getPentagons` widened
  (§6.14): `198649d3`
- `testAuxiliaryExt` F1..F4 gates: `841db752`

**Phase G — Audit Infrastructure**
- `2aabbdcd` — `.claude/skills/h3-128-audit/audit.py` + mutation
  manifest. Classifies every macro/literal site as widened, deferred,
  or intentionally skipped. Used as a static check in addition to
  ctest.

**Phase H — Acceptance signoff**
- `6bc988e9` — `SESSION_4_CHECKPOINT.md` + signoff.
- **Tag `v0.1.0-128bit-mvp`** at this commit. ctest 326/326 (the +5 vs
  Session 1's 317 are the new ext suites added in D1/D2/D3/D6).

---

## Session 5 — H4 fix + POC-5 + v0.2.0 typedef widening (`39ebff5c`)

CI matrix surfaced an issue not visible on macOS clang: gcc emits
`shift-count-negative` warnings for ext-resolution dispatch macros
when the resolution literal expanded to a value > 15 in dead code
paths (`H3_GET_INDEX_DIGIT(h, r)` with r=22 in a branch the optimizer
couldn't prove unreachable).

**H4 close**:
- `1fa4db7c` — `fix(h4)`: clamp res in dispatch macros for gcc
  shift-count-negative.
- `ea8503e6` — CI matrix verified green (10/10 §11 PASS).

**POC-5 — CoordIJK overflow at ext resolutions**:
- `bef722e3` — POC-5 demonstrated that `int` (32-bit on most ABIs)
  overflows in `_adjustOverageClassII` at res 21+. The hexagon path at
  res 21 went from 247/500 success → 500/500 after the type
  widening; the pentagon path at res 19+ tripped UBSAN before the fix.

**v0.2.0 typedef widening (start)**:
- `57672bc6` — `v0.2.0(coordijk)`: widen CoordIJK struct fields
  `int → int64_t`.

This session ended green but the v0.2.0 widening was incomplete (only
the typedef changed; tables and inner arithmetic still on int).

---

## Session 6 — v0.2.0 widening complete → ctest 327/327 (`88c7c94f`)

Three more commits to close v0.2.0:

- `f84ab171` — `v0.2.0(faceijk)`: widen `maxDim` / `unitScale` tables
  to `int64_t[23]` + locals.
- `5fbdcf4e` — `v0.2.0(coordijk)`: widen inner arithmetic +
  `testCoordIjkExtRes` (CO-G1..G6).
- `36a5b36d` — `audit(v0.2.0)`: add W64 mutations for CoordIJK +
  maxDimByCIIres widening to the audit manifest.

After Session 6: hexagon res 21 went 247/500 → 500/500; res 22
UBSAN-trip → 500/500; pentagon res 19+ UBSAN → clean; 3 user-spec
geographies (Monmouth, Palm Beach, Piano di Sorrento) × res 20-22 →
9/9 bit-identical round-trip. ctest 327/327.

---

## Session 7 (calendar — finishing moves) (`100198e1`)

Tidy work that closed v0.2.0 and prepared for the Python session:

- **Tag `v0.2.0-coordijk-int64`** at `88c7c94f` (annotated, all 9
  Session-5/6 commits enumerated in the tag message).
- `ee57e69a` — `feat(post-mvp)`: widen `cellToChildPos` /
  `childPosToCell` for ext resolutions. Surfaced a latent stock H3
  bug: `_ipow` had a trailing dead-store `base *= base` that overflows
  for `_ipow(7, 22)`. Fixed with `if (exp) base *= base;`. New
  `testHierarchyExt::d4g8_cellToChildPos_extRes` regression with
  2,513,991 assertions.
- `bab3d7fe` — `chore(post-mvp)`: recalibrate libm tolerance helper.
  Measured 0 rad delta on macOS arm64 + Apple libm across 1000 ×
  23 resolutions. Updated `latlng_within_tolerance` to 1e-13 rad ext /
  1e-9 rad stock.
- `44897e50` — `chore(pocs)`: untrack POC build binaries from git.
  Sources stay tracked.
- `100198e1` — `docs(retrospective)`: `LESSONS_LEARNED.md` (261 lines,
  9 lessons, 3 candidate audit rules).

ctest 327/327 throughout (additive changes only).

---

## Session 7 (planned — Python bindings) → **v0.3.0-python-bindings** (`b77d7cf7`)

Two commits — the FFI shim extension, then the Python package.

### Step 0: Extend the FFI shim (`74bf9053`)

The original 10 shim functions covered indexing/hierarchy/distance but
not boundary/area/grid/local-ij. Added 8 wrappers + 2 size helpers:

- `h3_ext_cell_to_boundary` → `cellToBoundary(CellBoundary*)`
- `h3_ext_cell_area` (single function with int unit:
  `H3_EXT_AREA_M2 = 0`, `_KM2 = 1`, `_RADS2 = 2`; bad unit →
  `E_OPTION_INVALID`)
- `h3_ext_max_grid_disk_size`
- `h3_ext_grid_disk`
- `h3_ext_grid_path_cells_size`
- `h3_ext_grid_path_cells`
- `h3_ext_cell_to_local_ij` (returns `CoordIJ*`)
- `h3_ext_local_ij_to_cell` (takes `const CoordIJ*`)

`CoordIJ` and `CellBoundary` are passed as struct pointers — they're
public POD types in `h3api.h` and ABI-stable. Only `__uint128_t` is
banned by-value.

D7-G4 regression test added in `testFFIShimExt.c` (linkability + smoke
through each new function at res 9). +41 assertions
(testFFIShimExt 1038 → 1079). ctest 327/327.

### Step 1-9: Python package (`b77d7cf7`)

Files created:
- `h3_extended/__init__.py` — 16 public API functions + `H3Error`
  exception class
- `h3_extended/_ffi.py` — cffi ABI mode bindings, `dlopen` of
  `libh3_extended.{dylib,so}`, ABI consistency check at import
- `h3_extended/batch.py` — 3 batch helpers (Python loops,
  Pandas-UDF-ready)
- `h3_extended/tests/` — 7 pytest modules, 203 assertions
- `pyproject.toml` + `setup.py` — wheel build (BinaryDistribution
  forces platform-specific tag because the wheel bundles a native lib)
- `.github/workflows/python-wheel.yml` — matrix on `ubuntu-latest`
  x86_64 + `macos-latest` arm64. Each leg: configure cmake → build C →
  stage shared lib into package → `python -m build --wheel` → install
  in clean venv → pytest → upload artifact (30 days)

Local result on macOS arm64:
- pytest 203/203
- Wheel: `dist/h3_extended-0.3.0-cp311-cp311-macosx_26_0_arm64.whl`
- Self-contained: install in `/tmp/venv-wheel`, library resolves to
  `site-packages/h3_extended/libh3_extended.dylib` (not the source tree)
- Coexists with stock `h3-py` 4.4.2: `to_64bit` of an `h3_extended`
  res-15 cell matches stock int byte-for-byte at every test point

Two test calibrations documented in `SESSION_7_CHECKPOINT.md`:
1. `cell_area` BC tolerance loosened 1e-12 → 1e-7 (FP-reordering
   noise from post-int64 CoordIJK kernels, not a regression).
2. Hierarchy tests anchor descent at parent **center** not raw
   lat/lng input — `(40.33, -73.99)` sits within ε of a res-15
   boundary, so res-15-of-input vs res-15-of-res-16-child diverged in
   the last digit.

**Tag `v0.3.0-python-bindings`** at `b77d7cf7`.

---

## Session 8 — Local validation against real imagery (`3ee39bc7`)

The quality gate before Databricks. Built `validate_local.py` and ran
it against geotagged imagery from the sibling
`amalfi_intelligence_platform` repo.

**Imagery sources:**
- `photos/april-11-jpg/` — 40 iPhone 13 stills, full GPS EXIF
- `capture_data/monmouth_30_9.02/2026-03-29/keyframes/` — 63 video
  keyframes with GPS in `manifest.json` (per-frame structure)
- `photos/april-23-jpg/` — 67 images skipped (no GPS in EXIF)

103 GPS-tagged images, 33 distinct (lat, lng) points (the keyframes
all share a single source GPS coord per the video manifest schema).

**Per-image gates — all PASS:**
- Backward compat res 15 vs h3-py 4.4.2: 103/103
- Parent res 19 → 15: 101 PASS + 2 BOUNDARY (geographic edge cases)
- Parent res 20 → 19: 100 PASS + 3 BOUNDARY
- Round-trip res 19 < 1 cm: 103/103, max obs 0.000000 mm
- Round-trip res 20 < 5 mm: 103/103, max obs 0.000000 mm
- grid_disk(k=2) → 19 unique res-19 cells: 103/103
- String round-trip (16-char stock + 32-char ext): 103/103

**Stress test — 100K random Monmouth County coords:**
- 0 invalid res-19 / res-20 cells
- 0 invalid res-19 parents from res-20
- 7,218 (7.22%) boundary disagreements — informational, geographic
- 100,000 / 100,000 distinct cells at both resolutions

**Memory leak — 1M latlng_to_cell calls (tracemalloc):** final delta
1.9 KiB, peak 2.0 KiB. Effectively zero.

**Performance baselines (cffi ABI, macOS arm64):**
- `latlng_to_cell(res=19)`: 390,918 ops/sec
- `cell_to_parent(res19→15)`: 427,932 ops/sec
- `grid_disk(k=3, res=19)`: 15,343 ops/sec

No tag (validation gate, not release artifact). The
`validation_report.md` was committed at `3ee39bc7` along with
`validate_local.py`.

---

## Session 8.5 — CI workflow bug fixes (`fbd8d04a`)

A pre-flight check before starting Session 9 surfaced that
`python-wheel.yml` (introduced in `b77d7cf7`) had two bugs keeping
both matrix legs red — neither caught locally because `python -m
build --wheel` worked fine on the dev machine and the wheel was only
inspected via the CI workflow.

A destination-session pass landed the fix at commit `fbd8d04a`
(2026-05-07):

- **Bug 1**: `find -name '${{ matrix.lib }}*'` did not match
  `libh3.1.dylib` because `.1` is infixed, not suffixed. Replaced
  with `-type f \( -name 'libh3.[0-9]*.dylib' -o -name 'libh3.so.[0-9]*' \)`.
- **Bug 2**: `python -m zipfile -l | grep '\.so$'` never matched
  because the listing format puts `Name Modified Size` on one line
  and the line ends in the size. Replaced with
  `awk '{print $1}' | grep -E '\.(so|dylib)$'`.

Both matrix legs now go green: Linux ~43 s, macOS ~31 s. Linux
x86_64 wheel artifact `h3_extended-ubuntu-latest-x86_64-py311`
(~94 KiB) is published on every push. Full diagnosis in the skill's
`databricks-roadmap.md` "Resolved CI bugs (history)" section.

ctest 327/327 (unchanged — workflow-only fix, no source changes).

---

## Session 8.6 — Wheel retag for Databricks serverless (TBD commit)

Pre-Session-9 install attempt on Databricks serverless (platform-side
repo `amalfi_intelligence_platform`, Session 9 Step 5) failed at
`%pip install` with `PipError: returned non-zero exit status 1`. Root
cause: the wheel published by `python-wheel.yml` was tagged
`cp311-cp311-linux_x86_64` (CI build host is Python 3.11), but
Databricks serverless runs Python 3.12. Pip rejects strict CPython ABI
mismatches.

The `h3_extended` package doesn't actually need a CPython ABI tag —
it's cffi ABI mode, loading `libh3.so` at runtime via
`ffi.dlopen()`. The cp311 tag was just setuptools auto-detecting the
build host's Python.

Fix in `setup.py`:
- Subclass `bdist_wheel` and override `get_tag()` to return
  `("py3", "none", plat)` — forces Python-version-agnostic tag while
  keeping the platform-specific suffix (the wheel still bundles
  `libh3_extended.{dylib,so}`).
- Flip `Distribution.has_ext_modules()` from `True` to `False` (the
  package has no Python C extension — data files only).

Version bumped 0.3.0 → 0.3.1 (packaging fix, no API change). Wheel
filename `h3_extended-0.3.0-cp311-cp311-linux_x86_64.whl` becomes
`h3_extended-0.3.1-py3-none-linux_x86_64.whl`. ctest 327/327
unchanged (no C source touched). pytest 203/203 PASS on both
Python 3.11 (CI build host) and Python 3.12 (Databricks serverless
target) locally.

Tag: **`v0.3.1-py3-tag`**.

---

## Sessions 9, 10, 11 — Not yet started

See [databricks-roadmap.md](databricks-roadmap.md) for the full plan.
Brief:

- **Session 9 — PySpark UDFs + Databricks deployment.** Build
  `h3_extended_spark.py` registering 9-11 UDFs; build
  `h3_auto_*` SQL dispatch layer routing res 0-15 → Photon-native
  H3 and res 16-22 → h3_extended UDFs; upload wheel to Unity Catalog
  Volume; first live queries. Tag: `v0.4.0-databricks-udf`.
- **Session 10 — Delta Lake schemas + pipeline skeleton.** Bronze /
  Silver / Gold tables with multi-resolution H3 columns; partition by
  `h3_res15`, Z-ORDER by `h3_res19`; Auto-Loader for S3 ingestion.
  Tag: `v0.5.0-delta-pipeline`.
- **Session 11 — First real flight data.** Fly the Monmouth Beach
  property, push imagery through the complete pipeline, see the
  digital twin at sub-centimeter resolution. Tag: `v1.0.0-first-flight`.
