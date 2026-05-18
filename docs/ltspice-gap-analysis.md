# LTSpice / QSPICE Gap Analysis

Feature comparison against LTSpice XVII and QSPICE (its successor).
Conducted April 2026.

## Analysis Types

| Feature | LTSpice | neospice | Status |
|---------|---------|----------|--------|
| DC Operating Point (`.op`) | Yes | Yes | Parity |
| DC Sweep (`.dc`) | Yes | Yes | Parity |
| Transient (`.tran`) | Yes | Yes | Parity |
| AC Small-Signal (`.ac`) | Yes | Yes | Parity |
| Noise (`.noise`) | Yes | Yes | Parity |
| Transfer Function (`.tf`) | Yes | Yes | Parity |
| Sensitivity (`.sens`) | Yes | Yes | **neospice-only** (LTSpice lacks this) |
| Pole-Zero (`.pz`) | Yes | Yes | **neospice-only** (LTSpice lacks this) |
| Fourier / THD (`.four`) | Yes | Yes | Parity |
| Monte Carlo | `mc(nom,tol)` + `.step` | Not yet | **Gap** |
| Worst-Case Analysis | Manual via `.function` | Not yet | **Gap** |
| FRA (Frequency Response Analysis) | Feedback loop analysis via transient | No | Minor gap (niche) |
| POP (Periodic Operating Point) | Limited to ADI models | No | Minor gap |
| Distortion (`.disto`) | No | No | Neither has it |

## Device Models — MOSFETs

| Model | Level | LTSpice | neospice | Status |
|-------|-------|---------|----------|--------|
| MOS1 (Shichman-Hodges) | 1 | Yes | Yes | Parity |
| MOS2 (SPICE2) | 2 | Yes | No | Minor — legacy |
| MOS3 (semi-empirical) | 3 | Yes | Yes | Parity |
| BSIM1 | 4 | Yes | No | Minor — legacy |
| BSIM2 | 5 | Yes | No | Minor — legacy |
| MOS6 | 6 | Yes | No | Minor — legacy |
| MOS9 (temp-dependent) | 9 | Yes | Yes | Parity |
| BSIM3v3 | 8/49 | Yes | Yes (v3.24 + v3.3) | Parity |
| BSIMSOI | 9/10/58 | Yes | Yes | Parity |
| **EKV 2.6** | **12** | **Yes** | **No** | **Gap** — low-power analog |
| BSIM4 | 14/54 | Yes | Yes (BSIM4v7) | Parity |
| HiSIM_HV | 73 | Yes | Yes | Parity |
| HiSIM2 | 61/68 | No | Yes | **neospice-only** |
| **VDMOS** (power MOSFET) | — | **Yes (proprietary)** | **No** | **Gap** — power electronics staple |

## Device Models — BJTs

| Model | LTSpice | neospice | Status |
|-------|---------|----------|--------|
| Gummel-Poon | Yes | Yes | Parity |
| VBIC | Yes | Yes (levels 4/9/12/13) | Parity |
| **MEXTRAM** | **Yes** | **No** | **Gap** — RF/high-speed IC |
| **HiCUM** | **Yes** | **No** | **Gap** — RF/high-speed IC |

## Device Models — Other

| Model | LTSpice | neospice | Status |
|-------|---------|----------|--------|
| Diode (standard) | Yes | Yes | Parity |
| JFET (Shichman-Hodges) | Yes | Yes | Parity |
| JFET2 (Parker-Skellern) | No | Yes | **neospice-only** |
| HFET1 / HFET2 | No | Yes | **neospice-only** |
| Switches (S, W) | Yes | Yes | Parity |
| Transmission line (T) | Yes | Yes | Parity |
| Lossy line (LTRA / O) | Yes | Yes | Parity |
| Coupled inductors (K) | Yes | Yes | Parity |

## Directives & Netlist Features

| Feature | LTSpice | neospice | Status |
|---------|---------|----------|--------|
| `.param` / `.func` | Yes | Yes | Parity |
| `.subckt` / `.ends` | Yes | Yes | Parity |
| `.include` / `.lib` | Yes | Yes | Parity |
| `.global` | Yes | Yes | Parity |
| `.ic` / `.nodeset` | Yes | Yes | Parity |
| `.options` | Yes | Yes | Parity |
| `.step` (parameter sweeps) | Yes | Yes | Parity |
| `.meas` (measurements) | Yes | Yes (TRIG/TARG, FIND/WHEN, AVG/RMS/MIN/MAX/PP/INTEG, PARAM) | Parity |
| `.save` / `.print` / `.plot` | Yes | Yes | Parity |
| B-sources (behavioral) | Yes | Yes — with **auto-diff Jacobians** | **neospice is better** |
| `mc()` Monte Carlo function | Yes | No | **Gap** |
| S-parameter / Touchstone | Via s2spice conversion (indirect) | No | Minor gap |

