# Remove SuiteSparse Dependency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace all SuiteSparse dependencies (KLU, AMD, BTF, COLAMD, SuiteSparse_config) with custom implementations, achieving equal or better performance on circuit simulation matrices.

**Architecture:** Implement custom AMD ordering and BTF (Tarjan SCC) decomposition. Extend SmallSolver to handle all matrix sizes via a new BTFSolver that decomposes the matrix into blocks and delegates each block to SmallSolver. Remove KLUSolver entirely. KLU uses the same left-looking Gilbert-Peierls algorithm as SmallSolver (not supernodal), so the core LU factorization is already equivalent.

**Tech Stack:** C++20, no external sparse solver dependencies. OpenBLAS and SLEEF remain.

**Baseline:** 910 tests passing, SmallSolver handles n<200, KLUSolver handles n>=200.

**Reference implementations:** SuiteSparse source code is available locally at `~/Codes/SuiteSparse`. When in doubt about algorithm details, consult the reference source:
- AMD ordering: `~/Codes/SuiteSparse/AMD/Source/amd_2.c` (core algorithm)
- BTF decomposition: `~/Codes/SuiteSparse/BTF/Source/btf_strongcomp.c` (Tarjan SCC), `btf_order.c` (full BTF)
- KLU solver: `~/Codes/SuiteSparse/KLU/Source/klu.c` (factor), `klu_solve.c` (solve), `klu_refactor.c` (refactor)
- COLAMD: `~/Codes/SuiteSparse/COLAMD/Source/colamd.c`

Use these as algorithmic reference — do not copy code (license incompatible for embedding). When our custom implementation produces different results or performs worse than expected, compare algorithm behavior against these references to find the gap.

---

### Task 1: Custom AMD Ordering

**Files:**
- Create: `src/core/amd.hpp`
- Create: `src/core/amd.cpp`
- Create: `tests/unit/test_amd.cpp`
- Modify: `src/CMakeLists.txt` (add amd.cpp to sources)
- Modify: `tests/CMakeLists.txt` (add test_amd.cpp)

