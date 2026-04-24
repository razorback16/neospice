# Refactor Tracker

Identified simplification, deduplication, and code quality issues.
Ordered by impact-to-effort ratio.

---

## R1. Shared UCB constants header [LOW EFFORT / HIGH IMPACT]

**Status:** DONE — 105 files updated, `src/devices/ucb_compat.hpp` created, `transformer.py` updated. 866/866 tests pass.

**Problem:** ~48 translated files (`_load.cpp`, `_setup.cpp`, `_temp.cpp` across 16 devices)
each contain an identical block of 55-70 lines of `#ifndef`/`#define` macros: physical
constants (`CONSTvt0`, `CHARGE`), math wrappers (`FABS`, `MAX`, `MIN`), memory helpers
(`TMALLOC`, `NG_IGNORE`), and parameter-table builders (`IOP`, `IOPU`, `IP`, `OP`, etc.).
Total: ~3,200 lines of pure duplication.

Two groups exist:
- **Group A** (BJT, DIO, JFET, JFET2): 18 macros
- **Group B** (all others): Group A + 5 extra (`IOPA`, `IOPR`, `IOPAU`, `IPR`, `OPR`)

**Fix:**
1. Create `src/devices/ucb_compat.hpp` containing the full superset of macros.
2. Replace inline blocks in all 48 files with `#include "devices/ucb_compat.hpp"`.
3. Update migration script (`tools/ngspice_migrate/transformer.py:712-783`):
   change `wrap()` to emit `#include "devices/ucb_compat.hpp"` instead of
   the inline `compat_defines` string.

**Migration script change required:** Yes — `transformer.py` `wrap()` method.

---

## R2. Shared utility functions [LOW EFFORT / MEDIUM IMPACT]

**Status:** DONE — `src/devices/ucb_utils.hpp` created with `neo_to_ucb()`, `ucb_to_neo()`, `str_tolower()`. 28 files updated (16 device, 9 model card, 2 migration scripts, 1 test). Dead `to_lower_mc()` removed from 9 model card files. 866/866 tests pass.

**Problem:** Small helper functions copy-pasted into 14-15 device files each:
- `str_tolower()` — 15 files (e.g., `bjt_device.cpp:483`, `bsim4v7_device.cpp:1123`)
- `neo_to_ucb()` / `ucb_to_neo()` — 14 files (e.g., `bjt_device.cpp:32`)
- `to_lower_mc()` — 9 model card files
- Case-insensitive comparison lambda — `ltra.cpp:1495` and others

**Fix:** Create `src/devices/ucb_utils.hpp` with these shared functions.
Update migration script `gen_adapter.py` to emit the include instead of
generating inline helpers.

**Migration script change required:** Yes — `gen_adapter.py` generates
`neo_to_ucb()` and `str_tolower()` into each `_device.cpp`.

---

## R3. Template model card parser [MEDIUM EFFORT / HIGH IMPACT]

**Status:** DONE — `src/devices/model_card_utils.hpp` created with `validate_model_type()` and `convert_model_card_params<>()`. 9 model_card.cpp files simplified (~65 lines to ~25 lines each). Migration script updated. Net -343 lines. 866/866 tests pass.

**Problem:** 9 `*_model_card.cpp` files contain nearly identical logic (~70 lines each):
`to_lower_mc()` helper, type validation (NMOS/PMOS etc.), linear parameter lookup,
type-dispatch switch (`IF_REAL`/`IF_INTEGER`/`IF_FLAG`/`IF_STRING`), and error handling.
Only the namespace prefix and type-string names differ.

Files: `bsim3v32`, `hfet1`, `hfet2`, `mos3`, `mos9`, `jfet2`, `hisim2`, `hisimhv`,
`bsimsoi` model_card.cpp.

**Fix:** Create a generic `convert_model_card<DeviceTraits>(card, shared_card)` template
in `src/devices/model_card_utils.hpp`. Each device provides a traits struct with its
namespace, parameter table pointer, table size, and valid type strings.

**Secondary fix:** Replace linear `strcmp` scan with a pre-built
`std::unordered_map<string, const IfParm*>` (currently 9 files do O(n) lookups).

**Migration script change required:** Yes — `gen_model_card.py` should generate
a traits struct + template instantiation instead of the full function body.

---

## R4. Template device initialization (declare_internal_nodes / assign_offsets) [MEDIUM EFFORT / HIGH IMPACT]

**Status:** DONE — `src/devices/ucb_device_init.hpp` created with `ucb_declare_internal_nodes<>()`, `ucb_stamp_pattern()`, `ucb_compute_offsets()`, and `UCB_SPLICE_INSTANCE` macro. 16 device files updated. Migration script updated. Net -928 lines. 866/866 tests pass.

**Problem:** 14 `*_device.cpp` files contain nearly identical implementations of
`declare_internal_nodes()` (~45 lines) and `assign_offsets()` (~40 lines):
- Create scratch matrix + shim context
- Save/restore instance linked-list pointers
- Call UCB setup function
- Process reservation journal
- Resolve matrix pointers via TSTALLOC offsets

Only the namespace, struct types, and setup function name differ.

**Fix:** Create a `UCBDeviceBase<Model, Instance>` CRTP base or a free-function
template that accepts the setup function pointer and model/instance types.

**Migration script change required:** Yes — `gen_adapter.py` generates these
methods into each `_device.cpp`.

---

