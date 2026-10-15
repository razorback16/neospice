#pragma once
#include "core/types.hpp"
#include <algorithm>
#include <cmath>

namespace neospice {

// Faithful reimplementation of ngspice's CKTterr (cktterr.c).
// Computes (order+1)-th divided difference of a charge state to estimate
// local truncation error, then proposes a timestep so LTE stays within
// tolerance.  Updates *timeStep = min(*timeStep, proposed).
//
// Parameters:
//   qcap       - state offset for the charge variable (ccap = qcap+1)
//   states[]   - array of state buffer pointers (states[0] = most recent)
//   ctx        - integrator context (order, delta, delta_old, integrate_method)
//   opts       - simulation options (abstol, reltol, chgtol, trtol)
//   timeStep   - in/out: caller passes current minimum; updated if tighter
inline void ckt_terr(int qcap, const double* const* states,
                     const IntegratorCtx& ctx, const SimOptions& opts,
                     double& timeStep) {
    const int ccap = qcap + 1;
    const int order = ctx.order;
    if (order < 1) return;

    double volttol = opts.abstol + opts.reltol *
        std::max(std::abs(states[0][ccap]), std::abs(states[1][ccap]));
    double chargetol = std::max(std::abs(states[0][qcap]), std::abs(states[1][qcap]));
    chargetol = opts.reltol * std::max(chargetol, opts.chgtol) / ctx.delta;
    double tol = std::max(volttol, chargetol);

    // Compute divided differences. Orders 1 and 2 (every trapezoidal step)
    // are written out in scalars with the same operations in the same order
    // as the loop below. At -O3, GCC vectorizes that loop into stack stores
    // followed by overlapping loads; the loads cannot be forwarded from the
    // stores and stall, which made this function ~2x slower than ngspice's.
    const double* const dold = ctx.delta_old;
    double diff[8];
    if (order == 1) {
        const double d0 = (states[0][qcap] - states[1][qcap]) / dold[0];
        const double d1 = (states[1][qcap] - states[2][qcap]) / dold[1];
        diff[0] = (d0 - d1) / (dold[1] + dold[0]);
    } else if (order == 2) {
        double d0 = (states[0][qcap] - states[1][qcap]) / dold[0];
        double d1 = (states[1][qcap] - states[2][qcap]) / dold[1];
        const double d2 = (states[2][qcap] - states[3][qcap]) / dold[2];
        const double t1 = dold[2] + dold[1];
        d0 = (d0 - d1) / (dold[1] + dold[0]);
        d1 = (d1 - d2) / t1;
        diff[0] = (d0 - d1) / (t1 + dold[0]);
    } else {
        double deltmp[8];
        for (int i = order + 1; i >= 0; --i)
            diff[i] = states[i][qcap];
        for (int i = 0; i <= order; ++i)
            deltmp[i] = dold[i];

        int j = order;
        for (;;) {
            for (int i = 0; i <= j; ++i)
                diff[i] = (diff[i] - diff[i + 1]) / deltmp[i];
            if (--j < 0) break;
            for (int i = 0; i <= j; ++i)
                deltmp[i] = deltmp[i + 1] + dold[i];
        }
    }

    // LTE coefficient
    static const double gearCoeff[] = {0.5, 0.2222222222, 0.1363636364, 0.096, 0.07299270073, 0.05830903790};
    static const double trapCoeff[] = {0.5, 0.08333333333};
    double factor = 0;
    if (ctx.integrate_method == 1)
        factor = gearCoeff[order - 1];
    else
        factor = trapCoeff[order - 1];

    double del = opts.trtol * tol / std::max(opts.abstol, factor * std::abs(diff[0]));
    if (order == 2)
        del = std::sqrt(del);
    else if (order > 2)
        del = std::exp(std::log(del) / order);

    timeStep = std::min(timeStep, del);
}

} // namespace neospice
