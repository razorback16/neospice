#pragma once
#include "core/types.hpp"
#include "core/matrix.hpp"
#include <complex>
#include <optional>
#include <string>
#include <vector>

namespace neospice {

class Circuit;  // forward decl for declare_internal_nodes

constexpr int32_t GROUND_INTERNAL = -1;  // internal index for ground node

class Device {
public:
    explicit Device(std::string name) : name_(std::move(name)) {}
    virtual ~Device() = default;
    const std::string& name() const { return name_; }

    /// Called by Circuit::finalize() before branch assignment and sparsity
    /// build. Devices that need internal MNA nodes (e.g. BSIM4 resistance
    /// models) override this to allocate them via ckt.node().
    virtual void declare_internal_nodes(Circuit& /*ckt*/) {}

    virtual void stamp_pattern(SparsityBuilder& builder) const = 0;
    virtual void assign_offsets(const SparsityPattern& pattern) = 0;
    virtual void evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) = 0;
    virtual void limit_voltages(const std::vector<double>& /*old_v*/,
                                std::vector<double>& /*new_v*/) {}
    virtual void ac_stamp(const std::vector<double>& /*voltages*/,
                          NumericMatrix& /*G*/, NumericMatrix& /*C*/) {}

    /// Per-frequency AC stamp for devices with frequency-dependent matrix
    /// entries (e.g. transmission lines with propagation delay).
    /// Called once per frequency point, AFTER the base G+jωC matrix is built.
    /// Returns true if the device handled the stamp (caller skips G+jωC
    /// fallback for this device's entries); false means no action taken.
    /// ax is the interleaved complex NNZ array: ax[2*k]=real, ax[2*k+1]=imag.
    virtual bool ac_stamp_freq(double /*omega*/,
                               std::vector<double>& /*ax*/, int32_t /*nnz*/,
                               std::vector<std::complex<double>>& /*ac_rhs*/) {
        return false;  // not handled; caller falls back to G + jwC
    }

    virtual int32_t extra_vars() const { return 0; }

    /// Assign the branch (extra MNA variable) index during finalize().
    /// Devices with extra_vars() > 0 override this to store their branch
    /// index and advance `next` by extra_vars().
    virtual void assign_branch_index(int32_t& /*next*/) {}
    virtual std::vector<std::string> output_currents() const { return {}; }

    /// Number of BSIM-style state slots per instance (0 for stateless devices).
    /// Summed by Circuit during finalize() to size the per-circuit state buffers.
    virtual int32_t state_vars() const { return 0; }

    /// Bind three rotating state buffers and the per-instance base offset.
    /// state0 is the latest iterate; state1/state2 are previous timesteps
    /// (for PREDICTOR and integrator history). Default is a no-op.
    virtual void set_state_ptrs(double* /*state0*/, double* /*state1*/,
                                double* /*state2*/, int32_t /*base*/) {}

    /// Compute device-specific timestep limit from local truncation error.
    /// Returns suggested maximum timestep (or a very large value if no
    /// constraint).  Called after Newton convergence during transient analysis.
    virtual double compute_trunc(const IntegratorCtx& /*ctx*/,
                                 const SimOptions& /*opts*/) const {
        return 1e30;  // no constraint by default
    }

    /// Device-specific convergence check.  Called by Newton after node-voltage
    /// convergence passes.  Returns true if the device considers itself
    /// converged (e.g. all internal current checks pass).  The default is
    /// true — only devices with their own convergence criteria override this.
    virtual bool device_converged() const { return true; }

    /// Query a device operating-point or geometry parameter by name.
    /// Returns the parameter value, or std::nullopt if the name is
    /// unrecognized.  Case-insensitive.  Populated after a DC solve or
    /// during transient evaluation.
    virtual std::optional<double> query_param(const std::string& /*name*/) const {
        return std::nullopt;
    }

    /// Clear any cached temperature-dependent parameters so they will be
    /// recomputed on the next evaluate() call.  Must be called whenever the
    /// simulation temperature changes (e.g. temperature sweep).
    virtual void reset_temp() {}

    /// Set the simulation temperature used for noise calculations.
    /// Called by the noise solver before the frequency loop so that
    /// noise_sources() uses the correct temperature from SimOptions.
    void set_sim_temp(double t) { sim_temp_ = t; }
    double sim_temp() const { return sim_temp_; }

    /// Noise source descriptor: a current noise source between two nodes.
    struct NoiseSource {
        int32_t node_i;           // first node (GROUND_INTERNAL = -1 for ground)
        int32_t node_j;           // second node (GROUND_INTERNAL = -1 for ground)
        double spectral_density;  // A²/Hz (current noise spectral density)
    };

    /// Return the noise current sources for this device at the given
    /// frequency and DC operating point.  Default: no noise.
    /// Devices that generate noise (resistors, diodes, MOSFETs, BJTs)
    /// override this to return their noise contributions.
    virtual std::vector<NoiseSource> noise_sources(
        double /*freq*/, const std::vector<double>& /*dc_solution*/) const {
        return {};
    }

protected:
    std::string name_;
    double sim_temp_ = T_NOMINAL;  ///< simulation temperature for noise (K)

    // Helpers for ground-aware stamping
    static void stamp_if_not_ground(SparsityBuilder& builder, int32_t r, int32_t c) {
        if (r >= 0 && c >= 0) builder.add(r, c);
    }
    static MatrixOffset offset_if_not_ground(const SparsityPattern& pattern, int32_t r, int32_t c) {
        if (r >= 0 && c >= 0) return pattern.offset(r, c);
        return -1;
    }
    // add_if_valid is the preferred stamp helper for new code.  NOTE: raw
    // `mat.add(off, val)` is also accepted with sentinel-only semantics —
    // NumericMatrix::add no-ops on off == -1 (ground) and asserts on any
    // other negative offset (see core/matrix.cpp). The translated UCB
    // BSIM4v7 load path relies on that sentinel behaviour.
    static void add_if_valid(NumericMatrix& mat, MatrixOffset off, double val) {
        if (off >= 0) mat.add(off, val);
    }
    static void add_rhs_if_valid(std::vector<double>& rhs, int32_t node, double val) {
        if (node >= 0) rhs[node] += val;
    }
};

} // namespace neospice
