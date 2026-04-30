import pytest
from neospice.circuit import parse_value


def test_parse_integer():
    assert parse_value(100) == 100.0


def test_parse_float():
    assert parse_value(1.5) == 1.5


def test_parse_kilo():
    assert parse_value("1k") == 1000.0


def test_parse_pico():
    assert parse_value("100p") == 100e-12


def test_parse_mega():
    assert parse_value("10meg") == 10e6


def test_parse_nano():
    assert parse_value("4.7n") == pytest.approx(4.7e-9)


def test_parse_micro():
    assert parse_value("10u") == pytest.approx(10e-6)


def test_parse_milli():
    assert parse_value("2.2m") == 2.2e-3


def test_parse_scientific():
    assert parse_value("1e3") == 1000.0


def test_parse_invalid():
    with pytest.raises(ValueError):
        parse_value("abc")


def test_parse_negative():
    assert parse_value("-5.0") == -5.0
