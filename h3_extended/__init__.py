"""h3_extended — Python bindings for the H3-Extended (128-bit) C library.

Public API for resolutions 0–22. Resolutions 0–15 are bit-identical to
stock H3 (interoperable with h3-py via to_64bit / from_64bit).
Resolutions 16–22 are the H3-Extended range — sub-centimeter precision,
serialized as 32-character hex strings.

Coordinates are accepted and returned in **degrees** at the Python
boundary (matching h3-py); the C shim layer takes radians.
"""

from __future__ import annotations

import math
from typing import List, Tuple

from . import _ffi
from ._ffi import ffi, lib

__version__ = "0.3.1"

__all__ = [
    "H3Error",
    "latlng_to_cell",
    "cell_to_latlng",
    "cell_to_parent",
    "cell_to_children",
    "cell_to_boundary",
    "get_resolution",
    "get_effective_resolution",
    "cell_area",
    "grid_disk",
    "grid_distance",
    "grid_path_cells",
    "is_valid_cell",
    "cell_to_local_ij",
    "local_ij_to_cell",
    "to_64bit",
    "from_64bit",
]


class H3Error(Exception):
    """Raised when the underlying C library returns a non-zero H3Error code."""

    def __init__(self, code: int, where: str) -> None:
        self.code = code
        super().__init__(f"{where} returned H3Error code {code}")


_AREA_UNITS = {
    "m^2": _ffi.H3_EXT_AREA_M2,
    "km^2": _ffi.H3_EXT_AREA_KM2,
    "rads^2": _ffi.H3_EXT_AREA_RADS2,
}


def _check(err: int, where: str) -> None:
    if err != _ffi.E_SUCCESS:
        raise H3Error(err, where)


def _new_cell_buf():
    return ffi.new("uint8_t[16]")


def _cell_to_hex(cell_buf) -> str:
    """Serialize a 16-byte cell buffer to its canonical hex string.

    Stock res 0–15 cells produce 16-char strings (high 64 bits zero).
    Extended res 16–22 cells produce 32-char strings.
    """
    out = ffi.new("char[64]")
    err = lib.h3_ext_h3_to_string(cell_buf, out, 64)
    _check(err, "h3_to_string")
    return ffi.string(out).decode("ascii")


def _hex_to_cell(cell_hex: str):
    buf = _new_cell_buf()
    err = lib.h3_ext_string_to_h3(cell_hex.encode("ascii"), buf)
    _check(err, "string_to_h3")
    return buf


def latlng_to_cell(lat: float, lng: float, resolution: int) -> str:
    """Index a (lat, lng) point in degrees to a cell at the given resolution."""
    out = _new_cell_buf()
    err = lib.h3_ext_lat_lng_to_cell(
        math.radians(lat), math.radians(lng), int(resolution), out
    )
    _check(err, "lat_lng_to_cell")
    return _cell_to_hex(out)


def cell_to_latlng(cell_hex: str) -> Tuple[float, float]:
    """Return the (lat, lng) center of a cell in degrees."""
    cell = _hex_to_cell(cell_hex)
    lat = ffi.new("double *")
    lng = ffi.new("double *")
    err = lib.h3_ext_cell_to_lat_lng(cell, lat, lng)
    _check(err, "cell_to_lat_lng")
    return math.degrees(lat[0]), math.degrees(lng[0])


def cell_to_parent(cell_hex: str, parent_res: int) -> str:
    cell = _hex_to_cell(cell_hex)
    out = _new_cell_buf()
    err = lib.h3_ext_cell_to_parent(cell, int(parent_res), out)
    _check(err, "cell_to_parent")
    return _cell_to_hex(out)


def cell_to_children(cell_hex: str, child_res: int) -> List[str]:
    cell = _hex_to_cell(cell_hex)
    size = ffi.new("int64_t *")
    err = lib.h3_ext_cell_to_children_size(cell, int(child_res), size)
    _check(err, "cell_to_children_size")
    n = int(size[0])
    if n == 0:
        return []
    buf = ffi.new(f"uint8_t[{n * 16}]")
    count = ffi.new("int64_t *")
    err = lib.h3_ext_cell_to_children(cell, int(child_res), buf, count)
    _check(err, "cell_to_children")
    return [_cell_to_hex(ffi.cast("uint8_t *", buf) + i * 16) for i in range(int(count[0]))]


def cell_to_boundary(cell_hex: str) -> List[Tuple[float, float]]:
    """Return CCW boundary vertices in degrees."""
    cell = _hex_to_cell(cell_hex)
    bnd = ffi.new("CellBoundary *")
    err = lib.h3_ext_cell_to_boundary(cell, bnd)
    _check(err, "cell_to_boundary")
    return [
        (math.degrees(bnd.verts[i].lat), math.degrees(bnd.verts[i].lng))
        for i in range(bnd.numVerts)
    ]


