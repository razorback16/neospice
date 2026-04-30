/**********
Based on jfetmpar.c
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles

Modified to add PS model and new parameter definitions ( Anthony E. Parker )
   Copyright 1994  Macquarie University, Sydney Australia.
   10 Feb 1994: Added call to jfetparm.h
**********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/jfet2/jfet2_def.hpp"
#include "devices/jfet2/jfet2_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "devices/ucb_compat.hpp"

namespace neospice::jfet2 {

using namespace Shim;

int
JFET2mParam(int param, Shim::IfValue *value, JFET2Model *inModels)
{
    JFET2Model *model = (JFET2Model*)inModels;
    switch(param) {
        case JFET2_MOD_TNOM:
            model->JFET2tnomGiven = TRUE;
            model->JFET2tnom = value->rValue+CONSTCtoK;
            break;
#define PARAM(code,id,flag,ref,default,descrip) case id: \
                      model->flag = TRUE; model->ref = value->rValue; break;
#include "jfet2parm.hpp"
        case JFET2_MOD_NJF:
            if(value->iValue) {
                model->JFET2type = NJF;
            }
            break;
        case JFET2_MOD_PJF:
            if(value->iValue) {
                model->JFET2type = PJF;
            }
            break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}

} // namespace neospice::jfet2
