"""Batch operations — Python loops for now, structured for drop-in
Pandas UDF / Spark vectorization later. The single-call layer in
__init__.py is the contract; these helpers don't reach into the FFI
directly so any optimization (e.g. allocating one buffer per chunk)
can be added here without touching public callers.
"""

from __future__ import annotations

from typing import Iterable, List, Sequence

from . import cell_to_parent, get_resolution, latlng_to_cell

__all__ = [
    "batch_latlng_to_cell",
    "batch_cell_to_parent",
    "batch_get_resolution",
]


def batch_latlng_to_cell(
    lats: Sequence[float], lngs: Sequence[float], resolution: int
) -> List[str]:
    if len(lats) != len(lngs):
        raise ValueError(
            f"lats and lngs must have equal length; got {len(lats)} and {len(lngs)}"
        )
    res = int(resolution)
    return [latlng_to_cell(lat, lng, res) for lat, lng in zip(lats, lngs)]


def batch_cell_to_parent(cells: Iterable[str], parent_res: int) -> List[str]:
    pr = int(parent_res)
    return [cell_to_parent(c, pr) for c in cells]


def batch_get_resolution(cells: Iterable[str]) -> List[int]:
    return [get_resolution(c) for c in cells]
