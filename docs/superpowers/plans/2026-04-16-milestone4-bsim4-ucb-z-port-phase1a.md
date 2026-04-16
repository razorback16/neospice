# BSIM4v7 UCB Z-Port — Phase 1a (Scaffolding + Preprocessing)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Vendor UCB BSIM 4.7.0 source, stand up a SPICE3-compatibility shim thin enough for mechanical translation, and port the non-per-timestep routines (bsim4def.h, devsup.c, b4par.c, b4mpar.c, b4check.c, b4geo.c, b4temp.c, b4set.c). End state: the UCB preprocessing functions (`BSIM4temp`, `BSIM4setup`, `BSIM4checkModel`, `BSIM4PAeffGeo`) compile and produce outputs bit-matched to ngspice for a known model card. **Not in scope here:** `b4ld.c`, `b4acld.c`, `b4trunc.c`, Device interface wire-up, parser wire-up, re-enabling DC/transient comparison tests — those belong to Phase 1b.

**Architecture:**
- Vendor unmodified UCB source at `third_party/bsim4_4.7.0/` with the UC Berkeley `B4TERMS_OF_USE` license file preserved verbatim.
- Place the translated C++ at `src/devices/bsim4v7/`, one .cpp per UCB .c, mirroring UCB file names and symbol names (e.g. `BSIM4temp`, `BSIM4dioIjthVjmEval`). Keep UCB variable identifiers (`BSIM4u0temp`, `Vtm0`, `T0`, `T1`, …) unchanged so a reviewer can diff side-by-side against the UCB source.
- Replace SPICE3's `GENmodel`/`GENinstance` with plain C++ PODs named `BSIM4v7Model` / `BSIM4v7Instance` in namespace `neospice::bsim4v7`. Retain the full UCB field list (~600 fields) as members with identical names (`BSIM4vth0`, `BSIM4u0temp`, …) so the translated code body is unchanged.
- Replace `CKTcircuit*` with `struct Shim::Ckt` — a context struct exposing only the fields the translated code touches (temperature, gmin, tolerances, mode, state vectors, integrator coefficients).
- Replace `SMPmatrix` matrix-pointer machinery: `BSIM4setup` resolves per-entry offsets at setup time into `MatrixOffset` fields on the instance, and the Phase-1b load will stamp via direct `mat.add(off, v)` in place of `*(inst->BSIM4DdPtr) += v`.
- No Phase-1a code is visible to the rest of the simulator yet — nothing in `src/devices/bsim4v7/` is referenced from the top-level library until Phase 1b Task 14 wires it in. That isolation lets us delete the existing kernel (Task 2) without pulling the build offline.

**Tech Stack:** C++17, CMake, GoogleTest, UCB BSIM 4.7.0 (C89, ~24k LOC in scope).

**Source inventory (lines):**
| UCB file | LOC | Output C++ file | Task |
|---|---|---|---|
| `bsim4def.h` | 3624 | `bsim4v7_def.hpp` | 4 |
| `devsup.c` | 428 | `bsim4v7_devsup.cpp` | 5 |
| `b4par.c` | 193 | `bsim4v7_param.cpp` | 6 |
| `b4mpar.c` | 3619 | `bsim4v7_mpar.cpp` | 7 |
| `b4check.c` | 896 | `bsim4v7_check.cpp` | 8 |
| `b4geo.c` | 384 | `bsim4v7_geo.cpp` | 9 |
| `b4temp.c` | 2324 | `bsim4v7_temp.cpp` | 10 |
| `b4set.c` | 2521 | `bsim4v7_setup.cpp` | 11 |
| **total phase-1a** | **14 009** | | |

**Skipped permanently (out of framework scope):** `b4noi.c`, `b4pzld.c`, `inp2m.c`, `inpdomod.c`, `inpfindl.c`, `nevalsrc2.c`, `noisean.c`, `makedefs`.
**Deferred to Phase 1b/2:** `b4ld.c`, `b4acld.c`, `b4trunc.c`, `b4cvtest.c`, `b4ask.c`, `b4getic.c`, `b4del.c`, `b4mdel.c`, `b4dest.c`, `b4.c`, `bsim4itf.h`, `bsim4ext.h`, `b4mask.c`.

---

## Mechanical translation rules (apply consistently across Tasks 4–11)

These rules are the whole point of the "Z" strategy. Reviewers must be able to put the UCB `.c` and our `.cpp` side-by-side and see the same code with only syntactic replacements. Any step that deviates from this list is a red flag and must be called out in the commit message.

1. **Banner & license header**: copy the UCB banner comment block verbatim at the top of the C++ file; append one `// Translated to C++ for neospice on 2026-04-16. See third_party/bsim4_4.7.0/B4TERMS_OF_USE.` line below it.
2. **Type rewrites**:
   - `struct sBSIM4instance` / `BSIM4instance` → `BSIM4v7Instance`.
   - `struct sBSIM4model` / `BSIM4model` → `BSIM4v7Model`.
   - `GENmodel *` / `GENinstance *` → `BSIM4v7Model *` / `BSIM4v7Instance *` (UCB casts these at function entry; we drop the cast).
   - `CKTcircuit *ckt` → `Shim::Ckt *ckt`.
   - `IFvalue *` → `Shim::IfValue *`.
   - `IFuid` → `const char *`.
   - `SMPmatrix *` → `Shim::Matrix *`.
   - `Ndata *` → forward-declared, unused in Phase 1a.
3. **Include rewrites** (top of each .cpp):
   - Remove: `#include "spice.h"`, `"cktdefs.h"`, `"smpdefs.h"`, `"util.h"`, `"const.h"`, `"sperror.h"`, `"devdefs.h"`, `"suffix.h"`, `"ifsim.h"`, `"gendefs.h"`, `"trandefs.h"`, `"noisedef.h"`, `"complex.h"`.
   - Add: `#include "devices/bsim4v7/bsim4v7_def.hpp"`, `#include "devices/bsim4v7/bsim4v7_shim.hpp"`, `#include <cmath>`, `#include <cstdio>`.
