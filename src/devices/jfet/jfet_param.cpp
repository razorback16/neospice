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

/* ARGSUSED */
int
JFETparam(int param, Shim::IfValue *value, JFETInstance *inst, Shim::IfValue *select)
{
    JFETInstance *here = (JFETInstance *)inst;

    NG_IGNORE(select);

    switch(param) {
        case JFET_TEMP:
            here->JFETtemp = value->rValue+CONSTCtoK;
            here->JFETtempGiven = TRUE;
            break;
        case JFET_DTEMP:
            here->JFETdtemp = value->rValue;
            here->JFETdtempGiven = TRUE;
            break;
        case JFET_AREA:
            here->JFETarea = value->rValue;
            here->JFETareaGiven = TRUE;
            break;
       case JFET_M:
            here->JFETm = value->rValue;
            here->JFETmGiven = TRUE;
            break;
        case JFET_IC_VDS:
            here->JFETicVDS = value->rValue;
            here->JFETicVDSGiven = TRUE;
            break;
        case JFET_IC_VGS:
            here->JFETicVGS = value->rValue;
            here->JFETicVGSGiven = TRUE;
            break;
        case JFET_OFF:
            here->JFEToff = (value->iValue != 0);
            break;
        case JFET_IC:
            switch(value->v.numValue) {
                case 2:
                    here->JFETicVGS = *(value->v.vec.rVec+1);
                    here->JFETicVGSGiven = TRUE;
                case 1:
                    here->JFETicVDS = *(value->v.vec.rVec);
                    here->JFETicVDSGiven = TRUE;
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

} // namespace neospice::jfet
