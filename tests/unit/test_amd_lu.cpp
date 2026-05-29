#include <gtest/gtest.h>
#include "core/amd_lu_solver.hpp"
#include "core/neo_solver.hpp"
#include "core/matrix.hpp"
#include <random>
#include <vector>

using namespace neospice;

namespace {

// Build rhs = A * x_true using the matrix's stored entries.
std::vector<double> make_rhs(const SparsityPattern& pat, const NumericMatrix& mat,
                             const std::vector<double>& x_true) {
    std::vector<double> rhs(pat.size(), 0.0);
    for (auto& [r, c] : pat.entries())
        rhs[r] += mat.value(pat.offset(r, c)) * x_true[c];
    return rhs;
}

// Factor+solve with AmdLuSolver and assert it matches both x_true and NeoSolver.
void check_parity(const SparsityPattern& pat, const NumericMatrix& mat,
                  const std::vector<double>& x_true, double tol) {
    const int32_t n = pat.size();
    std::vector<double> rhs = make_rhs(pat, mat, x_true);

    // Reference solve with NeoSolver.
    std::vector<double> rhs_neo = rhs;
    NeoSolver neo;
    neo.symbolic(pat);
    bool neo_sing = neo.numeric(pat, mat);
    ASSERT_FALSE(neo_sing) << "NeoSolver reported singular for a nonsingular matrix";
    neo.solve(rhs_neo);

    // AmdLuSolver solve.
    std::vector<double> rhs_amd = rhs;
    AmdLuSolver amd;
    amd.symbolic(pat);
    bool amd_sing = amd.numeric(pat, mat);
    ASSERT_FALSE(amd_sing) << "AmdLuSolver reported singular for a nonsingular matrix";
    amd.solve(rhs_amd);

    for (int32_t i = 0; i < n; ++i) {
        EXPECT_NEAR(rhs_amd[i], x_true[i], tol) << "AmdLu vs x_true at " << i;
        EXPECT_NEAR(rhs_amd[i], rhs_neo[i], tol) << "AmdLu vs NeoSolver at " << i;
    }
}

}  // namespace

TEST(AmdLuSolver, Dense1x1) {
    SparsityBuilder sb(1);
    sb.add(0, 0);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 3.0);
    check_parity(pat, mat, {7.0}, 1e-12);
}

TEST(AmdLuSolver, Dense2x2) {
    SparsityBuilder sb(2);
    for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) sb.add(i, j);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 2.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(0, 1), 1.0);
    mat.add(pat.offset(1, 1), 3.0);
    check_parity(pat, mat, {1.6, 1.8}, 1e-10);
}

TEST(AmdLuSolver, DenseDiagDominant8x8) {
    int n = 8;
    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) sb.add(i, j);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            mat.add(pat.offset(i, j), (i == j) ? 20.0 : 1.0);
    std::vector<double> x_true(n);
    for (int i = 0; i < n; ++i) x_true[i] = i + 1;
    check_parity(pat, mat, x_true, 1e-9);
}

TEST(AmdLuSolver, Tridiagonal60) {
    int n = 60;
    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i - 1);
        if (i < n - 1) sb.add(i, i + 1);
    }
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    for (int i = 0; i < n; ++i) {
        mat.add(pat.offset(i, i), 4.0);
        if (i > 0) mat.add(pat.offset(i, i - 1), -1.0);
        if (i < n - 1) mat.add(pat.offset(i, i + 1), -1.0);
    }
    std::vector<double> x_true(n);
    for (int i = 0; i < n; ++i) x_true[i] = (i % 5) + 1;
    check_parity(pat, mat, x_true, 1e-9);
}

TEST(AmdLuSolver, Arrowhead) {
    // Hub node (0) connected to all others; classic AMD fill-reduction case.
    int n = 16;
    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i) sb.add(i, i);
    for (int i = 1; i < n; ++i) { sb.add(0, i); sb.add(i, 0); }
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    for (int i = 0; i < n; ++i) mat.add(pat.offset(i, i), (i == 0) ? 100.0 : 5.0);
    for (int i = 1; i < n; ++i) {
        mat.add(pat.offset(0, i), 1.0);
        mat.add(pat.offset(i, 0), 1.0);
    }
    std::vector<double> x_true(n);
    for (int i = 0; i < n; ++i) x_true[i] = (i % 3) + 0.5;
    check_parity(pat, mat, x_true, 1e-9);
}

