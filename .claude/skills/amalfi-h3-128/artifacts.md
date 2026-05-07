# Artifacts Inventory

Concrete things the fork has produced, with absolute paths and access
instructions. Use this as the lookup table when a session needs to
find a wheel, a tag, a binary, or a report.

---

## Repository

| What | Where |
|---|---|
| Authoritative remote | `git@github.com:devopulence/amalfi-h3-128.git` |
| Default branch (production) | `feat/h3-128-mvp` |
| Current HEAD | `fbd8d04a` (2026-05-07) — CI green, wheels published |
| Local clone (dev machine) | `/Users/johndesposito/amalfi_work/amalfi-h3-128/` |
| Upstream reference | `https://github.com/uber/h3.git` (v4.4.1, commit `69e01f3c`) |

The fork is **private**. Do not vendor into open-source repos.

---

## Tags

| Tag | Commit | Date | Meaning |
|---|---|---|---|
| `v0.1.0-128bit-mvp` | `6bc988e9` (annotated, points at `d9af5e79`) | 2026-05-04 | C library MVP — encode/decode/hierarchy/iterators/validation/auxiliary widened. ctest 326/326 |
| `v0.2.0-coordijk-int64` | `88c7c94f` (annotated) | 2026-05-05 | CoordIJK int32 → int64 widening. Unblocks res 20-22 sub-meter. ctest 327/327 |
| `v0.3.0-python-bindings` | `b77d7cf7` (annotated) | 2026-05-05 | h3_extended Python package + cffi + wheels + CI. pytest 203/203 |

Plus three earlier project tags from upstream (`v3.0.0` … `v3.6.0`)
that are not relevant to the fork's work.

The next planned tag (Session 9): `v0.4.0-databricks-udf`.

---

## Wheels

### Local wheel (macOS arm64, this dev machine)

```
/Users/johndesposito/amalfi_work/amalfi-h3-128/dist/
└── h3_extended-0.3.0-cp311-cp311-macosx_26_0_arm64.whl   (164 KiB)
```

Built by `python -m build --wheel`. Bundles
`libh3_extended.dylib` (renamed from `libh3.dylib`) inside the
package. Self-contained — installs into a fresh venv with no external
shared-library dependency beyond libSystem.

To rebuild it:
```bash
cd /Users/johndesposito/amalfi_work/amalfi-h3-128

# 1. Build C lib as shared object
mkdir -p build-release && cd build-release
cmake .. -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
         -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_SHARED_LIBS=ON \
         -DBUILD_TESTING=OFF \
         -DBUILD_BENCHMARKS=OFF \
         -DBUILD_FILTERS=OFF \
         -DBUILD_GENERATORS=OFF
cmake --build . -j --target h3
cd ..

# 2. Stage into package
cp build-release/lib/libh3.1.dylib h3_extended/libh3_extended.dylib
install_name_tool -id @rpath/libh3_extended.dylib \
                  h3_extended/libh3_extended.dylib

# 3. Build wheel
rm -rf dist build
python -m build --wheel
```

### CI-built wheels (both platforms)

The `.github/workflows/python-wheel.yml` workflow runs on every push
to `feat/h3-128-mvp` or `master`, plus on PR and on workflow_dispatch.
It builds:

- `h3_extended-ubuntu-latest-x86_64-py311` — Linux x86_64
  (the **Databricks delivery target**)
- `h3_extended-macos-latest-arm64-py311` — macOS arm64

Each leg builds the C library on the runner, stages it into the
package, builds the wheel, installs it in a clean venv from `/tmp`,
runs `pytest --pyargs h3_extended.tests` (203/203), then uploads the
wheel as a 30-day artifact.

To download the latest:

```bash
set -a && source /Users/johndesposito/amalfi_work/amalfi-h3-128/.env && set +a

# List recent runs
GH_TOKEN="$GITHUB_PAT" gh run list \
    -R devopulence/amalfi-h3-128 \
    --workflow python-wheel.yml --limit 5

# Download a specific run's artifacts
GH_TOKEN="$GITHUB_PAT" gh run download <run-id> \
    -R devopulence/amalfi-h3-128 \
    -n h3_extended-ubuntu-latest-x86_64-py311
```

---

## Compiled C library

| Build | Path | Use |
|---|---|---|
| Dev (sanitized, `.a`) | `build-dev/lib/libh3.a` | ctest, internal tests, fuzzers |
| Release (shared, `.dylib`) | `build-release/lib/libh3.1.dylib` | Source for Python wheel staging |
| Bundled in package | `h3_extended/libh3_extended.dylib` | What `_ffi.dlopen` loads at import |

The bundled lib is **gitignored** (the `.dylib` is in `.gitignore`'s
`*.dylib` rule). Only Python sources are tracked in the package
directory; the lib gets staged into place at wheel-build time.

---

## Python package source

```
/Users/johndesposito/amalfi_work/amalfi-h3-128/h3_extended/
├── __init__.py        16 public API functions + H3Error
├── _ffi.py            cffi cdef + dlopen + ABI consistency check
├── batch.py           3 batch helpers
└── tests/             7 pytest modules, 203 assertions
```

Plus at the project root:
- `pyproject.toml` — package metadata, build system
- `setup.py` — BinaryDistribution override

See [python-package.md](python-package.md) for full API.

---

## Validation script + report

