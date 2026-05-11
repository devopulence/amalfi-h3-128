# Databricks Roadmap (Sessions 9, 10, 11)

The next three sessions take the fork from "Python-callable" to
"production digital twin pipeline." Each maps to a tag in the C
library's release timeline.

| Session | Goal | Tag | Repo |
|---|---|---|---|
| 9 | PySpark UDFs + Databricks deployment | `v0.4.0-databricks-udf` | sibling repo (your Databricks repo) |
| 10 | Delta Lake schemas + pipeline skeleton | `v0.5.0-delta-pipeline` | sibling repo |
| 11 | First real flight data | `v1.0.0-first-flight` | sibling repo |

The session-plan source (`amalfi-h3-128-session-plan.md` in the fork)
specifies these in detail. This file extracts the actionable parts and
adds the integration logistics — paths to the wheel, dispatch
patterns, init scripts.

---

## Session 9 — PySpark UDFs + Databricks Deployment

**Goal:** Register `h3_extended` UDFs in Databricks and execute first
live queries against real coordinates on a cluster.

**Prerequisites:**
- Linux x86_64 wheel from `python-wheel.yml` CI artifact (see
  [artifacts.md](artifacts.md))
- Session 8 validation report clean (it is — at `3ee39bc7`)
- Databricks workspace + Unity Catalog + a target cluster

### Step 1 — Build the PySpark UDF module

Create `h3_extended_spark.py` in your Databricks repo. Per the session
plan §9, the UDF surface is:

| UDF | Wraps | Notes |
|---|---|---|
| `h3x_latlng_to_cell(lat, lng, res)` | `latlng_to_cell` | Returns hex string |
| `h3x_cell_to_latlng(cell)` | `cell_to_latlng` | Returns struct of `(lat, lng)` |
| `h3x_get_resolution(cell)` | `get_resolution` | Returns int |
| `h3x_cell_area_m2(cell)` | `cell_area(cell, "m^2")` | Returns double |
| `h3x_cell_to_parent(cell, parent_res)` | `cell_to_parent` | Returns hex string |
| `h3x_cell_to_children(cell, child_res)` | `cell_to_children` | Returns array<string> |
| `h3x_grid_disk(cell, k)` | `grid_disk` | Returns array<string> |
| `h3x_grid_distance(a, b)` | `grid_distance` | Returns long |
| `h3x_grid_path_cells(a, b)` | `grid_path_cells` | Returns array<string> |
| `h3x_to_res15(cell)` | `cell_to_parent(cell, 15)` | Convenience for partition column |
| `h3x_to_legacy_64bit(cell)` | `to_64bit` | For interop with stock H3 columns |

Plus a `register_all_udfs(spark)` helper that wires them into the
Spark SQL catalog so they're callable from `%sql` cells.

