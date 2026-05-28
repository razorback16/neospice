#pragma once
#include "core/types.hpp"
#include "core/matrix.hpp"
#include "neospice/types.hpp"
#include <cctype>
#include <complex>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace neospice {

struct OneBasedEvalArrays {
    double* rhs_old = nullptr;
    double* rhs = nullptr;
    int32_t size = 0;
};

extern thread_local OneBasedEvalArrays* tls_one_based_eval_arrays;

class Circuit;  // forward decl for declare_internal_nodes

constexpr int32_t GROUND_INTERNAL = static_cast<int32_t>(GND);  // internal index for ground node

class Device {
public:
    explicit Device(std::string name) : name_(std::move(name)) {}
    virtual ~Device() = default;
    const std::string& name() const { return name_; }

    virtual std::string device_type() const {
        if (name_.empty()) return "unknown";
        // Subcircuit expansion prefixes device names with hierarchy
        // (for example x1.m3). The local instance name after the final dot
        // is the SPICE device designator that ngspice uses for type lookup.
        std::size_t pos = 0;
        const std::size_t dot = name_.rfind('.');
        if (dot != std::string::npos && dot + 1 < name_.size())
            pos = dot + 1;
        switch (static_cast<unsigned char>(std::toupper(name_[pos]))) {
            case 'R': return "R"; case 'C': return "C"; case 'L': return "L";
            case 'V': return "V"; case 'I': return "I"; case 'K': return "K";
            case 'D': return "D"; case 'Q': return "Q"; case 'J': return "J";
            case 'M': return "M"; case 'E': return "E"; case 'G': return "G";
            case 'F': return "F"; case 'H': return "H"; case 'S': return "S";
            case 'W': return "W"; case 'B': return "B"; case 'T': return "T";
            case 'O': return "O"; case 'X': return "X";
            default: return "unknown";
        }
    }

    virtual std::vector<int32_t> external_nodes() const { return ext_nodes_; }

    virtual std::optional<double> primary_value() const { return std::nullopt; }

    virtual bool set_value(double /*value*/) { return false; }

    /// ngspice stores modeled devices under model lists and links instances
    /// at the list head. Circuit::finalize() uses these keys to mirror that
    /// setup/load traversal without changing neospice's ownership order.
    void set_ngspice_setup_order(int model_order, int instance_order) {
        ngspice_model_order_ = model_order;
        ngspice_instance_order_ = instance_order;
    }
    int ngspice_model_order() const { return ngspice_model_order_; }
    int ngspice_instance_order() const { return ngspice_instance_order_; }

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

    /// Return the MNA branch variable index for devices that carry an extra
    /// branch current variable (voltage sources, inductors, controlled
    /// sources, ASRC in voltage mode).  Returns -1 when the device has no
    /// branch variable.  Subclasses that call assign_branch_index() should
    /// override this.
    virtual int32_t branch_index() const { return -1; }

    /// Apply temperature-dependent parameter adjustments.
    /// Called during Circuit::finalize() with the simulation temperature and
    /// nominal temperature.  Subclasses that have temperature coefficients
    /// (resistors, capacitors, inductors, ASRC) override this.
    virtual void process_temperature(double /*sim_temp*/, double /*sim_tnom*/) {}

    /// Stamp this device's AC excitation into the complex RHS vector.
    /// Called during AC analysis to build the AC stimulus.  Only independent
    /// sources (VSource, ISource) with non-zero AC magnitude override this.
    /// @param n  total number of MNA variables (size of ac_rhs)
    virtual void apply_ac_excitation(std::vector<std::complex<double>>& /*ac_rhs*/,
                                     int32_t /*n*/) {}

    /// Number of BSIM-style state slots per instance (0 for stateless devices).
    /// Summed by Circuit during finalize() to size the per-circuit state buffers.
    virtual int32_t state_vars() const { return 0; }

    /// Bind three rotating state buffers and the per-instance base offset.
    /// state0 is the latest iterate; state1/state2 are previous timesteps
    /// (for PREDICTOR and integrator history). Default is a no-op.
    virtual void set_state_ptrs(double* /*state0*/, double* /*state1*/,
                                double* /*state2*/, double* /*state3*/,
                                int32_t /*base*/) {}

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
    virtual bool device_converged(const std::vector<double>& solution) const {
        (void)solution;
        return device_converged();
    }

    /// Query a device operating-point or geometry parameter by name.
    /// Returns the parameter value, or std::nullopt if the name is
    /// unrecognized.  Case-insensitive.  Populated after a DC solve or
    /// during transient evaluation.
    virtual std::optional<double> query_param(const std::string& /*name*/) const {
        return std::nullopt;
    }

    /// Returns true if the device's matrix contributions changed drastically
    /// enough during this iteration that the pivot ordering might be invalid
    /// (e.g. a switch flipped state).  Default is false — most devices never
    /// change matrix structure between Newton iterations.
    virtual bool matrix_structure_changed() const { return false; }

    /// Reset device state for a fresh simulation pass (e.g. between .step
    /// sweep iterations).  Subclasses override to clear internal caches.
    virtual void reset() {}

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

    /// Correlated noise source: two current noise sources with a phase
    /// relationship.  The combined output noise contribution is
    ///   S1*|H1|^2 + S2*|H2|^2 + 2*sqrt(S1*S2)*Re(conj(H1)*e^(j*phase)*H2)
    /// where H1 = adj[n1_i]-adj[n1_j], H2 = adj[n2_i]-adj[n2_j].
    struct CorrelatedNoiseSource {
        int32_t n1_i, n1_j;   // first source nodes
        int32_t n2_i, n2_j;   // second source nodes
        double psd1;           // spectral density of first source (A²/Hz)
        double psd2;           // spectral density of second source (A²/Hz)
        double phase;          // phase of source 2 relative to source 1 (radians)
    };

    virtual std::vector<CorrelatedNoiseSource> correlated_noise_sources(
        double /*freq*/, const std::vector<double>& /*dc_solution*/) const {
        return {};
    }

    struct SoaResult {
        bool ok = true;
        std::string param_name;
        double value = 0.0;
        double limit = 0.0;
    };

    virtual SoaResult check_soa(const std::vector<double>& /*solution*/) const {
        return {};
    }

protected:
    std::string name_;
    std::vector<int32_t> ext_nodes_;
    double sim_temp_ = T_NOMINAL;  ///< simulation temperature for noise (K)
    int ngspice_model_order_ = std::numeric_limits<int>::max();
    int ngspice_instance_order_ = -1;

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
