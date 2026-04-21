#pragma once

namespace neospice {

struct InductorModel {
    double tc1 = 0.0;
    double tc2 = 0.0;
    double tnom = -1.0;  // -1 = use simulation default
};

} // namespace neospice
