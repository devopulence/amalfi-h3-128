# C Library

## Repository

```
remote:    git@github.com:devopulence/amalfi-h3-128.git
local:     /Users/johndesposito/amalfi_work/amalfi-h3-128/
upstream:  https://github.com/uber/h3.git (v4.4.1, commit 69e01f3c)
branch:    feat/h3-128-mvp
```

## Build modes

Two cmake build directories are used:

### `build-dev/` — sanitizers + tests (development)

```bash
cd /Users/johndesposito/amalfi_work/amalfi-h3-128
mkdir -p build-dev && cd build-dev
cmake .. -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Werror -Wall -Wextra" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cd ..
cmake --build build-dev -j$(sysctl -n hw.ncpu)
(cd build-dev && ctest --output-on-failure -j4)
# Expected: 327/327 PASS
```

This is what the pre-commit hook uses, what every commit runs against,
and what every test/audit measurement is taken from.

### `build-release/` — shared library for the Python wheel

```bash
mkdir -p build-release && cd build-release
cmake .. -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
         -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_SHARED_LIBS=ON \
         -DBUILD_TESTING=OFF \
         -DBUILD_BENCHMARKS=OFF \
         -DBUILD_FILTERS=OFF \
         -DBUILD_GENERATORS=OFF
cmake --build . -j --target h3
# Produces lib/libh3.dylib (macOS) or lib/libh3.so (Linux)
```

This is what the Python wheel bundles (renamed to
`libh3_extended.dylib`/`.so` to avoid h3-py collision).

## ctest baseline progression

| After session | ctest | Net delta | What was added |
|---|---:|---:|---|
| Stock baseline (commit `69e01f3c`) | 316/316 | — | Stock H3 v4.4.1 — never modified |
| Session 1 (A/B/C) | 317/317 | +1 | testStringExt |
| Session 2 (D1/D2/D3/D6) | 321/321 | +4 | testEncoderExt, testDecoderExt, testAccessorExt, testRotationExt |
| Session 3 (D4/D5/D7) — partial | 325/325 | +4 | testHierarchyExt, testLocalIjExt, testFFIShimExt, plus retired/folded test |
| Session 4 (E/F/G/H) → v0.1.0 | 326/326 | +1 | testValidationExt + testAuxiliaryExt + audit infrastructure |
| Session 5 (H4 + POC-5 + v0.2.0 typedef) | 326/326 | 0 | No new test (POC-5 is standalone) |
| Session 6 (v0.2.0 widening complete) | 327/327 | +1 | testCoordIjkExtRes (CO-G1..G6) |
| Session 7-cal (cellToChildPos + libm tolerance) | 327/327 | 0 | D4-G8 added to testHierarchyExt; not a new suite |
| Session 7-py (FFI shim extension + Python) | 327/327 | 0 | D7-G4 added to testFFIShimExt; Python tests don't count toward ctest |
| Session 8 (validation gate, no source change) | 327/327 | 0 | — |

**The 316/316 stock baseline is byte-identical to unmodified upstream
H3.** Every fork commit verified this. Any red stock test = revert.

## Stock test contract

The 316 stock tests must pass byte-identically. Two test files have
their **assertion values widened** (not their behavior):

- `src/apps/testapps/testCoordIjkInternal.c`: `INT32_MAX` → `INT64_MAX`
  in the overage-recovery exhaustion path (Session 6, commit
  `5fbdcf4e`). The test is checking that the overage adjustment can
  consume the full integer range — extending the range to int64
  doesn't change pass/fail, just widens the tested domain.

That's the only stock test whose source changed. The other 315 stock
tests are unmodified.

## Ext test suites (additive — under `src/apps/testapps/test*Ext*.c`)

