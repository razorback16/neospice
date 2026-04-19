/// Unit tests for KLUSolver complex (klu_z_*) wrappers.
///
/// Each test builds a tiny complex linear system, factors it with
/// numeric_complex(), solves it with solve_complex(), and checks the
/// result against the analytically known solution.

#include "core/klu_solver.hpp"
#include "core/matrix.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using namespace neospice;

// ---------------------------------------------------------------------------
// Helper: build a SparsityPattern from (row,col) pairs.
// ---------------------------------------------------------------------------
static SparsityPattern make_pattern(int n,
                                    const std::vector<std::pair<int,int>>& entries)
{
    SparsityBuilder builder(n);
    for (auto& [r, c] : entries) {
        builder.add(r, c);
    }
    return builder.build();
}

// ---------------------------------------------------------------------------
// Test 1: 1x1 complex system  (1+j)*x = 2+j  →  x = 1.5 - 0.5j
//
//   x = (2+j)/(1+j) = (2+j)(1-j)/((1+j)(1-j))
//     = (2 - 2j + j - j²) / 2
//     = (2 + j²=1 … wait: j²=-1, so)
//     = (2 - 2j + j + 1) / 2 = (3 - j) / 2 = 1.5 - 0.5j   ✓
// ---------------------------------------------------------------------------
TEST(KLUComplex, Solve1x1)
{
    // Single-entry matrix: A(0,0) = 1+j
    auto pat = make_pattern(1, {{0, 0}});

    KLUSolver solver;
    solver.symbolic(pat);

    // ax: 2 doubles for 1 NNZ — interleaved real,imag
    std::vector<double> ax = {1.0, 1.0};
    solver.numeric_complex(pat, ax);

    // rhs: 2+j  →  {2.0, 1.0}
    std::vector<double> rhs = {2.0, 1.0};
    solver.solve_complex(rhs);

    EXPECT_NEAR(rhs[0],  1.5, 1e-12);   // real part
    EXPECT_NEAR(rhs[1], -0.5, 1e-12);   // imag part
}

// ---------------------------------------------------------------------------
// Test 2: 2x2 complex system
//
//   | (2+j)   (1+0j) | |x0|   | (5+3j) |
//   | (0+j)   (3+0j) | |x1| = | (3+4j) |
//
// Solve by hand:
//   Row 1: (2+j)x0 + x1 = 5+3j
//   Row 2: j*x0  + 3*x1 = 3+4j
//
//   From row 2: x1 = (3+4j - j*x0) / 3
//   Sub into row 1:
//     (2+j)x0 + (3+4j - j*x0)/3 = 5+3j
//     3(2+j)x0 + 3+4j - j*x0 = 15+9j
//     (6+3j - j)x0 = 12+5j
//     (6+2j)x0 = 12+5j
//     x0 = (12+5j)/(6+2j)
//        = (12+5j)(6-2j) / (36+4)
//        = (72 - 24j + 30j - 10j²) / 40
//        = (72 + 6j + 10) / 40
//        = (82 + 6j) / 40
//        = 2.05 + 0.15j
//   x1 = (3+4j - j*(2.05+0.15j)) / 3
//      = (3+4j - 2.05j - 0.15j²) / 3
//      = (3+4j - 2.05j + 0.15) / 3
//      = (3.15 + 1.95j) / 3
//      = 1.05 + 0.65j
//
// SparsityPattern entries added in (row,col) order.
// CSC sorts by column then row, so order in ax is:
//   col 0: (0,0)→(2+j), (1,0)→(0+j)
//   col 1: (0,1)→(1+0j), (1,1)→(3+0j)
// ax = {2,1, 0,1, 1,0, 3,0}
// ---------------------------------------------------------------------------
TEST(KLUComplex, Solve2x2)
{
    // Add entries in row-major order; CSC conversion will sort them.
    auto pat = make_pattern(2, {{0,0},{0,1},{1,0},{1,1}});

    KLUSolver solver;
    solver.symbolic(pat);

    // ax interleaved: col0-row0=(2+j), col0-row1=(0+j), col1-row0=(1+0j), col1-row1=(3+0j)
    std::vector<double> ax = {2.0, 1.0,   // (0,0): 2+j
                               0.0, 1.0,   // (1,0): 0+j
                               1.0, 0.0,   // (0,1): 1+0j
                               3.0, 0.0};  // (1,1): 3+0j
    solver.numeric_complex(pat, ax);

    // rhs interleaved: (5+3j, 3+4j)
    std::vector<double> rhs = {5.0, 3.0, 3.0, 4.0};
    solver.solve_complex(rhs);

    EXPECT_NEAR(rhs[0], 2.05, 1e-10);   // Re(x0)
    EXPECT_NEAR(rhs[1], 0.15, 1e-10);   // Im(x0)
    EXPECT_NEAR(rhs[2], 1.05, 1e-10);   // Re(x1)
    EXPECT_NEAR(rhs[3], 0.65, 1e-10);   // Im(x1)
}

// ---------------------------------------------------------------------------
// Test 3: Refactorize — factor 1x1 with (1+j), solve, then refactorize with
// (2+0j) and solve again.
//
//   Pass 1: (1+j)*x = 2+j  →  x = 1.5-0.5j   (same as Test 1)
//   Pass 2: (2+0j)*x = 2+j  →  x = 1.0+0.5j
// ---------------------------------------------------------------------------
TEST(KLUComplex, Refactorize)
{
    auto pat = make_pattern(1, {{0, 0}});

    KLUSolver solver;
    solver.symbolic(pat);

    // --- Pass 1: full factorization with (1+j) ---
    std::vector<double> ax1 = {1.0, 1.0};
    solver.numeric_complex(pat, ax1);

    std::vector<double> rhs1 = {2.0, 1.0};
    solver.solve_complex(rhs1);

    EXPECT_NEAR(rhs1[0],  1.5, 1e-12);
    EXPECT_NEAR(rhs1[1], -0.5, 1e-12);

    // --- Pass 2: refactorize with (2+0j) ---
    // (2+0j)*x = 2+j  →  x = (2+j)/2 = 1.0 + 0.5j
    std::vector<double> ax2 = {2.0, 0.0};
    solver.refactorize_complex(ax2);

    std::vector<double> rhs2 = {2.0, 1.0};
    solver.solve_complex(rhs2);

    EXPECT_NEAR(rhs2[0], 1.0, 1e-12);
    EXPECT_NEAR(rhs2[1], 0.5, 1e-12);
}
