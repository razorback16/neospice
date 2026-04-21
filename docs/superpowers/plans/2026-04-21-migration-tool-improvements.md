# Migration Tool Improvements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the ngspice device migration tool robust enough that new device migrations compile with near-zero manual work, while fixing BSIM4v7 accuracy.

**Architecture:** Learn-then-automate — debug BSIM4v7 first to find tool/adapter bugs, then add new generators (model card, parser, compute_trunc, query_param, RESOLVE extraction, sensitivity stripping, test scaffolding), validate by re-migrating BSIM4v7, then migrate new devices.

**Tech Stack:** Python 3 (migration tool), C++17 (neospice), CMake, Google Test, ngspice (reference)

---

## File Map

### New Python generator files
- `tools/ngspice_migrate/gen_model_card.py` — generates `<ns>_model_card.hpp/cpp` (model card conversion)
- `tools/ngspice_migrate/gen_parser.py` — generates `<ns>_parser.hpp` (parser integration helpers)
- `tools/ngspice_migrate/gen_test.py` — generates per-device test directory scaffold

### Modified Python files
- `tools/ngspice_migrate/__main__.py` — call new generators in orchestration
- `tools/ngspice_migrate/gen_adapter.py` — add compute_trunc, query_param, RESOLVE from def header
- `tools/ngspice_migrate/gen_def.py` — expose MatrixOffset field extraction API
- `tools/ngspice_migrate/descriptor.py` — add `model_types`, `charge_states` fields
- `tools/ngspice_migrate/transformer.py` — add sensitivity stripping pass

### New Python test files
- `tools/tests/test_gen_model_card.py`
- `tools/tests/test_gen_parser.py`
- `tools/tests/test_gen_test.py`
- `tools/tests/test_sensitivity_strip.py`
- `tools/tests/test_resolve_from_def.py`
- `tools/tests/test_compute_trunc_gen.py`

### C++ test restructuring
- `tests/devices/bsim4v7/` — new directory with split-out BSIM4v7 tests
- `tests/devices/dio/` — new directory with split-out DIO tests
- (similar for bjt, jfet, mos1, bsim3, vbic)

---

## Phase 1: BSIM4v7 Debugging

### Task 1: DC Operating Point Audit

**Files:**
- Create: `tests/devices/bsim4v7/test_bsim4v7_dc_audit.cpp`
- Create: `tests/devices/bsim4v7/circuits/nmos_single_bias.cir`
- Create: `tests/devices/bsim4v7/CMakeLists.txt`

- [ ] **Step 1: Create test circuit — single NMOS at known bias**

```spice
* Single NMOS DC bias point for audit
.param vgs_val=1.0 vds_val=1.8

vgs gate 0 dc {vgs_val}
vds drain 0 dc {vds_val}
m1 drain gate 0 0 nch W=10u L=100n

.model nch nmos level=14 version=4.7

.op
.end
```

Write to `tests/devices/bsim4v7/circuits/nmos_single_bias.cir`.

- [ ] **Step 2: Create CMakeLists.txt for bsim4v7 test directory**

```cmake
file(GLOB BSIM4V7_TEST_SOURCES "*.cpp")

add_executable(test_bsim4v7_device ${BSIM4V7_TEST_SOURCES})
target_link_libraries(test_bsim4v7_device PRIVATE neospice_lib ngspice_runner GTest::gtest_main)
target_compile_definitions(test_bsim4v7_device PRIVATE
    TEST_CIRCUITS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/circuits"
    NGSPICE_BINARY="${NGSPICE_BINARY}")
target_include_directories(test_bsim4v7_device PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)

add_test(NAME bsim4v7_device COMMAND test_bsim4v7_device)
```

Write to `tests/devices/bsim4v7/CMakeLists.txt`.

- [ ] **Step 3: Write DC audit test**

```cpp
#include <gtest/gtest.h>
#include "ngspice_runner.hpp"
#include "simulator.hpp"
#include "compare.hpp"

class BSIM4v7DCAudit : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

TEST_F(BSIM4v7DCAudit, SingleNMOS_OperatingPoint) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_single_bias.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);

    // Tight tolerance — this is the primary accuracy target
    auto cmp = compare_dc(ng_result, cs_result, {5e-2, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

Write to `tests/devices/bsim4v7/test_bsim4v7_dc_audit.cpp`.

- [ ] **Step 4: Add bsim4v7 test subdir to tests CMakeLists.txt**

In `tests/CMakeLists.txt`, add:
```cmake
add_subdirectory(devices/bsim4v7)
```

- [ ] **Step 5: Build and run the audit test**

Run: `cmake --build build --target test_bsim4v7_device && ./build/tests/devices/bsim4v7/test_bsim4v7_device`

The test will likely fail with a large error. Record the actual `worst_error` value — this is the baseline for improvement.

- [ ] **Step 6: Commit**

```bash
git add tests/devices/bsim4v7/
git commit -m "test: add BSIM4v7 DC operating point audit test"
```

### Task 2: Model Parameter Audit

**Files:**
- Create: `tests/devices/bsim4v7/test_bsim4v7_param_audit.cpp`

- [ ] **Step 1: Write parameter dump test**

This test creates a BSIM4v7 model card via `to_bsim4_card()`, then reads key parameters from the UCB struct and compares them against ngspice's defaults. The goal is to find parameters that are missing, wrongly mapped, or have incorrect defaults.

```cpp
#include <gtest/gtest.h>
#include "parser/model_cards.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include "ngspice_runner.hpp"

class BSIM4v7ParamAudit : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
};

