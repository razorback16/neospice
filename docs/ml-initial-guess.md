# ML-Based Initial Guess for DC Convergence

**Research proposal.** The learned predictor is not implemented. It would supply
an optional starting vector for Newton iteration. It must preserve convergence
checks, explicit initial conditions, and existing recovery methods.
Use the [roadmap](ROADMAP.md) for current priorities.

## Problem Statement

A poor starting vector can increase Newton iterations or require convergence
recovery. A learned predictor might improve the starting vector by using circuit
structure, device parameters, and supply constraints. This benefit requires
measurement against the current engine, rather than a zero-start baseline.

The implemented [`compute_initial_guess()`](../src/core/node_classify.cpp)
honors `.ic` and `.nodeset`, with `.nodeset` taking precedence. It propagates
known voltages through independent sources with meaningful DC values.
Unseeded variables remain zero. It does not infer transistor bias or propagate
voltage unchanged through resistors.

## Device initialization and circuit-wide guesses

### Initialization modes

Device-local initialization and a circuit-wide starting vector serve different
purposes. Junction initialization lets device models choose terminal guesses.
Later Newton evaluations use voltage limiting and convergence checks.

Check the exact initialization sequence and device behavior against the
[pinned ngspice 47 reference](ngspice47-reference.md) before integrating a
predictor. A predictor must not bypass initialization modes or device limiting.

### Circuit-wide bias prediction

A device-local guess does not solve the surrounding bias network. The proposed
predictor would use connections across the circuit to estimate that bias.
It cannot guarantee a consistent operating point or select the intended state
of a bistable circuit without additional constraints.

## Prior Art

The references cover learned warm starts, circuit graph representations,
power-flow prediction, and circuit datasets. They are research inputs, not
neospice validation evidence. Check each paper's task, dataset, license, and
available artifacts before reuse. Power-flow results do not establish circuit
DC convergence improvements.

## Candidate predictors

### Approach 1 — Rule-Based Heuristics

Use the implemented source-constraint heuristic as the baseline. Additional
rules could estimate resistor-divider voltages or common transistor bias patterns.
Each rule needs defined applicability and tests. A resistor alone does not
justify copying a supply voltage into a loaded node.

### Approach 2 — Supervised Regression Model

Predict each node voltage from a feature vector using an MLP or boosted trees.
Candidate features include source distance, source voltage, degree, attached
device types, terminal roles, feedback membership, and a divider estimate.
Independent node predictions provide a simpler comparison model for the GNN.

Train only on qualified operating points. Split by complete circuits and model
families to prevent leakage between parameter variants. Normalize voltages with
a documented supply scale. Define behavior for missing, zero, and multiple rails.

### Approach 3 — Graph Neural Network

Use a graph neural network (GNN) to retain connections and terminal roles.
Compare it with the heuristic and regression model before choosing a runtime
dependency. Better voltage prediction alone does not establish better convergence.

## GNN design

### Message passing

Message passing can propagate supply and device information through circuit
connections. A fixed number of rounds limits the receptive field. Circuit size,
long feedback paths, and disconnected components require explicit evaluation.

### Bipartite graph

Represent both electrical nodes and devices as graph nodes. Terminal connections
form edges with roles such as gate, drain, source, bulk, anode, and cathode.
This representation preserves multi-terminal devices without treating them as
single wire edges.

### Normalization

Component values span many orders of magnitude. Candidate encodings include
logarithmic magnitudes, signed values, and ratios of related components.
Define zero handling and retain units and signs. Normalize predicted voltages
by a declared supply scale, then convert them to volts before Newton iteration.
Supply normalization does not remove device thresholds or process dependence.

### Node and Edge Features

| Graph item | Candidate features |
|---|---|
| Device | Type, value magnitude/sign, model parameters, `.off` flag |
| Electrical node | Ground flag, supply constraints, normalized supply value, degree |
| Terminal edge | Terminal role and controlling-terminal flag |

Unknown device types require a defined fallback. Do not silently substitute a
known model with different bias behavior.

### Message Passing Architecture

A heterogeneous GATv2 model is one candidate. It alternates aggregation from
electrical nodes to devices and from devices to electrical nodes.
Initial experiment settings are six rounds, 128 hidden units, four attention
heads, and dropout of 0.1. These are tuning inputs, not established optimums.

### Physics-Informed Loss (KCL Regularisation)

Combine supervised voltage error with a scaled circuit residual:

```text
L = mean((V_pred - V_reference)^2) + lambda * residual_loss
```

