#pragma once

namespace neospice {

struct ResistorModel {
    double tc1 = 0.0;
    double tc2 = 0.0;
    double rac = -1.0;  // -1 = not specified
    double kf = 0.0;    // flicker noise coefficient
    double af = 1.0;    // flicker noise exponent
    double tnom = -1.0;  // -1 = use simulation default
};

} // namespace neospice
