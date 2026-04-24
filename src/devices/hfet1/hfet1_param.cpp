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
#include "devices/ucb_compat.hpp"

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
