# ML-Based Initial Guess for DC Convergence

## Problem Statement

neospice currently initialises every Newton-Raphson solve with all node voltages
set to zero. This is the worst possible starting point for most real circuits: a
5 V power rail is obviously not 0 V, a MOSFET drain in a biased amplifier is
nowhere near 0 V, and a latch that snaps to one of two stable states needs a
guess that already breaks its symmetry.

A bad initial guess has two consequences:

1. **Slow convergence** — Newton takes 20–50 extra iterations walking from 0 V
   toward the true operating point.
2. **Divergence** — for circuits with strong positive feedback (latches, Schmitt
   triggers, oscillators at startup, regulators with high loop gain) Newton never
   finds the basin of attraction at all and falls back to gmin/source stepping or
   fails entirely.

neospice currently fails on 419 of ~35,000 KiCad SPICE library circuits. The
majority are DC convergence failures, not bugs. A better initial guess would
reduce or eliminate most of these.

---

## What ngspice Actually Does (and Why It's Still Not Enough)

Before designing an ML solution it is worth understanding what ngspice does for
initialisation, because neospice already matches this behaviour.

### The three-phase initialisation sequence

ngspice does not start from all zeros. It uses a three-mode Newton cascade:

**MODEINITJCT (first iteration)** — each device is called in "junction guess"
mode and is allowed to set its own terminal voltages to something physically
plausible, independently of the global solution vector. Key examples:

- **MOS1** (`mos1_load.cpp:344`): if no `.ic` is given, sets
  `vbs = −1 V`, `vgs = Vto` (threshold voltage), `vds = 0`.  The body-source
  junction is reverse-biased and the gate is placed exactly at threshold so the
  device is on the edge of conduction — giving Newton a non-zero gradient to
  work with.
- **BSIM4v7** (`bsim4v7_load.cpp:263`): sets `vds = 0.1 V`,
  `vgs = Vth0 + 0.1 V`, `vbs = 0`. Gate is placed just above threshold.

If the device has an `.off` flag in the netlist, all terminals are forced to
zero instead, placing it explicitly in cutoff.

**MODEINITFIX (second iteration)** — devices read the previous iteration's
result to stabilise against large swings.

**MODEINITFLOAT (third+ iteration)** — normal Newton corrector with full
voltage limiting and convergence checks.

### Why it is still local and not global

The MODEINITJCT trick is **device-local, not circuit-global**. Each transistor
makes a sensible guess for its own terminals, but the guess is made in isolation.
The MOSFET does not know what voltage the circuit is actually delivering to its
drain. In a three-stage amplifier, the second stage's drain guess is still just
`Vth + 0.1 V` regardless of what the first stage computes.

This is precisely the gap an ML model can fill: a model that has seen thousands
of circuits can propagate supply information through the graph and produce a
globally consistent starting point — something MODEINITJCT cannot do.

---

## Prior Art

| Work | Year | Approach | Result |
|------|------|----------|--------|
| OSTI ML Power Flow Initialiser [1] | 2024 | Random Forest on grid topology features | 2,106 previously non-converging cases converged |
| GNN Analog Circuit Pretraining (Stanford) [2] | 2022 | GNN pretrained on node voltage prediction | 10× sample efficiency; generalises across topologies |
| BoA-PTA [3] | 2022 | Bayesian optimisation over solver hyperparameters | 2.2× average speedup, 3.5× maximum on 43 benchmarks |
| Warm-Start Fixed-Point Algorithms (JMLR) [4] | 2024 | Neural network → warm start, then fixed-point iterations | Significant reduction in iterations; convergence guarantees preserved |
| Masala-CHAI [5] | 2024 | LLM-generated SPICE netlists at scale | 7,500 captioned netlists; establishes dataset infrastructure |
| ZeroSim [6] | 2025 | Transformer-based zero-shot circuit evaluation | Zero-shot generalisation across 60+ topologies trained on 3.6M instances |
| KCLNet [7] | 2025 | Physics-informed GNN with KCL constraint loss | Improved out-of-distribution generalisation on power flow |

The circuit simulation problem is harder than power flow because circuits span
far more topology classes (digital, analog, RF, mixed-signal), device models are
more nonlinear, and the "correct" operating point is sometimes ambiguous
(bistable circuits). But the core idea transfers directly.

---

## Three Approaches (Increasing Complexity)

### Approach 1 — Rule-Based Heuristics

No ML. Pure graph traversal over the parsed circuit.

**Algorithm:**