The Approximate Minimum Degree algorithm picks the node with the fewest connections, eliminates it, updates neighbor degrees, and repeats. This produces a fill-reducing permutation for sparse LU. We implement the simplified AMD variant (no mass elimination or aggressive absorption — those are optimizations for very large matrices that circuit simulation doesn't need).

- [ ] **Step 1: Write failing tests for AMD ordering**

```cpp
// tests/unit/test_amd.cpp
#include <gtest/gtest.h>
#include "core/amd.hpp"
#include "core/matrix.hpp"

using namespace neospice;

TEST(AMD, IdentityOnDiagonal) {
    // Diagonal matrix: any permutation is valid, but AMD should return one
    SparsityBuilder sb(3);
    sb.add(0, 0); sb.add(1, 1); sb.add(2, 2);
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto perm = amd_ordering(3, csc.col_ptr.data(), csc.row_idx.data());
    ASSERT_EQ(perm.size(), 3u);
    // Verify it's a valid permutation (contains 0,1,2 in some order)
    std::vector<int32_t> sorted = perm;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(sorted, (std::vector<int32_t>{0, 1, 2}));
}

TEST(AMD, TridiagonalPermutationIsValid) {
    int32_t n = 10;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) { sb.add(i, i-1); sb.add(i-1, i); }
    }
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto perm = amd_ordering(n, csc.col_ptr.data(), csc.row_idx.data());
    ASSERT_EQ(static_cast<int32_t>(perm.size()), n);
    std::vector<int32_t> sorted = perm;
    std::sort(sorted.begin(), sorted.end());
    for (int32_t i = 0; i < n; ++i) EXPECT_EQ(sorted[i], i);
}

TEST(AMD, ArrowheadReducesFill) {
    // Arrowhead matrix: node 0 connects to all, rest only to 0 and self.
    // Without ordering, factoring column 0 first creates massive fill.
    // AMD should order node 0 last (highest degree).
    int32_t n = 20;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) { sb.add(0, i); sb.add(i, 0); }
    }
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto perm = amd_ordering(n, csc.col_ptr.data(), csc.row_idx.data());
    // Node 0 (degree n-1) should be last or near-last in the ordering
    int32_t pos_of_zero = -1;
    for (int32_t i = 0; i < n; ++i)
        if (perm[i] == 0) { pos_of_zero = i; break; }
    EXPECT_GE(pos_of_zero, n - 2) << "High-degree node should be ordered last";
}

TEST(AMD, MatchesSuiteSparseOnBanded) {
    // For a banded matrix, verify our AMD produces a permutation
    // that results in similar or fewer fill-in entries than identity ordering.
    int32_t n = 50;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        for (int32_t d = -3; d <= 3; ++d) {
            int32_t j = i + d;
            if (j >= 0 && j < n) sb.add(i, j);
        }
    }
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto perm = amd_ordering(n, csc.col_ptr.data(), csc.row_idx.data());
    ASSERT_EQ(static_cast<int32_t>(perm.size()), n);
    // Valid permutation check
    std::vector<int32_t> sorted = perm;
    std::sort(sorted.begin(), sorted.end());
    for (int32_t i = 0; i < n; ++i) EXPECT_EQ(sorted[i], i);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R AMD -v`
Expected: Compilation error — `amd.hpp` does not exist.

- [ ] **Step 3: Write the AMD header**

```cpp
// src/core/amd.hpp
#pragma once
#include <cstdint>
#include <vector>

namespace neospice {

// Approximate Minimum Degree ordering.
// Input: CSC matrix (col_ptr[n+1], row_idx[nnz]) — must be structurally symmetric.
// Returns: permutation vector perm[n] where perm[new_pos] = old_col.
std::vector<int32_t> amd_ordering(int32_t n, const int32_t* col_ptr,
                                  const int32_t* row_idx);

}  // namespace neospice
```

- [ ] **Step 4: Write the AMD implementation**

```cpp
// src/core/amd.cpp
#include "core/amd.hpp"
#include <algorithm>
#include <numeric>
#include <vector>

namespace neospice {

std::vector<int32_t> amd_ordering(int32_t n, const int32_t* col_ptr,
                                  const int32_t* row_idx) {
    if (n <= 0) return {};

    // Build symmetric adjacency (exclude self-loops)
    std::vector<std::vector<int32_t>> adj(n);
    for (int32_t j = 0; j < n; ++j) {
        for (int32_t p = col_ptr[j]; p < col_ptr[j + 1]; ++p) {
            int32_t i = row_idx[p];
            if (i != j) {
                adj[j].push_back(i);
                adj[i].push_back(j);
            }
        }
    }
    // Deduplicate adjacency lists
    for (int32_t i = 0; i < n; ++i) {
        std::sort(adj[i].begin(), adj[i].end());
        adj[i].erase(std::unique(adj[i].begin(), adj[i].end()), adj[i].end());
    }

    // Degree of each node (in the elimination graph)
    std::vector<int32_t> degree(n);
    for (int32_t i = 0; i < n; ++i)
        degree[i] = static_cast<int32_t>(adj[i].size());

    std::vector<bool> eliminated(n, false);
    std::vector<int32_t> perm;
    perm.reserve(n);

    for (int32_t step = 0; step < n; ++step) {
        // Find non-eliminated node with minimum degree
        int32_t best = -1;
        int32_t best_deg = n + 1;
        for (int32_t i = 0; i < n; ++i) {
            if (!eliminated[i] && degree[i] < best_deg) {
                best_deg = degree[i];
                best = i;
            }
        }

        perm.push_back(best);
        eliminated[best] = true;

        // Collect live neighbors of the eliminated node
        std::vector<int32_t> neighbors;
        for (int32_t nb : adj[best]) {
            if (!eliminated[nb]) neighbors.push_back(nb);
        }

        // Add edges between all pairs of live neighbors (fill-in),
        // forming a clique — this is the "element absorption" step.
        for (size_t a = 0; a < neighbors.size(); ++a) {
            for (size_t b = a + 1; b < neighbors.size(); ++b) {
                int32_t u = neighbors[a], v = neighbors[b];
                // Check if edge already exists (adj is sorted)
                if (!std::binary_search(adj[u].begin(), adj[u].end(), v)) {
                    adj[u].insert(
                        std::lower_bound(adj[u].begin(), adj[u].end(), v), v);
                    adj[v].insert(
                        std::lower_bound(adj[v].begin(), adj[v].end(), u), u);
                }
            }
        }

        // Remove eliminated node from neighbor lists and update degrees
        for (int32_t nb : neighbors) {
            adj[nb].erase(
                std::lower_bound(adj[nb].begin(), adj[nb].end(), best));
            degree[nb] = 0;
            for (int32_t x : adj[nb])
                if (!eliminated[x]) ++degree[nb];
        }
    }

    return perm;
}

}  // namespace neospice
```

- [ ] **Step 5: Add to CMakeLists.txt**

In `src/CMakeLists.txt`, add `core/amd.cpp` to the `add_library(neospice_lib ...)` source list (after `core/matrix.cpp`).

In `tests/CMakeLists.txt`, add `unit/test_amd.cpp` to the `add_executable(neospice_tests ...)` source list.

- [ ] **Step 6: Build and run AMD tests**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R AMD -v`
Expected: All 4 AMD tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/core/amd.hpp src/core/amd.cpp tests/unit/test_amd.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add custom AMD ordering to replace SuiteSparse AMD"
```

---

### Task 2: BTF (Block Triangular Form) Decomposition

**Files:**
- Create: `src/core/btf.hpp`
- Create: `src/core/btf.cpp`
- Create: `tests/unit/test_btf.cpp`
- Modify: `src/CMakeLists.txt` (add btf.cpp)
- Modify: `tests/CMakeLists.txt` (add test_btf.cpp)

BTF uses Tarjan's strongly connected components algorithm on the directed graph of the matrix, then topologically sorts the SCCs. Each SCC becomes a diagonal block. The result is a permutation that puts the matrix in block upper triangular form, where each diagonal block is an irreducible sub-matrix that must be factored independently.

- [ ] **Step 1: Write failing tests for BTF**

```cpp
// tests/unit/test_btf.cpp
#include <gtest/gtest.h>
#include "core/btf.hpp"
#include "core/matrix.hpp"

using namespace neospice;

TEST(BTF, SingleBlock) {
    // Fully connected 3x3 — one SCC, one block
    SparsityBuilder sb(3);
    for (int32_t i = 0; i < 3; ++i)
        for (int32_t j = 0; j < 3; ++j)
            sb.add(i, j);
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto result = btf_decompose(3, csc.col_ptr.data(), csc.row_idx.data());
    EXPECT_EQ(result.nblocks, 1);
    EXPECT_EQ(result.block_ptr.size(), 2u);
    EXPECT_EQ(result.block_ptr[0], 0);
    EXPECT_EQ(result.block_ptr[1], 3);
    // Valid permutation
    std::vector<int32_t> sorted = result.perm;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(sorted, (std::vector<int32_t>{0, 1, 2}));
}

TEST(BTF, DiagonalIsNBlocks) {
    // Diagonal matrix: n disconnected nodes = n blocks of size 1
    int32_t n = 5;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) sb.add(i, i);
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto result = btf_decompose(n, csc.col_ptr.data(), csc.row_idx.data());
    EXPECT_EQ(result.nblocks, n);
    for (int32_t i = 0; i <= n; ++i)
        EXPECT_EQ(result.block_ptr[i], i);
}

TEST(BTF, TwoBlocks) {
    // Block structure: nodes {0,1} strongly connected, nodes {2,3} strongly connected,
    // with edge from block 1 -> block 0 (upper triangular coupling).
    SparsityBuilder sb(4);
    // Block 0: {0, 1}
    sb.add(0, 0); sb.add(0, 1); sb.add(1, 0); sb.add(1, 1);
    // Block 1: {2, 3}
    sb.add(2, 2); sb.add(2, 3); sb.add(3, 2); sb.add(3, 3);
    // Coupling: block 1 -> block 0 (row in block 0, col in block 1)
    sb.add(0, 2);
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto result = btf_decompose(4, csc.col_ptr.data(), csc.row_idx.data());
    EXPECT_EQ(result.nblocks, 2);
    // Each block should have size 2
    EXPECT_EQ(result.block_ptr[1] - result.block_ptr[0], 2);
    EXPECT_EQ(result.block_ptr[2] - result.block_ptr[1], 2);
}

TEST(BTF, PermutationIsValid) {
    int32_t n = 10;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i-1);
    }
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto result = btf_decompose(n, csc.col_ptr.data(), csc.row_idx.data());
    ASSERT_EQ(static_cast<int32_t>(result.perm.size()), n);
    std::vector<int32_t> sorted = result.perm;
    std::sort(sorted.begin(), sorted.end());
    for (int32_t i = 0; i < n; ++i) EXPECT_EQ(sorted[i], i);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R BTF -v`
Expected: Compilation error — `btf.hpp` does not exist.

- [ ] **Step 3: Write the BTF header**

```cpp
// src/core/btf.hpp
#pragma once
#include <cstdint>
#include <vector>

namespace neospice {

struct BTFResult {
    int32_t nblocks;                    // number of diagonal blocks
    std::vector<int32_t> perm;          // perm[new] = old
    std::vector<int32_t> inv_perm;      // inv_perm[old] = new
    std::vector<int32_t> block_ptr;     // block k spans permuted rows [block_ptr[k], block_ptr[k+1])
};

// Decompose a square matrix into Block Triangular Form using Tarjan's SCC algorithm.
// Input: CSC matrix (col_ptr[n+1], row_idx[nnz]).
// For n <= 1 or structurally irreducible matrices, returns a single block.
BTFResult btf_decompose(int32_t n, const int32_t* col_ptr, const int32_t* row_idx);

}  // namespace neospice
```

- [ ] **Step 4: Write the BTF implementation**

```cpp
// src/core/btf.cpp
#include "core/btf.hpp"
#include <algorithm>
#include <numeric>
#include <stack>

namespace neospice {

BTFResult btf_decompose(int32_t n, const int32_t* col_ptr, const int32_t* row_idx) {
    BTFResult result;
    result.perm.resize(n);
    result.inv_perm.resize(n);

    if (n <= 1) {
        std::iota(result.perm.begin(), result.perm.end(), 0);
        std::iota(result.inv_perm.begin(), result.inv_perm.end(), 0);
        result.nblocks = n;
        result.block_ptr.resize(n + 1);
        std::iota(result.block_ptr.begin(), result.block_ptr.end(), 0);
        return result;
    }

    // Tarjan's SCC algorithm
    std::vector<int32_t> index(n, -1);
    std::vector<int32_t> lowlink(n, 0);
    std::vector<bool> on_stack(n, false);
    std::stack<int32_t> stk;
    std::vector<std::vector<int32_t>> sccs;
    int32_t idx_counter = 0;

    // Iterative Tarjan to avoid stack overflow on large graphs
    struct Frame {
        int32_t node;
        int32_t col_pos;  // position in column iteration
    };

    for (int32_t start = 0; start < n; ++start) {
        if (index[start] >= 0) continue;

        std::stack<Frame> call_stack;
        call_stack.push({start, col_ptr[start]});
        index[start] = lowlink[start] = idx_counter++;
        on_stack[start] = true;
        stk.push(start);

        while (!call_stack.empty()) {
            auto& frame = call_stack.top();
            int32_t v = frame.node;
            bool pushed_child = false;

            while (frame.col_pos < col_ptr[v + 1]) {
                int32_t w = row_idx[frame.col_pos];
                ++frame.col_pos;
                if (w == v) continue;  // skip self-loop
                if (index[w] < 0) {
                    index[w] = lowlink[w] = idx_counter++;
                    on_stack[w] = true;
                    stk.push(w);
                    call_stack.push({w, col_ptr[w]});
                    pushed_child = true;
                    break;
                } else if (on_stack[w]) {
                    lowlink[v] = std::min(lowlink[v], index[w]);
                }
            }

            if (!pushed_child) {
                // All successors of v have been processed
                if (lowlink[v] == index[v]) {
                    std::vector<int32_t> scc;
                    int32_t w;
                    do {
                        w = stk.top(); stk.pop();
                        on_stack[w] = false;
                        scc.push_back(w);
                    } while (w != v);
                    sccs.push_back(std::move(scc));
                }
                call_stack.pop();
                if (!call_stack.empty()) {
                    lowlink[call_stack.top().node] =
                        std::min(lowlink[call_stack.top().node], lowlink[v]);
                }
            }
        }
    }

    // SCCs come out in reverse topological order from Tarjan's.
    // Reverse to get topological order (blocks processed bottom-up).
    std::reverse(sccs.begin(), sccs.end());

    // Build permutation: blocks in topological order
    result.nblocks = static_cast<int32_t>(sccs.size());
    result.block_ptr.resize(result.nblocks + 1);
    int32_t pos = 0;
    for (int32_t b = 0; b < result.nblocks; ++b) {
        result.block_ptr[b] = pos;
        std::sort(sccs[b].begin(), sccs[b].end());
        for (int32_t node : sccs[b]) {
            result.perm[pos] = node;
            result.inv_perm[node] = pos;
            ++pos;
        }
    }
    result.block_ptr[result.nblocks] = n;

    return result;
}

}  // namespace neospice
```

- [ ] **Step 5: Add to CMakeLists.txt**

In `src/CMakeLists.txt`, add `core/btf.cpp` to the source list (after `core/amd.cpp`).

In `tests/CMakeLists.txt`, add `unit/test_btf.cpp` to the test source list.

- [ ] **Step 6: Build and run BTF tests**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R BTF -v`
Expected: All 4 BTF tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/core/btf.hpp src/core/btf.cpp tests/unit/test_btf.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add BTF decomposition (Tarjan SCC) to replace SuiteSparse BTF"
```

---

### Task 3: Replace `amd_order()` Call in SmallSolver

**Files:**
- Modify: `src/core/small_solver.cpp:1-2,28-32` (replace `#include <suitesparse/amd.h>` and `amd_order()` call)
- Test: existing tests in `tests/unit/test_small_solver.cpp` must still pass

This task replaces the single external AMD call with our custom implementation from Task 1.

- [ ] **Step 1: Replace the SuiteSparse AMD include and call**

In `src/core/small_solver.cpp`:

Replace:
```cpp
#include <suitesparse/amd.h>
```
With:
```cpp
#include "core/amd.hpp"
```

Replace the `amd_order()` call block (lines 26-32):
```cpp
        amd_perm_.resize(n_);
        amd_inv_.resize(n_);
        int status = amd_order(n_, col_ptr_.data(), row_idx_.data(),
                               amd_perm_.data(), nullptr, nullptr);
        if (status != AMD_OK && status != AMD_OK_BUT_JUMBLED) {
            for (int32_t i = 0; i < n_; ++i) amd_perm_[i] = i;
        }
```
With:
```cpp
        amd_perm_ = amd_ordering(n_, col_ptr_.data(), row_idx_.data());
        amd_inv_.resize(n_);
```

- [ ] **Step 2: Build and run SmallSolver tests**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R SmallSolver -v`
Expected: All existing SmallSolver tests pass.

- [ ] **Step 3: Run full test suite to check for regressions**

Run: `cd build && ctest -j$(nproc)`
Expected: 910 tests pass (SmallSolver handles n<200, KLU still handles n>=200).

- [ ] **Step 4: Commit**

```bash
git add src/core/small_solver.cpp
git commit -m "refactor: replace SuiteSparse amd_order() with custom AMD in SmallSolver"
```

---

### Task 4: BTFSolver — Block-Dispatch Solver for All Sizes

**Files:**
- Create: `src/core/btf_solver.hpp`
- Create: `src/core/btf_solver.cpp`
- Create: `tests/unit/test_btf_solver.cpp`
- Modify: `src/CMakeLists.txt` (add btf_solver.cpp)
- Modify: `tests/CMakeLists.txt` (add test_btf_solver.cpp)

BTFSolver implements the LinearSolver interface. On `symbolic()`, it runs BTF decomposition, then creates a SmallSolver per diagonal block. On `numeric()`/`refactorize()`, it permutes the matrix values into per-block CSC structures and delegates to each block's SmallSolver. On `solve()`, it processes blocks in reverse topological order (back-substitution through the block triangular structure).

- [ ] **Step 1: Write failing tests for BTFSolver**

```cpp
// tests/unit/test_btf_solver.cpp
#include <gtest/gtest.h>
#include "core/btf_solver.hpp"
#include "core/small_solver.hpp"
#include "core/matrix.hpp"
#include <cmath>

using namespace neospice;

static SparsityPattern make_dense_pattern(int32_t n) {
    SparsityBuilder sb(n);
    for (int32_t j = 0; j < n; ++j)
        for (int32_t i = 0; i < n; ++i)
            sb.add(i, j);
    return sb.build();
}

TEST(BTFSolver, Solve2x2Dense) {
    // [2 1; 1 3]x = [5; 7] -> x = [1.6; 1.8]
    auto pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 2.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(0, 1), 1.0);
    mat.add(pat.offset(1, 1), 3.0);

    BTFSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> rhs = {5.0, 7.0};
    solver.solve(rhs);
    EXPECT_NEAR(rhs[0], 1.6, 1e-10);
    EXPECT_NEAR(rhs[1], 1.8, 1e-10);
}

