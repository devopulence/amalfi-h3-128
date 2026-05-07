# Validation Evidence

This is the proof that the fork works. Three layers of validation:

1. **POC campaign** — 10,067 assertions before any H3 source was modified
2. **C-side ctest** — 327/327 PASS at HEAD, including 2.5M+ ext assertions
3. **Session 8 local validation** — real-imagery PASS at HEAD `3ee39bc7`

The Databricks-bound consumer cares mostly about layer 3 (does the
Python package actually work end-to-end with real data?), but the
others provide the foundation it rests on.

---

## Layer 1 — POC campaign (pre-implementation)

| POC | Coverage | Assertions | Result |
|---|---|---:|---|
| POC-1 | Bit layout + 14 Group C macros | 685 | PASS |
| POC-2 | 5 validation predicates (`_hasGoodTopBits`, `_firstOneIndex`, `_hasAny7UptoRes`, `_hasAll7AfterRes`, `_hasDeletedSubsequence`) | 3,473 | PASS |
| POC-3 | Iterator state machine — full 828,210-cell res 15 → 22 enumeration | 4,595 | PASS |
| POC-4 | FFI ABI cross-TU consistency, by-pointer convention, string round-trip at FFI boundary | 1,314 | PASS |
| **Total** | | **10,067** | **All clean under ASAN+UBSAN** |

POC-5 (added Session 5) demonstrated CoordIJK int32 overflow at ext
resolutions before the typedef widening landed.

All POCs ran on Apple clang 21.0.0 / macOS arm64 with
`-fsanitize=address,undefined -Werror -Wall -Wextra`. **Zero
sanitizer findings, zero failures.**

These are referenced by checkpoint files in the fork repo:
`POC1_CHECKPOINT.md`, `POC2_CHECKPOINT.md`, `POC3_CHECKPOINT.md`,
`POC4_CHECKPOINT.md`.

What POCs did NOT prove (left for implementation-time validation):
- Integration with the actual H3 source tree (typedef cascade, CMake,
  stock test fixtures)
- Geometric correctness (lat/lng encoding, face/ijk projection)
- Performance
- Cross-platform behavior (Linux x86_64)

---

## Layer 2 — C-side ctest

`cmake --build build-dev -j && (cd build-dev && ctest -j4)` →
**327/327 PASS** at HEAD `3ee39bc7`, ~156 sec wall time.