1. Walk the netlist graph from every independent voltage source outward.
2. Assign every node a starting voltage based on its nearest voltage source and
   the type of components in the path:
   - Node directly shorted to a voltage source → init to source value
   - Node separated by a resistor → init to source value (resistors don't change
     DC voltage in isolation)
   - MOSFET drain in common-source configuration → init to VDD
   - MOSFET source → init to VSS (or VDD − Vgs_typ for source followers)
   - Differential pair tails → init to VDD/2
   - Op-amp output with negative feedback → init near the virtual ground of
     the inputs
3. For nodes with no clear path to a voltage source → leave at 0 V (safe default)

**Strengths:** Zero training data needed. Zero inference cost at runtime. Can
be implemented in a day. This is the heuristic "Automatic Node Classification"
approach, implemented as `compute_initial_guess()` in
`src/core/node_classify.cpp`.

**Weaknesses:** Only works for common topologies. Fails on unusual bias schemes
and circuits where the operating point is non-obvious from topology alone.

**Expected impact:** Solves ~30–40% of convergence failures with negligible
engineering risk.

**Files to modify:**
- `src/core/dc.cpp` — add `compute_heuristic_initial_guess(Circuit&)` call
  before the first Newton iteration
- `src/core/circuit.hpp` — expose `nodes_connected_to_source()` helper

---

### Approach 2 — Supervised Regression Model

Train a model on (circuit features → DC node voltages). Use it as the starting
point for Newton.

#### Feature Engineering (per node)

| Feature | How to compute |
|---------|---------------|
| Nearest voltage source value | BFS from node, record first V-source found |
| Hop distance to nearest V-source | BFS depth |
| Node degree | Number of components connected |
| Component type histogram | Count of R, C, L, MOSFET, BJT, Diode attached |
| Is gate / drain / source / base | MOSFET/BJT terminal type at this node |
| Supply rail flag | Is this node directly connected to a V-source? |
| In a feedback loop | Simple cycle detection |
| Estimated Thevenin voltage | Voltage divider from nearest sources through resistors |

#### Model Architecture

A simple **MLP (Multi-Layer Perceptron)** or **Gradient Boosted Trees** over
the per-node feature vector. Each node gets an independent prediction.

```
Input:  feature_vector(node_i)  ← ~10 scalar features
Output: predicted_voltage(node_i)  ← scalar
```

Because nodes are predicted independently, this scales linearly with circuit
size and requires no special graph architecture.

#### Training Data

Use neospice itself. For every circuit that **successfully converges**, store:

```
(circuit_features, dc_solution_voltages)
```

The neospice KiCad test suite already provides ~34,000 circuits. Running them
all produces a training set of ~500,000–5,000,000 node samples (depending on
average circuit size).

#### Training Procedure

1. Run all circuits through neospice. Collect (features, voltages) pairs for
   converged cases only.
2. Normalise voltages by the circuit's maximum supply voltage (makes the model
   supply-agnostic: a 3.3 V and a 5 V circuit look similar).
3. Train an MLP: 3 hidden layers × 128 units, ReLU activations, MSE loss.
4. Validate on held-out circuits (not individual nodes — hold out entire circuits
   to test generalisation to unseen topologies).

#### Safety

The model is purely advisory. If Newton diverges from the ML guess, the existing
convergence fallbacks (gmin stepping, source stepping, pseudo-transient) still
fire. The ML guess makes them needed less often — it does not replace them.

**Expected impact:** Solves ~50–60% of remaining convergence failures after the
rule-based heuristic. Reduces average Newton iteration count by ~30%.

---

### Approach 3 — Graph Neural Network

A GNN treats the circuit as a graph directly — no hand-crafted features. This
is the recommended long-term approach and the focus of the rest of this document.

---

## GNN Architecture — Detailed Design

### Why a GNN?

A circuit is fundamentally a graph. The voltage at a node depends on its
neighbours (KCL), which depend on their neighbours, and so on. Message-passing
GNNs replicate this propagation exactly:

- Each message-passing round aggregates information from neighbours — identical
  in spirit to Gaussian elimination spreading information through the MNA matrix.
- After K rounds (K ≈ circuit diameter), every node has seen the whole circuit.
- The model learns that "MOSFET drain connected to supply through resistor ≈
  bias at VDD/2" as a transferable pattern that generalises across topologies.

### Bipartite Graph — Not Node-Only

The natural but wrong approach is: circuit nodes = graph nodes, wires = graph
edges. The problem: a MOSFET has 4 terminals. You cannot represent it as a
single edge without losing information about which terminal is gate vs drain.

The correct approach is a **bipartite graph** where both circuit nodes and
components are graph nodes. Edges encode terminal connections:

```
Circuit node "drain" ──[DRAIN]──> MOSFET M1 <──[GATE]── Circuit node "gate"
                                      |
                                   [SOURCE]
                                      |
                               Circuit node "source"
```

