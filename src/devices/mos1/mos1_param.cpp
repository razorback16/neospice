/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified: 2000 AlansFixes
**********/
/*
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/mos1/mos1_def.hpp"
#include "devices/mos1/mos1_shim.hpp"
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

namespace neospice::mos1 {

using namespace Shim;

/* ARGSUSED */
int
MOS1param(int param, Shim::IfValue *value, MOS1Instance *inst, Shim::IfValue *select)
{
    double scale;

    MOS1Instance *here = (MOS1Instance *)inst;

    NG_IGNORE(select);

    if (!cp_getvar("scale", CP_REAL, &scale))
        scale = 1;

    switch(param) {
        case MOS1_TEMP:
            here->MOS1temp = value->rValue+CONSTCtoK;
            here->MOS1tempGiven = TRUE;
            break;
        case MOS1_DTEMP:
            here->MOS1dtemp = value->rValue;
            here->MOS1dtempGiven = TRUE;
            break;
        case MOS1_M:
            here->MOS1m = value->rValue;
            here->MOS1mGiven = TRUE;
            break;
        case MOS1_W:
            here->MOS1w = value->rValue * scale;
            here->MOS1wGiven = TRUE;
            break;
        case MOS1_L:
            here->MOS1l = value->rValue * scale;
            here->MOS1lGiven = TRUE;
            break;
        case MOS1_AS:
            here->MOS1sourceArea = value->rValue * scale * scale;
            here->MOS1sourceAreaGiven = TRUE;
            break;
        case MOS1_AD:
            here->MOS1drainArea = value->rValue * scale * scale;
            here->MOS1drainAreaGiven = TRUE;
            break;
        case MOS1_PS:
            here->MOS1sourcePerimiter = value->rValue * scale;
            here->MOS1sourcePerimiterGiven = TRUE;
            break;
        case MOS1_PD:
            here->MOS1drainPerimiter = value->rValue * scale;
            here->MOS1drainPerimiterGiven = TRUE;
            break;
        case MOS1_NRS:
            here->MOS1sourceSquares = value->rValue;
            here->MOS1sourceSquaresGiven = TRUE;
            break;
        case MOS1_NRD:
            here->MOS1drainSquares = value->rValue;
            here->MOS1drainSquaresGiven = TRUE;
            break;
        case MOS1_OFF:
            here->MOS1off = (value->iValue != 0);
            break;
        case MOS1_IC_VBS:
            here->MOS1icVBS = value->rValue;
            here->MOS1icVBSGiven = TRUE;
            break;
        case MOS1_IC_VDS:
            here->MOS1icVDS = value->rValue;
            here->MOS1icVDSGiven = TRUE;
            break;
        case MOS1_IC_VGS:
            here->MOS1icVGS = value->rValue;
            here->MOS1icVGSGiven = TRUE;
            break;
        case MOS1_IC:
            switch(value->v.numValue){
                case 3:
                    here->MOS1icVBS = *(value->v.vec.rVec+2);
                    here->MOS1icVBSGiven = TRUE;
                case 2:
                    here->MOS1icVGS = *(value->v.vec.rVec+1);
                    here->MOS1icVGSGiven = TRUE;
                case 1:
                    here->MOS1icVDS = *(value->v.vec.rVec);
                    here->MOS1icVDSGiven = TRUE;
                    break;
                default:
                    return Shim::E_BADPARM;
            }
            break;
        case MOS1_L_SENS:
            if(value->iValue) {
                here->MOS1senParmNo = 1;
                here->MOS1sens_l = 1;
            }
            break;
        case MOS1_W_SENS:
            if(value->iValue) {
                here->MOS1senParmNo = 1;
                here->MOS1sens_w = 1;
            }
            break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}

} // namespace neospice::mos1