TEST_F(BSIM4v7ParamAudit, DefaultModelParams) {
    // Create a minimal model card with just type set
    neospice::ModelCard card;
    card.name = "nch";
    card.type = "nmos";
    auto bsim_card = neospice::to_bsim4_card(card);

    // Run ngspice to get its defaults via 'show' command
    auto ng_params = ngspice_->get_model_params(
        ".model nch nmos level=14 version=4.7");

    // Compare critical parameters
    auto& ucb = bsim_card->ucb;

    // Check that VTH0, K1, K2, TOXE, etc. match ngspice defaults
    // These are the parameters most likely to affect DC accuracy
    if (ng_params.count("vth0"))
        EXPECT_NEAR(ucb.BSIM4v7vth0, ng_params["vth0"], 1e-6)
            << "VTH0 mismatch";
    if (ng_params.count("toxe"))
        EXPECT_NEAR(ucb.BSIM4v7toxe, ng_params["toxe"], 1e-12)
            << "TOXE mismatch";
    if (ng_params.count("k1"))
        EXPECT_NEAR(ucb.BSIM4v7k1, ng_params["k1"], 1e-6)
            << "K1 mismatch";
    // Add more parameters as needed based on what the DC audit reveals
}
```

Write to `tests/devices/bsim4v7/test_bsim4v7_param_audit.cpp`.

Note: `NgspiceRunner::get_model_params()` may need to be added if it doesn't exist. If not available, use `ngspice_->run_raw()` with a script that uses `show` to dump model params, then parse the output.

- [ ] **Step 2: Build and run**

Run: `cmake --build build --target test_bsim4v7_device && ./build/tests/devices/bsim4v7/test_bsim4v7_device --gtest_filter=*ParamAudit*`

Record all mismatched parameters. These are candidate root causes for the DC error.

- [ ] **Step 3: Fix any parameter mapping bugs found**

For each mismatched parameter, trace through `to_bsim4_card()` in `src/parser/model_cards.cpp` and the `BSIM4v7mParam()` dispatcher in `src/devices/bsim4v7/bsim4v7_mpar.cpp`. Fix the root cause. Common issues:
- Parameter keyword in mPTable doesn't match ngspice's name
- Default value not set in UCB struct initialization
- Type dispatch masking wrong (IF_REAL vs IF_INTEGER)

- [ ] **Step 4: Commit fixes**

```bash
git add -u
git commit -m "fix(bsim4v7): correct model parameter mapping [list specific params]"
```

### Task 3: Evaluate() Orchestration Audit

**Files:**
- Modify: `src/devices/bsim4v7/bsim4v7_device.cpp` (if bugs found)
- Modify: `src/devices/bsim4v7/bsim4v7_shim.hpp` (if bugs found)

- [ ] **Step 1: Add verbose mode to BSIM4v7 evaluate()**

Temporarily instrument `BSIM4v7Device::evaluate()` to dump ghost voltage array, RHS, and key state variables after each load() call. Gate behind `NEOSPICE_DEBUG_BSIM4` environment variable.

Key things to check:
- Ghost voltage array sizing: must be `max_node + 1` in UCB convention (1-based)
- Node index mapping: `neo_to_ucb()` must produce correct 1-based indices
- Mode flags: `ckt.CKTmode` must match what ngspice uses at the same solver stage
- RHS indexing: UCB uses 1-based RHS arrays; verify ghost_rhs is sized correctly
- Integration coefficients: `ckt.CKTag[0..7]` must match ngspice's coefficients

- [ ] **Step 2: Compare against ngspice debug output**

Run ngspice with debug mode (`set ngdebug`) or instrument ngspice's b4v7ld.c to dump the same values at the first Newton iteration. Compare:
- Terminal voltages seen by the load function
- Ids computed
- Matrix stamp values (diagonal and off-diagonal)
- RHS contributions

- [ ] **Step 3: Fix adapter/shim bugs found**

Common issues to look for:
- Ghost voltage array off-by-one (UCB expects node[0] = ground = 0.0)
- Missing temperature initialization before first temp() call
- Integration order not set correctly for DC (should be 0 or 1)
- Mode flags not matching ngspice's sequence (MODEDCOP → MODEINITJCT → MODEINITFIX → MODEINITPRED)

- [ ] **Step 4: Re-run DC audit test, verify improvement**

Run: `./build/tests/devices/bsim4v7/test_bsim4v7_device --gtest_filter=*SingleNMOS*`

Target: worst_error < 0.05 (5%).

- [ ] **Step 5: Commit fixes**

```bash
git add -u
git commit -m "fix(bsim4v7): [describe adapter/shim bugs fixed]"
```

### Task 4: Tighten Existing BSIM4v7 Test Tolerances

**Files:**
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Tighten NMOS_DC_IV tolerance**

After Phase 1 fixes, tighten the tolerance from `{5.0, 1e-6}` to the best achievable:

```cpp
// In test_ngspice_compare.cpp, NMOS_DC_IV test:
auto cmp = compare_dc(ng_result, cs_result, {5e-2, 1e-6});  // was {5.0, 1e-6}
```

- [ ] **Step 2: Tighten transient tolerances**

```cpp
// CMOSInverterTransient:
auto cmp = compare_transient(*cs_result.transient, ng_result, {5e-2, 5e-2});  // was {2.5e-1, 5e-2}

// CMOSInverterTransientWithResistance:
auto cmp = compare_transient(ng_result, *cs_result.transient, {1e-1, 5e-2});  // was {5e-1, 5e-2}
```

If any test still fails at the tighter tolerance, record the actual error and keep the tolerance at the minimum passing value. Document why in a comment.

- [ ] **Step 3: Run full test suite**

Run: `cmake --build build && ctest --output-on-failure`

All tests must pass.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/test_ngspice_compare.cpp
git commit -m "test(bsim4v7): tighten DC and transient tolerances after adapter fixes"
```

### Task 5: Classify Bugs for Tool Improvements

No code changes — this is an analysis step.

- [ ] **Step 1: Catalog all bugs found in Tasks 1-4**

For each bug, record:
- **Location**: which generator produced the buggy code (gen_adapter, gen_shim, transformer, or manual code)
- **Category**: tool-fixable or framework-only
- **Fix**: what change to the generator would prevent this bug in future migrations

- [ ] **Step 2: Document in the plan**

Update this plan's Phase 2 tasks with any additional generator fixes needed based on the bug catalog. If new categories of bugs were found that aren't covered by the planned generators, add new tasks.

---

## Phase 2: Tool Improvements

### Task 6: Add `model_types` and `charge_states` to Descriptor

**Files:**
- Modify: `tools/ngspice_migrate/descriptor.py`
- Test: `tools/tests/test_descriptor.py`

- [ ] **Step 1: Write failing test for new descriptor fields**

```python
# In tools/tests/test_descriptor.py, add:

def test_model_types_loaded():
    """model_types parsed from YAML into ModelType list."""
    yaml_text = """
model:
  ngspice_prefix: "DIO"
  neospice_name: "dio"
  neospice_namespace: "dio"
  instance_struct: "DIOinstance"
  model_struct: "DIOmodel"
  instance_tag: "sDIOinstance"
  model_tag: "sDIOmodel"
  cpp_instance: "DIOInstance"
  cpp_model: "DIOModel"
  terminals:
    - { name: "pos", field: "DIOposNode" }
    - { name: "neg", field: "DIOnegNode" }
  state_count: 5
  state_base_field: "DIOstate"
  next_instance_field: "DIOnextInstance"
  instances_field: "DIOinstances"
  next_model_field: "DIOnextModel"
  model_ptr_field: "DIOmodPtr"
  name_field: "DIOname"
  source_files:
    setup: "diosetup.c"
    load: "dioload.c"
    temp: "diotemp.c"
    devsup: "dio.c"
  setup_function: "DIOsetup"
  temp_function: "DIOtemp"
  load_function: "DIOload"
  model_types:
    - { spice_name: "d", flag_field: "", flag_value: 0 }
  charge_states: [3]
"""
    import tempfile, pathlib
    with tempfile.NamedTemporaryFile(suffix=".yaml", mode="w", delete=False) as f:
        f.write(yaml_text)
        f.flush()
        desc = load_descriptor(pathlib.Path(f.name))
    assert len(desc.model_types) == 1
    assert desc.model_types[0].spice_name == "d"
    assert desc.charge_states == [3]
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools && python -m pytest tests/test_descriptor.py::test_model_types_loaded -v`

Expected: FAIL — `ModelDescriptor` has no `model_types` or `charge_states` attributes.

- [ ] **Step 3: Add dataclasses and fields to descriptor.py**

In `tools/ngspice_migrate/descriptor.py`, add after the `GeomParam` dataclass:

```python
@dataclass
class ModelType:
    spice_name: str    # e.g. "nmos", "pmos", "d", "npn", "pnp", "njf", "pjf"
    flag_field: str    # e.g. "BSIM4v7type" — empty if no type flag needed
    flag_value: int    # e.g. 1 for NMOS, -1 for PMOS, 0 if no flag
```

Add to `ModelDescriptor`:
```python
    model_types: List[ModelType] = field(default_factory=list)
    charge_states: List[int] = field(default_factory=list)
```

In `load_descriptor()`, parse these new fields from the YAML `model_types` and `charge_states` keys. `model_types` is a list of dicts; `charge_states` is a list of ints (optional, defaults to empty).

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tools && python -m pytest tests/test_descriptor.py::test_model_types_loaded -v`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/ngspice_migrate/descriptor.py tools/tests/test_descriptor.py
git commit -m "feat(migrate): add model_types and charge_states to descriptor"
```