This is used by CktGNN [8] and Circuit-GNN [9]. Each component gets its own
embedding and edges carry terminal-role information. The model learns that
being connected to a MOSFET gate is fundamentally different from being connected
to a drain or a resistor terminal.

### The Normalisation Problem — Critical for Generalisation

Component values in real circuits span ~18 orders of magnitude:

```
Resistors:   1 mΩ (PCB trace)  →  100 GΩ (ESD protection)
Capacitors:  0.1 fF (gate cap) →  100 mF (bulk decoupling)
Voltages:    1 mV (noise)      →  600 V  (power electronics)
```

Without careful normalisation the model memorises absolute values instead of
learning circuit structure. Three rules:

**a) Log-scale all component values:**
```python
feature = log10(abs(value))
# 1 kΩ → 3.0,   10 MΩ → 7.0,   1 fF → −15.0
```

**b) Normalise output voltages to supply fraction:**
```
predicted_output = V_node / V_supply   # range [−1, +1] for dual supply
```
At inference time: `V_node = predicted × V_supply`. A 3.3 V circuit and a 5 V
circuit with the same topology look identical to the model.

**c) Use ratios for component values where possible:**
Encode `R2 / R1` rather than `R1` and `R2` independently. Ratios are
scale-invariant and transfer across supply voltages. ZeroSim [6] uses exactly
this normalisation strategy to achieve zero-shot generalisation across 60+
amplifier topologies.

### Node and Edge Features

**Component node features (one per component):**

| Feature | Encoding |
|---------|----------|
| Component type | One-hot: {R, C, L, MOSFET_N, MOSFET_P, BJT_NPN, BJT_PNP, Vsource, Isource, Diode, Switch, ...} |
| Primary value | `log10(value)` — resistance, capacitance, etc. |
| Secondary params | `log10(Vth)` for MOSFET, `log10(Is)` for diode, `log10(beta)` for BJT |
| `.off` flag | Binary — device starts in cutoff |

**Circuit node features (one per node):**

| Feature | Encoding |
|---------|----------|
| Is ground (node 0) | Binary — ground is always 0 V, a hard anchor |
| Is supply node | Binary + `V_supply / V_supply_max` (normalised) |
| Node degree | Integer — how many components connect here |

**Edge features (one per terminal connection):**

| Feature | Encoding |
|---------|----------|
| Terminal role | One-hot: {DRAIN, GATE, SOURCE, BULK, ANODE, CATHODE, PLUS, MINUS, BASE, COLLECTOR, EMITTER} |
| Is controlling terminal | Binary — gate/base vs output terminal |

The terminal role encoding is the single most important edge feature. A node
connected to a MOSFET gate sees very high impedance and has a different voltage
character than a node connected to a drain. Without this the model cannot
distinguish them and will predict incorrect voltages.

### Message Passing Architecture

**Recommended: Heterogeneous Graph Attention Network (HGATv2)**

Standard homogeneous GNNs treat all nodes identically. Our bipartite graph has
two node types (circuit nodes and component nodes) and many edge types (terminal
roles). A heterogeneous GNN handles this with separate weight matrices per type.

The two-phase message-passing loop:

```
Round 1: Each component aggregates features from its connected circuit nodes
         → component embedding = "what voltages are around me"

Round 2: Each circuit node aggregates from all connected components
         → node embedding = "what components are attached and how"

Round 3+: Information propagates further through the graph
          → a node 3 hops from the supply rail learns about that supply
            through chained message passing
```

**Hyperparameters:**

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Message-passing rounds | 6 | Covers diameter of typical analog circuits (4–8 hops) |
| Hidden dimension | 128 | Sufficient per Stanford paper [2]; larger yields diminishing returns |
| Attention heads | 4 | Standard for GATv2 |
| Dropout | 0.1 | Light regularisation |

### Physics-Informed Loss (KCL Regularisation)

Training uses supervised MSE loss plus a KCL regularisation term that requires
no extra labels — only the predicted voltages and component values:

```
L_supervised = || V_predicted − V_true ||²

For each circuit node i:
    I_net(i) = Σ (predicted current through each attached component)
    L_KCL    = I_net(i)²        ← should be zero by KCL

L_total = L_supervised + λ × mean(L_KCL)    where λ = 0.01
```

For a resistor: `I = (V_node1 − V_node2) / R`. The KCL term is computed
analytically from predicted voltages — no simulation required. KCLNet [7] shows
this significantly improves out-of-distribution generalisation by preventing the
model from predicting physically impossible voltage distributions.

### Full Architecture Specification

