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
    double Roff = 1e6;   // off-state resistance (Ohm)
};

// ---------------------------------------------------------------------------
// Smooth transition helper (cubic hermite step function)
//
// x is the normalized control variable: x = (ctrl - Vt) / max(|Vh|, 1e-12)
// Returns 0 when x <= -1, 1 when x >= 1, cubic blend in between.
// ---------------------------------------------------------------------------
inline double switch_smooth_step(double x) {
    if (x <= -1.0) return 0.0;
    if (x >=  1.0) return 1.0;
    return 0.5 + x * (0.75 - 0.25 * x * x);
}

// ---------------------------------------------------------------------------
// VSwitch — Voltage-controlled switch (S element)
//
// Netlist syntax: S<name> n+ n- nc+ nc- modelname
//
// Stamps a variable conductance G between n+ and n-.
// G is determined by the smooth transition function applied to
//   Vc = V(nc+) - V(nc-)
// with threshold Vt and hysteresis Vh from the model card.
// ---------------------------------------------------------------------------

class VSwitch : public Device {
public:
    VSwitch(std::string name,
            int32_t node_pos, int32_t node_neg,
            int32_t node_ctrl_pos, int32_t node_ctrl_neg,
            const SwitchModel& model);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    const SwitchModel& model() const { return model_; }

private:
    int32_t np_, nn_;       // output nodes
    int32_t ncp_, ncn_;     // control voltage nodes
    SwitchModel model_;

    double last_g_ = 0.0;   // cached conductance from last evaluate() for AC

    MatrixOffset off_pp_ = -1;
    MatrixOffset off_pn_ = -1;
    MatrixOffset off_np_ = -1;
    MatrixOffset off_nn_ = -1;

    double compute_conductance(const std::vector<double>& voltages) const;
};

// ---------------------------------------------------------------------------
// CSwitch — Current-controlled switch (W element)
//
// Netlist syntax: W<name> n+ n- Vsense modelname
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
            const SwitchModel& model);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    const SwitchModel& model() const { return model_; }

private:
    int32_t np_, nn_;       // output nodes
    const VSource* sense_;  // sensing VSource (non-owning)
    SwitchModel model_;

    double last_g_ = 0.0;   // cached conductance from last evaluate() for AC

    MatrixOffset off_pp_ = -1;
    MatrixOffset off_pn_ = -1;
    MatrixOffset off_np_ = -1;
    MatrixOffset off_nn_ = -1;

    double compute_conductance(const std::vector<double>& voltages) const;
};

} // namespace neospice
