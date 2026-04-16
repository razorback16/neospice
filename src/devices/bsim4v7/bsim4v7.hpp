#pragma once
#include "devices/device.hpp"
#include "devices/bsim4v7/bsim4v7_params.hpp"
#include "devices/bsim4v7/bsim4v7_eval.hpp"

namespace neospice {

class BSIM4v7 : public Device {
public:
    BSIM4v7(std::string name, int32_t node_drain, int32_t node_gate,
            int32_t node_source, int32_t node_body, const BSIM4v7Params& params);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void limit_voltages(const std::vector<double>& old_v,
                        std::vector<double>& new_v) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    std::vector<std::string> output_currents() const override {
        return { "id(" + name_ + ")" };
    }

    // Transient companion model for intrinsic MOSFET charges (Qg, Qd, Qb)
    void set_transient(double dt);
    void clear_transient();
    void set_integration_method(int method);
    void accept_step_from_solution(const std::vector<double>& sol);
    void init_dc_state(const std::vector<double>& sol);

private:
    int32_t nd_, ng_, ns_, nb_;  // drain, gate, source, body
    BSIM4v7Params params_;

    // Cached evaluation result for AC
    BSIM4v7EvalResult last_eval_;

    // 4x4 offset matrix (4 terminals: d, g, s, b)
    // [dd, dg, ds, db]
    // [gd, gg, gs, gb]
    // [sd, sg, ss, sb]
    // [bd, bg, bs, bb]
    MatrixOffset off_[4][4];

    int32_t terminal(int i) const;

    // Transient state
    bool transient_ = false;
    double dt_ = 0.0;
    int integration_method_ = 0;
    // Previous step charges
    double Qg_prev_ = 0.0, Qd_prev_ = 0.0, Qb_prev_ = 0.0;
    double Ig_prev_ = 0.0, Id_cap_prev_ = 0.0, Ib_prev_ = 0.0;
};

} // namespace neospice