### Task 7: RESOLVE Extraction from def.hpp

**Files:**
- Modify: `tools/ngspice_migrate/gen_def.py` — expose `extract_matrix_offset_fields()`
- Modify: `tools/ngspice_migrate/gen_adapter.py` — use def-based extraction
- Create: `tools/tests/test_resolve_from_def.py`

- [ ] **Step 1: Write failing test for MatrixOffset field extraction**

```python
# tools/tests/test_resolve_from_def.py
from ngspice_migrate.gen_def import extract_matrix_offset_fields

def test_extract_matrix_offset_fields():
    """Extract MatrixOffset field names from transformed def header."""
    def_text = """
struct DIOInstance {
    neospice::MatrixOffset DIOposPosPtr{-1};
    neospice::MatrixOffset DIOnegNegPtr{-1};
    neospice::MatrixOffset DIOposPrimePosPrimePtr{-1};
    double DIOarea;
    int DIOstate;
    neospice::MatrixOffset DIOposPosPrimePtr{-1};
};
"""
    fields = extract_matrix_offset_fields(def_text)
    assert fields == [
        "DIOposPosPtr",
        "DIOnegNegPtr",
        "DIOposPrimePosPrimePtr",
        "DIOposPosPrimePtr",
    ]
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools && python -m pytest tests/test_resolve_from_def.py -v`

Expected: FAIL — `extract_matrix_offset_fields` not defined.

- [ ] **Step 3: Implement extraction in gen_def.py**

Add to `tools/ngspice_migrate/gen_def.py`:

```python
import re

def extract_matrix_offset_fields(def_text: str) -> list[str]:
    """Extract field names typed as neospice::MatrixOffset from def header text."""
    pattern = r'neospice::MatrixOffset\s+(\w+)\s*\{'
    return re.findall(pattern, def_text)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tools && python -m pytest tests/test_resolve_from_def.py -v`

Expected: PASS.

- [ ] **Step 5: Update gen_adapter.py to use def-based extraction**

In `tools/ngspice_migrate/gen_adapter.py`, modify the `assign_offsets()` generation (around line 418). Instead of calling `extract_tstalloc_fields(setup_source)`, first try `extract_matrix_offset_fields(def_content)`. Fall back to TSTALLOC extraction if def content is not available.

The `generate_adapter_cpp()` function signature needs a new parameter: `def_content: str = ""`.

```python
def generate_adapter_cpp(desc: _Descriptor, setup_source: str = "",
                         def_content: str = "") -> str:
    # ...
    # In the assign_offsets section:
    if def_content:
        from .gen_def import extract_matrix_offset_fields
        fields = extract_matrix_offset_fields(def_content)
    else:
        fields = extract_tstalloc_fields(setup_source, desc.matrix_ptr_suffix)
    # ... generate RESOLVE calls from fields
```

- [ ] **Step 6: Update __main__.py to pass def_content to adapter generator**

In `tools/ngspice_migrate/__main__.py`, after generating the def header (around line 107), pass the generated def content to the adapter generator:

```python
# After line 107 (def generation):
def_content = generate_def_hpp(def_raw, desc)

# At line 126 (adapter generation), pass def_content:
adapter_cpp = generate_adapter_cpp(desc, setup_content, def_content)
```

- [ ] **Step 7: Run existing adapter tests**

Run: `cd tools && python -m pytest tests/test_gen_adapter.py -v`

All existing tests must still pass.

- [ ] **Step 8: Commit**

```bash
git add tools/ngspice_migrate/gen_def.py tools/ngspice_migrate/gen_adapter.py \
        tools/ngspice_migrate/__main__.py tools/tests/test_resolve_from_def.py
git commit -m "feat(migrate): extract RESOLVE fields from def header MatrixOffset types"
```

### Task 8: Sensitivity Stripping Pass

**Files:**
- Modify: `tools/ngspice_migrate/transformer.py`
- Create: `tools/tests/test_sensitivity_strip.py`

- [ ] **Step 1: Write failing test for sensitivity stripping**

```python
# tools/tests/test_sensitivity_strip.py
from ngspice_migrate.transformer import Transformer, TransformerConfig

def test_strip_sencond_block():
    """SenCond variable and if-block are removed."""
    cfg = TransformerConfig(
        instance_struct="DIOinstance", model_struct="DIOmodel",
        instance_tag="sDIOinstance", model_tag="sDIOmodel",
        cpp_instance="DIOInstance", cpp_model="DIOModel",
        prefix="DIO", namespace="dio",
    )
    source = '''#include "diodefs.h"
int DIOload(void *inModel, void *inCkt)
{
    int SenCond = 0;
    double x = 1.0;

    if (SenCond) {
        x = x * 2.0;
        goto next1;
    }

    x = x + 1.0;

next1:
    return 0;
}
'''
    t = Transformer(cfg)
    result = t.translate(source)
    assert "SenCond" not in result
    assert "next1" not in result
    assert "x = x + 1.0" in result


def test_strip_cktseninfo_block():
    """CKTsenInfo guard blocks are removed."""
    cfg = TransformerConfig(
        instance_struct="DIOinstance", model_struct="DIOmodel",
        instance_tag="sDIOinstance", model_tag="sDIOmodel",
        cpp_instance="DIOInstance", cpp_model="DIOModel",
        prefix="DIO", namespace="dio",
    )
    source = '''#include "diodefs.h"
int DIOload(void *inModel, void *inCkt)
{
    double x = 1.0;
    if ((info = ckt->CKTsenInfo) && ckt->CKTsenInfo->SENmode == TRANSEN) {
        x = x * 3.0;
    }
    x = x + 1.0;
    return 0;
}
'''
    t = Transformer(cfg)
    result = t.translate(source)
    assert "CKTsenInfo" not in result
    assert "SENmode" not in result
    assert "TRANSEN" not in result
    assert "x = x + 1.0" in result
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools && python -m pytest tests/test_sensitivity_strip.py -v`

Expected: FAIL — sensitivity code survives translation.

- [ ] **Step 3: Implement sensitivity stripping in transformer.py**

Add a new method to the `Transformer` class, called between `strip_omp_blocks()` and `split_banner()`:

```python
def strip_sensitivity(self, text: str) -> str:
    """Remove sensitivity analysis code blocks.

    Patterns removed:
    1. 'int SenCond = 0;' declarations
    2. 'if (SenCond) { ... }' blocks (with brace matching)
    3. 'if ((info = ckt->CKTsenInfo) ...) { ... }' blocks
    4. 'goto next1;' / 'goto next2;' statements
    5. 'next1:' / 'next2:' labels (when only used by sensitivity gotos)
    """
    import re

    # Remove SenCond declarations
    text = re.sub(r'^\s*int\s+SenCond\s*=\s*0\s*;\s*\n', '', text, flags=re.MULTILINE)

    # Remove if (SenCond) { ... } blocks — use brace-depth tracking
    text = self._strip_guarded_block(text, r'if\s*\(\s*SenCond\s*\)')

    # Remove if (... CKTsenInfo ...) { ... } blocks
    text = self._strip_guarded_block(text, r'if\s*\([^)]*CKTsenInfo[^)]*\)')

    # Remove goto next1/next2 statements
    text = re.sub(r'^\s*goto\s+next[12]\s*;\s*\n', '', text, flags=re.MULTILINE)

    # Remove next1:/next2: labels (only if no other gotos reference them)
    for label in ['next1', 'next2']:
        if f'goto {label}' not in text:
            text = re.sub(rf'^\s*{label}\s*:\s*\n', '', text, flags=re.MULTILINE)

    return text

def _strip_guarded_block(self, text: str, guard_pattern: str) -> str:
    """Remove an if-block matching guard_pattern, tracking brace depth."""
    import re
    result = []
    i = 0
    lines = text.split('\n')
    while i < len(lines):
        if re.search(guard_pattern, lines[i]):
            # Find opening brace
            depth = 0
            started = False
            while i < len(lines):
                for ch in lines[i]:
                    if ch == '{':
                        depth += 1
                        started = True
                    elif ch == '}':
                        depth -= 1
                if started and depth <= 0:
                    i += 1
                    break
                i += 1
        else:
            result.append(lines[i])
            i += 1
    return '\n'.join(result)
```