4. **Math functions**: `exp`, `log`, `sqrt`, `pow`, `fabs`, `tanh`, `sinh`, `cosh`, `atan`, `floor`, `ceil` are unchanged (C++ `<cmath>` provides them in the global namespace via `<math.h>`; we include `<cmath>` and keep unqualified calls — they resolve).
5. **Constants**: keep UCB `#define` blocks (`#define Kb 1.3806226e-23`, `#define EPSSI 1.03594e-10`, `#define MAX_EXPL …`, `#define EXPL_THRESHOLD 100.0`, `#define DELTA_1 0.02`, …) at the top of each .cpp. These are file-local; duplication across files is fine and matches UCB.
6. **Error return codes**: `return(OK)` → `return 0;`. `return(E_BADPARM)` → `return Shim::E_BADPARM;`. `return(E_PARMRANGE)` → `return Shim::E_PARMRANGE;`. These live in `bsim4v7_shim.hpp` as `constexpr int`.
7. **Logging macros**: `fprintf(stderr, ...)` → unchanged. `SPfrontEnd->IFerror(...)` → `Shim::report_error(...)` (a free function we provide that forwards to `fprintf(stderr, ...)`).
8. **Model-loop pattern**: UCB code iterates `for (; model != NULL; model = model->BSIM4nextModel)` and inside `for (; here != NULL; here = here->BSIM4nextInstance)`. In Phase 1a only `BSIM4temp`/`BSIM4setup` iterate like this; we keep the loops verbatim. Phase-1b `BSIM4load` will also.
9. **Bit-fields / given flags**: `model->BSIM4vth0Given` etc. are `int` in UCB; keep as `int`. Parameter parsing will set them to 1 when user supplies a value.
10. **Matrix stamping (Phase 1a only receives `BSIM4setup`, not `BSIM4load`)**: every `TSTALLOC(name, row, col)` macro expands in UCB to `(here->name = SMPmakeElt(matrix, here->row, here->col)) == NULL`. In our shim, `Shim::Matrix::make_elt(r, c)` returns a `MatrixOffset` (not a pointer) and stores it on the instance. We rewrite each `TSTALLOC` line mechanically — e.g. `TSTALLOC(BSIM4DdPtr, BSIM4dNodePrime, BSIM4dNodePrime)` → `inst->BSIM4DdPtr = matrix->make_elt(inst->BSIM4dNodePrime, inst->BSIM4dNodePrime);`. The field type on `BSIM4v7Instance` changes from `double *` to `MatrixOffset`, mass-replaced in Task 4.
11. **State-array indices**: every `*(ckt->CKTstate0 + here->BSIM4qb)` stays verbatim in Phase 1a because `BSIM4setup` is the only user of state indices and it only *allocates* them. `here->BSIM4states = *states;  *states += BSIM4numStates;` in UCB — in our shim `states` is a `int *` pointing to a running offset and this is unchanged.
12. **Memory allocation**: UCB uses `FREE`/`CKALLOC`/`tmalloc`. In Phase 1a the only caller is `BSIM4PAeffGeo` for stress-arrays. Replace `tmalloc(sizeof(double) * n)` → `new double[n]` and `FREE(ptr)` → `delete[] ptr;`. If UCB later reassigns the pointer we must preserve `if (ptr) delete[] ptr;` guards. (Task 9 flags this.)
13. **Function signatures**: UCB uses old K&R style in some files (`BSIM4load(inModel, ckt) GENmodel *inModel; CKTcircuit *ckt;`). Rewrite to ISO C++ form (`int BSIM4load(BSIM4v7Model *model, Shim::Ckt *ckt)`). Task 10 of this plan is the only one that routinely hits K&R style — b4temp.c is already ISO.
14. **`static` linkage**: UCB file-local helpers (`static int …`) keep `static` — translate to `namespace { … }` inside the C++ file.
15. **Global tables**: `IFparm BSIM4pTable[]`, `BSIM4mPTable[]`, `BSIM4names[]` live in b4par.c / b4mpar.c / b4.c. We produce them as `constexpr std::array<Shim::IfParm, N> BSIM4pTable = { … };` in the corresponding .cpp, with the same initializer entries.

---

## File structure

```
third_party/bsim4_4.7.0/
    code/                         (unmodified UCB sources, 31 files)
    B4TERMS_OF_USE                (UCB license, copied verbatim)
    README.md                     (our acknowledgement + source pointer)

src/devices/bsim4v7/
    bsim4v7_def.hpp               (from bsim4def.h)
    bsim4v7_shim.hpp              (CKTshim, SMPshim, IFvalue stubs)
    bsim4v7_shim.cpp              (shim helper impls)
    bsim4v7_devsup.cpp            (from devsup.c)
    bsim4v7_param.cpp             (from b4par.c — BSIM4pTable, BSIM4param)
    bsim4v7_mpar.cpp              (from b4mpar.c — BSIM4mPTable, BSIM4mParam)
    bsim4v7_check.cpp             (from b4check.c — BSIM4checkModel)
    bsim4v7_geo.cpp               (from b4geo.c — BSIM4PAeffGeo et al.)
    bsim4v7_temp.cpp              (from b4temp.c — BSIM4temp)
    bsim4v7_setup.cpp             (from b4set.c — BSIM4setup)
    CMakeLists.txt                (Phase-1a library target, not yet linked)

tests/unit/
    test_bsim4v7_ucb_setup.cpp    (Task 12: golden test)
```

---

## Task 1: Vendor UCB source + license

**Files:**
- Create: `third_party/bsim4_4.7.0/code/` (copy of `/tmp/bsim4_inspect/code/`)
- Create: `third_party/bsim4_4.7.0/B4TERMS_OF_USE`
- Create: `third_party/bsim4_4.7.0/README.md`

- [ ] **Step 1: Copy the UCB source tree**

```bash
mkdir -p third_party/bsim4_4.7.0/code
cp /tmp/bsim4_inspect/code/* third_party/bsim4_4.7.0/code/
ls third_party/bsim4_4.7.0/code | wc -l
# Expected: 31
```

- [ ] **Step 2: Copy the UCB license**

```bash
cp /home/subhagato/Codes/ngspice/src/spicelib/devices/bsim4v7/B4TERMS_OF_USE third_party/bsim4_4.7.0/B4TERMS_OF_USE
head -5 third_party/bsim4_4.7.0/B4TERMS_OF_USE
# Expected: first non-blank line is "The terms under which the software is provided are as the following."
```

- [ ] **Step 3: Write the acknowledgement README**

Content of `third_party/bsim4_4.7.0/README.md`:

```markdown
# BSIM 4.7.0 (vendored)

Vendored source of the Berkeley BSIM 4.7.0 MOSFET model, released by UC Berkeley
on 2011-04-08. Authors: Weidong Liu, Xuemei Xi, Mohan Dunga, Ali Niknejad,
Wenwei Yang, Chenming Hu, Tanvir Morshed, Darsen Lu — UC Berkeley BSIM Research
Group.

Upstream: https://www.bsim.berkeley.edu/BSIM4/BSIM470.zip

This source is kept unmodified as a reference for the translated C++ port at
`src/devices/bsim4v7/`. Do not edit files in this tree; regenerate by
re-downloading the upstream zip if it ever moves.

## License

See `B4TERMS_OF_USE` in this directory. The translation at
`src/devices/bsim4v7/` is a derivative work governed by the same terms.

Neospice acknowledges the UC Berkeley BSIM Research Group as required by the
BSIM Standard Model License.
```

- [ ] **Step 4: Verify .gitignore does not exclude the vendored tree**

```bash
git check-ignore -v third_party/bsim4_4.7.0/code/b4ld.c
# Expected: nothing (exit code 1 = not ignored)
echo "exit=$?"
```

- [ ] **Step 5: Commit**

```bash
git add third_party/bsim4_4.7.0
git commit -m "$(cat <<'EOF'
vendor: UCB BSIM 4.7.0 source (reference for Z-port)

Verbatim copy of https://www.bsim.berkeley.edu/BSIM4/BSIM470.zip under
the BSIM Standard Model License (see B4TERMS_OF_USE). Will be translated
1:1 to C++ at src/devices/bsim4v7/; this tree remains unmodified as the
review-diff oracle.
EOF
)"
```

---

## Task 2: Demolish the current bsim4v7 kernel

**Files:**
- Delete: `src/devices/bsim4v7/bsim4v7.cpp`, `bsim4v7.hpp`, `bsim4v7_eval.cpp`, `bsim4v7_eval.hpp`, `bsim4v7_params.hpp`
- Delete: `tests/unit/test_bsim4v7.cpp`, `test_bsim4v7_dibl_clamp.cpp`, `test_bsim4v7_subthreshold_gm.cpp`, `test_bsim4v7_vaclm.cpp`, `test_newton_limiting.cpp`
- Modify: `src/devices/CMakeLists.txt` or whichever CMakeLists references the bsim4v7 sources — drop the bsim4v7 entries
- Modify: `tests/CMakeLists.txt` — drop the deleted test entries
- Modify: `src/parser/parser.cpp` (or wherever the `M` card is handled) — stub the M-card handler so it registers a `NullMosfet` device or skips, so the parser still returns OK on `nmos_iv.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp` — gate the four MOSFET tests (`NMOS_DC_IV`, `DISABLED_CMOSInverterTransient`, `DISABLED_RingOscillator5Stage`, any others referencing M-cards) with `GTEST_SKIP() << "MOSFET kernel rebuilding in Phase 1b"` at the top of the test body

- [ ] **Step 1: Grep for all references to the current bsim4v7 symbols**

