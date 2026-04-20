/**
 * Lossy Transmission Line (LTRA) device implementation.
 *
 * Based on the ngspice LTRA device by Jaijeet S. Roychowdhury (1990).
 * Ported to the neospice C++ framework.
 *
 * Copyright 1990 Regents of the University of California.  All rights reserved.
 * Author: 1990 Jaijeet S. Roychowdhury
 */

#include "devices/ltra.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace neospice {

// ========================================================================
// Bessel functions and impulse response helpers (from ltramisc.c)
// ========================================================================
namespace ltra {

double bessI0(double x) {
    double ax, ans, y;
    if ((ax = std::fabs(x)) < 3.75) {
        y = x / 3.75; y *= y;
        ans = 1.0 + y * (3.5156229 + y * (3.0899424 + y * (1.2067492
            + y * (0.2659732 + y * (0.360768e-1 + y * 0.45813e-2)))));
    } else {
        y = 3.75 / ax;
        ans = (std::exp(ax) / std::sqrt(ax)) * (0.39894228 + y * (0.1328592e-1
            + y * (0.225319e-2 + y * (-0.157565e-2 + y * (0.916281e-2
            + y * (-0.2057706e-1 + y * (0.2635537e-1 + y * (-0.1647633e-1
            + y * 0.392377e-2))))))));
    }
    return ans;
}

double bessI1(double x) {
    double ax, ans, y;
    if ((ax = std::fabs(x)) < 3.75) {
        y = x / 3.75; y *= y;
        ans = ax * (0.5 + y * (0.87890594 + y * (0.51498869 + y * (0.15084934
            + y * (0.2658733e-1 + y * (0.301532e-2 + y * 0.32411e-3))))));
    } else {
        y = 3.75 / ax;
        ans = 0.2282967e-1 + y * (-0.2895312e-1 + y * (0.1787654e-1 - y * 0.420059e-2));
        ans = 0.39894228 + y * (-0.3988024e-1 + y * (-0.362018e-2
            + y * (0.163801e-2 + y * (-0.1031555e-1 + y * ans))));
        ans *= (std::exp(ax) / std::sqrt(ax));
    }
    return (x < 0.0 ? -ans : ans);
}

double bessI1xOverX(double x) {
    double ax, ans, y;
    if ((ax = std::fabs(x)) < 3.75) {
        y = x / 3.75; y *= y;
        ans = 0.5 + y * (0.87890594 + y * (0.51498869 + y * (0.15084934
            + y * (0.2658733e-1 + y * (0.301532e-2 + y * 0.32411e-3)))));
    } else {
        y = 3.75 / ax;
        ans = 0.2282967e-1 + y * (-0.2895312e-1 + y * (0.1787654e-1 - y * 0.420059e-2));
        ans = 0.39894228 + y * (-0.3988024e-1 + y * (-0.362018e-2
            + y * (0.163801e-2 + y * (-0.1031555e-1 + y * ans))));
        ans *= (std::exp(ax) / (ax * std::sqrt(ax)));
    }
    return ans;
}

double rlcH1dashFunc(double time, double /*T*/, double alpha, double beta) {
    if (alpha == 0.0) return 0.0;
    double exparg = -beta * time;
    double besselarg = alpha * time;
    return (bessI1(besselarg) - bessI0(besselarg)) * alpha * std::exp(exparg);
}

double rlcH2Func(double time, double T, double alpha, double beta) {
    if (alpha == 0.0 || time < T) return 0.0;
    double besselarg = (time != T) ? alpha * std::sqrt(time * time - T * T) : 0.0;
    double exparg = -beta * time;
    return alpha * alpha * T * std::exp(exparg) * bessI1xOverX(besselarg);
}

double rlcH3dashFunc(double time, double T, double alpha, double beta) {
    if (alpha == 0.0 || time < T) return 0.0;
    double exparg = -beta * time;
    double besselarg = (time != T) ? alpha * std::sqrt(time * time - T * T) : 0.0;
    double returnval = alpha * time * bessI1xOverX(besselarg) - bessI0(besselarg);
    returnval *= alpha * std::exp(exparg);
    return returnval;
}

double rlcH1dashTwiceIntFunc(double time, double beta) {
    if (beta == 0.0) return time;
    double arg = beta * time;
    if (arg == 0.0) return 0.0;
    return (bessI1(arg) + bessI0(arg)) * time * std::exp(-arg) - time;
}

double rlcH3dashIntFunc(double time, double T, double beta) {
    if (time <= T || beta == 0.0) return 0.0;
    double exparg = -beta * time;
    double besselarg = beta * std::sqrt(time * time - T * T);
    return std::exp(exparg) * bessI0(besselarg) - std::exp(-beta * T);
}

double rcH1dashTwiceIntFunc(double time, double cbyr) {
    return std::sqrt(4 * cbyr * time / M_PI);
}

double rcH2TwiceIntFunc(double time, double rclsqr) {
    if (time != 0.0) {
        double temp = rclsqr / (4 * time);
        return (time + rclsqr * 0.5) * std::erfc(std::sqrt(temp))
            - std::sqrt(time * rclsqr / M_PI) * std::exp(-temp);
    }
    return 0.0;
}

double rcH3dashTwiceIntFunc(double time, double cbyr, double rclsqr) {
    if (time != 0.0) {
        double temp = rclsqr / (4 * time);
        double val = 2 * std::sqrt(time / M_PI) * std::exp(-temp)
            - std::sqrt(rclsqr) * std::erfc(std::sqrt(temp));
        return std::sqrt(cbyr) * val;
    }
    return 0.0;
}

double intlinfunc(double lolimit, double hilimit,
                  double lovalue, double hivalue,
                  double t1, double t2) {
    double width = t2 - t1;
    if (width == 0.0) return 0.0;
    double m = (hivalue - lovalue) / width;
    return (hilimit - lolimit) * lovalue + 0.5 * m *
        ((hilimit - t1) * (hilimit - t1) - (lolimit - t1) * (lolimit - t1));
}

double twiceintlinfunc(double lolimit, double hilimit, double otherlolimit,
                       double lovalue, double hivalue,
                       double t1, double t2) {
    double width = t2 - t1;
    if (width == 0.0) return 0.0;
    double m = (hivalue - lovalue) / width;
    double temp1 = hilimit - t1;
    double temp2 = lolimit - t1;
    double temp3 = otherlolimit - t1;
    double dummy = lovalue * ((hilimit - otherlolimit) * (hilimit - otherlolimit) -
        (lolimit - otherlolimit) * (lolimit - otherlolimit));
    dummy += m * ((temp1 * temp1 * temp1 - temp2 * temp2 * temp2) / 3.0 -
        temp3 * temp3 * (hilimit - lolimit));
    return dummy * 0.5;
}

double thriceintlinfunc(double lolimit, double hilimit,
                        double secondlolimit, double thirdlolimit,
                        double lovalue, double hivalue,
                        double t1, double t2) {
    double width = t2 - t1;
    if (width == 0.0) return 0.0;
    double m = (hivalue - lovalue) / width;
    double temp1 = hilimit - t1;
    double temp2 = lolimit - t1;
    double temp3 = secondlolimit - t1;
    double temp4 = thirdlolimit - t1;
    double temp5 = hilimit - thirdlolimit;
    double temp6 = lolimit - thirdlolimit;
    double temp7 = secondlolimit - thirdlolimit;
    double temp8 = hilimit - lolimit;
    double temp9 = hilimit - secondlolimit;
    double temp10 = lolimit - secondlolimit;
    double dummy = lovalue * ((temp5*temp5*temp5 - temp6*temp6*temp6) / 3.0
        - temp7 * temp5 * temp8);
    dummy += m * (((temp1*temp1*temp1*temp1 - temp2*temp2*temp2*temp2) * 0.25
        - temp3*temp3*temp3 * temp8) / 3.0
        - temp4*temp4 * 0.5 * (temp9*temp9 - temp10*temp10));
    return dummy * 0.5;
}

int quadInterp(double t, double t1, double t2, double t3,
               double* c1, double* c2, double* c3) {
    if (t == t1) { *c1 = 1; *c2 = 0; *c3 = 0; return 0; }
    if (t == t2) { *c1 = 0; *c2 = 1; *c3 = 0; return 0; }
    if (t == t3) { *c1 = 0; *c2 = 0; *c3 = 1; return 0; }
    if ((t2-t1)==0 || (t3-t2)==0 || (t1-t3)==0) return 1;
    double f1 = (t-t2)*(t-t3), f2 = (t-t1)*(t-t3), f3 = (t-t1)*(t-t2);
    if ((t2-t1) != 0) { f1 /= (t1-t2); f2 /= (t2-t1); }
    else { f1 = 0; f2 = 0; }
    if ((t3-t2) != 0) { f2 /= (t2-t3); f3 /= (t2-t3); }
    else { f2 = 0; f3 = 0; }
    if ((t3-t1) != 0) { f1 /= (t1-t3); f3 /= (t1-t3); }
    else { f1 = 0; f2 = 0; }
    *c1 = f1; *c2 = f2; *c3 = f3;
    return 0;
}

int linInterp(double t, double t1, double t2, double* c1, double* c2) {
    if (t1 == t2) return 1;
    if (t == t1) { *c1 = 1; *c2 = 0; return 0; }
    if (t == t2) { *c1 = 0; *c2 = 1; return 0; }
    double temp = (t - t1) / (t2 - t1);
    *c2 = temp; *c1 = 1 - temp;
    return 0;
}

void rcCoeffsSetup(double* h1dashfirstcoeff, double* h2firstcoeff,
                   double* h3dashfirstcoeff,
                   double* h1dashcoeffs, double* h2coeffs,
                   double* h3dashcoeffs,
                   int /*listsize*/, double cbyr, double rclsqr,
                   double curtime, const double* timelist,
                   int timeindex, double reltol) {
    int auxindex = timeindex;
    double delta1 = curtime - timelist[auxindex];
    double lolimit1 = 0.0;
    double hilimit1 = delta1;

    double h1lovalue1 = 0.0;
    double h1hivalue1 = std::sqrt(4 * cbyr * hilimit1 / M_PI);
    double h1dummy1 = h1hivalue1 / delta1;
    *h1dashfirstcoeff = h1dummy1;
    double h1relval = std::fabs(h1dummy1 * reltol);

    double temp = rclsqr / (4 * hilimit1);
    double temp2 = (temp >= 100.0 ? 0.0 : std::erfc(std::sqrt(temp)));
    double temp3 = std::exp(-temp);
    double temp4 = std::sqrt(rclsqr);
    double temp5 = std::sqrt(cbyr);

    double h2lovalue1 = 0.0;
    double h2hivalue1 = (hilimit1 != 0.0) ?
        (hilimit1 + rclsqr * 0.5) * temp2 - std::sqrt(hilimit1 * rclsqr / M_PI) * temp3 : 0.0;
    double h2dummy1 = h2hivalue1 / delta1;
    *h2firstcoeff = h2dummy1;
    double h2relval = std::fabs(h2dummy1 * reltol);

    double h3lovalue1 = 0.0;
    double h3hivalue1 = (hilimit1 != 0.0) ?
        temp5 * (2 * std::sqrt(hilimit1 / M_PI) * temp3 - temp4 * temp2) : 0.0;
    double h3dummy1 = h3hivalue1 / delta1;
    *h3dashfirstcoeff = h3dummy1;
    double h3relval = std::fabs(h3dummy1 * reltol);

    int doh1 = 1, doh2 = 1, doh3 = 1;

    for (int i = auxindex; i > 0; i--) {
        double delta2 = delta1;
        (void)delta2;
        delta1 = timelist[i] - timelist[i - 1];
        lolimit1 = hilimit1;
        hilimit1 = curtime - timelist[i - 1];

        if (doh1) {
            double h1hivalue2 = h1hivalue1;
            double h1dummy2 = h1dummy1;
            h1lovalue1 = h1hivalue2;
            h1hivalue1 = std::sqrt(4 * cbyr * hilimit1 / M_PI);
            h1dummy1 = (h1hivalue1 - h1lovalue1) / delta1;
            h1dashcoeffs[i] = h1dummy1 - h1dummy2;
            if (std::fabs(h1dashcoeffs[i]) < h1relval) doh1 = 0;
        } else {
            h1dashcoeffs[i] = 0.0;
        }

        if (doh2 || doh3) {
            temp = rclsqr / (4 * hilimit1);
            temp2 = (temp >= 100.0 ? 0.0 : std::erfc(std::sqrt(temp)));
            temp3 = std::exp(-temp);
        }

        if (doh2) {
            double h2hivalue2 = h2hivalue1;
            double h2dummy2 = h2dummy1;
            h2lovalue1 = h2hivalue2;
            h2hivalue1 = (hilimit1 != 0.0) ?
                (hilimit1 + rclsqr * 0.5) * temp2 - std::sqrt(hilimit1 * rclsqr / M_PI) * temp3 : 0.0;
            h2dummy1 = (h2hivalue1 - h2lovalue1) / delta1;
            h2coeffs[i] = h2dummy1 - h2dummy2;
            if (std::fabs(h2coeffs[i]) < h2relval) doh2 = 0;
        } else {
            h2coeffs[i] = 0.0;
        }

        if (doh3) {
            double h3hivalue2 = h3hivalue1;
            double h3dummy2 = h3dummy1;
            h3lovalue1 = h3hivalue2;
            h3hivalue1 = (hilimit1 != 0.0) ?
                temp5 * (2 * std::sqrt(hilimit1 / M_PI) * temp3 - temp4 * temp2) : 0.0;
            h3dummy1 = (h3hivalue1 - h3lovalue1) / delta1;
            h3dashcoeffs[i] = h3dummy1 - h3dummy2;
            if (std::fabs(h3dashcoeffs[i]) < h3relval) doh3 = 0;
        } else {
            h3dashcoeffs[i] = 0.0;
        }
    }
}

void rlcCoeffsSetup(double* h1dashfirstcoeff, double* h2firstcoeff,
                    double* h3dashfirstcoeff,
                    double* h1dashcoeffs, double* h2coeffs,
                    double* h3dashcoeffs,
                    int /*listsize*/, double T, double alpha, double beta,
                    double curtime, const double* timelist,
                    int timeindex, double reltol, int* auxindexptr) {
    int auxindex;

    if (T == 0.0) {
        auxindex = timeindex;
    } else {
        if (curtime - T <= 0.0) {
            auxindex = 0;
        } else {
            unsigned exact = 0;
            int i;
            for (i = timeindex; i >= 0; i--) {
                if (curtime - timelist[i] == T) { exact = 1; break; }
                if (curtime - timelist[i] > T) break;
            }
            auxindex = exact ? i - 1 : i;
        }
    }

    // h2 and h3dash first coefficients
    double besselarg = 0.0, exparg, expterm, bessi1overxterm, bessi0term;
    double alphasqTterm = 0.0, expbetaTterm = 0.0;
    double h2lovalue1 = 0.0, h2hivalue1 = 0.0, h2dummy1 = 0.0;
    double h3lovalue1 = 0.0, h3hivalue1 = 0.0, h3dummy1 = 0.0;
    double h2relval = 0.0, h3relval = 0.0;
    double lolimit1, hilimit1, delta1;

    if (auxindex != 0) {
        lolimit1 = T;
        hilimit1 = curtime - timelist[auxindex];
        delta1 = hilimit1 - lolimit1;

        h2lovalue1 = rlcH2Func(T, T, alpha, beta);
        besselarg = (hilimit1 > T) ? alpha * std::sqrt(hilimit1*hilimit1 - T*T) : 0.0;
        exparg = -beta * hilimit1;
        expterm = std::exp(exparg);
        bessi1overxterm = bessI1xOverX(besselarg);
        alphasqTterm = alpha * alpha * T;
        h2hivalue1 = ((alpha == 0.0) || (hilimit1 < T)) ? 0.0 :
            alphasqTterm * expterm * bessi1overxterm;
        h2dummy1 = twiceintlinfunc(lolimit1, hilimit1, lolimit1,
            h2lovalue1, h2hivalue1, lolimit1, hilimit1) / delta1;
        *h2firstcoeff = h2dummy1;
        h2relval = std::fabs(reltol * h2dummy1);

        h3lovalue1 = 0.0;
        bessi0term = bessI0(besselarg);
        expbetaTterm = std::exp(-beta * T);
        h3hivalue1 = ((hilimit1 <= T) || (beta == 0.0)) ? 0.0 :
            expterm * bessi0term - expbetaTterm;
        h3dummy1 = intlinfunc(lolimit1, hilimit1, h3lovalue1,
            h3hivalue1, lolimit1, hilimit1) / delta1;
        *h3dashfirstcoeff = h3dummy1;
        h3relval = std::fabs(h3dummy1 * reltol);
    } else {
        *h2firstcoeff = *h3dashfirstcoeff = 0.0;
    }

    // h1dash first coefficient
    lolimit1 = 0.0;
    hilimit1 = curtime - timelist[timeindex];
    delta1 = hilimit1 - lolimit1;
    exparg = -beta * hilimit1;
    expterm = std::exp(exparg);

    double h1lovalue1 = 0.0;
    double h1hivalue1 = (beta == 0.0) ? hilimit1 :
        ((hilimit1 == 0.0) ? 0.0 :
        (bessI1(-exparg) + bessI0(-exparg)) * hilimit1 * expterm - hilimit1);
    double h1dummy1 = h1hivalue1 / delta1;
    *h1dashfirstcoeff = h1dummy1;
    double h1relval = std::fabs(h1dummy1 * reltol);

    int doh1 = 1, doh2 = 1, doh3 = 1;
    double lolimit2, hilimit2, delta2;

    for (int i = timeindex; i > 0; i--) {
        if (doh1 || doh2 || doh3) {
            lolimit2 = lolimit1;
            hilimit2 = hilimit1;
            delta2 = delta1;
            (void)delta2;

            lolimit1 = hilimit2;
            hilimit1 = curtime - timelist[i - 1];
            delta1 = timelist[i] - timelist[i - 1];

            exparg = -beta * hilimit1;
            expterm = std::exp(exparg);
        }

        if (doh1) {
            double h1hivalue2 = h1hivalue1;
            double h1dummy2 = h1dummy1;
            h1lovalue1 = h1hivalue2;
            h1hivalue1 = (beta == 0.0) ? hilimit1 :
                ((hilimit1 == 0.0) ? 0.0 :
                (bessI1(-exparg) + bessI0(-exparg)) * hilimit1 * expterm - hilimit1);
            h1dummy1 = (h1hivalue1 - h1lovalue1) / delta1;
            h1dashcoeffs[i] = h1dummy1 - h1dummy2;
            if (std::fabs(h1dashcoeffs[i]) <= h1relval) doh1 = 0;
        } else {
            h1dashcoeffs[i] = 0.0;
        }

        if (i <= auxindex) {
            if (doh2 || doh3)
                besselarg = (hilimit1 > T) ? alpha * std::sqrt(hilimit1*hilimit1 - T*T) : 0.0;

            if (doh2) {
                double h2hivalue2 = h2hivalue1;
                double h2dummy2 = h2dummy1;
                h2lovalue1 = h2hivalue2;
                bessi1overxterm = bessI1xOverX(besselarg);
                h2hivalue1 = ((alpha == 0.0) || (hilimit1 < T)) ? 0.0 :
                    alphasqTterm * expterm * bessi1overxterm;
                h2dummy1 = twiceintlinfunc(lolimit1, hilimit1, lolimit1,
                    h2lovalue1, h2hivalue1, lolimit1, hilimit1) / delta1;
                h2coeffs[i] = h2dummy1 - h2dummy2 +
                    intlinfunc(lolimit2, hilimit2, h2lovalue1, h2hivalue1,
                               lolimit2, hilimit2);
                if (std::fabs(h2coeffs[i]) <= h2relval) doh2 = 0;
            } else {
                h2coeffs[i] = 0.0;
            }

            if (doh3) {
                double h3hivalue2 = h3hivalue1;
                double h3dummy2 = h3dummy1;
                h3lovalue1 = h3hivalue2;
                bessi0term = bessI0(besselarg);
                h3hivalue1 = ((hilimit1 <= T) || (beta == 0.0)) ? 0.0 :
                    expterm * bessi0term - expbetaTterm;
                h3dummy1 = intlinfunc(lolimit1, hilimit1, h3lovalue1,
                    h3hivalue1, lolimit1, hilimit1) / delta1;
                h3dashcoeffs[i] = h3dummy1 - h3dummy2;
                if (std::fabs(h3dashcoeffs[i]) <= h3relval) doh3 = 0;
            } else {
                h3dashcoeffs[i] = 0.0;
            }
        }
    }
    *auxindexptr = auxindex;
}

int straightLineCheck(double x1, double y1, double x2, double y2,
                      double x3, double y3, double reltol, double abstol) {
    double QUADarea1 = (std::fabs(y2) + std::fabs(y1)) * 0.5 * std::fabs(x2 - x1);
    double QUADarea2 = (std::fabs(y3) + std::fabs(y2)) * 0.5 * std::fabs(x3 - x2);
    double QUADarea3 = (std::fabs(y3) + std::fabs(y1)) * 0.5 * std::fabs(x3 - x1);
    double TRarea = std::fabs(QUADarea3 - QUADarea1 - QUADarea2);
    double area = QUADarea1 + QUADarea2;
    return (area * reltol + abstol > TRarea) ? 1 : 0;
}

// LTE calculation (from ltramisc.c LTRAlteCalculate)
double lteCalculate(double curtime,
                    const LTRAModel& model,
                    const LossyTransmissionLine& inst,
                    const std::vector<double>& timePoints,
                    int timeIndex,
                    const std::vector<double>& /*rhsOld*/) {
    // Placeholder — the full LTE calculation requires access to the instance
    // history arrays which are private. For now, return 0 (no LTE constraint
    // from convolution).  The timestep is still limited by the delay-based
    // truncation in compute_trunc_ltra().
    (void)curtime; (void)model; (void)inst; (void)timePoints; (void)timeIndex;
    return 0.0;
}

} // namespace ltra

