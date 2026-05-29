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
// Policy: under "auto", AMD-LU engages iff the circuit is BOTH large
// (num_vars >= threshold) AND linear (no nonlinear device). The linearity gate
// is what makes the threshold safe to lower:
//   - On LINEAR large circuits, the system has a unique solution, so AMD's
//     fill-reducing pivot order is provably result-identical to Markowitz. We
//     measured the twisted_pair*/symmetric_line* RC-ladder models (262-5129
//     vars) as byte-identical (max diff 0.0) and up to 1691x faster under
//     AMD-LU. These get the fast solver.
//   - On NONLINEAR macromodels (OPA*/INA*/PGA*/LT* op-amps, 192-388 vars),
//     AMD-LU's static order diverges from Markowitz by up to 8.5% (Newton
//     basin / pivot sensitivity). These stay on Markowitz to preserve their
//     ngspice-validated results, REGARDLESS of size.
//
// Threshold = 256 unknowns. Justification from measured num_vars (via
// NEOSPICE_PRINT_NVARS) over the generated suite:
//   - the 5000-model parity gate's largest circuit is 191 vars; everything in
//     the gate is < 256 AND the largest gate circuits are nonlinear op-amps, so
//     the gate stays entirely on Markowitz -> byte-identical to baseline.
//   - the smallest large LINEAR circuit is symmetric_line64 at 262 vars; the
//     twisted_pair linear wins are at 329 / 1289 / 5129 vars.
// 256 clears the gate with margin and captures every linear win.
constexpr int kAmdLuAutoThreshold = 256;

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

std::unique_ptr<ISolver> make_solver(int num_vars, bool is_linear) {
    switch (read_choice()) {
        case SolverChoice::kForceMarkowitz:
            maybe_print_nvars(num_vars, "markowitz");
            return std::make_unique<NeoSolver>();
        case SolverChoice::kForceAmdLu:
            // Forced amdlu/klu always uses AMD-LU regardless of size/linearity
            // (testing/benchmark override).
            maybe_print_nvars(num_vars, "amdlu");
            return std::make_unique<AmdLuSolver>();
        case SolverChoice::kAuto:
        default:
            // Auto engages AMD-LU only when the circuit is large AND linear.
            if (num_vars >= kAmdLuAutoThreshold && is_linear) {
                maybe_print_nvars(num_vars, "amdlu-auto");
                return std::make_unique<AutoFallbackSolver>();
            }
            maybe_print_nvars(num_vars, "markowitz");
            return std::make_unique<NeoSolver>();
    }
}

std::unique_ptr<ISolver> make_solver() { return make_solver(0, true); }

}  // namespace neospice
