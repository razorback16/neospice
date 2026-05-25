#pragma once
#include "devices/device.hpp"
#include "devices/vcvs_nonlinear.hpp"  // for CtrlPair and TablePoint
#include "devices/asrc/expression_ast.hpp"

namespace neospice {

class VSource;  // forward declaration for I() refs

// ---------------------------------------------------------------------------
// NonlinearVCCS — Polynomial VCCS (G element, POLY form)
//
// Netlist syntax: G1 out 0 POLY(N) cp1 cn1 [cp2 cn2 ...] c0 c1 c2 ...
//
// The output current I = f(V1, V2, ...) flows from np to external circuit.
// No branch variable needed — Newton companion is stamped directly.
//
// Newton linearization:
//   I = f(Vc) ≈ f(Vc0) + sum_k(df/dVk * (Vk - Vk0))
//
//   KCL at np (current leaves): contribution is -I
//   KCL at nn (current enters): contribution is +I
//
//   Jacobian stamps (per control pair k):
//     mat[np, cpk] += -df/dVk
//     mat[np, cnk] +=  df/dVk
//     mat[nn, cpk] +=  df/dVk
//     mat[nn, cnk] += -df/dVk
//
//   RHS:
//     rhs[np] += -(I - sum_k(df/dVk * Vk))  = -f(Vc) + sum_k(df/dVk * Vk)
//     rhs[nn] +=  (I - sum_k(df/dVk * Vk))  =  f(Vc) - sum_k(df/dVk * Vk)
// ---------------------------------------------------------------------------

class NonlinearVCCS : public Device {
public:
    NonlinearVCCS(std::string name,
                  int32_t node_pos, int32_t node_neg,
                  std::vector<CtrlPair> ctrl_pairs,
                  std::vector<double> coefficients);

    std::string device_type() const override { return "G"; }
    int32_t extra_vars() const override { return 0; }

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
    double eval_poly(const std::vector<double>& ctrl_v,
                     std::vector<double>& derivs) const;

    int32_t np_;
    int32_t nn_;
    std::vector<CtrlPair> ctrl_pairs_;
    std::vector<double>   coeffs_;

    // Per control pair: (np, cpk), (np, cnk), (nn, cpk), (nn, cnk)
    std::vector<MatrixOffset> off_np_cp_;
    std::vector<MatrixOffset> off_np_cn_;
    std::vector<MatrixOffset> off_nn_cp_;
    std::vector<MatrixOffset> off_nn_cn_;
};

// ---------------------------------------------------------------------------
// TableVCCS — Piecewise-linear table VCCS (G element, TABLE form)
//
// Simple form:  G1 out 0 TABLE {V(in)}       = (x1,y1) (x2,y2) ...
// Expr form:    G1 out 0 TABLE {V(in)*scale}  = (x1,y1) (x2,y2) ...
//
// Output current I = interp(ctrl_value) where ctrl_value is either
// V(ctrl_pos)-V(ctrl_neg) or the result of evaluating an expression.
// Piecewise-linear interpolation with endpoint clamping.
// ---------------------------------------------------------------------------

class TableVCCS : public Device {
public:
    // Simple form: control is V(ctrl_pos) - V(ctrl_neg)
    TableVCCS(std::string name,
              int32_t node_pos, int32_t node_neg,
              int32_t ctrl_pos, int32_t ctrl_neg,
              std::vector<TablePoint> table_points);

    // Expression form: control is an arbitrary expression
    TableVCCS(std::string name,
              int32_t node_pos, int32_t node_neg,
              asrc::CompiledExpression expr,
              std::vector<int32_t> var_indices,
              std::vector<int32_t> var_indices2,
              std::vector<const VSource*> vsource_ptrs,
              std::vector<TablePoint> table_points);

    std::string device_type() const override { return "G"; }
    int32_t extra_vars() const override { return 0; }

    std::vector<int32_t> external_nodes() const override;

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

private:
    double interp(double x, double& slope) const;
    void fill_var_values(const std::vector<double>& voltages);
    int32_t var_circuit_index(int i) const;

    int32_t np_;
    int32_t nn_;

    // Simple mode: direct node control
    int32_t ncp_ = -1;
    int32_t ncn_ = -1;
    MatrixOffset off_np_ncp_  = -1;
    MatrixOffset off_np_ncn_  = -1;
    MatrixOffset off_nn_ncp_  = -1;
    MatrixOffset off_nn_ncn_  = -1;

    // Expression mode
    bool has_expr_ = false;
    asrc::CompiledExpression expr_;
    std::vector<int32_t> var_indices_;
    std::vector<int32_t> var_indices2_;
    std::vector<const VSource*> vsource_ptrs_;
    mutable std::vector<double> var_values_;
    mutable std::vector<double> derivs_;

    struct VarStamp {
        MatrixOffset off_np = -1;
        MatrixOffset off_nn = -1;
        MatrixOffset off_np2 = -1;
        MatrixOffset off_nn2 = -1;
    };
    std::vector<VarStamp> var_stamps_;

    std::vector<TablePoint> table_;
};

} // namespace neospice
