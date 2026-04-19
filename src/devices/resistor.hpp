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

    double resistance() const { return resistance_; }

    /// Optional flicker (1/f) noise parameters.
    /// If noise_kf == 0 (default), no flicker noise is added (backward compatible).
    double noise_kf = 0.0;  // Flicker noise coefficient
    double noise_af = 1.0;  // Flicker noise current exponent

private:
    int32_t np_;          // positive node (GROUND_INTERNAL = -1 for ground)
    int32_t nn_;          // negative node (GROUND_INTERNAL = -1 for ground)
    double resistance_;

    MatrixOffset off_pp_ = -1;
    MatrixOffset off_pn_ = -1;
    MatrixOffset off_np_ = -1;
    MatrixOffset off_nn_ = -1;
};

} // namespace neospice
