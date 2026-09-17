# BSIM4 goldens from ngspice 47

`probe.cir` explicitly selects `VERSION=4.7.0`, the BSIM4 kernel implemented
by neospice's BSIM4v7 device. The ngspice release version and the BSIM model
version are separate: the reference simulator is ngspice 47.

Build the checksum-pinned reference using
[`build-ngspice47-reference.sh`](../../scripts/build-ngspice47-reference.sh).
Its default compiler flags retain debug symbols. From the repository root,
with `NGSPICE47_ROOT` pointing to that build's output directory, run:

```sh
SPICE_SCRIPTS="$NGSPICE47_ROOT/cli/share/ngspice/scripts" \
  gdb -q -batch -x tests/goldens/capture_bsim4v7.gdb --args \
  "$NGSPICE47_ROOT/cli/bin/ngspice" -b tests/goldens/probe.cir
```

The script reads 13 preprocessing fields at the first `BSIM4v7load` call,
after setup and temperature processing, then lets the simulation complete.
It does not patch the simulator or modify its circuit data. `probe.cir` also
prints the drain current at full precision. The JSON fixture records these
values; the setup and load unit tests retain their existing tolerances.

The [capture record](../../docs/evidence/joss/2026-09-11-ngspice47-goldens.json)
contains hashes and commands, with the complete debugger output alongside it.
