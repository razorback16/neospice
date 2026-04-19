#pragma once
#include "devices/device.hpp"

namespace neospice {

// ---------------------------------------------------------------------------
// VCCS — Voltage-Controlled Current Source (G element)
//
// Netlist syntax: Gout np nn nc+ nc- gm
//
// The output current I_out = gm * (V(nc+) - V(nc-)) flows from np to nn
// through the external circuit.  No branch variable is needed — the
// transconductance is stamped directly into 4 matrix positions.
//
// MNA stamps (KCL contributions):
//   At np (current leaves): mat[np,  ncp] += -gm
//                           mat[np,  ncn] +=  gm
//   At nn (current enters): mat[nn,  ncp] +=  gm
//                           mat[nn,  ncn] += -gm
//
// No RHS contribution.  extra_vars() returns 0 (no branch variable).
// ---------------------------------------------------------------------------

class VCCS : public Device {
public:
    VCCS(std::string name,
         int32_t node_pos, int32_t node_neg,
         int32_t node_ctrl_pos, int32_t node_ctrl_neg,
         double gm);

    // Device interface — no extra variable needed
    int32_t extra_vars() const override { return 0; }

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

private:
    int32_t np_;   // output positive node  (GROUND_INTERNAL = -1)
    int32_t nn_;   // output negative node  (GROUND_INTERNAL = -1)
    int32_t ncp_;  // control positive node (GROUND_INTERNAL = -1)
    int32_t ncn_;  // control negative node (GROUND_INTERNAL = -1)
    double  gm_;

    // Cached matrix offsets for the 4 stamp positions
    MatrixOffset off_np_ncp_  = -1;  // (np,  ncp)
    MatrixOffset off_np_ncn_  = -1;  // (np,  ncn)
    MatrixOffset off_nn_ncp_  = -1;  // (nn,  ncp)
    MatrixOffset off_nn_ncn_  = -1;  // (nn,  ncn)
};

} // namespace neospice
