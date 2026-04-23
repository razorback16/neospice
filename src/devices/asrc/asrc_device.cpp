#include "devices/asrc/asrc_device.hpp"
#include "devices/vsource.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx
#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace neospice {

// ===========================================================================
// Construction
// ===========================================================================

ASRCDevice::ASRCDevice(std::string name, int32_t node_pos, int32_t node_neg,
                       Mode mode, asrc::CompiledExpression expression,
                       std::vector<int32_t> resolved_node_indices,
                       std::vector<int32_t> resolved_node_indices2,
                       std::vector<const VSource*> vsource_ptrs)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , mode_(mode)
    , expr_(std::move(expression))
    , var_indices_(std::move(resolved_node_indices))
    , var_indices2_(std::move(resolved_node_indices2))
    , vsource_ptrs_(std::move(vsource_ptrs))
{
    int nv = expr_.num_vars();
    var_values_.resize(nv, 0.0);
    derivs_.resize(nv, 0.0);

    // Find the TIME variable index
    for (int i = 0; i < nv; ++i) {
        if (expr_.var_refs()[i].kind == asrc::VarKind::NODE_VOLTAGE &&
            expr_.var_refs()[i].name1 == "__time__") {
            time_var_idx_ = i;
            break;
        }
    }

    // Find the TEMPER variable index
    for (int i = 0; i < nv; ++i) {
        if (expr_.var_refs()[i].kind == asrc::VarKind::NODE_VOLTAGE &&
            expr_.var_refs()[i].name1 == "__temper__") {
            temper_var_idx_ = i;
            break;
        }
    }

    // Find the HERTZ variable index
    for (int i = 0; i < nv; ++i) {
        if (expr_.var_refs()[i].kind == asrc::VarKind::NODE_VOLTAGE &&
            expr_.var_refs()[i].name1 == "__hertz__") {
            hertz_var_idx_ = i;
            break;
        }
    }
}

// ===========================================================================
// process_temperature — compute output scaling factor from tc1/tc2
// ===========================================================================

void ASRCDevice::process_temperature(double sim_temp, double sim_tnom) {
    // Follows ngspice asrcload.c exactly:
    //   difference = (device_temp + dtemp) - tnom
    //   factor = 1 + tc1*diff + tc2*diff^2
    double temp = (temp_ > 0) ? temp_ : sim_temp;
    // If temp was explicitly given, dtemp is ignored (ngspice behavior)
    double dtemp = (temp_ > 0) ? 0.0 : dtemp_;
    double difference = (temp + dtemp) - sim_tnom;
    output_scale_ = 1.0 + tc1_ * difference + tc2_ * difference * difference;
}

// ===========================================================================
// var_circuit_index — resolve the MNA solution vector index for variable i
// ===========================================================================

int32_t ASRCDevice::var_circuit_index(int i) const {
    const auto& ref = expr_.var_refs()[i];
    if (ref.kind == asrc::VarKind::BRANCH_CURRENT) {
        // Read branch index from the VSource pointer (set during finalize)
        assert(vsource_ptrs_[i] != nullptr);
        return vsource_ptrs_[i]->branch_index();
    }
    return var_indices_[i];  // node index (or -2 for TIME)
}

// ===========================================================================
// Branch index assignment
// ===========================================================================

void ASRCDevice::assign_branch_index(int32_t& next) {
    if (mode_ == Mode::VOLTAGE) {
        branch_idx_ = next;
        next += 1;
    }
}

std::vector<std::string> ASRCDevice::output_currents() const {
    if (mode_ == Mode::VOLTAGE) {
        return { "I(" + name_ + ")" };
    }
    return {};
}

// ===========================================================================
// Sparsity pattern
// ===========================================================================

