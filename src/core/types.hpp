#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <complex>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <cmath>

namespace neospice {
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
    double chgtol = 1e-14;   // charge tolerance for device LTE (ngspice default)
    double gmin   = 1e-12;
    double temp   = T_NOMINAL;
    double tnom   = T_NOMINAL; // nominal temperature for model parameters (default 27°C = 300.15K)
    int max_iter  = 100;
    int itl1      = 100;    // DC iteration limit (alias for max_iter)
    int itl4      = 50;     // transient iteration limit per timepoint
    std::string method = "trap"; // integration method: "trap" or "gear"
    int lte_ref_mode = 0;       // LTE reference mode: 0=per-node, 1=max-all, 2=max-per-signal-over-time
    bool verbose  = false;
};

/// Populated by the transient/DC driver before each Newton load, read by
/// state-storing devices (BSIM4v7).
/// DC op uses mode=MODEDCOP(0x10)|MODEINITJCT/FIX.
/// DC sweep uses mode=MODEDCTRANCURVE(0x40)|MODEINITJCT/FIX.
/// Transient DC preamble uses mode=MODETRANOP(0x20)|MODEINITJCT/FIX.
struct IntegratorCtx {
    int    mode  = 0;       // Shim-style CKTmode bitfield
    double ag[8] = {};      // UCB integrator coeffs (BE/Trap/Gear2)
    double delta = 0.0;
    double delta_old[8] = {};
    int    order = 1;
    int    integrate_method = 0;  // 0=trapezoidal, 1=gear
    double current_time = 0.0;
    double ac_freq = 0.0;

    double lte_coefficient() const {
        if (order <= 1) return 0.5;
        return (integrate_method == 0) ? (1.0 / 12.0) : (2.0 / 9.0);
    }

    // Published by the analysis driver (dc.cpp / transient.cpp) before the
    // Newton stamp loop so state-storing devices can read user-configured
    // temperature / tolerances without threading them through the Device
    // interface. Lifetime: the Circuit::options field (never dangling).
    const SimOptions* options = nullptr;
};

class ParseError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class ConvergenceError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
} // namespace neospice
