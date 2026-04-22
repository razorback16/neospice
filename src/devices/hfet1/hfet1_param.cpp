/**********
Imported from MacSpice3f4 - Antony Wilson
Modified: Paolo Nenzi
**********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/hfet1/hfet1_def.hpp"
#include "devices/hfet1/hfet1_shim.hpp"
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

namespace neospice::hfet1 {

using namespace Shim;

// hfetdefs.h content is in hfet1_def.hpp (already included via shim)
/* ARGSUSED */
int
HFETAparam(int param, Shim::IfValue *value, HFETAInstance *inst, Shim::IfValue *select)
{
    HFETAInstance *here = (HFETAInstance*)inst;

    NG_IGNORE(select);

    switch(param) {
        case HFETA_LENGTH:
            here->HFETAlength = value->rValue;
            here->HFETAlengthGiven = TRUE;
            break;
        case HFETA_WIDTH:
            here->HFETAwidth = value->rValue;
            here->HFETAwidthGiven = TRUE;
            break;
        case HFETA_M:
            here->HFETAm = value->rValue;
            here->HFETAmGiven = TRUE;
            break;
        case HFETA_IC_VDS:
            here->HFETAicVDS = value->rValue;
            here->HFETAicVDSGiven = TRUE;
            break;
        case HFETA_IC_VGS:
            here->HFETAicVGS = value->rValue;
            here->HFETAicVGSGiven = TRUE;
            break;
        case HFETA_OFF:
            here->HFETAoff = value->iValue;
            break;
        case HFETA_IC:
            switch(value->v.numValue) {
                case 2:
                    here->HFETAicVGS = *(value->v.vec.rVec+1);
                    here->HFETAicVGSGiven = TRUE;
                case 1:
                    here->HFETAicVDS = *(value->v.vec.rVec);
                    here->HFETAicVDSGiven = TRUE;
                    break;
                default:
                    return Shim::E_BADPARM;
            }
            break;
        case HFETA_TEMP:
            here->HFETAtemp = value->rValue + CONSTCtoK;
            here->HFETAtempGiven = TRUE;
            break;
        case HFETA_DTEMP:
            here->HFETAdtemp = value->rValue;
            here->HFETAdtempGiven = TRUE;
            break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}

} // namespace neospice::hfet1
