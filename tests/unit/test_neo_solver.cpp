#include <gtest/gtest.h>
#include "core/neo_solver.hpp"
#include "core/linear_solver.hpp"
#include "core/matrix.hpp"

using namespace neospice;

static SparsityPattern make_dense_pattern(int32_t n) {
    SparsityBuilder sb(n);
    for (int32_t j = 0; j < n; ++j)
        for (int32_t i = 0; i < n; ++i)
            sb.add(i, j);
    return sb.build();
}

TEST(NeoSolver, Solve1x1) {
    SparsityPattern pat = make_dense_pattern(1);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 3.0);

    NeoSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> rhs = {9.0};
    solver.solve(rhs);
    EXPECT_NEAR(rhs[0], 3.0, 1e-12);
}

TEST(NeoSolver, Solve2x2) {
    // [2 1; 1 3]x = [5; 7] -> x = [1.6; 1.8]
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 2.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(0, 1), 1.0);
    mat.add(pat.offset(1, 1), 3.0);

    NeoSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> rhs = {5.0, 7.0};
    solver.solve(rhs);
    EXPECT_NEAR(rhs[0], 1.6, 1e-10);
    EXPECT_NEAR(rhs[1], 1.8, 1e-10);
}

TEST(NeoSolver, Solve5x5) {
    // Diagonally dominant 5x5
    int32_t n = 5;
    SparsityPattern pat = make_dense_pattern(n);
    NumericMatrix mat(pat);
    // Diagonal = 10, off-diag = 1
    for (int32_t i = 0; i < n; ++i)
        for (int32_t j = 0; j < n; ++j)
            mat.add(pat.offset(i, j), (i == j) ? 10.0 : 1.0);

    NeoSolver solver;
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

TEST(NeoSolver, Refactorize) {
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 2.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(0, 1), 1.0);
    mat.add(pat.offset(1, 1), 3.0);

    NeoSolver solver;
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

TEST(NeoSolver, SolveBeforeSymbolicThrows) {
    NeoSolver solver;
    std::vector<double> rhs = {1.0};
    EXPECT_THROW(solver.solve(rhs), std::logic_error);
}

TEST(NeoSolver, NumericBeforeSymbolicThrows) {
    NeoSolver solver;
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 1.0);
    mat.add(pat.offset(1, 1), 1.0);
    EXPECT_THROW(solver.numeric(pat, mat), std::logic_error);
}

TEST(NeoSolver, RefactorizeBeforeNumericThrows) {
    NeoSolver solver;
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 1.0);
    mat.add(pat.offset(1, 1), 1.0);
    solver.symbolic(pat);
    EXPECT_THROW(solver.refactorize(mat), std::logic_error);
}

TEST(NeoSolver, SolveSizeMismatchThrows) {
    SparsityPattern pat = make_dense_pattern(2);
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 1.0);
    mat.add(pat.offset(1, 1), 1.0);

    NeoSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    std::vector<double> rhs = {1.0};  // wrong size
    EXPECT_THROW(solver.solve(rhs), std::invalid_argument);
}

TEST(NeoSolver, ComplexBeforeSymbolicThrows) {
    NeoSolver solver;
    SparsityPattern pat = make_dense_pattern(2);
    std::vector<double> ax = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
    std::vector<double> rhs = {1.0, 0.0, 0.0, 0.0};

    EXPECT_THROW(solver.numeric_complex(pat, ax), std::logic_error);
    EXPECT_THROW(solver.refactorize_complex(ax), std::logic_error);
    EXPECT_THROW(solver.solve_complex(rhs), std::logic_error);
}

TEST(NeoSolver, ComplexSolve1x1) {
    // (3+4i)x = (11+2i) -> x = (11+2i)(3-4i)/25 = (41-38i)/25 = (1.64, -1.52)
    SparsityPattern pat = make_dense_pattern(1);
    NeoSolver solver;
    solver.symbolic(pat);

    // ax: interleaved [real, imag] for each CSC entry
    std::vector<double> ax = {3.0, 4.0};  // (3+4i)
    solver.numeric_complex(pat, ax);

    std::vector<double> rhs = {11.0, 2.0};  // (11+2i)
    solver.solve_complex(rhs);
    EXPECT_NEAR(rhs[0], 1.64, 1e-10);
    EXPECT_NEAR(rhs[1], -1.52, 1e-10);
}

