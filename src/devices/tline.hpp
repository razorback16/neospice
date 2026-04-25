#pragma once
#include "devices/device.hpp"
#include <vector>

namespace neospice {

/// Lossless transmission line (T element) — Branin companion model.
///
/// SPICE syntax: T<name> p1+ p1- p2+ p2- Z0=val TD=val
///
/// At each transient timestep the TL is modelled as two independent
/// Norton-equivalent circuits (one per port):
///
///   Port 1: conductance G0=1/Z0 || current source  I_hist1 = e1/Z0
///   Port 2: conductance G0=1/Z0 || current source  I_hist2 = e2/Z0
///
/// where:
///   e1(t) = V2(t-TD) + Z0*I2(t-TD)   (wave incident on port 1)
///   e2(t) = V1(t-TD) + Z0*I1(t-TD)   (wave incident on port 2)
///
/// History is stored as a circular time-ordered list of {time, V1, I1, V2, I2}
/// records.  After each accepted timestep the transient solver calls
/// accept_step() to push the new record.  evaluate() interpolates the
/// delayed values from this list.
///
/// DC: a lossless transmission line is a short circuit at DC.  We model this
/// by stamping a large conductance (1e9 S) that ties p1+ to p2+ and p1- to
/// p2-.  This ensures correct DC coupling between the two ports.
///
/// AC: the exact frequency-domain Y-matrix is stamped per frequency via
/// ac_stamp_freq().  The Y-parameters are:
///   Y11 = Y22 = -j * G0 * cot(ω·TD)    (self-admittance)
///   Y12 = Y21 =  j * G0 * csc(ω·TD)    (cross-admittance)
/// These are purely imaginary for a lossless line and reproduce the exact
/// phase shift and impedance at every frequency.
class TransmissionLine : public Device {
public:
    TransmissionLine(std::string name,
                     int32_t p1_pos, int32_t p1_neg,
                     int32_t p2_pos, int32_t p2_neg,
                     double z0, double td);

    // Device interface
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;
    bool ac_stamp_freq(double omega,
                       std::vector<double>& ax, int32_t nnz,
                       std::vector<std::complex<double>>& ac_rhs) override;

    // Called by the transient solver after each accepted timestep.
    // Records the converged port voltages and currents into the history buffer
    // so that delayed values are available for future timesteps.
    void accept_step(double time, const std::vector<double>& solution);

    // Enable or disable the transient companion model.
    // When false (DC mode), a shunt conductance is stamped on each port.
    void set_transient(bool enable);

    // Initialize the delay-line history from the DC operating point solution.
    void init_dc_state(const std::vector<double>& sol);

    /// Return breakpoints at t = k*TD for k = 1, 2, ... up to tstop.
    std::vector<double> get_breakpoints(double tstart, double tstop) const;

    void set_ic(double v1, double i1, double v2, double i2);
    bool has_ic() const { return has_ic_; }

    double z0() const { return z0_; }
    double td() const { return td_; }

    int32_t p1_pos() const { return p1p_; }
    int32_t p1_neg() const { return p1n_; }
    int32_t p2_pos() const { return p2p_; }
    int32_t p2_neg() const { return p2n_; }

    std::vector<int32_t> external_nodes() const override { return {p1p_, p1n_, p2p_, p2n_}; }

private:
    int32_t p1p_, p1n_, p2p_, p2n_;
    double z0_, td_;
    double g0_;           // 1/Z0

    bool transient_ = false;

    bool has_ic_ = false;
    double ic_v1_ = 0.0, ic_i1_ = 0.0, ic_v2_ = 0.0, ic_i2_ = 0.0;

    /// One record per accepted timestep
    struct HistoryPoint {
        double time;
        double v1;   // V(p1p) - V(p1n)  at this timestep
        double i1;   // Port-1 current into the line (= G0*v1 - e1/Z0)
        double v2;   // V(p2p) - V(p2n)  at this timestep
        double i2;   // Port-2 current into the line
    };
    std::vector<HistoryPoint> history_;

    // Cached delayed source values (updated in evaluate() from the history).
    double e1_ = 0.0;   // wave arriving at port 1 from port 2 (delayed)
    double e2_ = 0.0;   // wave arriving at port 2 from port 1 (delayed)

    // Port-1 matrix offsets: conductance shunt between p1p and p1n
    MatrixOffset off_p1pp_ = -1, off_p1pn_ = -1, off_p1np_ = -1, off_p1nn_ = -1;
    // Port-2 matrix offsets: conductance shunt between p2p and p2n
    MatrixOffset off_p2pp_ = -1, off_p2pn_ = -1, off_p2np_ = -1, off_p2nn_ = -1;

    // Cross-port matrix offsets (for DC short-circuit model: p1+↔p2+ and p1-↔p2-)
    MatrixOffset off_p1p_p2p_ = -1, off_p1p_p2n_ = -1;
    MatrixOffset off_p1n_p2p_ = -1, off_p1n_p2n_ = -1;
    MatrixOffset off_p2p_p1p_ = -1, off_p2p_p1n_ = -1;
    MatrixOffset off_p2n_p1p_ = -1, off_p2n_p1n_ = -1;

    /// Interpolate delayed wave values from the history buffer.
    /// Sets e1_ and e2_ for use in the next evaluate() call.
    void update_delayed_values(double t_delayed);
};

} // namespace neospice