| File | Purpose |
|---|---|
| `validate_local.py` | Session 8 end-to-end validator. Reads geotagged imagery, runs per-image gates + 100K stress + 1M memory leak + performance baselines |
| `validation_report.md` | Latest run output. PASS at HEAD `3ee39bc7` |

To re-run:
```bash
cd /Users/johndesposito/amalfi_work/amalfi-h3-128
.venv-test/bin/python validate_local.py
# Default: 100,000 stress + 1M memory + 103 imagery
```

Customize:
```bash
.venv-test/bin/python validate_local.py \
    --imagery /path/to/imagery_dir \
    --imagery /path/to/another_dir \
    --stress-n 10000 \
    --memory-n 100000 \
    --report /tmp/myreport.md
```

The script handles two GPS-source modes per directory:
1. JSON manifest (`manifest.json` next to imagery) — capture_data schema
2. EXIF GPSInfo IFD — for stock JPEG photo dirs

---

## CI workflows

```
.github/workflows/
├── ci.yml                        Stock H3 CI (inherited from upstream)
├── cifuzz.yml                    OSS-Fuzz integration
├── deploy-website.yml            Upstream website deployment
├── python-wheel.yml              Session 7-py addition — wheel matrix
├── test-bench.yml                Benchmarks
├── test-fuzzer.yml               Fuzz harness
├── test-linux.yml                Linux build matrix
├── test-macos.yml                macOS build matrix
├── test-pkg-config.yml           pkg-config consumer test
├── test-website.yml              Website test
└── test-windows.yml              Windows build (#error trips on this fork)
```

The two that matter for the fork's deliverables:
- `ci.yml` — proves the C library still builds + tests on both
  platforms with sanitizers
- `python-wheel.yml` — produces the consumable wheels

---

## Documentation in the fork

```
/Users/johndesposito/amalfi_work/amalfi-h3-128/
├── CLAUDE.md                            Top-level guidance for sessions in the fork
├── POC_MANDATE.md                       The 5 mandatory patterns + POC results
├── POC_TRANSFER_GUIDE.md                Maps POC code → implementation phases
├── SESSION_GUIDE.md                     Original 3-session architecture
├── h3-extended-playbook-v4.1.0.md       Strategic playbook (§0–§14)
├── amalfi-h3-128-session-plan.md        Session 6-11 plan (post-MVP)
├── LESSONS_LEARNED.md                   9 lessons from Sessions 5-7
├── README.md                            Stock H3 README (unchanged)
├── poc1.md, poc2.md, poc3.md, poc4.md, poc5.md     POC specifications
├── POC1_CHECKPOINT.md … POC4_CHECKPOINT.md          POC results
├── SESSION_1_CHECKPOINT.md … SESSION_8 (S8 = validation_report.md)
└── validation_report.md                 Session 8 PASS evidence
```

If you want to understand WHY a decision was made, those docs are the
source. If you want to understand WHAT to do next, this skill is the
source.

---

## Key paths cheat-sheet

```
# Repo
/Users/johndesposito/amalfi_work/amalfi-h3-128/

# C library — sanitized dev build
/Users/johndesposito/amalfi_work/amalfi-h3-128/build-dev/

# C library — release shared object source
/Users/johndesposito/amalfi_work/amalfi-h3-128/build-release/lib/libh3.1.dylib

# Python package source
/Users/johndesposito/amalfi_work/amalfi-h3-128/h3_extended/

# Wheel output
/Users/johndesposito/amalfi_work/amalfi-h3-128/dist/h3_extended-0.3.0-*.whl

# Validation
/Users/johndesposito/amalfi_work/amalfi-h3-128/validate_local.py
/Users/johndesposito/amalfi_work/amalfi-h3-128/validation_report.md

# Imagery (sibling repo — read-only consumption)
/Users/johndesposito/amalfi_work/amalfi_intelligence_platform/photos/april-11-jpg/
/Users/johndesposito/amalfi_work/amalfi_intelligence_platform/capture_data/monmouth_30_9.02/2026-03-29/

# Pre-existing test venv (cffi+pytest+h3+exifread+Pillow installed)
/Users/johndesposito/amalfi_work/amalfi-h3-128/.venv-test/

# CI auth (NEVER commit this — has GITHUB_PAT)
/Users/johndesposito/amalfi_work/amalfi-h3-128/.env

# This skill
/Users/johndesposito/.claude/skills/amalfi-h3-128/
```

The `.env` file contains `GITHUB_PAT` and is gitignored. Use it as:

```bash
set -a && source /Users/johndesposito/amalfi_work/amalfi-h3-128/.env && set +a
GH_TOKEN="$GITHUB_PAT" gh <command>
```

---

## What this fork does NOT produce (yet)

For situational awareness — these are real Databricks artifacts the
sibling repo will need to build:

- **PySpark UDFs** (`h3_extended_spark.py` from Session 9) — does not
  exist
- **SQL dispatch layer** (`h3_auto_*` functions) — does not exist
- **Delta table schemas** (`bronze.drone_flights`, `silver.*`,
  `gold.*` from Session 10) — do not exist
- **Auto-Loader configuration for S3 ingestion** — does not exist
- **Cluster init script for wheel install** — does not exist
- **Unity Catalog Volume layout** — does not exist

Session 9 onward (next session in another repo) will create these.
See [databricks-roadmap.md](databricks-roadmap.md).
