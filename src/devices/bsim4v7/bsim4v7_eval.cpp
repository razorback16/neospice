#include "devices/bsim4v7/bsim4v7_eval.hpp"
#include <cmath>
#include <algorithm>

namespace neospice {

static constexpr double EPS0 = 8.854187817e-12;  // vacuum permittivity
static constexpr double EPSOX = 3.9 * EPS0;       // SiO2 permittivity
static constexpr double EPSSUB = 11.7 * EPS0;     // Si permittivity
static constexpr double KBQ = 8.617333262e-5;     // k/q in eV/K
static constexpr double Q_ELEC = 1.602176634e-19;
static constexpr double KB = 1.380649e-23;

BSIM4v7EvalResult bsim4v7_evaluate(
    double Vgs, double Vds, double Vbs,
    const BSIM4v7Params& p,
    double temp) {

    BSIM4v7EvalResult r{};

    const double Vt = KB * temp / Q_ELEC;
    const double Cox = EPSOX / p.TOXE;
    const double Weff = p.W * p.nf;
    const double Leff = p.L;
    const double WL = Weff / Leff;

    // --- Threshold voltage ---
    double sqrtPhis = std::sqrt(std::max(0.4, 0.4 - Vbs));  // simplified 2*phi_s
    double Vth = p.VTH0 + p.K1 * sqrtPhis - p.K2 * Vbs
                 - p.ETA0 * Vds - p.DSUB * Vds;

    // --- Subthreshold region ---
    double n_sub = 1.0 + p.NFACTOR * EPSSUB / (Cox * Leff);
    double Vgst = Vgs - Vth;

    // Smooth transition using log-sum-exp
    double Vgst_eff;
    if (Vgst > 40.0 * n_sub * Vt) {
        Vgst_eff = Vgst;
    } else if (Vgst < -40.0 * n_sub * Vt) {
        Vgst_eff = n_sub * Vt * std::exp(Vgst / (n_sub * Vt));
    } else {
        Vgst_eff = n_sub * Vt * std::log(1.0 + std::exp(Vgst / (n_sub * Vt)));
    }

    // --- Mobility degradation ---
    double Eeff = (Vgst_eff + 2.0 * (0.4 - Vbs)) / (6.0 * p.TOXE);
    double mu = p.U0 / (1.0 + p.UA * std::pow(std::abs(Eeff), p.EU)
                         + p.UB * Eeff * Eeff);

    // --- Saturation voltage ---
    double Esat = 2.0 * p.VSAT / mu;
    double EsatL = Esat * Leff;
    double Vdsat = (EsatL * Vgst_eff) / (EsatL + Vgst_eff);

    // --- Drain-source voltage clamping ---
    double Vds_eff;
    {
        double delta4 = p.DELTA;
        double tmp = Vdsat - Vds - delta4;
        double tmp2 = std::sqrt(tmp * tmp + 4.0 * delta4 * Vdsat);
        Vds_eff = Vdsat - 0.5 * (tmp + tmp2);
    }

    // --- Drain current ---
    double Ids_lin = WL * mu * Cox * Vgst_eff * Vds_eff;
    double Va = Vds_eff / EsatL;
    double Ids = Ids_lin / (1.0 + Va);

    // --- Channel length modulation ---
    double CLM = 1.0;
    if (p.PCLM > 0.0 && Vds > Vdsat) {
        CLM = 1.0 + p.PCLM * std::log(1.0 + (Vds - Vds_eff) / (p.PCLM * Vdsat + 1e-20));
    }
    Ids *= CLM;

    // --- Conductances (numerical derivatives for robustness) ---
    // We use analytical approximations for the main terms
    double dIds_dVgst = WL * mu * Cox * Vds_eff / (1.0 + Va);
    double dVgst_dVgs = 1.0;
    if (Vgst < 40.0 * n_sub * Vt) {
        double expg = std::exp(Vgst / (n_sub * Vt));
        dVgst_dVgs = expg / (1.0 + expg);
    }

    r.Ids = Ids;
    r.gm = dIds_dVgst * dVgst_dVgs * CLM;

    // Output conductance
    r.gds = Ids * 0.01 / (std::abs(Vds) + 0.01);  // simplified
    if (Vds > Vdsat && p.PCLM > 0.0) {
        r.gds += Ids * p.PCLM / (Vds - Vds_eff + p.PCLM * Vdsat + 1e-20);
    }

    // Body transconductance
    r.gmb = r.gm * p.K1 / (2.0 * sqrtPhis + 1e-20);

    // --- Intrinsic capacitances (Meyer model simplified) ---
    double Coxeff = Cox * Weff * Leff;
    if (Vgst_eff > 0.0) {
        double x = Vds_eff / (2.0 * Vgst_eff + 1e-20);
        x = std::min(x, 1.0);
        r.Cgs = (2.0 / 3.0) * Coxeff * (1.0 - x * x / ((2.0 - x) * (2.0 - x) + 1e-20));
        r.Cgd = (2.0 / 3.0) * Coxeff * (1.0 - ((1.0 - x) * (1.0 - x)) / ((2.0 - x) * (2.0 - x) + 1e-20));
    } else {
        r.Cgs = 0.5 * Coxeff;
        r.Cgd = 0.5 * Coxeff;
    }
    r.Cgb = 0.0;  // simplified

    // Overlap capacitances
    r.Cgs += p.CGSO * Weff;
    r.Cgd += p.CGDO * Weff;
    r.Cgb += p.CGBO * Leff;

    // Junction capacitances (simplified)
    double AS = (p.AS > 0.0) ? p.AS : Weff * 0.5e-6;
    double AD = (p.AD > 0.0) ? p.AD : Weff * 0.5e-6;
    double PS_val = (p.PS > 0.0) ? p.PS : 2.0 * Weff + 1e-6;
    double PD_val = (p.PD > 0.0) ? p.PD : 2.0 * Weff + 1e-6;

    double Vbs_junc = std::min(Vbs, 0.0);
    double Vbd_junc = std::min(Vbs - Vds, 0.0);

    r.Cbs = p.CJ * AS * std::pow(1.0 - Vbs_junc / p.PB, -p.MJ)
            + p.CJSW * PS_val * std::pow(1.0 - Vbs_junc / p.PBSW, -p.MJSW);
    r.Cbd = p.CJ * AD * std::pow(1.0 - Vbd_junc / p.PB, -p.MJ)
            + p.CJSW * PD_val * std::pow(1.0 - Vbd_junc / p.PBSW, -p.MJSW);

    // Charges (integral of capacitances — simplified)
    r.Qg = r.Cgs * Vgs + r.Cgd * (Vgs - Vds) + r.Cgb * (Vgs - Vbs);
    r.Qd = -r.Cgd * (Vgs - Vds);
    r.Qb = -r.Cgb * (Vgs - Vbs);

    return r;
}

} // namespace neospice