TEST(NeoSolver, ComplexSolve2x2) {
    // [(2+1i) (1+0i)] [x0]   [(5+3i)]
    // [(0+0i) (3+2i)] [x1] = [(6+4i)]
    // x1 = (6+4i)/(3+2i) = (26/13, 0/13) = (2, 0)
    // x0 = ((5+3i) - (1+0i)*(2+0i)) / (2+1i) = (3+3i)/(2+1i) = (9/5, 3/5) = (1.8, 0.6)
    SparsityPattern pat = make_dense_pattern(2);
    NeoSolver solver;
    solver.symbolic(pat);

    // CSC order: col 0: (0,0)=(2+1i), (1,0)=(0+0i); col 1: (0,1)=(1+0i), (1,1)=(3+2i)
    std::vector<double> ax = {
        2.0, 1.0,   // (0,0): 2+1i
        0.0, 0.0,   // (1,0): 0+0i
        1.0, 0.0,   // (0,1): 1+0i
        3.0, 2.0    // (1,1): 3+2i
    };
    solver.numeric_complex(pat, ax);

    std::vector<double> rhs = {5.0, 3.0, 6.0, 4.0};  // [(5+3i), (6+4i)]
    solver.solve_complex(rhs);
    EXPECT_NEAR(rhs[0], 1.8, 1e-10);
    EXPECT_NEAR(rhs[1], 0.6, 1e-10);
    EXPECT_NEAR(rhs[2], 2.0, 1e-10);
    EXPECT_NEAR(rhs[3], 0.0, 1e-10);
}

TEST(NeoSolver, ComplexRefactorize) {
    SparsityPattern pat = make_dense_pattern(2);
    NeoSolver solver;
    solver.symbolic(pat);

    // First factorization
    std::vector<double> ax1 = {2.0, 1.0, 0.0, 0.0, 1.0, 0.0, 3.0, 2.0};
    solver.numeric_complex(pat, ax1);
    std::vector<double> rhs1 = {5.0, 3.0, 6.0, 4.0};
    solver.solve_complex(rhs1);

    // Refactorize with new values: [(4+0i) (1+1i); (2+0i) (3+0i)]
    std::vector<double> ax2 = {4.0, 0.0, 2.0, 0.0, 1.0, 1.0, 3.0, 0.0};
    solver.refactorize_complex(ax2);

    // Solve and verify via A*x = b
    // A = [(4+0i)(1+1i); (2+0i)(3+0i)]
    // rhs = [(10+2i); (8+0i)]
    std::vector<double> rhs2 = {10.0, 2.0, 8.0, 0.0};
    solver.solve_complex(rhs2);

    // Verify by computing A*x and checking it equals b
    // x0 = rhs2[0] + i*rhs2[1], x1 = rhs2[2] + i*rhs2[3]
    double x0r = rhs2[0], x0i = rhs2[1], x1r = rhs2[2], x1i = rhs2[3];
    // row 0: (4+0i)*x0 + (1+1i)*x1
    double b0r = 4.0*x0r + (x1r - x1i);
    double b0i = 4.0*x0i + (x1i + x1r);
    // row 1: (2+0i)*x0 + (3+0i)*x1
    double b1r = 2.0*x0r + 3.0*x1r;
    double b1i = 2.0*x0i + 3.0*x1i;
    EXPECT_NEAR(b0r, 10.0, 1e-10);
    EXPECT_NEAR(b0i, 2.0, 1e-10);
    EXPECT_NEAR(b1r, 8.0, 1e-10);
    EXPECT_NEAR(b1i, 0.0, 1e-10);
}

TEST(NeoSolver, FactoryDispatch) {
    auto small = create_solver(10);
    EXPECT_NE(dynamic_cast<NeoSolver*>(small.get()), nullptr);
    auto large = create_solver(200);
    EXPECT_NE(dynamic_cast<NeoSolver*>(large.get()), nullptr);
}

TEST(NeoSolver, Solve24x24) {
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

    NeoSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    // Compute rhs = A * x_true, then verify solve recovers x_true
    std::vector<double> x_true(n);
    for (int32_t i = 0; i < n; ++i)
        x_true[i] = static_cast<double>(i + 1);

    std::vector<double> rhs(n, 0.0);
    for (auto& [r, c] : pat.entries())
        rhs[r] += mat.value(pat.offset(r, c)) * x_true[c];

    solver.solve(rhs);
    for (int32_t i = 0; i < n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-10);
}