```bash
grep -rn "BSIM4v7\|bsim4v7" src tests --include="*.cpp" --include="*.hpp" --include="CMakeLists.txt" | grep -v third_party
```

Expected output: enumerated list of every symbol / include / CMake entry that names the current kernel. This is the demolition worklist.

- [ ] **Step 2: Delete the source files**

```bash
git rm src/devices/bsim4v7/bsim4v7.cpp \
       src/devices/bsim4v7/bsim4v7.hpp \
       src/devices/bsim4v7/bsim4v7_eval.cpp \
       src/devices/bsim4v7/bsim4v7_eval.hpp \
       src/devices/bsim4v7/bsim4v7_params.hpp
```

- [ ] **Step 3: Delete the obsolete unit tests**

```bash
git rm tests/unit/test_bsim4v7.cpp \
       tests/unit/test_bsim4v7_dibl_clamp.cpp \
       tests/unit/test_bsim4v7_subthreshold_gm.cpp \
       tests/unit/test_bsim4v7_vaclm.cpp \
       tests/unit/test_newton_limiting.cpp
```

Rationale: each of these locked in a specific hand-port assertion (Ids value, gm value, VACLM slope). The UCB values will differ. We re-derive golden tests from UCB output in Phase 1b.

- [ ] **Step 4: Update `tests/CMakeLists.txt` to drop the deleted tests**

Remove these lines from the `add_executable(neospice_tests ...)` list:
```
unit/test_bsim4v7.cpp
unit/test_bsim4v7_dibl_clamp.cpp
unit/test_bsim4v7_subthreshold_gm.cpp
unit/test_bsim4v7_vaclm.cpp
unit/test_newton_limiting.cpp
```

- [ ] **Step 5: Find and stub the parser M-card handler**

Run `grep -rn '"M"\|m_card\|parse_mosfet\|BSIM4v7' src/parser` and identify the function that instantiates `BSIM4v7`. Change it to either:
- If a `NullDevice` exists: instantiate that.
- Otherwise: add a TODO comment, skip the card with `fprintf(stderr, "M card skipped: kernel under rebuild\n");`, and leave the circuit without the MOSFET.

Either way, the parser must no longer `#include "devices/bsim4v7/..."`. Remove those includes.

- [ ] **Step 6: Gate the MOSFET ngspice-compare tests**

In `tests/unit/test_ngspice_compare.cpp`, for each of:
- `NgspiceCompareTest::NMOS_DC_IV` (currently enabled)
- `DISABLED_CMOSInverterTransient` (already disabled — no change)
- `DISABLED_RingOscillator5Stage` (already disabled — no change)

Prepend to the body of `NMOS_DC_IV`:
```cpp
    GTEST_SKIP() << "MOSFET kernel under rebuild (Phase 1b of UCB Z-port)";
```

- [ ] **Step 7: Update `src/devices/CMakeLists.txt`**

Drop any `bsim4v7/` source references from the `add_library` / `target_sources` call. Leave the `src/devices/bsim4v7/` directory empty for now — Task 3 repopulates it.

- [ ] **Step 8: Build + test**

```bash
cmake --build build 2>&1 | tail -20
ctest --test-dir build --output-on-failure 2>&1 | tail -30
```

Expected: clean build, all previously-enabled non-MOSFET tests pass, NMOS_DC_IV shows as SKIPPED. Count of enabled tests should drop but pass rate stays 100%.

- [ ] **Step 9: Commit**

```bash
git add -u
git commit -m "$(cat <<'EOF'
bsim4v7: demolish hand-ported kernel ahead of UCB Z-port

Remove src/devices/bsim4v7/{bsim4v7,bsim4v7_eval,bsim4v7_params}.{cpp,hpp}
and the unit tests that asserted hand-port values (test_bsim4v7,
test_bsim4v7_dibl_clamp, test_bsim4v7_subthreshold_gm, test_bsim4v7_vaclm,
test_newton_limiting). NMOS_DC_IV ngspice-compare test is gated with
GTEST_SKIP until Phase 1b wires in the UCB translation.

Parser M-card handler stubbed out. No backward-compat shim — clean swap.
EOF
)"
```

---

## Task 3: SPICE3 shim header

**Files:**
- Create: `src/devices/bsim4v7/bsim4v7_shim.hpp`
- Create: `src/devices/bsim4v7/bsim4v7_shim.cpp`
- Create: `src/devices/bsim4v7/CMakeLists.txt` (empty object library — targets added in later tasks)
- Modify: `src/devices/CMakeLists.txt` to `add_subdirectory(bsim4v7)`

- [ ] **Step 1: Write the shim header**

Contents of `src/devices/bsim4v7/bsim4v7_shim.hpp`:

```cpp
#pragma once
#include "core/matrix.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace neospice::bsim4v7 {

// --- Error codes (subset of UCB sperror.h used by Phase 1a files) -----------
namespace Shim {
    constexpr int OK          = 0;
    constexpr int E_BADPARM   = -1;
    constexpr int E_PARMRANGE = -2;
    constexpr int E_NOMEM     = -3;
    constexpr int E_UNSUPP    = -4;

    // --- UCB IFvalue replacement -------------------------------------------
    // UCB's IFvalue is a tagged union of all parameter types. We mirror the
    // subset the model actually uses: iValue, rValue, sValue (string), vValue
    // (vector of double for IFparseTree). Tag is implicit by which accessor
    // is used; the parameter table tells the caller which to read.
    struct IfValue {
        int         iValue = 0;
        double      rValue = 0.0;
        const char *sValue = nullptr;
        // Vector value (for string arrays / real arrays):
        struct { int numValue; double *vec; } v{};
    };

    // --- UCB IFparm replacement --------------------------------------------
    struct IfParm {
        const char *keyword;
        int         id;
        int         dataType;   // UCB's IF_FLAG / IF_REAL / IF_INTEGER / IF_STRING etc.
        const char *description;
    };
    // UCB dataType flag bits we honour in Phase 1a:
    constexpr int IF_REAL    = 0x01;
    constexpr int IF_INTEGER = 0x02;
    constexpr int IF_STRING  = 0x04;
    constexpr int IF_FLAG    = 0x08;
    constexpr int IF_ASK     = 0x100;
    constexpr int IF_SET     = 0x200;
    constexpr int IF_REDUNDANT = 0x400;  // UCB uses for aliases

    // --- CKTcircuit replacement --------------------------------------------
    // Only the fields that b4temp.c + b4set.c actually read are declared here.
    // b4ld.c will extend this in Phase 1b (state vectors, integrator coeffs).
    struct Ckt {
        double CKTtemp       = 300.15;  // K; 27 C default
        double CKTnomTemp    = 300.15;
        double CKTgmin       = 1e-12;
        double CKTreltol     = 1e-3;
        double CKTabstol     = 1e-12;
        double CKTvoltTol    = 1e-6;
        int    CKTmode       = 0;        // MODEDC | MODETRAN etc. (bit flags)
        int    CKTbadMos3    = 0;        // UCB convention: unused here
        // Phase-1b extensions (declared now so BSIM4setup can reference when it
        // allocates state offsets):
        int    CKTnumStates  = 0;        // running counter updated in BSIM4setup
    };

    // --- SMPmatrix replacement ---------------------------------------------
    // UCB calls SMPmakeElt(matrix, row, col) to reserve a sparse entry and
    // get back a (double *) into the matrix's internal storage. We replace
    // that pointer with a MatrixOffset into our NumericMatrix, resolved by
    // the SparsityBuilder the caller passes in.
    class Matrix {
    public:
        Matrix(SparsityBuilder &builder) : builder_(builder) {}
        // make_elt: reserve (row, col) and return the offset. Grounds (-1)
        // return -1 so the caller can skip.
        MatrixOffset make_elt(int row, int col);
    private:
        SparsityBuilder &builder_;
    };

    // --- Error reporting stub ----------------------------------------------
    // UCB calls SPfrontEnd->IFerror(ERR_WARNING, fmt, varargs). We log to
    // stderr. The translated code uses Shim::report_error(level, fmt, ...).
    void report_error(int level, const char *fmt, ...);
    constexpr int ERR_WARNING = 1;
    constexpr int ERR_FATAL   = 2;

    // --- UCB FREE / tmalloc replacement -----------------------------------
    // UCB allocates doubles with tmalloc(sizeof(double)*n). We use new[]/delete[].
    // Keep helper inline so the translated code can call Shim::FREE(ptr) verbatim.
    template <typename T>
    inline void FREE(T *&p) { delete[] p; p = nullptr; }
    template <typename T>
    inline T *tmalloc(std::size_t n) { return new T[n](); }
} // namespace Shim

} // namespace neospice::bsim4v7
```

