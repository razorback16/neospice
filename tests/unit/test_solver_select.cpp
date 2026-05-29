// Stage 4 solver-selection policy tests.
//
// Covers:
//   - make_solver(num_vars) picks Markowitz below the auto threshold and AMD-LU
//     at/above it (observed via ISolver::name()).
//   - NEOSPICE_SOLVER env override semantics (auto / amdlu / klu / markowitz /
//     sparse) and the legacy NEOSPICE_FORCE_AMDLU alias.
//   - The runtime cross-solver fallback: an auto-selected AMD-LU solver still
//     produces a correct solve, and a hard-singular system is reported singular
//     (not silently masked) without hanging/crashing.

#include <gtest/gtest.h>
#include "core/solver_iface.hpp"
#include "core/matrix.hpp"
#include <cstdlib>
#include <string>
#include <vector>

using namespace neospice;

namespace {

// RAII helper to set/restore an environment variable for one test.
class ScopedEnv {
public:
    explicit ScopedEnv(const char* name) : name_(name) {
        if (const char* v = std::getenv(name)) {
            had_ = true;
            old_ = v;
        }
    }
    void set(const char* value) { ::setenv(name_.c_str(), value, 1); }
    void unset() { ::unsetenv(name_.c_str()); }
    ~ScopedEnv() {
        if (had_) ::setenv(name_.c_str(), old_.c_str(), 1);
        else ::unsetenv(name_.c_str());
    }
private:
    std::string name_;
    bool had_ = false;
    std::string old_;
};

// Build a symmetric positive-definite tridiagonal pattern/matrix of size n.
struct Tridiag {
    SparsityPattern pat;
    NumericMatrix mat;
    static Tridiag build(int n) {
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
        return Tridiag{std::move(pat), std::move(mat)};
    }
};

std::vector<double> make_rhs(const SparsityPattern& pat, const NumericMatrix& mat,
                             const std::vector<double>& x) {
    std::vector<double> rhs(pat.size(), 0.0);
    for (auto& [r, c] : pat.entries())
        rhs[r] += mat.value(pat.offset(r, c)) * x[c];
    return rhs;
}

}  // namespace

// ---------------------------------------------------------------------------
// Auto policy: size threshold
// ---------------------------------------------------------------------------

TEST(SolverSelect, AutoBelowThresholdPicksMarkowitz) {
    ScopedEnv solver("NEOSPICE_SOLVER");   solver.unset();
    ScopedEnv force("NEOSPICE_FORCE_AMDLU"); force.unset();

    const int thr = amdlu_auto_threshold();
    ASSERT_GT(thr, 1);

    auto s0 = make_solver(0);
    EXPECT_STREQ(s0->name(), "markowitz");

    auto s1 = make_solver(thr - 1);
    EXPECT_STREQ(s1->name(), "markowitz");
}

TEST(SolverSelect, AutoAtOrAboveThresholdPicksAmdLu) {
    ScopedEnv solver("NEOSPICE_SOLVER");   solver.unset();
    ScopedEnv force("NEOSPICE_FORCE_AMDLU"); force.unset();

    const int thr = amdlu_auto_threshold();
    // The auto path wraps AMD-LU in the fallback solver, which reports the
    // active engine — AMD-LU before any fallback. name() must read "amdlu".
    auto s_at = make_solver(thr);
    EXPECT_STREQ(s_at->name(), "amdlu");

    auto s_big = make_solver(thr * 2);
    EXPECT_STREQ(s_big->name(), "amdlu");
}

// Every KiCad parity circuit (measured library MAX = 5129 unknowns) must stay
// on Markowitz under the default policy. Guard the threshold against regression.
TEST(SolverSelect, KicadMaxStaysBelowThreshold) {
    EXPECT_GE(amdlu_auto_threshold(), 2 * 5129)
        << "threshold must stay above 2x the measured KiCad library max so no "
           "parity circuit auto-switches to AMD-LU";
}

// ---------------------------------------------------------------------------
// Env override semantics
// ---------------------------------------------------------------------------

TEST(SolverSelect, EnvForceAmdLuOverridesSmallSize) {
    ScopedEnv force("NEOSPICE_FORCE_AMDLU"); force.unset();
    ScopedEnv solver("NEOSPICE_SOLVER");

    for (const char* v : {"amdlu", "klu"}) {
        solver.set(v);
        auto s = make_solver(1);  // tiny circuit, but forced
        EXPECT_STREQ(s->name(), "amdlu") << "NEOSPICE_SOLVER=" << v;
    }
}

