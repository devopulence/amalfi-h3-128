"""cffi ABI-mode bindings for the H3-Extended (128-bit) C library.

The H3Index type is __uint128_t in C, which cffi cannot bind portably.
POC-4 (the FFI ABI proof) established the by-pointer convention: every
H3Index parameter passes through the shim as a pointer to a 16-byte
buffer (uint8_t[16]). The C shim dereferences and writes the buffer in
native byte order; Python treats it as opaque bytes. Hex-string
conversion always goes through h3_ext_h3_to_string / h3_ext_string_to_h3
so Python never has to decode the layout.

The shared library is bundled inside the package and renamed to
libh3_extended (avoiding collision with stock h3-py if both are
installed).
"""

from __future__ import annotations

import os
import platform
import sys
from pathlib import Path

from cffi import FFI

ffi = FFI()

# Type, struct, and shim function declarations. Mirrors h3ExtShim.h, with
# H3Index expressed as uint8_t* (16-byte buffers) per POC-4 convention.
ffi.cdef(
    """
    typedef uint32_t H3Error;

    typedef struct {
        double lat;
        double lng;
    } LatLng;

    typedef struct {
        int numVerts;
        LatLng verts[10];   /* MAX_CELL_BNDRY_VERTS */
    } CellBoundary;

    typedef struct {
        int i;
        int j;
    } CoordIJ;

    /* Original 10 shims. */
    H3Error h3_ext_lat_lng_to_cell(double lat, double lng, int res,
                                   uint8_t *out);
    H3Error h3_ext_cell_to_lat_lng(const uint8_t *cell, double *lat,
                                   double *lng);
    H3Error h3_ext_cell_to_parent(const uint8_t *cell, int parentRes,
                                  uint8_t *out);
    H3Error h3_ext_cell_to_children(const uint8_t *cell, int childRes,
                                    uint8_t *children, int64_t *count);
    H3Error h3_ext_cell_to_children_size(const uint8_t *cell, int childRes,
                                         int64_t *out);
    H3Error h3_ext_is_valid_cell(const uint8_t *cell, int *out);
    H3Error h3_ext_get_resolution(const uint8_t *cell, int *out);
    H3Error h3_ext_grid_distance(const uint8_t *a, const uint8_t *b,
                                 int64_t *out);
    H3Error h3_ext_h3_to_string(const uint8_t *cell, char *out, size_t sz);
    H3Error h3_ext_string_to_h3(const char *str, uint8_t *out);

    /* Session 7 follow-on shims. */
    H3Error h3_ext_cell_to_boundary(const uint8_t *cell, CellBoundary *out);
    H3Error h3_ext_cell_area(const uint8_t *cell, int unit, double *out);
    H3Error h3_ext_max_grid_disk_size(int k, int64_t *out);
    H3Error h3_ext_grid_disk(const uint8_t *origin, int k, uint8_t *out);
    H3Error h3_ext_grid_path_cells_size(const uint8_t *start,
                                        const uint8_t *end, int64_t *out);
    H3Error h3_ext_grid_path_cells(const uint8_t *start, const uint8_t *end,
                                   uint8_t *out);
    H3Error h3_ext_cell_to_local_ij(const uint8_t *origin, const uint8_t *cell,
                                    uint32_t mode, CoordIJ *out);
    H3Error h3_ext_local_ij_to_cell(const uint8_t *origin, const CoordIJ *ij,
                                    uint32_t mode, uint8_t *out);

    /* Cross-TU consistency probes. */
    size_t h3_ext_sizeof_h3index(void);
    size_t h3_ext_alignof_h3index(void);
    """
)

H3_INDEX_BYTES = 16  # sizeof(__uint128_t)
MAX_CELL_BNDRY_VERTS = 10

H3_EXT_AREA_M2 = 0
H3_EXT_AREA_KM2 = 1
H3_EXT_AREA_RADS2 = 2

# H3 error codes that the public API exposes by name. Full list lives in
# h3api.h::H3ErrorCodes.
E_SUCCESS = 0
E_FAILED = 1
E_DOMAIN = 2
E_LATLNG_DOMAIN = 3
E_RES_DOMAIN = 4
E_CELL_INVALID = 5
E_DIR_EDGE_INVALID = 6
E_UNDIR_EDGE_INVALID = 7
E_VERTEX_INVALID = 8
E_PENTAGON = 9
E_DUPLICATE_INPUT = 10
E_NOT_NEIGHBORS = 11
E_RES_MISMATCH = 12
E_MEMORY_ALLOC = 13
E_MEMORY_BOUNDS = 14
E_OPTION_INVALID = 15


def _library_filename() -> str:
    system = platform.system()
    if system == "Darwin":
        return "libh3_extended.dylib"
    if system == "Linux":
        return "libh3_extended.so"
    raise RuntimeError(
        f"h3_extended does not support {system}; macOS arm64 and "
        "Linux x86_64 are the supported platforms."
    )


def _resolve_library_path() -> Path:
    """Locate the bundled shared library.

    Search order:
      1. The package directory (where the wheel installs it).
      2. H3_EXTENDED_LIB env override (development convenience).
    """
    env = os.environ.get("H3_EXTENDED_LIB")
    if env:
        path = Path(env)
        if path.exists():
            return path
        raise FileNotFoundError(
            f"H3_EXTENDED_LIB={env} does not exist."
        )

    pkg_dir = Path(__file__).resolve().parent
    bundled = pkg_dir / _library_filename()
    if bundled.exists():
        return bundled

    raise FileNotFoundError(
        f"Could not find {_library_filename()} in {pkg_dir}. "
        "Ensure the wheel was built with the bundled shared library, or "
        "set H3_EXTENDED_LIB to point at a libh3_extended build."
    )


lib = ffi.dlopen(str(_resolve_library_path()))


def assert_abi_consistent() -> None:
    """Confirm the loaded library reports sizeof/alignof H3Index == 16.

    Mirrors POC-4 D7-G2 — guards against accidentally loading a 64-bit
    stock libh3 in place of libh3_extended.
    """
    sz = lib.h3_ext_sizeof_h3index()
    al = lib.h3_ext_alignof_h3index()
    if sz != H3_INDEX_BYTES:
        raise RuntimeError(
            f"libh3_extended sizeof(H3Index) = {sz}, expected "
            f"{H3_INDEX_BYTES}. Wrong library loaded?"
        )
    if al != H3_INDEX_BYTES:
        raise RuntimeError(
            f"libh3_extended alignof(H3Index) = {al}, expected "
            f"{H3_INDEX_BYTES}."
        )


assert_abi_consistent()