void ASRCDevice::stamp_pattern(SparsityBuilder& builder) const {
    const auto& refs = expr_.var_refs();

    if (mode_ == Mode::VOLTAGE) {
        // MNA branch coupling (like VSource)
        stamp_if_not_ground(builder, np_, branch_idx_);
        stamp_if_not_ground(builder, nn_, branch_idx_);
        stamp_if_not_ground(builder, branch_idx_, np_);
        stamp_if_not_ground(builder, branch_idx_, nn_);

        // Jacobian: (branch, var_node) for each variable
        for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
            if (time_var_idx_ == i) continue;
            if (temper_var_idx_ == i) continue;
            if (hertz_var_idx_ == i) continue;

            switch (refs[i].kind) {
            case asrc::VarKind::NODE_VOLTAGE:
                stamp_if_not_ground(builder, branch_idx_, var_indices_[i]);
                break;
            case asrc::VarKind::DIFF_VOLTAGE:
                stamp_if_not_ground(builder, branch_idx_, var_indices_[i]);
                stamp_if_not_ground(builder, branch_idx_, var_indices2_[i]);
                break;
            case asrc::VarKind::BRANCH_CURRENT: {
                int32_t br = var_circuit_index(i);
                stamp_if_not_ground(builder, branch_idx_, br);
                break;
            }
            }
        }
    } else {
        // Current mode: (np, var_node) and (nn, var_node)
        for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
            if (time_var_idx_ == i) continue;
            if (temper_var_idx_ == i) continue;
            if (hertz_var_idx_ == i) continue;

            switch (refs[i].kind) {
            case asrc::VarKind::NODE_VOLTAGE:
                stamp_if_not_ground(builder, np_, var_indices_[i]);
                stamp_if_not_ground(builder, nn_, var_indices_[i]);
                break;
            case asrc::VarKind::DIFF_VOLTAGE:
                stamp_if_not_ground(builder, np_, var_indices_[i]);
                stamp_if_not_ground(builder, nn_, var_indices_[i]);
                stamp_if_not_ground(builder, np_, var_indices2_[i]);
                stamp_if_not_ground(builder, nn_, var_indices2_[i]);
                break;
            case asrc::VarKind::BRANCH_CURRENT: {
                int32_t br = var_circuit_index(i);
                stamp_if_not_ground(builder, np_, br);
                stamp_if_not_ground(builder, nn_, br);
                break;
            }
            }
        }
    }
}

// ===========================================================================
// Offset assignment
// ===========================================================================

void ASRCDevice::assign_offsets(const SparsityPattern& pattern) {
    if (mode_ == Mode::VOLTAGE) {
        off_np_branch_ = offset_if_not_ground(pattern, np_, branch_idx_);
        off_nn_branch_ = offset_if_not_ground(pattern, nn_, branch_idx_);
        off_branch_np_ = offset_if_not_ground(pattern, branch_idx_, np_);
        off_branch_nn_ = offset_if_not_ground(pattern, branch_idx_, nn_);
    }

    const auto& refs = expr_.var_refs();
    var_stamps_.resize(refs.size());

    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        if (time_var_idx_ == i) continue;
        if (temper_var_idx_ == i) continue;
        if (hertz_var_idx_ == i) continue;

        if (mode_ == Mode::VOLTAGE) {
            switch (refs[i].kind) {
            case asrc::VarKind::NODE_VOLTAGE:
                var_stamps_[i].off_a = offset_if_not_ground(pattern, branch_idx_, var_indices_[i]);
                break;
            case asrc::VarKind::DIFF_VOLTAGE:
                var_stamps_[i].off_a  = offset_if_not_ground(pattern, branch_idx_, var_indices_[i]);
                var_stamps_[i].off_a2 = offset_if_not_ground(pattern, branch_idx_, var_indices2_[i]);
                break;
            case asrc::VarKind::BRANCH_CURRENT: {
                int32_t br = var_circuit_index(i);
                var_stamps_[i].off_a = offset_if_not_ground(pattern, branch_idx_, br);
                break;
            }
            }
        } else {
            switch (refs[i].kind) {
            case asrc::VarKind::NODE_VOLTAGE:
                var_stamps_[i].off_a = offset_if_not_ground(pattern, np_, var_indices_[i]);
                var_stamps_[i].off_b = offset_if_not_ground(pattern, nn_, var_indices_[i]);
                break;
            case asrc::VarKind::DIFF_VOLTAGE:
                var_stamps_[i].off_a  = offset_if_not_ground(pattern, np_, var_indices_[i]);
                var_stamps_[i].off_b  = offset_if_not_ground(pattern, nn_, var_indices_[i]);
                var_stamps_[i].off_a2 = offset_if_not_ground(pattern, np_, var_indices2_[i]);
                var_stamps_[i].off_b2 = offset_if_not_ground(pattern, nn_, var_indices2_[i]);
                break;
            case asrc::VarKind::BRANCH_CURRENT: {
                int32_t br = var_circuit_index(i);
                var_stamps_[i].off_a = offset_if_not_ground(pattern, np_, br);
                var_stamps_[i].off_b = offset_if_not_ground(pattern, nn_, br);
                break;
            }
            }
        }
    }
}

