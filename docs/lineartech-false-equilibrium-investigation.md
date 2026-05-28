# LinearTech Op-Amp False Equilibrium — Investigation Findings

## RESOLVED (2026-05-28)

The root cause was **NOT** pivot order / LU roundoff as hypothesized below. It was
a bug in `Resistor`: any resistance below 1 mΩ was clamped up to `1e-3 Ω`
(`src/devices/resistor.cpp`). The LT op-amp output stage has `RC 17 0 1.063E-04`
(≈9409 S) balanced against `GC` (VCCS, gain 9408). Clamping RC to 1e-3 Ω
(1000 S) destroyed that balance by ~9.4×, driving node 17 (and the output) into
the wrong basin. ngspice (`restemp.c`) only substitutes 1 mΩ when **no**
resistance is given; a specified value is used directly.

Confirmed by tracing the DC Newton trajectory in an instrumented ngspice debug
build vs neospice: the two diverged at iteration 1 (neospice node 17 was 10×
ngspice's), and a minimal `GC`/`RC` test reproduced V(17)=9.408 (neospice) vs
1.0 (ngspice). Fix: only guard against a zero/non-finite resistance. After the
fix the standard test gives v(out)=1.00002, matching ngspice; all 992 unit tests
pass. The analysis below is retained for historical context but its root-cause
conclusion is superseded.

## Problem Statement

61 LinearTech op-amp models (LT1012, LT1007, LT1001, etc.) converge to a **false equilibrium** (v(out)≈-0.163V) instead of the correct operating point (v(out)=1.0V for the standard test circuit). Both neospice and ngspice use direct Newton—ngspice converges correctly purely due to different roundoff from its SPARSE 1.3 Markowitz solver.

## Circuit Topology (LT1012 Example)

```
V+ = 5V, V- = -5V, V(in_p) = 0.5V, V(in_m) via feedback resistors

Internal subcircuit path:
  Q1/Q2 diff pair → GA(VCCS, gm=1.131e-4) → node 8 (R2=100kΩ)
                  → GB(VCCS, gain=196) → node 1
                  → RO1(100Ω) → output (node 6)

Feedback/clamping:
  GC(VCCS, gain=9408): V(output) → current into node 17
  RC(1.063e-4 Ω): node 17 to ground (conductance = 9409 S)
  D1(1→17), D2(17→1): clamp diodes (Is=1.179e-19)
```

Key parameters: GC gain (9408) and RC conductance (9409 S) dominate the circuit. Node 17 is an extremely stiff node.

## Root Cause Analysis

### Why ngspice succeeds
ngspice uses SPARSE 1.3 with Markowitz pivoting (threshold 0.001, different tie-breaking from our solver). The different factorization order produces different roundoff in the Newton direction, steering early iterations toward the correct basin. No convergence aids are used—ngspice's `CKTnodeDamping=0` by default.

### What happens in neospice (direct Newton)
The Newton iteration trace reveals:

| Iter | v(out_net) | v(x1.17) | max_diff | Note |
|------|-----------|-----------|----------|------|
| 1    | 0.036     | 0.342     | —        | JCT→FIX transition |
| 5    | 0.531     | 2.257     | 13.7     | x1.17 growing |
| 9    | **1.002** | **9.424** | 0.026    | Output correct! Node 17 wrong |
| 10-12| ~1.0      | ~9.4      | 0.023    | Slowly converging... |
| 13   | —         | —         | **2.37** | Sudden divergence |
| 14+  | —         | —         | 2.37+    | Limit cycle |

**Critical observation**: Direct Newton reaches v(out)=1.0 at iteration 9, but node x1.17 is at 9.4V (should be ~1.0V). The diode D2 (anode=17, cathode=1) sees ~8.4V forward bias—physically impossible. Iterations 10-12 nearly converge (md=0.023) but then a sudden jump at iter 13 triggers a limit cycle.

### The false equilibrium mechanism
1. GC produces enormous current into node 17 proportional to v(out)
2. Node 17's voltage should be clamped by D1/D2 to within ~0.7V of node 1
3. During Newton, DEVpnjlim limits the **linearization voltage** but not the actual node voltage
4. Node 17 accumulates voltage across iterations (each step adds ~1.5V)
5. Once x1.17 >> v(node 1), D2 is numerically saturated and its Jacobian contribution becomes negligible
6. The system settles into a false fixed point where D2 "thinks" it's forward-biased but the actual physics are violated

## Approaches Tried

### 1. Node Damping (3.5V threshold)
**Result**: Fails. Limits per-iteration change to 3.5V but accumulation over 8+ iterations still pushes x1.17 past the crossover. Even at 1.5V threshold, same issue—just slower.

### 2. Oscillation Detection + Best-Solution Revert
**Result**: Partially works—detects the divergence at iter 13 and reverts. But the "best" solution at iter 9-12 isn't actually converged (md=0.023 > tolerance). Stuck in a revert loop.

### 3. Pseudo-Transient Continuation (PTC)
Multiple sub-approaches tried:

- **PTC from zero IC**: Large G_pseudo crushes all nodes to 0. Companion G*V_prev=G*0=0 provides no drive. Solution never evolves from zero.
- **PTC with warm start (from gmin stepping result)**: V_prev≈-0.163V (the wrong equilibrium). Companion reinforces the wrong basin. Reducing G leads back to divergence.
- **PTC with INITJCT→INITFLOAT transition**: Mode switch + G reduction simultaneously causes failure. Fixed by keeping same G during transition, but subsequent G reduction still fails.
- **Aggressive final probes**: Tried probing (remove companion, run plain Newton) at G=954 and G=0.93. v(out_net)≈0 at both—PTC never found the correct basin.

**Fundamental PTC limitation**: For this circuit, the false equilibrium at v(out)≈0 is the nearest equilibrium from zero. PTC's backward Euler companion with zero initial conditions naturally settles there.

### 4. Gain Stepping (combined source + gain)
**Result**: Gets to ~35.7% of full gain then hits a **bifurcation**. Steps below 1e-7 fail. Jump-to-full from 35.7% also fails.

**Hypothesis**: Near dep_src_fact≈0.44, the internal diodes D1/D2 switch state (off→on), creating a discontinuity that Newton can't track. The Jacobian becomes singular at the bifurcation point.

### 5. diag_gmin fix (applied to node variables only)
**Result**: Fixed a correctness bug (was applying gmin to branch current rows, corrupting voltage source equations). But 1e-12 on diagonal is negligible for this problem.

### 6. Supply-rail clamping
**Result**: Ineffective. Rail_max = max_node_value + 1.0 ≈ 6V. But x1.17's correct value (1.0V) is well within rails—the issue is the intermediate state during Newton, not the final value.

## Open Problems

### P1: Newton doesn't monotonically converge from the correct basin
At iter 9, v(out)=1.0 (correct) and md=0.026. But Newton can't complete convergence because x1.17 is at 9.4V. A **backtracking line search** would detect that the full Newton step at iter 13 increases the residual and scale back, potentially allowing convergence from the iter 9 state.

### P2: No convergence aid escapes the zero/false basin
All current aids (gmin stepping, source stepping, gain stepping, PTC) end up near v(out)≈0 or ≈-0.163V. We need either:
- **OPtran (transient-based OP)**: Run actual transient from t=0 with added capacitances. Circuit's parasitic caps provide physical damping. Most SPICE simulators use this as last resort.
- **Continuation past bifurcation**: Arc-length continuation or PTC specifically at the gain-stepping stuck point (~35% gain).
- **Randomized perturbation**: Try Newton from multiple random initial conditions. If any perturbation lands in the correct basin, Newton converges.

### P3: Pivot order affects basin selection
The core difference with ngspice is the LU factorization pivot order. Options:
- Try **full pivoting** (`numeric_fullpivot`) during direct Newton for these circuits
- Try **random pivot perturbation**: add tiny random noise (1e-15 scale) to matrix entries before factorization to change tie-breaking
- Implement a second pivot strategy (e.g., different Markowitz threshold) and try both

### P4: DEVpnjlim doesn't prevent unphysical junction voltages
The pnjlim mechanism limits the linearization voltage for I-V calculation but doesn't prevent the actual node voltage from growing unboundedly. A hard clamp on Vd = V(anode) - V(cathode) within the Newton iteration might help, but could also prevent convergence for legitimately large reverse-bias scenarios.

## Recommended Next Steps (in priority order)

1. **Backtracking line search in Newton** (addresses P1):
   - After computing Newton step Δx, evaluate ||f(x + Δx)||
   - If ||f(x + Δx)|| > ||f(x)||, try α=0.5, 0.25, etc.
   - This prevents the overshoot at iter 13 and may allow convergence from iter 9's state
   - Low risk of regression (line search can only reduce step, never increase)

2. **OPtran implementation** (addresses P2):
   - Add virtual capacitors (C=1e-12 F) to all nodes
   - Run 100-200 backward Euler steps with progressively larger dt
   - Uses circuit physics (KVL/KCL with time evolution) to find correct steady state
   - This is how ngspice's `-o` option works and is standard in commercial SPICE

3. **Alternative pivot ordering** (addresses P3):
   - Try `numeric_fullpivot` for DC OP when direct Newton fails
   - Or implement Markowitz with threshold=0.001 (ngspice's value; ours is 0.1)
   - Test if different pivot ordering reaches correct basin

4. **Junction voltage hard limit** (addresses P4):
   - In diode `limit_voltages()`, clamp V(anode)-V(cathode) to [-100Vt, +40Vt]
   - Prevents the unphysical x1.17=9.4V state from forming
   - Requires careful testing to avoid breaking legitimate circuits

## Current State of Code

Modified files with experimental/debug changes:
- `src/core/newton.cpp`: 3.5V damping, oscillation detection, debug prints (n≤25), PTC damping skip
- `src/core/convergence.cpp`: PTC with warm start, JCT→FLOAT transition handling, debug prints
- `src/core/dc.cpp`: diag_gmin = opts.gmin for direct attempt

These changes should be reviewed and cleaned up before proceeding with any of the recommended approaches.

## Test Commands

```bash
# Build
cd build && cmake --build . -j$(nproc)

# Test single LT1012
./build/neospice /tmp/test_lt1012_verbose.cir

# Run KiCad comparison (all 5000 models)
python3 tools/compare_kicad_models.py --max 5000 --save results/compare_5k.json --jobs 8

# Run specific LinearTech models
python3 tools/compare_kicad_models.py --filter "LinearTech" --save results/lt_only.json --jobs 8
```
