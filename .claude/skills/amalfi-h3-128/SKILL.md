---
name: amalfi-h3-128
description: "Reference for the H3-Extended (128-bit) fork at devopulence/amalfi-h3-128 — Uber H3 widened from 64-bit to 128-bit indexes to unlock spatial resolutions 16-22 (~1cm). Use this skill when working on Databricks integration of the h3_extended Python package, building PySpark UDFs that need ext-resolution indexing, deploying Delta Lake schemas with multi-resolution H3 columns, debugging cross-platform parity issues between macOS arm64 and Linux x86_64 builds, or when a session in any sibling repo needs to understand what artifacts the fork produces and how to consume them. Covers the C library state, the Python package contract, validation evidence, available wheels and tags, and the Sessions 9-11 Databricks roadmap."
---

# Amalfi H3-Extended (128-bit) — Cross-Repo Reference Skill

This skill is documentation, not action. It tells a session in another
repository **what the fork at `devopulence/amalfi-h3-128` provides, how
it was built and validated, and what is left to do** (Databricks
deployment in Sessions 9-11). Read whichever detail file matches your
need — they are independent.

---

## Executive Summary (as of 2026-05-05, commit `fbd8d04a`)

The H3-Extended fork widens Uber H3 from `uint64_t` to `__uint128_t`
indexes, unlocking resolutions 16-22 (~1cm-precision hexagons) for the
Amalfi Intelligence spatial digital twin platform. The architecture
was validated by **four POCs (10,067 assertions, ASAN+UBSAN clean)**
before any H3 source was touched, then implemented across 8 sessions.

**Status checkpoints:**

| Tag | Date | Meaning |
|---|---|---|
| `v0.1.0-128bit-mvp` | 2026-05-04 | C library complete: encode/decode/hierarchy/iterators/validation/auxiliary widened. ctest 326/326 |
| `v0.2.0-coordijk-int64` | 2026-05-05 | CoordIJK int32 → int64 widened (unblocks res 20-22 sub-meter precision). ctest 327/327 |
| `v0.3.0-python-bindings` | 2026-05-05 | Python `h3_extended` package via cffi ABI mode, wheel build, CI matrix. pytest 203/203 |

Plus Session 8 (no tag, validation gate): real-imagery PASS — 103
geotagged images run through the full res-15/19/20 suite. Round-trip
precision **0.000000 mm at every image** on macOS arm64.

**Three artifacts a Databricks session needs:**

1. **Linux x86_64 wheel** — built by `.github/workflows/python-wheel.yml`
   on every push. Download from GitHub Actions artifacts (named
   `h3_extended-ubuntu-latest-x86_64-py311`).
2. **`h3_extended` Python API** — 16 public functions, 3 batch
   helpers, all at `import h3_extended` after `pip install` of the
   wheel. Coexists with stock `h3-py` v4.x.
3. **`to_64bit` / `from_64bit` interop** — convert between 128-bit
   ext-res cells and stock 64-bit ints for any res 0-15 cell, allowing
   Photon-accelerated stock H3 to handle low-res operations and
   `h3_extended` to handle res 16-22.

**What's done as of `fbd8d04a` (2026-05-07):**

- ✅ C library widening + tests (327/327 ctest, sanitizers clean)
- ✅ Python `h3_extended` package + cffi bindings + 203/203 pytest
- ✅ Local validation against real imagery (Session 8 — PASS)
- ✅ CI green on both Linux x86_64 and macOS arm64 matrix legs
- ✅ Linux x86_64 wheel artifact published and downloadable

**Two things NOT yet done (Session 9 picks up here):**

1. **PySpark UDFs.** The `h3_extended_spark.py` module that registers
   UDFs against a Spark session does not exist yet.
2. **Databricks deployment.** Wheel upload to Unity Catalog Volume,
   cluster init, dispatch SQL functions (`h3_auto_latlng_to_cell`
   etc.) for hybrid stock-Photon / ext-Python routing — none of
   this exists yet.

---

## Index

