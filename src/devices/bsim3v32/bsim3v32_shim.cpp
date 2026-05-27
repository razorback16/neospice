#include "devices/bsim3v32/bsim3v32_shim.hpp"
#include <cstdarg>

namespace neospice::bsim3v32::Shim {

neospice::MatrixOffset Matrix::make_elt(int row, int col) {
    if (row < 0 || col < 0) {
        journal_.emplace_back(-1, -1);
        return -1;
    }
    builder_.add(row, col);
    neospice::MatrixOffset id = static_cast<neospice::MatrixOffset>(journal_.size());
    journal_.emplace_back(row, col);
    return id;
}

std::vector<neospice::MatrixOffset>
Matrix::resolve_offsets(const neospice::SparsityPattern &pat) const {
    std::vector<neospice::MatrixOffset> out;
    out.reserve(journal_.size());
    for (auto &[r, c] : journal_) {
        if (r < 0 || c < 0) out.push_back(-1);
        else out.push_back(pat.offset(r, c));
    }
    return out;
}

int Ckt::add_internal_node(const char *name) {
    if (node_alloc) return node_alloc(name);
    return CKTinternalNodeCounter++;
}

static CKTnode s_tmp_node_;

int CKTmkVolt(Ckt *ckt, CKTnode **node_out,
              const char * /*basename*/, const char *suffix) {
    int idx = ckt->add_internal_node(suffix);
    s_tmp_node_.number = idx;
    s_tmp_node_.name   = suffix;
    if (node_out) *node_out = &s_tmp_node_;
    return OK;
}

int NIintegrate(Ckt *ckt, double *geq, double *ceq,
                double cap, int qcap) {
    const int ccap = qcap + 1;
    double *s0 = ckt->CKTstate0 + qcap;
    double *s1 = ckt->CKTstate1 + qcap;
    double *s2 = ckt->CKTstate2 + qcap;

    int order = ckt->CKTorder;
    if (order < 1) order = 1;
    if (order > 2) order = 2;

    double deriv;
    if (ckt->CKTintegrateMethod == 0 && order == 2) {
        // Trapezoidal order 2: needs previous-derivative correction
        deriv = -s1[1] * ckt->xmu_ratio + ckt->CKTag[0]*s0[0] + ckt->CKTag[1]*s1[0];
    } else {
        // BE (order 1) or Gear: pure coefficient sum
        deriv = ckt->CKTag[0]*s0[0];
        if (order >= 1) deriv += ckt->CKTag[1]*s1[0];
        if (order >= 2) deriv += ckt->CKTag[2]*s2[0];
    }
    s0[1] = deriv;

    *geq = ckt->CKTag[0] * cap;
    *ceq = s0[1] - (*geq) * s0[0];

    return OK;
}

void report_error(int /*level*/, const char *fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

} // namespace neospice::bsim3v32::Shim

namespace neospice::bsim3v32 {

double DEVlimvds(double vnew, double vold) {
    if(vold >= 3.5) {
        if(vnew > vold) {
            vnew = std::fmin(vnew,(3 * vold) +2);
        } else {
            if (vnew < 3.5) vnew = std::fmax(vnew,2);
        }
    } else {
        if(vnew > vold) vnew = std::fmin(vnew,4);
        else vnew = std::fmax(vnew,-.5);
    }
    return vnew;
}

double DEVpnjlim(double vnew, double vold, double vt, double vcrit, int *icheck) {
    double arg;
    if((vnew > vcrit) && (std::fabs(vnew - vold) > (vt + vt))) {
        if(vold > 0) {
            arg = (vnew - vold) / vt;
            if(arg > 0) vnew = vold + vt * (2+std::log(arg-2));
            else vnew = vold - vt * (2+std::log(2-arg));
        } else {
            vnew = vt *std::log(vnew/vt);
        }
        *icheck = 1;
    } else {
       if (vnew < 0) {
           if (vold > 0) arg = -1*vold-1;
           else arg = 2*vold-1;
           if (vnew < arg) { vnew = arg; *icheck = 1; }
           else *icheck = 0;
        } else *icheck = 0;
    }
    return vnew;
}

double DEVfetlim(double vnew, double vold, double vto) {
    double vtsthi = std::fabs(2*(vold-vto))+2;
    double vtstlo = std::fabs(vold-vto)+1;
    double vtox   = vto + 3.5;
    double delv   = vnew-vold;
    double vtemp;
    if (vold >= vto) {
        if(vold >= vtox) {
            if(delv <= 0) {
                if(vnew >= vtox) {
                    if(-delv >vtstlo) vnew = vold - vtstlo;
                } else vnew = std::fmax(vnew,vto+2);
            } else {
                if(delv >= vtsthi) vnew = vold + vtsthi;
            }
        } else {
            if(delv <= 0) vnew = std::fmax(vnew,vto-.5);
            else vnew = std::fmin(vnew,vto+4);
        }
    } else {
        if(delv <= 0) {
            if(-delv >vtsthi) vnew = vold - vtsthi;
        } else {
            vtemp = vto + .5;
            if(vnew <= vtemp) {
                if(delv >vtstlo) vnew = vold + vtstlo;
            } else vnew = vtemp;
        }
    }
    return vnew;
}

int ACM_SourceDrainResistances(
    int /*ACM*/, double /*LD*/, double /*LDIF*/, double /*HDIF*/, double /*WMLT*/,
    double /*w*/, double /*XW*/, double /*RSH*/,
    int /*drainSquaresGiven*/, double /*RD*/, double /*RDC*/, double /*drainSquares*/,
    int /*sourceSquaresGiven*/, double /*RS*/, double /*RSC*/, double /*sourceSquares*/,
    double *drainConductance, double *sourceConductance) {
    if (drainConductance)  *drainConductance  = 0.0;
    if (sourceConductance) *sourceConductance = 0.0;
    return 0;
}
int ACM_saturationCurrents(
    int /*ACM*/, int /*CALCACM*/, int /*GEO*/, double /*HDIF*/, double /*WMLT*/,
    double /*w*/, double /*XW*/,
    double /*jctTempSatCurDensity*/, double /*jctSidewallTempSatCurDensity*/,
    int /*drainAreaGiven*/, double /*drainArea*/,
    int /*drainPerimeterGiven*/, double /*drainPerimeter*/,
    int /*sourceAreaGiven*/, double /*sourceArea*/,
    int /*sourcePerimeterGiven*/, double /*sourcePerimeter*/,
    double *DrainSatCurrent, double *SourceSatCurrent) {
    if (DrainSatCurrent)  *DrainSatCurrent  = 1e-14;
    if (SourceSatCurrent) *SourceSatCurrent = 1e-14;
    return 0;
}
int ACM_junctionCapacitances(
    int /*ACM*/, int /*CALCACM*/, int /*GEO*/, double /*HDIF*/, double /*WMLT*/,
    double /*w*/, double /*XW*/,
    int /*drainAreaGiven*/, double /*drainArea*/,
    int /*drainPerimeterGiven*/, double /*drainPerimeter*/,
    int /*sourceAreaGiven*/, double /*sourceArea*/,
    int /*sourcePerimeterGiven*/, double /*sourcePerimeter*/,
    double /*CJ*/, double /*CJSW*/, double /*CJGATE*/,
    double *czbd, double *czbdsw, double *czbdswg,
    double *czbs, double *czbssw, double *czbsswg) {
    if (czbd) *czbd = 0.0;
    if (czbdsw) *czbdsw = 0.0;
    if (czbdswg) *czbdswg = 0.0;
    if (czbs) *czbs = 0.0;
    if (czbssw) *czbssw = 0.0;
    if (czbsswg) *czbsswg = 0.0;
    return 0;
}

} // namespace neospice::bsim3v32