## QSPICE Features (Industry Direction)

QSPICE is LTSpice's successor by the same author (Mike Engelhardt), now owned by Qorvo. These represent where the industry is heading.

| Feature | QSPICE | neospice | Priority |
|---------|--------|----------|----------|
| C++ behavioral model plugins | Native — compile C++ into DLL components | No | High — natural fit for neospice |
| Verilog-A device models | Via C++ compilation | No | High — industry standard for foundry PDKs |
| Mixed-signal / digital (XSPICE-like) | Yes | No | Medium |
| Multi-process simulation | Yes | No (single-threaded) | Medium — parallel sweeps planned |
| 80-bit extended precision | Optional QSPICE80 mode | No | Low |
| MEXTRAM 504 w/ self-heating | Yes | No | Medium |
| SiC / wide-bandgap models | Native | No | Niche |
| Model Generator (from datasheets) | Yes | No | Medium |

## neospice Advantages Over LTSpice

Features where neospice is ahead or differentiated:

1. **Auto-differentiation in B-sources** — exact symbolic Jacobians vs. numerical perturbation
2. **Pole-Zero analysis** (`.pz`) — LTSpice doesn't support this
3. **Sensitivity analysis** (`.sens`) — LTSpice doesn't support this
4. **Clean C++ API** — `Simulator`, `Circuit` with typed device methods, handle-based result accessors
5. **Circuit introspection** — `node_names()`, `device_names()`, `device_info()`, `set_param()`
6. **1.5–6x faster** than ngspice (direct in-process, no subprocess overhead)
7. **NeoSolver** — custom dense + sparse column-LU with AMD ordering, Markowitz pivoting, Gilbert-Peierls reach
8. **Advanced convergence** — 4-stage fallback (Newton → GMIN → source stepping → pseudo-transient)
9. **Trap ringing detection** — automatic Gear-2 fallback
10. **3 LTE reference modes** — per-node, max-all, max-per-signal (Xyce-inspired)
11. **HiSIM2, JFET2, HFET1/2** — device models LTSpice doesn't have

## Prioritized Gaps

### High Impact (widely used by LTSpice users)

1. **Monte Carlo / statistical analysis** — `mc()` function, Gaussian/uniform distributions, histogram/yield reporting. This is table-stakes for production IC design workflows.

2. **VDMOS power MOSFET model** — LTSpice's proprietary VDMOS is the #1 reason power electronics engineers use it. Every major power MOSFET vendor (Infineon, ON Semi, TI, etc.) ships VDMOS-format SPICE models.

3. **Verilog-A compiler** — the industry-standard portable device model format. Would unlock hundreds of foundry PDK models and let users add custom devices without modifying the simulator source.

4. **MEXTRAM / HiCUM BJT models** — critical for RF and high-speed IC design. MEXTRAM 504 is the NXP/Infineon standard; HiCUM is used by GlobalFoundries and IHP.

### Medium Impact

5. **EKV 2.6 MOSFET** — preferred model for low-power analog and education (simpler parameter extraction than BSIM).

6. **Worst-case / corner analysis** — systematic PVT (process/voltage/temperature) sweeps with min/max/nominal permutations.

7. **C++ behavioral model plugin system** — QSPICE's killer feature. neospice's C++ foundation makes this a natural extension. Load user-compiled `.so`/`.dll` as circuit elements.

8. **Python bindings** — already on roadmap. LTSpice has no API at all; this would be a major differentiator.

### Low Impact / Niche

9. **Legacy MOSFET levels** (MOS2, MOS6, BSIM1, BSIM2) — rarely used in new designs.
10. **S-parameter / Touchstone import** — LTSpice's own support is indirect via external tools.
11. **FRA (Frequency Response Analysis)** — specialized feedback loop stability analysis.
12. **POP (Periodic Operating Point)** — steady-state finder for switching circuits.
13. **80-bit extended precision** — edge case for extreme numerical sensitivity.
