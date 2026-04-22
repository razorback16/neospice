/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified: 2000 AlansFixes
**********/
/*
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/mos3/mos3_def.hpp"
#include "devices/mos3/mos3_shim.hpp"
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

namespace neospice::mos3 {

using namespace Shim;

/* ARGSUSED */
int
MOS3param(int param, Shim::IfValue *value, MOS3Instance *inst, Shim::IfValue *select)
{
    double scale;

    MOS3Instance *here = (MOS3Instance *)inst;

    NG_IGNORE(select);

    if (!cp_getvar("scale", CP_REAL, &scale))
        scale = 1;

    switch(param) {
    	
        case MOS3_M:
            here->MOS3m = value->rValue;
            here->MOS3mGiven = TRUE;
            break;
        case MOS3_W:
            here->MOS3w = value->rValue * scale;
            here->MOS3wGiven = TRUE;
            break;
        case MOS3_L:
            here->MOS3l = value->rValue * scale;
            here->MOS3lGiven = TRUE;
            break;
        case MOS3_AS:
            here->MOS3sourceArea = value->rValue * scale * scale;
            here->MOS3sourceAreaGiven = TRUE;
            break;
        case MOS3_AD:
            here->MOS3drainArea = value->rValue * scale * scale;
            here->MOS3drainAreaGiven = TRUE;
            break;
        case MOS3_PS:
            here->MOS3sourcePerimiter = value->rValue * scale;
            here->MOS3sourcePerimiterGiven = TRUE;
            break;
        case MOS3_PD:
            here->MOS3drainPerimiter = value->rValue * scale;
            here->MOS3drainPerimiterGiven = TRUE;
            break;
        case MOS3_NRS:
            here->MOS3sourceSquares = value->rValue;
            here->MOS3sourceSquaresGiven = TRUE;
            break;
        case MOS3_NRD:
            here->MOS3drainSquares = value->rValue;
            here->MOS3drainSquaresGiven = TRUE;
            break;
        case MOS3_OFF:
            here->MOS3off = (value->iValue != 0);
            break;
        case MOS3_IC_VBS:
            here->MOS3icVBS = value->rValue;
            here->MOS3icVBSGiven = TRUE;
            break;
        case MOS3_IC_VDS:
            here->MOS3icVDS = value->rValue;
            here->MOS3icVDSGiven = TRUE;
            break;
        case MOS3_IC_VGS:
            here->MOS3icVGS = value->rValue;
            here->MOS3icVGSGiven = TRUE;
            break;
        case MOS3_TEMP:
            here->MOS3temp = value->rValue+CONSTCtoK;
            here->MOS3tempGiven = TRUE;
            break;
        case MOS3_DTEMP:
            here->MOS3dtemp = value->rValue;
            here->MOS3dtempGiven = TRUE;
            break;
        case MOS3_IC:
            switch(value->v.numValue){
                case 3:
                    here->MOS3icVBS = *(value->v.vec.rVec+2);
                    here->MOS3icVBSGiven = TRUE;
                case 2:
                    here->MOS3icVGS = *(value->v.vec.rVec+1);
                    here->MOS3icVGSGiven = TRUE;
                case 1:
                    here->MOS3icVDS = *(value->v.vec.rVec);
                    here->MOS3icVDSGiven = TRUE;
                    break;
                default:
                    return Shim::E_BADPARM;
            }
            break;
        case MOS3_L_SENS:
            if(value->iValue) {
                here->MOS3senParmNo = 1;
                here->MOS3sens_l = 1;
            }
            break;
        case MOS3_W_SENS:
            if(value->iValue) {
                here->MOS3senParmNo = 1;
                here->MOS3sens_w = 1;
            }
            break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}

} // namespace neospice::mos3
