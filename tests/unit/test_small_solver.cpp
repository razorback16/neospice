#include <gtest/gtest.h>
#include "core/small_solver.hpp"
#include "core/klu_solver.hpp"
#include "core/matrix.hpp"

using namespace neospice;

static SparsityPattern make_dense_pattern(int32_t n) {
    SparsityBuilder sb(n);
    for (int32_t j = 0; j < n; ++j)
        for (int32_t i = 0; i < n; ++i)
            sb.add(i, j);
    return sb.build();
}

TEST(SmallSolver, Solve1x1) {
    SparsityPattern pat = make_dense_pattern(1);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 3.0);

    SmallSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> rhs = {9.0};
    solver.solve(rhs);
    EXPECT_NEAR(rhs[0], 3.0, 1e-12);
}

TEST(SmallSolver, Solve2x2) {
    // [2 1; 1 3]x = [5; 7] -> x = [1.6; 1.8]
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 2.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(0, 1), 1.0);
    mat.add(pat.offset(1, 1), 3.0);

    SmallSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> rhs = {5.0, 7.0};
    solver.solve(rhs);
    EXPECT_NEAR(rhs[0], 1.6, 1e-10);
    EXPECT_NEAR(rhs[1], 1.8, 1e-10);
}

TEST(SmallSolver, Solve5x5) {
    // Diagonally dominant 5x5
    int32_t n = 5;
    SparsityPattern pat = make_dense_pattern(n);
    NumericMatrix mat(pat);
    // Diagonal = 10, off-diag = 1
    for (int32_t i = 0; i < n; ++i)
        for (int32_t j = 0; j < n; ++j)
            mat.add(pat.offset(i, j), (i == j) ? 10.0 : 1.0);

    SmallSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    // rhs = A * [1,2,3,4,5]
    std::vector<double> x_true = {1, 2, 3, 4, 5};
    std::vector<double> rhs(n, 0.0);
    for (int32_t i = 0; i < n; ++i)
        for (int32_t j = 0; j < n; ++j)
            rhs[i] += ((i == j) ? 10.0 : 1.0) * x_true[j];

    solver.solve(rhs);
    for (int32_t i = 0; i < n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-10);
}

TEST(SmallSolver, Refactorize) {
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 2.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(0, 1), 1.0);
    mat.add(pat.offset(1, 1), 3.0);

    SmallSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    // New values: [4 2; 1 5]x = [14; 17] -> x = [2; 3]
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

TEST(SmallSolver, SolveBeforeSymbolicThrows) {
    SmallSolver solver;
    std::vector<double> rhs = {1.0};
    EXPECT_THROW(solver.solve(rhs), std::logic_error);
}

TEST(SmallSolver, NumericBeforeSymbolicThrows) {
    SmallSolver solver;
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 1.0);
    mat.add(pat.offset(1, 1), 1.0);
    EXPECT_THROW(solver.numeric(pat, mat), std::logic_error);
}

TEST(SmallSolver, RefactorizeBeforeNumericThrows) {
    SmallSolver solver;
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 1.0);
    mat.add(pat.offset(1, 1), 1.0);
    solver.symbolic(pat);
    EXPECT_THROW(solver.refactorize(mat), std::logic_error);
}

TEST(SmallSolver, SolveSizeMismatchThrows) {
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 1.0);
    mat.add(pat.offset(1, 1), 1.0);

    SmallSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> rhs = {1.0};  // wrong size
    EXPECT_THROW(solver.solve(rhs), std::invalid_argument);
}

TEST(SmallSolver, ComplexMethodsThrow) {
    SmallSolver solver;
    SparsityPattern pat = make_dense_pattern(2);
    std::vector<double> ax = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
    std::vector<double> rhs = {1.0, 0.0, 0.0, 0.0};

    EXPECT_THROW(solver.numeric_complex(pat, ax), std::logic_error);
    EXPECT_THROW(solver.refactorize_complex(ax), std::logic_error);
    EXPECT_THROW(solver.solve_complex(rhs), std::logic_error);
}

TEST(SmallSolver, Solve24x24) {
    // Boundary of dense tier: tridiagonal 24x24
    int32_t n = 24;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i - 1);
        if (i < n - 1) sb.add(i, i + 1);
    }
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    for (int32_t i = 0; i < n; ++i) {
        mat.add(pat.offset(i, i), 4.0);
        if (i > 0) mat.add(pat.offset(i, i - 1), -1.0);
        if (i < n - 1) mat.add(pat.offset(i, i + 1), -1.0);
    }

    SmallSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    // Compare against KLU
    KLUSolver klu;
    klu.symbolic(pat);
    klu.numeric(pat, mat);

    std::vector<double> rhs_small(n), rhs_klu(n);
    for (int32_t i = 0; i < n; ++i)
        rhs_small[i] = rhs_klu[i] = static_cast<double>(i + 1);

    solver.solve(rhs_small);
    klu.solve(rhs_klu);

    for (int32_t i = 0; i < n; ++i)
        EXPECT_NEAR(rhs_small[i], rhs_klu[i], 1e-10);
}
