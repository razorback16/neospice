#pragma once
#include "core/transient.hpp"
#include "core/dc.hpp"
#include "core/ac.hpp"
#include <string>

namespace neospice {

struct Tolerance {
    double relative = 1e-3;
    double absolute = 1e-9;
};

struct CompareResult {
    bool passed;
    std::string worst_signal;
    double worst_error;
    int num_points_compared;
};

CompareResult compare_dc(const DCResult& expected, const DCResult& actual, Tolerance tol = {});
CompareResult compare_transient(const TransientResult& expected, const TransientResult& actual, Tolerance tol = {});
CompareResult compare_ac(const ACResult& expected, const ACResult& actual, Tolerance tol = {});

} // namespace neospice