// ========================================================================
// LTRAModel::setup()
// ========================================================================

bool LTRAModel::setup(double ckt_reltol, double ckt_abstol) {
    if (stLineReltol == 0.0) stLineReltol = ckt_reltol;
    if (stLineAbstol == 0.0) stLineAbstol = ckt_abstol;

    if (!len_given) return false; // length required

    // Classify line type
    bool hasR = (R != 0.0);
    bool hasG = (G != 0.0);
    bool hasL = (L != 0.0);
    bool hasC = (C != 0.0);

    int paramCount = (hasR?1:0) + (hasG?1:0) + (hasL?1:0) + (hasC?1:0);
    if (paramCount <= 1) return false; // need at least 2 params

    if (!hasR && !hasG && hasC && hasL) {
        specialCase = LTRA_CASE_LC;
    } else if (hasR && !hasG && hasC && hasL) {
        specialCase = LTRA_CASE_RLC;
    } else if (hasR && !hasG && hasC && !hasL) {
        specialCase = LTRA_CASE_RC;
    } else if (hasR && !hasG && !hasC && hasL) {
        return false; // RL not supported
    } else if (hasR && hasG && !hasC && !hasL) {
        specialCase = LTRA_CASE_RG;
    } else if (hasG && (hasC || hasL)) {
        return false; // nonzero G (except RG) not supported
    } else {
        return false;
    }

    // Set interpolation defaults
    if (howToInterp != LTRA_INTERP_LIN &&
        howToInterp != LTRA_INTERP_QUAD &&
        howToInterp != LTRA_INTERP_MIXED) {
        howToInterp = LTRA_INTERP_QUAD;
    }

    // Compute derived parameters
    switch (specialCase) {
    case LTRA_CASE_LC:
        Z0 = std::sqrt(L / C);
        Y0 = 1.0 / Z0;
        td = std::sqrt(L * C) * len;
        attenuation = 1.0;
        break;

    case LTRA_CASE_RLC:
        Z0 = std::sqrt(L / C);
        Y0 = 1.0 / Z0;
        td = std::sqrt(L * C) * len;
        alpha = 0.5 * (R / L);
        beta = alpha;
        attenuation = std::exp(-beta * td);
        if (alpha > 0.0) {
            intH1dash = -1.0;
            intH2 = 1.0 - attenuation;
            intH3dash = -attenuation;
        } else {
            intH1dash = intH2 = intH3dash = 0.0;
        }

        if (!truncDontCut) {
            // Find maximum safe step for impulse response accuracy
            double xbig = td + 9 * td;  // td + large margin
            double xsmall = td;
            double xmid = 0.5 * (xbig + xsmall);
            int maxiter = 50, iters = 0;
            for (;;) {
                iters++;
                double y1big = ltra::rlcH2Func(xbig, td, alpha, beta);
                double y1mid = ltra::rlcH2Func(xmid, td, alpha, beta);
                double y1small = ltra::rlcH2Func(xsmall, td, alpha, beta);
                double y2big = ltra::rlcH3dashFunc(xbig, td, beta, beta);
                double y2mid = ltra::rlcH3dashFunc(xmid, td, beta, beta);
                double y2small = ltra::rlcH3dashFunc(xsmall, td, beta, beta);
                int done = ltra::straightLineCheck(xbig, y1big, xmid, y1mid, xsmall,
                    y1small, stLineReltol, stLineAbstol) +
                    ltra::straightLineCheck(xbig, y2big, xmid, y2mid, xsmall,
                    y2small, stLineReltol, stLineAbstol);
                if (done == 2 || iters > maxiter) break;
                xbig = xmid;
                xmid = 0.5 * (xbig + xsmall);
            }
            maxSafeStep = xbig - td;
        }
        break;

    case LTRA_CASE_RC:
        cByR = C / R;
        rclsqr = R * C * len * len;
        intH1dash = 0.0;
        intH2 = 1.0;
        intH3dash = 0.0;
        break;

    case LTRA_CASE_RG:
        // DC parameters computed in load
        break;

    default:
        return false;
    }

    return true;
}