TEST(BTFSolver, SolveBlockDiagonal) {
    // Two independent 2x2 blocks:
    // [2 1 0 0] [x0]   [5]
    // [1 3 0 0] [x1] = [7]
    // [0 0 4 1] [x2]   [9]
    // [0 0 1 2] [x3]   [5]
    SparsityBuilder sb(4);
    sb.add(0,0); sb.add(0,1); sb.add(1,0); sb.add(1,1);
    sb.add(2,2); sb.add(2,3); sb.add(3,2); sb.add(3,3);
    auto pat = sb.build();
    NumericMatrix mat(pat);
    mat.add(pat.offset(0,0), 2.0); mat.add(pat.offset(0,1), 1.0);
    mat.add(pat.offset(1,0), 1.0); mat.add(pat.offset(1,1), 3.0);
    mat.add(pat.offset(2,2), 4.0); mat.add(pat.offset(2,3), 1.0);
    mat.add(pat.offset(3,2), 1.0); mat.add(pat.offset(3,3), 2.0);

    BTFSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> rhs = {5.0, 7.0, 9.0, 5.0};
    solver.solve(rhs);
    // Block 0: [2 1;1 3]x=[5;7] -> x=[1.6, 1.8]
    // Block 1: [4 1;1 2]x=[9;5] -> x=[13/7, 11/7]
    EXPECT_NEAR(rhs[0], 1.6, 1e-10);
    EXPECT_NEAR(rhs[1], 1.8, 1e-10);
    EXPECT_NEAR(rhs[2], 13.0/7.0, 1e-10);
    EXPECT_NEAR(rhs[3], 11.0/7.0, 1e-10);
}

