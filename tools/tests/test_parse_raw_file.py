"""Unit tests for compare_kicad_models.parse_raw_file multi-plot selection.

Regression guard for the tube.lib "ng=0 everywhere" harness artifact: a single
.raw file may contain several plots (ngspice honors stray file-scope
.AC/.TRAN/.TEMP cards left in included libraries and emits
[AC Analysis, Operating Point, Transient Analysis] into one raw). The parser
must select the Operating Point plot, not blindly take the first (often a
stimulus-free AC plot full of zeros/denormals stored 16 bytes/value).
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

# compare_kicad_models lives in tools/ (one level up from tools/tests/).
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from compare_kicad_models import parse_raw_file  # noqa: E402


def _emit_plot(plotname, complex_flag, variables, point):
    """Build one SPICE-raw plot block (header + binary first data point).

    `variables` is a list of names; `point` is a parallel list of float values.
    For complex plots each value is stored as 16 bytes (re, im); we store the
    given value as the real part and 0.0 as the imaginary part.
    """
    flags = "complex" if complex_flag else "real"
    lines = [
        f"Plotname: {plotname}",
        f"Flags: {flags}",
        f"No. Variables: {len(variables)}",
        "No. Points: 1",
        "Variables:",
    ]
    for i, name in enumerate(variables):
        lines.append(f"\t{i}\t{name}\tvoltage")
    lines.append("Binary:\n")
    header = "\n".join(lines).encode("utf-8")

    payload = b""
    for v in point:
        if complex_flag:
            payload += struct.pack("d", v) + struct.pack("d", 0.0)
        else:
            payload += struct.pack("d", v)
    return header + payload


def _write_raw(tmp_path, blocks):
    path = tmp_path / "test.raw"
    path.write_bytes(b"".join(blocks))
    return str(path)


def test_selects_operating_point_over_leading_ac_plot(tmp_path):
    """Mimics tube.lib: leading complex AC plot of zeros, then real OP plot."""
    variables = ["v(in_p)", "v(net_g)", "v(net_k)"]
    ac = _emit_plot("AC Analysis", True, variables, [0.0, 0.0, 0.0])
    op = _emit_plot("Operating Point", False, variables, [0.5, 0.0, 0.4708922919])
    tran = _emit_plot("Transient Analysis", False, variables, [0.5, 0.0, 0.47])
    path = _write_raw(tmp_path, [ac, op, tran])

    vals = parse_raw_file(path)
    assert vals is not None
    assert vals["v(in_p)"] == 0.5
    assert abs(vals["v(net_k)"] - 0.4708922919) < 1e-12
    assert vals["v(net_g)"] == 0.0


def test_single_operating_point_plot(tmp_path):
    """neospice-style raw: a single real Operating Point plot."""
    variables = ["v(in_p)", "v(net_k)"]
    op = _emit_plot("Operating Point", False, variables, [0.5, 0.4708922923])
    path = _write_raw(tmp_path, [op])

    vals = parse_raw_file(path)
    assert vals is not None
    assert vals["v(in_p)"] == 0.5
    assert abs(vals["v(net_k)"] - 0.4708922923) < 1e-12


def test_falls_back_to_first_real_plot_when_no_op(tmp_path):
    """No Operating Point plot: prefer the first real (non-complex) plot."""
    variables = ["v(a)", "v(b)"]
    ac = _emit_plot("AC Analysis", True, variables, [0.0, 0.0])
    tran = _emit_plot("Transient Analysis", False, variables, [1.0, 2.0])
    path = _write_raw(tmp_path, [ac, tran])

    vals = parse_raw_file(path)
    assert vals is not None
    assert vals["v(a)"] == 1.0
    assert vals["v(b)"] == 2.0


def test_missing_file_returns_none(tmp_path):
    assert parse_raw_file(str(tmp_path / "nope.raw")) is None
