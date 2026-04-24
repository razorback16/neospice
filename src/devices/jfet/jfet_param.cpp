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
#include "devices/ucb_compat.hpp"

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
