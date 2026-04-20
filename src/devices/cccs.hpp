#pragma once
#include "devices/device.hpp"
#include "devices/vsource.hpp"

namespace neospice {

// ---------------------------------------------------------------------------
// CCCS — Current-Controlled Current Source (F element)
//
// Netlist syntax: Fout np nn Vsense gain
//
// The output current I_out = gain * I(Vsense), where I(Vsense) is the branch
// current through the named VSource (acting as a current sensor).
//
// No branch variable is needed — the current gain is stamped directly into
// 2 matrix positions (like VCCS, but against the sense branch column instead
// of control node voltage columns).
//
// MNA stamps (KCL contributions):
//   At np (current leaves): mat[np, sense_branch] += -gain
//   At nn (current enters): mat[nn, sense_branch] += +gain
//
// No RHS contribution.  extra_vars() returns 0 (no branch variable).
// ---------------------------------------------------------------------------

class CCCS : public Device {
public:
    /// vsense must remain valid for the lifetime of this CCCS (owned by Circuit).
    CCCS(std::string name,
         int32_t node_pos, int32_t node_neg,
         double gain,
         const VSource* vsense);

    // Device interface — no extra variable needed
    int32_t extra_vars() const override { return 0; }

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    void set_multiplier(double m) { m_ = m; }

private:
    int32_t        np_;      // output positive node  (GROUND_INTERNAL = -1)
    int32_t        nn_;      // output negative node  (GROUND_INTERNAL = -1)
    double         gain_;    // current gain (dimensionless)
    double         m_ = 1.0; // multiplier (m instances in parallel)
    const VSource* vsense_;  // sensing VSource (non-owning)

    // Cached matrix offsets for the 2 stamp positions
    MatrixOffset off_np_sense_ = -1;  // (np, sense_branch)
    MatrixOffset off_nn_sense_ = -1;  // (nn, sense_branch)
};

} // namespace neospice