TEST(AmdLuSolver, RandomSparseSPD) {
    int n = 80;
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> ud(-1.0, 1.0);

    // Build a symmetric sparse pattern, make SPD via diagonal dominance.
    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i) sb.add(i, i);
    std::uniform_int_distribution<int> nd(0, n - 1);
    for (int e = 0; e < 4 * n; ++e) {
        int i = nd(rng), j = nd(rng);
        if (i == j) continue;
        sb.add(i, j); sb.add(j, i);
    }
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);

    // Off-diagonals symmetric, diagonal = sum of |row off-diag| + margin.
    std::vector<double> rowsum(n, 0.0);
    for (auto& [r, c] : pat.entries()) {
        if (r == c) continue;
        if (r < c) {
            double v = ud(rng);
            mat.add(pat.offset(r, c), v);
            mat.add(pat.offset(c, r), v);
            rowsum[r] += std::abs(v);
            rowsum[c] += std::abs(v);
        }
    }
    for (int i = 0; i < n; ++i)
        mat.add(pat.offset(i, i), rowsum[i] + 1.0);

    std::vector<double> x_true(n);
    for (int i = 0; i < n; ++i) x_true[i] = std::sin(0.3 * i) + 1.0;
    check_parity(pat, mat, x_true, 1e-7);
}

TEST(AmdLuSolver, RandomNonsymmetricDiagDominant) {
    int n = 70;
    std::mt19937 rng(987);
    std::uniform_real_distribution<double> ud(-1.0, 1.0);
    std::uniform_int_distribution<int> nd(0, n - 1);

    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i) sb.add(i, i);
    for (int e = 0; e < 5 * n; ++e) {
        int i = nd(rng), j = nd(rng);
        if (i != j) sb.add(i, j);
    }
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    std::vector<double> rowabs(n, 0.0);
    for (auto& [r, c] : pat.entries()) {
        if (r == c) continue;
        double v = ud(rng);
        mat.add(pat.offset(r, c), v);
        rowabs[r] += std::abs(v);
    }
    for (int i = 0; i < n; ++i)
        mat.add(pat.offset(i, i), rowabs[i] + 2.0);
    std::vector<double> x_true(n);
    for (int i = 0; i < n; ++i) x_true[i] = std::cos(0.2 * i) - 0.3;
    check_parity(pat, mat, x_true, 1e-7);
}

TEST(AmdLuSolver, IndefiniteNeedsPivoting) {
    // Tiny zero-diagonal matrix that forces a row pivot:
    // [0 1; 1 1] x = [1; 2] -> x = [1; 1]
    SparsityBuilder sb(2);
    sb.add(0, 1); sb.add(1, 0); sb.add(1, 1);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 1), 1.0);
    mat.add(pat.offset(1, 0), 1.0);
    mat.add(pat.offset(1, 1), 1.0);
    check_parity(pat, mat, {1.0, 1.0}, 1e-12);
}

TEST(AmdLuSolver, ZeroDiagBlock4x4) {
    // Saddle-point-like: [[0 0 1 0],[0 0 1 1],[1 1 5 0],[0 1 0 3]]
    int n = 4;
    SparsityBuilder sb(n);
    auto E = [&](int r, int c) { sb.add(r, c); };
    E(0,2); E(1,2); E(1,3); E(2,0); E(2,1); E(2,2); E(3,1); E(3,3);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    mat.add(pat.offset(0,2), 1.0);
    mat.add(pat.offset(1,2), 1.0);
    mat.add(pat.offset(1,3), 1.0);
    mat.add(pat.offset(2,0), 1.0);
    mat.add(pat.offset(2,1), 1.0);
    mat.add(pat.offset(2,2), 5.0);
    mat.add(pat.offset(3,1), 1.0);
    mat.add(pat.offset(3,3), 3.0);
    check_parity(pat, mat, {2.0, -1.0, 0.5, 1.5}, 1e-9);
}

TEST(AmdLuSolver, DiagGminApplied) {
    // Singular without gmin (zero diagonal), nonsingular with gmin on diagonal.
    // A = [[0 0],[0 0]] + gmin*I. With gmin=1e-3, A = 1e-3 I -> x = b/1e-3.
    SparsityBuilder sb(2);
    sb.add(0, 0); sb.add(1, 1);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 0.0);
    mat.add(pat.offset(1, 1), 0.0);

    double gmin = 1e-3;
    std::vector<double> rhs = {1.0, 2.0};

    NeoSolver neo;
    neo.symbolic(pat);
    ASSERT_FALSE(neo.numeric(pat, mat, gmin));
    std::vector<double> rhs_neo = rhs;
    neo.solve(rhs_neo);

    AmdLuSolver amd;
    amd.symbolic(pat);
    ASSERT_FALSE(amd.numeric(pat, mat, gmin));
    std::vector<double> rhs_amd = rhs;
    amd.solve(rhs_amd);

    for (int i = 0; i < 2; ++i)
        EXPECT_NEAR(rhs_amd[i], rhs_neo[i], 1e-9) << "gmin parity at " << i;
}

