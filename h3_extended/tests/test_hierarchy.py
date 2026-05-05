"""Parent/child semantics across the stock-ext (res 15 → 16) boundary."""

import pytest

import h3_extended as h3x

LAT, LNG = 40.33, -73.99


def test_res16_child_has_res15_parent():
    """Crossing the stock→ext boundary in the parent direction.

    Anchor the test at a res-15 cell's *center*, which is guaranteed
    interior at all coarser resolutions. (40.33, -73.99) happens to sit
    within ε of a res-15 cell edge, so res-15 vs res-16 indexing of the
    raw input land in different cells — a geography-specific edge case,
    not a hierarchy bug.)
    """
    p15 = h3x.latlng_to_cell(LAT, LNG, 15)
    plat, plng = h3x.cell_to_latlng(p15)
    c16 = h3x.latlng_to_cell(plat, plng, 16)
    assert h3x.get_resolution(p15) == 15
    assert h3x.cell_to_parent(c16, 15) == p15


def test_res15_to_res16_children_count_seven():
    p15 = h3x.latlng_to_cell(LAT, LNG, 15)
    children = h3x.cell_to_children(p15, 16)
    assert len(children) == 7
    for c in children:
        assert h3x.get_resolution(c) == 16
        assert h3x.cell_to_parent(c, 15) == p15
        assert len(c) == 32


@pytest.mark.parametrize(
    "parent_res,child_res",
    [(0, 16), (5, 18), (10, 20), (15, 22), (16, 22), (18, 22)],
)
def test_descend_then_ascend_recovers_parent(parent_res, child_res):
    """Pick the parent cell, descend from its center, then ascend — must
    recover the original parent. Anchoring at the parent's center sidesteps
    edge cells where raw-lat/lng indexing at child_res might land in a
    sibling parent (see test_res16_child_has_res15_parent)."""
    p = h3x.latlng_to_cell(LAT, LNG, parent_res)
    plat, plng = h3x.cell_to_latlng(p)
    c = h3x.latlng_to_cell(plat, plng, child_res)
    assert h3x.cell_to_parent(c, parent_res) == p


def test_res22_full_descent():
    c0 = h3x.latlng_to_cell(LAT, LNG, 0)
    c22 = h3x.latlng_to_cell(LAT, LNG, 22)
    assert h3x.cell_to_parent(c22, 0) == c0


def test_children_size_matches_seven_per_step():
    """Hexagon parent → child count is 7^(child_res - parent_res)."""
    parent = h3x.latlng_to_cell(LAT, LNG, 14)
    for delta in (1, 2, 3):
        children = h3x.cell_to_children(parent, 14 + delta)
        assert len(children) == 7**delta