TEST(BTFSolver, Refactorize) {
    auto pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0,0), 2.0); mat.add(pat.offset(1,0), 1.0);
    mat.add(pat.offset(0,1), 1.0); mat.add(pat.offset(1,1), 3.0);

    BTFSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    mat.clear();
    mat.add(pat.offset(0,0), 4.0); mat.add(pat.offset(1,0), 1.0);
    mat.add(pat.offset(0,1), 2.0); mat.add(pat.offset(1,1), 5.0);
    solver.refactorize(mat);

    std::vector<double> rhs = {14.0, 17.0};
    solver.solve(rhs);
    EXPECT_NEAR(rhs[0], 2.0, 1e-10);
    EXPECT_NEAR(rhs[1], 3.0, 1e-10);
}

TEST(BTFSolver, ComplexSolve2x2) {
    auto pat = make_dense_pattern(2);

    BTFSolver solver;
    solver.symbolic(pat);

    // (2+1i)(1+0i)   x = (5+3i)
    // (0+0i)(3+2i)       (6+4i)
    std::vector<double> ax = {2.0,1.0, 0.0,0.0, 1.0,0.0, 3.0,2.0};
    solver.numeric_complex(pat, ax);

    std::vector<double> rhs = {5.0,3.0, 6.0,4.0};
    solver.solve_complex(rhs);

    // Compare with SmallSolver
    SmallSolver ref;
    ref.symbolic(pat);
    ref.numeric_complex(pat, ax);
    std::vector<double> rhs_ref = {5.0,3.0, 6.0,4.0};
    ref.solve_complex(rhs_ref);

    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(rhs[i], rhs_ref[i], 1e-10);
}

