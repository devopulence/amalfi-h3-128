# Amalfi H3 Extended — Post-MVP Session Plan

> **Effective after:** Session 6 completion (v0.2.0 CoordIJK int64 widening)
> **Pairs with:** `CLAUDE.md` v1.0.0, `h3-extended-claude-code-prompt-v2.4.0.md`

---

## Session 6 (IN PROGRESS): v0.2.0 — CoordIJK int64 Widening

**Goal:** Unblock resolution 20-22 full precision by widening CoordIJK from int32 to int64.

**Steps (from SESSION_5_CHECKPOINT.md):**
1. Widen `maxDimByCIIres` / `unitScaleByCIIres` arrays to `int64_t[23]`
2. Widen `_adjustOverageClassII` / `_adjustPentVertOverage` int locals → `int64_t`
3. Add `testCoordIjkExtRes` regression suite (res 19-22 round-trip closure at Monmouth 40.33/-73.99, Palm Beach 26.71/-80.05, Piano di Sorrento 40.63/14.40)
4. Update `audit.py` to reclassify the table sites from `deferred:post-mvp` → `widened`

**Exit criteria:** 
- All `ctest` passing (326+ tests)
- Res 20 `latLngToCell` → `cellToLatLng` round-trip closes within 1cm at all three target geographies
- Res 22 round-trip closes within 1mm
- Pentagon paths at res 19-22 stable (non-monotonic failure from POC-5 resolved)
- ASAN+UBSAN clean
- Tag: `v0.2.0-coordijk-int64`

---

## Session 7: Python Bindings + FFI Shim Integration

**Goal:** Build the `h3_extended` Python package that wraps the C library via the D7 FFI shim layer, making all 27 MVP functions callable from Python.

**Prerequisites:** v0.2.0 tagged, CI green.

**Steps:**

1. **Create the Python package structure:**
```
h3_extended/
├── __init__.py          # Public Python API
├── _ffi.py              # cffi bindings to libh3_extended.so
├── batch.py             # Vectorized batch operations (prep for Pandas UDFs)
├── tests/
│   ├── test_basic.py           # Res 0-15 backward compat
│   ├── test_extended_res.py    # Res 16-22 creation, area, edge length
│   ├── test_hierarchy.py       # Parent/child across res 15→16 boundary
│   ├── test_backward_compat.py # Compare against stock h3-py v4.x
│   ├── test_precision.py       # Round-trip tolerance at all three geographies
│   ├── test_grid_ops.py        # gridDisk, gridDistance, gridPathCells at ext res
│   └── test_string.py          # Hex string round-trip (15-16 char stock, 32 char ext)
└── setup.py             # Wheel build configuration
```

2. **Implement cffi bindings in `_ffi.py`:**
   - Load `libh3_extended.so` (renamed from `libh3.so` during wheel build to avoid collision with stock h3-py)
   - Declare all 27 MVP functions from the FFI shim (`h3ExtShim.h`) using `ffi.cdef()`
   - All `H3Index` parameters pass through the shim as `const uint8_t *` (16-byte arrays) — never by value
   - Handle endianness: document that the shim uses little-endian byte order on x86_64

3. **Implement public API in `__init__.py`:**
   - `latlng_to_cell(lat, lng, resolution) → str` (hex string)
   - `cell_to_latlng(cell_hex) → (lat, lng)`
   - `cell_to_parent(cell_hex, parent_res) → str`
   - `cell_to_children(cell_hex, child_res) → list[str]`
   - `cell_to_boundary(cell_hex) → list[(lat, lng)]`
   - `get_resolution(cell_hex) → int`
   - `cell_area(cell_hex, unit='m^2') → float`
   - `grid_disk(cell_hex, k) → list[str]`
   - `grid_distance(cell_a, cell_b) → int`
   - `grid_path_cells(cell_a, cell_b) → list[str]`
   - `is_valid_cell(cell_hex) → bool`
   - `cell_to_local_ij(origin, cell) → (i, j)`
   - `local_ij_to_cell(origin, i, j) → str`
   - `to_64bit(cell_hex) → int` (only valid res 0-15)
   - `from_64bit(legacy_int) → str`
   - `get_effective_resolution(cell_hex) → int` (returns true 0-22, not the bits 52-55 value)

