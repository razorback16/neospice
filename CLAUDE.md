# neospice

## Branching

Work directly on `main` unless the user explicitly asks for a feature branch.

## Documentation

Update relevant documentation (README, roadmap, etc.) before pushing to remote.

## Reference simulator

ngspice is the reference implementation. neospice must match ngspice's behavior — never the other way around. When results differ, assume neospice is wrong until proven otherwise. Do not loosen tolerances or change ngspice options to hide discrepancies; fix the neospice implementation instead.

ngspice source code is available locally at `~/Codes/ngspice`.

## Reference libraries

SuiteSparse source code is available locally at `~/Codes/SuiteSparse`. Contains KLU, AMD, BTF, COLAMD, and other sparse solver components used as reference for our custom implementations.

## Test diagnostics

To see actual error margins in ngspice comparison tests, reconfigure with:

```
cmake .. -DNEOSPICE_DEBUG_COMPARE=ON
```

This enables `NEOSPICE_DEBUG_COMPARE` which prints `MARGIN_TRAN|signal|actual_err|tolerance|headroom` to stderr for every compare_transient / compare_dc / compare_ac call.
