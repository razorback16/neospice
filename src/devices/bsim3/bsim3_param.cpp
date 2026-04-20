/**** BSIM3v3.3.0, Released by Xuemei Xi 07/29/2005 ****/

/**********
 * Copyright 2004 Regents of the University of California. All rights reserved.
 * File: b3par.c of BSIM3v3.3.0
 * Author: 1995 Min-Chie Jeng and Mansun Chan
 * Author: 1997-1999 Weidong Liu.
 * Author: 2001 Xuemei Xi
 **********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/bsim3/bsim3_def.hpp"
#include "devices/bsim3/bsim3_shim.hpp"
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

namespace neospice::bsim3 {

using namespace Shim;

int
BSIM3param (
int param,
Shim::IfValue *value,
BSIM3Instance *inst,
Shim::IfValue *select)
{
    double scale;

    BSIM3Instance *here = (BSIM3Instance*)inst;

    NG_IGNORE(select);

    if (!cp_getvar("scale", CP_REAL, &scale))
        scale = 1;

    switch(param)
    {   case BSIM3_W:
            here->BSIM3w = value->rValue*scale;
            here->BSIM3wGiven = TRUE;
            break;
        case BSIM3_L:
            here->BSIM3l = value->rValue*scale;
            here->BSIM3lGiven = TRUE;
            break;
        case BSIM3_M:
            here->BSIM3m = value->rValue;
            here->BSIM3mGiven = TRUE;
            break;
        case BSIM3_AS:
            here->BSIM3sourceArea = value->rValue*scale*scale;
            here->BSIM3sourceAreaGiven = TRUE;
            break;
        case BSIM3_AD:
            here->BSIM3drainArea = value->rValue*scale*scale;
            here->BSIM3drainAreaGiven = TRUE;
            break;
        case BSIM3_PS:
            here->BSIM3sourcePerimeter = value->rValue*scale;
            here->BSIM3sourcePerimeterGiven = TRUE;
            break;
        case BSIM3_PD:
            here->BSIM3drainPerimeter = value->rValue*scale;
            here->BSIM3drainPerimeterGiven = TRUE;
            break;
        case BSIM3_NRS:
            here->BSIM3sourceSquares = value->rValue;
            here->BSIM3sourceSquaresGiven = TRUE;
            break;
        case BSIM3_NRD:
            here->BSIM3drainSquares = value->rValue;
            here->BSIM3drainSquaresGiven = TRUE;
            break;
        case BSIM3_OFF:
            here->BSIM3off = value->iValue;
            break;
        case BSIM3_IC_VBS:
            here->BSIM3icVBS = value->rValue;
            here->BSIM3icVBSGiven = TRUE;
            break;
        case BSIM3_IC_VDS:
            here->BSIM3icVDS = value->rValue;
            here->BSIM3icVDSGiven = TRUE;
            break;
        case BSIM3_IC_VGS:
            here->BSIM3icVGS = value->rValue;
            here->BSIM3icVGSGiven = TRUE;
            break;
        case BSIM3_NQSMOD:
            here->BSIM3nqsMod = value->iValue;
            here->BSIM3nqsModGiven = TRUE;
            break;
        case BSIM3_ACNQSMOD:
            here->BSIM3acnqsMod = value->iValue;
            here->BSIM3acnqsModGiven = TRUE;
            break;
        case BSIM3_GEO:
            here->BSIM3geo = value->iValue;
            here->BSIM3geoGiven = TRUE;
            break;
        case BSIM3_DELVTO:
            here->BSIM3delvto = value->rValue;
            here->BSIM3delvtoGiven = TRUE;
            break;
        case BSIM3_MULU0:
            here->BSIM3mulu0 = value->rValue;
            here->BSIM3mulu0Given = TRUE;
            break;
        case BSIM3_IC:
            switch(value->v.numValue){
                case 3:
                    here->BSIM3icVBS = *(value->v.vec.rVec+2);
                    here->BSIM3icVBSGiven = TRUE;
                case 2:
                    here->BSIM3icVGS = *(value->v.vec.rVec+1);
                    here->BSIM3icVGSGiven = TRUE;
                case 1:
                    here->BSIM3icVDS = *(value->v.vec.rVec);
                    here->BSIM3icVDSGiven = TRUE;
                    break;
                default:
                    return Shim::E_BADPARM;
            }
            break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}




} // namespace neospice::bsim3