| Suite | Session | Assertions (current) | What it covers |
|---|---|---:|---|
| `testStringExt` | 1 | 4,051 | C2/C3 hex round-trip for ext cells |
| `testEncoderExt` | 2 | 8,305 | latLngToCell at res 16-22, byte identity at res 0-15 |
| `testDecoderExt` | 2 | 5,401 | cellToLatLng round-trip + decoder regressions |
| `testAccessorExt` | 2 | 559 | getResolution, getIndexDigit, constructCell |
| `testRotationExt` | 2 | 302 | Six rotations 6-cycle property |
| `testHierarchyExt` | 3, 7 | 2,513,991 | D4-G0 stock canary, G1..G7 ext gates, G8 cellToChildPos |
| `testLocalIjExt` | 3 | 1,707 | cellToLocalIj round-trip at ext res |
| `testFFIShimExt` | 3, 7 | 1,079 | D7-G1/G2/G3/G4 — all 18 shims + NULL contract |
| `testValidationExt` | 4 | 220 | E1..E7 — _hasGoodTopBits, _firstOneIndex, etc. |
| `testAuxiliaryExt` | 4 | 771 | F1..F4 — boundary, faces, pentagons |
| `testCoordIjkExtRes` | 6 | ~3,000 | CO-G1..G6 — int64 overflow gates |

Total ext assertions ≈ 2,540,000 (dominated by testHierarchyExt's
res-22 enumeration).

## Audit infrastructure

`.claude/skills/h3-128-audit/audit.py` is a static check that lives
alongside ctest. It enumerates every macro/literal site in the H3
source that should have one of the five widening rules applied (LB,
GR, DW, DR, RW, INIT) and classifies each as:

- `widened` — fixed in the fork
- `deferred` — not needed for MVP, will widen later (e.g. polyfill,
  edges, vertices, compactCells)
- `intentionally_skipped` — POC-validated to be unsafe to touch (e.g.
  POC files themselves, stock test assertions)

Plus a `mutations/manifest.json` that records "if you reverted this
widening, this test would fail" for each rule. Used for negative
testing — before declaring a widening complete, verify the mutation
test would have caught its absence.

Current audit state (HEAD `3ee39bc7`):
- **23 sites classified**
- 13 widened
- 10 deferred (with reasons)
- **0 CRITICAL, 0 FINDING**

Run with `python3 .claude/skills/h3-128-audit/audit.py`.

## FFI shim surface — 18 functions + 2 size probes

Located in `src/h3lib/include/h3ExtShim.h` and
`src/h3lib/lib/h3ExtShim.c`. Pattern 4 contract: `H3Index` always
by-pointer, all return `H3Error`, NULL → `E_FAILED`, `h3_ext_` prefix.

### Original 10 (Session 3, POC-4 contract)

```c
H3Error h3_ext_lat_lng_to_cell(double lat, double lng, int res, H3Index *out);
H3Error h3_ext_cell_to_lat_lng(const H3Index *cell, double *lat, double *lng);
H3Error h3_ext_cell_to_parent(const H3Index *cell, int parentRes, H3Index *out);
H3Error h3_ext_cell_to_children(const H3Index *cell, int childRes,
                                H3Index *children, int64_t *count);
H3Error h3_ext_cell_to_children_size(const H3Index *cell, int childRes,
                                     int64_t *out);
H3Error h3_ext_is_valid_cell(const H3Index *cell, int *out);
H3Error h3_ext_get_resolution(const H3Index *cell, int *out);
H3Error h3_ext_grid_distance(const H3Index *a, const H3Index *b, int64_t *out);
H3Error h3_ext_h3_to_string(const H3Index *cell, char *out, size_t sz);
H3Error h3_ext_string_to_h3(const char *str, H3Index *out);
```

### Session-7 additions (8 wrappers)

```c
/* Area unit codes for h3_ext_cell_area. */
#define H3_EXT_AREA_M2    0
#define H3_EXT_AREA_KM2   1
#define H3_EXT_AREA_RADS2 2

H3Error h3_ext_cell_to_boundary(const H3Index *cell, CellBoundary *out);
H3Error h3_ext_cell_area(const H3Index *cell, int unit, double *out);
H3Error h3_ext_max_grid_disk_size(int k, int64_t *out);
H3Error h3_ext_grid_disk(const H3Index *origin, int k, H3Index *out);
H3Error h3_ext_grid_path_cells_size(const H3Index *start, const H3Index *end,
                                    int64_t *out);
H3Error h3_ext_grid_path_cells(const H3Index *start, const H3Index *end,
                               H3Index *out);
H3Error h3_ext_cell_to_local_ij(const H3Index *origin, const H3Index *cell,
                                uint32_t mode, CoordIJ *out);
H3Error h3_ext_local_ij_to_cell(const H3Index *origin, const CoordIJ *ij,
                                uint32_t mode, H3Index *out);
```