// ===========================================================================
// Fill variable values from circuit solution
// ===========================================================================

void ASRCDevice::fill_var_values(const std::vector<double>& voltages) {
    const auto& refs = expr_.var_refs();
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        if (i == time_var_idx_) {
            var_values_[i] = current_time_;
            continue;
        }

        if (i == temper_var_idx_) {
            double temp_celsius = 27.0;
            if (tls_integrator_ctx && tls_integrator_ctx->options)
                temp_celsius = tls_integrator_ctx->options->temp - 273.15;
            var_values_[i] = temp_celsius;
            continue;
        }

        if (i == hertz_var_idx_) {
            var_values_[i] = tls_integrator_ctx ? tls_integrator_ctx->ac_freq : 0.0;
            continue;
        }

        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE: {
            int32_t idx = var_indices_[i];
            var_values_[i] = (idx >= 0) ? voltages[idx] : 0.0;
            break;
        }
        case asrc::VarKind::DIFF_VOLTAGE: {
            int32_t idx1 = var_indices_[i];
            int32_t idx2 = var_indices2_[i];
            double v1 = (idx1 >= 0) ? voltages[idx1] : 0.0;
            double v2 = (idx2 >= 0) ? voltages[idx2] : 0.0;
            var_values_[i] = v1 - v2;
            break;
        }
        case asrc::VarKind::BRANCH_CURRENT: {
            int32_t br = var_circuit_index(i);
            var_values_[i] = (br >= 0) ? voltages[br] : 0.0;
            break;
        }
        }
    }
}

// ===========================================================================
// evaluate — Newton stamp for DC/transient
// ===========================================================================

void ASRCDevice::evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) {
    fill_var_values(voltages);

    // Set dt for DDT() evaluation
    if (tls_integrator_ctx) {
        expr_.set_dt(tls_integrator_ctx->delta);
    } else {
        expr_.set_dt(0.0);
    }

    // Evaluate expression and get derivatives
    double f_val = expr_.evaluate(var_values_, derivs_);

    // Store for convergence test
    prev_value_ = current_value_;
    current_value_ = f_val;
    has_prev_value_ = true;

    // Store derivatives for AC analysis
    ac_derivs_ = derivs_;
    ac_values_valid_ = true;

    const auto& refs = expr_.var_refs();

    // Temperature coefficient scaling factor (ngspice asrcload.c)
    const double factor = output_scale_;

    if (mode_ == Mode::VOLTAGE) {
        // ---------------------------------------------------------------
        // Voltage source mode — MNA formulation
        // Branch equation: V(np) - V(nn) - f(vars) = 0
        //
        // Linearized:
        //   mat[np, branch]     += 1
        //   mat[nn, branch]     -= 1
        //   mat[branch, np]     += 1
        //   mat[branch, nn]     -= 1
        //   mat[branch, var_k]  -= df/dx_k * factor
        //   rhs[branch]         += factor * (f(x0) - sum_k df/dx_k * x_k0)
        // ---------------------------------------------------------------

        // MNA coupling
        add_if_valid(mat, off_np_branch_,  1.0);
        add_if_valid(mat, off_nn_branch_, -1.0);
        add_if_valid(mat, off_branch_np_,  1.0);
        add_if_valid(mat, off_branch_nn_, -1.0);

        // Expression Jacobian and companion RHS
        double rhs_val = f_val;
        for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
            if (i == time_var_idx_) continue;
            if (i == temper_var_idx_) continue;
            if (i == hertz_var_idx_) continue;

            double d = derivs_[i];
            rhs_val -= d * var_values_[i];

            switch (refs[i].kind) {
            case asrc::VarKind::NODE_VOLTAGE:
            case asrc::VarKind::BRANCH_CURRENT:
                add_if_valid(mat, var_stamps_[i].off_a, -d * factor);
                break;
            case asrc::VarKind::DIFF_VOLTAGE:
                add_if_valid(mat, var_stamps_[i].off_a,  -d * factor);
                add_if_valid(mat, var_stamps_[i].off_a2,  d * factor);
                break;
            }
        }

        if (branch_idx_ >= 0) {
            rhs[branch_idx_] += factor * rhs_val;
        }

    } else {
        // ---------------------------------------------------------------
        // Current source mode — direct KCL stamp
        // I = f(vars) flows from np to nn (out of np, into nn)
        //
        // Linearized:
        //   mat[np, var_k] += df/dx_k * factor
        //   mat[nn, var_k] -= df/dx_k * factor
        //   rhs[np] -= factor * (f(x0) - sum_k df/dx_k * x_k0)
        //   rhs[nn] += factor * (f(x0) - sum_k df/dx_k * x_k0)
        // ---------------------------------------------------------------

        double companion = f_val;

        for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
            if (i == time_var_idx_) continue;
            if (i == temper_var_idx_) continue;
            if (i == hertz_var_idx_) continue;

            double d = derivs_[i];
            companion -= d * var_values_[i];

            switch (refs[i].kind) {
            case asrc::VarKind::NODE_VOLTAGE:
            case asrc::VarKind::BRANCH_CURRENT:
                add_if_valid(mat, var_stamps_[i].off_a,  d * factor);
                add_if_valid(mat, var_stamps_[i].off_b, -d * factor);
                break;
            case asrc::VarKind::DIFF_VOLTAGE:
                add_if_valid(mat, var_stamps_[i].off_a,   d * factor);
                add_if_valid(mat, var_stamps_[i].off_b,  -d * factor);
                add_if_valid(mat, var_stamps_[i].off_a2, -d * factor);
                add_if_valid(mat, var_stamps_[i].off_b2,  d * factor);
                break;
            }
        }

        add_rhs_if_valid(rhs, np_, -companion * factor);
        add_rhs_if_valid(rhs, nn_,  companion * factor);
    }
}