## R5. AnalysisCommand as std::variant [MEDIUM EFFORT / MEDIUM IMPACT]

**Status:** DONE — Replaced flat struct with 8 per-analysis structs (`OpCmd`, `TranCmd`, `ACCmd`, etc.) and `std::variant`. `ACMode` promoted to standalone enum. Parser, runner, and tests updated. 866/866 tests pass.

**Problem:** `AnalysisCommand` in `circuit.hpp:88-111` is a flat struct with a tag enum
and mutually exclusive fields (`tran_tstep`/`ac_mode`/`noise_output`/`tf_output`/
`pz_in_pos` etc.). All fields always present in memory despite only one group being valid.

**Fix:** Define per-analysis structs (`TranCmd`, `ACCmd`, `DCCmd`, `NoiseCmd`, `TFCmd`,
`PZCmd`, `SensCmd`, `FourierCmd`) and use
`std::variant<TranCmd, ACCmd, DCCmd, NoiseCmd, TFCmd, PZCmd, SensCmd, FourierCmd>`.

---

## R6. Generic model card storage in Circuit [MEDIUM EFFORT / MEDIUM IMPACT]

**Status:** DONE — Replaced 16 forward decls + 16 methods + 16 vectors with type-erased `ModelCardHolder` and single `add_model_card<T>()` template. Net -65 lines. Adding new device types requires zero changes to circuit.hpp/cpp. 866/866 tests pass.

**Problem:** `circuit.hpp:212-227` has 16 separate
`std::vector<std::unique_ptr<XXXModelCard>>` members and 16 corresponding
`add_*_model_card()` methods in `circuit.cpp:89-151`. Adding a new device requires
touching both files to add boilerplate.

**Fix:** Type-erased container or template registration pattern. One
`add_model_card<T>(card)` method and one heterogeneous storage map keyed by `type_index`.

---

## R7. SimulationResult as std::variant [LOW EFFORT / LOW IMPACT]

**Status:** DONE — Replaced 8 `std::optional` fields with `AnalysisResult = std::variant<std::monostate, DCResult, ...>`. `measures`, `print_output`, `step` remain orthogonal. 30+ test files, CLI, and raw_writer updated. 866/866 tests pass.

**Problem:** `neospice.hpp:20-32` holds 8 `std::optional` result types but only one is
populated per run.

**Fix:** `std::variant<DCResult, TransientResult, ACResult, ...>`.

---

## R8. Shared test base fixture [LOW EFFORT / LOW IMPACT]

**Status:** DONE — `tests/devices/ngspice_compare_base.hpp` created. 17 test files updated to inherit from `NgspiceComparisonTest`. 866/866 tests pass.

**Problem:** 17 `test_*_compare.cpp` files define identical test fixtures:
```cpp
class XxxValidation : public ::testing::Test {
protected:
    void SetUp() override { ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY); }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};
```

**Fix:** Create `tests/devices/ngspice_compare_base.hpp` with a shared base class.

---

## R9. Reduce dynamic_cast dispatch [HIGH EFFORT / MEDIUM IMPACT]

**Status:** DONE — Added 3 virtual methods to Device (`branch_index()`, `process_temperature()`, `apply_ac_excitation()`). dynamic_cast count: 99 -> 63 (36% reduction). 15 files changed. Remaining casts are one-off lookups not worth abstracting. 866/866 tests pass.

**Problem:** 99 `dynamic_cast` calls for device-type dispatch in `ac.cpp`, `circuit.cpp`,
`output.cpp`, and other core files. Each new device type requires updating every dispatch
site.

**Fix:** Add virtual methods to `Device` base class (e.g., `contribute_ac_excitation()`,
`query_output_variable()`) so dispatch happens via vtable instead of RTTI chains.

---

## R10. Decompose solve_transient() [MEDIUM EFFORT / MEDIUM IMPACT]

**Status:** DONE — Extracted 15 named helper functions, 12 named constants. `solve_transient()` reduced from 624 lines to 222 lines (~120 lines of logic). 866/866 tests pass.

**Problem:** `transient.cpp:70-693` — 625 lines, 7 levels of nesting. Handles DC OP,
transient init, Newton loop, timestep control, breakpoints, and convergence in one function.
Multiple magic numbers (`100.0`, `1000`, `500000`).

**Fix:** Extract named helpers: `compute_dc_operating_point()`,
`initialize_transient_state()`, `advance_timestep()`, `check_convergence()`. Replace
magic numbers with named constants.

---

## Code quality notes (not tracked as separate items)

These are inherited from ngspice UCB translations and low priority to fix:

- **Uninitialized variables** in device load functions (e.g., `bsimsoi_load.cpp:180-250`).
  Hundreds of `double` declarations without initialization. Risk of UB.
- **`register` keyword** in UCB code — meaningless since C++11.
- **`sprintf` without bounds** in `hfet1_load.cpp:747`, `hisim2_load.cpp:1360`.
- **Macro state indexing** (`#define B4SOIvbd B4SOIstates+ 0`) — no type safety.
- **`thread_local IntegratorCtx*`** in `circuit.hpp:242` — implicit global state.
- **Massive UCB functions** (`B4SOIload` 10,821 lines) — mechanical translations, not
  worth refactoring unless rewritten from scratch.
- **4,000+ line _def.hpp headers** — inherited struct layouts, impractical to change
  without rewriting the UCB shim layer.
