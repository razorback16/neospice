# ngspice 42 aborted-analysis leak

The reference-wrapper test
`NgspiceLibrary.RejectsAbortedAnalysisWithPartialPlot` intentionally runs
conflicting ideal voltage sources. The simulation must fail; a partial plot
must not be accepted as a valid reference result. With leak detection enabled,
the test also reports 451 leaked bytes in seven allocations in the local
ngspice 42 shared library after cleanup.

[The standalone reproducer](../tools/repro_ngspice42_abort_leak.cpp) uses only
the ngspice shared-library API and reproduces the same allocation totals.
It does not link the neospice engine or reference wrapper. This establishes
upstream ownership of this observed abort-path leak; it does not establish
that all library or simulator paths are free of other leaks.

```sh
c++ -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  tools/repro_ngspice42_abort_leak.cpp -lngspice \
  -o /tmp/neospice-ngspice42-abort-leak
ASAN_OPTIONS=detect_leaks=1 /tmp/neospice-ngspice42-abort-leak
```

Observed September 10, 2026, with the system `libngspice.so.0` version 42:
exit status 1, `451 byte(s) leaked in 7 allocation(s)`.
The unsanitized shared library appears in the allocation stacks. Exact byte
counts may vary by library build. Local initial diagnostic log:
`/tmp/neospice-joss-ngspice-abort-leak.log`.

LeakSanitizer needs a runtime where ptrace restrictions do not prevent its
process inspection. The observed run used an approved execution outside that
sandbox. No leak suppressions or disabled leak detection were used. Keep the
failure-path regression and this sanitizer finding visible; a successful
functional assertion is not a clean sanitizer result. Testing the separately
pinned newer ngspice baseline remains pending under the JOSS goal.

## Additional transmission-line finding

Expanded sanitizer coverage at checkpoint 4 also found a 1,560-byte leak in one
allocation after `NgspiceCompareTest.TlineIC` passes its functional assertions.
The allocation stack enters ngspice's `realloc` path. The
[file-based standalone reproducer](../tools/repro_ngspice42_file_leak.cpp), which
links only libngspice, produces the same leak for the unchanged fixture:

```sh
c++ -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  tools/repro_ngspice42_file_leak.cpp -lngspice \
  -o /tmp/neospice-ngspice42-file-leak
ASAN_OPTIONS=detect_leaks=1 /tmp/neospice-ngspice42-file-leak \
  tests/circuits/tline_ic.cir
```

Observed exit status 1 and `1560 byte(s) leaked in 1 allocation(s)` with ngspice
42. The log is retained in checkpoint 4 evidence and locally at
`/tmp/neospice-joss-checkpoint4-ngspice-tline-leak.log`. This is an additional
upstream lifetime defect exposed by broader coverage, not an ASan finding in
neospice's transmission-line implementation. No suppressions were added.

## Checkpoint 6 expanded line coverage

The expanded selection also reports leaks after the DC, AC and no-UIC line
tests pass their functional assertions. The same file-based reproducer, still
linking only libngspice, independently reproduces each new fixture's leak:

| Fixture | Leaked bytes | Allocations | Exit status |
|---|---:|---:|---:|
| `tests/circuits/tline_dc.cir` | 120 | 1 | 1 |
| `tests/circuits/tline_ac.cir` | 120 | 1 | 1 |
| `tests/circuits/tline_ic_no_uic.cir` | 1,440 | 1 | 1 |

Use the same command above, substituting the fixture path. Logs are embedded
in checkpoint 6 evidence and retained locally as
`/tmp/neospice-joss-checkpoint6-ngspice-<fixture-name>-leak.log`.
Two separate tests use the no-UIC fixture and each reports its own leak; these
are separate process executions, not two distinct source-level defects.
Together with the original IC line and abort cases, six selected test processes
report leaks. All retain leak detection and their assertions. The expanded
sanitizer result is not clean, and a current ngspice baseline still needs testing.

## Checkpoint 7 sensitivity finding

The repaired `Sens.NgspiceComparison` reads and compares actual shared-library
results. It passes the numerical assertions, then reports a 10-byte leak in
three allocations. The same standalone file-based program reproduces that
total for `tests/circuits/sens_divider.cir` with no neospice code linked:

```sh
ASAN_OPTIONS=detect_leaks=1 /tmp/neospice-ngspice42-file-leak \
  tests/circuits/sens_divider.cir
```

Observed exit status 1; local log:
`/tmp/neospice-joss-checkpoint7-ngspice-sens-leak.log`. The log is embedded in
checkpoint 7 evidence. This newly exercised library path is an additional
upstream leak finding, not a reason to restore the old test that ignored
ngspice's output. No suppression or disabled leak detection was used.