// ===========================================================================
// ac_stamp — small-signal AC stamp
// ===========================================================================

void ASRCDevice::ac_stamp(const std::vector<double>& voltages,
                          NumericMatrix& G, NumericMatrix& /*C*/) {
    // Use stored derivatives from the DC operating point
    if (!ac_values_valid_) {
        fill_var_values(voltages);
        expr_.evaluate(var_values_, ac_derivs_);
        ac_values_valid_ = true;
    }

    // Temperature coefficient scaling factor (matches DC stamp)
    const double factor = output_scale_;

    const auto& refs = expr_.var_refs();

    if (mode_ == Mode::VOLTAGE) {
        add_if_valid(G, off_np_branch_,  1.0);
        add_if_valid(G, off_nn_branch_, -1.0);
        add_if_valid(G, off_branch_np_,  1.0);
        add_if_valid(G, off_branch_nn_, -1.0);

        for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
            if (i == time_var_idx_) continue;
            if (i == temper_var_idx_) continue;
            if (i == hertz_var_idx_) continue;
            double d = ac_derivs_[i];
            switch (refs[i].kind) {
            case asrc::VarKind::NODE_VOLTAGE:
            case asrc::VarKind::BRANCH_CURRENT:
                add_if_valid(G, var_stamps_[i].off_a, -d * factor);
                break;
            case asrc::VarKind::DIFF_VOLTAGE:
                add_if_valid(G, var_stamps_[i].off_a,  -d * factor);
                add_if_valid(G, var_stamps_[i].off_a2,  d * factor);
                break;
            }
        }
    } else {
        for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
            if (i == time_var_idx_) continue;
            if (i == temper_var_idx_) continue;
            if (i == hertz_var_idx_) continue;
            double d = ac_derivs_[i];
            switch (refs[i].kind) {
            case asrc::VarKind::NODE_VOLTAGE:
            case asrc::VarKind::BRANCH_CURRENT:
                add_if_valid(G, var_stamps_[i].off_a,  d * factor);
                add_if_valid(G, var_stamps_[i].off_b, -d * factor);
                break;
            case asrc::VarKind::DIFF_VOLTAGE:
                add_if_valid(G, var_stamps_[i].off_a,   d * factor);
                add_if_valid(G, var_stamps_[i].off_b,  -d * factor);
                add_if_valid(G, var_stamps_[i].off_a2, -d * factor);
                add_if_valid(G, var_stamps_[i].off_b2,  d * factor);
                break;
            }
        }
    }
}

// ===========================================================================
// Convergence test
// ===========================================================================

bool ASRCDevice::device_converged() const {
    if (!has_prev_value_) return true;
    double tol = 1e-3 * std::max(std::abs(prev_value_), std::abs(current_value_)) + 1e-6;
    return std::abs(current_value_ - prev_value_) <= tol;
}

} // namespace neospice
