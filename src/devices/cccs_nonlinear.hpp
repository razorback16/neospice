#pragma once
#include "devices/device.hpp"
#include "devices/vsource.hpp"

namespace neospice {

// ---------------------------------------------------------------------------
// NonlinearCCCS — Polynomial CCCS (F element, POLY form)
//
// Netlist syntax: F1 np nn POLY(N) Vs1 [Vs2 ...] c0 c1 c2 ...
//
// The output current I = f(I1, I2, ...) flows from np to external circuit.
// No branch variable needed — Newton companion is stamped directly.
//
// Newton linearization:
//   I = f(Ic) ~= f(Ic0) + sum_k(df/dIk * (Ik - Ik0))
//
//   Jacobian stamps (per sensing VSource k):
//     mat[np, sense_branch_k] += df/dIk
//     mat[nn, sense_branch_k] -= df/dIk
//
//   RHS:
//     rhs[np] += -(f(Ic0) - sum_k(df/dIk * Ik0))
//     rhs[nn] +=  (f(Ic0) - sum_k(df/dIk * Ik0))
// ---------------------------------------------------------------------------

class NonlinearCCCS : public Device {
public:
    NonlinearCCCS(std::string name,
                  int32_t node_pos, int32_t node_neg,
                  std::vector<const VSource*> vsenses,
                  std::vector<double> coefficients);

    std::string device_type() const override { return "F"; }
    int32_t extra_vars() const override { return 0; }

    std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

private:
    double eval_poly(const std::vector<double>& ctrl_i,
                     std::vector<double>& derivs) const;

    int32_t np_;
    int32_t nn_;
    std::vector<const VSource*> vsenses_;
    std::vector<double>   coeffs_;

    // Per sensing VSource: (np, sense_branch_k), (nn, sense_branch_k)
    std::vector<MatrixOffset> off_np_sense_;
    std::vector<MatrixOffset> off_nn_sense_;
};

} // namespace neospice
