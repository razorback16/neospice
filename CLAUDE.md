# neospice


## Reference simulator

ngspice is the reference implementation. neospice must match ngspice's behavior — never the other way around. When results differ, assume neospice is wrong until proven otherwise. Do not loosen tolerances or change ngspice options to hide discrepancies; fix the neospice implementation instead.

ngspice source code is available locally at `~/Codes/ngspice`.

## Reference libraries

SuiteSparse source code is available locally at `~/Codes/SuiteSparse`. Contains KLU, AMD, BTF, COLAMD, and other sparse solver components used as reference for our custom implementations.
