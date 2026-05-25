#pragma once
#include "devices/device.hpp"
#include <memory>
#include <string>
#include <vector>

namespace neospice {

// Forward declarations
struct LTRAModel;

/// Lossy Transmission Line (O element) using convolution-based transient
/// analysis.  Supports RC, RLC, LC, and RG line types.
///
/// SPICE syntax: O<name> p1+ p1- p2+ p2- <modelname>
///
/// Model card: .model <name> LTRA R=val L=val G=val C=val LEN=val
///
/// The implementation is based on the ngspice LTRA device by Jaijeet
/// Roychowdhury.  The transient analysis uses convolution of port voltages
/// and currents with impulse response functions h1', h2, h3'.
///
/// The device introduces two internal branch variables (i1 and i2) as MNA
/// equations, making it a 6-variable element (4 terminal nodes + 2 branch
/// currents).
///
/// DC analysis uses a simplified equivalent circuit:
///   - RLC/LC/RC: simple resistive model (R*L series)
///   - RG: exact DC (cosh/sinh formulation)
///
/// AC analysis uses the exact frequency-domain two-port Y-matrix:
///   Y0(s) = sqrt((sC+G)/(sL+R))
///   lambda(s) = sqrt((sC+G)*(sL+R))
///
/// The AC stamp is frequency-dependent and requires special handling since
/// neospice uses the G+jwC split.  We implement this by stamping directly
/// into the complex Y-matrix during ac_stamp().

// ---- Line type classification ----
enum LTRASpecialCase {
    LTRA_CASE_LTRA = 0,   // general (not supported)
    LTRA_CASE_RLC  = 37,
    LTRA_CASE_RC   = 38,
    LTRA_CASE_RG   = 39,
    LTRA_CASE_LC   = 40,
    LTRA_CASE_RL   = 41,
};

// ---- Interpolation method ----
enum LTRAInterpMethod {
    LTRA_INTERP_LIN   = 34,
    LTRA_INTERP_QUAD  = 35,
    LTRA_INTERP_MIXED = 36,
};

// ---- LTE control type ----
enum LTRALteControl {
    LTRA_CTRL_FULL = 26,
    LTRA_CTRL_HALF = 27,
    LTRA_CTRL_NONE = 28,
};

// ---- Step limit ----
enum LTRAStepLimit {
    LTRA_STEP_LIMIT   = 32,
    LTRA_STEP_NOLIMIT = 33,
};

/// Shared model parameters for lossy transmission line.
struct LTRAModel {
    // Input parameters
    double R = 0.0;       // resistance per unit length
    double L = 0.0;       // inductance per unit length
    double G = 0.0;       // conductance per unit length (shunt)
    double C = 0.0;       // capacitance per unit length
    double len = 0.0;     // line length
    double nl = 0.25;     // normalized length
    double f = 1e9;       // frequency for NL

    bool R_given = false;
    bool L_given = false;
    bool G_given = false;
    bool C_given = false;
    bool len_given = false;

    // Computed parameters
    double td = 0.0;               // propagation delay
    double Z0 = 0.0;               // characteristic impedance
    double Y0 = 0.0;               // characteristic admittance (1/Z0)
    double alpha = 0.0;            // attenuation constant
    double beta = 0.0;             // for RLC: alpha = beta = R/(2L)
    double attenuation = 1.0;      // exp(-beta*td)
    double cByR = 0.0;             // C/R for RC case
    double rclsqr = 0.0;           // R*C*len^2 for RC case

    // DC analysis helpers (RG case)
    double coshlrootGR = 0.0;
    double rRsLrGRorG = 0.0;
    double rGsLrGRorR = 0.0;

    // Integral values of impulse response functions
    double intH1dash = 0.0;
    double intH2 = 0.0;
    double intH3dash = 0.0;

    // Convolution coefficient arrays (model-level, shared across instances)
    std::vector<double> h1dashCoeffs;
    std::vector<double> h2Coeffs;
    std::vector<double> h3dashCoeffs;
    double h1dashFirstCoeff = 0.0;
    double h2FirstCoeff = 0.0;
    double h3dashFirstCoeff = 0.0;
    int auxIndex = 0;

