#pragma once
#include "devices/device.hpp"
#include "devices/vsource.hpp"
#include <string>

namespace neospice {

// ---------------------------------------------------------------------------
// SwitchModel — SPICE-style model parameters for SW and CSW switches
// ---------------------------------------------------------------------------

struct SwitchModel {
    std::string name;
    bool is_voltage_controlled = true;  // true = SW (V-controlled), false = CSW (I-controlled)
    double Vt  = 0.0;    // threshold voltage (SW) / threshold current (CSW)
    double Vh  = 0.0;    // hysteresis voltage (SW) / hysteresis current (CSW)
    double Ron  = 1.0;   // on-state resistance (Ohm)
    double Roff = 1e12;  // off-state resistance (Ohm) — ngspice default
};

// ---------------------------------------------------------------------------
// 4-state hysteresis model (matches ngspice swload.c / cswload.c)
// ---------------------------------------------------------------------------

enum class SwitchState : int {
    REALLY_OFF = 0,
    REALLY_ON  = 1,
    HYST_OFF   = 2,
    HYST_ON    = 3,
};

/// Returns true when the switch state means the switch is conducting.
inline bool switch_is_on(SwitchState s) {
    return s == SwitchState::REALLY_ON || s == SwitchState::HYST_ON;
}

// ---------------------------------------------------------------------------
// VSwitch — Voltage-controlled switch (S element)
//
// Netlist syntax: S<name> n+ n- nc+ nc- modelname [ON|OFF]
//
// Stamps a variable conductance G between n+ and n-.
// G is determined by the 4-state hysteresis model applied to
//   Vc = V(nc+) - V(nc-)
// with threshold Vt and hysteresis Vh from the model card.
// ---------------------------------------------------------------------------

class VSwitch : public Device {
public:
    VSwitch(std::string name,
            int32_t node_pos, int32_t node_neg,
            int32_t node_ctrl_pos, int32_t node_ctrl_neg,
            const SwitchModel& model,
            bool initial_on = false);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;
    bool device_converged() const override { return !state_changed_; }
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;

    std::vector<int32_t> external_nodes() const override { return {np_, nn_, ncp_, ncn_}; }

    const SwitchModel& model() const { return model_; }

private:
    int32_t np_, nn_;       // output nodes
    int32_t ncp_, ncn_;     // control voltage nodes
    SwitchModel model_;
    bool initial_on_;

    SwitchState current_state_;
    SwitchState previous_state_;
    bool state_changed_ = false;
    bool prev_state_changed_ = false;
    double last_g_ = 0.0;   // cached conductance from last evaluate() for AC

    MatrixOffset off_pp_ = -1;
    MatrixOffset off_pn_ = -1;
    MatrixOffset off_np_ = -1;
    MatrixOffset off_nn_ = -1;
};

// ---------------------------------------------------------------------------
// CSwitch — Current-controlled switch (W element)
//
// Netlist syntax: W<name> n+ n- Vsense modelname [ON|OFF]
//
// Same as VSwitch but the control variable is the branch current of the
// sense voltage source (same mechanism as CCCS/CCVS).
// ---------------------------------------------------------------------------

class CSwitch : public Device {
public:
    /// vsense must remain valid for the lifetime of this CSwitch (owned by Circuit).
    CSwitch(std::string name,
            int32_t node_pos, int32_t node_neg,
            const VSource* sense,
            const SwitchModel& model,
            bool initial_on = false);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;
    bool device_converged() const override { return !state_changed_; }
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;

    std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }

    const SwitchModel& model() const { return model_; }

private:
    int32_t np_, nn_;       // output nodes
    const VSource* sense_;  // sensing VSource (non-owning)
    SwitchModel model_;
    bool initial_on_;

    SwitchState current_state_;
    SwitchState previous_state_;
    bool state_changed_ = false;
    bool prev_state_changed_ = false;
    double last_g_ = 0.0;   // cached conductance from last evaluate() for AC

    MatrixOffset off_pp_ = -1;
    MatrixOffset off_pn_ = -1;
    MatrixOffset off_np_ = -1;
    MatrixOffset off_nn_ = -1;
};

} // namespace neospice
