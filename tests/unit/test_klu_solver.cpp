#include <gtest/gtest.h>
#include "core/klu_solver.hpp"
#include "core/matrix.hpp"

using namespace cudaspice;

// Helper: build the 2x2 fully-dense pattern and stamp values.
//
// Matrix layout (CSC order, col-major):
//   col 0: rows 0,1
//   col 1: rows 0,1
static SparsityPattern make_2x2_pattern() {
    SparsityBuilder sb(2);
    sb.add(0, 0);
    sb.add(1, 0);
    sb.add(0, 1);
    sb.add(1, 1);
    return sb.build();
}

// ---------------------------------------------------------------------------
// TEST: basic 2x2 solve
//   [2 1][x]   [5]        x0 = 1.6
//   [1 3][x] = [7]   =>   x1 = 1.8
// ---------------------------------------------------------------------------
TEST(KLUSolver, Solve2x2) {
    SparsityPattern pat = make_2x2_pattern();
    NumericMatrix   mat(pat);

    // Stamp A = [[2,1],[1,3]]
    mat.add(pat.offset(0, 0), 2.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(0, 1), 1.0);
    mat.add(pat.offset(1, 1), 3.0);

    KLUSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> rhs = {5.0, 7.0};
    solver.solve(rhs);

    EXPECT_NEAR(rhs[0], 1.6, 1e-10);
    EXPECT_NEAR(rhs[1], 1.8, 1e-10);
}

// ---------------------------------------------------------------------------
// TEST: refactorize with same sparsity, new values
//   [4 2][x]   [14]        x0 = 2
//   [1 5][x] = [17]   =>   x1 = 3
// ---------------------------------------------------------------------------
TEST(KLUSolver, Refactorize) {
    SparsityPattern pat = make_2x2_pattern();
    NumericMatrix   mat(pat);

    // Initial factorization with first matrix.
    mat.add(pat.offset(0, 0), 2.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(0, 1), 1.0);
    mat.add(pat.offset(1, 1), 3.0);

    KLUSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    // Now stamp new values: A = [[4,2],[1,5]]
    mat.clear();
    mat.add(pat.offset(0, 0), 4.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(0, 1), 2.0);
    mat.add(pat.offset(1, 1), 5.0);

    solver.refactorize(mat);

    std::vector<double> rhs = {14.0, 17.0};
    solver.solve(rhs);

    EXPECT_NEAR(rhs[0], 2.0, 1e-10);
    EXPECT_NEAR(rhs[1], 3.0, 1e-10);
}

// ---------------------------------------------------------------------------
// TEST: error propagation — solve before symbolic throws
// ---------------------------------------------------------------------------
TEST(KLUSolver, SolveBeforeSymbolicThrows) {
    KLUSolver solver;
    std::vector<double> rhs = {1.0, 2.0};
    EXPECT_THROW(solver.solve(rhs), std::logic_error);
}