    // Classification and options
    LTRASpecialCase specialCase = LTRA_CASE_LTRA;
    LTRAInterpMethod howToInterp = LTRA_INTERP_QUAD;
    LTRALteControl lteConType = LTRA_CTRL_NONE;
    LTRAStepLimit stepLimit = LTRA_STEP_LIMIT;
    bool truncNR = false;
    bool truncDontCut = false;
    bool printFlag = false;
    double maxSafeStep = 1e30;

    // Tolerance parameters
    double reltol = 1.0;
    double abstol = 1.0;
    double stLineReltol = 0.0;   // set from ckt reltol if 0
    double stLineAbstol = 0.0;   // set from ckt abstol if 0
    double chopReltol = 0.0;
    double chopAbstol = 0.0;

    /// Classify the line type and compute derived parameters.
    /// Returns false if the parameters are invalid.
    bool setup(double ckt_reltol, double ckt_abstol);
};

/// A single lossy transmission line instance.
class LossyTransmissionLine : public Device {
public:
    LossyTransmissionLine(std::string name,
                          int32_t p1_pos, int32_t p1_neg,
                          int32_t p2_pos, int32_t p2_neg,
                          std::shared_ptr<LTRAModel> model);

    // Device interface
    void declare_internal_nodes(Circuit& ckt) override;
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;
    int32_t extra_vars() const override { return 2; }  // two branch currents
    std::vector<int32_t> external_nodes() const override { return {p1p_, p1n_, p2p_, p2n_}; }
    void assign_branch_index(int32_t& next) override;
    std::vector<std::string> output_currents() const override;

    // Transient support
    void accept_step(double time, const std::vector<double>& solution);
    void set_transient(bool enable);
    void init_dc_state(const std::vector<double>& solution);

    // Timestep control
    double compute_trunc_ltra(double current_time, double timestep) const;

    // Timestep control (base class override)
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;

    // Parameter query
    std::optional<double> query_param(const std::string& name) const override;

    // Accessors
    int32_t p1_pos() const { return p1p_; }
    int32_t p1_neg() const { return p1n_; }
    int32_t p2_pos() const { return p2p_; }
    int32_t p2_neg() const { return p2n_; }
    int32_t br_eq1() const { return br1_; }
    int32_t br_eq2() const { return br2_; }
    const LTRAModel& model() const { return *model_; }

    // History accessors (for LTE calculation)
    const std::vector<double>& hist_v1() const { return v1_; }
    const std::vector<double>& hist_i1() const { return i1_; }
    const std::vector<double>& hist_v2() const { return v2_; }
    const std::vector<double>& hist_i2() const { return i2_; }
    const std::vector<double>& hist_times() const { return times_; }
    double init_volt1() const { return initVolt1_; }
    double init_cur1()  const { return initCur1_; }
    double init_volt2() const { return initVolt2_; }
    double init_cur2()  const { return initCur2_; }

    // IC setters (for parser)
    void set_ic(double v1, double i1, double v2, double i2) {
        initVolt1_ = v1; icV1Given_ = true;
        initCur1_  = i1; icC1Given_ = true;
        initVolt2_ = v2; icV2Given_ = true;
        initCur2_  = i2; icC2Given_ = true;
    }
    void set_ic_v1(double v) { initVolt1_ = v; icV1Given_ = true; }
    void set_ic_i1(double v) { initCur1_  = v; icC1Given_ = true; }
    void set_ic_v2(double v) { initVolt2_ = v; icV2Given_ = true; }
    void set_ic_i2(double v) { initCur2_  = v; icC2Given_ = true; }

private:
    int32_t p1p_, p1n_, p2p_, p2n_;  // terminal nodes
    int32_t br1_ = -1, br2_ = -1;   // branch equation indices
    std::shared_ptr<LTRAModel> model_;
    bool transient_ = false;

    // Initial condition values (from OP or user IC)
    double initVolt1_ = 0.0, initCur1_ = 0.0;
    double initVolt2_ = 0.0, initCur2_ = 0.0;
    bool icV1Given_ = false, icC1Given_ = false;
    bool icV2Given_ = false, icC2Given_ = false;

    // Accumulated excitation for RHS
    double input1_ = 0.0, input2_ = 0.0;

    // History arrays for port voltages and currents
    std::vector<double> v1_, i1_, v2_, i2_;
    // Corresponding time points for history (same length as v1_, etc.)
    std::vector<double> times_;