```
Input:
  Bipartite graph G = (V_circuit ∪ V_components, E_terminals)
  Circuit node features:   4-dim  (ground flag, supply flag, supply_norm, degree)
  Component node features: 8-dim  (type one-hot, log_value, log_secondary, off_flag)
  Edge features:          16-dim  (terminal role one-hot, controlling_flag)

Model:
  Heterogeneous GATv2
  6 message-passing rounds (circuit→component→circuit alternating)
  Hidden dim: 128
  Attention heads: 4
  Dropout: 0.1

Output:
  1 scalar per circuit node: normalised voltage V_node / V_supply

Loss:
  L = MSE(V_pred, V_true) + 0.01 × KCL_residual

Training:
  Optimiser: AdamW, lr=1e-3, weight decay=1e-4
  Schedule: cosine decay, 100 epochs
  Batch size: 32 circuits (variable-size graphs via PyTorch Geometric DataLoader)
  Hardware: ~2–4 hours on V100, ~30 min on A100

Inference:
  ~0.5 ms per 100-node circuit on CPU
  ~50 µs on GPU
  Export: ONNX for embedding in neospice C++ runtime
```

---

## Training Pipeline — Three Phases

Supervised training alone is insufficient because its objective is misaligned
with what we actually care about. The supervised loss minimises the distance
between predicted and true voltages, but the true objective is Newton-Raphson
convergence speed and success rate. A prediction 0.5 V off the true answer
might converge in 3 Newton iterations because it lands in a wide, smooth basin;
a prediction only 0.1 V off might land near a saddle point and take 50
iterations. The MSE loss cannot distinguish these.

This is the same misalignment that motivated RLHF for LLMs: supervised
pretraining gives a plausible model, RL fine-tuning aligns it to the actual
objective. The three phases below follow the same logic.

### Phase 1 — Supervised Pretraining

```
Loss = MSE(V_predicted, V_true)
```

Trains the GNN to accurately predict DC node voltages from circuit structure.
Fast, stable, and data-efficient. Gives the model a good initialisation for the
subsequent phases. Runs on the full 34K KiCad corpus + supplementary datasets.

### Phase 2 — Residual Fine-Tuning (Differentiable, No RL Needed)

Before running Newton, compute how well the predicted voltages satisfy KCL —
the **initial residual norm**:

```
R(x₀) = || F(x₀) ||    ← sum of KCL violations at all nodes
```

For a resistor: `I = (V₁ − V₂) / R`. For a MOSFET: use its MODEINITJCT
current model. The residual is **differentiable with respect to predicted
voltages** — we can backpropagate through device evaluations (or use finite
differences for highly nonlinear devices).

```
Loss_phase2 = MSE(V_pred, V_true) + λ · || F(V_pred) ||
```

λ is increased progressively over training until the physics constraint
dominates. The model is pushed toward predictions that are not just close to
the true voltages numerically, but are also physically self-consistent with the
circuit's KCL equations — which is what determines whether Newton converges.
This phase does not require running Newton-Raphson at all.

### Phase 3 — REINFORCE on Actual Newton Convergence (True RL)

The simulator itself becomes the reward oracle. This is a **contextual bandit**
problem (one-step RL), not a multi-step MDP: each circuit is a context, the
action is the initial voltage guess, and the reward is the Newton convergence
quality. There is no temporal credit assignment and no discount factor.

```
Policy:   π_θ(circuit) = GNN_θ(circuit) + ε,   ε ~ N(0, σ²I)
Reward:   r = −log(iterations)      if Newton converges
          r = −50                    if Newton fails
Update:   θ ← θ + α · ∇_θ log π_θ(a) · (r − b)
```

where `b` is a **baseline** (running average of recent rewards) that reduces
variance. The Gaussian noise `ε` provides exploration — without it the
deterministic GNN always produces the same voltage and the policy gradient is
zero. Start with `σ = 0.5 V` (wide exploration), decay to `σ = 0.05 V` over
training.

To prevent catastrophic forgetting of the supervised pretraining, add a KL
penalty keeping the RL policy close to the supervised policy — exactly like
PPO's clipping or RLHF's KL regularisation:

```
Loss_RL = −E[r] + β · KL(π_θ || π_supervised)
```

#### Training Loop

```python
for circuit in curriculum:            # start with the 419 currently failing circuits
    V_base = GNN(circuit)             # deterministic base prediction

    rewards, log_probs = [], []
    for _ in range(N_samples):        # N = 10–20 for variance reduction
        eps    = torch.randn_like(V_base) * sigma
        V_init = V_base + eps

        result = neospice.solve_dc(circuit, initial_guess=V_init)
        r      = -log(result.iterations) if result.converged else -50

        rewards.append(r)
        log_probs.append(normal_log_prob(eps, mean=0, std=sigma))

    advantage = tensor(rewards) - mean(rewards)           # baseline subtraction
    kl        = kl_divergence(V_base, V_supervised)       # catastrophic forgetting guard

    loss = -(log_probs * advantage).mean() + beta * kl
    loss.backward()
    optimizer.step()

sigma *= decay_rate                   # anneal exploration noise
```

