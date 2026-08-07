// SPDX-License-Identifier: BSD-3-Clause
// See NOTICE and CREDITS.md for full attribution.
//
// ni_integrate.hpp — shared NIintegrate for the translated device shims.
//
// ngspice's NIintegrate (src/maths/ni/niinteg.c) forms the companion model for
// one charge state: it writes the branch current into state0[qcap+1] and
// returns the conductance/equivalent-current pair the device stamps.
//
// Every *_shim.cpp used to carry its own byte-identical copy of this, so a
// single correction to the trapezoidal formula had to be applied nineteen
// times.  Each shim declares its own `Ckt` inside its own namespace, so the
// shared form is a template rather than a plain function; each shim's
// NIintegrate is now a one-line forwarder.
//
// The arithmetic below is load-bearing for ngspice parity: `ag[0]*(q0 - q1)`
// and `s0[1] - ag[0]*s0[0]` are written in ngspice's operation order and must
// not be algebraically rearranged.
#pragma once

namespace neospice::shim {

template <typename Ckt>
inline int ni_integrate(Ckt *ckt, double *geq, double *ceq,
                        double cap, int qcap) {
    double *s0 = ckt->CKTstate0 + qcap;
    double *s1 = ckt->CKTstate1 + qcap;

    int order = ckt->CKTorder;
    if (order < 1) order = 1;
    if (order > 2) order = 2;

    double deriv;
    if (ckt->CKTintegrateMethod == 0 && order == 2) {
        // Trapezoidal order 2: NIcomCof stores the previous-current
        // multiplier in ag[1]; the charge difference is formed before
        // scaling, matching ngspice.
        deriv = -s1[1] * ckt->CKTag[1]
              + ckt->CKTag[0] * (s0[0] - s1[0]);
    } else {
        // Backward Euler (order 1) or Gear-2: pure coefficient sum.
        deriv = ckt->CKTag[0] * s0[0];
        if (order >= 1) deriv += ckt->CKTag[1] * s1[0];
        if (order >= 2) deriv += ckt->CKTag[2] * (ckt->CKTstate2 + qcap)[0];
    }
    s0[1] = deriv;

    *geq = ckt->CKTag[0] * cap;
    *ceq = s0[1] - ckt->CKTag[0] * s0[0];

    return 0;  // OK
}

} // namespace neospice::shim
