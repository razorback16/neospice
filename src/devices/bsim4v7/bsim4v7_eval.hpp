#pragma once
#include "bsim4v7_params.hpp"

namespace neospice {

struct BSIM4v7EvalResult {
    double Ids;    // Drain-source current
    double gm;     // Transconductance dIds/dVgs
    double gds;    // Output conductance dIds/dVds
    double gmb;    // Body transconductance dIds/dVbs

    // Intrinsic charges
    double Qg;     // Gate charge
    double Qd;     // Drain charge
    double Qb;     // Body charge

    // Capacitances (dQ/dV) for AC
    double Cgs;    // dQg/dVgs
    double Cgd;    // dQg/dVgd
    double Cgb;    // dQg/dVgb
    double Cbd;    // dQb/dVbd (junction)
    double Cbs;    // dQb/dVbs (junction)
};

// Pure evaluation function — takes terminal voltages and model params,
// returns currents, conductances, and charges.
// This function is CPU-portable and will be adapted for CUDA later.
// Terminal voltages are internal (already mapped for NMOS/PMOS).
BSIM4v7EvalResult bsim4v7_evaluate(
    double Vgs, double Vds, double Vbs,
    const BSIM4v7Params& params,
    double temp);

} // namespace neospice
