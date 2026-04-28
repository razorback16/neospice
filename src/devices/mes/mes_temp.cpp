/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 S. Hwang
**********/

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
MEStemp(MESModel *inModel, Shim::Ckt *ckt)
        /* load the mes structure with those pointers needed later 
         * for fast matrix loading 
         */
{
    MESModel *model = inModel;
    double xfc, temp;

    NG_IGNORE(ckt);

    /*  loop through all the diode models */
    for( ; model != NULL; model = model->MESnextModel ) {

        if(model->MESdrainResist != 0) {
            model->MESdrainConduct = 1/model->MESdrainResist;
        } else {
            model->MESdrainConduct = 0;
        }
        if(model->MESsourceResist != 0) {
            model->MESsourceConduct = 1/model->MESsourceResist;
        } else {
            model->MESsourceConduct = 0;
        }

        model->MESdepletionCap = model->MESdepletionCapCoeff *
                model->MESgatePotential;
        xfc = (1 - model->MESdepletionCapCoeff);
        temp = sqrt(xfc);
        model->MESf1 = model->MESgatePotential * (1 - temp)/(1-.5);
        model->MESf2 = temp * temp * temp;
        model->MESf3 = 1 - model->MESdepletionCapCoeff * (1 + .5);
        model->MESvcrit = CONSTvt0 * log(CONSTvt0/
                (CONSTroot2 * model->MESgateSatCurrent));

    }
    return 0;
}

} // namespace neospice::mes
