/**********
Portions of this file are translated from UC Berkeley SPICE3F5
(noisean.c, adjoint noise analysis) and derive from the earlier
SPICE2G6 (1983) Fortran, tracing to L. Nagel's SPICE, UC Berkeley
ERL Memorandum M382 (1973).

Copyright 1990 Regents of the University of California. All rights reserved.
(Permissive BSD-style "Berkeley Spice3" license.)
See NOTICE and CREDITS.md for full attribution.
**********/

#include "core/noise.hpp"
#include "core/freq_utils.hpp"
#include "core/dc.hpp"
#include "core/neo_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/inductor.hpp"
#include "core/ckt_mode.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <stdexcept>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

NoiseResult solve_noise(Circuit& ckt,
                        const std::string& output_node,
                        const std::string& input_src,
                        ACMode mode,
                        int npoints, double fstart, double fstop) {
    auto t_start = std::chrono::steady_clock::now();
    const int32_t n = ckt.num_vars();

    // ---------------------------------------------------------------
    // 1. Find the output node index
    // ---------------------------------------------------------------
    int32_t out_idx;
    try {
        out_idx = ckt.node_index(to_lower(output_node));
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Noise analysis: output node '" + output_node + "' not found");
    }
    if (out_idx < 0) {
        throw std::runtime_error("Noise analysis: output node cannot be ground");
    }

    // ---------------------------------------------------------------
    // 2. Find the input voltage source and its branch index
    // ---------------------------------------------------------------
    const VSource* input_vs = nullptr;
    for (const auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<const VSource*>(dev.get())) {
            if (to_lower(vs->name()) == to_lower(input_src)) {
                input_vs = vs;
                break;
            }
        }
    }
    if (!input_vs) {
        throw std::runtime_error("Noise analysis: input source '" + input_src + "' not found");
    }
    int32_t input_branch = input_vs->branch_index();
    if (input_branch < 0 || input_branch >= n) {
        throw std::runtime_error("Noise analysis: input source '" + input_src +
                                 "' has invalid branch index");
    }

    // ---------------------------------------------------------------
    // 3. DC operating point (same as AC analysis)
    // ---------------------------------------------------------------
    // Use the same operating-point implementation as .op and AC, including
    // reference-order true-gmin and transient continuation. A separate reduced
    // fallback chain can reject a bias point that .op successfully finds.
    const auto dc = solve_dc(ckt);
    if (!dc.status.converged) {
        NoiseResult fail_result;
        fail_result.status = dc.status;
        return fail_result;
    }
    const auto* operating_point = ckt.operating_point();
    if (!operating_point || static_cast<int32_t>(operating_point->size()) != n)
        throw std::logic_error("Noise analysis: DC operating point cache missing");
    const std::vector<double> dc_solution = *operating_point;
    // Persist diag_gmin baseline after DC convergence
    ckt.options.diag_gmin = ckt.options.gshunt;

    // ---------------------------------------------------------------
    // 4. MODEINITSMSIG pass — compute small-signal parameters
    // ---------------------------------------------------------------
    {
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

    // ---------------------------------------------------------------
    // 4b. Build G and C matrices
    // ---------------------------------------------------------------
    const auto& pattern = ckt.pattern();
    NumericMatrix G(pattern);
    NumericMatrix C(pattern);
    G.clear();
    C.clear();

    for (auto& dev : ckt.devices()) {
        dev->ac_stamp(dc_solution, G, C);
    }

    // ---------------------------------------------------------------
    // 4c. Propagate simulation temperature to all devices so that
    //     noise_sources() uses the correct temperature from SimOptions.
    // ---------------------------------------------------------------
    for (auto& dev : ckt.devices()) {
        dev->set_sim_temp(ckt.options.temp);
    }

    // ---------------------------------------------------------------
    // 5. Generate frequency points
    // ---------------------------------------------------------------
    auto freqs = generate_frequencies(mode, npoints, fstart, fstop, ckt.options.reltol, FrequencyAnalysis::Noise);
    if (freqs.empty()) {
        throw SimulationError("Noise analysis: invalid or empty frequency sweep", SimStatus{.converged = false});
    }

    // ---------------------------------------------------------------
    // 6. One complex solver for both systems, as in ngspice noisean.c:
    //    NIacIter factors Y and solves Y x = e_input for the gain, then
    //    NInzIter reuses that factorization to solve Y^T adj = e_out
    //    (SMPcaSolve -> spSolveTransposed).
    // ---------------------------------------------------------------
    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(pattern);

    // ---------------------------------------------------------------
    // 7. Prepare result
    // ---------------------------------------------------------------
    NoiseResult noise_result;
    noise_result.frequency = freqs;
    noise_result.output_noise_density.resize(freqs.size(), 0.0);
    noise_result.input_noise_density.resize(freqs.size(), 0.0);

    // Initialize per-device breakdown; remember each device's row so the
    // sweep does not look names up per frequency.
    std::vector<std::vector<double>*> device_rows(ckt.devices().size(), nullptr);
    for (std::size_t d = 0; d < ckt.devices().size(); ++d) {
        const auto& dev = ckt.devices()[d];
        auto sources = dev->noise_sources(1.0, dc_solution);
        auto corr = dev->correlated_noise_sources(1.0, dc_solution);
        if (!sources.empty() || !corr.empty()) {
            auto& row = noise_result.device_noise[to_lower(dev->name())];
            row.resize(freqs.size(), 0.0);
            device_rows[d] = &row;
        }
    }

    // ---------------------------------------------------------------
    // 8. Frequency sweep
    // ---------------------------------------------------------------
    const int32_t nnz = pattern.nnz();
    std::vector<double> ax(2 * nnz);
    std::vector<std::complex<double>> device_rhs(n);
    // Interleaved (re, im) per variable.
    std::vector<double> rhs_gain(2 * n);
    std::vector<double> rhs_adj(2 * n);
    for (size_t fi = 0; fi < freqs.size(); ++fi) {
        double omega = 2.0 * M_PI * freqs[fi];

        ckt.integrator_ctx.ac_freq = freqs[fi];
        for (int32_t k = 0; k < nnz; ++k) {
            ax[2 * k] = G.data()[k];
            ax[2 * k + 1] = omega * C.data()[k];
        }
        std::fill(device_rhs.begin(), device_rhs.end(), std::complex<double>{});
        // Use the same frequency-dependent admittance as AC. Deterministic
        // AC-source amplitudes are excluded: noise gain uses its unit input
        // excitation and the adjoint uses a unit output excitation below.
        for (auto& dev : ckt.devices())
            dev->ac_stamp_freq(omega, ax, nnz, device_rhs);

        if (fi == 0) {
            solver->numeric_complex(pattern, ax);
        } else {
            solver->refactorize_complex(ax);
        }

        // ---- Compute gain: Y * x = e_input ----
        // The input excitation is a unit voltage at the input source's branch equation
        std::fill(rhs_gain.begin(), rhs_gain.end(), 0.0);
        rhs_gain[2 * input_branch] = 1.0;
        solver->solve_complex(rhs_gain);

        // Gain from input source to output node
        double gain_re = rhs_gain[2 * out_idx];
        double gain_im = rhs_gain[2 * out_idx + 1];
        double gain_sq = gain_re * gain_re + gain_im * gain_im;

        // ---- Solve adjoint: Y^T * adj = e_out ----
        std::fill(rhs_adj.begin(), rhs_adj.end(), 0.0);
        rhs_adj[2 * out_idx] = 1.0;  // unit real excitation at output node
        solver->solve_complex_transposed(rhs_adj);
        // rhs_adj[2i] = Re(adj[i]), rhs_adj[2i+1] = Im(adj[i])

        // ---- Accumulate noise from all devices ----
        double total_output_noise = 0.0;

        for (std::size_t d = 0; d < ckt.devices().size(); ++d) {
            const auto& dev = ckt.devices()[d];
            auto sources = dev->noise_sources(freqs[fi], dc_solution);
            if (sources.empty()) continue;

            double device_contribution = 0.0;
            for (const auto& ns : sources) {
                // Adjoint values at the two noise source nodes
                double adj_i_re = 0.0, adj_i_im = 0.0;
                double adj_j_re = 0.0, adj_j_im = 0.0;

                if (ns.node_i >= 0 && ns.node_i < n) {
                    adj_i_re = rhs_adj[2 * ns.node_i];
                    adj_i_im = rhs_adj[2 * ns.node_i + 1];
                }
                if (ns.node_j >= 0 && ns.node_j < n) {
                    adj_j_re = rhs_adj[2 * ns.node_j];
                    adj_j_im = rhs_adj[2 * ns.node_j + 1];
                }

                // |adj[i] - adj[j]|^2
                double diff_re = adj_i_re - adj_j_re;
                double diff_im = adj_i_im - adj_j_im;
                double transfer_sq = diff_re * diff_re + diff_im * diff_im;

                // Output noise contribution: S * |H|^2
                device_contribution += ns.spectral_density * transfer_sq;
            }

            total_output_noise += device_contribution;

            // Per-device breakdown
            if (device_rows[d]) (*device_rows[d])[fi] = device_contribution;
        }

        // ---- Accumulate correlated noise from all devices ----
        for (std::size_t d = 0; d < ckt.devices().size(); ++d) {
            const auto& dev = ckt.devices()[d];
            auto corr = dev->correlated_noise_sources(freqs[fi], dc_solution);
            if (corr.empty()) continue;

            double device_contribution = 0.0;
            for (const auto& cs : corr) {
                auto adj_val = [&](int32_t node, double& re, double& im) {
                    re = im = 0.0;
                    if (node >= 0 && node < n) {
                        re = rhs_adj[2 * node];
                        im = rhs_adj[2 * node + 1];
                    }
                };
                double h1_re_i, h1_im_i, h1_re_j, h1_im_j;
                double h2_re_i, h2_im_i, h2_re_j, h2_im_j;
                adj_val(cs.n1_i, h1_re_i, h1_im_i);
                adj_val(cs.n1_j, h1_re_j, h1_im_j);
                adj_val(cs.n2_i, h2_re_i, h2_im_i);
                adj_val(cs.n2_j, h2_re_j, h2_im_j);

                double h1_re = h1_re_i - h1_re_j;
                double h1_im = h1_im_i - h1_im_j;
                double h2_re = h2_re_i - h2_re_j;
                double h2_im = h2_im_i - h2_im_j;

                double s1_sq = std::sqrt(cs.psd1);
                double s2_sq = std::sqrt(cs.psd2);
                double cos_p = std::cos(cs.phase);
                double sin_p = std::sin(cs.phase);

                double re_out = s1_sq * h1_re + s2_sq * (cos_p * h2_re - sin_p * h2_im);
                double im_out = s1_sq * h1_im + s2_sq * (cos_p * h2_im + sin_p * h2_re);
                device_contribution += re_out * re_out + im_out * im_out;
            }

            total_output_noise += device_contribution;

            if (device_rows[d]) (*device_rows[d])[fi] += device_contribution;
        }

        noise_result.output_noise_density[fi] = total_output_noise;

        // Match ngspice noisean.c / noisedef.h: N_MINGAIN bounds squared
        // transfer gain, including zero gain, before referring noise to input.
        // It is an analysis convention, independent of comparison tolerances.
        constexpr double min_gain_squared = 1e-20;
        const double inverse_gain_squared = 1.0 / std::max(gain_sq, min_gain_squared);
        noise_result.input_noise_density[fi] = total_output_noise * inverse_gain_squared;
    }

    auto t_end = std::chrono::steady_clock::now();
    noise_result.status.converged = true;
    noise_result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
    return noise_result;
}

} // namespace neospice
