# Incremental re-simulation

Keep one `Circuit` and use `update_param()` with `Simulator::re_solve()` for DC,
or `Simulator::re_solve_ac()` for an AC sweep. Reusable symbolic factorization
and workspaces belong to that circuit. Independent circuits have independent
caches. Values/results are recomputed, so an update cannot return an old result.

```python
import neospice as ns
sim = ns.Simulator()
circuit = sim.parse('Divider\nV1 in 0 10\nR1 in out 1k\nR2 out 0 1k\n.op\n.end\n')
for resistance in [1000, 1500, 2000]:
    circuit.update_param('r2', resistance)
    result = sim.re_solve(circuit)
    assert result.status.converged
    print(resistance, result.voltage('out'))
print(circuit.reuse_statistics().dc_symbolic_analyses)  # 1
```

C++ uses the same names. For AC:

```cpp
ckt.update_param("c1", 47e-9);
auto ac = sim.re_solve_ac(ckt, neospice::ACMode::DEC, 20, 10, 1e6);
```

`update_param` changes the supported primitive primary value, not a netlist
`.param` expression or semiconductor model card. Unknown or unsupported devices,
nonfinite values, zero resistance, negative capacitance, and nonpositive
inductance are rejected before mutation. Existing `set_param()` retains its
boolean interface. Both paths invalidate the operating point and temperature
preparation, so temperature coefficients and multipliers are reapplied after
changes. Changing `options.temp` / `options.tnom` also triggers preparation.

A finalized circuit's topology is immutable: adding nodes/devices raises an
error. Build a new circuit for topology changes. Circuit moves transfer cache
ownership. `clear_reuse_cache()` releases cached solvers and resets counters;
it does not change component values. The DC cache rebuilds automatically if
the `NEOSPICE_SOLVER` / `NEOSPICE_FORCE_AMDLU` selection changes between calls.
Do not mutate process environment while other simulation threads are running.

`reuse_statistics()` reports DC/AC run and symbolic-analysis counts. Ordinary
`run_dc` / `run_ac` calls retain their existing fresh-solver behavior and do not
populate these caches. Calling `re_solve` once initializes the DC cache.
Calling `re_solve_ac` initializes AC and, when a fresh operating point is needed,
DC workspaces. Each retains its symbolic pattern for subsequent calls.

Linear DC circuits reuse the numeric pivot path. Nonlinear circuits retain the
normal Newton initialization and convergence fallbacks to preserve operating-
point selection; they reuse the symbolic structure but can reorder numerically.
AC reuses the symbolic pattern and numeric refactorization, with a full numeric
factorization retry if a pivot becomes singular. Failed solves discard the
corresponding cached solver before reuse.

All affected analyses restamp the matrix and solve all requested frequencies.
There is no approximation based on frequency significance, no partial-device
restamping, and no incremental transient resume. These are possible extensions.
No sub-millisecond latency or speedup claim is made without a measured workload.
