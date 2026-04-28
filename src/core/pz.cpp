#include "core/pz.hpp"
#include "core/circuit.hpp"
#include "core/dc.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/neo_solver.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

extern "C" void dggev_(char* jobvl, char* jobvr, int* n,
                       double* a, int* lda, double* b, int* ldb,
                       double* alphar, double* alphai, double* beta,
                       double* vl, int* ldvl, double* vr, int* ldvr,
                       double* work, int* lwork, int* info);

namespace neospice {

PZResult solve_pz(Circuit& ckt,
                  const std::string& in_pos, const std::string& in_neg,
                  const std::string& out_pos, const std::string& out_neg,
                  PZTransferType transfer, PZType type)
{
    auto t_start = std::chrono::steady_clock::now();
    const int32_t n = ckt.num_vars();

    // 1. DC operating point (same approach as ac.cpp)
    std::vector<double> dc_solution(n, 0.0);
    std::vector<char> pinned(n, 0);
    for (auto& [node_idx, value] : ckt.nodeset) {
        if (node_idx >= 0 && node_idx < n) {
            dc_solution[node_idx] = value;
            pinned[node_idx] = 1;
        }
    }
    for (auto& [node_idx, value] : ckt.ic) {
        if (node_idx >= 0 && node_idx < n && !pinned[node_idx]) {
            dc_solution[node_idx] = value;
        }
    }

    auto dc_solver = std::make_unique<NeoSolver>();
    dc_solver->symbolic(ckt.pattern());
    ckt.integrator_ctx.options = &ckt.options;

    constexpr int MODEDCOP_BIT    = 0x10;
    constexpr int MODEINITJCT_BIT = 0x200;
    constexpr int MODEINITFIX_BIT = 0x400;

    ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITJCT_BIT;
    auto dc_result = newton_solve(ckt, *dc_solver, dc_solution, ckt.options);
    if (dc_result.converged) {
        dc_solution = dc_result.solution;
    } else {
        ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
        dc_result = gmin_stepping(ckt, *dc_solver, dc_solution, ckt.options);
        if (dc_result.converged) {
            dc_solution = dc_result.solution;
        } else {
            ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
            dc_result = source_stepping(ckt, *dc_solver, dc_solution, ckt.options);
            if (dc_result.converged) {
                dc_solution = dc_result.solution;
            } else {
                ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
                dc_result = pseudo_transient(ckt, *dc_solver, dc_solution, ckt.options);
                if (dc_result.converged) {
                    dc_solution = dc_result.solution;
                } else {
                    throw ConvergenceError("PZ analysis: DC operating point failed to converge");
                }
            }
        }
    }

    // 2. MODEINITSMSIG pass — compute small-signal parameters at DC operating point
    {
        constexpr int MODEAC_BIT        = 0x2;
        constexpr int MODEINITSMSIG_BIT = 0x800;
        ckt.integrator_ctx.mode = MODEAC_BIT | MODEINITSMSIG_BIT;

        NumericMatrix smsig_mat(ckt.pattern());
        smsig_mat.clear();
        std::vector<double> smsig_rhs(n, 0.0);

        struct IntegratorCtxGuard {
            IntegratorCtxGuard(const IntegratorCtx& c) { tls_integrator_ctx = &c; }
            ~IntegratorCtxGuard()                      { tls_integrator_ctx = nullptr; }
        } guard(ckt.integrator_ctx);
        for (auto& dev : ckt.devices()) {
            dev->evaluate(dc_solution, smsig_mat, smsig_rhs);
        }
    }

    // 3. Build sparse G and C matrices
    const auto& pattern = ckt.pattern();
    NumericMatrix G(pattern);
    NumericMatrix C(pattern);
    G.clear();
    C.clear();
    for (auto& dev : ckt.devices()) {
        dev->ac_stamp(dc_solution, G, C);
    }

    // 4. Convert to dense column-major matrices (LAPACK format)
    std::vector<double> Gdense(n * n, 0.0);
    std::vector<double> Cdense(n * n, 0.0);
    const auto& entries = pattern.entries();
    for (int32_t k = 0; k < pattern.nnz(); ++k) {
        int32_t row = entries[k].first;
        int32_t col = entries[k].second;
        Gdense[col * n + row] = G.data()[k];   // column-major
        Cdense[col * n + row] = C.data()[k];
    }

    PZResult result;
    result.type = type;
    result.transfer_type = transfer;
    result.input_pos = in_pos;
    result.input_neg = in_neg;
    result.output_pos = out_pos;
    result.output_neg = out_neg;

    // 5. Compute poles via generalized eigenvalue: G*x = lambda*C*x => s = -lambda
    if (type == PZType::POLES || type == PZType::BOTH) {
        char jobvl = 'N', jobvr = 'N';
        int nn = n;
        int lda = n, ldb = n;
        std::vector<double> alphar(n), alphai(n), beta(n);
        int ldvl = 1, ldvr = 1;
        double vl_dummy, vr_dummy;
        double work_opt;
        int lwork = -1;
        int info = 0;

        // Make working copies (dggev overwrites inputs)
        std::vector<double> A = Gdense;
        std::vector<double> B = Cdense;

        // Workspace query
        dggev_(&jobvl, &jobvr, &nn, A.data(), &lda, B.data(), &ldb,
               alphar.data(), alphai.data(), beta.data(),
               &vl_dummy, &ldvl, &vr_dummy, &ldvr,
               &work_opt, &lwork, &info);

        lwork = static_cast<int>(work_opt);
        std::vector<double> work(lwork);

        // Restore A and B (workspace query may have modified them)
        A = Gdense;
        B = Cdense;

        // Actual computation
        dggev_(&jobvl, &jobvr, &nn, A.data(), &lda, B.data(), &ldb,
               alphar.data(), alphai.data(), beta.data(),
               &vl_dummy, &ldvl, &vr_dummy, &ldvr,
               work.data(), &lwork, &info);

        if (info == 0) {
            for (int i = 0; i < nn; ++i) {
                if (std::abs(beta[i]) > 1e-30) {
                    // s = -lambda = -(alpha/beta)
                    double re = -alphar[i] / beta[i];
                    double im = -alphai[i] / beta[i];
                    result.poles.emplace_back(re, im);
                }
                // beta[i] ~ 0 means infinite eigenvalue => not a physical pole, skip
            }
            // Sort by real part (most negative first)
            std::sort(result.poles.begin(), result.poles.end(),
                      [](const auto& a, const auto& b) { return a.real() < b.real(); });
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    result.status.converged = true;
    result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
    return result;
}

} // namespace neospice
