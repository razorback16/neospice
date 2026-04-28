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

int
MESmParam(int param, Shim::IfValue *value, MESModel *inModel)
{
    MESModel *model = inModel;
    switch(param) {
        case MES_MOD_VTO:
            model->MESthresholdGiven = TRUE;
            model->MESthreshold = value->rValue;
            break;
        case MES_MOD_ALPHA:
            model->MESalphaGiven = TRUE;
            model->MESalpha = value->rValue;
            break;
        case MES_MOD_BETA:
            model->MESbetaGiven = TRUE;
            model->MESbeta = value->rValue;
            break;
        case MES_MOD_LAMBDA:
            model->MESlModulationGiven = TRUE;
            model->MESlModulation = value->rValue;
            break;
        case MES_MOD_B:
            model->MESbGiven = TRUE;
            model->MESb = value->rValue;
            break;
        case MES_MOD_RD:
            model->MESdrainResistGiven = TRUE;
            model->MESdrainResist = value->rValue;
            break;
        case MES_MOD_RS:
            model->MESsourceResistGiven = TRUE;
            model->MESsourceResist = value->rValue;
            break;
        case MES_MOD_CGS:
            model->MEScapGSGiven = TRUE;
            model->MEScapGS = value->rValue;
            break;
        case MES_MOD_CGD:
            model->MEScapGDGiven = TRUE;
            model->MEScapGD = value->rValue;
            break;
        case MES_MOD_PB:
            model->MESgatePotentialGiven = TRUE;
            model->MESgatePotential = value->rValue;
            break;
        case MES_MOD_IS:
            model->MESgateSatCurrentGiven = TRUE;
            model->MESgateSatCurrent = value->rValue;
            break;
        case MES_MOD_FC:
            model->MESdepletionCapCoeffGiven = TRUE;
            model->MESdepletionCapCoeff = value->rValue;
            break;
        case MES_MOD_NMF:
            if(value->iValue) {
                model->MEStype = NMF;
            }
            break;
        case MES_MOD_PMF:
            if(value->iValue) {
                model->MEStype = PMF;
            }
            break;
	case MES_MOD_KF:
	    model->MESfNcoefGiven = TRUE;
	    model->MESfNcoef = value->rValue;
	    break;
	case MES_MOD_AF:
	    model->MESfNexpGiven = TRUE;
	    model->MESfNexp = value->rValue;
	    break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}

} // namespace neospice::mes
