"""grid_disk / grid_distance / grid_path_cells at extended resolutions."""

import pytest

import h3_extended as h3x

LAT, LNG = 40.33, -73.99


def test_grid_disk_k0_is_origin_only():
    origin = h3x.latlng_to_cell(LAT, LNG, 9)
    disk = h3x.grid_disk(origin, 0)
    assert disk == [origin]


@pytest.mark.parametrize("res", [9, 15, 18, 20])
def test_grid_disk_k1_yields_seven_cells(res):
    origin = h3x.latlng_to_cell(LAT, LNG, res)
    disk = h3x.grid_disk(origin, 1)
    # Hexagon: 1 + 6 = 7. Pentagon would yield 6, but Sandy Hook is not
    # near a pentagon, so 7 is expected at any of these resolutions.
    assert len(disk) == 7, f"res {res} k=1 disk size {len(disk)}, expected 7"
    assert origin in disk
    # All neighbors at the same resolution.
    for c in disk:
        assert h3x.get_resolution(c) == res


@pytest.mark.parametrize("res", [9, 15, 18, 20])
def test_grid_disk_k2_yields_19_cells(res):
    origin = h3x.latlng_to_cell(LAT, LNG, res)
    disk = h3x.grid_disk(origin, 2)
    # 1 + 6 + 12 = 19 for a hexagon disk at k=2.
    assert len(disk) == 19


def test_grid_distance_self_is_zero():
    cell = h3x.latlng_to_cell(LAT, LNG, 18)
    assert h3x.grid_distance(cell, cell) == 0


def test_grid_distance_to_neighbor_is_one():
    origin = h3x.latlng_to_cell(LAT, LNG, 9)
    disk = h3x.grid_disk(origin, 1)
    neighbors = [c for c in disk if c != origin]
    for n in neighbors:
        assert h3x.grid_distance(origin, n) == 1


def test_grid_path_cells_self_is_singleton():
    cell = h3x.latlng_to_cell(LAT, LNG, 18)
    assert h3x.grid_path_cells(cell, cell) == [cell]


def test_grid_path_cells_to_neighbor_is_two_step():
    origin = h3x.latlng_to_cell(LAT, LNG, 9)
    disk = h3x.grid_disk(origin, 1)
    neighbor = next(c for c in disk if c != origin)
    path = h3x.grid_path_cells(origin, neighbor)
    assert len(path) == 2
    assert path[0] == origin
    assert path[-1] == neighbor


@pytest.mark.parametrize("res", [18, 19, 20])
def test_local_ij_round_trip(res):
    origin = h3x.latlng_to_cell(LAT, LNG, res)
    disk = h3x.grid_disk(origin, 1)
    for c in disk:
        i, j = h3x.cell_to_local_ij(origin, c)
        recovered = h3x.local_ij_to_cell(origin, i, j)
        assert recovered == c, f"res {res}: ({i},{j}) round-trip changed cell"
