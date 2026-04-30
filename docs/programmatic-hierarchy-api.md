# Programmatic Hierarchy API Plan

**Status:** Planned
**Scope:** Native `Simulator::include()`, `Simulator::subckt()`, and typed subcircuit instantiation APIs

## Summary

neospice already supports `.include`, `.lib`, `.subckt`, `.ends`, `.global`, nested `X` instances, and recursive subcircuit flattening through the netlist parser. That path is string/file based:

1. `Simulator::load(path)` reads a file and resolves `.include`/`.lib`.
2. `Simulator::parse(text)` tokenizes netlist text.
3. `NetlistParser` extracts `.subckt` definitions.
4. `expand_all_instances()` recursively flattens `X...` instances into primitive device lines.
5. The normal parser builds a flat `Circuit`.

The missing feature is a native programmatic hierarchy API that lets C++ and Python users define reusable subcircuits and include external model libraries without manually building netlist strings.

## Goals

- Add a first-class API for loading model/library files into a simulator context.
- Add a first-class API for defining and instantiating reusable subcircuits.
- Reuse the existing subcircuit expansion code where possible instead of duplicating hierarchy logic in every device builder.
- Preserve existing `Simulator::load()` and `Simulator::parse()` behavior.
- Keep the final `Circuit` flat, matching the current solver architecture.
- Support Python bindings with the same semantics as C++.

## Non-Goals

- Do not make the solver hierarchical.
- Do not require every `Device` implementation to understand subcircuits.
- Do not replace the existing netlist parser.
- Do not implement interactive schematic editing primitives in this phase.

## Current Behavior

### Subcircuits

Subcircuits work when they appear in netlist text:

```spice
.subckt rdiv in out r=1k
R1 in mid {r}
R2 mid out {r}
.ends

V1 vin 0 10
X1 vin vout rdiv r=2k
.op
.end
```

The parser expands this approximately into:

```spice
V1 vin 0 10
x1.r1 vin x1.mid 2000
x1.r2 x1.mid vout 2000
.op
```

Internal node names and device names are prefixed with the instance path. Ground and `.global` nodes are not prefixed. Nested `X` instances are expanded recursively with a depth limit.

### Includes

`Simulator::load(path)` resolves `.include` and `.lib` relative to `path`. `Simulator::parse(text)` does not have a base directory, so relative includes in raw text are not well-defined unless the caller has already expanded them.

## Why a Native API Is More Than a Thin Wrapper

A minimal wrapper could append strings and call `parse()`, but a proper API needs persistent context:

- A base directory for resolving relative includes.
- A registry of included model cards, library sections, and subcircuit definitions.
- A way to instantiate subcircuits from typed code.
- Case-insensitive name lookup compatible with SPICE.
- Parameter defaults, overrides, `.global` nodes, and nested definitions.
- Cycle detection and useful source/error reporting.

Without that context, `include()` and `subckt()` would be fragile conveniences rather than reliable library APIs.

## Proposed Public API

### C++

```cpp
neospice::Simulator sim;

sim.include("models/sky130.lib", "tt");   // optional section for .lib-style files
sim.include("common_models.cir");         // include all cards/definitions

sim.subckt(R"(
.subckt inv in out vdd vss wp=2u wn=1u
M1 out in vss vss NMOD W={wn} L=0.15u
M2 out in vdd vdd PMOD W={wp} L=0.15u
.ends
)");

neospice::Circuit ckt;
auto in = ckt.node("in");
auto out = ckt.node("out");
auto vdd = ckt.node("vdd");

ckt.V("VDD", vdd, neospice::GND, 1.8);
ckt.X("X1", {in, out, vdd, neospice::GND}, "inv", {{"wp", 4e-6}});

auto flat = sim.finalize(ckt);
auto dc = sim.run_dc(flat);
```

### Lower-Level Builder Option

For more type safety, add a subcircuit builder object later:

```cpp
sim.subckt("inv", {"in", "out", "vdd", "vss"}, [](Subckt& s) {
    auto in = s.port("in");
    auto out = s.port("out");
    auto vdd = s.port("vdd");
    auto vss = s.port("vss");
    s.M("M1", out, in, vss, vss, "NMOD", Param{"wn"}, 0.15e-6);
    s.M("M2", out, in, vdd, vdd, "PMOD", Param{"wp"}, 0.15e-6);
});
```

This is more work because it needs symbolic parameters in typed builders. The string-backed `subckt()` should ship first.

### Python

```python
sim = neospice.Simulator()
sim.include("models.lib", section="tt")
sim.subckt("""
.subckt rdiv in out r=1k
R1 in mid {r}
R2 mid out {r}
.ends
""")

ckt = neospice.Circuit()
ckt.V("V1", "vin", "0", 10.0)
ckt.X("X1", ["vin", "vout"], "rdiv", {"r": 2000.0})
dc = sim.run_dc(sim.finalize(ckt))
```

## Internal Architecture

### New Data Structures

Add a persistent hierarchy context, owned by `Simulator`:

```cpp
struct IncludeRecord {
    std::filesystem::path path;
    std::optional<std::string> section;
};

struct HierarchyContext {
    std::filesystem::path base_dir;
    std::vector<IncludeRecord> includes;
    std::unordered_map<std::string, SubcircuitDef> subckts;
    std::unordered_map<std::string, ModelCard> models;
    std::unordered_set<std::string> global_nodes;
};
```

