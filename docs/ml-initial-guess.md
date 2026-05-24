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

## Prior Art

This is not a new idea. The same problem exists in power-grid simulation (AC
power flow is also Newton-Raphson on a large nonlinear system) and has been
studied extensively.

| Work | Year | Approach | Result |
|------|------|----------|--------|
| OSTI ML Power Flow Initialiser | 2024 | Random Forest on grid topology features | 2,106 previously non-converging cases converged |
| GNN Analog Circuit Pretraining (Stanford) | 2022 | GNN pretrained to predict node voltages | 10× sample efficiency vs. random init; generalises across topologies |
| BoA-PTA | 2022 | Bayesian optimisation over solver hyperparameters | 2.2× average speedup, 3.5× maximum on 43 benchmarks |
| Warm-Start Fixed-Point Algorithms (JMLR) | 2024 | Neural network → warm start, then fixed-point iterations | Significant reduction in iterations; convergence guarantees preserved |
| Masala-CHAI | 2024 | LLM-generated SPICE netlists at scale | 7,500 captioned netlists; establishes dataset infrastructure |

The circuit simulation problem is harder than power flow because:
- Circuits span far more topology classes (digital, analog, RF, mixed-signal)
- Device models are more nonlinear (BSIM4 vs. a transmission line)
- The "correct" operating point is sometimes ambiguous (bistable circuits)

But the same core idea applies: **a model that has seen thousands of similar
circuits can predict a starting point close enough that Newton converges
immediately.**

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
be implemented in a day. Directly addresses the "Automatic Node Classification"
item in `dc-convergence-improvements.md` (Priority 3C).

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

#### Inference

At simulation start, before Newton:

```python
for each node in circuit:
    features = extract_features(node, circuit)
    predicted_v = model.predict(features)
    initial_guess[node] = predicted_v
```

Total inference cost: microseconds for a 100-node circuit (one MLP forward pass
per node, trivially parallelisable).

#### Safety

The model is purely advisory. If Newton diverges from the ML guess, the existing
convergence fallbacks (gmin stepping, source stepping, pseudo-transient) still
fire. The ML guess makes them needed less often — it does not replace them.

To detect convergence to the *wrong* operating point (e.g., wrong latch state),
compare the final solution against the ML prediction. If they differ by more than
a configurable threshold (e.g., 1 V), optionally try again from the all-zeros
start to see if a different OP exists.

**Expected impact:** Solves ~50–60% of remaining convergence failures after the
rule-based heuristic. Reduces average Newton iteration count by ~30%.

**Strengths:** Simple to implement, interpretable, fast to train (minutes on the
KiCad dataset), runs at microsecond speed.

**Weaknesses:** Per-node features ignore circuit topology — a node in an RC
low-pass filter and a node in an identical-looking feedback network look the same
to the model even though their operating points may differ. Generalisation to
exotic topologies is uncertain.

---

### Approach 3 — Graph Neural Network

A GNN treats the circuit as a graph directly — no hand-crafted features. Nodes
in the graph are circuit nodes; edges are components.

#### Why a GNN?

A circuit is fundamentally a graph. The voltage at a node depends on its
neighbours (KCL), which depend on their neighbours, and so on. Message-passing
GNNs replicate this propagation:

- **Each node** starts with an embedding: what component types are attached, what
  their values are.
- **Each message-passing round** aggregates information from neighbours — exactly
  like Gaussian elimination spreading information through the circuit.
- **After K rounds** (K = circuit diameter), every node has seen the whole circuit.
- **Output layer** predicts voltage at each node.

The 2022 Stanford paper (arXiv:2203.15913) showed that GNNs pretrained on DC
prediction generalise across circuit topologies with 10× better sample efficiency
than per-circuit models.

#### Architecture

```
Node features (initial embedding):
  - One-hot component type (R, C, L, MOSFET_G, MOSFET_D, MOSFET_S, ...)
  - Log-scaled component value
  - Voltage source value (if directly connected)

Edge features:
  - Component type
  - Log-scaled value

Message passing:
  - 4–6 rounds of GraphSAGE or GATv2 aggregation
  - Hidden dimension: 128

Output:
  - Linear layer → predicted voltage per node
  - Normalised by max supply voltage
```

