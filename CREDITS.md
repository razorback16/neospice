# Credits and Lineage

neospice is an independent C++20 reimplementation of SPICE that descends from
the UC Berkeley SPICE family. It is **not** original or from-scratch code: its
core algorithms — DC operating point, DC sweep, transient analysis (the
MODE / INITF state machine and LTE timestep control), AC, noise, transfer
function, pole-zero, sensitivity, and the Newton-Raphson convergence aids — and
many of its device models are translated from Berkeley SPICE3F5 and ngspice.

ngspice is used as the reference implementation. neospice aims to match
ngspice's behavior; where results differ, neospice is assumed wrong until
proven otherwise.

For the authoritative, per-file copyright and license breakdown, see
[NOTICE](NOTICE).

## SPICE lineage

```
Nagel SPICE (1973)
  -> SPICE2 (1975)
    -> SPICE2G6 (1983, Fortran)
      -> SPICE3F5 (UC Berkeley, C)
        -> ngspice (maintained)
          -> neospice (C++ reimplementation)
```

## Licensing

neospice is released under the MIT License (see [LICENSE](LICENSE)). This is
compatible with its Berkeley heritage because Berkeley SPICE3 is distributed
under a permissive BSD-style license (the "Berkeley Spice3" license), which
allows reuse, modification, and relicensing provided the original copyright
notices are preserved. Those notices, along with the additional terms that
apply to specific device models (e.g. the BSIM and HiSIM compact models), are
documented in [NOTICE](NOTICE) on a per-component basis.

## Historical references

- L. W. Nagel and D. O. Pederson, "SPICE," Memorandum No. ERL-M382,
  UC Berkeley, April 1973.
- L. W. Nagel, "SPICE2: A Computer Program to Simulate Semiconductor Circuits,"
  Memorandum No. ERL-M520, UC Berkeley, May 1975.
- SPICE2G6 (1983) and SPICE3F5 (UC Berkeley).

The Berkeley copyright preserved throughout the core engine is:

> Copyright 1990 Regents of the University of California. All rights reserved.

ngspice, the maintained SPICE3F5 descendant used as the reference
implementation, is credited to Paolo Nenzi, Holger Vogt, Dietmar Warning, and
the ngspice maintainers.

## Sparse solver lineage

The sparse solver stack (`src/core/neo_solver.cpp`, `amd.cpp`, `btf.cpp`,
`matching.cpp`) traces to:

- **Sparse 1.3** — the NeoSolver sparse-LU factorization is Sparse 1.3-compatible.
  Credit to Kenneth S. Kundert and Alberto Sangiovanni-Vincentelli, "Sparse 1.3"
  (UC Berkeley), the sparse-matrix package used by SPICE3.
- **SuiteSparse AMD** — the Approximate Minimum Degree ordering derives from and
  matches SuiteSparse AMD by Timothy A. Davis, Patrick Amestoy, and Iain Duff
  (BSD-3-Clause licensed).
- **SuiteSparse BTF** — the BTF / maximum-transversal logic likewise traces to
  Timothy A. Davis's SuiteSparse BTF (BSD-3-Clause licensed).

See [NOTICE](NOTICE) for the full per-component copyright list.