Add pending subcircuit instances to `Circuit`:

```cpp
struct SubcktInstance {
    std::string name;
    std::vector<NodeId> nodes;
    std::string subckt_name;
    std::unordered_map<std::string, double> params;
};
```

The solver still receives a finalized flat circuit. `Circuit::X()` records pending instances. `Simulator::finalize(ckt)` lowers those pending instances through the hierarchy context and existing flattener.

### Reuse Existing Parser Pieces

Existing code that should be reused or refactored:

- `NetlistParser::resolve_includes()`
- `SubcircuitDef`
- `expand_all_instances()`
- parameter expression resolution in `parser/expression.*`
- `.global` handling
- parser device/model dispatch after flattening

The likely refactor is to extract parser preprocessing into reusable functions:

```cpp
struct PreprocessedNetlist {
    std::vector<TokenizedLine> flat_lines;
    std::unordered_map<std::string, SubcircuitDef> subckts;
    std::unordered_map<std::string, double> params;
    std::unordered_set<std::string> global_nodes;
};

PreprocessedNetlist preprocess_netlist(
    std::string_view text,
    const std::filesystem::path& base_dir,
    const HierarchyContext* context);
```

## Implementation Plan

### Phase 1: Refactor Preprocessing

- Extract include resolution, `.subckt` extraction, `.param`, `.func`, and `.global` collection out of `NetlistParser::parse()`.
- Preserve current behavior for `parse()` and `load()`.
- Add tests proving the refactor is behavior-neutral.

### Phase 2: Persistent Include Context

- Add `Simulator::include(path)` and `Simulator::include(path, section)`.
- Store included model cards and subcircuit definitions in `HierarchyContext`.
- Resolve relative paths against an explicit base directory.
- Detect circular includes using the existing include stack behavior.
- Add Python bindings.

### Phase 3: String-Backed Subcircuit Registry

- Add `Simulator::subckt(std::string_view text)`.
- Parse and store exactly one or more `.subckt` definitions from the text.
- Reject primitive top-level elements in `subckt()` input unless explicitly allowed later.
- Support nested subcircuit definitions using existing extraction logic.
- Add duplicate-name policy: reject by default; optional replace flag later.

### Phase 4: Typed `X()` Instantiation

- Add `Circuit::X(name, nodes, subckt_name, params)` returning `DevId` or a new `SubcktId`.
- Store pending `SubcktInstance` entries until flattening.
- Add `Simulator::finalize(Circuit&) -> Circuit` or `Simulator::expand(Circuit&) -> Circuit`.
- Generate tokenized `X` lines from pending instances and feed them into `expand_all_instances()`.
- Parse the generated flat netlist into a final `Circuit`.

### Phase 5: Native Subckt Builder

- Add a `Subckt` builder type.
- Support symbolic parameters in builder methods.
- Lower builder definitions into `SubcircuitDef` or generated netlist tokens.
- Add Python-friendly construction if the C++ API proves stable.

## Error Handling

Required errors:

- missing include file
- circular include
- missing library section
- duplicate model/subckt names
- unknown subckt in `X()`
- wrong port count
- recursive subckt expansion beyond depth limit
- parameter evaluation failure
- relative include without a base directory

Errors should include the instance name, subckt name, file path when available, and source line when available.

## Test Plan

### Unit Tests

- `Simulator::include()` loads model cards and subckts.
- include paths resolve relative to the base file.
- duplicate includes do not corrupt state.
- circular includes throw.
- `Simulator::subckt()` stores definitions and rejects malformed definitions.
- `Circuit::X()` validates port count and unknown subckt names during finalize.
- parameter defaults and overrides match parser behavior.
- `.global` nodes remain unprefixed.
- nested subcircuits expand correctly.

### Equivalence Tests

For each feature, compare:

1. A normal netlist string using `.include`, `.subckt`, and `X...`.
2. The equivalent programmatic API using `include()`, `subckt()`, and `X()`.

DC, transient, and AC results should match within existing tolerances.

### Python Tests

- include a model file and instantiate a device/subckt.
- define a subckt from a multiline string.
- instantiate `X()` with string node names.
- verify errors propagate as Python exceptions.

## Risks and Design Choices

- `Circuit` is currently a flat construction API. Adding `X()` introduces a pending hierarchical stage, so it should be clearly separated from `finalize()`.
- Returning a new flat `Circuit` from `Simulator::finalize()` avoids mutating user-owned circuits unexpectedly.
- String-backed `subckt()` is much lower risk than a native subckt builder and should be the first shipped API.
- Native builder support needs symbolic parameter expressions, not just `double`, so it should be a follow-up after the lowering path is stable.

## Recommended First Deliverable

Ship this minimal proper API first:

```cpp
Simulator sim;
sim.set_base_dir("project");
sim.include("models.lib", "tt");
sim.subckt(subckt_text);

Circuit ckt;
ckt.X("X1", {a, b, c}, "myblock", {{"r", 2e3}});

Circuit flat = sim.expand(ckt);
auto result = sim.run_dc(flat);
```

This gives users a real programmatic hierarchy workflow while reusing the parser and flattener that already exist.
