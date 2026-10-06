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
    GradientResult, ACGradientResult, ReuseStatistics,
    SensEntry,
    SensResult,
    SimulatorOptions,
    SimStatus,
    SimulationResult,
    Simulator,
    SinSpec,
    SourceSpec,
    StepResult,
    SweepPoint, SweepOptions, SweepSample, SweepResult,
    VariationDistribution, ParameterVariation, MonteCarloOptions,
    SampleStatistics, summarize_samples,
    TFResult,
    TransientOptions,
    TransientResult,
)

from neospice.circuit import parse_value  # noqa: F401

__version__ = "0.2.0"

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


def sweep(netlist: str, points: list[dict[str, float] | SweepPoint], *,
          workers: int = 0, seed: int = 0) -> SweepResult:
    """Run ordered independent jobs. Dicts override declared top-level .param values.

    Use SweepPoint for device values and Celsius temperature corners.
    Each sample retains its error; callers must check errors before aggregation.
    """
    converted = []
    for point in points:
        if isinstance(point, SweepPoint):
            converted.append(point)
        else:
            item = SweepPoint()
            item.parameters = point
            converted.append(item)
    options = SweepOptions()
    options.workers, options.seed = workers, seed
    return Simulator().run_sweep(netlist, converted, options, os.path.isfile(netlist))


def monte_carlo(netlist: str, params: list[ParameterVariation], *, n: int = 100,
                seed: int = 0, workers: int = 0,
                correlation: list[list[float]] | None = None) -> SweepResult:
    """Seeded Gaussian/uniform top-level parameter variations.

    Spread is absolute sigma for Gaussian, half-width for uniform. Correlation
    requires Gaussian variations. Sampling is stable across worker counts on
    the same C++ standard-library implementation.
    """
    options = MonteCarloOptions()
    options.samples = n
    options.execution.seed, options.execution.workers = seed, workers
    if correlation is not None:
        options.correlation = correlation
    return Simulator().monte_carlo(netlist, params, options, os.path.isfile(netlist))


def sensitivity(netlist: str, outputs: list[str], parameters: list[str] | None = None,
                **opts: Any) -> GradientResult:
    """DC adjoint Jacobian [output][parameter] for R/C/L and source DC values."""
    circuit = _load_or_parse(netlist)
    _apply_opts(circuit, opts)
    return Simulator().sensitivity(circuit, outputs, parameters or [])


def sensitivity_ac(netlist: str, outputs: list[str], parameters: list[str],
                   frequencies: list[float], **opts: Any) -> ACGradientResult:
    """Complex adjoint Jacobian [frequency][output][parameter], linear circuits only."""
    circuit = _load_or_parse(netlist)
    _apply_opts(circuit, opts)
    return Simulator().sensitivity_ac(circuit, outputs, parameters, frequencies)
