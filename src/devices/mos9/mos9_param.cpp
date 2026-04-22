/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified: Alan Gillespie
**********/
/*
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/mos9/mos9_def.hpp"
#include "devices/mos9/mos9_shim.hpp"
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
#ifndef IOPA
#define IOPA(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}
#endif
#ifndef IOPR
#define IOPR(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|Shim::IF_REDUNDANT|c), d}
#endif
#ifndef IOPAU
#define IOPAU(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}
#endif
#ifndef IPR
#define IPR(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_REDUNDANT|c), d}
#endif
#ifndef OPR
#define OPR(a,b,c,d) {a, b, (Shim::IF_ASK|Shim::IF_REDUNDANT|c), d}
#endif

namespace neospice::mos9 {

using namespace Shim;

/* ARGSUSED */
int
MOS9param(int param, Shim::IfValue *value, MOS9Instance *inst,
          Shim::IfValue *select)
{
    MOS9Instance *here = (MOS9Instance *)inst;

    NG_IGNORE(select);

    switch(param) {

        case MOS9_M:
            here->MOS9m = value->rValue;
            here->MOS9mGiven = TRUE;
            break;
        case MOS9_W:
            here->MOS9w = value->rValue;
            here->MOS9wGiven = TRUE;
            break;
        case MOS9_L:
            here->MOS9l = value->rValue;
            here->MOS9lGiven = TRUE;
            break;
        case MOS9_AS:
            here->MOS9sourceArea = value->rValue;
            here->MOS9sourceAreaGiven = TRUE;
            break;
        case MOS9_AD:
            here->MOS9drainArea = value->rValue;
            here->MOS9drainAreaGiven = TRUE;
            break;
        case MOS9_PS:
            here->MOS9sourcePerimiter = value->rValue;
            here->MOS9sourcePerimiterGiven = TRUE;
            break;
        case MOS9_PD:
            here->MOS9drainPerimiter = value->rValue;
            here->MOS9drainPerimiterGiven = TRUE;
            break;
        case MOS9_NRS:
            here->MOS9sourceSquares = value->rValue;
            here->MOS9sourceSquaresGiven = TRUE;
            break;
        case MOS9_NRD:
            here->MOS9drainSquares = value->rValue;
            here->MOS9drainSquaresGiven = TRUE;
            break;
        case MOS9_OFF:
            here->MOS9off = (value->iValue != 0);
            break;
        case MOS9_IC_VBS:
            here->MOS9icVBS = value->rValue;
            here->MOS9icVBSGiven = TRUE;
            break;
        case MOS9_IC_VDS:
            here->MOS9icVDS = value->rValue;
            here->MOS9icVDSGiven = TRUE;
            break;
        case MOS9_IC_VGS:
            here->MOS9icVGS = value->rValue;
            here->MOS9icVGSGiven = TRUE;
            break;
        case MOS9_TEMP:
            here->MOS9temp = value->rValue+CONSTCtoK;
            here->MOS9tempGiven = TRUE;
            break;
        case MOS9_DTEMP:
            here->MOS9dtemp = value->rValue;
            here->MOS9dtempGiven = TRUE;
            break;
        case MOS9_IC:
            switch(value->v.numValue){
                case 3:
                    here->MOS9icVBS = *(value->v.vec.rVec+2);
                    here->MOS9icVBSGiven = TRUE;
                case 2:
                    here->MOS9icVGS = *(value->v.vec.rVec+1);
                    here->MOS9icVGSGiven = TRUE;
                case 1:
                    here->MOS9icVDS = *(value->v.vec.rVec);
                    here->MOS9icVDSGiven = TRUE;
                    break;
                default:
                    return Shim::E_BADPARM;
            }
            break;
        case MOS9_L_SENS:
            if(value->iValue) {
                here->MOS9senParmNo = 1;
                here->MOS9sens_l = 1;
            }
            break;
        case MOS9_W_SENS:
            if(value->iValue) {
                here->MOS9senParmNo = 1;
                here->MOS9sens_w = 1;
            }
            break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}

} // namespace neospice::mos9