- [ ] **Step 2: Write the shim impl**

Contents of `src/devices/bsim4v7/bsim4v7_shim.cpp`:

```cpp
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include <cstdarg>

namespace neospice::bsim4v7::Shim {

MatrixOffset Matrix::make_elt(int row, int col) {
    if (row < 0 || col < 0) return -1;
    builder_.add(row, col);
    // Offset resolution happens later when SparsityPattern is built; caller
    // must call pattern.offset(row, col) themselves, or re-invoke make_elt
    // inside assign_offsets(). For Phase 1a BSIM4setup stores (row, col)
    // pairs as an intermediate; Phase 1b rewrites to direct offset lookup.
    return 0;  // sentinel: "reserved, resolve later"
}

void report_error(int /*level*/, const char *fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

} // namespace neospice::bsim4v7::Shim
```

- [ ] **Step 3: Write the subdir CMake file**

Contents of `src/devices/bsim4v7/CMakeLists.txt`:

```cmake
# UCB BSIM 4.7.0 Z-port (Phase 1a scaffolding).
# Sources are added task-by-task. Not yet linked into the top-level library.
add_library(bsim4v7_obj OBJECT
    bsim4v7_shim.cpp
)
target_include_directories(bsim4v7_obj PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bsim4v7_obj PUBLIC
    neospice_core  # for NumericMatrix / SparsityBuilder
)
```

Before writing this file, verify the core library target name:
```bash
grep -rn "add_library" src/CMakeLists.txt src/core/CMakeLists.txt 2>/dev/null | head -5
```
Substitute the discovered target name for `neospice_core` in the `target_link_libraries` line above.

- [ ] **Step 4: Wire the subdir into the parent CMake**