TEST(NeoSolver, SparseTier25x25) {
    // 25x25 tridiagonal -- crosses dense/sparse boundary
    int32_t n = 25;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i-1);
        if (i < n-1) sb.add(i, i+1);
    }
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    for (int32_t i = 0; i < n; ++i) {
        mat.add(pat.offset(i, i), 4.0);
        if (i > 0) mat.add(pat.offset(i, i-1), -1.0);
        if (i < n-1) mat.add(pat.offset(i, i+1), -1.0);
    }

    NeoSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    // Compute rhs = A * x_true, then verify solve recovers x_true
    std::vector<double> x_true(n);
    for (int32_t i = 0; i < n; ++i)
        x_true[i] = static_cast<double>(i + 1);

    std::vector<double> rhs(n, 0.0);
    for (auto& [r, c] : pat.entries())
        rhs[r] += mat.value(pat.offset(r, c)) * x_true[c];

    solver.solve(rhs);
    for (int32_t i = 0; i < n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-10);
}

TEST(NeoSolver, SparseTier100x100) {
    // 100x100 banded sparse (diagonal + 3 sub/super-diagonals)
    int32_t n = 100;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);  // diagonal
    }
    // Add off-diagonal entries
    for (int32_t j = 0; j < n; ++j) {
        for (int32_t k = 1; k <= 3 && j+k < n; ++k) {
            sb.add(j+k, j);
            sb.add(j, j+k);
        }
    }
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);

    // Diagonally dominant: diag = 20, off-diag = 1
    for (auto& [r, c] : pat.entries()) {
        mat.add(pat.offset(r, c), (r == c) ? 20.0 : 1.0);
    }

    NeoSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    // Compute rhs = A * x_true, then verify solve recovers x_true
    std::vector<double> x_true(n);
    for (int32_t i = 0; i < n; ++i)
        x_true[i] = static_cast<double>(i % 7 + 1);

    std::vector<double> rhs(n, 0.0);
    for (auto& [r, c] : pat.entries())
        rhs[r] += mat.value(pat.offset(r, c)) * x_true[c];

    solver.solve(rhs);
    for (int32_t i = 0; i < n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-9);
}

TEST(NeoSolver, SparseTierRefactorize) {
    int32_t n = 50;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i-1);
        if (i < n-1) sb.add(i, i+1);
    }
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    for (int32_t i = 0; i < n; ++i) {
        mat.add(pat.offset(i, i), 4.0);
        if (i > 0) mat.add(pat.offset(i, i-1), -1.0);
        if (i < n-1) mat.add(pat.offset(i, i+1), -1.0);
    }

    NeoSolver solver;
    solver.symbolic(pat);
    solver.numeric(pat, mat);

    // Refactorize with different values
    mat.clear();
    for (int32_t i = 0; i < n; ++i) {
        mat.add(pat.offset(i, i), 8.0);
        if (i > 0) mat.add(pat.offset(i, i-1), -2.0);
        if (i < n-1) mat.add(pat.offset(i, i+1), -2.0);
    }

    solver.refactorize(mat);

    // Compute rhs = A * x_true with refactored values, verify solve recovers x_true
    std::vector<double> x_true(n, 1.0);
    std::vector<double> rhs(n, 0.0);
    for (auto& [r, c] : pat.entries())
        rhs[r] += mat.value(pat.offset(r, c)) * x_true[c];

    solver.solve(rhs);
    for (int32_t i = 0; i < n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-10);
}

TEST(NeoSolver, SparseTierComplex50x50) {
    int32_t n = 50;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i-1);
        if (i < n-1) sb.add(i, i+1);
    }
    SparsityPattern pat = sb.build();
    int32_t nnz = pat.nnz();

    // Build complex ax: diag = (10+1i), off-diag = (-1+0.5i)
    std::vector<double> ax(2 * nnz, 0.0);
    int32_t idx = 0;
    for (auto& [r, c] : pat.entries()) {
        if (r == c) { ax[2*idx] = 10.0; ax[2*idx+1] = 1.0; }
        else { ax[2*idx] = -1.0; ax[2*idx+1] = 0.5; }
        ++idx;
    }

    NeoSolver solver;
    solver.symbolic(pat);
    solver.numeric_complex(pat, ax);

    // Build known rhs from x_true, verify solve recovers x_true
    std::vector<double> x_true(2*n);
    for (int32_t i = 0; i < n; ++i) {
        x_true[2*i] = static_cast<double>(i+1);
        x_true[2*i+1] = 0.5;
    }

    // Compute rhs = A * x_true (complex matrix-vector multiply)
    std::vector<double> rhs(2*n, 0.0);
    idx = 0;
    for (auto& [r, c] : pat.entries()) {
        double ar = ax[2*idx], ai = ax[2*idx+1];
        double xr = x_true[2*c], xi = x_true[2*c+1];
        rhs[2*r]   += ar*xr - ai*xi;
        rhs[2*r+1] += ar*xi + ai*xr;
        ++idx;
    }

    solver.solve_complex(rhs);
    for (int32_t i = 0; i < 2*n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-9);
}
