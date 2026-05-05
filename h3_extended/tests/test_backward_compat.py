"""Backward-compatibility — res 0-15 outputs must match stock h3-py v4.x exactly.

Skipped if h3-py is not installed in the test environment.
"""

import pytest

import h3_extended as h3x

h3 = pytest.importorskip("h3", reason="stock h3-py not installed")


GEOS = [
    ("Monmouth", 40.33, -73.99),
    ("Palm Beach", 26.71, -80.05),
    ("Piano di Sorrento", 40.63, 14.40),
]


@pytest.mark.parametrize("name,lat,lng", GEOS)
@pytest.mark.parametrize("res", list(range(0, 16)))
def test_latlng_to_cell_matches_stock(name, lat, lng, res):
    """The 64-bit value extracted via to_64bit must equal stock h3-py."""
    ext_cell = h3x.latlng_to_cell(lat, lng, res)
    stock_int = h3.latlng_to_cell(lat, lng, res)
    # Stock h3-py v4.x returns either int or hex string depending on version.
    if isinstance(stock_int, str):
        stock_int = int(stock_int, 16)
    assert h3x.to_64bit(ext_cell) == stock_int, (
        f"{name} res {res}: ext={ext_cell} -> {h3x.to_64bit(ext_cell):016x}, "
        f"stock={stock_int:016x}"
    )


@pytest.mark.parametrize("name,lat,lng", GEOS)
@pytest.mark.parametrize("res", [0, 5, 9, 12, 15])
def test_cell_to_latlng_matches_stock(name, lat, lng, res):
    ext_cell = h3x.latlng_to_cell(lat, lng, res)
    stock_cell = h3.latlng_to_cell(lat, lng, res)
    ext_lat, ext_lng = h3x.cell_to_latlng(ext_cell)
    stock_lat, stock_lng = h3.cell_to_latlng(stock_cell)
    assert abs(ext_lat - stock_lat) < 1e-12
    assert abs(ext_lng - stock_lng) < 1e-12


def test_from_64bit_matches_stock_round_trip():
    for name, lat, lng in GEOS:
        for res in [0, 5, 9, 12, 15]:
            stock = h3.latlng_to_cell(lat, lng, res)
            stock_int = int(stock, 16) if isinstance(stock, str) else stock
            recreated = h3x.from_64bit(stock_int)
            assert h3x.to_64bit(recreated) == stock_int


def test_to_64bit_rejects_ext_cells():
    """A res 16+ cell uses the high 64 bits and cannot be downcast."""
    ext_cell = h3x.latlng_to_cell(40.33, -73.99, 18)
    with pytest.raises(h3x.H3Error):
        h3x.to_64bit(ext_cell)


@pytest.mark.parametrize("name,lat,lng", GEOS)
@pytest.mark.parametrize("res", [0, 5, 9, 12, 15])
def test_cell_area_m2_matches_stock(name, lat, lng, res):
    """cell_area routes through the same C code path as stock h3-py for
    res 0-15; the only divergence is double-precision noise from
    iterative spherical-geometry kernels. 1e-7 relative tolerance covers
    observed worst case (~3.6e-9) with comfortable headroom and still
    catches any structural regression."""
    ext_cell = h3x.latlng_to_cell(lat, lng, res)
    stock_cell = h3.latlng_to_cell(lat, lng, res)
    ext_area = h3x.cell_area(ext_cell, "m^2")
    stock_area = h3.cell_area(stock_cell, "m^2")
    rel = abs(ext_area - stock_area) / stock_area
    assert rel < 1e-7, f"{name} res {res}: rel diff {rel}"