#### Handling Subcircuits

Circuits in neospice are flattened before simulation (the parser expands all
`.subckt` instances). The GNN operates on the flattened graph. Hierarchical
GNNs are a research direction but not needed here.

#### Training Data

Same corpus as Approach 2, but stored as graphs. The `Circuit` object in
neospice already exposes node adjacency — exporting to a PyTorch Geometric
`Data` object requires iterating `Circuit::devices()` and recording
`(node_a, node_b, component_type, value)` tuples.

#### Strengths

- Topology-aware: two nodes with identical local features but different circuit
  context will get different predictions
- Generalises to unseen topologies via transfer learning
- Scales to large circuits (GNN inference is O(nodes + edges))
- Can be pretrained on a large public dataset (Masala-CHAI, symbench) and
  fine-tuned on neospice's own converged solutions

#### Weaknesses

- More complex to implement and maintain (PyTorch Geometric dependency, or a
  custom message-passing implementation in C++)
- Training requires GPU for reasonable iteration time
- Inference adds a Python/C++ bridge or requires embedding the model in the
  simulator (ONNX export is viable)
- May struggle with very large circuits (>1,000 nodes) without batching tricks

**Expected impact:** Best generalisation of all three approaches. Likely resolves
60–80% of convergence failures. Unlocks transfer learning from public datasets.

---

## Combining All Three (Recommended Architecture)

The three approaches are not mutually exclusive. The recommended stack:

```
1. Rule-based heuristics (zero cost, always on)
        ↓  produces guess_v0
2. MLP regression (microseconds, replaces guess_v0 if model confidence is high)
        ↓  produces guess_v1
3. Newton-Raphson from guess_v1
        ↓  if fails:
4. Existing fallbacks (gmin, source stepping, pseudo-transient)
```

