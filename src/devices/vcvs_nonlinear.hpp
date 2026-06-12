#pragma once
#include "devices/device.hpp"
#include "devices/asrc/expression_ast.hpp"
#include <utility>
#include <vector>

namespace neospice {

class VSource;  // forward declaration for I() refs

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
    bool is_nonlinear() const override { return true; }
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

// ---------------------------------------------------------------------------
// Smoothed piecewise-linear table lookup, matching ngspice's E/G TABLE.
//
// ngspice rewrites `E/G ... TABLE {expr} (x0,y0)...(xn,yn)` into an XSPICE
// `pwl` code model with `input_domain=0.1 fraction=TRUE limit=TRUE`
// (src/frontend/inpcom.c). That model:
//   1. pads the breakpoint array with one synthetic point at each end
//      (x extended by linear continuation; y held flat — limit=TRUE), and
//   2. rounds every corner with a parabola over ±input_domain (= 0.1 × the
//      smaller adjacent segment) via cm_smooth_corner (src/xspice/cm/cmutil.c).
// The net effect is that exactly at a breakpoint the output is the parabola's
// midpoint value, not the raw yi — e.g. at the first breakpoint of (0,0)(1,1)
// the output is 0.025·range, not 0. Plain saturating interpolation diverges
// from ngspice here; this routine reproduces it bit-for-bit.
//
// `tbl` must be sorted ascending by x with size >= 2.
inline double pwl_smooth_interp(const std::vector<TablePoint>& tbl,
                                double x, double& slope) {
    const int n = static_cast<int>(tbl.size());
    // Padded arrays: real points at indices 1..n, synthetic ends at 0 and n+1.
    const int m = n + 2;
    std::vector<double> X(m), Y(m);
    for (int i = 1; i <= n; ++i) { X[i] = tbl[i - 1].x; Y[i] = tbl[i - 1].y; }
    X[0]     = 2.0 * X[1]     - X[2];
    X[m - 1] = 2.0 * X[m - 2] - X[m - 3];
    // limit=TRUE: flat (constant) extrapolation of y beyond the table.
    Y[0]     = Y[1];
    Y[m - 1] = Y[m - 2];

    if (x <= (X[0] + X[1]) / 2.0) {
        slope = (Y[1] - Y[0]) / (X[1] - X[0]);
        return Y[0] + (x - X[0]) * slope;
    }
    if (x >= (X[m - 2] + X[m - 1]) / 2.0) {
        slope = (Y[m - 1] - Y[m - 2]) / (X[m - 1] - X[m - 2]);
        return Y[m - 1] + (x - X[m - 1]) * slope;
    }

    constexpr double input_domain = 0.1;  // ngspice default for the rewrite
    for (int i = 1; i <= m - 2; ++i) {
        if (x < (X[i] + X[i + 1]) / 2.0) {
            const double lower_seg = X[i] - X[i - 1];
            const double upper_seg = X[i + 1] - X[i];
            // fraction=TRUE: domain is a fraction of the smaller segment.
            const double dom = input_domain *
                (lower_seg <= upper_seg ? lower_seg : upper_seg);
            const double tl = X[i] - dom;
            const double tu = X[i] + dom;
            if (x < tl) {                       // lower linear region
                slope = (Y[i] - Y[i - 1]) / lower_seg;
                return Y[i] + (x - X[i]) * slope;
            } else if (x < tu) {                // parabolic corner (cm_smooth_corner)
                const double lower_slope = (Y[i] - Y[i - 1]) / lower_seg;
                const double upper_slope = (Y[i + 1] - Y[i]) / upper_seg;
                const double x_upper = X[i] + dom;
                const double y_upper = Y[i] + upper_slope * dom;
                const double a = ((upper_slope - lower_slope) / 4.0) * (1.0 / dom);
                const double b = upper_slope - 2.0 * a * x_upper;
                const double c = y_upper - a * x_upper * x_upper - b * x_upper;
                slope = 2.0 * a * x + b;
                return a * x * x + b * x + c;
            } else {                            // upper linear region
                slope = (Y[i + 1] - Y[i]) / upper_seg;
                return Y[i] + (x - X[i]) * slope;
            }
        }
    }
    // Unreachable given the midpoint guards above, but stay defined.
    slope = 0.0;
    return tbl.back().y;
}

class TableVCVS : public Device {
public:
    bool is_nonlinear() const override { return true; }
    // Simple form: control is V(ctrl_pos) - V(ctrl_neg)
    TableVCVS(std::string name,
              int32_t node_pos, int32_t node_neg,
              int32_t ctrl_pos, int32_t ctrl_neg,
              std::vector<TablePoint> table_points);

    // Expression form: control is an arbitrary scalar expression
    // E1 out 0 TABLE {(V(inp)-V(inm))*5000} = (x1,y1) (x2,y2) ...
    TableVCVS(std::string name,
              int32_t node_pos, int32_t node_neg,
              asrc::CompiledExpression expr,
              std::vector<int32_t> var_indices,
              std::vector<int32_t> var_indices2,
              std::vector<const VSource*> vsource_ptrs,
              std::vector<TablePoint> table_points);

    std::string device_type() const override { return "E"; }

    void set_branch_index(int32_t idx);
    int32_t branch_index() const override { return branch_idx_; }

    int32_t extra_vars() const override { return 1; }
    void assign_branch_index(int32_t& next) override {
        set_branch_index(next); next += extra_vars();
    }
    std::vector<std::string> output_currents() const override;

    std::vector<int32_t> external_nodes() const override;

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

private:
    // Returns the interpolated y value and the slope at x.
    double interp(double x, double& slope) const;
    void fill_var_values(const std::vector<double>& voltages);
    int32_t var_circuit_index(int i) const;

    int32_t np_;
    int32_t nn_;
    int32_t ncp_ = -1;
    int32_t ncn_ = -1;
    std::vector<TablePoint> table_;
    int32_t branch_idx_ = -1;

    MatrixOffset off_np_branch_  = -1;
    MatrixOffset off_nn_branch_  = -1;
    MatrixOffset off_branch_np_  = -1;
    MatrixOffset off_branch_nn_  = -1;

    // Simple-mode control offsets
    MatrixOffset off_branch_ncp_ = -1;
    MatrixOffset off_branch_ncn_ = -1;

    // Expression mode
    bool has_expr_ = false;
    asrc::CompiledExpression expr_;
    std::vector<int32_t> var_indices_;
    std::vector<int32_t> var_indices2_;
    std::vector<const VSource*> vsource_ptrs_;
    mutable std::vector<double> var_values_;
    mutable std::vector<double> derivs_;

    // Special simulator-variable indices into var_refs (-1 if unused).  These
    // are not circuit nodes: TIME/TEMPER/HERTZ are injected at evaluate() from
    // the active IntegratorCtx and skipped in stamping / node enumeration.
    int time_var_idx_   = -1;
    int temper_var_idx_ = -1;
    int hertz_var_idx_  = -1;

    struct VarStamp {
        MatrixOffset off_branch  = -1;  // (branch, var_node1)
        MatrixOffset off_branch2 = -1;  // (branch, var_node2)
    };
    std::vector<VarStamp> var_stamps_;
};

} // namespace neospice
