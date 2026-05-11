# Python Package — `h3_extended`

cffi ABI-mode bindings for the H3-Extended C library. Sits alongside
stock `h3-py` (renamed lib + `h3_ext_`-prefixed symbols → no
collision; both can be installed and imported in the same process).

Version: **0.3.1** (tag `v0.3.1-py3-tag`, packaging fix on top of `v0.3.0-python-bindings` `b77d7cf7`).

## Package structure

```
h3_extended/
├── __init__.py                  # Public API (16 functions) + H3Error class
├── _ffi.py                      # cffi ABI bindings + dlopen + ABI consistency check
├── batch.py                     # 3 batch helpers (Python loops, Pandas-UDF-ready)
├── libh3_extended.{dylib,so}    # Bundled at wheel-build time (NOT in git)
└── tests/
    ├── __init__.py
    ├── test_basic.py            # res 0-15 fundamentals (23)
    ├── test_extended_res.py     # res 16-22 (23)
    ├── test_hierarchy.py        # cross stock-ext boundary (10)
    ├── test_backward_compat.py  # vs h3-py 4.4.2 (80)
    ├── test_precision.py        # round-trip closure 3 geographies (18)
    ├── test_grid_ops.py         # gridDisk/Distance/PathCells, local_ij (16)
    └── test_string.py           # 16-char + 32-char hex round-trip (33)
```

Plus at the project root:
- `pyproject.toml` — setuptools backend, cffi runtime dep, pytest+h3
  test deps
- `setup.py` — `BinaryDistribution` override forcing platform-specific
  wheel tag (the package bundles a native lib so the wheel is NOT pure)

## Public API — 16 functions

All functions accept and return `H3Index` cells as **hex strings**
(stock h3-py compatible at res 0-15: 1-16-char lowercase hex; ext res
16-22: 32-char zero-padded). lat/lng are in **degrees** at the Python
boundary; the cffi layer converts to radians for the C shim.

```python
import h3_extended as h3x
```

### Indexing
```python
h3x.latlng_to_cell(lat: float, lng: float, resolution: int) -> str
h3x.cell_to_latlng(cell_hex: str) -> tuple[float, float]
h3x.cell_to_boundary(cell_hex: str) -> list[tuple[float, float]]
```

### Resolution
```python
h3x.get_resolution(cell_hex: str) -> int           # 0..22
h3x.get_effective_resolution(cell_hex: str) -> int # alias of get_resolution
h3x.cell_area(cell_hex: str, unit: str = "m^2") -> float
    # unit: "m^2" | "km^2" | "rads^2"
```

### Hierarchy
```python
h3x.cell_to_parent(cell_hex: str, parent_res: int) -> str
h3x.cell_to_children(cell_hex: str, child_res: int) -> list[str]
```

### Grid
```python
h3x.grid_disk(cell_hex: str, k: int) -> list[str]
h3x.grid_distance(cell_a: str, cell_b: str) -> int
h3x.grid_path_cells(cell_a: str, cell_b: str) -> list[str]
```

### Local IJ (single-frame coordinates around an origin cell)
```python
h3x.cell_to_local_ij(origin: str, cell: str, mode: int = 0) -> tuple[int, int]
h3x.local_ij_to_cell(origin: str, i: int, j: int, mode: int = 0) -> str
```

### Validation
```python
h3x.is_valid_cell(cell_hex: str) -> bool
```

### Stock-h3 interop (res 0-15 only)
```python
h3x.to_64bit(cell_hex: str) -> int
    # Returns the stock-h3 64-bit integer for any res 0-15 cell.
    # Raises H3Error(E_RES_DOMAIN) for res 16+ (high 64 bits non-zero).

h3x.from_64bit(legacy_int: int) -> str
    # Construct a 128-bit cell hex string from a stock-h3 64-bit int.
    # Useful for reading legacy data and feeding it into ext-aware code.
```

### Errors

```python
class H3Error(Exception):
    code: int    # H3 error code (E_FAILED=1, E_DOMAIN=2, ...)
    # message includes the function name where the error originated
```

The H3 error codes mirror `h3api.h::H3ErrorCodes`:

| Code | Constant | Meaning |
|---:|---|---|
| 0 | E_SUCCESS | (not raised) |
| 1 | E_FAILED | Generic failure / NULL pointer |
| 2 | E_DOMAIN | Argument out of domain |
| 3 | E_LATLNG_DOMAIN | lat/lng was NaN |
| 4 | E_RES_DOMAIN | resolution out of [0, 22] |
| 5 | E_CELL_INVALID | Not a valid H3 cell |
| 9 | E_PENTAGON | Pentagon distortion (path/distance failure) |
| 11 | E_NOT_NEIGHBORS | gridPath endpoints not in same icosahedron face |
| 12 | E_RES_MISMATCH | parent_res > cell_res, etc. |
| 15 | E_OPTION_INVALID | Bad mode / unit code |

## Batch helpers (`h3_extended.batch`)

```python
from h3_extended.batch import (
    batch_latlng_to_cell, batch_cell_to_parent, batch_get_resolution
)

cells = batch_latlng_to_cell([40.33, 26.71], [-73.99, -80.05], 19)
parents = batch_cell_to_parent(cells, parent_res=15)
resolutions = batch_get_resolution(cells)
```

Currently Python loops calling the per-call API. Structured for
drop-in Pandas UDF / vectorized upgrade later — the public contract is
stable, only the body changes.

## Installation

### From wheel (recommended)

```bash
pip install h3_extended-0.3.1-py3-none-<platform>.whl
```

Wheel filename pattern (since v0.3.1 — retagged from `cp311-cp311` so any
Python 3.x with cffi can install, including Databricks serverless 3.12):
- macOS arm64: `h3_extended-0.3.1-py3-none-macosx_*_arm64.whl`
- Linux x86_64: `h3_extended-0.3.1-py3-none-linux_x86_64.whl`

Wheels are produced by `.github/workflows/python-wheel.yml` on every
push and uploaded as 30-day artifacts. Download with `gh run
download` (see `SKILL.md` Quick Start).

### From source

```bash
cd /path/to/amalfi-h3-128
mkdir -p build-release && cd build-release
cmake .. -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
         -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_SHARED_LIBS=ON \
         -DBUILD_TESTING=OFF
cmake --build . -j --target h3
cp lib/libh3.dylib ../h3_extended/libh3_extended.dylib  # macOS
# or:
# cp lib/libh3.so ../h3_extended/libh3_extended.so       # Linux
cd ..
pip install -e .
```

The `pip install -e .` (editable install) is good for development.
The library is found via `Path(__file__).parent / "libh3_extended.*"`.

### Library resolution order

`h3_extended/_ffi.py` resolves the bundled lib in this order:

1. `H3_EXTENDED_LIB` environment variable (absolute path) — useful
   when developing the C library and want to point Python at a
   freshly built `build-release/lib/libh3.dylib` without recopying.
2. `<package_dir>/libh3_extended.{dylib,so}` — the wheel-bundled
   default.

If neither is found, import fails with a clear error.

### ABI consistency check at import

`_ffi.py` calls `h3_ext_sizeof_h3index()` and
`h3_ext_alignof_h3index()` at import time. Both must return 16. If
the wrong (stock 64-bit) libh3 is somehow loaded, import fails fast
with:

```
RuntimeError: libh3_extended sizeof(H3Index) = 8, expected 16. Wrong
library loaded?
```

## Performance baselines (Session 8 measurement)

cffi ABI mode, macOS arm64, single-thread, no batching:

| Operation | ops/sec |
|---|---:|
| `latlng_to_cell(res=19)` | 390,918 |
| `cell_to_parent(res19→15)` | 427,932 |
| `grid_disk(k=3, res=19)` | 15,343 |

These are the per-row UDF baselines for Databricks comparison
(Session 9). Stock h3-py is typically 1-2M ops/sec for the same calls
(no cffi overhead) — the ~3-5× slowdown matches the playbook
expectation of "2-5× acceptable for ext-aware code paths."

For batch workloads the relevant baseline is the
`batch_latlng_to_cell` loop, which has the same per-call cost (Python
loop). When/if the batch helpers move to a Pandas UDF or a vectorized
cffi path, this is the reference number to beat.

## Test coverage

```
pytest h3_extended/tests/ -v
# 203 passed in 0.17s
```

