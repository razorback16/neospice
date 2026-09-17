#include "devices/tline.hpp"
#include <span>
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
      z0_(z0), td_(td)
{
    assert(z0 > 0.0 && "Z0 must be positive");
    assert(td >= 0.0 && "TD must be non-negative");
}

// ---------------------------------------------------------------------------
// stamp_pattern / assign_offsets
// ---------------------------------------------------------------------------

void TransmissionLine::assign_branch_index(int32_t& next) {
    br1_ = next++;
    br2_ = next++;
}

void TransmissionLine::stamp_pattern(SparsityBuilder& builder) const {
    const auto vars = variables();
    for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 6; ++col)
            if (row >= 4 || (row < 2 ? col == 4 : col == 5))
                stamp_if_not_ground(builder, vars[row], vars[col]);
}

void TransmissionLine::assign_offsets(const SparsityPattern& pattern) {
    const auto vars = variables();
    for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 6; ++col)
            offsets_[row][col] = (row >= 4 || (row < 2 ? col == 4 : col == 5))
                ? offset_if_not_ground(pattern, vars[row], vars[col]) : -1;
}

void TransmissionLine::stamp_port_equations(NumericMatrix& mat) const {
    // Port KCL and outgoing waves: V1 - Z0*I1 and V2 - Z0*I2.
    for (int port = 0; port < 2; ++port) {
        const int pos = 2 * port, neg = pos + 1, branch = 4 + port;
        add_if_valid(mat, offsets_[pos][branch], 1.0);
        add_if_valid(mat, offsets_[neg][branch], -1.0);
        add_if_valid(mat, offsets_[branch][pos], 1.0);
        add_if_valid(mat, offsets_[branch][neg], -1.0);
        add_if_valid(mat, offsets_[branch][branch], -z0_);
    }
}

// ---------------------------------------------------------------------------
// update_delayed_values
// ---------------------------------------------------------------------------

