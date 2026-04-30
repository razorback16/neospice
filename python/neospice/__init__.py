from __future__ import annotations

import os
from typing import Any

from neospice._core import (  # noqa: F401
    ACMode,
    ACOptions,
    ACResult,
    Circuit,
    ConvergenceMethod,
    DCResult,
    DCSweepParam,
    DCSweepResult,
    DeviceInfo,
    IntegrationMethod,
    MeasureResult,
    NoiseResult,
    PulseSpec,
    PZResult,
    PZTransferType,
    PZType,
    SensEntry,
    SensResult,
    SimulatorOptions,
    SimStatus,
    SimulationResult,
    Simulator,
    SinSpec,
    SourceSpec,
    StepResult,
    TFResult,
    TransientOptions,
    TransientResult,
)

from neospice.circuit import parse_value  # noqa: F401

__version__ = "0.1.0"

_MODE_MAP = {"dec": ACMode.DEC, "oct": ACMode.OCT, "lin": ACMode.LIN}


def _resolve_mode(mode: str | ACMode) -> ACMode:
    if isinstance(mode, ACMode):
        return mode
    return _MODE_MAP[mode.lower()]


_VALID_OPTS = frozenset({
    "abstol", "reltol", "vntol", "trtol", "chgtol", "gmin",
    "temp", "tnom", "max_iter", "itl1", "itl4", "method", "verbose",
})


def _load_or_parse(netlist: str) -> Circuit:
    sim = Simulator()
    if os.path.exists(netlist):
        return sim.load(netlist)
    return sim.parse(netlist)


def _apply_opts(ckt: Circuit, opts: dict[str, Any]) -> None:
    if not opts:
        return
    bad = opts.keys() - _VALID_OPTS
    if bad:
        raise TypeError(f"Unknown option(s): {', '.join(sorted(bad))}")
    for k, v in opts.items():
        setattr(ckt.options, k, v)


def dc(netlist: str, **opts: Any) -> DCResult:
    sim = Simulator()
    ckt = _load_or_parse(netlist)
    _apply_opts(ckt, opts)
    return sim.run_dc(ckt)


def transient(netlist: str, *, tstep: float, tstop: float, **opts: Any) -> TransientResult:
    sim = Simulator()
    ckt = _load_or_parse(netlist)
    _apply_opts(ckt, opts)
    return sim.run_transient(ckt, tstep, tstop)


def ac(
    netlist: str,
    *,
    mode: str | ACMode = "dec",
    npoints: int = 100,
    fstart: float = 1.0,
    fstop: float = 1e9,
    **opts: Any,
) -> ACResult:
    sim = Simulator()
    ckt = _load_or_parse(netlist)
    _apply_opts(ckt, opts)
    return sim.run_ac(ckt, _resolve_mode(mode), npoints, fstart, fstop)


def noise(
    netlist: str,
    *,
    output: str,
    input_src: str,
    mode: str | ACMode = "dec",
    npoints: int = 100,
    fstart: float = 1.0,
    fstop: float = 1e9,
    **opts: Any,
) -> NoiseResult:
    sim = Simulator()
    ckt = _load_or_parse(netlist)
    _apply_opts(ckt, opts)
    return sim.run_noise(ckt, output, input_src, _resolve_mode(mode), npoints, fstart, fstop)


def dc_sweep(netlist: str, params: list[DCSweepParam], **opts: Any) -> DCSweepResult:
    sim = Simulator()
    ckt = _load_or_parse(netlist)
    _apply_opts(ckt, opts)
    return sim.run_dc_sweep(ckt, params)


def tf(netlist: str, *, output: str, input_src: str, **opts: Any) -> TFResult:
    sim = Simulator()
    ckt = _load_or_parse(netlist)
    _apply_opts(ckt, opts)
    return sim.run_tf(ckt, output, input_src)


def sens(netlist: str, *, output: str, **opts: Any) -> SensResult:
    sim = Simulator()
    ckt = _load_or_parse(netlist)
    _apply_opts(ckt, opts)
    return sim.run_sens(ckt, output)


def run(netlist: str, **opts: Any) -> SimulationResult:
    sim = Simulator()
    ckt = _load_or_parse(netlist)
    _apply_opts(ckt, opts)
    return sim.run(ckt)
