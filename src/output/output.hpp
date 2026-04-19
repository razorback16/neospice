#pragma once
#include "core/circuit.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "core/dc.hpp"
#include "core/noise.hpp"
#include <string>

namespace neospice {

/// Format a .print command as tabular ASCII output.
/// analysis_type determines which result is used (tran/ac/dc/noise).
/// Returns the formatted table as a string.
std::string format_print(const PrintCommand& cmd,
                          const TransientResult* tran,
                          const ACResult* ac,
                          const DCSweepResult* dc_sweep,
                          const NoiseResult* noise);

/// Format a .plot command as ASCII waveform plot.
/// One plot per signal is generated; all are concatenated.
/// Returns the ASCII plot as a string.
std::string format_plot(const PrintCommand& cmd,
                         const TransientResult* tran,
                         const ACResult* ac,
                         const DCSweepResult* dc_sweep,
                         const NoiseResult* noise);

} // namespace neospice