In `src/devices/CMakeLists.txt` add `add_subdirectory(bsim4v7)` (or equivalent — verify what's present).

- [ ] **Step 5: Build**

```bash
cmake --build build 2>&1 | tail -10
```

Expected: `bsim4v7_obj` builds cleanly, rest of the project still builds.

- [ ] **Step 6: Commit**

```bash
git add src/devices/bsim4v7 src/devices/CMakeLists.txt
git commit -m "$(cat <<'EOF'
bsim4v7: SPICE3 compatibility shim (CKT, SMPmatrix, IF* stubs)

Thin replacement for spice.h/cktdefs.h/smpdefs.h/ifsim.h used by the
UCB translation. Declares only the symbols Phase 1a needs (BSIM4temp
and BSIM4setup); Phase 1b will extend Shim::Ckt with state vectors
and integrator coefficients.
EOF
)"
```

---

## Task 4: Port `bsim4def.h` → `bsim4v7_def.hpp`

**Files:**
- Source: `third_party/bsim4_4.7.0/code/bsim4def.h` (3624 lines)
- Create: `src/devices/bsim4v7/bsim4v7_def.hpp`

- [ ] **Step 1: Copy the UCB header verbatim**

```bash
cp third_party/bsim4_4.7.0/code/bsim4def.h \
   src/devices/bsim4v7/bsim4v7_def.hpp
```

- [ ] **Step 2: Apply the mechanical rewrites**

Edit `src/devices/bsim4v7/bsim4v7_def.hpp` with these changes in order:

1. **Replace the include-guard**: `#ifndef BSIM4 / #define BSIM4 / #endif` → `#pragma once`.
2. **Strip UCB includes**: delete the lines `#include "ifsim.h"`, `#include "gendefs.h"`, `#include "cktdefs.h"`, `#include "complex.h"`, `#include "noisedef.h"`.
3. **Add our includes** below the banner:
   ```cpp
   #include "devices/bsim4v7/bsim4v7_shim.hpp"
   #include "core/matrix.hpp"
   ```
4. **Wrap all top-level declarations** in `namespace neospice::bsim4v7 { ... }`.
5. **Rename the structs**:
   - `typedef struct sBSIM4instance { … } BSIM4instance;` → `struct BSIM4v7Instance { … };`
   - `typedef struct sBSIM4model { … } BSIM4model;` → `struct BSIM4v7Model { … };`
   - All internal references like `struct sBSIM4model *BSIM4modPtr;` → `BSIM4v7Model *BSIM4modPtr;`.
   - Internal `struct sBSIM4instance *BSIM4nextInstance;` → `BSIM4v7Instance *BSIM4nextInstance;`.
6. **Convert `IFuid BSIM4name` → `const char *BSIM4name;`**.
7. **Convert matrix pointer fields**: every `double *BSIM4XxYyPtr;` (there are ~100 of these — they all end in `Ptr` and have type `double *`) → `MatrixOffset BSIM4XxYyPtr;`. Use a regex substitution:
   ```
   s/\bdouble \*BSIM4([A-Za-z0-9]+)Ptr;/MatrixOffset BSIM4\1Ptr;/
   ```
8. **Convert `CKTnode *` fields**: `CKTnode *BSIM4dNodePrimePtr;` (if any exist) → `int BSIM4dNodePrime;` — check with grep; bsim4def.h has `int BSIM4dNode;` style already, so this is usually a no-op.
9. **Leave all `double`, `int`, `unsigned` field declarations alone.** They are the heart of the port; renaming them would break the mechanical diff.
10. **Remove the `#ifdef __STDC__ … #else … #endif` preprocessor wraps** around forward-decls at the bottom of the file (UCB keeps both K&R and ISO signatures). Keep only the ISO branch and rewrite `extern int BSIM4load();` → remove (we'll re-declare in each .cpp).
11. **At the bottom of the file**, before the closing namespace brace, add the canonical forward declarations:
    ```cpp
    // Phase 1a forward decls — implementations live in the matching .cpp.
    int  BSIM4temp     (BSIM4v7Model *model, Shim::Ckt *ckt);
    int  BSIM4setup    (Shim::Matrix *matrix, BSIM4v7Model *model,
                        Shim::Ckt *ckt, int *states);
    int  BSIM4param    (int param, Shim::IfValue *value,
                        BSIM4v7Instance *inst, Shim::IfValue *select);
    int  BSIM4mParam   (int param, Shim::IfValue *value,
                        BSIM4v7Model *model);
    int  BSIM4checkModel(BSIM4v7Model *model, BSIM4v7Instance *here,
                        Shim::Ckt *ckt);
    int  BSIM4PAeffGeo (double nf, int geo, int minSD,
                        double Weffcj, double DMCG, double DMCI, double DMDG,
                        double *Ps, double *Pd,
                        double *As, double *Ad);
    int  BSIM4RdseffGeo(double nf, int geo, int rgeo, int minSD,
                        double Weffcj, double Rsh, double DMCG, double DMCI,
                        double DMDG, int Type, double *Rtot);
    int  BSIM4RdsEndIso(double Weffcj, double Rsh, double DMCG, double DMCI,
                        double DMDG, double nuEnd, int rgeo,
                        int Type, double *Rend);
    int  BSIM4RdsEndSha(double Weffcj, double Rsh, double DMCG, double DMCI,
                        double DMDG, double nuEnd, int rgeo,
                        int Type, double *Rend);
    int  BSIM4polyDepletion(double phi, double ngate, double epsgate,
                            double coxe, double Vgs, double *Vgs_eff,
                            double *dVgs_eff_dVg);

    // UCB parameter ID constants (the #define block near the bottom of
    // bsim4def.h). Keep the defines verbatim — they are ~1200 lines of
    // `#define BSIM4_MOD_VTH0 1001` style constants referenced by the
    // BSIM4pTable / BSIM4mPTable initializers.
    ```
12. **Keep all UCB `#define` blocks verbatim** (parameter IDs, capMod constants, `BSIM4numStates` count, etc.). They occupy the lower ~1200 lines of the file and are referenced by b4par.c / b4mpar.c / b4ld.c.

- [ ] **Step 3: Verify the file compiles as a header**

Create a scratch .cpp in `src/devices/bsim4v7/`:
```cpp
// bsim4v7_def_compile_check.cpp — deleted after verification
#include "devices/bsim4v7/bsim4v7_def.hpp"
static_assert(sizeof(neospice::bsim4v7::BSIM4v7Model) > 0);
static_assert(sizeof(neospice::bsim4v7::BSIM4v7Instance) > 0);
int main() { return 0; }
```

Add it temporarily to `bsim4v7_obj` sources, build:
```bash
cmake --build build 2>&1 | tail -20
```

Expected: clean build. Any missing type reference fails here — fix by re-checking rule 5/6/7 before proceeding.

- [ ] **Step 4: Remove the compile-check file**

```bash
rm src/devices/bsim4v7/bsim4v7_def_compile_check.cpp
# also remove its entry from bsim4v7/CMakeLists.txt
```

- [ ] **Step 5: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_def.hpp \
        src/devices/bsim4v7/CMakeLists.txt
git commit -m "$(cat <<'EOF'
bsim4v7: port bsim4def.h → bsim4v7_def.hpp (Z-port task 4)

Mechanical translation of UCB's struct definitions:
- BSIM4instance → BSIM4v7Instance, BSIM4model → BSIM4v7Model
- double * matrix-ptr fields → MatrixOffset
- #ifdef __STDC__ / K&R guards removed
All field names preserved for side-by-side review.
EOF
)"
```

---

## Task 5: Port `devsup.c` → `bsim4v7_devsup.cpp`

**Files:**
- Source: `third_party/bsim4_4.7.0/code/devsup.c` (428 lines)
- Create: `src/devices/bsim4v7/bsim4v7_devsup.cpp`
- Modify: `src/devices/bsim4v7/CMakeLists.txt` (add to sources)

**Scope:** `devsup.c` exposes diode/junction helpers (`DEVpnjlim`, `DEVlimvds`, `DEVfetlim`, `DEVsmooth`) that BSIM4 calls from load and setup. Phase 1a needs these declared and compilable; bodies are small and straight math, so we translate everything.

- [ ] **Step 1: Copy the UCB file**

```bash
cp third_party/bsim4_4.7.0/code/devsup.c \
   src/devices/bsim4v7/bsim4v7_devsup.cpp
```

- [ ] **Step 2: Apply the mechanical rewrites**

1. Strip UCB `#include "spice.h"`, `"cktdefs.h"`, `"devdefs.h"`, `"math.h"` (math goes to `<cmath>`).
2. Add `#include "devices/bsim4v7/bsim4v7_def.hpp"` and `#include <cmath>`.
3. Wrap all function definitions in `namespace neospice::bsim4v7 { … }`.
4. Keep all function bodies byte-identical except for the above.
5. Old K&R prototypes (`DEVpnjlim(vnew, vold, vt, vcrit, icheck) double vnew, vold, vt, vcrit; int *icheck;`) → rewrite to `double DEVpnjlim(double vnew, double vold, double vt, double vcrit, int *icheck)`.

- [ ] **Step 3: Forward-declare in `bsim4v7_def.hpp`**

Append to the forward-decl block (before closing namespace):
```cpp
double DEVpnjlim   (double vnew, double vold, double vt, double vcrit, int *icheck);
double DEVlimvds   (double vnew, double vold);
double DEVfetlim   (double vnew, double vold, double vto);
double DEVsmooth   (double vnew, double delta);
```

(Check `devsup.c` contents at translation time — if it defines additional helpers, add them too.)

- [ ] **Step 4: Add to CMake**

In `src/devices/bsim4v7/CMakeLists.txt` under `add_library(bsim4v7_obj OBJECT ...)`, add `bsim4v7_devsup.cpp`.

- [ ] **Step 5: Build**

```bash
cmake --build build 2>&1 | tail -10
```

- [ ] **Step 6: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_devsup.cpp \
        src/devices/bsim4v7/bsim4v7_def.hpp \
        src/devices/bsim4v7/CMakeLists.txt
git commit -m "bsim4v7: port devsup.c → bsim4v7_devsup.cpp (Z-port task 5)"
```

---

## Task 6: Port `b4par.c` → `bsim4v7_param.cpp`

**Files:**
- Source: `third_party/bsim4_4.7.0/code/b4par.c` (193 lines)
- Create: `src/devices/bsim4v7/bsim4v7_param.cpp`

**Scope:** `b4par.c` holds `BSIM4param` — the function the SPICE3 parser calls to push a single instance parameter (W, L, AS, AD, …) into a `BSIM4instance`. We need it so the Phase-1b parser can populate instance structs.

- [ ] **Step 1: Copy the file**

```bash
cp third_party/bsim4_4.7.0/code/b4par.c \
   src/devices/bsim4v7/bsim4v7_param.cpp
```

- [ ] **Step 2: Apply the mechanical rewrites**

1. Strip `#include "spice.h"`, `"ifsim.h"`, `"cktdefs.h"`, `"gendefs.h"`, `"bsim4def.h"`, `"sperror.h"`, `"suffix.h"`, `"util.h"`.
2. Add `#include "devices/bsim4v7/bsim4v7_def.hpp"`.
3. Wrap in `namespace neospice::bsim4v7 { … }`.
4. Old K&R header `BSIM4param(param, value, inst, select) int param; IFvalue *value; GENinstance *inst; IFvalue *select;` → `int BSIM4param(int param, Shim::IfValue *value, BSIM4v7Instance *here, Shim::IfValue * /*select*/)`.
5. `GENinstance *inst;  BSIM4instance *here = (BSIM4instance *)inst;` → drop the cast, just use the typed arg directly.
6. `value->rValue` / `value->iValue` references unchanged — our `Shim::IfValue` provides the same fields.
7. `return(OK);` → `return Shim::OK;`.
8. `return(E_BADPARM);` → `return Shim::E_BADPARM;`.

- [ ] **Step 3: Add the `BSIM4pTable[]` and `BSIM4pTSize`**

The parameter table normally lives in `b4.c` (we don't port b4.c in Phase 1a — it's the SPICEdev registration which our framework doesn't use). But b4par.c references `BSIM4pTable` indirectly only through the constants; the table itself is consumed by the parser.

**Option A (chosen):** move the `IFparm BSIM4pTable[]` initializer from `b4.c` into `bsim4v7_param.cpp`. Read `third_party/bsim4_4.7.0/code/b4.c` and locate the `IFparm BSIM4pTable[] = { … };` block (typically lines 50-300 — verify at port time). Copy it into our file. Translate each entry:
- `IFparm` → `Shim::IfParm`
- `IF_REAL | IF_SET | IF_ASK` bit flags unchanged (they match our `Shim::IF_*` constants).
- The keyword string, id, and description fields unchanged.

Store as:
```cpp
const Shim::IfParm BSIM4pTable[] = {
    // … verbatim initializer entries …
};
const int BSIM4pTSize = sizeof(BSIM4pTable) / sizeof(BSIM4pTable[0]);
```

- [ ] **Step 4: Build**

Add `bsim4v7_param.cpp` to `CMakeLists.txt`, build, fix any reference errors (usually missing `BSIM4_W`, `BSIM4_L` etc. enum values — those come from `bsim4def.h` parameter ID defines which we preserved in Task 4).

- [ ] **Step 5: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_param.cpp \
        src/devices/bsim4v7/CMakeLists.txt
git commit -m "bsim4v7: port b4par.c + BSIM4pTable → bsim4v7_param.cpp (Z-port task 6)"
```

---

## Task 7: Port `b4mpar.c` → `bsim4v7_mpar.cpp`

**Files:**
- Source: `third_party/bsim4_4.7.0/code/b4mpar.c` (3619 lines)
- Create: `src/devices/bsim4v7/bsim4v7_mpar.cpp`

**Scope:** `BSIM4mParam` — pushes model parameters (VTH0, U0, TOXE, K1, …) into a `BSIM4model`. This is the large one — ~800 parameters, each one a 4-line `case` block. Pure mechanical translation.

- [ ] **Step 1: Copy + rewrite** (same mechanical pattern as Task 6)

```bash
cp third_party/bsim4_4.7.0/code/b4mpar.c \
   src/devices/bsim4v7/bsim4v7_mpar.cpp
```

Apply the Task-6 rewrite list items 1-4, plus:
- `GENmodel *inModel; BSIM4model *mod = (BSIM4model *)inModel;` → drop cast; function signature is `int BSIM4mParam(int param, Shim::IfValue *value, BSIM4v7Model *mod)`.
- Lift the `BSIM4mPTable[]` initializer from `b4.c` (same as Task 6 lifted `BSIM4pTable[]`); store as `const Shim::IfParm BSIM4mPTable[]` and `const int BSIM4mPTSize`.
- Keep all case statements verbatim — they are hundreds of lines of `case BSIM4_MOD_XXX: mod->BSIM4xxx = value->rValue; mod->BSIM4xxxGiven = 1; break;` which require zero semantic changes.

- [ ] **Step 2: Build**

Expected: clean build. Any `undefined reference to BSIM4_MOD_XXX` means Task 4 lost a `#define` — go back and restore from bsim4def.h.

- [ ] **Step 3: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_mpar.cpp \
        src/devices/bsim4v7/CMakeLists.txt
git commit -m "bsim4v7: port b4mpar.c + BSIM4mPTable → bsim4v7_mpar.cpp (Z-port task 7)"
```

---

## Task 8: Port `b4check.c` → `bsim4v7_check.cpp`

**Files:**
- Source: `third_party/bsim4_4.7.0/code/b4check.c` (896 lines)
- Create: `src/devices/bsim4v7/bsim4v7_check.cpp`

**Scope:** `BSIM4checkModel` — runs after setup to validate parameter ranges, print warnings for known-bad combinations, and clip non-physical values. UCB logs warnings to `model->BSIM4modName` + a log-file descriptor `fplog`. Replace that with `stderr`.

- [ ] **Step 1: Copy + rewrite**

```bash
cp third_party/bsim4_4.7.0/code/b4check.c \
   src/devices/bsim4v7/bsim4v7_check.cpp
```

Apply the standard mechanical-rewrite list. Additional file-specific edits:
- UCB opens a log file with `fopen("bsim4.out", "w")` in `BSIM4checkModel`. Replace with `FILE *fplog = stderr;` and delete the `fclose(fplog)` at the function end.
- `fprintf(fplog, …)` calls stay verbatim.

- [ ] **Step 2: Build**

- [ ] **Step 3: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_check.cpp \
        src/devices/bsim4v7/CMakeLists.txt
git commit -m "bsim4v7: port b4check.c → bsim4v7_check.cpp (Z-port task 8)"
```

---

## Task 9: Port `b4geo.c` → `bsim4v7_geo.cpp`

**Files:**
- Source: `third_party/bsim4_4.7.0/code/b4geo.c` (384 lines)
- Create: `src/devices/bsim4v7/bsim4v7_geo.cpp`

**Scope:** `BSIM4PAeffGeo`, `BSIM4RdseffGeo`, `BSIM4RdsEndIso`, `BSIM4RdsEndSha` — compute effective source/drain area/perimeter and end resistance, given geometry mode (GEOMOD) and shared-diffusion flag. Pure math, no SPICE3 globals.

- [ ] **Step 1: Copy + rewrite**

```bash
cp third_party/bsim4_4.7.0/code/b4geo.c \
   src/devices/bsim4v7/bsim4v7_geo.cpp
```

Apply the standard mechanical rewrite list. Functions have all-`double`/`int` signatures already (no `CKTcircuit` references), so this is the simplest port of the batch.

- [ ] **Step 2: Build + commit**

```bash
cmake --build build 2>&1 | tail -5
git add src/devices/bsim4v7/bsim4v7_geo.cpp \
        src/devices/bsim4v7/CMakeLists.txt
git commit -m "bsim4v7: port b4geo.c → bsim4v7_geo.cpp (Z-port task 9)"
```

---

## Task 10: Port `b4temp.c` → `bsim4v7_temp.cpp`

**Files:**
- Source: `third_party/bsim4_4.7.0/code/b4temp.c` (2324 lines)
- Create: `src/devices/bsim4v7/bsim4v7_temp.cpp`

**Scope:** `BSIM4temp` — per-instance temperature-dependent precompute (VTH0 temp-shift, mobility temp-shift, junction saturation currents, stress effect, binning interpolation). This is the second-largest file in Phase 1a and the one most likely to have fiddly UCB-specific helpers.

- [ ] **Step 1: Copy**

```bash
cp third_party/bsim4_4.7.0/code/b4temp.c \
   src/devices/bsim4v7/bsim4v7_temp.cpp
```

- [ ] **Step 2: Apply mechanical rewrites (standard list) plus these file-specific items**

1. **Nested model/instance loops** at the top of BSIM4temp:
   ```c
   for (; model != NULL; model = model->BSIM4nextModel) {
       for (here = model->BSIM4instances; here != NULL; here = here->BSIM4nextInstance) {
   ```
   Stay verbatim. We keep the intrusive linked-list pattern because the translated `BSIM4setup` populates those list pointers.
2. `model->BSIM4type` is `NMOS`/`PMOS` (UCB defines as `1` / `-1` in `devdefs.h`). Add to `bsim4v7_def.hpp`:
   ```cpp
   constexpr int NMOS =  1;
   constexpr int PMOS = -1;
   ```
3. `EXP_THRESHOLD`, `MAX_EXP`, `MIN_EXP`, `DELTA_1`, `Kb`, `KboQ`, `EPS0`, `EPSSI`, `PI` are file-local macros — keep the `#define` block verbatim at the top.
4. **Stress effect** (mid-file): UCB uses `BSIM4DMCGeff`, `BSIM4DMCIeff`, `BSIM4DMDGeff` — these are member fields already declared in Task 4. No changes.
5. **`ckt->CKTtemp` references**: UCB reads `model->BSIM4tnom`, `ckt->CKTtemp`. Both exist on our `Shim::Ckt` and `BSIM4v7Model`. No changes.
6. Any `static` file-local helper functions: wrap in anonymous namespace (`namespace { … }`) at the top of the .cpp, before `BSIM4temp`.

- [ ] **Step 3: Build**

```bash
cmake --build build 2>&1 | tail -20
```

Expected: clean build. If the build fails with "undefined reference to `model->BSIM4xxx`" for any field, check bsim4def.h for that field and restore it to `bsim4v7_def.hpp` if missing.

- [ ] **Step 4: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_temp.cpp \
        src/devices/bsim4v7/bsim4v7_def.hpp \
        src/devices/bsim4v7/CMakeLists.txt
git commit -m "bsim4v7: port b4temp.c → bsim4v7_temp.cpp (Z-port task 10)"
```

---

## Task 11: Port `b4set.c` → `bsim4v7_setup.cpp`

**Files:**
- Source: `third_party/bsim4_4.7.0/code/b4set.c` (2521 lines)
- Create: `src/devices/bsim4v7/bsim4v7_setup.cpp`

**Scope:** `BSIM4setup` — per-instance layout setup: derives `BSIM4l`, `BSIM4w`, binning interpolation of model parameters onto each instance's size, and allocates matrix entries / state-vector offsets. This is the file where the matrix-offset rewrite (mechanical rule 10) bites hardest — expect ~150 `TSTALLOC` calls.

- [ ] **Step 1: Copy**

```bash
cp third_party/bsim4_4.7.0/code/b4set.c \
   src/devices/bsim4v7/bsim4v7_setup.cpp
```

- [ ] **Step 2: Apply mechanical rewrites (standard list) plus these file-specific items**

1. The `TSTALLOC` macro usage dominates the second half of the file. In UCB, the macro is defined at the top as:
   ```c
   #define TSTALLOC(ptr, first, second) \
       if ((here->ptr = SMPmakeElt(matrix, here->first, here->second)) == NULL) \
           { return(E_NOMEM); }
   ```
   Replace the UCB definition with a ported version that uses our matrix facade:
   ```cpp
   #define TSTALLOC(ptr, first, second) \
       do { here->ptr = matrix->make_elt(here->first, here->second); } while (0)
   ```
   (No error return — `make_elt` does not allocate dynamic memory in our backend; it only appends to the sparsity builder.)
2. The `CKTsetNodeName`, `CKTmkVolt`, `CKTmkCur` helpers are called to register internal nodes (gate-resistance node, body-resistance node, intrinsic drain-source nodes) — these allocate node numbers. Replace with direct calls to a new shim helper `Shim::Ckt::add_internal_node(ckt, name)` that delegates to our core node-allocator. Add this helper to `bsim4v7_shim.hpp`/`bsim4v7_shim.cpp` — signature:
   ```cpp
   int Shim::Ckt::add_internal_node(const char *name);
   ```
   For Phase 1a, implement it as a counter that returns sequential IDs starting from 1000 (we never execute the stamping, so the node numbers don't collide with the real parser-issued ones). Phase 1b replaces this with the real node-table registrar.
3. State-vector allocation block near the top of `BSIM4setup`:
   ```c
   here->BSIM4states = *states;
   *states += BSIM4numStates;
   ```
   Stays verbatim. `BSIM4numStates` is a `#define` in `bsim4def.h` (preserved in Task 4).
4. The `BSIM4checkModel(model, here, ckt)` call near the top of the instance loop — unchanged, our Task-8 port exposes the same signature.
5. Calls to `BSIM4PAeffGeo(…)` and `BSIM4RdseffGeo(…)` — unchanged (ported in Task 9).

- [ ] **Step 3: Build**

```bash
cmake --build build 2>&1 | tail -30
```

Watch for:
- Missing `BSIM4xxxPtr` fields on the instance — those are the matrix-offset fields from Task 4. Any missing means the regex in Task-4 Step 2.7 missed a line; fix `bsim4v7_def.hpp` and rebuild.
- Type mismatches on `matrix->make_elt` args — UCB uses `here->BSIM4dNodePrime` which is `int`. Our `make_elt(int, int)` expects the same. No cast needed.

- [ ] **Step 4: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_setup.cpp \
        src/devices/bsim4v7/bsim4v7_shim.hpp \
        src/devices/bsim4v7/bsim4v7_shim.cpp \
        src/devices/bsim4v7/CMakeLists.txt
git commit -m "bsim4v7: port b4set.c → bsim4v7_setup.cpp (Z-port task 11)"
```

---

## Task 12: Golden preprocessing test

**Files:**
- Create: `tests/unit/test_bsim4v7_ucb_setup.cpp`
- Create: `tests/goldens/bsim4v7_nmos_setup.json` (ngspice-derived expected values)
- Modify: `tests/CMakeLists.txt` (add new test source + link `bsim4v7_obj`)

**Purpose:** Prove the preprocessing chain (`BSIM4mParam` → `BSIM4param` → `BSIM4checkModel` → `BSIM4temp` → `BSIM4setup`) produces numerically-correct output for a known model card, without yet running `BSIM4load`. This is the acceptance gate for Phase 1a.

- [ ] **Step 1: Derive goldens from ngspice**

Run ngspice against a minimal probe deck with the model card from `tests/circuits/nmos_iv.cir` and a single-point `.op` analysis, then extract the instance/model state:

```bash
cat > /tmp/probe.cir <<'EOF'
NMOS probe
VDD d 0 0.1
VGS g 0 0.8
VBS b 0 0
M1 d g 0 b NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.print dc v(d) v(g) v(b) i(vdd)
.control
op
show M1
.endc
.end
EOF
/usr/bin/ngspice -b /tmp/probe.cir 2>&1 | grep -E "bsim4vth0|u0temp|BSIM4vfb|leff|weff|vsattemp" | tee /tmp/probe.out
```

Read `/tmp/probe.out` and transcribe the numerical values into `tests/goldens/bsim4v7_nmos_setup.json`:
```json
{
  "model_card": "NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9",
  "W": 1e-6, "L": 1e-7,
  "temp_K": 300.15,
  "expected": {
    "BSIM4vth0":    "<from ngspice show>",
    "BSIM4u0temp":  "<from ngspice show>",
    "BSIM4leff":    "<from ngspice show>",
    "BSIM4weff":    "<from ngspice show>",
    "BSIM4vfb":     "<from ngspice show>",
    "BSIM4vsattemp":"<from ngspice show>",
    "BSIM4k1ox":    "<from ngspice show>",
    "BSIM4cdep0":   "<from ngspice show>",
    "BSIM4phi":     "<from ngspice show>"
  },
  "tolerance_rel": 1e-10
}
```

If `show M1` in ngspice's control mode does not surface the internal precompute values, fall back to source-patching ngspice's `BSIM4temp`/`BSIM4setup` to `fprintf(stderr, "bsim4vth0=%.17e\n", here->BSIM4vth0);` and re-running. Keep that patch local; don't commit it.

- [ ] **Step 2: Write the test**

Contents of `tests/unit/test_bsim4v7_ucb_setup.cpp`:

```cpp
#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include "core/matrix.hpp"
#include <cmath>

using namespace neospice::bsim4v7;

namespace {
// Populate a model with the nmos_iv.cir card values, run the UCB
// preprocessing chain, and compare against ngspice goldens.
struct ModelFixture {
    BSIM4v7Model   model{};
    BSIM4v7Instance inst{};
    Shim::Ckt       ckt{};
    SparsityBuilder builder;
    Shim::Matrix    matrix;
    int             states = 0;

    ModelFixture() : matrix(builder) {
        // Model card: NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
        model.BSIM4type       = NMOS;
        model.BSIM4mobMod     = 0;
        model.BSIM4capMod     = 2;
        model.BSIM4rdsMod     = 0;
        model.BSIM4vth0       = 0.4;    model.BSIM4vth0Given = 1;
        model.BSIM4u0         = 0.04;   model.BSIM4u0Given   = 1;
        model.BSIM4toxe       = 2e-9;   model.BSIM4toxeGiven = 1;
        // Let BSIM4checkModel supply defaults for K1, K2, NDEP, XJ, etc.

        // Instance: W=1u, L=100n
        inst.BSIM4w = 1e-6; inst.BSIM4wGiven = 1;
        inst.BSIM4l = 1e-7; inst.BSIM4lGiven = 1;
        inst.BSIM4nf = 1.0; inst.BSIM4nfGiven = 1;
        inst.BSIM4modPtr = &model;
        model.BSIM4instances = &inst;
        model.BSIM4nextModel = nullptr;
        inst.BSIM4nextInstance = nullptr;

        ckt.CKTtemp = 300.15;
        ckt.CKTnomTemp = 300.15;

        // Node numbers — any positive ints suffice for Phase 1a setup.
        inst.BSIM4dNode = 1; inst.BSIM4gNodeExt = 2;
        inst.BSIM4sNode = 0; inst.BSIM4bNode = 3;
    }
};

TEST(BSIM4v7UCBSetup, NmosVth0K1LeffWeffMatchNgspice) {
    ModelFixture f;
    ASSERT_EQ(0, BSIM4checkModel(&f.model, &f.inst, &f.ckt));
    ASSERT_EQ(0, BSIM4temp    (&f.model, &f.ckt));
    ASSERT_EQ(0, BSIM4setup   (&f.matrix, &f.model, &f.ckt, &f.states));

    // Geometry goldens are exact (Leff = L - 2*LINT with LINT=0 default):
    EXPECT_NEAR(f.inst.BSIM4leff, 1.0e-7, 1e-17);
    EXPECT_NEAR(f.inst.BSIM4weff, 1.0e-6, 1e-16);
    EXPECT_NEAR(f.inst.BSIM4vth0, 0.4,    1e-10);
    // The remaining seven assertions use the ngspice values derived in Step 1.
    // Transcribe directly from /tmp/probe.out into these lines, using relative
    // tolerance 1e-10 scaled to each value's magnitude:
    //   EXPECT_NEAR(f.inst.BSIM4u0temp,   <ng>, std::abs(<ng>) * 1e-10);
    //   EXPECT_NEAR(f.inst.BSIM4vfb,      <ng>, std::abs(<ng>) * 1e-10);
    //   EXPECT_NEAR(f.inst.BSIM4vsattemp, <ng>, std::abs(<ng>) * 1e-10);
    //   EXPECT_NEAR(f.inst.BSIM4k1ox,     <ng>, std::abs(<ng>) * 1e-10);
    //   EXPECT_NEAR(f.inst.BSIM4cdep0,    <ng>, std::abs(<ng>) * 1e-10);
    //   EXPECT_NEAR(f.inst.BSIM4phi,      <ng>, std::abs(<ng>) * 1e-10);
    // Uncomment and substitute each <ng> with the literal from the goldens JSON.
}
} // namespace
```

- [ ] **Step 3: Wire the test into CMake**

In `tests/CMakeLists.txt`:

1. Add `unit/test_bsim4v7_ucb_setup.cpp` to the `neospice_tests` sources.
2. Add `bsim4v7_obj` to the `target_link_libraries(neospice_tests PRIVATE …)` line.

- [ ] **Step 4: Run the test**

```bash
cmake --build build 2>&1 | tail -10
ctest --test-dir build -R BSIM4v7UCBSetup --output-on-failure
```

Expected: PASS. Any FAIL means the translation of `BSIM4temp` or `BSIM4setup` diverged from UCB — rare for a mechanical port, but the likely cause is (a) a `#define` constant lost in Task 4 or (b) a missing struct field. Fix and re-run; do not relax tolerances.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/test_bsim4v7_ucb_setup.cpp \
        tests/goldens/bsim4v7_nmos_setup.json \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(bsim4v7): golden preprocessing test against ngspice goldens

Runs the Phase-1a ported UCB preprocessing chain (BSIM4checkModel →
BSIM4temp → BSIM4setup) on the NMOS model card from nmos_iv.cir and
asserts that BSIM4vth0, BSIM4u0temp, BSIM4leff, BSIM4weff, BSIM4vfb,
BSIM4vsattemp, BSIM4k1ox, BSIM4cdep0 and BSIM4phi match ngspice to
1e-10 relative tolerance. Goldens derived from ngspice show M1 output
on 2026-04-16.
EOF
)"
```

---

## Task 13: Self-review + memory update

- [ ] **Step 1: Run the full test suite**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -30
```

Expected: all previously-enabled tests still pass (minus the five MOSFET unit tests we deleted and the NMOS_DC_IV we skipped); the new `BSIM4v7UCBSetup.NmosVth0K1LeffWeffMatchNgspice` test passes.

- [ ] **Step 2: Side-by-side diff spot-check**

Pick three random 40-line windows in `bsim4v7_temp.cpp` and diff against the matching range in `third_party/bsim4_4.7.0/code/b4temp.c`, applying the mechanical rewrite rules in reverse by eye:

```bash
diff -u third_party/bsim4_4.7.0/code/b4temp.c src/devices/bsim4v7/bsim4v7_temp.cpp | head -200
```

Red flags:
- Any line of UCB code present in `b4temp.c` that is missing in our port (beyond the strippable include/header lines).
- Any numerical constant altered.
- Any `if` / loop guard changed.
- Any new code we invented that is not in UCB (bodies must be transcribed, not rewritten).

Fix any divergences found. Commit as `bsim4v7: diff-review follow-ups to temp.cpp translation` if needed.

- [ ] **Step 3: Verify license compliance**

```bash
cat third_party/bsim4_4.7.0/B4TERMS_OF_USE | head -30
grep -c "Copyright.*Regents of the University of California" \
     src/devices/bsim4v7/*.cpp src/devices/bsim4v7/*.hpp
```

Expected: non-zero on each ported file — the UCB banner must be preserved verbatim at the top.

- [ ] **Step 4: Update the mind memory**

Record completion:

```
memory_add {
  space: "projects/spice-cpp",
  name: "m4-phase1a-ucb-port-complete",
  content: "**What**: Phase 1a of UCB BSIM 4.7.0 Z-port landed.\n\n**Scope delivered**: vendored UCB source at third_party/bsim4_4.7.0/ under B4TERMS_OF_USE license. Translated bsim4def.h, devsup.c, b4par.c, b4mpar.c, b4check.c, b4geo.c, b4temp.c, b4set.c to C++ at src/devices/bsim4v7/. Added BSIM4v7UCBSetup.NmosVth0K1LeffWeffMatchNgspice golden test — passes to 1e-10.\n\n**Deleted**: prior hand-port (bsim4v7.cpp, bsim4v7_eval.cpp, bsim4v7_params.hpp) and its unit tests. NMOS_DC_IV in test_ngspice_compare.cpp gated with GTEST_SKIP — restored in Phase 1b.\n\n**Where**: src/devices/bsim4v7/ (14k LOC of translated UCB), third_party/bsim4_4.7.0/code/ (reference, read-only).\n\n**Learned**: matrix-pointer rewrite via TSTALLOC redefinition is surgical — one macro edit converts all ~150 call sites in b4set.c. IFparm tables lifted from b4.c into the matching .cpp cleanly. Shim::Ckt stays small (one dozen fields) — Phase 1b grows it when b4ld.c needs state vectors.\n\n**Next**: Phase 1b plan — port b4ld.c + BSIM4load, wire Device interface, re-baseline NMOS_DC_IV, re-enable CMOSInverterTransient and RingOscillator5Stage.",
  tags: ["cat:decision","cat:pattern","milestone:4","status:complete","area:bsim4v7"],
  links_to: ["bsim4-ucb-port-pivot-decision"]
}
```

- [ ] **Step 5: Final commit + tag**

No additional files to commit beyond what the previous tasks produced. Tag the tree:

```bash
git tag -a m4-phase1a-complete -m "BSIM4v7 UCB Z-port: Phase 1a (scaffolding + preprocessing)"
git log --oneline -10
```

Expected: ~13 commits, each one a single task, each with a clear message.

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-04-16-milestone4-bsim4-ucb-z-port-phase1a.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. For this plan, use **Opus** for Tasks 4, 7, 10, 11 (the big translations — judgement required when UCB has odd constructs) and **Sonnet** for Tasks 1, 2, 3, 5, 6, 8, 9, 12, 13 (mechanical or small).

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints for review.

**Which approach?**