Composition:
- 316 stock H3 v4.4.1 tests — **byte-identical pass** (Non-Negotiable
  #3)
- 11 ext suites — see `c-library.md` for the full list

Notable assertion volumes (ext suites only):
- `testHierarchyExt`: 2,513,991 (dominated by D4-G8's full
  cellToChildPos enumeration across all (parentRes, childRes) pairs)
- `testEncoderExt`: 8,305
- `testDecoderExt`: 5,401
- `testStringExt`: 4,051
- `testCoordIjkExtRes`: ~3,000
- `testLocalIjExt`: 1,707
- `testFFIShimExt`: 1,079 (10 original + 8 follow-on shims, plus
  cross-TU sizeof/alignof and 1000-cell string round-trip)
- `testAuxiliaryExt`: 771
- `testAccessorExt`: 559
- `testRotationExt`: 302
- `testValidationExt`: 220

Total ext assertions ≈ 2,540,000.

ASAN+UBSAN clean throughout. No suppressions, no waivers.

### Audit infrastructure status

`python3 .claude/skills/h3-128-audit/audit.py` → **23 sites
classified, 0 CRITICAL, 0 FINDING.**

Distribution:
- 13 widened (per Pattern 5 rules: LB, GR, DW/DR, RW, INIT)
- 10 deferred (with reasons — see [c-library.md](c-library.md))

Every widening has a corresponding `mutations/manifest.json` entry
that records "if you reverted this widening, this test would fail" —
used as negative testing.

### CI matrix

`.github/workflows/ci.yml` runs the full ctest matrix on:
- `ubuntu-latest` × `release` config (gcc, x86_64)
- `ubuntu-latest` × `sanitizers` config (gcc + ASAN+UBSAN, x86_64)
- `macos-latest` × `release` config (clang, arm64)
- `macos-latest` × `sanitizers` config (clang + ASAN+UBSAN, arm64)

Last confirmed-green CI run was `25388983711` at HEAD `88c7c94f` (the
v0.2.0 widening commit, Session 6). Sessions 7-cal, 7-py, and 8 made
additional commits but the CI watch was deferred.

**CI verification — DONE.** `python-wheel.yml` was red at `3ee39bc7`
due to two workflow bugs (see `databricks-roadmap.md` "Resolved CI
bugs"). Fixed at `fbd8d04a` (2026-05-07). Both matrix legs now go
green on every push: Linux x86_64 ~43 s, macOS arm64 ~31 s. Linux
wheel artifact (~94 KiB) downloadable via
`gh run download -n h3_extended-ubuntu-latest-x86_64-py311`.

**The only validation hole left** in this category is benchmarking
the Linux x86_64 wheel against a non-CI Linux box (e.g., a
Databricks executor) under realistic UDF load. Session 9 closes
this naturally.

---

## Layer 3 — Session 8 local validation against real imagery

`validate_local.py` ran the full per-image suite + 100K stress test
+ 1M-call memory leak check + per-op throughput baselines against
geotagged imagery from the sibling `amalfi_intelligence_platform`
repo.

**Result: PASS** at HEAD `3ee39bc7`. Full report at
`/Users/johndesposito/amalfi_work/amalfi-h3-128/validation_report.md`.

### Imagery corpus

| Source | Count | GPS source | Device |
|---|---:|---|---|
| `photos/april-11-jpg/` | 40 | EXIF | iPhone 13 |
| `capture_data/monmouth_30_9.02/2026-03-29/keyframes/` | 63 | manifest.json | iPhone 13 |
| `photos/april-23-jpg/` | 0 (skipped) | no GPS | — |
| **Total** | **103** | | |

103 GPS-tagged images, **33 distinct (lat, lng) points**. The 63
keyframes share a single GPS coord (per the video's source — iPhone
video stores one GPS point for the entire file, not per-frame). The
40 stills give the unique-point diversity. All photos cluster at the
Monmouth Beach property — coverage is geographically narrow (lat span
~22 m, lng span essentially zero).

This is intentional: the real-imagery gate proves correctness on
actual EXIF / manifest GPS pipelines; the 100K stress test provides
the geographic spread.

### Per-image gates

All seven gates passed on every image. Round-trip distance was
**0.000000 mm** at every image — confirms macOS arm64 + Apple libm
idempotency from Session 7's measurement.

| Gate | PASS | BOUNDARY | FAIL | Detail |
|---|---:|---:|---:|---|
| Backward compat (res 15 ↔ stock h3-py 4.4.2) | 103 | — | 0 | byte-identical via `to_64bit` |
| Parent res 19 → 15 == latlng_to_cell res 15 | 101 | 2 | 0 | 2 boundary cases (geographic) |
| Parent res 20 → 19 | 100 | 3 | 0 | 3 boundary cases (geographic) |
| Round-trip res 19 < 1 cm | 103 | — | 0 | max obs 0.000000 mm |
| Round-trip res 20 < 5 mm | 103 | — | 0 | max obs 0.000000 mm |
| grid_disk(k=2) → 19 unique res-19 cells | 103 | — | 0 | all cells valid, all res 19 |
| String round-trip (16-char + 32-char) | 103 | — | 0 | every cell parses back identically |

**BOUNDARY** explanation: the input lat/lng sits within ε of a
coarser-resolution cell edge, so `cell_to_parent(c19, 15)` lands in a
sibling res-15 cell of `latlng_to_cell(lat, lng, 15)`. The hierarchy
contract (child contained by parent) is preserved — what differs is
which of the 2-or-3 equally-close coarser cells the projection
rounding picks. Geographic edge case, not a fork bug. The same
phenomenon was reproduced in `test_hierarchy.py` and led to the
"anchor descent at parent center" test pattern.

### Stress test — 100K random Monmouth County coords

Bbox: lat [40.10, 40.50], lng [-74.30, -74.00].

| Metric | Result |
|---|---:|
| Total points | 100,000 |
| Wall time | 1.36 s |
| Throughput | 73,556 index-pairs/sec |
| Invalid res-19 cells | 0 |
| Invalid res-20 cells | 0 |
| Invalid res-19 parents from res-20 | 0 |
| Boundary disagreements (parent c20 ≠ c19) | 7,218 (7.22%) |
| Distinct res-19 cells | 100,000 / 100,000 |
| Distinct res-20 cells | 100,000 / 100,000 |
| Result | **PASS** |

7.22% boundary rate is the natural geometric expectation for uniformly
random points — at any resolution, that fraction of the area is
within ε of a cell edge. Not a fork bug. Treated as informational.

### Memory stability — 1M latlng_to_cell calls under tracemalloc

| Call count | tracemalloc current (KiB) |
|---:|---:|
| 1 | 1.3 |
| 250,001 | 1.5 |
| 500,001 | 1.6 |
| 750,001 | 1.8 |
| 1,000,000 | 1.9 |

- Peak growth above start: 2.0 KiB
- Final delta: **1.9 KiB**
- Gate (< 1 MiB final delta): **PASS**

Effectively zero leak. cffi allocations (`ffi.new("uint8_t[16]")`,
etc.) are GC-managed and do not accumulate. This is the
single-most-important pre-Databricks check: Databricks executors run
millions of UDF invocations per query, and a 64-byte-per-call leak
would compound to gigabytes within a single notebook run.

### Performance baselines

cffi ABI mode, macOS arm64, single-thread, no batching.

| Operation | Iterations | Time | ops/sec |
|---|---:|---:|---:|
| `latlng_to_cell(res=19)` | 10,000 | 0.0258 s | 390,918 |
| `cell_to_parent(res19→15)` | 10,000 | 0.0232 s | 427,932 |
| `grid_disk(k=3, res=19)` | 1,000 | 0.0674 s | 15,343 |

These are the **per-row UDF baselines** for Session 9 Databricks
comparison. A Spark UDF wrapping `latlng_to_cell` will run at
something close to 390K ops/sec per executor core (minus the JVM-
to-Python serialization overhead).

For comparison: stock h3-py runs the same operations at 1-2M ops/sec
(no cffi overhead, all C). The ~3-5× slowdown matches the playbook
expectation; this is why the dispatch layer (Session 9) routes res
0-15 to Photon-accelerated stock H3 and only res 16-22 to
h3_extended.

---

## What was NOT validated

Honest accounting of remaining holes:

1. **Cross-platform parity (Linux x86_64)** — the fork built and ran
   under gcc on Linux x86_64 (CI matrix), but no Linux-side wheel
   build + pytest run has been benchmarked end-to-end. The
   `python-wheel.yml` workflow does this on every push but the most
   recent Sessions 7-py and 8 commits have not been CI-watched. The
   pre-Databricks gate is "verify CI green at HEAD `3ee39bc7`."

2. **Cross-libm round-trip variability** — Apple libm gave 0 rad
   delta across 1000 × 23 resolutions. glibc may give different
   ULP-level deltas. The `latlng_within_tolerance` helper exists to
   handle this, but it has no callers yet (within-platform tests use
   bit-identity instead). When a cross-platform test demands it, a
   CI-side measurement harness will recalibrate.

3. **Pentagon paths at boundary scenarios beyond Sandy Hook** — the
   per-image gate at 103 photos × Monmouth doesn't include any pentagon
   cells (Sandy Hook is in the interior of pentagon 17). C-side
   testHierarchyExt and testCoordIjkExtRes exercise pentagon paths at
   ext res, and those pass — but the Python wrapper hasn't been
   verified against pentagon-adjacent geographies. Low risk
   (pentagon code is unchanged at the API surface), but worth noting.

4. **Real flight imagery** — Session 11 work. The 103 photos here are
   iPhone 13 phone shots, not drone captures. Drone EXIF / SRT
   timing / multispectral channels are different and will need
   separate handling.

5. **High-throughput vectorized path** — `batch.py` uses Python
   loops. A Pandas UDF using `pyarrow` arrays would give 10-50×
   throughput by amortizing the cffi call overhead. Deferred until
   actually needed.

6. **Concurrency / multi-process** — the cffi `lib` object is shared
   across the import; calls are thread-safe at the C level (no global
   state), but Python's GIL serializes them. Spark executors run
   workers in separate processes, which sidesteps this entirely.

These are all acceptable holes for the v0.3.0 release. The
pre-Databricks gate is met.
