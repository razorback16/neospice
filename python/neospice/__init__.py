from __future__ import annotations

import os
from typing import Any

from neospice._core import (  # noqa: F401
    ACMode,
    ACOptions,
    ACResult,
    Circuit,
    CircuitBuilder,
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
    SimStatus,
    SimulationResult,
    Simulator,
    SimulatorOptions,
    SinSpec,
    SourceSpec,
    StepResult,
    TFResult,
    TransientOptions,
    TransientResult,
)

__version__ = "0.1.0"

_MODE_MAP = {"dec": ACMode.DEC, "oct": ACMode.OCT, "lin": ACMode.LIN}


def _resolve_mode(mode: str | ACMode) -> ACMode:
    if isinstance(mode, ACMode):
        return mode
    return _MODE_MAP[mode.lower()]


_VALID_OPTS = frozenset({"abstol", "reltol", "vntol", "trtol", "gmin"})


def _make_sim(**opts: Any) -> Simulator:
    if opts:
        bad = opts.keys() - _VALID_OPTS
        if bad:
            raise TypeError(f"Unknown simulator option(s): {', '.join(sorted(bad))}")
        so = SimulatorOptions()
        for k, v in opts.items():
            setattr(so, k, v)
        return Simulator(so)
    return Simulator()


def _load_or_parse(sim: Simulator, netlist: str) -> Circuit:
    if os.path.exists(netlist):
        return sim.load(netlist)
    return sim.parse(netlist)


def dc(netlist: str, **opts: Any) -> DCResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_dc(ckt)


def transient(netlist: str, *, tstep: float, tstop: float, **opts: Any) -> TransientResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
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
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
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
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_noise(ckt, output, input_src, _resolve_mode(mode), npoints, fstart, fstop)


def dc_sweep(netlist: str, params: list[DCSweepParam], **opts: Any) -> DCSweepResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_dc_sweep(ckt, params)


def tf(netlist: str, *, output: str, input_src: str, **opts: Any) -> TFResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_tf(ckt, output, input_src)


def sens(netlist: str, *, output: str, **opts: Any) -> SensResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_sens(ckt, output)


def run(netlist: str, **opts: Any) -> SimulationResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run(ckt)