TEST(SolverSelect, EnvForceMarkowitzOverridesLargeSize) {
    ScopedEnv force("NEOSPICE_FORCE_AMDLU"); force.unset();
    ScopedEnv solver("NEOSPICE_SOLVER");

    for (const char* v : {"markowitz", "sparse"}) {
        solver.set(v);
        auto s = make_solver(amdlu_auto_threshold() * 4);  // huge, but forced
        EXPECT_STREQ(s->name(), "markowitz") << "NEOSPICE_SOLVER=" << v;
    }
}

TEST(SolverSelect, EnvAutoStringIsTheDefaultPolicy) {
    ScopedEnv force("NEOSPICE_FORCE_AMDLU"); force.unset();
    ScopedEnv solver("NEOSPICE_SOLVER");
    solver.set("auto");
    EXPECT_STREQ(make_solver(1)->name(), "markowitz");
    EXPECT_STREQ(make_solver(amdlu_auto_threshold())->name(), "amdlu");
}

TEST(SolverSelect, LegacyForceAmdLuAlias) {
    ScopedEnv solver("NEOSPICE_SOLVER");   solver.unset();
    ScopedEnv force("NEOSPICE_FORCE_AMDLU");

    force.set("1");
    EXPECT_STREQ(make_solver(1)->name(), "amdlu");

    force.set("0");  // disabled -> back to auto
    EXPECT_STREQ(make_solver(1)->name(), "markowitz");

    force.set("");   // empty -> disabled
    EXPECT_STREQ(make_solver(1)->name(), "markowitz");
}

// NEOSPICE_SOLVER takes precedence over the legacy alias.
TEST(SolverSelect, ExplicitSolverBeatsLegacyAlias) {
    ScopedEnv solver("NEOSPICE_SOLVER");
    ScopedEnv force("NEOSPICE_FORCE_AMDLU");
    solver.set("markowitz");
    force.set("1");
    EXPECT_STREQ(make_solver(1)->name(), "markowitz");
}

// ---------------------------------------------------------------------------
// Runtime fallback: auto-selected AMD-LU still solves correctly
// ---------------------------------------------------------------------------

// On a well-conditioned large system the auto path uses AMD-LU and the solve is
// correct (no fallback needed). Exercises make_solver -> AutoFallbackSolver ->
// AmdLuSolver end to end at exactly the threshold size.
TEST(SolverSelect, AutoSelectedAmdLuSolvesCorrectly) {
    ScopedEnv solver("NEOSPICE_SOLVER");   solver.unset();
    ScopedEnv force("NEOSPICE_FORCE_AMDLU"); force.unset();

    const int n = amdlu_auto_threshold();
    Tridiag t = Tridiag::build(n);
    std::vector<double> x_true(n);
    for (int i = 0; i < n; ++i) x_true[i] = (i % 7) + 1;
    std::vector<double> rhs = make_rhs(t.pat, t.mat, x_true);

    auto s = make_solver(n);
    ASSERT_STREQ(s->name(), "amdlu");
    s->symbolic(t.pat);
    bool singular = s->numeric(t.pat, t.mat);
    ASSERT_FALSE(singular);
    s->solve(rhs);
    for (int i = 0; i < n; ++i)
        EXPECT_NEAR(rhs[i], x_true[i], 1e-7) << "x[" << i << "]";
    // No fallback should have happened: still AMD-LU.
    EXPECT_STREQ(s->name(), "amdlu");
}

// A structurally singular matrix (a zero row) must be reported singular by BOTH
// engines: the auto path tries AMD-LU, falls back to Markowitz, and Markowitz
// also reports singular -> the singular status propagates (no masking, no hang).
TEST(SolverSelect, HardSingularReportedAndDoesNotHang) {
    const int n = amdlu_auto_threshold();
    // Diagonal everywhere except row/col k which is left empty (zero pivot).
    const int k = n / 2;
    SparsityBuilder sb(n);
    for (int i = 0; i < n; ++i)
        if (i != k) sb.add(i, i);
    // Give k a structural slot off-diagonal so the column exists but the pivot
    // is zero -> genuinely singular for any pivoting strategy.
    sb.add(k, (k + 1) % n);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    for (int i = 0; i < n; ++i)
        if (i != k) mat.add(pat.offset(i, i), 3.0);
    mat.add(pat.offset(k, (k + 1) % n), 1.0);  // row k has no diagonal pivot

    ScopedEnv solver("NEOSPICE_SOLVER");   solver.unset();
    ScopedEnv force("NEOSPICE_FORCE_AMDLU"); force.unset();

    auto s = make_solver(n);
    s->symbolic(pat);
    bool singular = s->numeric(pat, mat);
    EXPECT_TRUE(singular)
        << "a structurally singular system must be reported singular by both "
           "AMD-LU and the Markowitz fallback (not masked)";
}
