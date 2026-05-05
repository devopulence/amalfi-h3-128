"""Hex string round-trip — stock (16-char) and extended (32-char)."""

import pytest

import h3_extended as h3x
from h3_extended._ffi import ffi, lib

LAT, LNG = 40.33, -73.99


@pytest.mark.parametrize("res", list(range(0, 16)))
def test_stock_res_string_under_17_chars(res):
    c = h3x.latlng_to_cell(LAT, LNG, res)
    # Stock H3 h3ToString uses %llx — variable length, max 16. The high
    # 64 bits of stock cells are zero, so length is purely the formatted
    # uint64. Just bound it.
    assert 1 <= len(c) <= 16
    # Round-trip via the public API: re-derive from latlng → re-stringify.
    # Internal round-trip is exercised by test_hierarchy / test_basic.


@pytest.mark.parametrize("res", list(range(16, 23)))
def test_ext_res_string_is_32_chars(res):
    c = h3x.latlng_to_cell(LAT, LNG, res)
    assert len(c) == 32, f"res {res} string length {len(c)}, expected 32: {c}"
    assert all(ch in "0123456789abcdef" for ch in c)


@pytest.mark.parametrize("res", [0, 5, 9, 15, 16, 18, 20, 22])
def test_string_round_trip_via_ffi(res):
    """Round-trip the cell through the C string codec directly to check
    bit-for-bit identity at the FFI boundary (mirror of D7-G3)."""
    c = h3x.latlng_to_cell(LAT, LNG, res)
    # Re-parse and re-emit to exercise both directions.
    buf = ffi.new("uint8_t[16]")
    err = lib.h3_ext_string_to_h3(c.encode("ascii"), buf)
    assert err == 0
    out = ffi.new("char[64]")
    err = lib.h3_ext_h3_to_string(buf, out, 64)
    assert err == 0
    assert ffi.string(out).decode("ascii") == c


def test_invalid_hex_returns_error_via_is_valid():
    # is_valid_cell should treat unparseable strings as invalid, not raise.
    assert h3x.is_valid_cell("zzzz") is False
    assert h3x.is_valid_cell("") is False


def test_to_64bit_round_trip_for_all_stock_res():
    for res in range(0, 16):
        c = h3x.latlng_to_cell(LAT, LNG, res)
        n = h3x.to_64bit(c)
        assert h3x.from_64bit(n) == c