// ========================================================================
// LossyTransmissionLine implementation
// ========================================================================

LossyTransmissionLine::LossyTransmissionLine(
    std::string name,
    int32_t p1_pos, int32_t p1_neg,
    int32_t p2_pos, int32_t p2_neg,
    std::shared_ptr<LTRAModel> model)
    : Device(std::move(name)),
      p1p_(p1_pos), p1n_(p1_neg),
      p2p_(p2_pos), p2n_(p2_neg),
      model_(std::move(model))
{
}

void LossyTransmissionLine::declare_internal_nodes(Circuit& /*ckt*/) {
    // Branch equations are handled via extra_vars() + assign_branch_index()
}

void LossyTransmissionLine::assign_branch_index(int32_t& next) {
    br1_ = next++;
    br2_ = next++;
}

std::vector<std::string> LossyTransmissionLine::output_currents() const {
    return { name() + "#branch1", name() + "#branch2" };
}

void LossyTransmissionLine::stamp_pattern(SparsityBuilder& builder) const {
    // Stamp all 20 matrix entries for the 6-variable MNA formulation
    // ibr1 row: pos1, neg1, pos2, neg2, ibr1, ibr2
    stamp_if_not_ground(builder, br1_, p1p_);
    stamp_if_not_ground(builder, br1_, p1n_);
    stamp_if_not_ground(builder, br1_, p2p_);
    stamp_if_not_ground(builder, br1_, p2n_);
    builder.add(br1_, br1_);
    builder.add(br1_, br2_);

    // ibr2 row: pos1, neg1, pos2, neg2, ibr1, ibr2
    stamp_if_not_ground(builder, br2_, p1p_);
    stamp_if_not_ground(builder, br2_, p1n_);
    stamp_if_not_ground(builder, br2_, p2p_);
    stamp_if_not_ground(builder, br2_, p2n_);
    builder.add(br2_, br1_);
    builder.add(br2_, br2_);

    // pos1 row: ibr1
    stamp_if_not_ground(builder, p1p_, br1_);
    // neg1 row: ibr1
    stamp_if_not_ground(builder, p1n_, br1_);
    // pos2 row: ibr2
    stamp_if_not_ground(builder, p2p_, br2_);
    // neg2 row: ibr2
    stamp_if_not_ground(builder, p2n_, br2_);

    // Diagonal entries for pos1, neg1, pos2, neg2 (for preordering)
    stamp_if_not_ground(builder, p1p_, p1p_);
    stamp_if_not_ground(builder, p1n_, p1n_);
    stamp_if_not_ground(builder, p2p_, p2p_);
    stamp_if_not_ground(builder, p2n_, p2n_);
}

