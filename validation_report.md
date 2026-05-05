# H3-Extended Local Validation Report (Session 8)

- Started:  2026-05-05 17:28:39 EDT
- Finished: 2026-05-05 17:28:48 EDT
- Total images processed: **103**
- Stock h3-py compared: **4.4.2**
- h3_extended version: **0.3.0**
- Gate result: **PASS**

## Image Inventory

| Source directory | Count | GPS source | Device |
|---|---:|---|---|
| `/Users/johndesposito/amalfi_work/amalfi_intelligence_platform/capture_data/monmouth_30_9.02/2026-03-29/keyframes` | 63 | manifest | iPhone 13 |
| `/Users/johndesposito/amalfi_work/amalfi_intelligence_platform/photos/april-11-jpg` | 40 | exif | iPhone 13 |

GPS extent: lat [40.3371, 40.3373], lng [-73.9852, -73.9852]
Distinct (lat, lng) points: **33**

## Per-Image Gates

| Gate | Result | Detail |
|---|---|---|
| Backward compat (res 15 ↔ stock h3-py) | PASS | PASS=103, FAIL=0  |
| Parent res 19 → 15 (== latlng_to_cell res 15) | PASS (2 BOUNDARY) | PASS=101, BOUNDARY=2, FAIL=0 BOUNDARY = res-15 edge case (geographic, not bug) |
| Parent res 20 → 19 | PASS (3 BOUNDARY) | PASS=100, BOUNDARY=3, FAIL=0 BOUNDARY = res-19 edge case (geographic, not bug) |
| Round-trip res 19 < 1cm | PASS | PASS=103, FAIL=0 max obs 0.000000 mm |
| Round-trip res 20 < 5mm | PASS | PASS=103, FAIL=0 max obs 0.000000 mm |
| grid_disk(k=2) → 19 unique valid res-19 cells | PASS | PASS=103, FAIL=0  |
| String round-trip (16-char stock, 32-char ext) | PASS | PASS=103, FAIL=0  |

**Boundary cases (parent res 19 → 15):**


## Stress Test — random Monmouth County coords

- Total: **100,000** points in 1.34 s (74,464 index-pairs/sec)
- Invalid cells at res 19: **0**
- Invalid cells at res 20: **0**
- Invalid res-19 parents from res-20: **0**
- Boundary disagreements (parent of c20 ≠ c19 of same coord): **7,218** (7.22%) — informational; geographic edge case, not a fail
- Distinct res 19 cells: **100,000**
- Distinct res 20 cells: **100,000**
- Result: **PASS**

## Memory Stability — 1M latlng_to_cell calls (tracemalloc)

| Calls | tracemalloc current (KiB) |
|---:|---:|
| 1 | 1.3 |
| 250,001 | 1.5 |
| 500,001 | 1.6 |
| 750,001 | 1.8 |
| 1,000,000 | 1.9 |

- Peak growth above start: **2.0 KiB**
- Final delta vs start: **1.9 KiB**
- Leak gate (< 1 MiB final delta): **PASS**

## Performance Baselines

| Operation | Iterations | Total (s) | ops/sec |
|---|---:|---:|---:|
| latlng_to_cell(res=19) | 10,000 | 0.0256 | 390,918 |
| cell_to_parent(res19→15) | 10,000 | 0.0234 | 427,932 |
| grid_disk(k=3, res=19) | 1,000 | 0.0652 | 15,343 |

Recorded for Databricks UDF comparison (Session 9).

## Notes

- **Imagery coverage** is geographically narrow: lat span ~27.5 m, lng span ~3.4 m, 33 distinct (lat,lng) points across 103 images. The 63 video keyframes share a single source GPS point per the manifest; the 40 stills span a single property. The 100K stress test provides geographic spread; the imagery gates prove correctness on real-world EXIF/manifest coords.
- BOUNDARY rows are reported for diagnostic transparency. They occur when the original lat/lng input sits within ε of a coarser-resolution cell edge, so `cell_to_parent(c19, 15)` lands in a sibling res-15 cell of `latlng_to_cell(lat, lng, 15)`. The hierarchy contract (child contained by parent) is preserved — what differs is which of the two-or-more equally-close coarser cells the projection rounding picks. This mirrors the same diagnostic exposed in the Python test suite (test_hierarchy.py).

- This validation gate has no release tag. Per the session plan, Session 8 is the quality gate before Databricks deployment (Session 9 → `v0.4.0-databricks-udf`).
