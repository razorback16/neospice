#pragma once
#include "devices/device.hpp"
#include "devices/vsource.hpp"

namespace neospice {

// ---------------------------------------------------------------------------
// CCVS — Current-Controlled Voltage Source (H element)
//
// Netlist syntax: Hout np nn Vsense transresistance
//
// The output voltage V(np) - V(nn) = R_m * I(Vsense), where I(Vsense) is
// the branch current through the named VSource (acting as a current sensor).
//
// Adds one branch variable I_branch (the output current) to the MNA system.
//
// MNA stamps:
//   KCL at np:       mat[np,     branch]       += +1
//   KCL at nn:       mat[nn,     branch]       += -1
//   Branch equation: mat[branch, np]           += +1
//                    mat[branch, nn]           += -1
//                    mat[branch, sense_branch] += -R_m
//   RHS:             rhs[branch]               =  0
// ---------------------------------------------------------------------------

class CCVS : public Device {
public:
    /// vsense must remain valid for the lifetime of this CCVS (owned by Circuit).
    CCVS(std::string name,
         int32_t node_pos, int32_t node_neg,
         double rm,
         const VSource* vsense);

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
    int32_t        np_;          // output positive node  (GROUND_INTERNAL = -1)
    int32_t        nn_;          // output negative node  (GROUND_INTERNAL = -1)
    double         rm_;          // transresistance (Ohms)
    const VSource* vsense_;      // sensing VSource (non-owning)
    int32_t        branch_idx_ = -1;

    // Cached matrix offsets
    MatrixOffset off_np_branch_     = -1;  // (np,     branch)
    MatrixOffset off_nn_branch_     = -1;  // (nn,     branch)
    MatrixOffset off_branch_np_     = -1;  // (branch, np)
    MatrixOffset off_branch_nn_     = -1;  // (branch, nn)
    MatrixOffset off_branch_sense_  = -1;  // (branch, sense_branch)
};

} // namespace neospice
