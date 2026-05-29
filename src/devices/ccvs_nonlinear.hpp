#pragma once
#include "devices/device.hpp"
#include "devices/vsource.hpp"

namespace neospice {

// ---------------------------------------------------------------------------
// NonlinearCCVS — Polynomial CCVS (H element, POLY form)
//
// Netlist syntax: H1 np nn POLY(N) Vs1 [Vs2 ...] c0 c1 c2 ...
//
// The output voltage V(np) - V(nn) = f(I1, I2, ...) where:
//   f is a polynomial in the sensing currents Ik = I(Vsk).
//
// POLY(1): f = c0 + c1*I1 + c2*I1^2 + c3*I1^3 + ...
// POLY(2): f = c0 + c1*I1 + c2*I2 + c3*I1^2 + c4*I1*I2 + c5*I2^2 + ...
//   (standard SPICE multi-variate polynomial expansion)
//
// Newton linearization: branch equation V(np) - V(nn) - f(I) = 0
//   mat[branch, np]           += 1
//   mat[branch, nn]           -= 1
//   mat[branch, sense_branch] -= df/dIk  (per sensing VSource k)
//   rhs[branch]               += f(I0) - sum_k(df/dIk * Ik0)
//
// KCL at output nodes:
//   mat[np, branch] += 1
//   mat[nn, branch] -= 1
// ---------------------------------------------------------------------------

class NonlinearCCVS : public Device {
public:
    bool is_nonlinear() const override { return true; }
    NonlinearCCVS(std::string name,
                  int32_t node_pos, int32_t node_neg,
                  std::vector<const VSource*> vsenses,
                  std::vector<double> coefficients);

    void set_branch_index(int32_t idx);
    int32_t branch_index() const override { return branch_idx_; }

    std::string device_type() const override { return "H"; }
    int32_t extra_vars() const override { return 1; }
    void assign_branch_index(int32_t& next) override {
        set_branch_index(next); next += extra_vars();
    }
    std::vector<std::string> output_currents() const override;

    std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

private:
    // Evaluate polynomial value and partial derivatives at given control currents.
    // derivs[k] = df/dIk
    double eval_poly(const std::vector<double>& ctrl_i,
                     std::vector<double>& derivs) const;

    int32_t np_;
    int32_t nn_;
    std::vector<const VSource*> vsenses_;
    std::vector<double>   coeffs_;
    int32_t branch_idx_ = -1;

    // Cached matrix offsets
    MatrixOffset off_np_branch_ = -1;   // (np, branch)
    MatrixOffset off_nn_branch_ = -1;   // (nn, branch)
    MatrixOffset off_branch_np_ = -1;   // (branch, np)
    MatrixOffset off_branch_nn_ = -1;   // (branch, nn)

    // Per sensing VSource: (branch, sense_branch_k)
    std::vector<MatrixOffset> off_branch_sense_;
};

} // namespace neospice
