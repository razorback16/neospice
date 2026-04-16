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
    // Clamp DIBL contribution: in real BSIM4 this saturates; here we clamp Vds
    // contribution to Vth so Newton overshoots don't produce huge negative Vth.
    double Vds_clamped = std::max(0.0, std::min(Vds, 5.0));
    double Vth = p.VTH0 + p.K1 * sqrtPhis - p.K2 * Vbs
                 - p.ETA0 * Vds_clamped - p.DSUB * Vds_clamped;

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

    // --- Mobility degradation (BSIM4 mobMod=0, matches ngspice b4ld.c) ---
    // T3 = (Vgsteff + 2*Vth) / TOXE ; mu = U0 / (1 + T3*(UA + UB*T3))
    double T3 = (Vgst_eff + 2.0 * Vth) / p.TOXE;
    double Denomi = 1.0 + T3 * (p.UA + p.UB * T3);
    if (Denomi < 1e-6) Denomi = 1e-6;
    double mu = p.U0 / Denomi;

    // --- Abulk bulk-charge correction (ngspice b4v7ld.c:1376-1430) ---
    // We omit Lpe (lateral pocket implant profile) and Vth_NarrowW (narrow-width
    // Vth correction) since we don't model pocket implants. This yields the
    // bulk-planar Abulk. Xdep (depletion depth) is approximated from NDEP.
    // NDEP is stored in cm^-3 (ngspice convention); convert to SI (m^-3) for the Xdep formula.
    double Xdep = std::sqrt(2.0 * EPSSUB * 0.4 / (Q_ELEC * p.NDEP * 1.0e6));
    double T_abk9 = 0.5 * p.K1 / sqrtPhis;
    double T_abk1 = T_abk9 + p.K2;             // no K3B·Vth_NarrowW term
    double T_abk9b = std::sqrt(p.XJ * Xdep);
    double tmp1_abk = Leff + 2.0 * T_abk9b;
    double T_abk5 = Leff / tmp1_abk;
    double tmp2_abk = p.A0 * T_abk5;
    double tmp3_abk = Weff + p.B1;
    double tmp4_abk = (std::abs(tmp3_abk) < 1e-18) ? 0.0 : p.B0 / tmp3_abk;
    double T_abk2 = tmp2_abk + tmp4_abk;

    double Abulk0 = 1.0 + T_abk1 * T_abk2;
    double T_abk7 = T_abk5 * T_abk5 * T_abk5;   // T5^3
    double T_abk8 = p.AGS * p.A0 * T_abk7;
    double dAbulk_dVg = -T_abk1 * T_abk8;
    double Abulk = Abulk0 + dAbulk_dVg * Vgst_eff;

    // Smoothing clamp when Abulk0 or Abulk fall below 0.1
    if (Abulk0 < 0.1) {
        double T9 = 1.0 / (3.0 - 20.0 * Abulk0);
        Abulk0 = (0.2 - Abulk0) * T9;
    }
    if (Abulk < 0.1) {
        double T9 = 1.0 / (3.0 - 20.0 * Abulk);
        Abulk = (0.2 - Abulk) * T9;
    }

    // KETA body-bias modulation on Abulk (with smoothing for Vbs < -0.9/KETA)
    double T_keta = p.KETA * Vbs;
    double T0_keta;
    if (T_keta >= -0.9) {
        T0_keta = 1.0 / (1.0 + T_keta);
    } else {
        double T1_keta = 1.0 / (0.8 + T_keta);
        T0_keta = (17.0 + 20.0 * T_keta) * T1_keta;
    }
    Abulk  *= T0_keta;
    Abulk0 *= T0_keta;

    // --- Rds source/drain series resistance (ngspice b4v7ld.c:1351-1374) ---
    // Formula: Rds = RDSWMIN + 0.5·RDSW·(1/(1+PRWG·Vgsteff) + PRWB·(sqrtPhis-sqrtPhi0) + sqrt((...)² + 0.01))
    // Simplified: drop Vbs dependence of sqrtPhis (we've already used sqrtPhis for Vth).
    // Weff is in metres; RDSW is in Ω·µm, so multiply RDSW by 1e-6 and divide by Weff.
    double Rds = 0.0;
    if (Weff > 1e-18) {
        double T0_rds = 1.0 + p.PRWG * Vgst_eff;
        if (T0_rds < 0.1) T0_rds = 0.1;  // avoid negative / division blow-up
        double T1_rds = p.PRWB * (sqrtPhis - std::sqrt(0.4));  // body-bias term
        double T2_rds = 1.0 / T0_rds + T1_rds;
        double T3_rds = T2_rds + std::sqrt(T2_rds * T2_rds + 0.01);  // smooth max(T2, 0)
        double rds0_ohm_m = (p.RDSW * 1e-6) / Weff;  // convert Ω·µm → Ω for this W
        Rds = p.RDSWMIN * 1e-6 / Weff + 0.5 * rds0_ohm_m * T3_rds;
    }

    // --- Saturation voltage ---
    double Esat = 2.0 * p.VSAT / mu;
    double EsatL = Esat * Leff;
    // Vdsat with Abulk bulk-charge correction (b4v7ld.c:1731-1735, simplified)
    double Vdsat = (EsatL * Vgst_eff) / (Abulk * EsatL + Vgst_eff);

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
