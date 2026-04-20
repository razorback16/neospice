#pragma once
// ---------------------------------------------------------------------------
// asrc_device.hpp — ASRC (B element) behavioral source device
//
// Syntax:
//   Bname node+ node- V={expression}   -- behavioral voltage source
//   Bname node+ node- I={expression}   -- behavioral current source
//
// Expressions may reference V(node), V(n1,n2), I(Vname), TIME, and math
// functions (sin, cos, exp, log, sqrt, abs, min, max, pow, etc.).
//
// Voltage mode adds an MNA branch variable (like VSource).
// Current mode stamps directly into KCL rows (like ISource).
//
// Jacobian entries are computed via forward-mode automatic differentiation
// of the compiled expression AST (no numerical perturbation needed).
// ---------------------------------------------------------------------------

#include "devices/device.hpp"
#include "devices/asrc/expression_ast.hpp"
#include <string>
#include <vector>

namespace neospice {

class VSource;  // forward decl

// ---------------------------------------------------------------------------
// ASRC — Arbitrary Source (B element)
// ---------------------------------------------------------------------------

class ASRCDevice : public Device {
public:
    enum class Mode { VOLTAGE, CURRENT };

    /// Construct from parsed data.
    ///   name:        instance name (e.g. "B1")
    ///   node_pos/neg: output terminal indices
    ///   mode:        VOLTAGE or CURRENT
    ///   expression:  the compiled expression AST
    ///   resolved_node_indices: per-variable node index (for V() refs)
    ///   resolved_node_indices2: second node (for V(n1,n2) diff refs)
    ///   vsource_ptrs: per-variable VSource* (for I() refs, nullptr otherwise)
    ASRCDevice(std::string name, int32_t node_pos, int32_t node_neg,
               Mode mode, asrc::CompiledExpression expression,
               std::vector<int32_t> resolved_node_indices,
               std::vector<int32_t> resolved_node_indices2,
               std::vector<const VSource*> vsource_ptrs);

    // -- Device interface --

    int32_t extra_vars() const override {
        return (mode_ == Mode::VOLTAGE) ? 1 : 0;
    }
    void assign_branch_index(int32_t& next) override;
    std::vector<std::string> output_currents() const override;

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    bool device_converged() const override;

    /// Set the simulation time for the next evaluate call.
    void set_time(double t) { current_time_ = t; }

    int32_t branch_index() const { return branch_idx_; }
    Mode mode() const { return mode_; }
    const asrc::CompiledExpression& expression() const { return expr_; }

private:
    /// Get the circuit solution vector index for variable i.
    /// For I() refs, reads the VSource's branch_index (valid after finalize step 1).
    int32_t var_circuit_index(int i) const;

    /// Collect current variable values from the circuit solution vector.
    void fill_var_values(const std::vector<double>& voltages);

    int32_t np_;           // positive node (GROUND_INTERNAL = -1)
    int32_t nn_;           // negative node (GROUND_INTERNAL = -1)
    Mode    mode_;
    int32_t branch_idx_ = -1;  // MNA branch index (voltage mode only)

    asrc::CompiledExpression expr_;

    // Resolved variable indices for V() refs (node index, -1 = ground)
    std::vector<int32_t> var_indices_;    // primary node index
    std::vector<int32_t> var_indices2_;   // second node (DIFF_VOLTAGE only)

    // For I() refs: pointer to VSource device (branch_index read lazily)
    std::vector<const VSource*> vsource_ptrs_;

    // Working buffers (reused between evaluations)
    mutable std::vector<double> var_values_;
    mutable std::vector<double> derivs_;

    // Convergence test
    mutable double prev_value_ = 0.0;
    mutable double current_value_ = 0.0;
    mutable bool has_prev_value_ = false;

    // Simulation time
    double current_time_ = 0.0;

    // Time variable index (-1 if expression doesn't use TIME)
    int time_var_idx_ = -1;

    // -- Matrix offsets --

    // Voltage mode: MNA branch stamps
    MatrixOffset off_np_branch_ = -1;
    MatrixOffset off_nn_branch_ = -1;
    MatrixOffset off_branch_np_ = -1;
    MatrixOffset off_branch_nn_ = -1;

    // Per-variable Jacobian entries
    struct VarStamp {
        MatrixOffset off_a = -1;   // (branch, node) or (np, node)
        MatrixOffset off_b = -1;   // (nn, node) — current mode only
        MatrixOffset off_a2 = -1;  // for DIFF_VOLTAGE second node
        MatrixOffset off_b2 = -1;  // for DIFF_VOLTAGE second node (nn)
    };
    std::vector<VarStamp> var_stamps_;

    // AC precomputed derivatives (stored during DC evaluate)
    mutable std::vector<double> ac_derivs_;
    mutable bool ac_values_valid_ = false;
};

} // namespace neospice