void LossyTransmissionLine::assign_offsets(const SparsityPattern& pattern) {
    off_ibr1_pos1_ = offset_if_not_ground(pattern, br1_, p1p_);
    off_ibr1_neg1_ = offset_if_not_ground(pattern, br1_, p1n_);
    off_ibr1_pos2_ = offset_if_not_ground(pattern, br1_, p2p_);
    off_ibr1_neg2_ = offset_if_not_ground(pattern, br1_, p2n_);
    off_ibr1_ibr1_ = pattern.offset(br1_, br1_);
    off_ibr1_ibr2_ = pattern.offset(br1_, br2_);

    off_ibr2_pos1_ = offset_if_not_ground(pattern, br2_, p1p_);
    off_ibr2_neg1_ = offset_if_not_ground(pattern, br2_, p1n_);
    off_ibr2_pos2_ = offset_if_not_ground(pattern, br2_, p2p_);
    off_ibr2_neg2_ = offset_if_not_ground(pattern, br2_, p2n_);
    off_ibr2_ibr1_ = pattern.offset(br2_, br1_);
    off_ibr2_ibr2_ = pattern.offset(br2_, br2_);

    off_pos1_ibr1_ = offset_if_not_ground(pattern, p1p_, br1_);
    off_neg1_ibr1_ = offset_if_not_ground(pattern, p1n_, br1_);
    off_pos2_ibr2_ = offset_if_not_ground(pattern, p2p_, br2_);
    off_neg2_ibr2_ = offset_if_not_ground(pattern, p2n_, br2_);

    off_pos1_pos1_ = offset_if_not_ground(pattern, p1p_, p1p_);
    off_neg1_neg1_ = offset_if_not_ground(pattern, p1n_, p1n_);
    off_pos2_pos2_ = offset_if_not_ground(pattern, p2p_, p2p_);
    off_neg2_neg2_ = offset_if_not_ground(pattern, p2n_, p2n_);
}

