#pragma once
#include "devices/device.hpp"

namespace neospice {

// ---------------------------------------------------------------------------
// VCVS — Voltage-Controlled Voltage Source (E element)
//
// Netlist syntax: Eout np nn nc+ nc- gain
//
// The output voltage V(np) - V(nn) = gain * (V(nc+) - V(nc-)).
// Adds one branch variable I_branch (the output current) to the MNA system.
//
// MNA stamps:
//   KCL at np:       mat[np,   branch] += +1
//   KCL at nn:       mat[nn,   branch] += -1
//   Branch equation: mat[branch, np]   += +1
//                    mat[branch, nn]   += -1
//                    mat[branch, ncp]  += -gain
//                    mat[branch, ncn]  += +gain
//   RHS:             rhs[branch]       =  0  (no independent voltage)
// ---------------------------------------------------------------------------

class VCVS : public Device {
public:
    VCVS(std::string name,
         int32_t node_pos, int32_t node_neg,
         int32_t node_ctrl_pos, int32_t node_ctrl_neg,
         double gain);

    /// Assign the MNA branch variable index.
    void set_branch_index(int32_t idx);
    int32_t branch_index() const override { return branch_idx_; }

    // Device interface
    int32_t extra_vars() const override { return 1; }
    void assign_branch_index(int32_t& next) override {
        set_branch_index(next); next += extra_vars();
    }
    std::vector<std::string> output_currents() const override;

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

private:
    int32_t np_;          // output positive node  (GROUND_INTERNAL = -1)
    int32_t nn_;          // output negative node  (GROUND_INTERNAL = -1)
    int32_t ncp_;         // control positive node (GROUND_INTERNAL = -1)
    int32_t ncn_;         // control negative node (GROUND_INTERNAL = -1)
    double  gain_;
    int32_t branch_idx_ = -1;

    // Cached matrix offsets
    MatrixOffset off_np_branch_   = -1;  // (np, branch)
    MatrixOffset off_nn_branch_   = -1;  // (nn, branch)
    MatrixOffset off_branch_np_   = -1;  // (branch, np)
    MatrixOffset off_branch_nn_   = -1;  // (branch, nn)
    MatrixOffset off_branch_ncp_  = -1;  // (branch, ncp)
    MatrixOffset off_branch_ncn_  = -1;  // (branch, ncn)
};

} // namespace neospice
