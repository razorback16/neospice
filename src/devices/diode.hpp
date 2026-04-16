#pragma once
#include "devices/device.hpp"
#include <string>
#include <cmath>

namespace cudaspice {

// ---------------------------------------------------------------------------
// DiodeModel — SPICE-style diode model parameters
// ---------------------------------------------------------------------------

struct DiodeModel {
    std::string name = "D";
    double Is  = 1e-14;   // Saturation current (A)
    double N   = 1.0;     // Emission coefficient
    double Cj0 = 0.0;     // Zero-bias junction capacitance (F)
    double Vj  = 0.7;     // Junction potential (V)
    double M   = 0.5;     // Grading coefficient
    double Tt  = 0.0;     // Transit time (s), diffusion capacitance
    double Bv  = 100.0;   // Breakdown voltage (V)
    double Ibv = 1e-3;    // Current at breakdown (A)
};

// ---------------------------------------------------------------------------
// Diode — nonlinear two-terminal device using Shockley equation
// ---------------------------------------------------------------------------

class Diode : public Device {
public:
    Diode(std::string name, int32_t node_anode, int32_t node_cathode,
          const DiodeModel& model);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void limit_voltages(const std::vector<double>& old_v,
                        std::vector<double>& new_v) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    std::vector<std::string> output_currents() const override;

    void set_temp(double temp);

private:
    int32_t na_, nc_;          // anode, cathode node indices
    DiodeModel model_;
    double temp_ = T_NOMINAL;  // operating temperature (K)

    MatrixOffset off_aa_ = -1, off_ac_ = -1, off_ca_ = -1, off_cc_ = -1;

    double last_gd_ = 0.0;     // cached small-signal conductance for AC
    double last_cd_ = 0.0;     // cached small-signal capacitance for AC
};

} // namespace cudaspice
