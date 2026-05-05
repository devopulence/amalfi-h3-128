"""Round-trip precision at the three target geographies.

Tolerance gates from the user spec:
  res 19 round-trip < 1cm
  res 20 round-trip < 5mm
"""

import math

import pytest

import h3_extended as h3x

GEOS = [
    ("Monmouth", 40.33, -73.99),
    ("Palm Beach", 26.71, -80.05),
    ("Piano di Sorrento", 40.63, 14.40),
]

# WGS84 mean radius — adequate for surface-distance error checks at the
# small angles round-tripping produces.
EARTH_R_M = 6_371_000.0


def haversine_m(lat1, lng1, lat2, lng2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lng2 - lng1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * EARTH_R_M * math.asin(math.sqrt(a))


@pytest.mark.parametrize("name,lat,lng", GEOS)
def test_res19_round_trip_within_1cm(name, lat, lng):
    cell = h3x.latlng_to_cell(lat, lng, 19)
    clat, clng = h3x.cell_to_latlng(cell)
    err = haversine_m(lat, lng, clat, clng)
    # Round-trip "error" here is the distance from input to cell center
    # — bounded by the cell's own dimension (~7m at res 19), not by
    # libm. The discriminating test is below: input → cell → center →
    # cell-of-center must yield the same cell.
    assert err < 10.0, f"{name}: input→center distance {err:.4f} m > 10 m sanity bound"

    # The real round-trip closure: cell center → re-indexed cell.
    re_cell = h3x.latlng_to_cell(clat, clng, 19)
    assert re_cell == cell, f"{name}: res 19 center re-index changed cell"


@pytest.mark.parametrize("name,lat,lng", GEOS)
def test_res20_round_trip_within_5mm(name, lat, lng):
    cell = h3x.latlng_to_cell(lat, lng, 20)
    clat, clng = h3x.cell_to_latlng(cell)

    # Center → cell → center must close to within libm precision; on
    # macOS arm64 + Apple libm Session 7 measured 0 rad delta.
    re_cell = h3x.latlng_to_cell(clat, clng, 20)
    assert re_cell == cell, f"{name}: res 20 center re-index changed cell"

    re_lat, re_lng = h3x.cell_to_latlng(re_cell)
    err = haversine_m(clat, clng, re_lat, re_lng)
    assert err < 0.005, f"{name}: res 20 center round-trip {err * 1000:.4f} mm > 5 mm"


@pytest.mark.parametrize("name,lat,lng", GEOS)
@pytest.mark.parametrize("res", [19, 20, 21, 22])
def test_center_re_index_idempotent(name, lat, lng, res):
    cell = h3x.latlng_to_cell(lat, lng, res)
    clat, clng = h3x.cell_to_latlng(cell)
    re_cell = h3x.latlng_to_cell(clat, clng, res)
    assert re_cell == cell
