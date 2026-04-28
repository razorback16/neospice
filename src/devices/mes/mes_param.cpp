/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 S. Hwang
**********/
/*
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/mes/mes_def.hpp"
#include "devices/mes/mes_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

#include "devices/ucb_compat.hpp"

namespace neospice::mes {

using namespace Shim;

/* ARGSUSED */
int
MESparam(int param, Shim::IfValue *value, MESInstance *inst, Shim::IfValue *select)
{
    MESInstance *here = (MESInstance*)inst;

    NG_IGNORE(select);

    switch(param) {
        case MES_AREA:
            here->MESarea = value->rValue;
            here->MESareaGiven = TRUE;
            break;
        case MES_M:
            here->MESm = value->rValue;
            here->MESmGiven = TRUE;
            break;
        case MES_IC_VDS:
            here->MESicVDS = value->rValue;
            here->MESicVDSGiven = TRUE;
            break;
        case MES_IC_VGS:
            here->MESicVGS = value->rValue;
            here->MESicVGSGiven = TRUE;
            break;
        case MES_OFF:
            here->MESoff = value->iValue;
            break;
        case MES_IC:
            switch(value->v.numValue) {
                case 2:
                    here->MESicVGS = *(value->v.vec.rVec+1);
                    here->MESicVGSGiven = TRUE;
                case 1:
                    here->MESicVDS = *(value->v.vec.rVec);
                    here->MESicVDSGiven = TRUE;
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

} // namespace neospice::mes
