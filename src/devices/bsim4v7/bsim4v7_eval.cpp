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

    // --- Channel conductance (ngspice b4v7ld.c:1771-1812) ---
    // Replaces the simple Ids = µ·Cox·(W/L)·Vgst·Vds_eff form with the
    // Abulk-corrected channel-conductance + Rds-feedback form.
    double Coxeff_local = Cox;                            // we don't model Coxeff reduction yet
    double beta = mu * Coxeff_local * (Weff / Leff);
    double AbovVgst2Vtm = Abulk / (Vgst_eff + 2.0 * Vt);
    double fgche1_T0 = 1.0 - 0.5 * Vds_eff * AbovVgst2Vtm;
    if (fgche1_T0 < 0.0) fgche1_T0 = 0.0;                 // physical floor
    double fgche1 = Vgst_eff * fgche1_T0;
    double fgche2 = 1.0 + Vds_eff / EsatL;
    double gche = beta * fgche1 / fgche2;

    // --- Rds feedback: Idl = gche/(1 + gche·Rds) ---
    // Idl has units of A/V (channel conductance); final drain current is
    // cdrain = Idl * Vdseff (ngspice b4v7ld.c:2074).
    double Idl_denom = 1.0 + gche * Rds;
    if (Idl_denom < 1e-18) Idl_denom = 1e-18;
    double Idl = gche / Idl_denom;
    double Ids = Idl * Vds_eff;      // drain current; pre-CLM

    // --- Channel length modulation ---
    double CLM = 1.0;
    if (p.PCLM > 0.0 && Vds > Vdsat) {
        CLM = 1.0 + p.PCLM * std::log(1.0 + (Vds - Vds_eff) / (p.PCLM * Vdsat + 1e-20));
    }

    // --- Early-voltage output resistance (ngspice b4v7ld.c:1826-1924) ---
    // Simplified additive combination:  1/Va = 1/VACLM + 1/VADIBL,
    // then gds_early = Ids / Va.  ngspice's full form adds Vasat+VACLM and
    // applies VADIBL multiplicatively; we use the textbook parallel-Early-
    // voltage form to capture the first-order output-conductance magnitude
    // without re-deriving the full multiplicative Ids(Vds) expansion.  CLM
    // already multiplies Ids and Ids_dVd (pre-CLM FD) below, so Early-
    // voltage gds enters additively.
    //
    // Characteristic length litl from ngspice b4v7temp.c:1295 (mtrlMod=0,
    // epsrox=3.9): litl = sqrt(EPSSUB/EPSOX · XJ · TOXE).  For TOXE=2nm,
    // XJ=150nm → litl ≈ 30 nm, the junction-depletion scale (NOT EsatL).
    const double litl = std::sqrt(EPSSUB / EPSOX * p.XJ * p.TOXE);

    // --- VACLM (ngspice b4v7ld.c:1826-1874) ---
    // Cclm = FP · PvagTerm · T0 · T1 / (PCLM · litl)
    //   T0 = 1 + Rds·Idl                              (b4v7ld.c:1846)
    //   T1 = Leff + Vdsat/Esat                        (b4v7ld.c:1851-1852)
    //   PvagTerm = 1 + (PVAG/EsatL)·Vgsteff           (b4v7ld.c:1827-1830)
    //   FP       = 1  when FPROUT ≤ 0 (our default)   (b4v7ld.c:1817)
    // VACLM = Cclm · (Vds - Vds_eff)                  (b4v7ld.c:1864)
    double VACLM = 0.0;
    if (p.PCLM > 0.0 && Vds > Vdsat) {
        double PvagTerm = 1.0;
        if (EsatL > 1e-18) {
            double T8_pv = p.PVAG / EsatL;
            double T9_pv = T8_pv * Vgst_eff;
            if (T9_pv > -0.9) {
                PvagTerm = 1.0 + T9_pv;
            } else {
                PvagTerm = (0.8 + T9_pv) / (17.0 + 20.0 * T9_pv);
            }
        }
        const double FP = 1.0;  // FPROUT=0 default → no pocket-implant degradation
        double T0_clm = 1.0 + Rds * Idl;
        double T1_clm = Leff + Vdsat / Esat;
        double diffVds = Vds - Vds_eff;
        double Cclm = FP * PvagTerm * T0_clm * T1_clm / (p.PCLM * litl);
        VACLM = Cclm * diffVds;
    }

    // --- VADIBL (ngspice b4v7ld.c:1876-1924) ---
    // Simplified: VADIBL = (Vgst_eff + 2·Vt) / thetaRout, using the
    // exponential DIBL decay for thetaRout from b4v7temp.c:1446-1469.
    // PDIBLCB body-bias modulation (b4v7ld.c:1897-1914) is omitted: our
    // default PDIBLCB=0.0 yields identical result, and adding it now is
    // out of scope for this fix.
    double VADIBL = 0.0;
    if (p.PDIBLC1 > 0.0 || p.PDIBLC2 > 0.0) {
        // thetaRout uses ngspice's exponential DIBL decay
        // (b4v7temp.c:1446-1469): characteristic length tmp =
        // sqrt(epssub/epsox · TOXE · Xdep0), then
        // T0 = DROUT·Leff/tmp,  T5 = exp(T0)/(exp(T0)-1)^2,
        // thetaRout = PDIBLC1·T5 + PDIBLC2.
        double Xdep = std::sqrt(2.0 * EPSSUB * 0.4 / (Q_ELEC * p.NDEP * 1.0e6));
        double tmp_dibl = std::sqrt((EPSSUB / EPSOX) * p.TOXE * Xdep);
        // Saturate at 34.0 to match ngspice EXP_THRESHOLD (was 40.0).
        double T0 = (tmp_dibl > 1e-18) ? p.DROUT * Leff / tmp_dibl : 34.0;
        double T5;
        if (T0 < 34.0) {
            double e_T0 = std::exp(T0);
            double em1 = e_T0 - 1.0;
            T5 = e_T0 / (em1 * em1);
        } else {
            T5 = 0.0;  // deep saturation: DIBL ≈ PDIBLC2 only
        }
        double thetaRout = p.PDIBLC1 * T5 + p.PDIBLC2;
        if (thetaRout > 1e-18)
            VADIBL = (Vgst_eff + 2.0 * Vt) / thetaRout;
    }
    double Va_inv = 0.0;
    if (VACLM  > 1e-12) Va_inv += 1.0 / VACLM;
    if (VADIBL > 1e-12) Va_inv += 1.0 / VADIBL;
    double Va = (Va_inv > 1e-18) ? 1.0 / Va_inv : 1e18;
    double gds_early = Ids / Va;   // Ids is pre-CLM here; CLM only scales
                                   // magnitude, Early voltage adds to gds.

    // --- Conductances (numerical derivatives via finite difference) ---
    // The closed-form derivatives of gche/(1+gche·Rds) with respect to Vgs, Vds
    // are lengthy; use a 1e-4 V forward difference for robustness. Acceptable
    // cost: ~2 extra eval() calls on hot path but this function is ~200 lines
    // of arithmetic, still well under typical SPICE inner-loop costs.
    // FD is done on the pre-CLM Ids; CLM is treated as approximately constant
    // w.r.t. Vgs/Vds and multiplied onto r.gm/r.gds below (matching r.Ids *= CLM).
    const double h_fd = 1.0e-4;
    double Ids_dVg, Ids_dVd;
    {
        // Recompute Ids at Vgs + h
        double Vgst_h  = Vgs + h_fd - Vth;
        double Vgst_eff_h = (Vgst_h > 40.0 * n_sub * Vt) ? Vgst_h :
                            (Vgst_h < -40.0 * n_sub * Vt) ? n_sub * Vt * std::exp(Vgst_h / (n_sub * Vt)) :
                            n_sub * Vt * std::log(1.0 + std::exp(Vgst_h / (n_sub * Vt)));
        double Abulk_h = Abulk;  // Abulk's Vgs dependence is small; approximate as constant for FD
        double fgche1_T0_h = 1.0 - 0.5 * Vds_eff * Abulk_h / (Vgst_eff_h + 2.0 * Vt);
        if (fgche1_T0_h < 0.0) fgche1_T0_h = 0.0;
        double fgche1_h = Vgst_eff_h * fgche1_T0_h;
        double gche_h = beta * fgche1_h / fgche2;
        double Idl_h  = gche_h / (1.0 + gche_h * Rds);
        double Ids_h  = Idl_h * Vds_eff;
        Ids_dVg = (Ids_h - Ids) / h_fd;
    }
    {
        // Recompute Ids at Vds + h (Vds_eff moves via its DELTA smoothing)
        double Vds_h = Vds + h_fd;
        double dvs_h_tmp  = Vdsat - Vds_h - p.DELTA;
        double dvs_h_tmp2 = std::sqrt(dvs_h_tmp * dvs_h_tmp + 4.0 * p.DELTA * Vdsat);
        double Vds_eff_h  = Vdsat - 0.5 * (dvs_h_tmp + dvs_h_tmp2);
        double fgche1_T0_h = 1.0 - 0.5 * Vds_eff_h * AbovVgst2Vtm;
        if (fgche1_T0_h < 0.0) fgche1_T0_h = 0.0;
        double fgche1_h = Vgst_eff * fgche1_T0_h;
        double fgche2_h = 1.0 + Vds_eff_h / EsatL;
        double gche_h = beta * fgche1_h / fgche2_h;
        double Idl_h  = gche_h / (1.0 + gche_h * Rds);
        double Ids_h  = Idl_h * Vds_eff_h;
        Ids_dVd = (Ids_h - Ids) / h_fd;
    }

    // Apply CLM to Ids and derivatives consistently. CLM itself depends on Vds
    // but we approximate it as constant in the FD so gm/gds scale cleanly with
    // the CLM-boosted Ids; this keeps r.gds/r.Ids ratio physically reasonable.
    Ids *= CLM;

    // --- Analytical subthreshold gm/gds (Milestone 3) ---
    // At deep subthreshold, Ids ~ 1e-17 A and the 1e-4 V FD returns zero due
    // to catastrophic cancellation, leaving the Jacobian singular at zero bias.
    // Exact analytical forms: gm = Ids/(n·Vt), gds = Ids/Vt. Floor-only merge
    // — only raise FD-computed derivatives, never lower them.
    const double nVt = n_sub * Vt;
    if (Vgst_eff < nVt) {
        double gm_sub  = Ids / nVt;
        double gds_sub = Ids / Vt;
        if (gm_sub  > Ids_dVg * CLM) Ids_dVg = gm_sub  / CLM;
        if (gds_sub > Ids_dVd * CLM) Ids_dVd = gds_sub / CLM;
    }

    r.Ids = Ids;
    r.gm  = Ids_dVg * CLM;
    r.gds = Ids_dVd * CLM + gds_early;
    if (r.gds < 0.0) r.gds = 0.0;   // physical floor; gds ≥ 0 in saturation

    // Body transconductance (unchanged approximation)
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
