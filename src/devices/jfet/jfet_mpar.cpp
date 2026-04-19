/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
**********/
/*
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/jfet/jfet_def.hpp"
#include "devices/jfet/jfet_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef CONSTvt0
#define CONSTvt0 0.025852037
#endif
#ifndef CONSTroot2
#define CONSTroot2 1.4142135623730950488
#endif
#ifndef CONSTCtoK
#define CONSTCtoK 273.15
#endif
#ifndef CHARGE
#define CHARGE 1.6021918e-19
#endif
#ifndef FABS
#define FABS(x) std::fabs(x)
#endif
#ifndef ABS
#define ABS(x) std::fabs(x)
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef TMALLOC
#define TMALLOC(type, num) (new type[num]())
#endif
#ifndef NG_IGNORE
#define NG_IGNORE(x) (void)(x)
#endif
#ifndef cp_getvar
#define cp_getvar(name, type, ptr) 0
#endif
#ifndef CP_REAL
#define CP_REAL 0
#endif
#ifndef NUMELEMS
#define NUMELEMS(ARRAY) (sizeof(ARRAY)/sizeof(*(ARRAY)))
#endif
#ifndef IOP
#define IOP(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}
#endif
#ifndef IOPU
#define IOPU(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}
#endif
#ifndef IP
#define IP(a,b,c,d) {a, b, (Shim::IF_SET|c), d}
#endif
#ifndef OP
#define OP(a,b,c,d) {a, b, (Shim::IF_ASK|c), d}
#endif
#ifndef OPU
#define OPU(a,b,c,d) {a, b, (Shim::IF_ASK|c), d}
#endif

namespace neospice::jfet {

using namespace Shim;

int
JFETmParam(int param, Shim::IfValue *value, JFETModel *inModels)
{
    JFETModel *model = (JFETModel*)inModels;
    switch(param) {
        case JFET_MOD_TNOM:
            model->JFETtnomGiven = TRUE;
            model->JFETtnom = value->rValue+CONSTCtoK;
            break;
        case JFET_MOD_VTO:
            model->JFETthresholdGiven = TRUE;
            model->JFETthreshold = value->rValue;
            break;
        case JFET_MOD_BETA:
            model->JFETbetaGiven = TRUE;
            model->JFETbeta = value->rValue;
            break;
        case JFET_MOD_LAMBDA:
            model->JFETlModulationGiven = TRUE;
            model->JFETlModulation = value->rValue;
            break;
        case JFET_MOD_RD:
            model->JFETdrainResistGiven = TRUE;
            model->JFETdrainResist = value->rValue;
            break;
        case JFET_MOD_RS:
            model->JFETsourceResistGiven = TRUE;
            model->JFETsourceResist = value->rValue;
            break;
        case JFET_MOD_CGS:
            model->JFETcapGSGiven = TRUE;
            model->JFETcapGS = value->rValue;
            break;
        case JFET_MOD_CGD:
            model->JFETcapGDGiven = TRUE;
            model->JFETcapGD = value->rValue;
            break;
        case JFET_MOD_PB:
            model->JFETgatePotentialGiven = TRUE;
            model->JFETgatePotential = value->rValue;
            break;
        case JFET_MOD_IS:
            model->JFETgateSatCurrentGiven = TRUE;
            model->JFETgateSatCurrent = value->rValue;
            break;
        case JFET_MOD_FC:
            model->JFETdepletionCapCoeffGiven = TRUE;
            model->JFETdepletionCapCoeff = value->rValue;
            break;
        case JFET_MOD_NJF:
            if(value->iValue) {
                model->JFETtype = NJF;
            }
            break;
        case JFET_MOD_PJF:
            if(value->iValue) {
                model->JFETtype = PJF;
            }
            break;
        case JFET_MOD_KF:
            model->JFETfNcoefGiven = TRUE;
            model->JFETfNcoef = value->rValue;
            break;
        case JFET_MOD_AF:
            model->JFETfNexpGiven = TRUE;
            model->JFETfNexp = value->rValue;
            break;
        /* Modification for Sydney University JFET model */
        case JFET_MOD_B:
            model->JFETbGiven = TRUE;
            model->JFETb = value->rValue;
            return 0;
        /* end Sydney University mod */
        case JFET_MOD_TCV:
            model->JFETtcvGiven = TRUE;
            model->JFETtcv = value->rValue;
            break;
        case JFET_MOD_BEX:
            model->JFETbexGiven = TRUE;
            model->JFETbex = value->rValue;
            break;
        case JFET_MOD_NLEV:
            model->JFETnlevGiven = TRUE;
            model->JFETnlev = value->iValue;
            break;
        case JFET_MOD_GDSNOI:
            model->JFETgdsnoiGiven = TRUE;
            model->JFETgdsnoi = value->rValue;
            break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}

} // namespace neospice::jfet