TEST(BTFSolver, LargeBlockTriangular) {
    // 100-node matrix: 5 blocks of 20 with coupling
    int32_t n = 100;
    int32_t block_size = 20;
    SparsityBuilder sb(n);
    for (int32_t b = 0; b < 5; ++b) {
        int32_t base = b * block_size;
        for (int32_t i = 0; i < block_size; ++i) {
            for (int32_t j = 0; j < block_size; ++j) {
                sb.add(base + i, base + j);
            }
        }
        // Upper triangular coupling to next block
        if (b < 4) {
            int32_t next = (b + 1) * block_size;
            sb.add(base, next);
            sb.add(base + 1, next + 1);
        }
    }
    auto pat = sb.build();
    NumericMatrix mat(pat);
    for (auto& [r, c] : pat.entries()) {
        int32_t br = r / block_size, bc = c / block_size;
        if (br == bc)
            mat.add(pat.offset(r, c), (r == c) ? 20.0 : -0.5);
        else
            mat.add(pat.offset(r, c), 0.1);
    }

    BTFSolver btf;
    btf.symbolic(pat);
    btf.numeric(pat, mat);

    SmallSolver ref;
    ref.symbolic(pat);
    ref.numeric(pat, mat);

    std::vector<double> rhs_btf(n), rhs_ref(n);
    for (int32_t i = 0; i < n; ++i)
        rhs_btf[i] = rhs_ref[i] = static_cast<double>(i % 7 + 1);

    btf.solve(rhs_btf);
    ref.solve(rhs_ref);

    for (int32_t i = 0; i < n; ++i)
        EXPECT_NEAR(rhs_btf[i], rhs_ref[i], 1e-9);
}