**Curriculum:** Begin RL fine-tuning exclusively on the 419 currently failing
circuits. These are where the reward signal is most informative and the
improvement headroom is largest. Once these converge reliably, expand to the
full 34K dataset.

#### Algorithm Comparison

| Algorithm | Variance | Complexity | Recommended |
|-----------|----------|------------|-------------|
| **REINFORCE + baseline** | High | Low | Yes — start here |
| **PPO** | Medium | Medium | Upgrade if REINFORCE is too noisy |
| **SAC (Soft Actor-Critic)** | Low | High | Overkill for a one-step bandit |
| **Differentiable residual** (Phase 2) | Zero | Low | Always do before Phase 3 |

Start with Phase 2 (differentiable residual), measure improvement on the 419
failing circuits, then add REINFORCE only if Phase 2 leaves a significant gap.
REINFORCE on a one-step bandit with a good baseline requires ~100 lines of
PyTorch and will likely close most of the remaining gap.

### What Each Phase Learns

| Phase | Optimises | Misses |
|-------|-----------|--------|
| 1 — Supervised | Accurate voltage values | Whether those values are good Newton starting points |
| 2 — Residual | Physically consistent predictions | Whether "consistent" means "easy to converge from" |
| 3 — REINFORCE | Actual Newton convergence speed | Nothing — this is the true objective |

Each phase narrows the gap between the model's training signal and what we
actually care about. The full pipeline is directly analogous to LLM training:
pretraining → SFT → RLHF, with Newton-Raphson convergence as the reward signal
instead of human preference.

### RL-Specific Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| High variance in REINFORCE | Baseline subtraction; 10–20 samples per circuit; shaped reward `−log(iterations)` instead of binary |
| Exploration in ℝⁿ (100+ dimensions) | Gaussian noise on all nodes; optionally perturb only high-uncertainty nodes from an ensemble |
| Newton is non-differentiable for Phase 3 | REINFORCE handles this via log-probability trick; Phase 2 covers the differentiable case |
| Catastrophic forgetting of pretraining | KL penalty keeping RL policy close to supervised policy |
| Bistable circuits: RL locks to one basin | Track which basin was found; flag circuits where predicted and converged voltages differ by >1 V |

---

## Generalisation — What to Expect

"Generalised" means different things at different ambition levels:

| Level | Meaning | Achievable with this design? |
|-------|---------|------------------------------|
| **Parametric** | Same topology, unseen component values | Yes — log-scaling handles this |
| **Inductive** | Unseen topologies from same circuit family | Yes — GNN's inductive bias |
| **Zero-shot** | Completely unseen topology classes | Partial — see below |

For the neospice use case the threshold is low: a prediction within ±0.5 V of
the true answer places Newton in the right basin, regardless of whether the
prediction is "perfect." This makes level 2 (inductive) the realistic and
sufficient target.

### What will generalise well

- **Seen topology families** (op-amps, filters, bias circuits seen in training):
  predictions within 5–10% of true voltage → Newton converges in 5–10 iterations
  instead of 30–50
- **Similar but unseen topologies**: predictions within 20–30% → Newton still
  gets a better start than all-zeros
- **Different supply voltages**: handled by normalisation — a 3.3 V circuit and
  5 V circuit look identical after normalisation

### What will still fail

- **Novel device types** never seen in training (e.g., HiSIM2-only circuits if
  training used only BSIM4): the model has no embedding for unknown devices.
  Fix: map unknown types to the nearest known type as a fallback.
- **Power electronics** (>50 V): KiCad dataset is sparse in this range;
  normalisation helps but there may be distribution shift. Supplement with
  synthetic generation.
- **Bistable circuits** (latches, flip-flops): two equally valid operating
  points. The model will average them or pick one inconsistently. These are
  exactly the hardest convergence cases. Mitigation: predict a distribution
  (mean + variance); use high variance as a flag to try multiple starting points
  or fall back immediately to source stepping.

The ML model is purely additive — a wrong prediction just means Newton takes a
few extra iterations from a bad start, identical to the current all-zeros
behaviour. The existing fallbacks catch everything the model misses.

---

## Open Source Landscape

### ZeroSim [6] — Not Open Source

The most relevant paper (November 2025, transformer-based zero-shot generalisation
across 60+ amplifier topologies, 3.6M training instances) has **no public code
or weights**. Cannot be used directly.

### What Is Open Source and Usable