In the `translate()` method (around line 811), call `strip_sensitivity()` after `strip_omp_blocks()`:

```python
text = self.strip_omp_blocks(text)
text = self.strip_sensitivity(text)  # NEW
banner, body = self.split_banner(text)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tools && python -m pytest tests/test_sensitivity_strip.py -v`

Expected: PASS.

- [ ] **Step 5: Run all existing transformer tests**

Run: `cd tools && python -m pytest tests/test_transformer.py -v`

All must pass — sensitivity stripping must not break existing translations.

- [ ] **Step 6: Commit**

```bash
git add tools/ngspice_migrate/transformer.py tools/tests/test_sensitivity_strip.py
git commit -m "feat(migrate): add sensitivity code stripping pass to transformer"
```

### Task 9: Auto-Generate `compute_trunc()`

**Files:**
- Modify: `tools/ngspice_migrate/gen_adapter.py`
- Create: `tools/tests/test_compute_trunc_gen.py`

- [ ] **Step 1: Write failing test for NIintegrate extraction**

```python
# tools/tests/test_compute_trunc_gen.py
from ngspice_migrate.gen_adapter import extract_charge_offsets

def test_extract_charge_offsets_from_load():
    """Extract charge state offsets from NIintegrate calls in translated load."""
    load_source = """
    error = NIintegrate(ckt,&geq,&ceq,capbd,here->BSIM4v7qbd);
    if(error) return(error);
    error = NIintegrate(ckt,&geq,&ceq,capbs,here->BSIM4v7qbs);
    if(error) return(error);
    error = NIintegrate(ckt,&gcqgb,&cqgate,cqgate,here->BSIM4v7qg);
    """
    offsets = extract_charge_offsets(load_source, "BSIM4v7")
    assert "BSIM4v7qbd" in offsets
    assert "BSIM4v7qbs" in offsets
    assert "BSIM4v7qg" in offsets


def test_extract_charge_offsets_empty():
    """No NIintegrate calls returns empty list."""
    offsets = extract_charge_offsets("x = 1;", "DIO")
    assert offsets == []
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools && python -m pytest tests/test_compute_trunc_gen.py -v`

Expected: FAIL — `extract_charge_offsets` not defined.

- [ ] **Step 3: Implement charge offset extraction**

In `tools/ngspice_migrate/gen_adapter.py`, add:

```python
def extract_charge_offsets(load_source: str, prefix: str) -> list[str]:
    """Extract charge state offset field names from NIintegrate calls."""
    import re
    pattern = rf'NIintegrate\s*\([^,]+,[^,]+,[^,]+,[^,]+,\s*here->({re.escape(prefix)}\w+)\s*\)'
    return list(dict.fromkeys(re.findall(pattern, load_source)))
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tools && python -m pytest tests/test_compute_trunc_gen.py -v`

Expected: PASS.

- [ ] **Step 5: Generate full compute_trunc() implementation**

In `gen_adapter.py`, replace the stub `compute_trunc()` generation (around line 558) with a full implementation when charge offsets are available:

```python
def _gen_compute_trunc(desc, charge_offsets, charge_states_override):
    """Generate compute_trunc() body with LTE calculation."""
    # Use descriptor charge_states override if provided, else use extracted offsets
    if charge_states_override:
        # charge_states are numeric offsets relative to state base
        offset_exprs = [f"state_base_ + {off}" for off in charge_states_override]
    elif charge_offsets:
        # charge_offsets are field names like "BSIM4v7qbd"
        offset_exprs = [f"inst_.{name}" for name in charge_offsets]
    else:
        # No charge info — return stub
        return '    // TODO: no charge state offsets found — implement manually\n    return 1e30;\n'

    lines = []
    lines.append('    if (ctx.order < 2 || ctx.delta <= 0.0) return 1e30;')
    lines.append('    if (!state0_ || !state1_ || !state2_) return 1e30;')
    lines.append('')
    lines.append('    const double h0 = ctx.delta;')
    lines.append('    const double h1 = ctx.delta_old[1];')
    lines.append('    if (h1 <= 0.0) return 1e30;')
    lines.append('')
    lines.append('    double dt_min = 1e30;')
    lines.append('    const double lte_coeff = ctx.lte_coefficient();')
    lines.append('')

    for expr in offset_exprs:
        lines.append(f'    {{ // charge offset: {expr}')
        lines.append(f'        const int qcap = {expr};')
        lines.append( '        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];')
        lines.append( '        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);')
        lines.append( '        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));')
        lines.append( '        if (tol > 0.0 && std::abs(dd2) > 1e-30) {')
        lines.append( '            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));')
        lines.append( '        }')
        lines.append( '    }')

    lines.append('    return dt_min;')
    return '\n'.join(lines)
```

Wire this into the `generate_adapter_cpp()` function, replacing the existing stub.

- [ ] **Step 6: Write test verifying generated compute_trunc has LTE code**

```python
# Add to test_compute_trunc_gen.py:
def test_generated_compute_trunc_has_lte():
    """Generated compute_trunc contains LTE formula when charge offsets found."""
    from ngspice_migrate.gen_adapter import _gen_compute_trunc
    code = _gen_compute_trunc(None, ["DIOcapCharge"], [])
    assert "lte_coefficient" in code
    assert "dd2" in code
    assert "dt_min" in code
    assert "inst_.DIOcapCharge" in code
    assert "TODO" not in code


def test_generated_compute_trunc_uses_override():
    """charge_states descriptor override uses numeric offsets."""
    from ngspice_migrate.gen_adapter import _gen_compute_trunc
    code = _gen_compute_trunc(None, [], [3])
    assert "state_base_ + 3" in code
    assert "TODO" not in code
```

- [ ] **Step 7: Run all tests**

Run: `cd tools && python -m pytest tests/test_compute_trunc_gen.py tests/test_gen_adapter.py -v`

All must pass.

- [ ] **Step 8: Commit**

```bash
git add tools/ngspice_migrate/gen_adapter.py tools/tests/test_compute_trunc_gen.py
git commit -m "feat(migrate): auto-generate compute_trunc() from NIintegrate analysis"
```

### Task 10: Auto-Generate `query_param()` Skeleton

**Files:**
- Modify: `tools/ngspice_migrate/gen_adapter.py`
- Modify: `tools/tests/test_gen_adapter.py`

- [ ] **Step 1: Write failing test for parameter table extraction**

```python
# Add to tools/tests/test_gen_adapter.py:
from ngspice_migrate.gen_adapter import extract_output_params

def test_extract_output_params():
    """Extract output parameter names and fields from devsup pTable."""
    devsup_source = '''
Shim::IfParm DIOmPTable[] = {
 {"is",  DIO_IS,  IF_REAL, "model saturation current"},
};
Shim::IfParm DIOpTable[] = {
 IOP("area",   DIO_AREA,   IF_REAL, "Area factor"),
 OP("vd",      DIO_VOLTAGE, IF_REAL, "Diode voltage"),
 OPR("id",     DIO_CURRENT, IF_REAL, "Diode current"),
 OPR("gd",     DIO_CONDUCT, IF_REAL, "Diode conductance"),
};
'''
    params = extract_output_params(devsup_source, "DIO")
    names = [p['name'] for p in params]
    assert "vd" in names
    assert "id" in names
    assert "gd" in names
    assert "area" in names
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools && python -m pytest tests/test_gen_adapter.py::test_extract_output_params -v`

