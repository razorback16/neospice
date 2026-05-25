#pragma once
#include "devices/device.hpp"
#include <utility>

namespace neospice {

// ---------------------------------------------------------------------------
// NonlinearVCVS — Polynomial VCVS (E element, POLY form)
//
// Netlist syntax: E1 out 0 POLY(N) cp1 cn1 [cp2 cn2 ...] c0 c1 c2 ...
//
// The output voltage V(np) - V(nn) = f(V1, V2, ...) where:
//   f is a polynomial in the control voltages Vk = V(cpk) - V(cnk).
//
// POLY(1): f = c0 + c1*V1 + c2*V1^2 + c3*V1^3 + ...
// POLY(2): f = c0 + c1*V1 + c2*V2 + c3*V1^2 + c4*V1*V2 + c5*V2^2 + ...
//   (standard SPICE multi-variate polynomial expansion)
//
// Newton linearization: branch equation V(np) - V(nn) - f(Vc) = 0
//   mat[branch, np]       += 1
//   mat[branch, nn]       -= 1
//   mat[branch, ctrl_pos] -= df/dVk  (per control pair k)
//   mat[branch, ctrl_neg] += df/dVk
//   rhs[branch]           += -(V(np) - V(nn) - f(Vc))  (residual)
// ---------------------------------------------------------------------------

struct CtrlPair {
    int32_t pos;  // GROUND_INTERNAL = -1
    int32_t neg;  // GROUND_INTERNAL = -1
};

class NonlinearVCVS : public Device {
public:
    NonlinearVCVS(std::string name,
                  int32_t node_pos, int32_t node_neg,
                  std::vector<CtrlPair> ctrl_pairs,
                  std::vector<double> coefficients);

    std::string device_type() const override { return "E"; }

    void set_branch_index(int32_t idx);
    int32_t branch_index() const override { return branch_idx_; }

    int32_t extra_vars() const override { return 1; }
    void assign_branch_index(int32_t& next) override {
        set_branch_index(next); next += extra_vars();
    }
    std::vector<std::string> output_currents() const override;

    std::vector<int32_t> external_nodes() const override {
        std::vector<int32_t> nodes = {np_, nn_};
        for (auto& cp : ctrl_pairs_) { nodes.push_back(cp.pos); nodes.push_back(cp.neg); }
        return nodes;
    }

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

private:
    // Evaluate polynomial value and partial derivatives at given control voltages.
    // derivs[k] = df/dVk  (Vk = V(ctrl_pairs_[k].pos) - V(ctrl_pairs_[k].neg))
    double eval_poly(const std::vector<double>& ctrl_v,
                     std::vector<double>& derivs) const;

    int32_t np_;
    int32_t nn_;
    std::vector<CtrlPair> ctrl_pairs_;
    std::vector<double>   coeffs_;
    int32_t branch_idx_ = -1;

    // Cached matrix offsets
    MatrixOffset off_np_branch_ = -1;   // (np, branch)
    MatrixOffset off_nn_branch_ = -1;   // (nn, branch)
    MatrixOffset off_branch_np_ = -1;   // (branch, np)
    MatrixOffset off_branch_nn_ = -1;   // (branch, nn)

    // Per control pair: (branch, ctrl_pos), (branch, ctrl_neg)
    std::vector<MatrixOffset> off_branch_cp_;
    std::vector<MatrixOffset> off_branch_cn_;
};

// ---------------------------------------------------------------------------
// TableVCVS — Piecewise-linear table VCVS (E element, TABLE form)
//
// Netlist syntax: E1 out 0 TABLE {V(in)} = (x1,y1) (x2,y2) ...
//
// The output voltage V(np) - V(nn) = interp(V(ctrl_pos) - V(ctrl_neg))
// where interp is a piecewise-linear function defined by the table points.
// Values outside the table range are clamped to the endpoint values.
//
// Newton linearization: branch equation V(np) - V(nn) - f(Vc) = 0
//   slope = df/dVc at current operating point (from interpolation segment)
//   mat[branch, np]       += 1
//   mat[branch, nn]       -= 1
//   mat[branch, ctrl_pos] -= slope
//   mat[branch, ctrl_neg] += slope
//   rhs[branch]           += -(V(np) - V(nn) - f(Vc))
// ---------------------------------------------------------------------------

struct TablePoint {
    double x;
    double y;
};

class TableVCVS : public Device {
public:
    TableVCVS(std::string name,
              int32_t node_pos, int32_t node_neg,
              int32_t ctrl_pos, int32_t ctrl_neg,
              std::vector<TablePoint> table_points);

    std::string device_type() const override { return "E"; }

    void set_branch_index(int32_t idx);
    int32_t branch_index() const override { return branch_idx_; }

    int32_t extra_vars() const override { return 1; }
    void assign_branch_index(int32_t& next) override {
        set_branch_index(next); next += extra_vars();
    }
    std::vector<std::string> output_currents() const override;

    std::vector<int32_t> external_nodes() const override { return {np_, nn_, ncp_, ncn_}; }

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

private:
    // Returns the interpolated y value and the slope at x.
    double interp(double x, double& slope) const;

    int32_t np_;
    int32_t nn_;
    int32_t ncp_;
    int32_t ncn_;
    std::vector<TablePoint> table_;
    int32_t branch_idx_ = -1;

    MatrixOffset off_np_branch_  = -1;
    MatrixOffset off_nn_branch_  = -1;
    MatrixOffset off_branch_np_  = -1;
    MatrixOffset off_branch_nn_  = -1;
    MatrixOffset off_branch_ncp_ = -1;
    MatrixOffset off_branch_ncn_ = -1;
};

} // namespace neospice