**CktGNN — [github.com/zehao-dong/CktGNN](https://github.com/zehao-dong/CktGNN)**
- ICLR 2023, PyTorch, MIT license
- Two-level GNN encoding analog circuits (op-amps) as bipartite graphs
- Ships with the Open Circuit Benchmark (OCB): 10,000 op-amps with simulation results
- **Directly reusable:** graph representation code, PyTorch Geometric pipeline, training loop
- **Needs replacing:** task head (designed for circuit optimisation, not voltage prediction)
- **Verdict: Best starting point for the GNN implementation**

**PretrainedPowerflowGNN — [github.com/KIT-IAI/PretrainedPowerflowGNN](https://github.com/KIT-IAI/PretrainedPowerflowGNN)**
- Based on PowerFlowNet, MIT license, PyTorch Geometric, pretrained weights included
- Predicts node voltages in power grids — same mathematics as circuit DC analysis
  (both solve nonlinear nodal equations via Newton-Raphson)
- **Directly reusable:** message-passing architecture, KCL-based loss, normalisation strategy
- **Needs adapting:** node/edge features (power grid uses bus type + power injection;
  we need component type + terminal role)
- **Verdict: Best architecture reference; pretrained weights are a useful starting point
  for transfer learning**

**Circuit-GNN — [github.com/hehaodele/circuit-gnn](https://github.com/hehaodele/circuit-gnn)**
- ICML 2019, MIT license, pretrained weights included
- GNN for distributed RF/microwave circuit design
- **Reusable:** general GNN framework; shows how to handle multi-terminal components
- **Not reusable:** focused on RF S-parameters, not DC node voltages

**GNN-Powerflow — [github.com/mukhlishga/gnn-powerflow](https://github.com/mukhlishga/gnn-powerflow)**
- Master's thesis, PyTorch Geometric, simpler codebase
- Good reference for understanding the power-flow-to-circuit analogy end-to-end

### Stanford GNN Pretraining [2] — Paper Only

The most directly relevant academic work (node voltage prediction as pretraining
task, 10× sample efficiency gain, generalises across circuit topologies) has no
code release. The architecture described in Section 3 of this document is our
independent re-implementation of the same ideas.

### Summary Table

| Resource | License | Pretrained Weights | Relevance | Action |
|----------|---------|-------------------|-----------|--------|
| ZeroSim [6] | None (no code) | No | Highest | Wait for release or reimplement |
| CktGNN [8] | MIT | No (trains fast) | High | Use graph representation + training pipeline |
| PretrainedPowerflowGNN | MIT | Yes | High | Use architecture + transfer learning |
| Circuit-GNN [9] | MIT | Yes | Medium | Reference for multi-terminal components |
| GNN-Powerflow | MIT | No | Medium | Reference implementation |
| Stanford [2] | None (no code) | No | Highest | Reimplement from paper |

---

## Recommended Build Plan

```
PretrainedPowerflowGNN  (architecture + KCL loss)
        +
CktGNN                  (bipartite graph representation + PyG pipeline)
        +
neospice KiCad 34K      (training data with ground-truth DC solutions)
        +
Masala-CHAI 7.5K        (topology diversity supplement)
        =
neospice-gnn            (~500 lines Python, ~2–3 weeks to working prototype)
```

---

## Available Datasets

### 1. neospice KiCad Test Suite (internal) — Primary

- **Size:** ~34,500 circuits, ~34,000 converging
- **Source:** KiCad SPICE simulation library, validated against ngspice
- **Format:** SPICE netlists + DC solutions from neospice
- **Coverage:** Broad analog/mixed-signal (op-amps, filters, oscillators,
  regulators, BJT/MOSFET amplifiers, digital gates)
- **Use:** Primary training set. Ground-truth voltages come from neospice's DC
  solutions for the converging subset.
- **Access:** Already in the test suite at `tests/`

### 2. symbench/spice-datasets

- **Size:** Thousands of netlists scraped from KiCad GitHub repositories
- **Source:** Open-source KiCad projects worldwide
- **Format:** Raw SPICE netlists (variable quality — many need cleanup)
- **Coverage:** Broad but noisy. Strong on power electronics and hobbyist circuits.
- **Use:** Pre-training data; augments the neospice internal dataset with more
  topology diversity.
- **URL:** https://github.com/symbench/spice-datasets

### 3. Masala-CHAI [5]

- **Size:** 7,500 captioned SPICE netlists extracted from 10 analog textbooks
- **Source:** LLM-assisted extraction from textbook schematics
- **Format:** Annotated SPICE netlists with circuit type labels
- **Coverage:** Textbook analog circuits — op-amp configurations, transistor
  amplifiers, filters, oscillators. Well-curated.
- **Use:** Topology diversity and circuit type labels for stratified training.
- **URL:** https://arxiv.org/abs/2411.14299

### 4. CktGNN Open Circuit Benchmark (OCB) [8]

- **Size:** 10,000 op-amp circuits with performance specifications
- **Source:** Randomly sampled op-amp topologies run through SPICE
- **Format:** Graph-structured + simulation results
- **Coverage:** Op-amps only, but diverse topologies
- **Use:** Supplementary training; useful for transfer learning experiments.
- **URL:** https://github.com/zehao-dong/CktGNN

### 5. Synthetic Generation (Recommended Supplement)

None of the existing datasets covers the hard cases — circuits that actually
fail to converge. A targeted synthetic dataset can be generated by
parametrically varying known-difficult topologies:

- Cross-coupled NAND latch with sweep of W/L ratios
- Schmitt trigger with sweep of feedback resistor ratios
- Current mirror with sweep of transistor count (2T to 20T)
- Bandgap reference with process corner variations
- Ring oscillator with varying stage count

For each variant, run neospice and record both the solution (if it converges)
and the number of iterations. Circuits requiring >50 iterations are high-value
training examples — they are exactly the hard cases the model must handle.

---

## Implementation Roadmap for neospice

### Phase 0 — Data Infrastructure (3 days)

Instrument neospice to emit training data as a side effect of normal simulation:

```cpp
// In dc.cpp, after successful convergence:
if (opts.emit_training_data) {
    TrainingDataWriter::write(circuit, dc_solution, "training_data.jsonl");
}
```

Each record:
```json
{
  "circuit_id": "kicad_opamp_filter_01",
  "nodes": ["in", "out", "vcc", "vee"],
  "components": [
    {"type": "R", "value": 1000, "terminals": [["in", "PLUS"], ["out", "MINUS"]]},
    {"type": "MOSFET_N", "vth": 0.5, "terminals": [["drain","DRAIN"],["gate","GATE"],["source","SOURCE"],["0","BULK"]]}
  ],
  "supply_voltage": 5.0,
  "dc_voltages_normalised": {"in": 0.0, "out": 0.5, "vcc": 1.0, "vee": -1.0}
}
```

Run the full test suite once to generate the initial corpus (~34K records).

### Phase 1 — Rule-Based Heuristic (1 day)

Implement `compute_heuristic_initial_guess()` in `src/core/dc.cpp`. BFS from
voltage sources, assign rail voltages to connected nodes. No external
dependencies. Baseline for measuring ML improvement.

### Phase 2 — MLP Model (1 week)

1. Extract per-node features from the JSONL training data
2. Train a 3-layer MLP with PyTorch (< 1 hour on CPU for 500K samples)
3. Export to ONNX
4. Load via `onnxruntime-c` (header-only) in neospice at startup
5. Run inference before Newton; inject predicted voltages as initial guess

Measure: convergence rate improvement on the 419 failing circuits.

### Phase 3 — GNN Supervised Training (2–4 weeks)

1. Build bipartite graph exporter from `Circuit` object to PyTorch Geometric
   `HeteroData`
2. Implement GATv2-based heterogeneous GNN using CktGNN as starting point
3. Add KCL regularisation loss following KCLNet [7] (Training Pipeline Phase 2)
4. Pretrain on symbench + Masala-CHAI; fine-tune on neospice KiCad corpus
5. Validate: measure convergence rate improvement on the 419 failing circuits

### Phase 4 — Residual Fine-Tuning (3–5 days)

Strengthen the KCL physics constraint progressively:

1. Implement a `compute_residual(circuit, V_predicted)` function that evaluates
   device models in MODEINITJCT mode and returns `|| F(x₀) ||`
2. Add it as a loss term with increasing λ schedule
3. Verify predictions become more physically consistent without regressing MSE
4. Re-measure convergence on the 419 failing circuits

### Phase 5 — REINFORCE Fine-Tuning (1–2 weeks)

1. Add a Python binding to neospice that accepts an initial voltage vector and
   returns `(converged: bool, iterations: int)` — the RL environment step
2. Implement the REINFORCE training loop with baseline subtraction and
   Gaussian exploration noise
3. Curriculum: train on the 419 failing circuits first, then expand to full 34K
4. Monitor: reward curve, convergence rate, average iterations on held-out set
5. Apply KL penalty to prevent catastrophic forgetting of Phase 3 pretraining
6. Anneal exploration noise σ from 0.5 V → 0.05 V over training

### Phase 6 — Export and Integration (2–3 days)

1. Export final trained model to ONNX (PyG supports ONNX export for inference)
2. Load via `onnxruntime-c` in neospice at startup
3. Replace MLP inference path with GNN; run full 34K suite regression

### Integration Architecture

```
parse netlist
    ↓
build bipartite graph from Circuit object  (new: src/core/circuit_graph.hpp)
    ↓
GNN inference → normalised voltages per node  (ONNX runtime, ~0.5 ms)
    ↓
denormalise: V_node = V_predicted × V_supply
    ↓
inject into solution vector as Newton initial guess
    ↓
Newton-Raphson from V_initial
    ↓  (if Newton diverges)
existing fallbacks: gmin stepping → source stepping → pseudo-transient
```

The model is loaded once at startup and controlled by `.options mlguess=1` in
the netlist. It is purely additive — disabling it restores current behaviour
exactly.

---

## Key Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Model predicts wrong basin (latch in wrong state) | Detect via large discrepancy between ML guess and converged solution; retry from zero |
| Wrong prediction adds latency for easy circuits | Only invoke model when direct Newton fails on first attempt; rule-based heuristic is always-on and free |
| Training data biased toward easy circuits | Weight hard cases (>50 Newton iterations) more heavily in loss; supplement with synthetic hard cases |
| ONNX runtime adds a build dependency | Guard with CMake option `NEOSPICE_ML_GUESS=OFF`; off by default until validated |
| Poor generalisation to unseen topology classes | Fall back to MODEINITJCT + existing convergence methods; ML is additive, never subtractive |
| Bistable circuits: model averages two valid OPs | Use predicted variance as a flag; high variance → skip ML guess and use rule-based or zero start |

---

## References

1. [A Machine Learning Initializer for Newton-Raphson AC Power Flow Convergence](https://www.osti.gov/servlets/purl/2447266) — US DOE, 2024
2. [Pretraining Graph Neural Networks for few-shot Analog Circuit Modeling and Design](https://arxiv.org/abs/2203.15913) — Stanford, 2022
3. [BoA-PTA: Bayesian Optimization Accelerated SPICE Solver](https://arxiv.org/pdf/2108.00257) — 2022
4. [Learning to Warm-Start Fixed-Point Optimization Algorithms](https://jmlr.org/papers/volume25/23-1174/23-1174.pdf) — JMLR, 2024
5. [Masala-CHAI: A Large-Scale SPICE Netlist Dataset for Analog Circuits](https://arxiv.org/abs/2411.14299) — 2024
6. [ZeroSim: Zero-Shot Analog Circuit Evaluation with Unified Transformer Embeddings](https://arxiv.org/abs/2511.07658) — 2025 *(no public code)*
7. [KCLNet: Physics-Informed Power Flow Prediction via Constraints Projections](https://arxiv.org/html/2506.12902v1) — 2025
8. [CktGNN: Circuit Graph Neural Network for Electronic Design Automation](https://arxiv.org/abs/2308.16406) — ICLR 2023 · [code](https://github.com/zehao-dong/CktGNN)
9. [Circuit-GNN: Graph Neural Networks for Distributed Circuit Design](https://semanticscholar.org/paper/Circuit-GNN:-Graph-Neural-Networks-for-Distributed-Zhang-He/d75882c1a45bf1b8982e4b77b123c66f3bf11ca5) — ICML 2019 · [code](https://github.com/hehaodele/circuit-gnn)
10. [PretrainedPowerflowGNN (KIT)](https://github.com/KIT-IAI/PretrainedPowerflowGNN) — MIT license, pretrained weights
11. [GNN-Powerflow (TU/e)](https://github.com/mukhlishga/gnn-powerflow) — PyTorch Geometric reference implementation
12. [symbench/spice-datasets](https://github.com/symbench/spice-datasets) — KiCad netlists from GitHub
13. [Data-driven approach for Newton-Raphson power flow](https://arxiv.org/pdf/2504.11650) — 2025
14. [RF-Informed GNNs for Circuit Performance Prediction](https://arxiv.org/pdf/2508.16403) — 2025
15. [Transfer of Performance Models Across Analog Circuit Topologies with GNNs](https://www.researchgate.net/publication/364043593_Transfer_of_Performance_Models_Across_Analog_Circuit_Topologies_with_Graph_Neural_Networks) — 2022
16. [GNN + PPO for warm-starting interior point solvers (power flow)](https://par.nsf.gov/servlets/purl/10576377) — 2023
17. [Learning Warm-Start Points for AC Optimal Power Flow](https://www.researchgate.net/publication/337790860_Learning_Warm-Start_Points_For_Ac_Optimal_Power_Flow) — RL warm-start for Newton-Raphson
18. [End-to-End Learning to Warm-Start for Real-Time Quadratic Optimization](https://proceedings.mlr.press/v211/sambharya23a/sambharya23a.pdf) — REINFORCE warm-start framework
19. [Leveraging Reward Gradients in Differentiable Physics Simulations](https://arxiv.org/pdf/2203.02857) — differentiable reward for physics simulators
20. [REINFORCE++: Stabilizing Critic-Free Policy Optimization](https://arxiv.org/html/2501.03262v9) — 2025
