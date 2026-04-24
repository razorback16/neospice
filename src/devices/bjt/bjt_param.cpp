/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
**********/
/*
 */

/*
 * This routine sets instance parameters for
 * BJTs in the circuit.
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/bjt/bjt_def.hpp"
#include "devices/bjt/bjt_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "devices/ucb_compat.hpp"

namespace neospice::bjt {

using namespace Shim;

/* ARGSUSED */
int
BJTparam(int param, Shim::IfValue *value, BJTInstance *instPtr, Shim::IfValue *select)
{
    BJTInstance *here = (BJTInstance*)instPtr;

    NG_IGNORE(select);

    switch(param) {
        case BJT_AREA:
            here->BJTarea = value->rValue;
            here->BJTareaGiven = TRUE;
            break;
	case BJT_AREAB:
            here->BJTareab = value->rValue;
            here->BJTareabGiven = TRUE;
            break;   
	case BJT_AREAC:
            here->BJTareac = value->rValue;
            here->BJTareacGiven = TRUE;
            break;     
        case BJT_M:
            here->BJTm = value->rValue;
            here->BJTmGiven = TRUE;
            break;	    
        case BJT_TEMP:
            here->BJTtemp = value->rValue + CONSTCtoK;
            here->BJTtempGiven = TRUE;
            break;
        case BJT_DTEMP:
            here->BJTdtemp = value->rValue;
            here->BJTdtempGiven = TRUE;
            break;	    
        case BJT_OFF:
            here->BJToff = (value->iValue != 0);
            break;
        case BJT_IC_VBE:
            here->BJTicVBE = value->rValue;
            here->BJTicVBEGiven = TRUE;
            break;
        case BJT_IC_VCE:
            here->BJTicVCE = value->rValue;
            here->BJTicVCEGiven = TRUE;
            break;
        case BJT_AREA_SENS:
            here->BJTsenParmNo = value->iValue;
            break;
        case BJT_IC :
            switch(value->v.numValue) {
                case 2:
                    here->BJTicVCE = *(value->v.vec.rVec+1);
                    here->BJTicVCEGiven = TRUE;
                case 1:
                    here->BJTicVBE = *(value->v.vec.rVec);
                    here->BJTicVBEGiven = TRUE;
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

} // namespace neospice::bjt
