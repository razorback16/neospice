#pragma once
#include "api/neospice.hpp"
#include <cmath>
#include <stdexcept>
#include <vector>

namespace neospice::bench {

// Integrity only: this is not reference agreement or a KCL residual check.
inline void require_diagnostic_dc(const DCResult& result) {
    if (!result.status.converged || result.node_voltages.empty())
        throw std::runtime_error("DC diagnostic failed: unconverged or empty output");
    for (const auto& [name, value] : result.node_voltages)
        if (!std::isfinite(value))
            throw std::runtime_error("DC diagnostic has nonfinite voltage: " + name);
    for (const auto& [name, value] : result.branch_currents)
        if (!std::isfinite(value))
            throw std::runtime_error("DC diagnostic has nonfinite current: " + name);
}

// The runner must reject invalid results. A failed warmup or later sample
// propagates instead of returning earlier samples for an apparently valid median.
template<class Run>
auto diagnostic_samples(int warmup, int runs, Run run) {
    if (warmup < 0 || runs <= 0)
        throw std::invalid_argument("Diagnostic sampling requires warmup >= 0 and runs > 0");
    for (int i = 0; i < warmup; ++i) (void)run();
    std::vector<decltype(run())> samples;
    samples.reserve(runs);
    for (int i = 0; i < runs; ++i) samples.push_back(run());
    return samples;
}

} // namespace neospice::bench
