#pragma once

namespace neospice {

struct CapacitorModel {
    double tc1 = 0.0;
    double tc2 = 0.0;
    double vc1 = 0.0;   // voltage coefficient 1
    double vc2 = 0.0;   // voltage coefficient 2
    double tnom = -1.0;  // -1 = use simulation default
};

} // namespace neospice
