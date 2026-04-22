/**** BSIM3v3.2.4, Released by Xuemei Xi 12/21/2001 ****/

/**********
 * Copyright 2001 Regents of the University of California. All rights reserved.
 * File: b3par.c of BSIM3v3.2.4
 * Author: 1995 Min-Chie Jeng and Mansun Chan
 * Author: 1997-1999 Weidong Liu.
 * Author: 2001 Xuemei Xi
 * Modified by Paolo Nenzi 2002 and Dietmar Warning 2003
 **********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/bsim3v32/bsim3v32_def.hpp"
#include "devices/bsim3v32/bsim3v32_shim.hpp"
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

namespace neospice::bsim3v32 {

using namespace Shim;

int
BSIM3v32param (int param, Shim::IfValue *value, BSIM3v32Instance *inst, Shim::IfValue *select)
{
    double scale;

    BSIM3v32Instance *here = (BSIM3v32Instance*)inst;

    NG_IGNORE(select);

    if (!cp_getvar("scale", CP_REAL, &scale))
        scale = 1;

    switch(param)
    {   case BSIM3v32_W:
            here->BSIM3v32w = value->rValue*scale;
            here->BSIM3v32wGiven = TRUE;
            break;
        case BSIM3v32_L:
            here->BSIM3v32l = value->rValue*scale;
            here->BSIM3v32lGiven = TRUE;
            break;
        case BSIM3v32_M:
            here->BSIM3v32m = value->rValue;
            here->BSIM3v32mGiven = TRUE;
            break;
        case BSIM3v32_AS:
            here->BSIM3v32sourceArea = value->rValue*scale*scale;
            here->BSIM3v32sourceAreaGiven = TRUE;
            break;
        case BSIM3v32_AD:
            here->BSIM3v32drainArea = value->rValue*scale*scale;
            here->BSIM3v32drainAreaGiven = TRUE;
            break;
        case BSIM3v32_PS:
            here->BSIM3v32sourcePerimeter = value->rValue*scale;
            here->BSIM3v32sourcePerimeterGiven = TRUE;
            break;
        case BSIM3v32_PD:
            here->BSIM3v32drainPerimeter = value->rValue*scale;
            here->BSIM3v32drainPerimeterGiven = TRUE;
            break;
        case BSIM3v32_NRS:
            here->BSIM3v32sourceSquares = value->rValue;
            here->BSIM3v32sourceSquaresGiven = TRUE;
            break;
        case BSIM3v32_NRD:
            here->BSIM3v32drainSquares = value->rValue;
            here->BSIM3v32drainSquaresGiven = TRUE;
            break;
        case BSIM3v32_OFF:
            here->BSIM3v32off = value->iValue;
            break;
        case BSIM3v32_IC_VBS:
            here->BSIM3v32icVBS = value->rValue;
            here->BSIM3v32icVBSGiven = TRUE;
            break;
        case BSIM3v32_IC_VDS:
            here->BSIM3v32icVDS = value->rValue;
            here->BSIM3v32icVDSGiven = TRUE;
            break;
        case BSIM3v32_IC_VGS:
            here->BSIM3v32icVGS = value->rValue;
            here->BSIM3v32icVGSGiven = TRUE;
            break;
        case BSIM3v32_NQSMOD:
            here->BSIM3v32nqsMod = value->iValue;
            here->BSIM3v32nqsModGiven = TRUE;
            break;
        case BSIM3v32_GEO:
            here->BSIM3v32geo = value->iValue;
            here->BSIM3v32geoGiven = TRUE;
            break;
        case BSIM3v32_DELVTO:
            here->BSIM3v32delvto = value->rValue;
            here->BSIM3v32delvtoGiven = TRUE;
            break;
        case BSIM3v32_MULU0:
            here->BSIM3v32mulu0 = value->rValue;
            here->BSIM3v32mulu0Given = TRUE;
            break;
        case BSIM3v32_IC:
            switch(value->v.numValue){
                case 3:
                    here->BSIM3v32icVBS = *(value->v.vec.rVec+2);
                    here->BSIM3v32icVBSGiven = TRUE;
                case 2:
                    here->BSIM3v32icVGS = *(value->v.vec.rVec+1);
                    here->BSIM3v32icVGSGiven = TRUE;
                case 1:
                    here->BSIM3v32icVDS = *(value->v.vec.rVec);
                    here->BSIM3v32icVDSGiven = TRUE;
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


} // namespace neospice::bsim3v32
