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
#include "devices/ucb_compat.hpp"

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
