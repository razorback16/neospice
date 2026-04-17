#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include <cstdarg>

namespace neospice::bsim4v7::Shim {

neospice::MatrixOffset Matrix::make_elt(int row, int col) {
    if (row < 0 || col < 0) {
        journal_.emplace_back(-1, -1);
        return -1;
    }
    builder_.add(row, col);
    neospice::MatrixOffset id = static_cast<neospice::MatrixOffset>(journal_.size());
    journal_.emplace_back(row, col);
    return id;
}

std::vector<neospice::MatrixOffset>
Matrix::resolve_offsets(const neospice::SparsityPattern &pat) const {
    std::vector<neospice::MatrixOffset> out;
    out.reserve(journal_.size());
    for (auto &[r, c] : journal_) {
        if (r < 0 || c < 0) out.push_back(-1);
        else out.push_back(pat.offset(r, c));
    }
    return out;
}

int Ckt::add_internal_node(const char * /*name*/) {
    return CKTinternalNodeCounter++;
}

// Implicit-integrator port — see header for rationale.
//
// Formula mirrors ngspice src/ckt/niintegr.c::NIintegrate (GEAR branch):
//
//   state0[ccap] = sum_{i=0..CKTorder} CKTag[i] * state[i][qcap]
//   *geq         = CKTag[0] * cap
//   *ceq         = state0[ccap] - *geq * state0[qcap]
//
// The UCB "states" array is indexed by integration history: states[0] is
// the current step, states[1] is t - h, states[2] is t - 2h, ... up to
// CKTorder.  We only carry three generations (state0/1/2) because the
// timestep controller caps order at 2 (BE + Gear2).  Any CKTorder > 2
// would be a caller bug — we clamp silently.
//
// BSIM4's call sites always pass cap=0.0; the load routine multiplies the
// analytic capacitance in by hand after it reads ceq.  Keeping cap as a
// parameter matches the UCB signature and lets future (non-BSIM4) callers
// hand us a linear capacitance and get a pre-stamped geq back.
int NIintegrate(Ckt *ckt, double *geq, double *ceq,
                double cap, int qcap) {
    const int ccap = qcap + 1;
    double *s0 = ckt->CKTstate0 + qcap;
    double *s1 = ckt->CKTstate1 + qcap;
    double *s2 = ckt->CKTstate2 + qcap;

    // Gear summation.  The i-th term draws from states[i] which maps to
    // CKTstate0 (i=0), CKTstate1 (i=1), CKTstate2 (i=2).  CKTorder is the
    // *highest* index consumed; clamp to the three we actually store.
    int order = ckt->CKTorder;
    if (order < 1) order = 1;
    if (order > 2) order = 2;

    double deriv = ckt->CKTag[0] * s0[0];
    if (order >= 1) deriv += ckt->CKTag[1] * s1[0];
    if (order >= 2) deriv += ckt->CKTag[2] * s2[0];
    s0[1] = deriv;   // state0[ccap] = numerical current

    *geq = ckt->CKTag[0] * cap;
    *ceq = s0[1] - (*geq) * s0[0];

    // Also mirror the result into CKTstate0[ccap] (already done above via
    // s0[1] = deriv).  We don't touch state1/state2 — the Circuit's state
    // ring rotation handles history on step acceptance.
    return OK;
}

void report_error(int /*level*/, const char *fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

} // namespace neospice::bsim4v7::Shim