| File | Read when… |
|---|---|
| [architecture.md](architecture.md) | You need the bit layout, the four non-negotiables, the five POC-validated coding patterns, or the widening rules (LB / GR / DW / DR / RW / INIT). |
| [implementation-history.md](implementation-history.md) | You want the session-by-session log of what was widened when, with commit SHAs and ctest progression. |
| [c-library.md](c-library.md) | You need to build the C library, run ctest, understand the FFI shim surface, or check audit state. |
| [python-package.md](python-package.md) | You're calling `h3_extended` from Python and need the function-by-function API, install paths, or performance baselines. |
| [validation.md](validation.md) | You want the Session 8 evidence — what was tested, what passed, what was NOT validated and why. |
| [artifacts.md](artifacts.md) | You need to find the wheel, the dylib, the validation report, the CI workflows, or any other tangible output. Path-by-path inventory. |
| [databricks-roadmap.md](databricks-roadmap.md) | You're starting Session 9, 10, or 11. Detailed plan for UDF registration, Delta schemas, and first-flight pipeline. |

---

## Quick Start — Consuming the Wheel from Another Repo

The fork is private (`devopulence/amalfi-h3-128`). Two consumption modes:

**A. Local development on macOS arm64**

```bash
# 1. Build the C library as a shared object (in the fork repo)
cd /path/to/amalfi-h3-128
mkdir -p build-release && cd build-release
cmake .. -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
         -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_SHARED_LIBS=ON \
         -DBUILD_TESTING=OFF
cmake --build . -j --target h3
# Produces build-release/lib/libh3.dylib

# 2. Stage into the package and build the wheel
cp build-release/lib/libh3.1.dylib h3_extended/libh3_extended.dylib
install_name_tool -id @rpath/libh3_extended.dylib \
                  h3_extended/libh3_extended.dylib
python -m build --wheel
# Produces dist/h3_extended-0.3.0-cp311-cp311-macosx_*.whl

# 3. Install in your other repo's venv
pip install /path/to/amalfi-h3-128/dist/h3_extended-0.3.0-*.whl
```

**B. Linux x86_64 (Databricks target)**

The CI workflow `.github/workflows/python-wheel.yml` produces a
Linux x86_64 wheel on every push to `feat/h3-128-mvp` or `master`.
Download via `gh`:

```bash
GH_TOKEN="$GITHUB_PAT" gh run list \
    -R devopulence/amalfi-h3-128 \
    --workflow python-wheel.yml \
    --limit 5

GH_TOKEN="$GITHUB_PAT" gh run download <run-id> \
    -R devopulence/amalfi-h3-128 \
    -n h3_extended-ubuntu-latest-x86_64-py311

# Yields: h3_extended-0.3.0-cp311-cp311-linux_x86_64.whl
```

This is the wheel to upload to a Databricks Unity Catalog Volume and
install on a cluster (full procedure in
[databricks-roadmap.md](databricks-roadmap.md)).

**Smoke test after install:**

```python
import h3_extended as h3x
import h3 as stock

# Interop sanity check
ext = h3x.latlng_to_cell(40.33, -73.99, 9)         # res 9 — stock-resolution
assert h3x.to_64bit(ext) == int(stock.latlng_to_cell(40.33, -73.99, 9), 16)

# Extended resolution
ext20 = h3x.latlng_to_cell(40.33, -73.99, 20)      # res 20 — ~5mm cell
assert len(ext20) == 32                              # 32-char canonical
assert h3x.cell_to_parent(ext20, 19) == h3x.latlng_to_cell(40.33, -73.99, 19)
```

---

## Authoritative Repository

```
git@github.com:devopulence/amalfi-h3-128.git
Branch:    feat/h3-128-mvp
Upstream:  https://github.com/uber/h3.git (v4.4.1, commit 69e01f3c)
HEAD:      fbd8d04a (2026-05-07) — CI green, wheels published
           (Session 8 validation PASS at 3ee39bc7, 2026-05-05)
```

The fork is **private and must not be open-sourced** (per `CLAUDE.md`
non-negotiable). Do not vendor it into open-source repos.