Expected: FAIL — `extract_output_params` not defined.

- [ ] **Step 3: Implement parameter extraction**

In `tools/ngspice_migrate/gen_adapter.py`, add:

```python
def extract_output_params(devsup_source: str, prefix: str) -> list[dict]:
    """Extract parameter entries from instance pTable in devsup source.

    Returns list of dicts: {'name': str, 'id': str, 'type': str, 'is_output': bool}
    """
    import re
    results = []
    # Match IOP, IOPR, OP, OPR, IOPA, IOPAU, IPR macro calls
    pattern = r'(IOP[ARU]*|OP[R]?)\s*\(\s*"(\w+)"\s*,\s*(\w+)\s*,\s*(\w+)'
    for m in re.finditer(pattern, devsup_source):
        macro, name, param_id, ptype = m.groups()
        is_output = macro.startswith("OP") or "O" in macro
        results.append({
            'name': name,
            'id': param_id,
            'type': ptype,
            'is_output': is_output,
        })
    return results
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tools && python -m pytest tests/test_gen_adapter.py::test_extract_output_params -v`

Expected: PASS.

- [ ] **Step 5: Generate query_param() skeleton from extracted params**

Replace the `query_param()` stub in `generate_adapter_cpp()` with a generated implementation:

```python
def _gen_query_param(desc, output_params):
    """Generate query_param() body from extracted parameter table."""
    if not output_params:
        return '    // TODO: no output parameters found — implement manually\n    return std::nullopt;\n'

    lines = []
    lines.append('    const std::string key = str_tolower(name);')
    lines.append(f'    const double m = inst_.{desc.prefix}m;')
    lines.append('')
    lines.append('    if (state0_ && state_base_ >= 0) {')
    for p in output_params:
        if p['is_output']:
            # Output params typically come from state vector — mark with TODO for field mapping
            lines.append(f'        if (key == "{p["name"]}") return 0.0; // TODO: map {p["id"]} to state/inst field, apply m scaling')
    lines.append('    }')
    lines.append('')
    lines.append('    // Geometry (not scaled by m)')
    for p in output_params:
        if not p['is_output']:
            lines.append(f'    if (key == "{p["name"]}") return inst_.{desc.prefix}{p["name"]};')
    lines.append('')
    lines.append('    return std::nullopt;')
    return '\n'.join(lines)
```

Wire this into `generate_adapter_cpp()`, passing the extracted params.

- [ ] **Step 6: Run all adapter tests**

Run: `cd tools && python -m pytest tests/test_gen_adapter.py -v`

All must pass.

- [ ] **Step 7: Commit**

```bash
git add tools/ngspice_migrate/gen_adapter.py tools/tests/test_gen_adapter.py
git commit -m "feat(migrate): auto-generate query_param() skeleton from parameter table"
```

### Task 11: Model Card Conversion Generator

**Files:**
- Create: `tools/ngspice_migrate/gen_model_card.py`
- Create: `tools/tests/test_gen_model_card.py`
- Modify: `tools/ngspice_migrate/__main__.py`

- [ ] **Step 1: Write failing test**

```python
# tools/tests/test_gen_model_card.py
from ngspice_migrate.gen_model_card import generate_model_card_hpp, generate_model_card_cpp
from test_gen_adapter import StubDescriptor, StubModelType

def make_desc():
    desc = StubDescriptor()
    desc.model_types = [
        StubModelType(spice_name="nmos", flag_field="BSIM4v7type", flag_value=1),
        StubModelType(spice_name="pmos", flag_field="BSIM4v7type", flag_value=-1),
    ]
    return desc


def test_model_card_hpp_has_function_decl():
    hpp = generate_model_card_hpp(make_desc())
    assert "to_tst_card" in hpp
    assert "ModelCard" in hpp
    assert "#pragma once" in hpp


def test_model_card_cpp_has_type_dispatch():
    cpp = generate_model_card_cpp(make_desc())
    assert '"nmos"' in cpp
    assert '"pmos"' in cpp
    assert "mPTable" in cpp
    assert "mParam" in cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools && python -m pytest tests/test_gen_model_card.py -v`

Expected: FAIL — module not found.

- [ ] **Step 3: Implement gen_model_card.py**

```python
"""Generate model card conversion functions from descriptor."""
from __future__ import annotations
from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from .descriptor import ModelDescriptor


def generate_model_card_hpp(desc: ModelDescriptor) -> str:
    """Generate <ns>_model_card.hpp with to_<ns>_card declaration."""
    ns = desc.neospice_name
    prefix = desc.prefix
    cpp_model = desc.cpp_model

    lines = [
        '#pragma once',
        f'#include "devices/{ns}/{ns}_device.hpp"',
        '#include "parser/model_cards.hpp"',
        '#include <memory>',
        '',
        'namespace neospice {',
        '',
        f'std::unique_ptr<{prefix}ModelCard> to_{ns}_card(const ModelCard& card);',
        '',
        '} // namespace neospice',
    ]
    return '\n'.join(lines) + '\n'


def generate_model_card_cpp(desc: ModelDescriptor) -> str:
    """Generate <ns>_model_card.cpp with to_<ns>_card implementation."""
    ns = desc.neospice_name
    prefix = desc.prefix
    cpp_model = desc.cpp_model

    lines = [
        f'#include "devices/{ns}/{ns}_model_card.hpp"',
        f'#include "devices/{ns}/{ns}_def.hpp"',
        f'#include "devices/{ns}/{ns}_shim.hpp"',
        '#include <algorithm>',
        '#include <cstdio>',
        '#include <cstring>',
        '#include <cctype>',
        '',
        'namespace neospice {',
        '',
        'static std::string to_lower_mc(const std::string& s) {',
        '    std::string r = s;',
        '    std::transform(r.begin(), r.end(), r.begin(), ::tolower);',
        '    return r;',
        '}',
        '',
        f'std::unique_ptr<{prefix}ModelCard> to_{ns}_card(const ModelCard& card) {{',
        f'    auto out = std::make_unique<{prefix}ModelCard>();',
        f'    auto& ucb = out->ucb;',
        '',
    ]

    # Type dispatch
    if desc.model_types:
        first = True
        for mt in desc.model_types:
            keyword = 'if' if first else '} else if'
            first = False
            lines.append(f'    {keyword} (card.type == "{mt.spice_name}") {{')
            if mt.flag_field:
                lines.append(f'        ucb.{mt.flag_field} = {mt.flag_value};')
                lines.append(f'        ucb.{mt.flag_field}Given = 1;')
        lines.append('    } else {')
        types_str = "/".join(mt.spice_name.upper() for mt in desc.model_types)
        lines.append(f'        throw ParseError("Model \'" + card.name + "\': unsupported type \'" + card.type + "\' (expected {types_str})");')
        lines.append('    }')
    lines.append('')

    # Parameter dispatch loop
    lines.append('    for (const auto& [lkey, val] : card.params) {')
    lines.append('        if (lkey == "level") continue;')
    lines.append('')
    lines.append(f'        const {ns}::Shim::IfParm* entry = nullptr;')
    lines.append(f'        for (int i = 0; i < {ns}::{prefix}mPTSize; ++i) {{')
    lines.append(f'            if (std::strcmp({ns}::{prefix}mPTable[i].keyword, lkey.c_str()) == 0) {{')
    lines.append(f'                entry = &{ns}::{prefix}mPTable[i];')
    lines.append('                break;')
    lines.append('            }')
    lines.append('        }')
    lines.append('        if (entry == nullptr) {')
    lines.append(f'            std::fprintf(stderr, "Warning: model \'%s\': unknown {prefix} parameter \'%s\' (ignored)\\n",')
    lines.append('                card.name.c_str(), lkey.c_str());')
    lines.append('            continue;')
    lines.append('        }')
    lines.append('')
    lines.append(f'        {ns}::Shim::IfValue v{{}};')
    lines.append('        int dtype = entry->dataType & 0x1F;')
    lines.append(f'        if (dtype & {ns}::Shim::IF_REAL) {{')
    lines.append('            v.rValue = val;')
    lines.append(f'        }} else if (dtype & {ns}::Shim::IF_INTEGER) {{')
    lines.append('            v.iValue = static_cast<int>(val);')
    lines.append(f'        }} else if (dtype & {ns}::Shim::IF_FLAG) {{')
    lines.append('            v.iValue = (val != 0.0) ? 1 : 0;')
    lines.append(f'        }} else if (dtype & {ns}::Shim::IF_STRING) {{')
    lines.append(f'            std::fprintf(stderr, "Warning: model \'%s\': string parameter \'%s\' not supported; using default\\n",')
    lines.append('                card.name.c_str(), lkey.c_str());')
    lines.append('            continue;')
    lines.append('        } else {')
    lines.append('            continue;')
    lines.append('        }')
    lines.append('')
    lines.append(f'        int rc = {ns}::{prefix}mParam(entry->id, &v, &ucb);')
    lines.append(f'        if (rc != {ns}::Shim::OK) {{')
    lines.append(f'            throw ParseError("Model \'" + card.name + "\': {prefix}mParam failed for \'" + lkey + "\'");')
    lines.append('        }')
    lines.append('    }')
    lines.append('')
    lines.append('    return out;')
    lines.append('}')
    lines.append('')
    lines.append('} // namespace neospice')

    return '\n'.join(lines) + '\n'
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tools && python -m pytest tests/test_gen_model_card.py -v`