def get_resolution(cell_hex: str) -> int:
    """Effective resolution 0–22.

    The shim's get_resolution wraps the widened H3 getResolution that
    already returns 0–22 (post Phase D3); equivalent to
    get_effective_resolution.
    """
    cell = _hex_to_cell(cell_hex)
    out = ffi.new("int *")
    err = lib.h3_ext_get_resolution(cell, out)
    _check(err, "get_resolution")
    return int(out[0])


def get_effective_resolution(cell_hex: str) -> int:
    """Alias for get_resolution. Provided for explicit-intent call sites."""
    return get_resolution(cell_hex)


def cell_area(cell_hex: str, unit: str = "m^2") -> float:
    if unit not in _AREA_UNITS:
        raise ValueError(
            f"unit must be one of {sorted(_AREA_UNITS)}; got {unit!r}"
        )
    cell = _hex_to_cell(cell_hex)
    out = ffi.new("double *")
    err = lib.h3_ext_cell_area(cell, _AREA_UNITS[unit], out)
    _check(err, "cell_area")
    return float(out[0])


def grid_disk(cell_hex: str, k: int) -> List[str]:
    cell = _hex_to_cell(cell_hex)
    size = ffi.new("int64_t *")
    err = lib.h3_ext_max_grid_disk_size(int(k), size)
    _check(err, "max_grid_disk_size")
    n = int(size[0])
    buf = ffi.new(f"uint8_t[{n * 16}]")
    err = lib.h3_ext_grid_disk(cell, int(k), buf)
    _check(err, "grid_disk")
    out: List[str] = []
    for i in range(n):
        slot = ffi.cast("uint8_t *", buf) + i * 16
        # gridDisk pads unused slots with zero cells; skip those.
        if all(slot[j] == 0 for j in range(16)):
            continue
        out.append(_cell_to_hex(slot))
    return out


def grid_distance(cell_a: str, cell_b: str) -> int:
    a = _hex_to_cell(cell_a)
    b = _hex_to_cell(cell_b)
    out = ffi.new("int64_t *")
    err = lib.h3_ext_grid_distance(a, b, out)
    _check(err, "grid_distance")
    return int(out[0])


def grid_path_cells(cell_a: str, cell_b: str) -> List[str]:
    a = _hex_to_cell(cell_a)
    b = _hex_to_cell(cell_b)
    size = ffi.new("int64_t *")
    err = lib.h3_ext_grid_path_cells_size(a, b, size)
    _check(err, "grid_path_cells_size")
    n = int(size[0])
    if n == 0:
        return []
    buf = ffi.new(f"uint8_t[{n * 16}]")
    err = lib.h3_ext_grid_path_cells(a, b, buf)
    _check(err, "grid_path_cells")
    return [_cell_to_hex(ffi.cast("uint8_t *", buf) + i * 16) for i in range(n)]


def is_valid_cell(cell_hex: str) -> bool:
    try:
        cell = _hex_to_cell(cell_hex)
    except H3Error:
        return False
    out = ffi.new("int *")
    err = lib.h3_ext_is_valid_cell(cell, out)
    if err != _ffi.E_SUCCESS:
        return False
    return bool(out[0])


def cell_to_local_ij(origin: str, cell: str, mode: int = 0) -> Tuple[int, int]:
    o = _hex_to_cell(origin)
    c = _hex_to_cell(cell)
    out = ffi.new("CoordIJ *")
    err = lib.h3_ext_cell_to_local_ij(o, c, int(mode), out)
    _check(err, "cell_to_local_ij")
    return int(out.i), int(out.j)


def local_ij_to_cell(origin: str, i: int, j: int, mode: int = 0) -> str:
    o = _hex_to_cell(origin)
    ij = ffi.new("CoordIJ *", [int(i), int(j)])
    out = _new_cell_buf()
    err = lib.h3_ext_local_ij_to_cell(o, ij, int(mode), out)
    _check(err, "local_ij_to_cell")
    return _cell_to_hex(out)


def to_64bit(cell_hex: str) -> int:
    """Return a stock-h3 64-bit integer for a res 0–15 cell.

    Raises H3Error(E_RES_DOMAIN) if the cell is at an extended resolution
    (high 64 bits non-zero) — the value would not fit in 64 bits.
    """
    cell = _hex_to_cell(cell_hex)
    # Native byte order: __uint128_t. macOS arm64 + Linux x86_64 are LE,
    # so high bytes occupy slots 8..15. If any of those are non-zero,
    # the cell is at ext resolution and cannot be downcast.
    raw = bytes(ffi.buffer(cell, 16))
    if any(b != 0 for b in raw[8:]):
        raise H3Error(_ffi.E_RES_DOMAIN, "to_64bit (ext-res cell)")
    return int.from_bytes(raw[:8], "little")


def from_64bit(legacy_int: int) -> str:
    """Construct a 128-bit cell hex string from a stock-h3 64-bit integer."""
    if legacy_int < 0 or legacy_int >= (1 << 64):
        raise ValueError("legacy_int must be in the range [0, 2**64).")
    cell = _new_cell_buf()
    raw = legacy_int.to_bytes(8, "little") + b"\x00" * 8
    ffi.memmove(cell, raw, 16)
    return _cell_to_hex(cell)