4. **Implement batch operations in `batch.py`:**
   - `batch_latlng_to_cell(lats, lngs, res) → list[str]`
   - `batch_cell_to_parent(cells, parent_res) → list[str]`
   - `batch_get_resolution(cells) → list[int]`
   - These are Python loops for now (Phase 1 performance) but structured for drop-in Pandas UDF upgrade

5. **Build the wheel:**
   - Build C library: `cmake .. -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_BUILD_TYPE=Release`
   - Copy and rename: `cp build/lib/libh3.so h3_extended/libh3_extended.so`
   - Build wheel: `python setup.py bdist_wheel`
   - Verify wheel installs cleanly in a fresh virtualenv
   - Test: `pytest h3_extended/tests/ -v`

6. **Run the full Python test suite:**
   - All tests must pass
   - Backward compat test must match stock h3-py v4.x for res 0-15
   - Precision test must close within tolerance at all three geographies
   - String round-trip must handle 15-16 char stock and 32 char extended

**Exit criteria:**
- All Python tests pass
- Wheel builds and installs cleanly on macOS arm64 and Linux x86_64
- `libh3_extended.so` loads without collision with stock h3-py
- All 27 MVP functions callable from Python
- Backward compat verified against stock h3-py v4.x
- Tag: `v0.3.0-python-bindings`

---

## Session 8: Local End-to-End Validation with Real Imagery

**Goal:** Validate the complete pipeline locally on your laptop using real drone/camera imagery from previous 64-bit development. Prove spatial indexing correctness at resolution 19-20 before touching Databricks.

**Prerequisites:** v0.3.0 tagged, Python bindings working.

**Why this session exists:** Finding a spatial indexing bug, precision edge case, or FFI memory issue on your laptop takes 5 minutes to debug. Finding the same bug on a Databricks cluster takes hours. This session is the quality gate between "library works" and "pipeline works."

**Steps:**

1. **Inventory existing test imagery:**
   - Locate all imagery from previous 64-bit H3 development work
   - Identify images with known GPS coordinates (geotagged photos, drone captures)
   - Categorize by type: aerial RGB, ground-level, any thermal if available
   - If no geotagged imagery exists, capture a few photos with a phone (GPS metadata in EXIF) at known locations around your Monmouth Beach property

