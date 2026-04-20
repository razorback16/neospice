/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Model Author: 1995 Colin McAndrew Motorola
Spice3 Implementation: 2003 Dietmar Warning DAnalyse GmbH
**********/

/*
 * This routine sets instance parameters for
 * VBICs in the circuit.
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/vbic/vbic_def.hpp"
#include "devices/vbic/vbic_shim.hpp"
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

namespace neospice::vbic {

using namespace Shim;

/* ARGSUSED */
int
VBICparam(int param, Shim::IfValue *value, VBICInstance *instPtr, Shim::IfValue *select)
{
    VBICInstance *here = (VBICInstance*)instPtr;

    NG_IGNORE(select);

    switch(param) {
        case VBIC_AREA:
            here->VBICarea = value->rValue;
            here->VBICareaGiven = TRUE;
            break;
        case VBIC_OFF:
            here->VBICoff = (value->iValue != 0);
            break;
        case VBIC_IC_VBE:
            here->VBICicVBE = value->rValue;
            here->VBICicVBEGiven = TRUE;
            break;
        case VBIC_IC_VCE:
            here->VBICicVCE = value->rValue;
            here->VBICicVCEGiven = TRUE;
            break;
        case VBIC_TEMP:
            here->VBICtemp = value->rValue+CONSTCtoK;
            here->VBICtempGiven = TRUE;
            break;
        case VBIC_DTEMP:
            here->VBICdtemp = value->rValue;
            here->VBICdtempGiven = TRUE;
            break;
        case VBIC_M:
            here->VBICm = value->rValue;
            here->VBICmGiven = TRUE;
            break;
        case VBIC_IC :
            switch(value->v.numValue) {
                case 2:
                    here->VBICicVCE = *(value->v.vec.rVec+1);
                    here->VBICicVCEGiven = TRUE;
                case 1:
                    here->VBICicVBE = *(value->v.vec.rVec);
                    here->VBICicVBEGiven = TRUE;
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

} // namespace neospice::vbic
