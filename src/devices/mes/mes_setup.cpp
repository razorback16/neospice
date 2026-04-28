/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 S. Hwang
Modified: 2000 AlansFixes
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

int
MESsetup(Shim::Matrix *matrix, MESModel *inModel, Shim::Ckt *ckt, int *states)
        /* load the diode structure with those pointers needed later 
         * for fast matrix loading 
         */
{
    MESModel *model = inModel;
    MESInstance *here;
    int error;
    Shim::CKTnode *tmp;

    /*  loop through all the diode models */
    for( ; model != NULL; model = model->MESnextModel ) {

        if( (model->MEStype != NMF) && (model->MEStype != PMF) ) {
            model->MEStype = NMF;
        }
        if(!model->MESthresholdGiven) {
            model->MESthreshold = -2;
        }
        if(!model->MESbetaGiven) {
            model->MESbeta = 2.5e-3;
        }
        if(!model->MESbGiven) {
            model->MESb = 0.3;
        }
        if(!model->MESalphaGiven) {
            model->MESalpha = 2;
        }
        if(!model->MESlModulationGiven) {
            model->MESlModulation = 0;
        }
        if(!model->MESdrainResistGiven) {
            model->MESdrainResist = 0;
        }
        if(!model->MESsourceResistGiven) {
            model->MESsourceResist = 0;
        }
        if(!model->MEScapGSGiven) {
            model->MEScapGS = 0;
        }
        if(!model->MEScapGDGiven) {
            model->MEScapGD = 0;
        }
        if(!model->MESgatePotentialGiven) {
            model->MESgatePotential = 1;
        }
        if(!model->MESgateSatCurrentGiven) {
            model->MESgateSatCurrent = 1e-14;
        }
        if(!model->MESdepletionCapCoeffGiven) {
            model->MESdepletionCapCoeff = .5;
        }
	if(!model->MESfNcoefGiven) {
	    model->MESfNcoef = 0;
	}
	if(!model->MESfNexpGiven) {
	    model->MESfNexp = 1;
	}

        /* loop through all the instances of the model */
        for (here = model->MESinstances; here != NULL ;
                here=here->MESnextInstance) {
            
            if(!here->MESareaGiven) {
                here->MESarea = 1.0;
            }
            if(!here->MESmGiven) {
                here->MESm = 1.0;
            }
            here->MESstate = *states;
            *states += MESnumStates;

            if(model->MESsourceResist != 0) {
                if(here->MESsourcePrimeNode == 0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->MESname,"source");
                if(error) return(error);
                here->MESsourcePrimeNode = tmp->number;
                
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
                here->MESsourcePrimeNode = here->MESsourceNode;
            }
            if(model->MESdrainResist != 0) {
                if(here->MESdrainPrimeNode == 0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->MESname,"drain");
                if(error) return(error);
                here->MESdrainPrimeNode = tmp->number;
                
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
                here->MESdrainPrimeNode = here->MESdrainNode;
            }

/* macro to make elements with built in test for out of memory */
#define TSTALLOC(ptr,first,second) \
{ here->ptr = matrix->make_elt(here->first, here->second); }

            /* TODO(translator): TSTALLOC macro kept as-is; needs manual rewrite. */

            TSTALLOC(MESdrainDrainPrimePtr,MESdrainNode,MESdrainPrimeNode);
            TSTALLOC(MESgateDrainPrimePtr,MESgateNode,MESdrainPrimeNode);
            TSTALLOC(MESgateSourcePrimePtr,MESgateNode,MESsourcePrimeNode);
            TSTALLOC(MESsourceSourcePrimePtr,MESsourceNode,MESsourcePrimeNode);
            TSTALLOC(MESdrainPrimeDrainPtr,MESdrainPrimeNode,MESdrainNode);
            TSTALLOC(MESdrainPrimeGatePtr,MESdrainPrimeNode,MESgateNode);
            TSTALLOC(MESdrainPrimeSourcePrimePtr,MESdrainPrimeNode,MESsourcePrimeNode);
            TSTALLOC(MESsourcePrimeGatePtr,MESsourcePrimeNode,MESgateNode);
            TSTALLOC(MESsourcePrimeSourcePtr,MESsourcePrimeNode,MESsourceNode);
            TSTALLOC(MESsourcePrimeDrainPrimePtr,MESsourcePrimeNode,MESdrainPrimeNode);
            TSTALLOC(MESdrainDrainPtr,MESdrainNode,MESdrainNode);
            TSTALLOC(MESgateGatePtr,MESgateNode,MESgateNode);
            TSTALLOC(MESsourceSourcePtr,MESsourceNode,MESsourceNode);
            TSTALLOC(MESdrainPrimeDrainPrimePtr,MESdrainPrimeNode,MESdrainPrimeNode);
            TSTALLOC(MESsourcePrimeSourcePrimePtr,MESsourcePrimeNode,MESsourcePrimeNode);
        }
    }
    return 0;
}

int
MESunsetup(MESModel *inModel, Shim::Ckt *ckt)
{
    MESModel *model;
    MESInstance *here;

    for (model = inModel; model != NULL;
	    model = model->MESnextModel)
    {
        for (here = model->MESinstances; here != NULL;
                here=here->MESnextInstance)
	{
	    if (here->MESsourcePrimeNode
		    && here->MESsourcePrimeNode != here->MESsourceNode)
	    {
		Shim::CKTdltNNum(ckt, here->MESsourcePrimeNode);
		here->MESsourcePrimeNode = 0;
	    }
	    if (here->MESdrainPrimeNode
		    && here->MESdrainPrimeNode != here->MESdrainNode)
	    {
		Shim::CKTdltNNum(ckt, here->MESdrainPrimeNode);
		here->MESdrainPrimeNode = 0;
	    }
	}
    }
    return 0;
}

} // namespace neospice::mes
