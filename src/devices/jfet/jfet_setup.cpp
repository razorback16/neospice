/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified: 2000 AlansFixes
Sydney University mods Copyright(c) 1989 Anthony E. Parker, David J. Skellern
        Laboratory for Communication Science Engineering
        Sydney University Department of Electrical Engineering, Australia
**********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/jfet/jfet_def.hpp"
#include "devices/jfet/jfet_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "devices/ucb_compat.hpp"

namespace neospice::jfet {

using namespace Shim;

int
JFETsetup(Shim::Matrix *matrix, JFETModel *inModel, Shim::Ckt *ckt, int *states)
        /* load the diode structure with those pointers needed later
         * for fast matrix loading
         */
{
    JFETModel *model = inModel;
    JFETInstance *here;
    int error;
    Shim::CKTnode *tmp;

    /*  loop through all the diode models */
    for( ; model != NULL; model = model->JFETnextModel ) {

        if( (model->JFETtype != NJF) && (model->JFETtype != PJF) ) {
            model->JFETtype = NJF;
        }
        if(!model->JFETthresholdGiven) {
            model->JFETthreshold = -2;
        }
        if(!model->JFETbetaGiven) {
            model->JFETbeta = 1e-4;
        }
        if(!model->JFETlModulationGiven) {
            model->JFETlModulation = 0;
        }
        if(!model->JFETdrainResistGiven) {
            model->JFETdrainResist = 0;
        }
        if(!model->JFETsourceResistGiven) {
            model->JFETsourceResist = 0;
        }
        if(!model->JFETcapGSGiven) {
            model->JFETcapGS = 0;
        }
        if(!model->JFETcapGDGiven) {
            model->JFETcapGD = 0;
        }
        if(!model->JFETgatePotentialGiven) {
            model->JFETgatePotential = 1;
        }
        if(!model->JFETgateSatCurrentGiven) {
            model->JFETgateSatCurrent = 1e-14;
        }
        if(!model->JFETdepletionCapCoeffGiven) {
            model->JFETdepletionCapCoeff = .5;
        }
        if(!model->JFETfNcoefGiven) {
            model->JFETfNcoef = 0;
        }
        if(!model->JFETfNexpGiven) {
            model->JFETfNexp = 1;
        }

        /* Modification for Sydney University JFET model */
        if(!model->JFETbGiven) {
            model->JFETb = 1.0;
        }
        /* end Sydney University mod */

        if(!model->JFETtcvGiven) {
            model->JFETtcv = 0.0;
        }
        if(!model->JFETbexGiven) {
            model->JFETbex = 0.0;
        }
        if(!model->JFETnlevGiven) {
            model->JFETnlev = 2;
        }
        if(!model->JFETgdsnoiGiven) {
            model->JFETgdsnoi = 1.0;
        }

        if(model->JFETdrainResist != 0) {
            model->JFETdrainConduct = 1/model->JFETdrainResist;
        } else {
            model->JFETdrainConduct = 0;
        }
        if(model->JFETsourceResist != 0) {
            model->JFETsourceConduct = 1/model->JFETsourceResist;
        } else {
            model->JFETsourceConduct = 0;
        }

        /* loop through all the instances of the model */
        for (here = model->JFETinstances; here != NULL ;
                here=here->JFETnextInstance) {

            if(!here->JFETareaGiven) {
                here->JFETarea = 1;
            }
            if(!here->JFETmGiven) {
                here->JFETm = 1;
            }
            here->JFETstate = *states;
            *states += 13;

            if(model->JFETsourceResist != 0) {
                if(here->JFETsourcePrimeNode == 0) {
                    error = Shim::CKTmkVolt(ckt,&tmp,here->JFETname,"source");
                    if(error) return(error);
                    here->JFETsourcePrimeNode = tmp->number;

                    if (ckt->CKTcopyNodesets) {
                        Shim::CKTnode *tmpNode;
                        const char * tmpName;

                        if (Shim::CKTinst2Node(ckt,here,3,&tmpNode,&tmpName)==OK) {
                            if (tmpNode->nsGiven) {
                                tmp->nodeset=tmpNode->nodeset;
                                tmp->nsGiven=tmpNode->nsGiven;
                            }
                        }
                    }
                }

            } else {
                here->JFETsourcePrimeNode = here->JFETsourceNode;
            }
            if(model->JFETdrainResist != 0) {
                if(here->JFETdrainPrimeNode == 0) {
                    error = Shim::CKTmkVolt(ckt,&tmp,here->JFETname,"drain");
                    if(error) return(error);
                    here->JFETdrainPrimeNode = tmp->number;

                    if (ckt->CKTcopyNodesets) {
                        Shim::CKTnode *tmpNode;
                        const char * tmpName;

                        if (Shim::CKTinst2Node(ckt,here,1,&tmpNode,&tmpName)==OK) {
                            if (tmpNode->nsGiven) {
                                tmp->nodeset=tmpNode->nodeset;
                                tmp->nsGiven=tmpNode->nsGiven;
                            }
                        }
                    }
                }

            } else {
                here->JFETdrainPrimeNode = here->JFETdrainNode;
            }

/* macro to make elements with built in test for out of memory */
#define TSTALLOC(ptr,first,second) \
{ here->ptr = matrix->make_elt(here->first, here->second); }

            TSTALLOC(JFETdrainDrainPrimePtr,JFETdrainNode,JFETdrainPrimeNode);
            TSTALLOC(JFETgateDrainPrimePtr,JFETgateNode,JFETdrainPrimeNode);
            TSTALLOC(JFETgateSourcePrimePtr,JFETgateNode,JFETsourcePrimeNode);
            TSTALLOC(JFETsourceSourcePrimePtr,JFETsourceNode,JFETsourcePrimeNode);
            TSTALLOC(JFETdrainPrimeDrainPtr,JFETdrainPrimeNode,JFETdrainNode);
            TSTALLOC(JFETdrainPrimeGatePtr,JFETdrainPrimeNode,JFETgateNode);
            TSTALLOC(JFETdrainPrimeSourcePrimePtr,JFETdrainPrimeNode,JFETsourcePrimeNode);
            TSTALLOC(JFETsourcePrimeGatePtr,JFETsourcePrimeNode,JFETgateNode);
            TSTALLOC(JFETsourcePrimeSourcePtr,JFETsourcePrimeNode,JFETsourceNode);
            TSTALLOC(JFETsourcePrimeDrainPrimePtr,JFETsourcePrimeNode,JFETdrainPrimeNode);
            TSTALLOC(JFETdrainDrainPtr,JFETdrainNode,JFETdrainNode);
            TSTALLOC(JFETgateGatePtr,JFETgateNode,JFETgateNode);
            TSTALLOC(JFETsourceSourcePtr,JFETsourceNode,JFETsourceNode);
            TSTALLOC(JFETdrainPrimeDrainPrimePtr,JFETdrainPrimeNode,JFETdrainPrimeNode);
            TSTALLOC(JFETsourcePrimeSourcePrimePtr,JFETsourcePrimeNode,JFETsourcePrimeNode);
        }
    }
    return 0;
}

int
JFETunsetup(JFETModel *inModel, Shim::Ckt *ckt)
{
    JFETModel *model;
    JFETInstance *here;

    for (model = inModel; model != NULL;
            model = model->JFETnextModel)
    {
        for (here = model->JFETinstances; here != NULL;
                here=here->JFETnextInstance)
        {
            if (here->JFETsourcePrimeNode
                    && here->JFETsourcePrimeNode != here->JFETsourceNode)
            {
                Shim::CKTdltNNum(ckt, here->JFETsourcePrimeNode);
                here->JFETsourcePrimeNode = 0;
            }
            if (here->JFETdrainPrimeNode
                    && here->JFETdrainPrimeNode != here->JFETdrainNode)
            {
                Shim::CKTdltNNum(ckt, here->JFETdrainPrimeNode);
                here->JFETdrainPrimeNode = 0;
            }
        }
    }
    return 0;
}

} // namespace neospice::jfet
