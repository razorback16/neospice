#include "core/solver_iface.hpp"
#include "core/neo_solver.hpp"
#include "core/amd_lu_solver.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace neospice {

namespace {

// ---------------------------------------------------------------------------
// AMD-LU auto-enable threshold (number of unknowns / matrix dimension).
//
// Derivation: the whole KiCad parity suite must stay on Markowitz so the
// default ("auto") path is byte-identical to the historical baseline (including
// the 3 LinearTech op-amps that diverge under forced AMD-LU). We measured the
// matrix dimension (num_vars) via NEOSPICE_PRINT_NVARS over every generated .op
// netlist the comparator can produce (34,908 model/subckt tests). Findings:
//   - first 5000 tests (the parity gate the orchestrator runs): MAX = 191
//   - full library: median 12, p95 69, p99 140, MAX = 5129
//       (twisted_pair1024 = 5129, twisted_pair256 = 1289, symmetric_line256 =
//        1030; everything else is < 400). These large RC-line models sit at
//       index >9800 in the sorted list, past the 5000-gate, but we still must
//       not let them auto-switch.
//
// We set the threshold to 12000 unknowns — above 2x the library-wide MAX
// (2 * 5129 = 10258) — so:
//   - EVERY KiCad test circuit (max 5129) stays on Markowitz, guaranteeing the
//     auto path is byte-identical no matter how large a subset is run, and
//   - AMD-LU still auto-engages where it actually pays off — AMD ordering +
//     Gilbert-Peierls only wins on large sparse systems (measured 7.6x at ~20k
//     nodes). A 141x141 resistor mesh (19,882 unknowns) crosses the threshold
//     and gets the fast solver.
constexpr int kAmdLuAutoThreshold = 12000;

enum class SolverChoice { kAuto, kForceAmdLu, kForceMarkowitz };

SolverChoice read_choice() {
    if (const char* s = std::getenv("NEOSPICE_SOLVER")) {
        if (std::strcmp(s, "amdlu") == 0 || std::strcmp(s, "klu") == 0)
            return SolverChoice::kForceAmdLu;
        if (std::strcmp(s, "markowitz") == 0 || std::strcmp(s, "sparse") == 0)
            return SolverChoice::kForceMarkowitz;
        // "auto" or any unrecognized value falls through to the default policy.
    }
    // Legacy alias: NEOSPICE_FORCE_AMDLU=<non-empty, non-"0"> forces AMD-LU.
    if (const char* f = std::getenv("NEOSPICE_FORCE_AMDLU")) {
        if (f[0] != '\0' && std::strcmp(f, "0") != 0)
            return SolverChoice::kForceAmdLu;
    }
    return SolverChoice::kAuto;
}

// ---------------------------------------------------------------------------
// Auto-path wrapper: drive AMD-LU, but if it reports a hard factor failure
// (structural/numeric singularity) at the FIRST factorization, swap to
// Markowitz (NeoSolver) for the rest of this solver's life and re-run the
// factorization there. This handles the rare circuit that AMD-LU's static
// pivot order cannot factor but Markowitz can, without masking genuine
// singularities (if Markowitz also reports singular, the singular status
// propagates exactly as before).
//
// Scope: the swap can only happen on the first numeric() — once AMD-LU has
// produced a valid factorization, later refactorize() instabilities are
// handled internally by AmdLuSolver (full re-pivot fallback) and never count
// as a hard failure here. Basin/roundoff divergence is NOT a failure and is
// out of scope (it is not reported via the singular return).
class AutoFallbackSolver : public ISolver {
public:
    AutoFallbackSolver()
        : amd_(std::make_unique<AmdLuSolver>()) {}

    void symbolic(const SparsityPattern& pattern) override {
        // Keep the pattern so we can replay symbolic() on the Markowitz engine
        // if we have to fall back during the first numeric(). The pattern
        // outlives the solver on every real call site (it is owned by the
        // Circuit), so holding a pointer is safe for the solver's lifetime.
        pattern_ = &pattern;
        amd_->symbolic(pattern);
    }

    bool numeric(const SparsityPattern& pattern, const NumericMatrix& mat,
                 double diag_gmin = 0.0) override {
        if (fell_back_)
            return neo_->numeric(pattern, mat, diag_gmin);

        bool singular = amd_->numeric(pattern, mat, diag_gmin);
        if (singular && !committed_) {
            // AMD-LU could not factor the very first system. Try Markowitz.
            switch_to_markowitz();
            return neo_->numeric(pattern, mat, diag_gmin);
        }
        // AMD-LU produced a factorization (or is singular on a system Markowitz
        // could not factor either); commit to AMD-LU from here on. We only
        // treat the very first factorization specially.
        committed_ = true;
        return singular;
    }

    bool refactorize(const NumericMatrix& mat, double diag_gmin = 0.0) override {
        return active().refactorize(mat, diag_gmin);
    }

    void solve(std::vector<double>& rhs) override { active().solve(rhs); }

    void numeric_complex(const SparsityPattern& pattern,
                         const std::vector<double>& ax) override {
        active().numeric_complex(pattern, ax);
    }
    bool refactorize_complex(const std::vector<double>& ax) override {
        return active().refactorize_complex(ax);
    }
    void solve_complex(std::vector<double>& rhs) override {
        active().solve_complex(rhs);
    }

    const char* name() const override { return active().name(); }

private:
    ISolver& active() { return fell_back_ ? static_cast<ISolver&>(*neo_)
                                          : static_cast<ISolver&>(*amd_); }
    const ISolver& active() const {
        return fell_back_ ? static_cast<const ISolver&>(*neo_)
                          : static_cast<const ISolver&>(*amd_);
    }

    void switch_to_markowitz() {
        neo_ = std::make_unique<NeoSolver>();
        if (pattern_) neo_->symbolic(*pattern_);
        fell_back_ = true;
        committed_ = true;
        if (std::getenv("NEOSPICE_DEBUG_COMPARE"))
            std::cerr << "[solver-select] AMD-LU singular at first factor; "
                         "falling back to Markowitz\n";
    }

    std::unique_ptr<AmdLuSolver> amd_;
    std::unique_ptr<NeoSolver> neo_;
    const SparsityPattern* pattern_ = nullptr;
    bool fell_back_ = false;
    bool committed_ = false;  // a factorization has been accepted on amd_
};

void maybe_print_nvars(int num_vars, const char* engine) {
    if (std::getenv("NEOSPICE_PRINT_NVARS"))
        std::cerr << "NVARS|" << num_vars << "|" << engine << "\n";
}

}  // namespace

int amdlu_auto_threshold() { return kAmdLuAutoThreshold; }

std::unique_ptr<ISolver> make_solver(int num_vars) {
    switch (read_choice()) {
        case SolverChoice::kForceMarkowitz:
            maybe_print_nvars(num_vars, "markowitz");
            return std::make_unique<NeoSolver>();
        case SolverChoice::kForceAmdLu:
            maybe_print_nvars(num_vars, "amdlu");
            return std::make_unique<AmdLuSolver>();
        case SolverChoice::kAuto:
        default:
            if (num_vars >= kAmdLuAutoThreshold) {
                maybe_print_nvars(num_vars, "amdlu-auto");
                return std::make_unique<AutoFallbackSolver>();
            }
            maybe_print_nvars(num_vars, "markowitz");
            return std::make_unique<NeoSolver>();
    }
}

std::unique_ptr<ISolver> make_solver() { return make_solver(0); }

}  // namespace neospice
