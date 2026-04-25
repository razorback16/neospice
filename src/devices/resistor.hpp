#pragma once
#include "devices/device.hpp"

namespace neospice {

class Resistor : public Device {
public:
    Resistor(std::string name, int32_t node_pos, int32_t node_neg, double resistance);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    /// Thermal noise: 4kT/R (current noise spectral density A²/Hz)
    std::vector<NoiseSource> noise_sources(
        double freq, const std::vector<double>& dc_solution) const override;

    double resistance() const { return resistance_eff_; }
    double resistance_nom() const { return resistance_nom_; }
    void set_resistance(double r) { resistance_eff_ = r; }

    /// Temperature coefficient setters (instance-level parameters)
    void set_tc1(double tc1) { tc1_ = tc1; }
    void set_tc2(double tc2) { tc2_ = tc2; }
    void set_scale(double s) { scale_ = s; }
    void set_multiplier(double m) { m_ = m; }
    void set_temp(double t) { temp_ = t; }
    void set_dtemp(double dt) { dtemp_ = dt; }
    void set_rac(double r) { rac_ = r; }
    double rac() const { return rac_; }

    std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
    std::optional<double> primary_value() const override { return resistance_eff_; }
    bool set_value(double value) override {
        resistance_nom_ = value; resistance_eff_ = value; return true;
    }

    /// Apply temperature-dependent adjustment to effective resistance.
    /// Called once during circuit finalize (or whenever temperature changes).
    void process_temperature(double sim_temp, double sim_tnom) override;

    /// Optional flicker (1/f) noise parameters.
    /// If noise_kf == 0 (default), no flicker noise is added (backward compatible).
    double noise_kf = 0.0;  // Flicker noise coefficient
    double noise_af = 1.0;  // Flicker noise current exponent

private:
    int32_t np_;          // positive node (GROUND_INTERNAL = -1 for ground)
    int32_t nn_;          // negative node (GROUND_INTERNAL = -1 for ground)
    double resistance_nom_;    // original (nominal) resistance value
    double resistance_eff_;    // effective value after temperature/scale adjustment

    double tc1_ = 0.0;        // temperature coefficient 1 (1/K)
    double tc2_ = 0.0;        // temperature coefficient 2 (1/K^2)
    double scale_ = 1.0;      // instance scale factor
    double m_ = 1.0;          // multiplier (m instances in parallel)
    double temp_ = -1.0;      // device temperature in K (-1 = use simulation default)
    double dtemp_ = 0.0;      // delta temperature in K
    double rac_ = -1.0;       // AC resistance (-1 = use DC resistance)

    MatrixOffset off_pp_ = -1;
    MatrixOffset off_pn_ = -1;
    MatrixOffset off_np_ = -1;
    MatrixOffset off_nn_ = -1;
};

} // namespace neospice