Expected: PASS.

- [ ] **Step 5: Wire into __main__.py**

In `tools/ngspice_migrate/__main__.py`, after adapter generation (around line 126), add:

```python
from .gen_model_card import generate_model_card_hpp, generate_model_card_cpp

if desc.model_types:
    mc_hpp = generate_model_card_hpp(desc)
    mc_cpp = generate_model_card_cpp(desc)
    write_file(output_dir / f"{ns}_model_card.hpp", mc_hpp)
    write_file(output_dir / f"{ns}_model_card.cpp", mc_cpp)
    generated_sources.extend([f"{ns}_model_card.cpp"])
```

- [ ] **Step 6: Commit**

```bash
git add tools/ngspice_migrate/gen_model_card.py tools/tests/test_gen_model_card.py \
        tools/ngspice_migrate/__main__.py
git commit -m "feat(migrate): add model card conversion generator"
```

### Task 12: Parser Integration Generator

**Files:**
- Create: `tools/ngspice_migrate/gen_parser.py`
- Create: `tools/tests/test_gen_parser.py`
- Modify: `tools/ngspice_migrate/__main__.py`

- [ ] **Step 1: Write failing test**

```python
# tools/tests/test_gen_parser.py
from ngspice_migrate.gen_parser import generate_parser_hpp
from test_gen_adapter import StubDescriptor, StubTerminal, StubGeomParam, StubModelType

def make_desc():
    desc = StubDescriptor()
    desc.terminals = [
        StubTerminal("pos", "DIOposNode"),
        StubTerminal("neg", "DIOnegNode"),
    ]
    desc.geometry = [
        StubGeomParam("area", "DIOarea", "DIOareaGiven", "1.0", False),
        StubGeomParam("m", "DIOm", "DIOmGiven", "1.0", False),
    ]
    desc.model_types = [StubModelType("d", "", 0)]
    return desc


def test_parser_hpp_has_create_function():
    hpp = generate_parser_hpp(make_desc())
    assert "create_tst_device" in hpp
    assert "create_tst_model_card" in hpp
    assert "#pragma once" in hpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools && python -m pytest tests/test_gen_parser.py -v`

Expected: FAIL — module not found.

- [ ] **Step 3: Implement gen_parser.py**

```python
"""Generate parser integration helpers from descriptor."""
from __future__ import annotations
from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from .descriptor import ModelDescriptor


def generate_parser_hpp(desc: ModelDescriptor) -> str:
    """Generate <ns>_parser.hpp with device creation and model card helpers."""
    ns = desc.neospice_name
    prefix = desc.prefix
    terminals = desc.terminals
    geom_params = desc.geometry

    lines = [
        '#pragma once',
        f'#include "devices/{ns}/{ns}_device.hpp"',
        f'#include "devices/{ns}/{ns}_model_card.hpp"',
        '#include "parser/model_cards.hpp"',
        '#include <memory>',
        '#include <string>',
        '#include <unordered_map>',
        '',
        'namespace neospice {',
        '',
        '// --- Model card cache and creation ---',
        f'inline std::unique_ptr<{prefix}ModelCard> create_{ns}_model_card(',
        '        const ModelCard& card) {',
        f'    return to_{ns}_card(card);',
        '}',
        '',
    ]

    # Geometry struct fill helper
    lines.append(f'// --- Geometry fill helper ---')
    lines.append(f'// Call this to populate a {prefix}Device::Geom from parsed MOSFET/element geometry.')
    lines.append(f'// Adapt field names to match your deferred element struct.')
    lines.append('')

    # Device creation helper comment
    lines.append(f'// --- Device creation ---')
    lines.append(f'// Use {prefix}Device::make({", ".join(["name"] + [f"n_{t.name}" for t in terminals] + ["geom", "model_card"])})')
    lines.append(f'// Terminals: {", ".join(t.name for t in terminals)}')
    if geom_params:
        lines.append(f'// Geometry: {", ".join(g.name for g in geom_params)}')
    lines.append('')

    lines.append('} // namespace neospice')
    return '\n'.join(lines) + '\n'
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tools && python -m pytest tests/test_gen_parser.py -v`

Expected: PASS.

- [ ] **Step 5: Wire into __main__.py**

In `tools/ngspice_migrate/__main__.py`, after model card generation:

```python
from .gen_parser import generate_parser_hpp

if desc.model_types:
    parser_hpp = generate_parser_hpp(desc)
    write_file(output_dir / f"{ns}_parser.hpp", parser_hpp)
```

- [ ] **Step 6: Commit**

```bash
git add tools/ngspice_migrate/gen_parser.py tools/tests/test_gen_parser.py \
        tools/ngspice_migrate/__main__.py
git commit -m "feat(migrate): add parser integration helper generator"
```

### Task 13: Test Scaffolding Generator

**Files:**
- Create: `tools/ngspice_migrate/gen_test.py`
- Create: `tools/tests/test_gen_test.py`
- Modify: `tools/ngspice_migrate/__main__.py`

- [ ] **Step 1: Write failing test**

