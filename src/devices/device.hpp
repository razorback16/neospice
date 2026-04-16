#pragma once
#include "core/types.hpp"
#include "core/matrix.hpp"
#include <string>
#include <vector>

namespace neospice {

constexpr int32_t GROUND_INTERNAL = -1;  // internal index for ground node

class Device {
public:
    explicit Device(std::string name) : name_(std::move(name)) {}
    virtual ~Device() = default;
    const std::string& name() const { return name_; }

    virtual void stamp_pattern(SparsityBuilder& builder) const = 0;
    virtual void assign_offsets(const SparsityPattern& pattern) = 0;
    virtual void evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) = 0;
    virtual void limit_voltages(const std::vector<double>& /*old_v*/,
                                std::vector<double>& /*new_v*/) {}
    virtual void ac_stamp(const std::vector<double>& voltages,
                          NumericMatrix& G, NumericMatrix& C) {}
    virtual int32_t extra_vars() const { return 0; }
    virtual std::vector<std::string> output_currents() const { return {}; }

protected:
    std::string name_;

    // Helpers for ground-aware stamping
    static void stamp_if_not_ground(SparsityBuilder& builder, int32_t r, int32_t c) {
        if (r >= 0 && c >= 0) builder.add(r, c);
    }
    static MatrixOffset offset_if_not_ground(const SparsityPattern& pattern, int32_t r, int32_t c) {
        if (r >= 0 && c >= 0) return pattern.offset(r, c);
        return -1;
    }
    static void add_if_valid(NumericMatrix& mat, MatrixOffset off, double val) {
        if (off >= 0) mat.add(off, val);
    }
    static void add_rhs_if_valid(std::vector<double>& rhs, int32_t node, double val) {
        if (node >= 0) rhs[node] += val;
    }
};

} // namespace neospice