The MLP and GNN can be enabled/disabled via `.options mlguess=1` in the netlist.
The rule-based heuristic is always on (it's free).

---

## Available Datasets

### 1. neospice KiCad Test Suite (internal)

- **Size:** ~34,500 circuits, ~34,000 converging
- **Source:** KiCad SPICE simulation library, validated against ngspice
- **Format:** SPICE netlists + ngspice reference solutions
- **Coverage:** Broad analog/mixed-signal (op-amps, filters, oscillators,
  regulators, BJT/MOSFET amplifiers, digital gates)
- **Use:** Primary training set for the MLP model. Ground-truth voltages come
  from neospice's own DC solutions.
- **Access:** Already in the test suite at `tests/`

### 2. symbench/spice-datasets

- **Size:** Thousands of netlists scraped from KiCad GitHub repositories
- **Source:** Open-source KiCad projects worldwide
- **Format:** Raw SPICE netlists (variable quality — many need cleanup)
- **Coverage:** Broad but noisy. Strong on power electronics and hobbyist circuits.
- **Use:** Pre-training data; augments the neospice internal dataset with more
  topology diversity.
- **URL:** https://github.com/symbench/spice-datasets

### 3. Masala-CHAI

- **Size:** 7,500 captioned SPICE netlists extracted from 10 analog electronics
  textbooks
- **Source:** LLM-assisted extraction from textbook schematics (2024, arXiv:2411.14299)
- **Format:** Annotated SPICE netlists with circuit type labels
- **Coverage:** Textbook analog circuits — op-amp configurations, transistor
  amplifiers, filters, oscillators. Well-curated and diverse.
- **Use:** Excellent for pretraining a GNN because circuits come with topology
  type labels (useful for stratified training/evaluation).
- **URL:** https://arxiv.org/abs/2411.14299

### 4. CircuitNet

- **Size:** 20,000+ samples from commercial EDA tool runs
- **Source:** Open-source digital designs run through Synopsys/Cadence tools
- **Format:** Routed netlists + timing/IR-drop/routability labels
- **Coverage:** Digital logic (not analog SPICE). Not directly usable for DC
  operating point prediction.
- **Use:** Relevant only if neospice adds digital/gate-level analysis. Not
  recommended for initial guess work.
- **URL:** https://github.com/circuitnet/CircuitNet

### 5. Synthetic Generation (recommended supplement)

None of the existing datasets covers all the hard cases — the circuits that
actually fail. A targeted synthetic dataset can be generated by parameterically
varying known-difficult topologies:

- Cross-coupled NAND latch with sweep of W/L ratios
- Schmitt trigger with sweep of feedback resistor ratios
- Current mirror with sweep of transistor count (2T to 20T)
- Bandgap reference with process corner variations
- Ring oscillator with varying stage count

For each variant, run neospice with a forced zero-start and record both the
solution (if it converges) and the number of iterations required. Circuits
requiring >50 iterations are hard cases — valuable training examples.

---

## Implementation Roadmap for neospice

### Phase 0 — Data Infrastructure (3 days)

Instrument neospice to emit training data as a side effect of simulation:

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
  "nodes": ["in", "out", "vcc", "vee", ...],
  "edges": [["in","out","R",1000], ["out","0","C",1e-9], ...],
  "dc_voltages": {"in": 0.0, "out": 2.5, "vcc": 5.0, ...}
}
```

Run the full test suite once to generate the initial training corpus.

### Phase 1 — Rule-Based Heuristic (1 day)

Implement `compute_heuristic_initial_guess()` in `src/core/dc.cpp`. BFS from
voltage sources, assign rail voltages to connected nodes. No external
dependencies.

Measure: how many of the 419 failing circuits now converge?

### Phase 2 — MLP Model (1 week)

1. Extract per-node features from the JSONL training data (Python script)
2. Train a 3-layer MLP with PyTorch (< 1 hour on CPU for 500K samples)
3. Export to ONNX
4. Load ONNX model in neospice at startup (use `onnxruntime-c` library, header-only)
5. Run inference before Newton; inject predicted voltages as initial guess

Measure: convergence rate improvement on the 419 failing circuits.

### Phase 3 — GNN Model (2–4 weeks, research effort)

1. Export circuit graphs to PyTorch Geometric format
2. Implement GNN training pipeline (GraphSAGE or GATv2)
3. Pretrain on symbench + Masala-CHAI; fine-tune on neospice corpus
4. Export to ONNX (PyG supports ONNX export for inference graphs)
5. Replace MLP inference with GNN inference in neospice

Measure: convergence rate, average Newton iterations, wall-clock time.

---

## Key Risks

| Risk | Mitigation |
|------|-----------|
| Model predicts wrong basin of attraction (e.g., wrong latch state) | Detect via large discrepancy between ML guess and converged solution; optionally retry from zero |
| Model adds latency for simple circuits that converge fine from zero | Only invoke ML model when direct Newton fails on first attempt |
| Training data is biased toward "easy" circuits | Augment with synthetic hard cases; weight hard cases more heavily in loss |
| ONNX runtime adds a build dependency | Make ML inference optional, guarded by a CMake flag |
| Model fails on circuit topologies not seen in training | Fall back to rule-based heuristic + existing convergence methods — ML is additive, never subtractive |

---

## References

1. [A Machine Learning Initializer for Newton-Raphson AC Power Flow Convergence](https://www.osti.gov/servlets/purl/2447266) — OSTI 2024
2. [Pretraining Graph Neural Networks for few-shot Analog Circuit Modeling and Design](https://arxiv.org/abs/2203.15913) — Stanford 2022
3. [BoA-PTA: Bayesian Optimization Accelerated SPICE Solver](https://arxiv.org/pdf/2108.00257) — 2022
4. [Learning to Warm-Start Fixed-Point Optimization Algorithms](https://jmlr.org/papers/volume25/23-1174/23-1174.pdf) — JMLR 2024
5. [Masala-CHAI: A Large-Scale SPICE Netlist Dataset for Analog Circuits](https://arxiv.org/abs/2411.14299) — 2024
6. [symbench/spice-datasets](https://github.com/symbench/spice-datasets) — KiCad circuits from GitHub
7. [Data-driven approach for Newton-Raphson power flow](https://arxiv.org/pdf/2504.11650) — 2025
