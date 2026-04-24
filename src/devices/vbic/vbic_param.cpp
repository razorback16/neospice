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
#include "devices/ucb_compat.hpp"

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