For resistors, currents follow `(V1 - V2) / R`. Semiconductor residuals require
actual model evaluation. MNA also includes source constraints and branch-current
unknowns. Define residual units, scaling, and missing state before training.
A low residual does not guarantee Newton convergence or the desired solution.

### Full Architecture Specification

The candidate model emits one normalized voltage per electrical node.
AdamW, cosine learning-rate decay, and batches of variable-size graphs are
initial training choices. Feature dimensions must follow the actual encodings.
Check export support for the selected graph operations before choosing ONNX.
Measure training cost, inference latency, memory, and binary size.

## Training Pipeline — Three Phases

Voltage error, equation residual, and Newton convergence measure different
objectives. Add training stages only when held-out results justify them.

### Phase 1 — Supervised Pretraining

Fit qualified operating-point voltages. Retain simulator version, model library,
parameters, temperature, convergence status, and circuit provenance with labels.
Use ngspice 47 comparisons to qualify reference behavior.

### Phase 2 — Residual Fine-Tuning (Differentiable, No RL Needed)

Add a scaled residual term and vary its weight on the validation set.
The residual evaluator must evaluate equations at the proposed state.
Device initialization routines that replace terminal guesses are unsuitable for
that purpose. Use analytical derivatives or checked finite differences where needed.

### Phase 3 — REINFORCE on Actual Newton Convergence (True RL)

Treat each circuit as a contextual-bandit input. The action is a starting vector.
The reward can penalize Newton iterations and failed solves. Include fallback
cost and total runtime when comparing policies.

A Gaussian policy supplies exploration. Evaluate sampled actions with the
policy's log probability. Preserve gradients through the policy mean when
computing that probability. A reward baseline reduces variance. A penalty for
departure from the supervised policy may limit forgetting.

#### Training Loop

1. Predict a mean starting vector for a training circuit.
2. Sample candidate vectors from the current policy.
3. Run the checked DC solve and record convergence, iterations, and runtime.
4. Compute advantages relative to a reward baseline.
5. Update policy weights using sampled-action log probabilities.
6. Check held-out convergence and recovery costs before retaining the update.

#### Algorithm Comparison

Start with residual training. Consider REINFORCE if convergence results leave a
useful gap. PPO is an alternative if policy-gradient variance prevents progress.
Compare complexity and simulator calls before adding another optimizer.

### What Each Phase Learns

| Stage | Objective | Limitation |
|---|---|---|
| Supervised | Match labeled voltages | Labels may omit alternative operating points |
| Residual | Satisfy circuit equations | Small residuals need not give reliable Newton starts |
| Policy gradient | Improve measured solve outcomes | Reward and training distribution limit transfer |

### RL-Specific Risks and Mitigations

Track policy variance, simulator-call budget, held-out regressions, and state
selection in bistable circuits. Exploration must respect fixed voltage constraints.
Retain the best qualified model and support disabling inference.

## Generalization evaluation

Evaluate parameter changes, unseen topologies within known families, and unseen
families separately. No fixed voltage-error threshold guarantees that a prediction
lies in a useful Newton basin.

### Evaluation groups

These are evaluation questions, not established outcomes. Stratify results by
circuit family, supply range, model family, temperature, and circuit size.

### Unsupported and ambiguous inputs

Unknown devices, unusual bias networks, and multiple valid operating points
need explicit fallback behavior. A wrong guess can add latency or select a
different valid state. Existing recovery methods do not guarantee success.

## Open Source Landscape

### ZeroSim [6]

Verify current artifact availability before selecting this work for reuse.


### Candidate repositories

Candidate repositories include CktGNN [8], PretrainedPowerflowGNN [10],
Circuit-GNN [9], and GNN-Powerflow [11]. Evaluate graph representations, loss
functions, and data pipelines separately. Check licenses and supported tasks.
Pretrained weights from power-flow tasks need independent circuit validation.

### GNN pretraining [2]

Check current artifacts and the paper's task before implementing its architecture.
Node-voltage pretraining is a research reference rather than a supplied neospice model.

### Summary Table

| Candidate | Proposed use |
|---|---|
| CktGNN / Circuit-GNN | Circuit graph encoding and dataset structure |
| PretrainedPowerflowGNN / GNN-Powerflow | Message passing and power-flow residual examples |
| Warm-start papers | Training objectives and policy-gradient methods |

## Recommended Build Plan

Qualify a dataset first. Compare the existing heuristic, an MLP, and a GNN on
identical held-out circuits. Add residual training before policy-gradient training.
Integrate a runtime only after results justify the dependency and inference cost.

## Available Datasets

### 1. KiCad corpus

Use a versioned corpus manifest and qualified solutions. Derive counts from the
manifest and current run records. Do not reuse historical failure totals as
current training-set sizes. Separate model defects from convergence failures.

