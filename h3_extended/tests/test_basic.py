"""Stock-resolution (0-15) creation, parent/child, validation."""

import math

import pytest

import h3_extended as h3x

# Sandy Hook, NJ — within Monmouth County test area.
LAT, LNG = 40.33, -73.99


@pytest.mark.parametrize("res", list(range(0, 16)))
def test_latlng_to_cell_returns_valid(res):
    c = h3x.latlng_to_cell(LAT, LNG, res)
    assert isinstance(c, str) and len(c) > 0
    assert h3x.is_valid_cell(c)
    assert h3x.get_resolution(c) == res


def test_round_trip_centers_within_radius():
    for res in range(0, 16):
        c = h3x.latlng_to_cell(LAT, LNG, res)
        lat, lng = h3x.cell_to_latlng(c)
        # Re-indexing the center returns the same cell.
        assert h3x.latlng_to_cell(lat, lng, res) == c


def test_parent_at_lower_res():
    c = h3x.latlng_to_cell(LAT, LNG, 9)
    p = h3x.cell_to_parent(c, 5)
    assert h3x.get_resolution(p) == 5
    # Anchor: the res-5 parent of the res-9 cell at this geography is
    # itself the res-5 cell containing the same point.
    assert p == h3x.latlng_to_cell(LAT, LNG, 5)


def test_children_count_and_membership():
    parent = h3x.latlng_to_cell(LAT, LNG, 5)
    children = h3x.cell_to_children(parent, 6)
    # Hexagons have 7 children at the next resolution.
    assert len(children) == 7
    # Each child reports the parent as its res-5 ancestor.
    for child in children:
        assert h3x.cell_to_parent(child, 5) == parent
        assert h3x.get_resolution(child) == 6


def test_is_valid_cell_rejects_garbage():
    assert not h3x.is_valid_cell("not_a_real_cell")
    assert not h3x.is_valid_cell("")


def test_cell_to_boundary_returns_finite_verts():
    c = h3x.latlng_to_cell(LAT, LNG, 9)
    verts = h3x.cell_to_boundary(c)
    assert 5 <= len(verts) <= 10
    for lat, lng in verts:
        assert math.isfinite(lat) and math.isfinite(lng)
        assert -90.0 <= lat <= 90.0
        assert -180.0 <= lng <= 180.0


def test_cell_area_decreases_with_resolution():
    prev = float("inf")
    for res in range(0, 16):
        c = h3x.latlng_to_cell(LAT, LNG, res)
        a = h3x.cell_area(c, "m^2")
        assert a > 0
        assert a < prev, f"area at res {res} ({a}) not smaller than res {res - 1} ({prev})"
        prev = a


def test_cell_area_unit_consistency():
    c = h3x.latlng_to_cell(LAT, LNG, 9)
    m2 = h3x.cell_area(c, "m^2")
    km2 = h3x.cell_area(c, "km^2")
    assert abs(m2 - km2 * 1e6) / m2 < 1e-9