```python
# tools/tests/test_gen_test.py
from ngspice_migrate.gen_test import generate_test_cmake, generate_test_dc
from test_gen_adapter import StubDescriptor

def test_test_cmake_has_target():
    cmake = generate_test_cmake(StubDescriptor())
    assert "test_tst_device" in cmake
    assert "neospice_lib" in cmake
    assert "GTest" in cmake


def test_test_dc_has_fixture():
    dc = generate_test_dc(StubDescriptor())
    assert "NgspiceRunner" in dc
    assert "compare_dc" in dc
    assert "TEST_F" in dc
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools && python -m pytest tests/test_gen_test.py -v`

Expected: FAIL.

- [ ] **Step 3: Implement gen_test.py**

```python
"""Generate per-device test scaffolding."""
from __future__ import annotations
from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from .descriptor import ModelDescriptor


def generate_test_cmake(desc: ModelDescriptor) -> str:
    ns = desc.neospice_name
    return f'''file(GLOB {ns.upper()}_TEST_SOURCES "*.cpp")

add_executable(test_{ns}_device ${{{ns.upper()}_TEST_SOURCES}})
target_link_libraries(test_{ns}_device PRIVATE neospice_lib ngspice_runner GTest::gtest_main)
target_compile_definitions(test_{ns}_device PRIVATE
    TEST_CIRCUITS_DIR="${{CMAKE_CURRENT_SOURCE_DIR}}/circuits"
    NGSPICE_BINARY="${{NGSPICE_BINARY}}")
target_include_directories(test_{ns}_device PRIVATE ${{CMAKE_SOURCE_DIR}}/src ${{CMAKE_SOURCE_DIR}}/tests)

add_test(NAME {ns}_device COMMAND test_{ns}_device)
'''


def generate_test_dc(desc: ModelDescriptor) -> str:
    ns = desc.neospice_name
    prefix = desc.prefix
    class_name = f'{prefix}DCTest'
    return f'''#include <gtest/gtest.h>
#include "ngspice_runner.hpp"
#include "simulator.hpp"
#include "compare.hpp"

class {class_name} : public ::testing::Test {{
protected:
    void SetUp() override {{
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }}
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
}};

TEST_F({class_name}, BasicDC) {{
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/basic_dc.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {{5e-2, 1e-9}});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}}
'''


def generate_test_transient(desc: ModelDescriptor) -> str:
    ns = desc.neospice_name
    prefix = desc.prefix
    class_name = f'{prefix}TransientTest'
    return f'''#include <gtest/gtest.h>
#include "ngspice_runner.hpp"
#include "simulator.hpp"
#include "compare.hpp"

class {class_name} : public ::testing::Test {{
protected:
    void SetUp() override {{
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }}
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
}};

TEST_F({class_name}, BasicTransient) {{
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/basic_transient.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    auto cmp = compare_transient(*cs_result.transient, ng_result, {{5e-2, 1e-3}});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}}
'''
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tools && python -m pytest tests/test_gen_test.py -v`

Expected: PASS.

- [ ] **Step 5: Wire into __main__.py**

Add optional `--gen-tests` flag to CLI. When set, also generate test directory:

```python
from .gen_test import generate_test_cmake, generate_test_dc, generate_test_transient

if args.gen_tests:
    test_dir = output_dir.parent.parent.parent / "tests" / "devices" / ns
    test_dir.mkdir(parents=True, exist_ok=True)
    (test_dir / "circuits").mkdir(exist_ok=True)
    write_file(test_dir / "CMakeLists.txt", generate_test_cmake(desc))
    write_file(test_dir / f"test_{ns}_dc.cpp", generate_test_dc(desc))
    write_file(test_dir / f"test_{ns}_transient.cpp", generate_test_transient(desc))
```

- [ ] **Step 6: Commit**

```bash
git add tools/ngspice_migrate/gen_test.py tools/tests/test_gen_test.py \
        tools/ngspice_migrate/__main__.py
git commit -m "feat(migrate): add test scaffolding generator"
```

### Task 14: Update Descriptors with New Fields

**Files:**
- Modify: `tools/descriptors/dio.yaml`
- Modify: `tools/descriptors/bjt.yaml`
- Modify: `tools/descriptors/jfet.yaml`
- Modify: `tools/descriptors/mos1.yaml`
- Modify: `tools/descriptors/bsim3.yaml`
- Modify: `tools/descriptors/bsim4v7.yaml`
- Modify: `tools/descriptors/vbic.yaml`

- [ ] **Step 1: Add model_types and charge_states to dio.yaml**

Append to `tools/descriptors/dio.yaml`:

```yaml
  model_types:
    - { spice_name: "d", flag_field: "", flag_value: 0 }
  charge_states: [3]  # DIOcapCharge
```

- [ ] **Step 2: Add to bjt.yaml**

```yaml
  model_types:
    - { spice_name: "npn", flag_field: "BJTtype", flag_value: 1 }
    - { spice_name: "pnp", flag_field: "BJTtype", flag_value: -1 }
  charge_states: [8, 10, 12, 14]  # qbe, qbc, qsub, qbx
```

- [ ] **Step 3: Add to jfet.yaml**

```yaml
  model_types:
    - { spice_name: "njf", flag_field: "JFETtype", flag_value: 1 }
    - { spice_name: "pjf", flag_field: "JFETtype", flag_value: -1 }
  charge_states: [9, 11]  # qgs, qgd
```

- [ ] **Step 4: Add to mos1.yaml**

```yaml
  model_types:
    - { spice_name: "nmos", flag_field: "MOS1type", flag_value: 1 }
    - { spice_name: "pmos", flag_field: "MOS1type", flag_value: -1 }
  charge_states: [5, 8, 11, 13, 15]  # qgs, qgd, qgb, qbd, qbs
```

- [ ] **Step 5: Add to bsim3.yaml**

```yaml
  model_types:
    - { spice_name: "nmos", flag_field: "BSIM3type", flag_value: 1 }
    - { spice_name: "pmos", flag_field: "BSIM3type", flag_value: -1 }
```

(charge_states: leave empty — extract from NIintegrate calls automatically)

- [ ] **Step 6: Add to bsim4v7.yaml**

```yaml
  model_types:
    - { spice_name: "nmos", flag_field: "BSIM4v7type", flag_value: 1 }
    - { spice_name: "pmos", flag_field: "BSIM4v7type", flag_value: -1 }
```

(charge_states: leave empty — extract from NIintegrate calls automatically)

- [ ] **Step 7: Add to vbic.yaml**

```yaml
  model_types:
    - { spice_name: "npn", flag_field: "VBICtype", flag_value: 1 }
    - { spice_name: "pnp", flag_field: "VBICtype", flag_value: -1 }
```

- [ ] **Step 8: Verify descriptors load**

Run: `cd tools && python -c "from ngspice_migrate.descriptor import load_descriptor; from pathlib import Path; [load_descriptor(Path(f'descriptors/{d}.yaml')) for d in ['dio','bjt','jfet','mos1','bsim3','bsim4v7','vbic']]"`

Must complete without errors.

- [ ] **Step 9: Commit**

```bash
git add tools/descriptors/*.yaml
git commit -m "feat(migrate): add model_types and charge_states to all descriptors"
```

### Task 15: Validate by Re-Migrating BSIM4v7

**Files:**
- Modify: `src/devices/bsim4v7/*` (re-generated files)

- [ ] **Step 1: Back up current BSIM4v7 manual implementations**

Before re-migration, save the manually-written code that we want to preserve:
- `ac_stamp()` implementation
- `noise_sources()` implementation
- `set_ic()` implementation
- Any manual fixes from Phase 1

```bash
cp src/devices/bsim4v7/bsim4v7_device.cpp /tmp/bsim4v7_device_backup.cpp
cp src/devices/bsim4v7/bsim4v7_device.hpp /tmp/bsim4v7_device_backup.hpp
```

