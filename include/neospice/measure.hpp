#pragma once
#include "neospice/types.hpp"

#include <span>
#include <utility>
#include <vector>

namespace neospice {
struct TransientResult;
struct ACResult;
struct NoiseResult;
}

namespace neospice::measure {

double rise_time(const TransientResult& r, NodeId node,
                 double low, double high);
double settling_time(const TransientResult& r, NodeId node,
                     double final_val, double tolerance);
double overshoot(const TransientResult& r, NodeId node, double final_val);
double rms(const TransientResult& r, NodeId node, double tstart, double tstop);
double bandwidth_3db(const ACResult& r, NodeId node);
std::pair<double,double> phase_margin(const ACResult& r, NodeId node);
std::pair<double,double> gain_margin(const ACResult& r, NodeId node);
double spot_noise(const NoiseResult& r, double freq);

} // namespace neospice::measure
