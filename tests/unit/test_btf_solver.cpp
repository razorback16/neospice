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

    std::vector<double> ax = {2.0,1.0, 0.0,0.0, 1.0,0.0, 3.0,2.0};
    solver.numeric_complex(pat, ax);

    std::vector<double> rhs = {5.0,3.0, 6.0,4.0};
    solver.solve_complex(rhs);

    SmallSolver ref;
    ref.symbolic(pat);
    ref.numeric_complex(pat, ax);
    std::vector<double> rhs_ref = {5.0,3.0, 6.0,4.0};
    ref.solve_complex(rhs_ref);

    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(rhs[i], rhs_ref[i], 1e-10);
}

TEST(BTFSolver, LargeBlockTriangular) {
    int32_t n = 100;
    int32_t block_size = 20;
    SparsityBuilder sb(n);
    for (int32_t b = 0; b < 5; ++b) {
        int32_t base = b * block_size;
        for (int32_t i = 0; i < block_size; ++i)
            for (int32_t j = 0; j < block_size; ++j)
                sb.add(base + i, base + j);
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
    int32_t n = 300;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i)
        for (int32_t d = -3; d <= 3; ++d) {
            int32_t j = i + d;
            if (j >= 0 && j < n) sb.add(i, j);
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

    std::vector<double> rhs(n, 0.0);
    for (auto& [r, c] : pat.entries())
        rhs[r] += mat.value(pat.offset(r, c)) * x_true[c];

    solver.solve(rhs);
    for (int32_t i = 0; i < n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-8);
}