### Cross-TU consistency probes

```c
size_t h3_ext_sizeof_h3index(void);   /* must return 16 */
size_t h3_ext_alignof_h3index(void);  /* must return 16 */
```

These are called at Python import time to fail-fast if the wrong
(stock 64-bit) libh3 is somehow loaded.

## Pre-commit hook

Installed at `.git/hooks/pre-commit`:

```bash
#!/bin/bash
set -e
cmake --build build-dev -j --quiet
(cd build-dev && ctest --output-on-failure --quiet)
echo "[pre-commit] All green."
```

Each commit takes ~9-10 minutes (full ctest run). **Never use
`--no-verify`.** If a commit needs to land without ctest passing, the
correct response is "fix the test or revert the edit," not "skip the
hook."

## CI (GitHub Actions)

Two workflows:

- `.github/workflows/ci.yml` — original C library CI matrix:
  `ubuntu-latest` + `macos-latest` × `release` + `sanitizers` config.
  Builds, tests, runs from upstream H3's CI heritage.
- `.github/workflows/python-wheel.yml` — added Session 7-py. Matrix
  on `ubuntu-latest` x86_64 + `macos-latest` arm64; each leg builds C
  → stages shared lib into Python package → `python -m build --wheel`
  → installs in clean venv → runs pytest from `/tmp` (avoid source
  shadowing) → uploads wheel as 30-day artifact.

CI green at HEAD `3ee39bc7` is the entry-point gate for any consumer.
Verify with:

```bash
set -a && source .env && set +a
GH_TOKEN="$GITHUB_PAT" gh run list -R devopulence/amalfi-h3-128 \
    --branch feat/h3-128-mvp --limit 5
```

## Re-running POCs as diagnostics

If a regression looks architectural, re-run the relevant POC (each
< 2 seconds):

```bash
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc1 poc1_bit_layout.c -lm && ./poc1
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc2 poc2_validation.c -lm && ./poc2
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc3 poc3_iterator.c -lm && ./poc3
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -c poc4_shim.c -o poc4_shim.o
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -c poc4_caller.c -o poc4_caller.o
gcc -fsanitize=address,undefined -o poc4 poc4_shim.o poc4_caller.o -lm \
    && ./poc4
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc5 poc5_coordijk_overflow.c -lm && ./poc5
```

The POC sources are in the repo root (`poc1_bit_layout.c`, etc.). The
binaries are gitignored.

## C-side deferrals (post-MVP)

These functions are NOT yet widened. They work for stock res 0-15 but
will return errors or produce wrong cells if called with ext-res
inputs. The Python package does NOT expose them yet.

| Function family | Scope estimate | Notes |
|---|---|---|
| Edges (`cellsToDirectedEdge`, `directedEdgeToBoundary`, etc.) | 1-2 commits | Lean on already-widened faceijk |
| Vertices (`cellToVertex`, `vertexToLatLng`, etc.) | 1-2 commits | Same profile as edges |
| `compactCells` / `uncompactCells` | 2-3 commits + new testCompactExt | Digit-equality compaction needs ext-aware comparison |
| `polyfill` / `polygonToCells` | 4-5 commits | Blocked on computing `MAX_EDGE_LENGTH_RADS[16..22]` and pole-cells lookup tables (playbook §5.6) |
| Auxiliary stat helpers (`getHexagonAreaAvg*`, `getNumCells`) | Few-line widening | Once table values are derived |
| B2 MSVC `#error` path | Cannot verify on macOS arm64 | Park indefinitely |