// ---------------------------------------------------------------------------
// evaluate — the main load function
// ---------------------------------------------------------------------------
void LossyTransmissionLine::evaluate(
    const std::vector<double>& voltages,
    NumericMatrix& mat, std::vector<double>& rhs)
{
    const auto& m = *model_;

    bool is_dc = !transient_;
    bool is_tran = transient_ && tls_integrator_ctx != nullptr;

    if (is_dc || m.specialCase == LTRA_CASE_RG) {
        // --- DC stamping ---
        switch (m.specialCase) {
        case LTRA_CASE_RG: {
            // Compute DC parameters for RG case
            double dummy1_val = m.len * std::sqrt(m.R * m.G);
            double dummy2_val = std::exp(-dummy1_val);
            dummy1_val = std::exp(dummy1_val);
            double coshlrootGR = 0.5 * (dummy1_val + dummy2_val);
            double rRsLrGRorG = (m.G <= 1.0e-10) ? m.len * m.R :
                0.5 * (dummy1_val - dummy2_val) * std::sqrt(m.R / m.G);
            double rGsLrGRorR = (m.R <= 1.0e-10) ? m.len * m.G :
                0.5 * (dummy1_val - dummy2_val) * std::sqrt(m.G / m.R);

            double gmin = 1e-12;  // small conductance for numerical stability

            add_if_valid(mat, off_ibr1_pos1_,  1.0);
            add_if_valid(mat, off_ibr1_neg1_, -1.0);
            add_if_valid(mat, off_ibr1_pos2_, -coshlrootGR);
            add_if_valid(mat, off_ibr1_neg2_,  coshlrootGR);
            add_if_valid(mat, off_ibr1_ibr2_,  (1.0 + gmin) * rRsLrGRorG);

            add_if_valid(mat, off_ibr2_ibr2_,  coshlrootGR);
            add_if_valid(mat, off_ibr2_pos2_, -(1.0 + gmin) * rGsLrGRorR);
            add_if_valid(mat, off_ibr2_neg2_,  (1.0 + gmin) * rGsLrGRorR);
            add_if_valid(mat, off_ibr2_ibr1_,  1.0);

            add_if_valid(mat, off_pos1_ibr1_,  1.0);
            add_if_valid(mat, off_neg1_ibr1_, -1.0);
            add_if_valid(mat, off_pos2_ibr2_,  1.0);
            add_if_valid(mat, off_neg2_ibr2_, -1.0);

            input1_ = input2_ = 0.0;
            break;
        }

        case LTRA_CASE_LC:
        case LTRA_CASE_RLC:
        case LTRA_CASE_RC: {
            // Simple resistive model for DC
            add_if_valid(mat, off_pos1_ibr1_,  1.0);
            add_if_valid(mat, off_neg1_ibr1_, -1.0);
            add_if_valid(mat, off_pos2_ibr2_,  1.0);
            add_if_valid(mat, off_neg2_ibr2_, -1.0);

            add_if_valid(mat, off_ibr1_ibr1_,  1.0);
            add_if_valid(mat, off_ibr1_ibr2_,  1.0);
            add_if_valid(mat, off_ibr2_pos1_,  1.0);
            add_if_valid(mat, off_ibr2_pos2_, -1.0);
            add_if_valid(mat, off_ibr2_ibr1_, -m.R * m.len);

            input1_ = input2_ = 0.0;
            break;
        }

        default:
            break;
        }
        return;
    }

    // --- Transient stamping ---
    if (!is_tran) return;

    const auto& ctx = *tls_integrator_ctx;
    double currentTime = ctx.current_time;

    // Get time points from the integrator context
    // We use the history arrays to track time points
    int timeIndex = static_cast<int>(v1_.size()) - 1;
    if (timeIndex < 0) timeIndex = 0;

    bool initTran = (ctx.mode & 0x1000) != 0;  // MODEINITTRAN
    bool initPred = (ctx.mode & 0x20) != 0;     // MODEINITPRED

    // Save initial conditions at start of transient
    if (initTran) {
        if (!(ctx.mode & 0x10000)) { // !MODEUIC
            double vp1p = (p1p_ >= 0) ? voltages[p1p_] : 0.0;
            double vp1n = (p1n_ >= 0) ? voltages[p1n_] : 0.0;
            double vp2p = (p2p_ >= 0) ? voltages[p2p_] : 0.0;
            double vp2n = (p2n_ >= 0) ? voltages[p2n_] : 0.0;
            initVolt1_ = vp1p - vp1n;
            initVolt2_ = vp2p - vp2n;
            initCur1_ = (br1_ >= 0 && br1_ < (int32_t)voltages.size()) ? voltages[br1_] : 0.0;
            initCur2_ = (br2_ >= 0 && br2_ < (int32_t)voltages.size()) ? voltages[br2_] : 0.0;
        }
    }

    // Matrix stamps (depend on model type)
    switch (m.specialCase) {
    case LTRA_CASE_RLC: {
        // Convolution terms first coefficients
        double d1 = m.Y0 * m.h1dashFirstCoeff;
        add_if_valid(mat, off_ibr1_pos1_,  d1);
        add_if_valid(mat, off_ibr1_neg1_, -d1);
        add_if_valid(mat, off_ibr2_pos2_,  d1);
        add_if_valid(mat, off_ibr2_neg2_, -d1);
    }
    // Fall through to LC (lossless-like parts)
    [[fallthrough]];
    case LTRA_CASE_LC:
        add_if_valid(mat, off_ibr1_pos1_,  m.Y0);
        add_if_valid(mat, off_ibr1_neg1_, -m.Y0);
        add_if_valid(mat, off_ibr1_ibr1_, -1.0);
        add_if_valid(mat, off_pos1_ibr1_,  1.0);
        add_if_valid(mat, off_neg1_ibr1_, -1.0);

        add_if_valid(mat, off_ibr2_pos2_,  m.Y0);
        add_if_valid(mat, off_ibr2_neg2_, -m.Y0);
        add_if_valid(mat, off_ibr2_ibr2_, -1.0);
        add_if_valid(mat, off_pos2_ibr2_,  1.0);
        add_if_valid(mat, off_neg2_ibr2_, -1.0);
        break;

    case LTRA_CASE_RC: {
        // Non-convolution parts
        add_if_valid(mat, off_ibr1_ibr1_, -1.0);
        add_if_valid(mat, off_pos1_ibr1_,  1.0);
        add_if_valid(mat, off_neg1_ibr1_, -1.0);

        add_if_valid(mat, off_ibr2_ibr2_, -1.0);
        add_if_valid(mat, off_pos2_ibr2_,  1.0);
        add_if_valid(mat, off_neg2_ibr2_, -1.0);

        // Convolution first terms
        double d1 = m.h1dashFirstCoeff;
        add_if_valid(mat, off_ibr1_pos1_,  d1);
        add_if_valid(mat, off_ibr1_neg1_, -d1);
        add_if_valid(mat, off_ibr2_pos2_,  d1);
        add_if_valid(mat, off_ibr2_neg2_, -d1);

        d1 = m.h2FirstCoeff;
        add_if_valid(mat, off_ibr1_ibr2_, -d1);
        add_if_valid(mat, off_ibr2_ibr1_, -d1);

        d1 = m.h3dashFirstCoeff;
        add_if_valid(mat, off_ibr1_pos2_, -d1);
        add_if_valid(mat, off_ibr1_neg2_,  d1);
        add_if_valid(mat, off_ibr2_pos1_, -d1);
        add_if_valid(mat, off_ibr2_neg1_,  d1);
        break;
    }
    default:
        break;
    }

    // RHS loading (only on INITPRED or INITRAN, i.e., first NR iteration)
    if (initPred || initTran) {
        input1_ = input2_ = 0.0;

        bool tdover = false;
        double v1d = 0.0, v2d = 0.0, i1d = 0.0, i2d = 0.0;

        // Interpolation for delayed values (LC/RLC cases)
        if (m.specialCase == LTRA_CASE_LC || m.specialCase == LTRA_CASE_RLC) {
            if (currentTime > m.td && !v1_.empty()) {
                tdover = true;

                // Find the time index just before (currentTime - td)
                double delayed_t = currentTime - m.td;
                int isaved = -1;
                // We need the timePoints array - reconstruct from history
                // The accept_step stores v1,i1,v2,i2 at each accepted timepoint
                // We need time points array - store separately
                // For now, use linear interpolation from history
                // Find bracket in our internal time tracking
                // NOTE: in the full integration, time points come from the transient solver
                // For now we use the accept_step history
            }
        }

        // Compute convolution contributions for RLC case
        if (m.specialCase == LTRA_CASE_RLC && timeIndex > 0) {
            // Convolution of h1dash with v1 and v2
            double d1 = 0.0, d2 = 0.0;
            for (int i = timeIndex; i > 0; i--) {
                if (i < (int)m.h1dashCoeffs.size() && m.h1dashCoeffs[i] != 0.0) {
                    d1 += m.h1dashCoeffs[i] * (v1_[i] - initVolt1_);
                    d2 += m.h1dashCoeffs[i] * (v2_[i] - initVolt2_);
                }
            }
            d1 += initVolt1_ * m.intH1dash;
            d2 += initVolt2_ * m.intH1dash;
            d1 -= initVolt1_ * m.h1dashFirstCoeff;
            d2 -= initVolt2_ * m.h1dashFirstCoeff;
            input1_ -= d1 * m.Y0;
            input2_ -= d2 * m.Y0;

            // Convolution of h2 with i2 and i1
            d1 = d2 = 0.0;
            if (tdover) {
                d1 = (i2d - initCur2_) * m.h2FirstCoeff;
                d2 = (i1d - initCur1_) * m.h2FirstCoeff;
                for (int i = m.auxIndex; i > 0; i--) {
                    if (i < (int)m.h2Coeffs.size() && m.h2Coeffs[i] != 0.0) {
                        d1 += m.h2Coeffs[i] * (i2_[i] - initCur2_);
                        d2 += m.h2Coeffs[i] * (i1_[i] - initCur1_);
                    }
                }
            }
            d1 += initCur2_ * m.intH2;
            d2 += initCur1_ * m.intH2;
            input1_ += d1;
            input2_ += d2;

            // Convolution of h3dash with v2 and v1
            d1 = d2 = 0.0;
            if (tdover) {
                d1 = (v2d - initVolt2_) * m.h3dashFirstCoeff;
                d2 = (v1d - initVolt1_) * m.h3dashFirstCoeff;
                for (int i = m.auxIndex; i > 0; i--) {
                    if (i < (int)m.h3dashCoeffs.size() && m.h3dashCoeffs[i] != 0.0) {
                        d1 += m.h3dashCoeffs[i] * (v2_[i] - initVolt2_);
                        d2 += m.h3dashCoeffs[i] * (v1_[i] - initVolt1_);
                    }
                }
            }
            d1 += initVolt2_ * m.intH3dash;
            d2 += initVolt1_ * m.intH3dash;
            input1_ += m.Y0 * d1;
            input2_ += m.Y0 * d2;
        }

        // Lossless-like parts for LC and RLC
        if (m.specialCase == LTRA_CASE_LC || m.specialCase == LTRA_CASE_RLC) {
            if (!tdover) {
                input1_ += m.attenuation * (initVolt2_ * m.Y0 + initCur2_);
                input2_ += m.attenuation * (initVolt1_ * m.Y0 + initCur1_);
            } else {
                input1_ += m.attenuation * (v2d * m.Y0 + i2d);
                input2_ += m.attenuation * (v1d * m.Y0 + i1d);
            }
        }

        // RC convolution
        if (m.specialCase == LTRA_CASE_RC && timeIndex > 0) {
            // h1dash convolution with v1 and v2
            double d1 = 0.0, d2 = 0.0;
            for (int i = timeIndex; i > 0; i--) {
                if (i < (int)m.h1dashCoeffs.size() && m.h1dashCoeffs[i] != 0.0) {
                    d1 += m.h1dashCoeffs[i] * (v1_[i] - initVolt1_);
                    d2 += m.h1dashCoeffs[i] * (v2_[i] - initVolt2_);
                }
            }
            d1 += initVolt1_ * m.intH1dash;
            d2 += initVolt2_ * m.intH1dash;
            d1 -= initVolt1_ * m.h1dashFirstCoeff;
            d2 -= initVolt2_ * m.h1dashFirstCoeff;
            input1_ -= d1;
            input2_ -= d2;

            // h2 convolution with i2 and i1
            d1 = d2 = 0.0;
            for (int i = timeIndex; i > 0; i--) {
                if (i < (int)m.h2Coeffs.size() && m.h2Coeffs[i] != 0.0) {
                    d1 += m.h2Coeffs[i] * (i2_[i] - initCur2_);
                    d2 += m.h2Coeffs[i] * (i1_[i] - initCur1_);
                }
            }
            d1 += initCur2_ * m.intH2;
            d2 += initCur1_ * m.intH2;
            d1 -= initCur2_ * m.h2FirstCoeff;
            d2 -= initCur1_ * m.h2FirstCoeff;
            input1_ += d1;
            input2_ += d2;

            // h3dash convolution with v2 and v1
            d1 = d2 = 0.0;
            for (int i = timeIndex; i > 0; i--) {
                if (i < (int)m.h3dashCoeffs.size() && m.h3dashCoeffs[i] != 0.0) {
                    d1 += m.h3dashCoeffs[i] * (v2_[i] - initVolt2_);
                    d2 += m.h3dashCoeffs[i] * (v1_[i] - initVolt1_);
                }
            }
            d1 += initVolt2_ * m.intH3dash;
            d2 += initVolt1_ * m.intH3dash;
            d1 -= initVolt2_ * m.h3dashFirstCoeff;
            d2 -= initVolt1_ * m.h3dashFirstCoeff;
            input1_ += d1;
            input2_ += d2;
        }
    }

    // Load RHS
    if (br1_ >= 0) rhs[br1_] += input1_;
    if (br2_ >= 0) rhs[br2_] += input2_;
}