TEST(AmdLuSolver, RefactorizeChangesValues) {
    int n = 40;
    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i - 1);
        if (i < n - 1) sb.add(i, i + 1);
    }
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    for (int i = 0; i < n; ++i) {
        mat.add(pat.offset(i, i), 4.0);
        if (i > 0) mat.add(pat.offset(i, i - 1), -1.0);
        if (i < n - 1) mat.add(pat.offset(i, i + 1), -1.0);
    }

    AmdLuSolver amd;
    amd.symbolic(pat);
    ASSERT_FALSE(amd.numeric(pat, mat));

    // New values
    mat.clear();
    for (int i = 0; i < n; ++i) {
        mat.add(pat.offset(i, i), 8.0);
        if (i > 0) mat.add(pat.offset(i, i - 1), -2.0);
        if (i < n - 1) mat.add(pat.offset(i, i + 1), -2.0);
    }
    ASSERT_FALSE(amd.refactorize(mat));

    std::vector<double> x_true(n, 1.0);
    std::vector<double> rhs = make_rhs(pat, mat, x_true);
    amd.solve(rhs);
    for (int i = 0; i < n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-10);
}

// Refactor fast path: after numeric(), change values, refactorize(), and assert
// the solve matches a fresh-from-numeric() solve of the SAME changed matrix.
// Also prove the fast (replay) path is actually taken (counter), not a fallback.
TEST(AmdLuSolver, RefactorFastPathMatchesFreshFactor) {
    int n = 50;
    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i - 1);
        if (i < n - 1) sb.add(i, i + 1);
        if (i + 2 < n) sb.add(i, i + 2);  // a little asymmetry/fill
    }
    SparsityPattern pat = sb.build();

    auto fill = [&](NumericMatrix& m, double s) {
        for (int i = 0; i < n; ++i) {
            m.add(pat.offset(i, i), 10.0 * s + 0.1 * i);
            if (i > 0) m.add(pat.offset(i, i - 1), -1.0 * s);
            if (i < n - 1) m.add(pat.offset(i, i + 1), -1.5 * s);
            if (i + 2 < n) m.add(pat.offset(i, i + 2), -0.3 * s);
        }
    };

    // Initial full factor on scale s0.
    NumericMatrix mat0(pat);
    fill(mat0, 1.0);
    AmdLuSolver amd;
    amd.symbolic(pat);
    ASSERT_FALSE(amd.numeric(pat, mat0));
    EXPECT_EQ(amd.refactor_fast_count(), 0);

    // Change values, then refactorize (should take fast path).
    NumericMatrix mat1(pat);
    fill(mat1, 1.7);
    ASSERT_FALSE(amd.refactorize(mat1));
    EXPECT_EQ(amd.refactor_fast_count(), 1) << "fast replay path not taken";
    EXPECT_EQ(amd.refactor_fallback_count(), 0) << "unexpected fallback";

    // Fresh-from-numeric reference solve of the SAME changed matrix.
    AmdLuSolver fresh;
    fresh.symbolic(pat);
    ASSERT_FALSE(fresh.numeric(pat, mat1));

    std::vector<double> x_true(n);
    for (int i = 0; i < n; ++i) x_true[i] = std::sin(0.2 * i) + 1.3;
    std::vector<double> rhs_ref = make_rhs(pat, mat1, x_true);
    std::vector<double> rhs_amd = rhs_ref;
    fresh.solve(rhs_ref);
    amd.solve(rhs_amd);
    for (int i = 0; i < n; ++i) {
        EXPECT_NEAR(rhs_amd[i], x_true[i], 1e-9) << "refactor vs x_true at " << i;
        EXPECT_NEAR(rhs_amd[i], rhs_ref[i], 1e-12)
            << "refactor vs fresh factor at " << i;  // bit-identical structure
    }

    // A second refactor with yet another value set still uses the fast path.
    NumericMatrix mat2(pat);
    fill(mat2, 0.6);
    ASSERT_FALSE(amd.refactorize(mat2));
    EXPECT_EQ(amd.refactor_fast_count(), 2);
    EXPECT_EQ(amd.refactor_fallback_count(), 0);
    std::vector<double> rhs2 = make_rhs(pat, mat2, x_true);
    amd.solve(rhs2);
    for (int i = 0; i < n; ++i)
        EXPECT_NEAR(rhs2[i], x_true[i], 1e-9) << "second refactor at " << i;
}