void TransmissionLine::update_delayed_values(double t_delayed) {
    if (history_.empty()) {
        e1_ = 0.0;
        e2_ = 0.0;
        return;
    }

    // When IC values seed history at negative times, allow interpolation there.
    if (t_delayed < history_.front().time) {
        // Extrapolate from earliest history point (constant extrapolation).
        const auto& h = history_.front();
        e1_ = h.v2 + z0_ * h.i2;
        e2_ = h.v1 + z0_ * h.i1;
        return;
    }

    // Find bounding history points for interpolation
    auto it = std::upper_bound(history_.begin(), history_.end(), t_delayed,
        [](double t, const HistoryPoint& hp) { return t < hp.time; });

    if (it == history_.begin()) {
        // t_delayed is before first point — use first point
        e1_ = 0.0; e2_ = 0.0;
        return;
    }

    // Quadratic (3-point Lagrange) if possible, else linear fallback
    const HistoryPoint* p0;
    const HistoryPoint* p1;
    const HistoryPoint* p2;

    bool use_quadratic = false;

    if (it == history_.end()) {
        // Extrapolate from last 3
        if (history_.size() >= 3) {
            p0 = &history_[history_.size()-3];
            p1 = &history_[history_.size()-2];
            p2 = &history_[history_.size()-1];
            use_quadratic = true;
        } else {
            // Not enough points, use last point
            const auto& h = history_.back();
            e1_ = h.v2 + z0_ * h.i2;
            e2_ = h.v1 + z0_ * h.i1;
            return;
        }
    } else if (it - history_.begin() >= 2) {
        // Normal case: it points past t_delayed, use (it-2, it-1, it)
        p0 = &*(it - 2);
        p1 = &*(it - 1);
        p2 = &*it;
        use_quadratic = true;
    } else if (it - history_.begin() == 1 && it + 1 != history_.end()) {
        // Only 1 point before it, grab one more after
        p0 = &*(it - 1);
        p1 = &*it;
        p2 = &*(it + 1);
        use_quadratic = true;
    } else {
        // Only 2 points total — linear fallback
        const auto& h1 = *(it - 1);
        const auto& h2 = *it;
        double denom = h2.time - h1.time;
        double alpha = (denom > 1e-300) ? (t_delayed - h1.time) / denom : 0.0;
        alpha = std::max(0.0, std::min(1.0, alpha));
        double v1_d = h1.v1 + alpha * (h2.v1 - h1.v1);
        double i1_d = h1.i1 + alpha * (h2.i1 - h1.i1);
        double v2_d = h1.v2 + alpha * (h2.v2 - h1.v2);
        double i2_d = h1.i2 + alpha * (h2.i2 - h1.i2);
        e1_ = v2_d + z0_ * i2_d;
        e2_ = v1_d + z0_ * i1_d;
        return;
    }

    if (use_quadratic) {
        // 3-point Lagrange interpolation
        double t0 = p0->time, t1 = p1->time, t2 = p2->time;
        double t = t_delayed;
        double f0 = ((t-t1)*(t-t2)) / ((t0-t1)*(t0-t2));
        double f1 = ((t-t0)*(t-t2)) / ((t1-t0)*(t1-t2));
        double f2 = ((t-t0)*(t-t1)) / ((t2-t0)*(t2-t1));

        double v1_d = f0*p0->v1 + f1*p1->v1 + f2*p2->v1;
        double i1_d = f0*p0->i1 + f1*p1->i1 + f2*p2->i1;
        double v2_d = f0*p0->v2 + f1*p1->v2 + f2*p2->v2;
        double i2_d = f0*p0->i2 + f1*p1->i2 + f2*p2->i2;

        e1_ = v2_d + z0_ * i2_d;
        e2_ = v1_d + z0_ * i1_d;
    }
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------

void TransmissionLine::evaluate(const std::vector<double>& /*voltages*/,
                                NumericMatrix& mat, std::span<double> rhs) {
    stamp_port_equations(mat);
    if (!transient_ || td_ == 0.0) {
        // At DC (or zero delay), the wave equations imply V1=V2 and I1=-I2.
        // No large-conductance approximation or common-mode connection is needed.
        for (int port = 0; port < 2; ++port) {
            const int branch = 4 + port, other = 1 - port;
            add_if_valid(mat, offsets_[branch][2 * other], -1.0);
            add_if_valid(mat, offsets_[branch][2 * other + 1], 1.0);
            add_if_valid(mat, offsets_[branch][4 + other], -z0_);
        }
        e1_ = e2_ = 0.0;
        return;
    }
    if (tls_integrator_ctx)
        update_delayed_values(tls_integrator_ctx->current_time - td_);
    add_rhs_if_valid(rhs, br1_, e1_);
    add_rhs_if_valid(rhs, br2_, e2_);
}

void TransmissionLine::ac_stamp(const std::vector<double>& /*voltages*/,
                                NumericMatrix& G, NumericMatrix& /*C*/) {
    stamp_port_equations(G);
}

bool TransmissionLine::ac_stamp_freq(double omega,
                                      std::vector<double>& ax, int32_t /*nnz*/,
                                      std::vector<std::complex<double>>& /*ac_rhs*/) {
    // Eliminate TRA's two internal voltage nodes from ngspice traacld.c:
    // V1-Z0*I1 = exp(-j*w*TD)*(V2+Z0*I2), and the symmetric equation.
    // Unlike cot/csc Y parameters, these equations have no resonance poles.
    const double phase = -omega * td_;
    const std::complex<double> delay{std::cos(phase), std::sin(phase)};
    const auto stamp = [&](int row, int col, std::complex<double> value) {
        const auto off = offsets_[row][col];
        if (off >= 0) {
            ax[2 * off] += value.real();
            ax[2 * off + 1] += value.imag();
        }
    };
    for (int port = 0; port < 2; ++port) {
        const int branch = 4 + port, other = 1 - port;
        stamp(branch, 2 * other, -delay);
        stamp(branch, 2 * other + 1, delay);
        stamp(branch, 4 + other, -z0_ * delay);
    }
    return true;
}

// ---------------------------------------------------------------------------
// accept_step
// ---------------------------------------------------------------------------

TransmissionLine::HistoryPoint TransmissionLine::sample(
        double time, const std::vector<double>& solution) const {
    const auto voltage = [&](int32_t index) { return index >= 0 ? solution[index] : 0.0; };
    return {time, voltage(p1p_) - voltage(p1n_), solution[br1_],
                  voltage(p2p_) - voltage(p2n_), solution[br2_]};
}

bool TransmissionLine::wave_slope_changed(const HistoryPoint& latest,
        const HistoryPoint& previous, const HistoryPoint& older,
        double dt, double previous_dt) const {
    if (dt <= 0.0 || previous_dt <= 0.0) return false;
    const auto changed = [&](double v, double p, double q) {
        const double d1 = (v - p) / dt;
        const double d2 = (p - q) / previous_dt;
        // ngspice TRA's instance defaults are reltol=1 and abstol=1.
        return std::abs(d1 - d2) >= std::max(std::abs(d1), std::abs(d2)) + 1.0;
    };
    return changed(latest.v1 + z0_ * latest.i1,
                   previous.v1 + z0_ * previous.i1, older.v1 + z0_ * older.i1) ||
           changed(latest.v2 + z0_ * latest.i2,
                   previous.v2 + z0_ * previous.i2, older.v2 + z0_ * older.i2);
}

std::optional<double> TransmissionLine::accept_step(double time,
        const std::vector<double>& solution, const IntegratorCtx& ctx,
        double min_break) {
    if (td_ == 0.0 || history_.size() < 3) return std::nullopt;
    // TRAaccept retains two points preceding the delayed interpolation bracket.
    size_t i = 2;
    while (i + 1 < history_.size() && time - td_ > history_[i].time) ++i;
    if (i > 2) history_.erase(history_.begin(), history_.begin() + (i - 2));
    if (time - history_.back().time <= min_break) return std::nullopt;
    const auto latest = sample(time, solution);
    const auto& previous = history_.back();
    const auto& older = history_[history_.size() - 2];
    std::optional<double> breakpoint;
    if (wave_slope_changed(latest, previous, older,
                           ctx.delta_old[0], ctx.delta_old[1]))
        breakpoint = previous.time + td_;
    history_.push_back(latest);
    return breakpoint;
}

double TransmissionLine::trunc_timestep(const IntegratorCtx& ctx,
        const std::vector<double>& solution) const {
    if (!transient_ || td_ == 0.0 || history_.size() < 2) return 1e30;
    // Match TRAtrunc's historical step denominators. This pre-acceptance
    // check prevents advancing past a delayed corner before TRAaccept can
    // insert its breakpoint into the driver's queue.
    const auto& previous = history_.back();
    if (wave_slope_changed(sample(ctx.current_time, solution), previous,
                           history_[history_.size() - 2],
                           ctx.delta_old[1], ctx.delta_old[2]))
        return previous.time + td_ - ctx.current_time;
    return 1e30;
}

// ---------------------------------------------------------------------------
// init_dc_state
// ---------------------------------------------------------------------------

void TransmissionLine::set_ic(double v1, double i1, double v2, double i2) {
    has_ic_ = true;
    ic_v1_ = v1; ic_i1_ = i1;
    ic_v2_ = v2; ic_i2_ = i2;
}

void TransmissionLine::init_dc_state(const std::vector<double>& sol, bool uic) {
    double v1, i1, v2, i2;
    // ngspice TRAload uses instance initial conditions only under MODEUIC.
    if (uic) {
        v1 = ic_v1_; i1 = ic_i1_;
        v2 = ic_v2_; i2 = ic_i2_;
    } else {
        double vp1p = (p1p_ >= 0) ? sol[p1p_] : 0.0;
        double vp1n = (p1n_ >= 0) ? sol[p1n_] : 0.0;
        double vp2p = (p2p_ >= 0) ? sol[p2p_] : 0.0;
        double vp2n = (p2n_ >= 0) ? sol[p2n_] : 0.0;
        v1 = vp1p - vp1n;
        v2 = vp2p - vp2n;
        i1 = sol[br1_];
        i2 = sol[br2_];
    }
    history_.clear();
    for (int k = 2; k >= 0; --k) {
        HistoryPoint hp;
        hp.time = -static_cast<double>(k) * td_;
        hp.v1 = v1; hp.i1 = i1;
        hp.v2 = v2; hp.i2 = i2;
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
