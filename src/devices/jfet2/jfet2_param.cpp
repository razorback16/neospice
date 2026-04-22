/**********
based on jfetpar.c
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles

Modified to jfet2 for PS model definition ( Anthony E. Parker )
   Copyright 1994  Macquarie University, Sydney Australia.
**********/
/*
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/jfet2/jfet2_def.hpp"
#include "devices/jfet2/jfet2_shim.hpp"
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

namespace neospice::jfet2 {

using namespace Shim;

/* ARGSUSED */
int
JFET2param(int param, Shim::IfValue *value, JFET2Instance *inst, Shim::IfValue *select)
{
    JFET2Instance *here = (JFET2Instance *)inst;

    NG_IGNORE(select);

    switch(param) {
        case JFET2_TEMP:
            here->JFET2temp = value->rValue+CONSTCtoK;
            here->JFET2tempGiven = TRUE;
            break;
        case JFET2_DTEMP:
            here->JFET2temp = value->rValue;
            here->JFET2tempGiven = TRUE;
            break;
        case JFET2_AREA:
            here->JFET2area = value->rValue;
            here->JFET2areaGiven = TRUE;
            break;
         case JFET2_M:
            here->JFET2m = value->rValue;
            here->JFET2mGiven = TRUE;
            break;
        case JFET2_IC_VDS:
            here->JFET2icVDS = value->rValue;
            here->JFET2icVDSGiven = TRUE;
            break;
        case JFET2_IC_VGS:
            here->JFET2icVGS = value->rValue;
            here->JFET2icVGSGiven = TRUE;
            break;
        case JFET2_OFF:
            here->JFET2off = (value->iValue != 0);
            break;
        case JFET2_IC:
            switch(value->v.numValue) {
                case 2:
                    here->JFET2icVGS = *(value->v.vec.rVec+1);
                    here->JFET2icVGSGiven = TRUE;
                case 1:
                    here->JFET2icVDS = *(value->v.vec.rVec);
                    here->JFET2icVDSGiven = TRUE;
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

} // namespace neospice::jfet2