// Refactor fallback: drive a previously-good diagonal pivot near zero so the
// reused pivot fails the growth check. Assert it falls back to a full factor
// AND still solves correctly (the fallback re-pivots).
TEST(AmdLuSolver, RefactorFallbackOnUnstablePivot) {
    // 3x3 dense. First factor with a strong diagonal so pivots = diagonal.
    int n = 3;
    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) sb.add(i, j);
    SparsityPattern pat = sb.build();

    NumericMatrix mat0(pat);
    // Diagonally dominant: pivots are the diagonal entries.
    double a0[3][3] = {{10, 1, 2}, {1, 12, 3}, {2, 1, 15}};
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) mat0.add(pat.offset(i, j), a0[i][j]);

    AmdLuSolver amd;
    amd.symbolic(pat);
    ASSERT_FALSE(amd.numeric(pat, mat0));

    // New values: collapse the (0,0) diagonal to ~0 while making the matrix
    // still nonsingular overall. Reusing the old pivot (row 0 at step for col 0)
    // would yield a tiny U(0,0) -> growth check trips -> fallback re-pivots.
    NumericMatrix mat1(pat);
    double a1[3][3] = {{1e-12, 1, 2}, {1, 12, 3}, {2, 1, 15}};
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) mat1.add(pat.offset(i, j), a1[i][j]);

    ASSERT_FALSE(amd.refactorize(mat1));
    EXPECT_GE(amd.refactor_fallback_count(), 1) << "expected pivot fallback";

    // Still solves correctly (compare against NeoSolver on the same matrix).
    std::vector<double> x_true = {2.0, -1.0, 0.5};
    std::vector<double> rhs = make_rhs(pat, mat1, x_true);

    NeoSolver neo;
    neo.symbolic(pat);
    ASSERT_FALSE(neo.numeric(pat, mat1));
    std::vector<double> rhs_neo = rhs;
    neo.solve(rhs_neo);

    std::vector<double> rhs_amd = rhs;
    amd.solve(rhs_amd);
    for (int i = 0; i < n; ++i) {
        EXPECT_NEAR(rhs_amd[i], x_true[i], 1e-7) << "fallback solve at " << i;
        EXPECT_NEAR(rhs_amd[i], rhs_neo[i], 1e-7) << "fallback vs neo at " << i;
    }
}

// gmin must be applied identically on the refactor fast path.
TEST(AmdLuSolver, RefactorAppliesGmin) {
    int n = 30;
    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i - 1);
        if (i < n - 1) sb.add(i, i + 1);
    }
    SparsityPattern pat = sb.build();
    auto fill = [&](NumericMatrix& m, double s) {
        for (int i = 0; i < n; ++i) {
            m.add(pat.offset(i, i), 3.0 * s);
            if (i > 0) m.add(pat.offset(i, i - 1), -1.0 * s);
            if (i < n - 1) m.add(pat.offset(i, i + 1), -1.0 * s);
        }
    };
    double gmin = 1e-2;

    NumericMatrix mat0(pat); fill(mat0, 1.0);
    AmdLuSolver amd; amd.symbolic(pat);
    ASSERT_FALSE(amd.numeric(pat, mat0, gmin));

    NumericMatrix mat1(pat); fill(mat1, 1.4);
    ASSERT_FALSE(amd.refactorize(mat1, gmin));
    EXPECT_EQ(amd.refactor_fast_count(), 1);

    // Fresh factor of the same matrix+gmin as reference.
    AmdLuSolver fresh; fresh.symbolic(pat);
    ASSERT_FALSE(fresh.numeric(pat, mat1, gmin));

    std::vector<double> rhs(n);
    for (int i = 0; i < n; ++i) rhs[i] = (i % 4) + 1.0;
    std::vector<double> rhs_amd = rhs, rhs_fresh = rhs;
    amd.solve(rhs_amd);
    fresh.solve(rhs_fresh);
    for (int i = 0; i < n; ++i)
        EXPECT_NEAR(rhs_amd[i], rhs_fresh[i], 1e-12) << "gmin refactor at " << i;
}

TEST(AmdLuSolver, StructurallySingular) {
    // Empty column 1 -> structurally singular.
    SparsityBuilder sb(2);
    sb.add(0, 0); sb.add(1, 0);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    mat.add(pat.offset(0, 0), 1.0);
    mat.add(pat.offset(1, 0), 1.0);
    AmdLuSolver amd;
    amd.symbolic(pat);
    EXPECT_TRUE(amd.numeric(pat, mat));  // singular
}

TEST(AmdLuSolver, ComplexDelegatesToNeoSolver) {
    SparsityBuilder sb(1);
    sb.add(0, 0);
    SparsityPattern pat = sb.build();
    AmdLuSolver amd;
    amd.symbolic(pat);
    std::vector<double> ax = {3.0, 4.0};  // (3+4i)
    amd.numeric_complex(pat, ax);
    std::vector<double> rhs = {11.0, 2.0};
    amd.solve_complex(rhs);
    EXPECT_NEAR(rhs[0], 1.64, 1e-10);
    EXPECT_NEAR(rhs[1], -1.52, 1e-10);
}