2. **Build the local validation script (`validate_local.py`):**
   ```python
   # This script simulates the full pipeline locally without Spark:
   # Image → extract GPS from EXIF → latLngToCell at res 15, 19, 20
   # → verify hex placement → compare res 15 against stock H3
   # → compute per-hex pixel statistics → output validation report
   ```
   
   The script must perform:
   
   a. **EXIF GPS extraction:**
      - Read lat/lng from image EXIF metadata (use Pillow or exifread)
      - Extract altitude if available (for GSD calculation)
      - Log coordinate precision from EXIF (typically 5-7 decimal places)
   
   b. **Multi-resolution H3 indexing:**
      - Index each image center at res 15 using both stock h3-py and h3_extended
      - **CRITICAL VALIDATION:** Verify res 15 hex from h3_extended matches stock h3-py exactly (backward compat gate)
      - Index at res 19 and res 20 using h3_extended
      - Verify parent-child consistency: `cell_to_parent(res19_cell, 15)` must equal the res 15 cell
      - Verify `cell_to_parent(res20_cell, 19)` must equal the res 19 cell
   
   c. **Pixel-to-hex mapping (if imagery has sufficient resolution):**
      - Divide image into a grid based on GSD and hex size
      - Assign each pixel region to its H3 res 19 hex
      - Compute per-hex statistics: avg R, G, B, pixel count
      - Verify adjacent hexes have different hex IDs (spatial discrimination test)
   
   d. **Round-trip precision validation:**
      - For each image: `latLngToCell(lat, lng, res)` → `cellToLatLng(cell)` → measure error
      - At res 19: error must be < 1cm
      - At res 20: error must be < 5mm
      - Test at all available image locations (not just the three canonical test points)
   
   e. **Grid neighborhood validation:**
      - For each image center hex at res 19: call `grid_disk(hex, k=2)`
      - Verify all returned hexes are valid
      - Verify all returned hexes are at res 19
      - Verify no duplicates
      - Verify hex count matches expected (1 + 6 + 12 = 19 for k=2)
   
   f. **String serialization validation:**
      - Every hex created during the test: convert to string, parse back, compare
      - Res 15 strings must be 16 chars (zero-padded per v2.4.0 §5)
      - Res 19-20 strings must be 32 chars
   
   g. **Performance baseline (document, don't optimize):**
      - Time 10,000 `latlng_to_cell` calls at res 19
      - Time 10,000 `cell_to_parent` calls
      - Time 1,000 `grid_disk(k=3)` calls
      - Record results for comparison against Databricks UDF performance later

3. **Generate validation report (`validation_report.md`):**
   - Total images processed
   - Backward compatibility: PASS/FAIL (res 15 match against stock h3-py)
   - Parent-child consistency: PASS/FAIL at every resolution pair
   - Round-trip precision: actual error in cm/mm at each resolution
   - Grid operations: PASS/FAIL
   - String serialization: PASS/FAIL
   - Performance baselines: ops/sec for each operation
   - Any anomalies, edge cases, or unexpected behavior documented

4. **Stress test with synthetic coordinates:**
   - Generate 100,000 random lat/lng pairs within Monmouth County bounding box
   - Index all at res 19 and res 20
   - Verify no crashes, no invalid cells, no duplicate hex IDs for distinct coordinates
   - Run `cell_to_parent` on all res 20 cells → verify all produce valid res 19 parents
   - This is the volume test that proves the library handles real-world data loads

5. **Memory and stability validation:**
   - Run the full validation script under Python's `tracemalloc` to profile memory usage
   - Run 1M `latlng_to_cell` calls and verify no memory leaks (flat memory profile)
   - If cffi allocations are growing, identify and fix before moving to Databricks where the leak would compound across millions of rows

**Exit criteria:**
- Backward compatibility: res 15 output matches stock h3-py for every test image
- Parent-child consistency: clean across res 15 → 19 → 20 for every test image
- Round-trip precision: < 1cm at res 19, < 5mm at res 20
- Grid operations: correct counts, no duplicates, all valid
- String round-trip: clean for every hex created during testing
- No memory leaks under 1M call stress test
- Performance baselines documented
- Validation report committed to repo
- No tag for this session — it's a validation gate, not a release artifact

---

## Session 9: PySpark UDFs + Databricks Deployment

**Goal:** Register H3 Extended UDFs in Databricks and execute first live queries against real coordinates on a cluster.

**Prerequisites:** Session 8 validation report clean, no memory leaks, all precision gates met.

**Steps:**

1. **Build the PySpark UDF module (`h3_extended_spark.py`):**
   - All 9 UDFs from v2.4.0 §12a: `h3x_latlng_to_cell`, `h3x_cell_to_latlng`, `h3x_get_resolution`, `h3x_cell_area_m2`, `h3x_cell_to_parent`, `h3x_cell_to_children`, `h3x_grid_disk`, `h3x_to_res15`, `h3x_to_legacy_64bit`
   - Add `h3x_grid_distance` and `h3x_grid_path_cells` UDFs
   - `register_all_udfs(spark)` helper for SQL access
   - All UDFs handle NULL inputs gracefully (return None, don't crash)

2. **Build the `h3_auto_*` dispatch layer (v2.4.0 §12c):**
   - SQL wrapper functions that route res 0-15 to Photon-accelerated native H3 and res 16-22 to h3_extended UDFs
   - Include the lat/lng vs lng/lat argument order comment
   - Register in Spark SQL catalog

3. **Upload wheel to Databricks:**
   - Upload `h3_extended-*.whl` to Unity Catalog Volume: `/Volumes/amalfi/libraries/`
   - Configure cluster init script or notebook-scoped `%pip install`
   - Verify `import h3_extended` works on driver and executor nodes

4. **First live queries:**
   - `SELECT h3x_latlng_to_cell(40.33, -73.99, 19)` — Monmouth County at res 19
   - `SELECT h3x_latlng_to_cell(26.71, -80.05, 20)` — Palm Beach at res 20
   - `SELECT h3x_latlng_to_cell(40.63, 14.40, 19)` — Piano di Sorrento at res 19
   - Verify results match local validation from Session 8
   - Test `h3_auto_latlng_to_cell` dispatch: res 13 routes to native, res 19 routes to extended

5. **Bulk performance test:**
   - Generate DataFrame with 100K lat/lng rows
   - Apply `h3x_latlng_to_cell` UDF at res 19
   - Document: rows/sec, executor memory, any serialization issues
   - Compare against Session 8 local performance baseline

**Exit criteria:**
- All UDFs register and execute on Databricks cluster
- Live query results match local validation
- Dispatch layer correctly routes stock vs extended resolutions
- Bulk performance documented
- Tag: `v0.4.0-databricks-udf`

---

## Session 10: Delta Lake Schemas + Pipeline Skeleton

**Goal:** Create the Bronze, Silver, and Gold Delta tables and wire up the ingestion pipeline skeleton.

**Prerequisites:** v0.4.0 tagged, UDFs working on cluster.

**Steps:**

1. **Create Delta tables from v2.4.0 §13:**
   - `bronze.drone_flights` — raw flight metadata, partitioned by `asset_type, asset_id`
   - `silver.georegistered_frames` — multi-resolution H3 columns (res 15, 19, 20), partitioned by `asset_type, asset_id, h3_res15`, Z-ORDERed by `h3_res19, frame_timestamp`
   - `gold.hex_health_timeline` — health scores, regression metrics, anomaly detection, MLflow traceability, partitioned by `asset_type, asset_id, h3_res15_parent`, Z-ORDERed by `h3_cell, observation_date`

2. **Build Silver transform notebook:**
   - Read from Bronze
   - Apply `h3x_latlng_to_cell` at res 15, 19, and 20
   - Apply `h3x_to_res15` for partition column
   - Write to Silver with merge/upsert logic

3. **Build Gold transform notebook:**
   - Read from Silver
   - Compute per-hex aggregated metrics (avg RGB, pixel count)
   - Placeholder health score (simple NDVI-like computation from RGB)
   - Compute regression fields (health_delta_7d, health_delta_30d, trend)
   - Write to Gold

4. **Test with synthetic data:**
   - Generate 10K synthetic flight records with coordinates spanning Monmouth County
   - Push through Bronze → Silver → Gold
   - Verify partition pruning with EXPLAIN
   - Verify Z-ORDER optimization runs

5. **Configure Auto-Loader for S3:**
   - Set up Auto-Loader to watch an S3 prefix for new flight data
   - Test with a manual file drop
   - Verify Bronze table receives the data

**Exit criteria:**
- All three Delta tables created with correct schemas and properties
- Bronze → Silver → Gold transform chain executes cleanly
- Partition pruning confirmed via EXPLAIN
- Auto-Loader ingests from S3
- Tag: `v0.5.0-delta-pipeline`

---

## Session 11: First Real Flight Data

**Goal:** Fly your property, push real imagery through the complete pipeline, and see the digital twin come alive.

**Prerequisites:** Part 107 certification, drone purchased, v0.5.0 tagged, pipeline operational.

**Steps:**

1. Fly your Monmouth Beach property with the DJI Mavic 3 Multispectral
2. Upload imagery to S3
3. Auto-Loader ingests to Bronze
4. Silver transform indexes at res 15, 19, 20
5. Gold transform computes health scores
6. Query the digital twin of your own property at sub-centimeter resolution

**Exit criteria:**
- Real drone imagery flowing through the complete pipeline
- Your property has a digital twin at resolution 19-20
- Health scores computed per hex
- Foundation for ML model training established
- Tag: `v1.0.0-first-flight`

---

## Summary: Session Roadmap

| Session | Goal | Artifact |
|---------|------|----------|
| 6 | CoordIJK int64 widening | `v0.2.0-coordijk-int64` |
| 7 | Python bindings + FFI | `v0.3.0-python-bindings` |
| 8 | Local validation with real imagery | Validation report (no tag) |
| 9 | PySpark UDFs + Databricks | `v0.4.0-databricks-udf` |
| 10 | Delta Lake schemas + pipeline | `v0.5.0-delta-pipeline` |
| 11 | First real flight data | `v1.0.0-first-flight` |

Six sessions from here to a live digital twin of your property. The 128-bit fork becomes a production intelligence platform.