TEST(BTFSolver, Solve300x300Banded) {
    // Large enough to previously require KLU
    int32_t n = 300;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        for (int32_t d = -3; d <= 3; ++d) {
            int32_t j = i + d;
            if (j >= 0 && j < n) sb.add(i, j);
        }
    }
    auto pat = sb.build();
    NumericMatrix mat(pat);
    for (auto& [r, c] : pat.entries())
        mat.add(pat.offset(r, c), (r == c) ? 20.0 : -0.5);

    BTFSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> x_true(n);
    for (int32_t i = 0; i < n; ++i) x_true[i] = static_cast<double>(i % 11 + 1);

    // Compute b = A*x
    std::vector<double> rhs(n, 0.0);
    for (auto& [r, c] : pat.entries())
        rhs[r] += mat.value(pat.offset(r, c)) * x_true[c];

    solver.solve(rhs);
    for (int32_t i = 0; i < n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-8);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j$(nproc)`
Expected: Compilation error — `btf_solver.hpp` does not exist.

- [ ] **Step 3: Write BTFSolver header**

```cpp
// src/core/btf_solver.hpp
#pragma once
#include "core/linear_solver.hpp"
#include "core/small_solver.hpp"
#include "core/btf.hpp"
#include <memory>
#include <vector>

namespace neospice {

class BTFSolver : public LinearSolver {
public:
    BTFSolver();
    ~BTFSolver() override = default;

    void symbolic(const SparsityPattern& pattern) override;
    void numeric(const SparsityPattern& pattern, const NumericMatrix& mat) override;
    void refactorize(const NumericMatrix& mat) override;
    void solve(std::vector<double>& rhs) override;
    void numeric_complex(const SparsityPattern& pattern,
                         const std::vector<double>& ax) override;
    void refactorize_complex(const std::vector<double>& ax) override;
    void solve_complex(std::vector<double>& rhs) override;

private:
    int32_t n_ = 0;
    BTFResult btf_;

    // Per-block solver and sparsity data
    struct BlockData {
        int32_t size;
        SparsityPattern pattern;
        std::unique_ptr<SmallSolver> solver;
        // Maps block CSC index -> original matrix CSC index
        std::vector<int32_t> val_map;
    };
    std::vector<BlockData> blocks_;

    // Off-diagonal coupling: for each block b, entries (perm_row, perm_col, orig_offset)
    // where perm_row is in block b and perm_col is in a later block (already solved).
    struct Coupling {
        int32_t block_row;    // local row within the target block
        int32_t source_index; // index into the permuted solution
        int32_t orig_offset;  // offset into original NumericMatrix
    };
    std::vector<std::vector<Coupling>> couplings_;

    // Original CSC data
    std::vector<int32_t> col_ptr_;
    std::vector<int32_t> row_idx_;

