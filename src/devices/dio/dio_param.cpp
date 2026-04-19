/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified by Paolo Nenzi 2003 and Dietmar Warning 2012
**********/
/*
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/dio/dio_def.hpp"
#include "devices/dio/dio_shim.hpp"
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

namespace neospice::dio {

using namespace Shim;

/* ARGSUSED */
int
DIOparam(int param, Shim::IfValue *value, DIOInstance *inst, Shim::IfValue *select)
{
    double scale;

    DIOInstance *here = (DIOInstance*)inst;

    NG_IGNORE(select);

    if (!cp_getvar("scale", CP_REAL, &scale))
        scale = 1;

    switch(param) {
        case DIO_AREA:
            here->DIOarea = value->rValue;
            here->DIOareaGiven = TRUE;
            break;
        case DIO_PJ:
            here->DIOpj = value->rValue;
            here->DIOpjGiven = TRUE;
            break;
        case DIO_W:
            here->DIOw = value->rValue * scale;
            here->DIOwGiven = TRUE;
            break;
        case DIO_L:
            here->DIOl = value->rValue * scale;
            here->DIOlGiven = TRUE;
            break;
        case DIO_M:
            here->DIOm = value->rValue;
            here->DIOmGiven = TRUE;
            break;

        case DIO_TEMP:
            here->DIOtemp = value->rValue+CONSTCtoK;
            here->DIOtempGiven = TRUE;
            break;
        case DIO_DTEMP:
            here->DIOdtemp = value->rValue;
            here->DIOdtempGiven = TRUE;
            break;    
        case DIO_OFF:
            here->DIOoff = (value->iValue != 0);
            break;
        case DIO_IC:
            here->DIOinitCond = value->rValue;
            break;
        case DIO_AREA_SENS:
            here->DIOsenParmNo = value->iValue;
            break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}

} // namespace neospice::dio
