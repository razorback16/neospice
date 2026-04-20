#include "devices/tline.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx
#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>

namespace neospice {

TransmissionLine::TransmissionLine(std::string name,
                                   int32_t p1_pos, int32_t p1_neg,
                                   int32_t p2_pos, int32_t p2_neg,
                                   double z0, double td)
    : Device(std::move(name)),
      p1p_(p1_pos), p1n_(p1_neg),
      p2p_(p2_pos), p2n_(p2_neg),
      z0_(z0), td_(td),
      g0_(1.0 / z0)
{
    assert(z0 > 0.0 && "Z0 must be positive");
    assert(td >= 0.0 && "TD must be non-negative");
}

// ---------------------------------------------------------------------------
// stamp_pattern / assign_offsets
// ---------------------------------------------------------------------------

void TransmissionLine::stamp_pattern(SparsityBuilder& builder) const {
    // Port 1 shunt conductance: between p1p and p1n
    stamp_if_not_ground(builder, p1p_, p1p_);
    stamp_if_not_ground(builder, p1p_, p1n_);
    stamp_if_not_ground(builder, p1n_, p1p_);
    stamp_if_not_ground(builder, p1n_, p1n_);

    // Port 2 shunt conductance: between p2p and p2n
    stamp_if_not_ground(builder, p2p_, p2p_);
    stamp_if_not_ground(builder, p2p_, p2n_);
    stamp_if_not_ground(builder, p2n_, p2p_);
    stamp_if_not_ground(builder, p2n_, p2n_);

    // Cross-port coupling (for DC short-circuit model: p1+↔p2+ and p1-↔p2-)
    stamp_if_not_ground(builder, p1p_, p2p_);
    stamp_if_not_ground(builder, p1p_, p2n_);
    stamp_if_not_ground(builder, p1n_, p2p_);
    stamp_if_not_ground(builder, p1n_, p2n_);
    stamp_if_not_ground(builder, p2p_, p1p_);
    stamp_if_not_ground(builder, p2p_, p1n_);
    stamp_if_not_ground(builder, p2n_, p1p_);
    stamp_if_not_ground(builder, p2n_, p1n_);
}

void TransmissionLine::assign_offsets(const SparsityPattern& pattern) {
    off_p1pp_ = offset_if_not_ground(pattern, p1p_, p1p_);
    off_p1pn_ = offset_if_not_ground(pattern, p1p_, p1n_);
    off_p1np_ = offset_if_not_ground(pattern, p1n_, p1p_);
    off_p1nn_ = offset_if_not_ground(pattern, p1n_, p1n_);

    off_p2pp_ = offset_if_not_ground(pattern, p2p_, p2p_);
    off_p2pn_ = offset_if_not_ground(pattern, p2p_, p2n_);
    off_p2np_ = offset_if_not_ground(pattern, p2n_, p2p_);
    off_p2nn_ = offset_if_not_ground(pattern, p2n_, p2n_);

    // Cross-port offsets (DC short-circuit model)
    off_p1p_p2p_ = offset_if_not_ground(pattern, p1p_, p2p_);
    off_p1p_p2n_ = offset_if_not_ground(pattern, p1p_, p2n_);
    off_p1n_p2p_ = offset_if_not_ground(pattern, p1n_, p2p_);
    off_p1n_p2n_ = offset_if_not_ground(pattern, p1n_, p2n_);
    off_p2p_p1p_ = offset_if_not_ground(pattern, p2p_, p1p_);
    off_p2p_p1n_ = offset_if_not_ground(pattern, p2p_, p1n_);
    off_p2n_p1p_ = offset_if_not_ground(pattern, p2n_, p1p_);
    off_p2n_p1n_ = offset_if_not_ground(pattern, p2n_, p1n_);
}

// ---------------------------------------------------------------------------
// update_delayed_values
// ---------------------------------------------------------------------------

void TransmissionLine::update_delayed_values(double t_delayed) {
    if (history_.empty() || t_delayed <= 0.0) {
        // No history yet or delay reaches before t=0 — assume zero initial state.
        e1_ = 0.0;
        e2_ = 0.0;
        return;
    }

    // If t_delayed is before the first history entry, use zero initial state.
    if (t_delayed <= history_.front().time) {
        e1_ = 0.0;
        e2_ = 0.0;
        return;
    }

    // If t_delayed is beyond the last entry, extrapolate with the last entry
    // (shouldn't happen in normal operation but be safe).
    if (t_delayed >= history_.back().time) {
        const auto& h = history_.back();
        e1_ = h.v2 + z0_ * h.i2;
        e2_ = h.v1 + z0_ * h.i1;
        return;
    }

    // Linear interpolation between the two bounding history points.
    // Binary search for the first entry with time > t_delayed.
    auto it = std::upper_bound(history_.begin(), history_.end(), t_delayed,
        [](double t, const HistoryPoint& hp) { return t < hp.time; });

    // 'it' points to the first element with time > t_delayed.
    // The element before it has time <= t_delayed.
    const auto& h1 = *(it - 1);
    const auto& h2 = *it;

    double alpha = (h2.time - h1.time > 1e-300)
                   ? (t_delayed - h1.time) / (h2.time - h1.time)
                   : 0.0;
    alpha = std::max(0.0, std::min(1.0, alpha));

    double v1_d  = h1.v1  + alpha * (h2.v1  - h1.v1 );
    double i1_d  = h1.i1  + alpha * (h2.i1  - h1.i1 );
    double v2_d  = h1.v2  + alpha * (h2.v2  - h1.v2 );
    double i2_d  = h1.i2  + alpha * (h2.i2  - h1.i2 );

    e1_ = v2_d + z0_ * i2_d;   // wave arriving at port 1
    e2_ = v1_d + z0_ * i1_d;   // wave arriving at port 2
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------

void TransmissionLine::evaluate(const std::vector<double>& voltages,
                                NumericMatrix& mat, std::vector<double>& rhs) {
    if (!transient_) {
        // DC: TL is a short circuit. Tie p1+↔p2+ and p1-↔p2- with large conductance.
        double g_dc = 1e9;
        // Tie p1p to p2p:
        add_if_valid(mat, off_p1pp_,    g_dc);   // (p1p,p1p) += g_dc
        add_if_valid(mat, off_p2pp_,    g_dc);   // (p2p,p2p) += g_dc
        add_if_valid(mat, off_p1p_p2p_, -g_dc);  // (p1p,p2p) -= g_dc
        add_if_valid(mat, off_p2p_p1p_, -g_dc);  // (p2p,p1p) -= g_dc
        // Tie p1n to p2n:
        add_if_valid(mat, off_p1nn_,    g_dc);   // (p1n,p1n) += g_dc
        add_if_valid(mat, off_p2nn_,    g_dc);   // (p2n,p2n) += g_dc
        add_if_valid(mat, off_p1n_p2n_, -g_dc);  // (p1n,p2n) -= g_dc
        add_if_valid(mat, off_p2n_p1n_, -g_dc);  // (p2n,p1n) -= g_dc
        e1_ = 0.0;
        e2_ = 0.0;
        return;
    }

    // Transient: compute delayed wave sources from history.
    if (tls_integrator_ctx) {
        double t_now = tls_integrator_ctx->current_time;
        update_delayed_values(t_now - td_);
    }

    // Stamp port-1 Norton companion: conductance G0 + current source e1/Z0
    // The Norton current source injects current from p1n to p1p: +e1/Z0 at p1p, -e1/Z0 at p1n.
    add_if_valid(mat, off_p1pp_,  g0_);
    add_if_valid(mat, off_p1pn_, -g0_);
    add_if_valid(mat, off_p1np_, -g0_);
    add_if_valid(mat, off_p1nn_,  g0_);

    // Port-2 Norton companion
    add_if_valid(mat, off_p2pp_,  g0_);
    add_if_valid(mat, off_p2pn_, -g0_);
    add_if_valid(mat, off_p2np_, -g0_);
    add_if_valid(mat, off_p2nn_,  g0_);

    // RHS current sources: I_hist = e/Z0 = e * G0
    double i_h1 = e1_ * g0_;
    double i_h2 = e2_ * g0_;

    add_rhs_if_valid(rhs, p1p_,  i_h1);
    add_rhs_if_valid(rhs, p1n_, -i_h1);
    add_rhs_if_valid(rhs, p2p_,  i_h2);
    add_rhs_if_valid(rhs, p2n_, -i_h2);
}

// ---------------------------------------------------------------------------
// ac_stamp
// ---------------------------------------------------------------------------

void TransmissionLine::ac_stamp(const std::vector<double>& /*voltages*/,
                                NumericMatrix& /*G*/, NumericMatrix& /*C*/) {
    // The lossless TL AC model uses frequency-dependent Y-parameters that are
    // purely imaginary at all frequencies.  Nothing is stamped into the
    // frequency-independent G or C matrices — all AC contributions are handled
    // by ac_stamp_freq() which is called at each frequency point.
}

// ---------------------------------------------------------------------------
// ac_stamp_freq — frequency-dependent cross-port coupling
// ---------------------------------------------------------------------------

bool TransmissionLine::ac_stamp_freq(double omega,
                                      std::vector<double>& ax, int32_t /*nnz*/,
                                      std::vector<std::complex<double>>& /*ac_rhs*/) {
    // Exact frequency-domain Y-matrix for a lossless transmission line:
    //
    //   Y11 = Y22 = -j * G0 * cot(omega * TD)     (self-admittance)
    //   Y12 = Y21 =  j * G0 * csc(omega * TD)     (cross-admittance)
    //
    // These are purely imaginary for a lossless line.  Near omega*TD = n*pi
    // the terms diverge; we use a large-value clamp for numerical stability.

    double theta = omega * td_;

    double sin_theta = std::sin(theta);
    double cos_theta = std::cos(theta);

    double y11_im, y12_im;

    if (std::abs(sin_theta) < 1e-12) {
        // Near resonance (omega*TD ~ n*pi): TL is approximately a short
        // circuit.  Use a large admittance to model this.
        double sign = (cos_theta > 0) ? 1.0 : -1.0;
        double big = g0_ * 1e12;
        y11_im = -sign * big;
        y12_im =  sign * big;
    } else {
        // Y11 = -j * G0 * cos(theta) / sin(theta)
        // Y12 =  j * G0 / sin(theta)
        y11_im = -g0_ * cos_theta / sin_theta;
        y12_im =  g0_ / sin_theta;
    }

    // Y11 and Y12 are purely imaginary: Y11 = j*y11_im, Y12 = j*y12_im
    // Stamp into ax as imaginary parts (ax[2*off+1])
    auto stamp_im = [&](MatrixOffset off, double im) {
        if (off >= 0) {
            ax[2 * off + 1] += im;
        }
    };

    // Self-admittance Y11 at port 1 (between p1p and p1n)
    stamp_im(off_p1pp_,  y11_im);
    stamp_im(off_p1pn_, -y11_im);
    stamp_im(off_p1np_, -y11_im);
    stamp_im(off_p1nn_,  y11_im);

    // Self-admittance Y22 at port 2 (between p2p and p2n)
    stamp_im(off_p2pp_,  y11_im);
    stamp_im(off_p2pn_, -y11_im);
    stamp_im(off_p2np_, -y11_im);
    stamp_im(off_p2nn_,  y11_im);

    // Cross-admittance Y12: port 1 ← port 2
    stamp_im(off_p1p_p2p_,  y12_im);
    stamp_im(off_p1p_p2n_, -y12_im);
    stamp_im(off_p1n_p2p_, -y12_im);
    stamp_im(off_p1n_p2n_,  y12_im);

    // Cross-admittance Y21: port 2 ← port 1 (symmetric)
    stamp_im(off_p2p_p1p_,  y12_im);
    stamp_im(off_p2p_p1n_, -y12_im);
    stamp_im(off_p2n_p1p_, -y12_im);
    stamp_im(off_p2n_p1n_,  y12_im);

    return true;
}

// ---------------------------------------------------------------------------
// accept_step
// ---------------------------------------------------------------------------

void TransmissionLine::accept_step(double time,
                                   const std::vector<double>& solution) {
    // Compute port voltages
    double vp1p = (p1p_ >= 0) ? solution[p1p_] : 0.0;
    double vp1n = (p1n_ >= 0) ? solution[p1n_] : 0.0;
    double vp2p = (p2p_ >= 0) ? solution[p2p_] : 0.0;
    double vp2n = (p2n_ >= 0) ? solution[p2n_] : 0.0;

    double v1 = vp1p - vp1n;
    double v2 = vp2p - vp2n;

    // Port currents: I = G0*V - e*G0
    // At the just-accepted timestep the stored e values are what was used
    // during that Newton solve, i.e., the delayed values from (t-TD).
    // Current into port 1 from the external circuit: I1 = G0*(V1 - e1/G0... wait
    // The companion model stamps G0 shunt and current source e*G0 into the RHS.
    // KCL at p1p: sum of currents = 0.
    // The current flowing INTO the device (into p1p) = G0*(Vp1p - Vp1n) - e1*G0
    //   = G0*v1 - e1*g0
    // But we want I1 as defined by the physics: the current entering the port.
    // In the Norton model: I1 = G0*V1 - I_src = G0*v1 - e1*g0
    double i1 = g0_ * v1 - e1_ * g0_;
    double i2 = g0_ * v2 - e2_ * g0_;

    HistoryPoint hp;
    hp.time = time;
    hp.v1 = v1;
    hp.i1 = i1;
    hp.v2 = v2;
    hp.i2 = i2;
    history_.push_back(hp);

    // Trim history older than TD + a small margin (keep a few extra points for
    // interpolation across large time jumps).
    if (!history_.empty()) {
        double t_keep = time - td_ - 2.0 * td_;   // keep 3 * TD of history
        if (t_keep > 0.0) {
            // Remove entries older than t_keep but keep at least 2 points.
            while (history_.size() > 2 && history_.front().time < t_keep) {
                history_.erase(history_.begin());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// init_dc_state
// ---------------------------------------------------------------------------

void TransmissionLine::init_dc_state(const std::vector<double>& sol) {
    double vp1p = (p1p_ >= 0) ? sol[p1p_] : 0.0;
    double vp1n = (p1n_ >= 0) ? sol[p1n_] : 0.0;
    double vp2p = (p2p_ >= 0) ? sol[p2p_] : 0.0;
    double vp2n = (p2n_ >= 0) ? sol[p2n_] : 0.0;
    double v1 = vp1p - vp1n;
    double v2 = vp2p - vp2n;
    // At DC steady-state with short-circuit model, i1 ~ 0, i2 ~ 0 through TL
    // (current flows through the external R network, not "through" the TL)
    // Seed history with these DC values
    history_.clear();
    for (int k = 2; k >= 0; --k) {
        HistoryPoint hp;
        hp.time = -static_cast<double>(k) * td_;
        hp.v1 = v1; hp.i1 = 0.0;
        hp.v2 = v2; hp.i2 = 0.0;
        history_.push_back(hp);
    }
}

// ---------------------------------------------------------------------------
// set_transient
// ---------------------------------------------------------------------------

void TransmissionLine::set_transient(bool enable) {
    transient_ = enable;
    if (!enable) {
        history_.clear();
        e1_ = 0.0;
        e2_ = 0.0;
    }
}

} // namespace neospice