**Critical NULL handling:** every UDF must return None on NULL inputs
(don't crash). Spark passes None for null cells; the underlying
`h3_extended` will raise `H3Error` on a None cell hex, so wrap each
UDF body in a None-check.

Skeleton:

```python
# h3_extended_spark.py
from typing import Optional, List
from pyspark.sql import SparkSession
from pyspark.sql.functions import udf
from pyspark.sql.types import (
    StringType, DoubleType, LongType, IntegerType, ArrayType, StructType, StructField
)

import h3_extended as h3x

LATLNG_SCHEMA = StructType([
    StructField("lat", DoubleType(), False),
    StructField("lng", DoubleType(), False),
])

@udf(StringType())
def h3x_latlng_to_cell(lat: Optional[float], lng: Optional[float],
                       res: Optional[int]) -> Optional[str]:
    if lat is None or lng is None or res is None:
        return None
    try:
        return h3x.latlng_to_cell(lat, lng, res)
    except h3x.H3Error:
        return None

@udf(LATLNG_SCHEMA)
def h3x_cell_to_latlng(cell: Optional[str]):
    if cell is None:
        return None
    try:
        lat, lng = h3x.cell_to_latlng(cell)
        return (lat, lng)
    except h3x.H3Error:
        return None

# ... 9 more UDFs follow the same pattern

def register_all_udfs(spark: SparkSession) -> None:
    spark.udf.register("h3x_latlng_to_cell", h3x_latlng_to_cell)
    spark.udf.register("h3x_cell_to_latlng", h3x_cell_to_latlng)
    # ... register all 11
```

### Step 2 — Build the dispatch (`h3_auto_*`) layer

The dispatch SQL functions transparently route to native (Photon-
accelerated) H3 for res 0-15 and to `h3_extended` UDFs for res 16-22.
This is critical: the slowdown of `h3_extended` (~3-5× vs native) is
acceptable for the few queries that need ext resolution, but every
res 0-15 query MUST hit Photon for cluster economics.

Pattern:

```sql
-- h3_auto_latlng_to_cell.sql
-- IMPORTANT: Photon's h3_longlat_as_cell signature is (lng, lat, res)
-- — note the lng/lat swap vs h3_extended's (lat, lng, res). Document
-- this discrepancy in the function body comment.
CREATE OR REPLACE FUNCTION h3_auto_latlng_to_cell(
    lat DOUBLE, lng DOUBLE, res INT
) RETURNS STRING
RETURN
  CASE
    WHEN res <= 15 THEN
      -- Photon-native: takes (lng, lat, res), returns BIGINT, hex-stringify
      lower(hex(h3_longlat_as_cell(lng, lat, res)))
    ELSE
      h3x_latlng_to_cell(lat, lng, res)  -- ext-aware UDF
  END;
```

Repeat for `h3_auto_cell_to_latlng`, `h3_auto_get_resolution`,
`h3_auto_cell_area`, `h3_auto_cell_to_parent`, etc. — see playbook
§12c.

### Step 3 — Upload wheel to Unity Catalog Volume

Standard pattern for Databricks-hosted Python wheels:

```python
# In a Databricks notebook on a cluster with Unity Catalog access
dbutils.fs.cp(
    "file:/local/path/h3_extended-0.3.1-py3-none-linux_x86_64.whl",
    "/Volumes/amalfi/libraries/h3_extended-0.3.1-py3-none-linux_x86_64.whl"
)
```

Or upload via the UI: Catalog → `amalfi.libraries` Volume → Upload.

**Cluster install options** — pick one:

**Option A: Cluster init script** (best for production clusters):

```bash
#!/bin/bash
# /Volumes/amalfi/init/install_h3_extended.sh
/databricks/python/bin/pip install \
    /Volumes/amalfi/libraries/h3_extended-0.3.1-py3-none-linux_x86_64.whl
```

Configure cluster: Advanced Options → Init Scripts → Volume path.

**Option B: Notebook-scoped install** (best for development):

```python
%pip install /Volumes/amalfi/libraries/h3_extended-0.3.1-py3-none-linux_x86_64.whl
dbutils.library.restartPython()
```

### Step 4 — First live queries

Verify on the three canonical test points:

```sql
SELECT
    h3x_latlng_to_cell(40.33,  -73.99,  19) AS monmouth_res19,
    h3x_latlng_to_cell(26.71,  -80.05,  20) AS palm_beach_res20,
    h3x_latlng_to_cell(40.63,   14.40,  19) AS sorrento_res19;
```

Expected (from local validation, must match byte-for-byte):
- monmouth_res19 → 32-char hex
- All cells valid via `h3x_is_valid_cell` (which we don't have a UDF
  for yet — add `h3x_is_valid_cell` to the UDF list in Step 1)
- `h3x_get_resolution(monmouth_res19)` returns 19

Test the dispatch:

```sql
-- res 13 → Photon native path
SELECT h3_auto_latlng_to_cell(40.33, -73.99, 13);

-- res 19 → ext UDF path
SELECT h3_auto_latlng_to_cell(40.33, -73.99, 19);
```

### Step 5 — Bulk performance test

```python
from pyspark.sql.functions import expr
import h3_extended_spark as h3xs

h3xs.register_all_udfs(spark)

df = spark.range(100_000).select(
    (40.10 + (col("id") % 1000) / 1000.0 * 0.4).alias("lat"),
    (-74.30 + (col("id") % 997)  / 997.0  * 0.3).alias("lng"),
)
df_indexed = df.withColumn(
    "cell19", expr("h3x_latlng_to_cell(lat, lng, 19)")
)
df_indexed.write.mode("overwrite").saveAsTable("amalfi.test.indexed_100k")
```

Document: rows/sec, executor memory, any serialization issues.
Compare against Session 8 baseline (390K ops/sec single-thread).
Expect 2-3× per executor core after JVM↔Python overhead, scaled by
core count.

### Exit criteria (Session 9)
- All UDFs register and execute on Databricks
- Live query results match local validation byte-for-byte
- Dispatch correctly routes stock vs extended
- Bulk performance documented
- Wheel installed via init script (production-ready)
- **Tag: `v0.4.0-databricks-udf`**

---

## Session 10 — Delta Lake Schemas + Pipeline Skeleton

**Goal:** Bronze, Silver, Gold Delta tables wired together with the
H3 multi-resolution columns; Auto-Loader for S3 ingestion.

**Prerequisites:** v0.4.0 tagged, UDFs working on cluster.

### Step 1 — Create the three tables

Schemas from playbook §13:

**Bronze — `bronze.drone_flights`**: raw flight metadata. Partitioned
by `asset_type, asset_id`. Columns: flight_id, capture_date,
capture_type, source_file, gps_lat, gps_lng, gps_alt_m, gps_accuracy_m,
duration_sec, manifest_json.

**Silver — `silver.georegistered_frames`**: per-frame imagery with
multi-resolution H3 columns (res 15, 19, 20). Partitioned by
`asset_type, asset_id, h3_res15`, Z-ORDERed by `h3_res19,
frame_timestamp`. Columns: frame_id, flight_id, frame_timestamp,
gps_lat, gps_lng, gps_alt_m, h3_res15, h3_res19, h3_res20, image_path,
image_bytes (or path to S3 object), avg_rgb (struct).

**Gold — `gold.hex_health_timeline`**: per-hex aggregated metrics over
time. Partitioned by `asset_type, asset_id, h3_res15_parent`,
Z-ORDERed by `h3_cell, observation_date`. Columns: h3_cell,
observation_date, asset_id, avg_health_score, health_delta_7d,
health_delta_30d, trend, anomaly_flag, mlflow_run_id (traceability).

```sql
CREATE TABLE bronze.drone_flights (
    flight_id        STRING,
    asset_type       STRING,
    asset_id         STRING,
    capture_date     DATE,
    capture_type     STRING,
    source_file      STRING,
    gps_lat          DOUBLE,
    gps_lng          DOUBLE,
    gps_alt_m        DOUBLE,
    gps_accuracy_m   DOUBLE,
    duration_sec     DOUBLE,
    manifest_json    STRING
)
PARTITIONED BY (asset_type, asset_id);
```

(Silver and Gold similarly — see playbook §13 for full DDL.)

### Step 2 — Build Silver transform notebook

```python
bronze_df = spark.read.table("bronze.drone_flights")

silver_df = bronze_df.select(
    "flight_id", "asset_type", "asset_id",
    expr("h3x_latlng_to_cell(gps_lat, gps_lng, 15)").alias("h3_res15"),
    expr("h3x_latlng_to_cell(gps_lat, gps_lng, 19)").alias("h3_res19"),
    expr("h3x_latlng_to_cell(gps_lat, gps_lng, 20)").alias("h3_res20"),
    "*",  # plus all bronze columns
)

silver_df.write.mode("append").saveAsTable("silver.georegistered_frames")
```

### Step 3 — Build Gold transform notebook

Per-hex aggregation with regression metrics. Health score is a
placeholder NDVI-like calculation from RGB at this stage; ML training
comes later.

### Step 4 — Test with synthetic data

Generate 10K synthetic flight records spanning Monmouth County, push
through Bronze → Silver → Gold, verify partition pruning (`EXPLAIN`)
and Z-ORDER optimization runs.

### Step 5 — Configure Auto-Loader for S3

```python
spark.readStream.format("cloudFiles") \
    .option("cloudFiles.format", "parquet") \
    .option("cloudFiles.schemaLocation", "/Volumes/amalfi/schemas/bronze") \
    .load("s3://amalfi-flights-raw/") \
    .writeStream \
    .option("checkpointLocation", "/Volumes/amalfi/checkpoints/bronze") \
    .toTable("bronze.drone_flights")
```

### Exit criteria (Session 10)
- Three Delta tables created with correct schemas + properties
- Bronze → Silver → Gold transform chain executes cleanly
- Partition pruning confirmed via EXPLAIN
- Auto-Loader ingests from S3
- **Tag: `v0.5.0-delta-pipeline`**

---

## Session 11 — First Real Flight Data

**Goal:** Fly the Monmouth Beach property, push real imagery through
the complete pipeline, see the digital twin at sub-centimeter
resolution.

**Prerequisites:** Part 107 certification, drone purchased (DJI Mavic
3 Multispectral per the hardware reference), v0.5.0 tagged, pipeline
operational.

**Steps:**
1. Fly the Monmouth Beach property
2. Upload imagery to S3
3. Auto-Loader ingests to Bronze
4. Silver transform indexes at res 15, 19, 20
5. Gold transform computes per-hex health scores
6. Query the digital twin at sub-centimeter resolution

**Exit criteria:**
- Real drone imagery flowing through the complete pipeline
- Property has a digital twin at resolution 19-20
- Health scores computed per hex
- Foundation for ML model training established
- **Tag: `v1.0.0-first-flight`**

---

## Pre-Session-9 Sanity Checklist

Before starting any of this in another repo, confirm:

- [x] **CI green** — fixed at commit `fbd8d04a` (2026-05-07). Both
      matrix legs go green on every push to `feat/h3-128-mvp`
      (Linux ~43 s, macOS ~31 s). See "Resolved CI bugs (history)"
      below if you hit a regression.

- [ ] Latest Linux x86_64 wheel pulled. Run:
      ```bash
      set -a && source /Users/johndesposito/amalfi_work/amalfi-h3-128/.env && set +a
      GH_TOKEN="$GITHUB_PAT" gh run list -R devopulence/amalfi-h3-128 \
          --workflow python-wheel.yml --limit 5
      GH_TOKEN="$GITHUB_PAT" gh run download <run-id> \
          -R devopulence/amalfi-h3-128 \
          -n h3_extended-ubuntu-latest-x86_64-py311 \
          -D /tmp/wheels
      ```
      Wheel is ~94 KiB.

- [ ] Wheel passes pytest in a Linux x86_64 venv (the CI workflow
      already does this — `pytest --pyargs h3_extended.tests` from
      `/tmp` outside the source tree, 203/203 PASS).

- [ ] You have a Databricks workspace with:
      - Unity Catalog enabled
      - A target catalog (e.g. `amalfi`) with WRITE permission
      - A Volume for libraries (e.g. `amalfi.libraries`)
      - A cluster running DBR 14.3+ (Photon enabled, Python 3.11+)

- [ ] You can connect via the Databricks CLI or the Databricks
      VS Code extension.

- [ ] Stock h3-py is available on the cluster (DBR ships with it; if
      not, `%pip install h3>=4.0`). The dispatch layer needs both.

- [ ] **Critical:** confirm Photon's `h3_longlat_as_cell` argument
      order on your DBR version. It's `(lng, lat, res)` historically
      but can change. The lat/lng swap vs `h3_extended.latlng_to_cell`
      is the most common bug — always log the values explicitly when
      first wiring up the dispatch.

---

## Resolved CI bugs (history — `fbd8d04a`)

The `python-wheel.yml` workflow shipped in `b77d7cf7` had two bugs
that kept both matrix legs red. A destination-session pass fixed
both at commit `fbd8d04a` (2026-05-07). Documented here so a future
regression in the same area is recognized fast:

### Bug 1 — `find` glob did not match versioned dylib

Original (broken):
```yaml
src="$(find build-release/lib -maxdepth 1 -name '${{ matrix.lib }}*' -not -type l | head -1)"
```

`matrix.lib=libh3.dylib` produces glob `libh3.dylib*` which does NOT
match the versioned real file `libh3.1.dylib` (the `.1` is infixed,
not suffixed). `-not -type l` excludes the symlink `libh3.dylib`,
so `src` ends up empty.

Resolution (live in `python-wheel.yml`):
```yaml
src="$(find build-release/lib -maxdepth 1 -type f \
    \( -name 'libh3.[0-9]*.dylib' -o -name 'libh3.so.[0-9]*' \) | head -1)"
```

The `-type f` filter naturally excludes symlinks (cleaner than
`-not -type l`); the explicit numeric-version patterns target the
real files on both platforms.

### Bug 2 — `grep` regex anchor never matched the zipfile listing

Original (broken):
```yaml
python -m zipfile -l "$wheel" | grep -E '\.(so|dylib)$'
```

`python -m zipfile -l` outputs `Name  Modified  Size` per line, so
the line ends in a digit (the size), not `.so`/`.dylib`. `$` never
matches.

Resolution (live in `python-wheel.yml`):
```yaml
python -m zipfile -l "$wheel" | awk '{print $1}' | grep -E '\.(so|dylib)$'
```

Extract column 1 first, then anchor the suffix match.

### If both legs go red again

Re-read both fixes in `.github/workflows/python-wheel.yml`. Most
likely cause of regression: cmake started producing a different
versioned filename pattern, or the zipfile listing format changed in
a Python upgrade.

---

## Common Gotchas (forward-looking warnings)

1. **The wheel must be Linux x86_64** for Databricks. macOS arm64
   wheel will not install on the cluster (will get
   `is not a supported wheel on this platform`).

2. **PEP 600 manylinux tag.** The current CI builds with `pip` default
   wheel tagging, which produces `linux_x86_64` (not `manylinux_2_x_x86_64`).
   This is fine for Databricks (which uses a known glibc), but if the
   wheel is ever shipped to PyPI it'll need `auditwheel repair` to
   convert to manylinux.

3. **The shim's `cell_area` takes a unit code** (0/1/2), not a
   string. The Python wrapper does the string→int mapping. Don't
   reach into the shim from Spark UDFs directly — go through
   `h3_extended.cell_area`.

4. **`to_64bit` raises on ext-res cells.** If a UDF column might
   contain mixed res 0-15 and res 16-22 cells, wrap `to_64bit` in a
   try/except (return None for ext cells).

5. **The Python loop in batch.py is the bottleneck for high-volume
   ingest.** A 1M-row Silver transform calling `h3x_latlng_to_cell`
   will take ~2.5 seconds per executor core (390K ops/sec). For
   1B-row backfills consider:
   - Running the Spark job with high parallelism (each core gets a
     manageable shard)
   - Or building a vectorized cffi path (a future optimization, not
     blocking)

6. **macOS arm64 + Apple libm gave 0 rad delta on round-trips.**
   Linux x86_64 + glibc may give ULP-level differences. The bit
   identity at res 19/20 round-trip should still hold (tested in C
   ctest under Linux gcc), but if a precision test fails on Linux at
   the 1e-15 level, that's libm divergence, not a fork bug. Use the
   `latlng_within_tolerance` helper instead of `==`.

7. **Pentagon neighborhoods.** Cells near pentagon vertices (12 of
   them globally, base cells 4, 14, 24, 38, 49, 58, 63, 72, 83, 97,
   107, 117) have only 5 neighbors instead of 6, and `gridDisk(k)`
   returns 5k+1 cells instead of 7k+1. Sandy Hook is on base cell 17
   (interior of a hexagon, so this never bites). When pipeline data
   includes points near pentagons, expect different cell counts and
   handle them gracefully.

---

## Where the dispatch layer sits architecturally

```
                User SQL / DataFrame query
                            │
                            ▼
           h3_auto_*  (CASE WHEN res <= 15 ... ELSE)
              ╱                          ╲
             ╱                            ╲
   res 0-15  ▼                              ▼  res 16-22
  Native Photon                       PySpark UDF (Python)
  h3_longlat_as_cell                  h3x_latlng_to_cell
  (BIGINT result,                     (STRING result,
   1-2M ops/sec/core)                  ~390K ops/sec/core)
   │                                    │
   └─────── lower(hex(...))             │
              │                          │
              ▼                          ▼
              All paths return STRING (lowercase hex)
              for downstream consistency.
```

This is the design that lets Photon be fast where it can and the cffi
shim be correct where Photon can't.

---

## Reference: artifact lookups for Session 9-11

```bash
# Source repo (the fork)
cd /Users/johndesposito/amalfi_work/amalfi-h3-128

# Latest tag for the C library
git tag -l 'v0.*' --sort=creatordate | tail -1
# → v0.3.1-py3-tag (active); v0.3.0-python-bindings (historical)

# Latest commit
git rev-parse HEAD
# → 3ee39bc7 (as of 2026-05-05)

# Local wheel (macOS arm64)
ls dist/
# → h3_extended-0.3.1-py3-none-macosx_*_arm64.whl

# Pull Linux x86_64 wheel from CI
set -a && source .env && set +a
GH_TOKEN="$GITHUB_PAT" gh run download <latest-run-id> \
    -R devopulence/amalfi-h3-128 \
    -n h3_extended-ubuntu-latest-x86_64-py311 \
    -D /tmp/wheels
ls /tmp/wheels/
# → h3_extended-0.3.1-py3-none-linux_x86_64.whl

# Smoke test (after install in Linux venv)
python -c "
import h3_extended as h3x, h3
ext = h3x.latlng_to_cell(40.33, -73.99, 9)
stock = h3.latlng_to_cell(40.33, -73.99, 9)
stock_int = int(stock, 16) if isinstance(stock, str) else stock
assert h3x.to_64bit(ext) == stock_int
print('OK — h3_extended works on this platform')
"
```
