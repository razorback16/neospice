<!-- Historical, unqualified measurements; preserved for audit only. -->
## Performance

The following timings are historical and lack the corrected accuracy-qualified measurement protocol required for the paper. They are retained for context, not as current performance claims.

Historical in-process measurements against ngspice-42 on Intel Core Ultra 9 285K, GCC 14, `-O3`. Both simulators were linked as libraries, but timed sections included file loading and sampling counts were not uniform across cases. ngspice used its default Sparse 1.3 path. See the [audit](docs/paper-readiness-audit.md) for measurement defects.

| Benchmark | ngspice | neospice | Speedup |
|---|---:|---:|---:|
| **Parse** THS4131 (77 nodes) | 435 us | 312 us | 1.4x |
| **Parse** resistor divider | 45 us | 11 us | 4.1x |
| **DC OP** THS4131 (14 BJTs) | 623 us | 483 us | 1.3x |
| **DC OP** resistor divider | 54 us | 10 us | 5.4x |
| **AC** THS4131, 81 points | 1.04 ms | 706 us | 1.5x |
| **AC** THS4131, 8001 points | 22.35 ms | 23.33 ms | 1.0x ngspice |
| **AC** RC lowpass, 91 points | 120 us | 18 us | 6.7x |
| **Transient** RC lowpass, 500 us | 1.17 ms | 284 us | 4.1x |
| **Transient** RLC series, 100 us | 1.66 ms | 397 us | 4.2x |
| **Transient** pulse source, 100 us | 1.02 ms | 118 us | 8.6x |
| **Noise** resistor divider, 91 pts | 91 us | 54 us | 1.7x |
| **DC sweep** V1, 1001 pts | 847 us | 207 us | 4.1x |
| **E2E** THS4131 (.op + .ac) | 746 us | 633 us | 1.2x |
| **E2E** OPA1632 (.op + .ac) | 6.72 ms | 3.59 ms | 1.9x |

See [docs/performance-comparison-with-ngspice.md](docs/performance-comparison-with-ngspice.md) for the full methodology and results.