    // Matrix offsets (20 entries for the 6x6 stamp pattern)
    MatrixOffset off_ibr1_pos1_ = -1, off_ibr1_neg1_ = -1;
    MatrixOffset off_ibr1_pos2_ = -1, off_ibr1_neg2_ = -1;
    MatrixOffset off_ibr1_ibr1_ = -1, off_ibr1_ibr2_ = -1;
    MatrixOffset off_ibr2_pos1_ = -1, off_ibr2_neg1_ = -1;
    MatrixOffset off_ibr2_pos2_ = -1, off_ibr2_neg2_ = -1;
    MatrixOffset off_ibr2_ibr1_ = -1, off_ibr2_ibr2_ = -1;
    MatrixOffset off_pos1_ibr1_ = -1, off_neg1_ibr1_ = -1;
    MatrixOffset off_pos2_ibr2_ = -1, off_neg2_ibr2_ = -1;
    MatrixOffset off_pos1_pos1_ = -1, off_neg1_neg1_ = -1;
    MatrixOffset off_pos2_pos2_ = -1, off_neg2_neg2_ = -1;

    // Helper: compute convolution RHS for transient analysis
    void compute_transient_rhs(int timeIndex, double currentTime,
                               const std::vector<double>& timePoints);

    // Helper: interpolate delayed values for LC/RLC cases
    struct InterpCoeffs {
        double qf1 = 0.0, qf2 = 0.0, qf3 = 0.0;
        double lf2 = 0.0, lf3 = 0.0;
        int isaved = 0;
        bool tdover = false;
    };
    InterpCoeffs compute_interp_coeffs(double currentTime,
                                        const std::vector<double>& timePoints,
                                        int timeIndex) const;
};

// ---- Impulse response functions (from ltramisc.c) ----
namespace ltra {

// Bessel functions
double bessI0(double x);
double bessI1(double x);
double bessI1xOverX(double x);

// RLC impulse response functions
double rlcH1dashFunc(double time, double T, double alpha, double beta);
double rlcH2Func(double time, double T, double alpha, double beta);
double rlcH3dashFunc(double time, double T, double alpha, double beta);
double rlcH1dashTwiceIntFunc(double time, double beta);
double rlcH3dashIntFunc(double time, double T, double beta);

// RC impulse response functions
double rcH1dashTwiceIntFunc(double time, double cbyr);
double rcH2TwiceIntFunc(double time, double rclsqr);
double rcH3dashTwiceIntFunc(double time, double cbyr, double rclsqr);

// Integration helper functions
double intlinfunc(double lolimit, double hilimit,
                  double lovalue, double hivalue,
                  double t1, double t2);
double twiceintlinfunc(double lolimit, double hilimit, double otherlolimit,
                       double lovalue, double hivalue,
                       double t1, double t2);
double thriceintlinfunc(double lolimit, double hilimit,
                        double secondlolimit, double thirdlolimit,
                        double lovalue, double hivalue,
                        double t1, double t2);

// Interpolation
int quadInterp(double t, double t1, double t2, double t3,
               double* c1, double* c2, double* c3);
int linInterp(double t, double t1, double t2,
              double* c1, double* c2);

// Coefficient setup
void rcCoeffsSetup(double* h1dashfirstcoeff, double* h2firstcoeff,
                   double* h3dashfirstcoeff,
                   double* h1dashcoeffs, double* h2coeffs,
                   double* h3dashcoeffs,
                   int listsize, double cbyr, double rclsqr,
                   double curtime, const double* timelist,
                   int timeindex, double reltol);

void rlcCoeffsSetup(double* h1dashfirstcoeff, double* h2firstcoeff,
                    double* h3dashfirstcoeff,
                    double* h1dashcoeffs, double* h2coeffs,
                    double* h3dashcoeffs,
                    int listsize, double T, double alpha, double beta,
                    double curtime, const double* timelist,
                    int timeindex, double reltol, int* auxindexptr);

// Straight line check
int straightLineCheck(double x1, double y1, double x2, double y2,
                      double x3, double y3, double reltol, double abstol);

// LTE calculation
double lteCalculate(double currentTime,
                    const LTRAModel& model,
                    const LossyTransmissionLine& inst,
                    const std::vector<double>& timePoints,
                    int timeIndex,
                    const std::vector<double>& rhsOld);

} // namespace ltra

} // namespace neospice