## Checkpoint 8 noise propagation finding

The new matched-line noise comparison passes its functional assertions and
reports 120 leaked bytes in one allocation. The standalone libngspice-only
program reproduces that total for `tests/circuits/noise_tline_matched.cir`:

```sh
ASAN_OPTIONS=detect_leaks=1 /tmp/neospice-ngspice42-file-leak \
  tests/circuits/noise_tline_matched.cir
```

Observed exit status 1; log:
`/tmp/neospice-joss-checkpoint8-ngspice-noise-tline-leak.log`. The checkpoint 8
artifact retains this output. This result is separate from the neospice
BSIM3/BSIM4 allocation-pairing defect found during the expanded sanitizer run:
that defect belongs to neospice and its generator, and is repaired by matching
`TMALLOC`'s `new[]` with `delete[]`. BSIMSOI uses `malloc` for its list and
retains `free`. Reference leaks are not suppressed.

## Checkpoint 8 expanded BSIM coverage

The full BSIM3/BSIM4 selection exposed additional reference-library findings.
The file-based standalone program reproduces the following with ngspice 42:

| Fixture under `tests/circuits/` | Leaked bytes | Allocations |
|---|---:|---:|
| `bsim3_nmos_dc.cir` | 184 | 6 |
| `bsim3_nmos_iv.cir` | 184 | 6 |
| `bsim3_pmos_dc.cir` | 184 | 6 |
| `bsim3_nmos_ac.cir` | 184 | 6 |
| `bsim3_cmos_inverter.cir` | 368 | 12 |
| `bsim3v32_nmos_dc_op.cir` | 184 | 6 |
| `bsim3v32_nmos_ac.cir` | 184 | 6 |

All exit with status 1. Reproducer logs are embedded in checkpoint 8 evidence.
These totals describe individual processes, not independent root causes.

Two tests also stop on a **heap-buffer-overflow inside libngspice while loading
a circuit**, before their functional checks complete. The standalone program
reproduces a 65-byte read from a 64-byte allocation using the exact paths:

```sh
ASAN_OPTIONS=detect_leaks=1 /tmp/neospice-ngspice42-file-leak \
  /home/subhagato/Codes/neospice/tests/circuits/nmos_cs_amp_ac.cir
ASAN_OPTIONS=detect_leaks=1 /tmp/neospice-ngspice42-file-leak \
  /tmp/bsim4v7_audit_explicit_VTH0_U0_TOXE_W1u_saturation_high.cir
```

The second fixture is generated by `BSIM4v7_DC_Audit_BiasRegions`; checkpoint 8
retains its contents. The relative path for the first fixture and a shorter
audit path do not trigger this report. This establishes path-sensitive
reference-library behavior; it does not yet identify the source-level repair.
The original tests keep their paths and remain failed. Logs:
`/tmp/neospice-joss-checkpoint8-ngspice-nmos-ac-absolute.log` and
`/tmp/neospice-joss-checkpoint8-ngspice-bsim-audit-overflow.log`.

After repairing neospice's allocation pairing, shared-list replacement and
the standalone model test's cleanup, the 295-test sanitizer selection retains
one waveform failure, 15 reference leak reports and these two reference buffer
overflows. This is not a clean sanitizer result. Reference-version comparison,
upstream source diagnosis and the release decision remain open.

## Additional MES reference-path reproduction (checkpoint 10)

The nine-fixture MES noise test exposes the same 65-byte read from a 64-byte
allocation in libngspice's source-command path. The standalone program, with
no neospice or test wrapper linked, reproduces it for the exact absolute path
`/home/subhagato/Codes/neospice/tests/circuits/mes_noise_area.cir`.
The other eight MES files complete the standalone lifecycle without a sanitizer
report. These executions diagnose ownership and memory behavior; they do not
replace the reference test's numerical assertions.

The original comparison test keeps its filenames and fails under sanitizers
before completing all nine cases. Full Release comparison passes. The current
306-test sanitizer selection has two numerical failures, 15 reference leak
reports and three reference buffer-overflow failures. No suppression or path
shortening is applied. A separate neospice-only sanitizer driver exercises all
nine files and requires successful status, 28 samples and finite positive output,
input and device densities; its result and source are retained in checkpoint 10.
This driver is a memory check, not an independent accuracy comparison.

Logs: `/tmp/neospice-joss-checkpoint10-ngspice-mes_noise_*.log` and
`/tmp/neospice-joss-checkpoint10-mes-sanitize.log`. The new standalone output is
embedded in [checkpoint 10 evidence](evidence/joss/2026-09-10-validation-10.json).
