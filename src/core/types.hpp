#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <complex>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <cmath>

namespace cudaspice {
using NodeIndex = int32_t;
constexpr NodeIndex GROUND = 0;
using MatrixOffset = int32_t;

constexpr double BOLTZMANN = 1.380649e-23;
constexpr double CHARGE_Q  = 1.602176634e-19;
constexpr double T_NOMINAL = 300.15;

inline double thermal_voltage(double temp = T_NOMINAL) {
    return BOLTZMANN * temp / CHARGE_Q;
}

struct SimOptions {
    double abstol = 1e-12;
    double reltol = 1e-3;
    double vntol  = 1e-6;
    double trtol  = 7.0;
    double gmin   = 1e-12;
    double temp   = T_NOMINAL;
    int max_iter  = 100;
};

class ParseError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class ConvergenceError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
} // namespace cudaspice
