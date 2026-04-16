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

    double resistance() const { return resistance_; }

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
