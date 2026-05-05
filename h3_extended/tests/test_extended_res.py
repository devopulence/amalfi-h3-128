"""Resolutions 16-22 — the H3-Extended range."""

import pytest

import h3_extended as h3x

LAT, LNG = 40.33, -73.99


@pytest.mark.parametrize("res", list(range(16, 23)))
def test_ext_res_creation_and_validation(res):
    c = h3x.latlng_to_cell(LAT, LNG, res)
    assert h3x.is_valid_cell(c), f"res {res} cell {c} reports invalid"
    assert h3x.get_resolution(c) == res
    assert h3x.get_effective_resolution(c) == res


@pytest.mark.parametrize("res", list(range(16, 23)))
def test_ext_res_serialize_to_32_chars(res):
    c = h3x.latlng_to_cell(LAT, LNG, res)
    assert len(c) == 32, (
        f"res {res} cell should be 32 hex chars (zero-padded 128-bit), got {len(c)}: {c}"
    )


@pytest.mark.parametrize("res", list(range(16, 23)))
def test_ext_res_area_strictly_decreasing(res):
    c_prev = h3x.latlng_to_cell(LAT, LNG, res - 1)
    c_this = h3x.latlng_to_cell(LAT, LNG, res)
    a_prev = h3x.cell_area(c_prev, "m^2")
    a_this = h3x.cell_area(c_this, "m^2")
    assert a_this < a_prev, f"res {res} area {a_this} not smaller than res {res - 1} {a_prev}"


def test_ext_res_area_sub_meter_at_res_20():
    """At res 20 a hexagon edge is ~3-5mm, so area is well below 1 m^2."""
    c = h3x.latlng_to_cell(LAT, LNG, 20)
    area = h3x.cell_area(c, "m^2")
    assert 0 < area < 1.0, f"res 20 area expected sub-square-meter, got {area}"


def test_ext_res_22_area_well_below_res_15():
    """Sanity ratio: res 22 area must be ~7^7 smaller than res 15 area."""
    c15 = h3x.latlng_to_cell(LAT, LNG, 15)
    c22 = h3x.latlng_to_cell(LAT, LNG, 22)
    a15 = h3x.cell_area(c15, "m^2")
    a22 = h3x.cell_area(c22, "m^2")
    ratio = a15 / a22
    # 7^7 = 823543, but pentagon/hexagon mix and projection make exact
    # ratios noisy. Assert order of magnitude.
    assert ratio > 1e5, f"area ratio res15/res22 = {ratio}, expected > 1e5"
