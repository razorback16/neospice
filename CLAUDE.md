# neospice

## Git workflow

Push directly to `main`. Do not create feature branches or pull requests.

## Reference simulator

ngspice is the reference implementation. neospice must match ngspice's behavior — never the other way around. When results differ, assume neospice is wrong until proven otherwise. Do not loosen tolerances or change ngspice options to hide discrepancies; fix the neospice implementation instead.

ngspice source code is available locally at `~/Codes/ngspice`. Key files for transient analysis:
- `src/spicelib/analysis/dctran.c` — main transient loop
- `src/spicelib/analysis/cktterr.c` — truncation error
- `src/spicelib/analysis/cktsetbk.c` — breakpoint setup