// ---------------------------------------------------------------------------
// ac_stamp — frequency-domain analysis using exact propagation function
// ---------------------------------------------------------------------------
void LossyTransmissionLine::ac_stamp(
    const std::vector<double>& /*voltages*/,
    NumericMatrix& G, NumericMatrix& C)
{
    const auto& m = *model_;

    // The LTRA AC model uses complex frequency-dependent Y-parameters.
    // The neospice framework uses the G + jwC split, which cannot represent
    // arbitrary complex, frequency-dependent stamps.
    //
    // For the LTRA device, the AC equations are:
    //   Y0(s)*V1 - I1 = exp(-lambda(s)*len) * (Y0(s)*V2 + I2)
    //   Y0(s)*V2 - I2 = exp(-lambda(s)*len) * (Y0(s)*V1 + I1)
    //
    // Since we can't do frequency-dependent stamps in the G+jwC framework,
    // we stamp a DC-like approximation into G.
    //
    // For RG case, the DC model is exact at all frequencies.
    // For other cases, this is an approximation.
    //
    // TODO: The full AC implementation would require extending the framework
    // to support frequency-dependent device evaluation, similar to how ngspice
    // calls the AC load function at each frequency point.

    switch (m.specialCase) {
    case LTRA_CASE_RG: {
        // DC model is exact - stamp the same as DC
        double dummy1 = m.len * std::sqrt(m.R * m.G);
        double dummy2 = std::exp(-dummy1);
        dummy1 = std::exp(dummy1);
        double coshlrootGR = 0.5 * (dummy1 + dummy2);
        double gmin = 1e-12;
        double rRsLrGRorG = (m.G <= 1e-10) ? m.len * m.R :
            0.5 * (dummy1 - dummy2) * std::sqrt(m.R / m.G);
        double rGsLrGRorR = (m.R <= 1e-10) ? m.len * m.G :
            0.5 * (dummy1 - dummy2) * std::sqrt(m.G / m.R);

        add_if_valid(G, off_ibr1_pos1_,  1.0);
        add_if_valid(G, off_ibr1_neg1_, -1.0);
        add_if_valid(G, off_ibr1_pos2_, -coshlrootGR);
        add_if_valid(G, off_ibr1_neg2_,  coshlrootGR);
        add_if_valid(G, off_ibr1_ibr2_,  (1.0 + gmin) * rRsLrGRorG);

        add_if_valid(G, off_ibr2_ibr2_,  coshlrootGR);
        add_if_valid(G, off_ibr2_pos2_, -(1.0 + gmin) * rGsLrGRorR);
        add_if_valid(G, off_ibr2_neg2_,  (1.0 + gmin) * rGsLrGRorR);
        add_if_valid(G, off_ibr2_ibr1_,  1.0);

        add_if_valid(G, off_pos1_ibr1_,  1.0);
        add_if_valid(G, off_neg1_ibr1_, -1.0);
        add_if_valid(G, off_pos2_ibr2_,  1.0);
        add_if_valid(G, off_neg2_ibr2_, -1.0);
        break;
    }

    default: {
        // For LC/RLC/RC cases: stamp the branch equations in a form
        // that at least provides the correct DC behavior.
        // V1 = Z0*I1 + Z0*I2 and V1 - V2 = R*L*I1 (resistive approx)
        add_if_valid(G, off_pos1_ibr1_,  1.0);
        add_if_valid(G, off_neg1_ibr1_, -1.0);
        add_if_valid(G, off_pos2_ibr2_,  1.0);
        add_if_valid(G, off_neg2_ibr2_, -1.0);

        add_if_valid(G, off_ibr1_ibr1_,  1.0);
        add_if_valid(G, off_ibr1_ibr2_,  1.0);
        add_if_valid(G, off_ibr2_pos1_,  1.0);
        add_if_valid(G, off_ibr2_pos2_, -1.0);
        add_if_valid(G, off_ibr2_ibr1_, -m.R * m.len);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Transient helpers
// ---------------------------------------------------------------------------

void LossyTransmissionLine::accept_step(double time,
                                         const std::vector<double>& solution) {
    double vp1p = (p1p_ >= 0) ? solution[p1p_] : 0.0;
    double vp1n = (p1n_ >= 0) ? solution[p1n_] : 0.0;
    double vp2p = (p2p_ >= 0) ? solution[p2p_] : 0.0;
    double vp2n = (p2n_ >= 0) ? solution[p2n_] : 0.0;

    double v1 = vp1p - vp1n;
    double v2 = vp2p - vp2n;
    double cur1 = (br1_ >= 0 && br1_ < (int32_t)solution.size()) ? solution[br1_] : 0.0;
    double cur2 = (br2_ >= 0 && br2_ < (int32_t)solution.size()) ? solution[br2_] : 0.0;

    v1_.push_back(v1);
    i1_.push_back(cur1);
    v2_.push_back(v2);
    i2_.push_back(cur2);

    // Update model coefficient arrays if needed
    auto& m = *model_;
    int timeIndex = static_cast<int>(v1_.size()) - 1;

    if (timeIndex >= (int)m.h1dashCoeffs.size()) {
        size_t newSize = m.h1dashCoeffs.size() + 100;
        m.h1dashCoeffs.resize(newSize, 0.0);
        m.h2Coeffs.resize(newSize, 0.0);
        m.h3dashCoeffs.resize(newSize, 0.0);
    }
}

void LossyTransmissionLine::set_transient(bool enable) {
    transient_ = enable;
    if (!enable) {
        v1_.clear(); i1_.clear();
        v2_.clear(); i2_.clear();
        input1_ = input2_ = 0.0;
    }
}

double LossyTransmissionLine::compute_trunc_ltra(double /*currentTime*/,
                                                   double timestep) const {
    const auto& m = *model_;
    double result = timestep;

    switch (m.specialCase) {
    case LTRA_CASE_LC:
    case LTRA_CASE_RLC:
        if (m.stepLimit == LTRA_STEP_LIMIT) {
            result = std::min(result, m.td);
        }
        if (m.specialCase == LTRA_CASE_RLC && !m.truncDontCut) {
            result = std::min(result, m.maxSafeStep);
        }
        break;
    case LTRA_CASE_RC:
    case LTRA_CASE_RG:
        break;
    default:
        break;
    }
    return result;
}

std::optional<double> LossyTransmissionLine::query_param(const std::string& name) const {
    auto ci_eq = [](const std::string& a, const char* b) {
        if (a.size() != std::strlen(b)) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower(a[i]) != std::tolower(b[i])) return false;
        return true;
    };

    const auto& m = *model_;
    if (ci_eq(name, "r"))   return m.R;
    if (ci_eq(name, "l"))   return m.L;
    if (ci_eq(name, "g"))   return m.G;
    if (ci_eq(name, "c"))   return m.C;
    if (ci_eq(name, "len")) return m.len;
    if (ci_eq(name, "z0"))  return m.Z0;
    if (ci_eq(name, "td"))  return m.td;
    if (ci_eq(name, "y0"))  return m.Y0;

    return std::nullopt;
}

} // namespace neospice