- [ ] **Step 2: Re-run migration tool on BSIM4v7**

```bash
python -m ngspice_migrate \
    tools/descriptors/bsim4v7.yaml \
    ~/Codes/ngspice/src/spicelib/devices/bsim4v7/ \
    src/devices/bsim4v7/ \
    --gen-tests
```

- [ ] **Step 3: Restore manual implementations**

Merge back the manual `ac_stamp()`, `noise_sources()`, `set_ic()`, and any Phase 1 fixes from the backup into the newly generated adapter file. The new file should have:
- Auto-generated `compute_trunc()` (verify charge offsets match manual version)
- Auto-generated `query_param()` skeleton (review and fix multiplier scaling)
- Auto-generated RESOLVE list (verify matches manual version)
- Preserved manual `ac_stamp()`, `noise_sources()`, `set_ic()`

- [ ] **Step 4: Build**

Run: `cmake --build build 2>&1 | head -50`

Fix any compilation errors. These inform further tool improvements.

- [ ] **Step 5: Run full test suite**

Run: `cmake --build build && ctest --output-on-failure`

All tests must pass. Compare BSIM4v7 tolerances against Phase 1 results — they should be equal or better.

- [ ] **Step 6: Commit**

```bash
git add src/devices/bsim4v7/
git commit -m "refactor(bsim4v7): re-migrate with improved tool, preserve manual implementations"
```

### Task 16: Split Test Monolith into Per-Device Directories

**Files:**
- Modify: `tests/unit/test_ngspice_compare.cpp` (remove device-specific tests)
- Create: `tests/devices/dio/test_dio_dc.cpp`, etc.
- Create: `tests/devices/bjt/`, `tests/devices/jfet/`, `tests/devices/mos1/`, `tests/devices/bsim3/`, `tests/devices/vbic/`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create per-device test directories**

For each device (dio, bjt, jfet, mos1, bsim3, vbic), create:
```
tests/devices/<device>/
  CMakeLists.txt          (same pattern as bsim4v7 from Task 1)
  circuits/               (move relevant .cir files from tests/circuits/)
  test_<device>_dc.cpp    (move DC tests from test_ngspice_compare.cpp)
  test_<device>_transient.cpp  (move transient tests)
  test_<device>_ac.cpp    (move AC tests if any)
```

- [ ] **Step 2: Move device-specific tests from monolith**

Extract each `TEST_F(NgspiceCompareTest, ...)` that is device-specific into the appropriate per-device test file. Keep shared infrastructure tests (RC, RLC, resistor divider, etc.) in the monolith.

- [ ] **Step 3: Move circuit files**

Move device-specific `.cir` files from `tests/circuits/` to `tests/devices/<device>/circuits/`. Update `TEST_CIRCUITS_DIR` references.

- [ ] **Step 4: Update CMakeLists.txt**

Add `add_subdirectory(devices/<device>)` for each device in `tests/CMakeLists.txt`.

- [ ] **Step 5: Build and run all tests**

Run: `cmake --build build && ctest --output-on-failure`

All tests must pass.

- [ ] **Step 6: Commit**

```bash
git add tests/
git commit -m "refactor(tests): split test monolith into per-device directories"
```

---

## Phase 3: New Device Migrations

### Task 17: Migrate MOS2

**Files:**
- Create: `tools/descriptors/mos2.yaml`
- Create: `src/devices/mos2/` (all generated files)
- Create: `tests/devices/mos2/`
- Modify: `src/parser/model_cards.hpp` (add include + declaration)
- Modify: `src/parser/model_cards.cpp` (add to_mos2_card if not using generated version)
- Modify: `src/parser/netlist_parser.cpp` (add LEVEL=2 dispatch)
- Modify: `src/CMakeLists.txt` (add subdirectory)

- [ ] **Step 1: Study ngspice MOS2 source**

```bash
ls ~/Codes/ngspice/src/spicelib/devices/mos2/
```

Identify: terminal count (4: d,g,s,b), state count, internal nodes, source files.

- [ ] **Step 2: Create mos2.yaml descriptor**

Follow the mos1.yaml pattern. Key fields:
- `ngspice_prefix: "MOS2"`
- `terminals: [{name: "drain", field: "MOS2dNode"}, ...]`
- `model_types: [{spice_name: "nmos", flag_field: "MOS2type", flag_value: 1}, {spice_name: "pmos", ...}]`
- `source_files: {setup: "mos2setup.c", load: "mos2load.c", temp: "mos2temp.c", ...}`

- [ ] **Step 3: Run migration tool**

```bash
python -m ngspice_migrate \
    tools/descriptors/mos2.yaml \
    ~/Codes/ngspice/src/spicelib/devices/mos2/ \
    src/devices/mos2/ \
    --gen-tests
```

- [ ] **Step 4: Build and fix compilation errors**

```bash
cmake --build build 2>&1 | head -50
```

Fix any remaining issues (likely minor include fixups or untranslated macros).

- [ ] **Step 5: Add parser integration**

Add LEVEL=2 case to the MOSFET level dispatch in `src/parser/netlist_parser.cpp` (around line 2499, after the `level == 1` block). Use the generated `to_mos2_card()` and `MOS2Device::make()`.

Add include to `src/parser/model_cards.hpp`:
```cpp
#include "devices/mos2/mos2_device.hpp"
#include "devices/mos2/mos2_model_card.hpp"
```

- [ ] **Step 6: Implement ac_stamp() manually**

Read `~/Codes/ngspice/src/spicelib/devices/mos2/mos2acld.c`. Split G/C entries following the MOS1 pattern. MOS2 uses the same Meyer capacitor model as MOS1.

- [ ] **Step 7: Create test circuits and run tests**

Create `tests/devices/mos2/circuits/nmos2_iv.cir` (basic IV curve) and a transient circuit. Run against ngspice.

- [ ] **Step 8: Commit**

```bash
git add tools/descriptors/mos2.yaml src/devices/mos2/ tests/devices/mos2/ \
        src/parser/model_cards.hpp src/parser/model_cards.cpp \
        src/parser/netlist_parser.cpp src/CMakeLists.txt
git commit -m "feat: add MOS2 (Level 2 MOSFET) device via migration tool"
```

### Task 18: Migrate MOS3

Same pattern as Task 17 but for MOS3 (Level 3 MOSFET). Use `mos3/` ngspice source. Add LEVEL=3 case to parser dispatch.

### Task 19: Migrate JFET2

Same pattern but for JFET2 (Parker-Skellern). Simpler than MOSFETs — 3 terminals, fewer states. Add J-card level dispatch (LEVEL=2) to parser.

### Task 20: Migrate BSIMSOI

Same pattern but for BSIMSOI. This is complex — SOI MOSFET with additional body terminal semantics. May require additional descriptor fields for SOI-specific features.

### Task 21: Migrate HISIM2

Same pattern but for HISIM2. Complex — Japanese foundry model with different parameter naming conventions.

---

## Phase 4: Update migrate-device Skill

### Task 22: Update Skill Documentation

**Files:**
- Modify: migrate-device skill file

- [ ] **Step 1: Update the skill doc**

Reflect the new tool capabilities:
- New auto-generated outputs (model card, parser, compute_trunc, query_param, tests)
- Reduced manual phases (Phase 4 ac_stamp is now the primary manual step)
- New descriptor fields (`model_types`, `charge_states`)
- `--gen-tests` flag
- Updated "What the Tool Handles vs Manual Work" table

- [ ] **Step 2: Commit**

```bash
git add [skill file path]
git commit -m "docs: update migrate-device skill for improved tool capabilities"
```