### 2. symbench/spice-datasets

Candidate source of KiCad netlists [12]. Check provenance, licenses, duplicate
circuits, parser failures, and reference qualification before using labels.

### 3. Masala-CHAI [5]

Candidate source of textbook circuit diversity. Check extracted netlists and
qualify generated solutions before adding them to training.

### 4. CktGNN Open Circuit Benchmark (OCB) [8]

Candidate source of op-amp topology diversity. Performance labels alone do not
supply per-node operating-point voltages.

### 5. Synthetic generation

Vary latch geometry, Schmitt-trigger feedback, current mirrors, bandgap corners,
and ring-oscillator stages. Record unsuccessful solves as well as successful ones.
Keep related variants in the same train/test group.

## Implementation Roadmap for neospice

### Phase 0 — Data Infrastructure

Add versioned graph export,
solution labels, convergence diagnostics, and provenance records.

### Phase 1 — Rule-Based Heuristic

The source-constraint heuristic already exists. Qualify it as the comparison baseline.
Additional topology rules remain proposed work.

### Phase 2 — MLP Model

Train the simpler regression model and measure held-out solve outcomes.
Check export and runtime integration before selecting dependencies.

### Phase 3 — GNN Supervised Training

Build the bipartite exporter and candidate model. Compare against the MLP with
identical dataset splits and solve limits.

### Phase 4 — Residual Fine-Tuning

Implement a checked residual evaluator. Compare prediction error, convergence,
and total solve cost before retaining residual training.

### Phase 5 — REINFORCE Fine-Tuning

Expose a controlled solve interface for candidate starting vectors.
Train and validate the policy against the same frozen circuit groups.

### Phase 6 — Export and Integration

Check exported predictions against the training runtime. Make inference optional.
Run reference comparisons and regressions with inference enabled and disabled.

### Integration Architecture

The proposed path is circuit parsing, graph export, inference, denormalization,
constraint validation, and a normal DC solve. Model load and inference failures
must retain the standard solve path. Decide whether inference precedes direct
Newton or follows a failed attempt. Neither policy is implemented.

A proposed `NEOSPICE_ML_GUESS` build option and `mlguess` runtime option would
control integration. Define model ownership and thread safety for parallel studies.

## Key Risks and Mitigations

| Risk | Required control |
|---|---|
| Wrong or ambiguous operating point | Preserve user constraints and report state selection |
| Added latency on easy circuits | Measure complete solve cost and define invocation policy |
| Biased training data | Group splits, hard cases, and reference qualification |
| New build dependency | Optional build/runtime integration and checked failure behavior |
| Unknown devices or distribution shift | Skip inference or use the standard solve path |
| Runtime export differences | Compare predictions and solve outcomes across runtimes |

## References

1. [A Machine Learning Initializer for Newton-Raphson AC Power Flow Convergence](https://www.osti.gov/servlets/purl/2447266) — US DOE, 2024
2. [Pretraining Graph Neural Networks for few-shot Analog Circuit Modeling and Design](https://arxiv.org/abs/2203.15913) — Stanford, 2022
3. [BoA-PTA: Bayesian Optimization Accelerated SPICE Solver](https://arxiv.org/pdf/2108.00257) — 2022
4. [Learning to Warm-Start Fixed-Point Optimization Algorithms](https://jmlr.org/papers/volume25/23-1174/23-1174.pdf) — JMLR, 2024
5. [Masala-CHAI: A Large-Scale SPICE Netlist Dataset for Analog Circuits](https://arxiv.org/abs/2411.14299) — 2024
6. [ZeroSim: Zero-Shot Analog Circuit Evaluation with Unified Transformer Embeddings](https://arxiv.org/abs/2511.07658) — 2025
7. [KCLNet: Physics-Informed Power Flow Prediction via Constraints Projections](https://arxiv.org/html/2506.12902v1) — 2025
8. [CktGNN: Circuit Graph Neural Network for Electronic Design Automation](https://arxiv.org/abs/2308.16406) — ICLR 2023 · [code](https://github.com/zehao-dong/CktGNN)
9. [Circuit-GNN: Graph Neural Networks for Distributed Circuit Design](https://semanticscholar.org/paper/Circuit-GNN:-Graph-Neural-Networks-for-Distributed-Zhang-He/d75882c1a45bf1b8982e4b77b123c66f3bf11ca5) — ICML 2019 · [code](https://github.com/hehaodele/circuit-gnn)
10. [PretrainedPowerflowGNN (KIT)](https://github.com/KIT-IAI/PretrainedPowerflowGNN) — candidate architecture reference
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