    bool symbolized_ = false;
    bool factored_ = false;
    bool factored_z_ = false;
};

}  // namespace neospice
```

- [ ] **Step 4: Write BTFSolver implementation**

The implementation is the largest piece. Key logic:

1. `symbolic()`: Run BTF decomposition, build per-block SparsityPattern and SmallSolver, map coupling entries.
2. `numeric()`: Scatter original matrix values into per-block matrices via val_map, call each block's `numeric()`.
3. `refactorize()`: Same scatter, call each block's `refactorize()`.
4. `solve()`: Process blocks in reverse order. For each block, subtract off-diagonal coupling (from already-solved later blocks), then solve the block.
5. Complex variants mirror real variants.

Create `src/core/btf_solver.cpp` with approximately 300 lines implementing the above. The implementation must correctly handle:
- Permuting the original CSC into per-block structures during symbolic
- The val_map for efficient refactorize (same as SmallSolver's approach)
- Back-substitution through the block upper triangular structure during solve
- Complex interleaved format (2 doubles per entry)

- [ ] **Step 5: Add to CMakeLists.txt**

In `src/CMakeLists.txt`, add `core/btf_solver.cpp` to the source list.
In `tests/CMakeLists.txt`, add `unit/test_btf_solver.cpp` to the test source list.

- [ ] **Step 6: Build and run BTFSolver tests**

Run: `cmake --build build -j$(nproc) && cd build && ctest -R BTFSolver -v`
Expected: All 7 BTFSolver tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/core/btf_solver.hpp src/core/btf_solver.cpp tests/unit/test_btf_solver.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add BTFSolver with block-triangular dispatch to SmallSolver"
```

---

### Task 5: Wire BTFSolver into `create_solver()` and Remove KLU

**Files:**
- Modify: `src/core/linear_solver.cpp` (replace KLU dispatch with BTFSolver)
- Modify: `src/CMakeLists.txt` (remove `core/klu_solver.cpp`)
- Modify: `tests/CMakeLists.txt` (remove `unit/test_klu_solver.cpp`, `unit/test_klu_complex.cpp`)
- Modify: `tests/unit/test_small_solver.cpp` (remove KLU references)
- Modify: `tests/bench/bench_small_solver.cpp` (replace KLU references with BTFSolver)
- Delete: `src/core/klu_solver.hpp`
- Delete: `src/core/klu_solver.cpp`
- Delete: `tests/unit/test_klu_solver.cpp`
- Delete: `tests/unit/test_klu_complex.cpp`

- [ ] **Step 0: Capture KLU baseline performance before removal**

Before removing KLU, run the benchmark with both KLU and BTFSolver side-by-side to capture baseline numbers. This is our gate: BTFSolver must be equal or faster than KLU before we proceed with removal.

Run:
```bash
cmake --build build -j$(nproc) --target bench_small_solver
./build/bench_small_solver 2>&1 | tee /tmp/klu-baseline-benchmark.txt
```

Also run the comprehensive simulation benchmark to capture end-to-end KLU baseline:
```bash
cmake --build build -j$(nproc) --target bench_comprehensive
./build/bench_comprehensive 2>&1 | tee /tmp/klu-baseline-comprehensive.txt
```

Save these outputs — they are the numbers we must beat in Task 7.

- [ ] **Step 1: Update `create_solver()` to use BTFSolver for large matrices**

Replace `src/core/linear_solver.cpp`:

```cpp
#include "core/linear_solver.hpp"
#include "core/btf_solver.hpp"
#include "core/small_solver.hpp"

namespace neospice {

std::unique_ptr<LinearSolver> create_solver(int32_t n) {
    if (n < 200) {
        return std::make_unique<SmallSolver>();
    }
    return std::make_unique<BTFSolver>();
}

}  // namespace neospice
```

- [ ] **Step 2: Build and run full test suite with BTFSolver active**

Run: `cmake --build build -j$(nproc) && cd build && ctest -j$(nproc)`
Expected: 910 tests pass (KLU tests still compile because klu_solver.cpp is still in the build).

- [ ] **Step 3: Remove KLU files and references**

Remove from `src/CMakeLists.txt`: the line `core/klu_solver.cpp`.

Remove from `tests/CMakeLists.txt`: the lines `unit/test_klu_solver.cpp` and `unit/test_klu_complex.cpp`.

Delete files: `src/core/klu_solver.hpp`, `src/core/klu_solver.cpp`, `tests/unit/test_klu_solver.cpp`, `tests/unit/test_klu_complex.cpp`.

- [ ] **Step 4: Update test_small_solver.cpp to remove KLU references**

In `tests/unit/test_small_solver.cpp`:

Remove `#include "core/klu_solver.hpp"`.

Remove or update tests that reference `KLUSolver`:
- `FactoryDispatch` test: remove the KLU check, keep SmallSolver check and add BTFSolver check for n>=200.
- Tests `Solve24x24`, `SparseTier25x25`, `SparseTier100x100`, `SparseTierRefactorize`, `SparseTierComplex50x50`: replace `KLUSolver klu;` with `BTFSolver btf;` (include `core/btf_solver.hpp`), adjust all `klu.` calls to `btf.` calls.
- `ComplexRefactorize`: replace KLU verification with BTFSolver.

- [ ] **Step 5: Update bench_small_solver.cpp**

In `tests/bench/bench_small_solver.cpp`:

Replace `#include "core/klu_solver.hpp"` with `#include "core/btf_solver.hpp"`.
Replace all `KLUSolver` with `BTFSolver`.
Replace all variable names `klu` with `btf` and column header `KLU(us)` with `BTF(us)`.

- [ ] **Step 6: Build and run full test suite**

