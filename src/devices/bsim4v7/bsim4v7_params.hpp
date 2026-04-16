#pragma once
#include <string>

namespace neospice {

struct BSIM4v7Params {
    // Model identification
    std::string name;
    bool is_pmos = false;   // true for PMOS, false for NMOS

    // Instance parameters (per-transistor)
    double W = 1e-6;        // Channel width (m)
    double L = 100e-9;      // Channel length (m)
    double nf = 1.0;        // Number of fingers
    double AS = 0.0, AD = 0.0;  // Source/drain area (m^2)
    double PS = 0.0, PD = 0.0;  // Source/drain perimeter (m)
    double NRS = 1.0, NRD = 1.0; // Source/drain squares

    // --- Threshold voltage ---
    double VTH0 = 0.7;     // Threshold voltage at zero body bias (V)
    double K1 = 0.5;       // First-order body bias coefficient (V^0.5)
    double K2 = -0.1;      // Second-order body bias coefficient
    double K3 = 80.0;      // Narrow width effect coefficient
    double K3B = 0.0;      // Body effect coefficient for K3
    double DVT0 = 2.2;     // Short-channel effect coefficient 0
    double DVT1 = 0.53;    // Short-channel effect coefficient 1
    double DVT2 = -0.032;  // Body bias coefficient of SCE
    double DVT0W = 0.0;    // Narrow-width effect coefficient 0
    double DVT1W = 5.3e6;  // Narrow-width effect coefficient 1
    double DVT2W = -0.032; // Body bias coefficient of NWE
    double DSUB = 0.56;    // DIBL coefficient in subthreshold region
    double ETA0 = 0.08;    // DIBL coefficient in subthreshold region
    double ETAB = -0.07;   // Body bias coefficient for DIBL
    double VOFF = -0.08;   // Threshold voltage offset in subthreshold

    // --- Mobility ---
    double U0 = 0.067;     // Low-field mobility (m^2/V/s)
    double UA = 1.0e-9;    // First-order mobility degradation (m/V) — BSIM4 default
    double UB = 1.0e-19;   // Second-order mobility degradation (m/V)^2 — BSIM4 default
    double UC = -4.65e-11; // Body bias effect on mobility
    double EU = 1.67;      // Exponent (unused in current mobMod=0 formula; kept for ABI)
    double VSAT = 1.5e5;   // Saturation velocity (m/s)
    double A0 = 1.0;       // Non-uniform depletion width coefficient
    double A1 = 0.0;       // First non-saturation factor
    double A2 = 1.0;       // Second non-saturation factor
    double AGS = 0.2;      // Gate bias coefficient of Abulk

    // --- Subthreshold ---
    double NFACTOR = 1.0;  // Subthreshold swing factor
    double CDSCD = 0.0;    // Drain/source to channel coupling cap
    double CDSCB = 0.0;    // Body bias sensitivity of CDSCD
    double CIT = 0.0;      // Interface trap capacitance
    double VOFFCV = 0.0;   // CV threshold voltage offset

    // --- Output resistance ---
    double PCLM = 1.3;     // Channel length modulation
    double PDIBLC1 = 0.39; // DIBL coefficient 1
    double PDIBLC2 = 0.0086; // DIBL coefficient 2
    double PDIBLCB = 0.0;  // Body bias coefficient of DIBL
    double DROUT = 0.56;   // L-dependence of DIBL
    double PSCBE1 = 4.24e8; // Substrate current body effect 1
    double PSCBE2 = 1e-5;   // Substrate current body effect 2
    double PVAG = 0.0;      // Gate dependence of output resistance
    double DELTA = 0.01;    // Effective Vds parameter

    // --- Capacitance ---
    double CGSO = 0.0;     // Gate-source overlap cap per width (F/m)
    double CGDO = 0.0;     // Gate-drain overlap cap per width (F/m)
    double CGBO = 0.0;     // Gate-body overlap cap per length (F/m)
    double CJ = 5e-4;      // Bottom junction cap (F/m^2)
    double CJSW = 5e-10;   // Sidewall junction cap (F/m)
    double CJSWG = 5e-10;  // Gate-edge sidewall junction cap (F/m)
    double MJ = 0.5;       // Bottom junction grading
    double MJSW = 0.33;    // Sidewall junction grading
    double MJSWG = 0.33;   // Gate sidewall junction grading
    double PB = 1.0;       // Bottom junction built-in potential (V)
    double PBSW = 1.0;     // Sidewall junction built-in potential (V)
    double PBSWG = 1.0;    // Gate sidewall junction built-in potential (V)

    // --- Gate current ---
    double AIGBACC = 1.36e-2;
    double BIGBACC = 1.71e-3;
    double CIGBACC = 0.075;

    // --- Oxide thickness and doping ---
    double TOXE = 3e-9;    // Electrical oxide thickness (m)
    double TOXP = 2.5e-9;  // Physical oxide thickness (m)
    double TOXM = 3e-9;    // Oxide thickness for mobility (m)
    double NDEP = 1.7e17;  // Channel doping concentration (1/cm^3)
    double NGATE = 0.0;    // Poly-gate doping concentration (1/cm^3)
    double NSD = 1e20;      // Source/drain doping (1/cm^3)

    // --- Temperature ---
    double TNOM = 27.0;    // Parameter extraction temperature (C)
    double UTE = -1.5;     // Temperature coefficient of mobility
    double KT1 = -0.11;    // Temperature coefficient of Vth
    double KT1L = 0.0;     // Channel length dependence of KT1
    double KT2 = 0.022;    // Body bias coefficient of KT1
    double AT = 3.3e4;     // Temperature coefficient of VSAT

    // --- Noise ---
    double NOIA = 6.25e41;
    double NOIB = 3.125e26;
    double NOIC = 8.75;
    double EF = 1.0;
    double EM = 4.1e7;

    // --- Parasitic resistance ---
    double RDSW = 200.0;   // Source/drain sheet resistance (ohm*um)
    double RSH = 0.0;      // Source/drain sheet resistance
    double PRWB = 0.0;     // Body bias coefficient of RDSW
    double PRWG = 0.0;     // Gate bias coefficient of RDSW
    double WR = 1.0;       // Width offset from Weff for Rds

    // Physical constants (derived, set during model init)
    double epsrox = 3.9;   // Oxide relative permittivity
    double EPSRSUB = 11.7; // Substrate relative permittivity
};

} // namespace neospice