| Module | Tests | What it covers |
|---|---:|---|
| test_basic | 23 | res 0-15 indexing, parent/child, validation, area monotonicity |
| test_extended_res | 23 | res 16-22 creation, 32-char string contract, sub-meter at res 20 |
| test_hierarchy | 10 | Cross stock-ext boundary, full descent res 0 → 22 |
| test_backward_compat | 80 | bit-identical match vs h3-py 4.4.2 (latlng_to_cell, cell_to_latlng, cell_area, to_64bit/from_64bit) at 3 geographies × {0, 5, 9, 12, 15} |
| test_precision | 18 | Round-trip closure at Monmouth/Palm Beach/Sorrento, res 19-22 idempotency |
| test_grid_ops | 16 | grid_disk(k=0,1,2), grid_distance, grid_path_cells, local_ij round-trip |
| test_string | 33 | Stock 1-16 char + ext 32-char round-trip via Python and FFI |

The `test_backward_compat` module is gated by `pytest.importorskip("h3")`
— it skips if stock h3-py is not installed in the test venv. CI
installs it explicitly to exercise this gate.

## Known test calibrations

Two non-obvious tolerance choices documented in `SESSION_7_CHECKPOINT.md`:

### 1. `cell_area` BC tolerance: 1e-12 → 1e-7

Stock h3-py and h3_extended both call the same `cellAreaM2` routines;
the post-int64 CoordIJK kernels reorder some FP operations differently
than stock, producing a ~3.6e-9 relative difference (last few bits of
the mantissa). This is **double-precision noise, not a regression**.
The 1e-7 threshold catches structural drift while accommodating the
reordering.

### 2. Hierarchy tests anchor at parent center, not raw lat/lng

`(40.33, -73.99)` (Sandy Hook test point) sits within ε of a res-15
cell boundary. `latlng_to_cell(40.33, -73.99, 15)` and
`cell_to_parent(latlng_to_cell(40.33, -73.99, 16), 15)` legitimately
disagree in the last hex digit (`8f2a13902a5e2aa` vs `…ab`) — the
projection rounding picks different sides at different child
resolutions. The fix is to anchor descent at the parent cell's
**center** (always interior to the parent at all coarser resolutions),
which is the test pattern now used.

The same phenomenon was reproduced at the C level in Session 8's
validation: 7.22% of random Monmouth-bbox coords land on a res-19
edge with the same disagreement pattern. It's a geometric edge case,
not a fork bug.

## Coexistence with stock h3-py

```python
import h3 as stock
import h3_extended as h3x

# Both work in the same process
stock_cell = stock.latlng_to_cell(40.33, -73.99, 9)   # int or hex string
ext_cell = h3x.latlng_to_cell(40.33, -73.99, 9)        # hex string

# Round-trip equivalence at res 0-15
stock_int = int(stock_cell, 16) if isinstance(stock_cell, str) else stock_cell
assert h3x.to_64bit(ext_cell) == stock_int
assert h3x.from_64bit(stock_int) == ext_cell  # except for surface canonicalization
```

The dispatch pattern for Databricks (Session 9) uses this:

```python
def auto_latlng_to_cell(lat, lng, res):
    if res <= 15:
        return stock.latlng_to_cell(lat, lng, res)   # Photon-accelerated
    return h3x.latlng_to_cell(lat, lng, res)         # ext-aware
```

## What's NOT in the API yet

Functions that exist in stock h3-py but NOT in h3_extended (because
the underlying C function is in the [c-library.md] post-MVP deferral
list):

- `cells_to_directed_edge`, `directed_edge_to_cells`,
  `directed_edge_to_boundary`, `origin_to_directed_edges`,
  `cell_to_edge_length` — all the **edge** functions
- `cell_to_vertex`, `cell_to_vertexes`, `vertex_to_latlng` — all the
  **vertex** functions
- `compact_cells`, `uncompact_cells`
- `polygon_to_cells` / `cells_to_polygons` (polyfill)
- `get_hexagon_area_avg_*`, `get_hexagon_edge_length_avg_*`,
  `get_num_cells`

When the corresponding C function is widened, adding the Python
binding is typically 1-2 lines in `_ffi.py` (cdef) and 5-10 lines in
`__init__.py` (Python wrapper).