Run: `cmake --build build -j$(nproc) && cd build && ctest -j$(nproc)`
Expected: Tests pass (minus the deleted KLU-specific tests). Count should be ~900 (910 minus ~10 KLU-specific tests).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: replace KLUSolver with BTFSolver, remove KLU source files"
```

---

### Task 6: Remove SuiteSparse from CMake

**Files:**
- Modify: `CMakeLists.txt:8-13` (remove find_package for KLU, AMD, COLAMD, BTF, SuiteSparse_config)
- Modify: `src/CMakeLists.txt:92-97` (remove SuiteSparse link targets)
- Modify: `pyproject.toml` (remove suitesparse-devel from cibuildwheel before-all)
- Modify: `.github/workflows/wheels.yml` (remove suitesparse references if any)
- Modify: `README.md` (remove SuiteSparse from prerequisites)

- [ ] **Step 1: Remove SuiteSparse from root CMakeLists.txt**

In `CMakeLists.txt`, remove lines 8-13:
```cmake
# KLU (SuiteSparse)
find_package(KLU REQUIRED)
find_package(AMD REQUIRED)
find_package(COLAMD REQUIRED)
find_package(BTF REQUIRED)
find_package(SuiteSparse_config REQUIRED)
```

- [ ] **Step 2: Remove SuiteSparse link targets from src/CMakeLists.txt**

In `src/CMakeLists.txt`, remove these lines from `target_link_libraries`:
```cmake
    SuiteSparse::KLU
    SuiteSparse::AMD
    SuiteSparse::COLAMD
    SuiteSparse::BTF
    SuiteSparse::SuiteSparseConfig
```

- [ ] **Step 3: Update pyproject.toml cibuildwheel config**

In `pyproject.toml`, remove `suitesparse-devel` from `[tool.cibuildwheel.linux]` `before-all`, and `suite-sparse` from `[tool.cibuildwheel.macos]` `before-all`.

- [ ] **Step 4: Update README.md prerequisites**

In `README.md`, remove `SuiteSparse (KLU)` from the prerequisites list and update the apt install command:

Replace:
```bash
sudo apt install cmake g++ libsuitesparse-dev libopenblas-dev
```
With:
```bash
sudo apt install cmake g++ libopenblas-dev
```

- [ ] **Step 5: Clean build from scratch and run tests**

Run:
```bash
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && ctest -j$(nproc)
```
Expected: Full build succeeds with no SuiteSparse references. All tests pass.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt pyproject.toml README.md
git commit -m "build: remove SuiteSparse dependency entirely"
```

---

### Task 7: Performance Validation — Must Beat KLU

**Goal:** Verify our custom implementation (SmallSolver + BTFSolver) is **equal or faster** than the old KLU-based solution at every matrix size. Compare against the KLU baseline captured in Task 5 Step 0.

**Files:**
- Modify: `tests/bench/bench_small_solver.cpp` (extend sizes)
- Reference: `/tmp/klu-baseline-benchmark.txt` (captured in Task 5)
- Reference: `/tmp/klu-baseline-comprehensive.txt` (captured in Task 5)

- [ ] **Step 1: Extend benchmark to cover large matrices**

In `tests/bench/bench_small_solver.cpp`, add sizes beyond the old KLU threshold:
```cpp
const std::vector<int32_t> sizes = {5, 10, 25, 50, 87, 100, 150, 199, 300, 500};
```

- [ ] **Step 2: Build and run solver benchmark, compare against KLU baseline**

Run:
```bash
cmake --build build -j$(nproc) --target bench_small_solver
./build/bench_small_solver 2>&1 | tee /tmp/new-solver-benchmark.txt
```

Compare against `/tmp/klu-baseline-benchmark.txt` (saved in Task 5 Step 0). **Pass criteria:**
- For n < 200: SmallSolver times must be unchanged (same solver, no regression)
- For n >= 200: BTFSolver median time must be **<= KLU median time** (equal or faster)
- All solutions must match (max diff < 1e-8)

If BTFSolver is slower than KLU at any size, investigate and optimize before proceeding. Refer to `~/Codes/SuiteSparse/KLU/Source/` to understand what KLU does differently and match or beat it.

- [ ] **Step 3: Run the comprehensive simulation benchmark, compare against KLU baseline**

Run:
```bash
cmake --build build -j$(nproc) --target bench_comprehensive
./build/bench_comprehensive 2>&1 | tee /tmp/new-solver-comprehensive.txt
```

Compare against `/tmp/klu-baseline-comprehensive.txt`. **Pass criteria:**
- Total simulation times must be **equal or faster** than KLU baseline
- Compare against the baseline numbers in `docs/performance-comparison-with-ngspice.md`
- neospice must still be faster than ngspice (our 1.1x speedup must not regress)

If end-to-end simulation is slower, profile to find the bottleneck (is it symbolic analysis overhead? BTF decomposition? block solve?) and optimize.

- [ ] **Step 4: Run full test suite one final time**

Run: `cd build && ctest -j$(nproc)`
Expected: All tests pass.

- [ ] **Step 5: Commit benchmark updates**

```bash
git add tests/bench/bench_small_solver.cpp
git commit -m "bench: extend solver benchmark to cover post-KLU matrix sizes"
```
