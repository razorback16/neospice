#pragma once
#include "devices/device.hpp"
#include <span>
#include <array>
#include <vector>

namespace neospice {

/// Lossless transmission line (T element) — Branin companion model.
///
/// SPICE syntax: T<name> p1+ p1- p2+ p2- Z0=val TD=val
///
/// At each transient timestep the line uses independent delayed-wave port
/// equations V1-Z0*I1=e1 and V2-Z0*I2=e2, with two MNA branch currents.
///
/// where:
///   e1(t) = V2(t-TD) + Z0*I2(t-TD)   (wave incident on port 1)
///   e2(t) = V1(t-TD) + Z0*I1(t-TD)   (wave incident on port 2)
///
/// History is stored as a time-ordered vector of {time, V1, I1, V2, I2}
/// records.  After each accepted timestep the transient solver calls
/// accept_step() to push the new record.  evaluate() interpolates the
/// delayed values from this list.
///
/// DC: the branch equations enforce equal differential port voltages and
/// opposite port currents, without tying the ports' common-mode voltages.
/// AC: V1-Z0*I1 = exp(-j*w*TD)*(V2+Z0*I2), and the symmetric equation.
/// This form remains well conditioned at the poles of the equivalent Y matrix.
class TransmissionLine : public Device {
public:
    TransmissionLine(std::string name,
                     int32_t p1_pos, int32_t p1_neg,
                     int32_t p2_pos, int32_t p2_neg,
                     double z0, double td);

    // Device interface
    int32_t extra_vars() const override { return 2; }
    void assign_branch_index(int32_t& next) override;
    int32_t branch_index() const override { return br1_; }
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::span<double> rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;
    bool ac_stamp_freq(double omega,
                       std::vector<double>& ax, int32_t nnz,
                       std::vector<std::complex<double>>& ac_rhs) override;

    // Called by the transient solver after each accepted timestep.
    // Records the converged port voltages and currents into the history buffer
    // so that delayed values are available for future timesteps.
    std::optional<double> accept_step(double time, const std::vector<double>& solution,
                                     const IntegratorCtx& ctx, double min_break);

    // TRAtrunc needs the converged solution, not a preceding Newton iterate.
    double trunc_timestep(const IntegratorCtx& ctx,
                          const std::vector<double>& solution) const;

    // Enable or disable the transient companion model.
    // When false (DC mode), the two wave equations use unit delay gain.
    void set_transient(bool enable);

    // Seed history from the DC operating point, or from IC values under UIC.
    void init_dc_state(const std::vector<double>& sol, bool uic = false);

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
    int32_t br1_ = -1, br2_ = -1;

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
    HistoryPoint sample(double time, const std::vector<double>& solution) const;
    bool wave_slope_changed(const HistoryPoint& latest, const HistoryPoint& previous,
                            const HistoryPoint& older, double dt, double previous_dt) const;

    // Cached delayed source values (updated in evaluate() from the history).
    double e1_ = 0.0;   // wave arriving at port 1 from port 2 (delayed)
    double e2_ = 0.0;   // wave arriving at port 2 from port 1 (delayed)

    // Variable order: p1+, p1-, p2+, p2-, I1, I2.
    std::array<int32_t, 6> variables() const {
        return {p1p_, p1n_, p2p_, p2n_, br1_, br2_};
    }
    std::array<std::array<MatrixOffset, 6>, 6> offsets_{};
    void stamp_port_equations(NumericMatrix& mat) const;

    /// Interpolate delayed wave values from the history buffer.
    /// Sets e1_ and e2_ for use in the next evaluate() call.
    void update_delayed_values(double t_delayed);
};

} // namespace neospice
