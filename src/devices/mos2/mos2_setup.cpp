/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified: 2000 AlansFixes
**********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/mos2/mos2_def.hpp"
#include "devices/mos2/mos2_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

#include "devices/ucb_compat.hpp"

namespace neospice::mos2 {

using namespace Shim;

int
MOS2setup(Shim::Matrix *matrix, MOS2Model *inModel, Shim::Ckt *ckt, int *states)
        /* load the MOS2 device structure with those pointers needed later 
         * for fast matrix loading 
         */
{
    MOS2Model *model = inModel;
    MOS2Instance *here;
    int error;
    Shim::CKTnode *tmp;

    /*  loop through all the MOS2 device models */
    for( ; model != NULL; model = MOS2nextModel(model)) {

        if(!model->MOS2typeGiven) {
            model->MOS2type = NMOS;
        }
        if(!model->MOS2latDiffGiven) {
            model->MOS2latDiff = 0;
        }
        if(!model->MOS2jctSatCurDensityGiven) {
            model->MOS2jctSatCurDensity = 0;
        }
        if(!model->MOS2jctSatCurGiven) {
            model->MOS2jctSatCur = 1e-14;
        }
        if(!model->MOS2drainResistanceGiven) {
            model->MOS2drainResistance = 0;
        }
        if(!model->MOS2sourceResistanceGiven) {
            model->MOS2sourceResistance = 0;
        }
        if(!model->MOS2sheetResistanceGiven) {
            model->MOS2sheetResistance = 0;
        }
        if(!model->MOS2gateSourceOverlapCapFactorGiven) {
            model->MOS2gateSourceOverlapCapFactor = 0;
        }
        if(!model->MOS2gateDrainOverlapCapFactorGiven) {
            model->MOS2gateDrainOverlapCapFactor = 0;
        }
        if(!model->MOS2gateBulkOverlapCapFactorGiven) {
            model->MOS2gateBulkOverlapCapFactor = 0;
        }
        if(!model->MOS2vt0Given) {
            model->MOS2vt0 = 0;
        }
        if(!model->MOS2bulkJctPotentialGiven) {
            model->MOS2bulkJctPotential = .8;
        }
        if(!model->MOS2capBDGiven) {
            model->MOS2capBD = 0;
        }
        if(!model->MOS2capBSGiven) {
            model->MOS2capBS = 0;
        }
        if(!model->MOS2sideWallCapFactorGiven) {
            model->MOS2sideWallCapFactor = 0;
        }
        if(!model->MOS2bulkJctBotGradingCoeffGiven) {
            model->MOS2bulkJctBotGradingCoeff = .5;
        }
        if(!model->MOS2bulkJctSideGradingCoeffGiven) {
            model->MOS2bulkJctSideGradingCoeff = .33;
        }
        if(!model->MOS2fwdCapDepCoeffGiven) {
            model->MOS2fwdCapDepCoeff = .5;
        }
        if(!model->MOS2phiGiven) {
            model->MOS2phi = .6;
        }
        if(!model->MOS2lambdaGiven) {
            model->MOS2lambda = 0;
        }
        if(!model->MOS2gammaGiven) {
            model->MOS2gamma = 0;
        }
        if(!model->MOS2narrowFactorGiven) {
            model->MOS2narrowFactor = 0;
        }
        if(!model->MOS2critFieldExpGiven) {
            model->MOS2critFieldExp = 0;
        }
        if(!model->MOS2critFieldGiven) {
            model->MOS2critField = 1e4;
        }
        if(!model->MOS2maxDriftVelGiven) {
            model->MOS2maxDriftVel = 0;
        }
        if(!model->MOS2junctionDepthGiven) {
            model->MOS2junctionDepth = 0;
        }
        if(!model->MOS2channelChargeGiven) {
            model->MOS2channelCharge = 1;
        }
        if(!model->MOS2fastSurfaceStateDensityGiven) {
            model->MOS2fastSurfaceStateDensity = 0;
        }
	if(!model->MOS2fNcoefGiven) {
	    model->MOS2fNcoef = 0;
	}
	if(!model->MOS2fNexpGiven) {
	    model->MOS2fNexp = 1;
	}

        /* loop through all the instances of the model */
        for (here = MOS2instances(model); here != NULL ;
                here=MOS2nextInstance(here)) {
         
         Shim::CKTnode *tmpNode;
         const char * tmpName;
            
            /* allocate a chunk of the state vector */
            here->MOS2states = *states;
            *states += MOS2numStates;


            if(!here->MOS2drainPerimiterGiven) {
                here->MOS2drainPerimiter = 0;
            }
            if(!here->MOS2icVBSGiven) {
                here->MOS2icVBS = 0;
            }
            if(!here->MOS2icVDSGiven) {
                here->MOS2icVDS = 0;
            }
            if(!here->MOS2icVGSGiven) {
                here->MOS2icVGS = 0;
            }
            if(!here->MOS2sourcePerimiterGiven) {
                here->MOS2sourcePerimiter = 0;
            }
            if(!here->MOS2vdsatGiven) {
                here->MOS2vdsat = 0;
            }
	    if (!here->MOS2drainSquaresGiven) {
		here->MOS2drainSquares=1;
	    }
	    if (!here->MOS2sourceSquaresGiven) {
		here->MOS2sourceSquares=1;
	    }
            if ((model->MOS2drainResistance != 0
                    || (here->MOS2drainSquares != 0
		    && model->MOS2sheetResistance != 0))) {
                if (here->MOS2dNodePrime == 0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->MOS2name,"internal#drain");
                if(error) return(error);
                here->MOS2dNodePrime = tmp->number;
                
                if (ckt->CKTcopyNodesets) {
                  if (Shim::CKTinst2Node(ckt,here,1,&tmpNode,&tmpName)==OK) {
                     if (tmpNode->nsGiven) {
                       tmp->nodeset=tmpNode->nodeset; 
                       tmp->nsGiven=tmpNode->nsGiven; 
                     }
                  }
                }
                }
                
            } else {
                here->MOS2dNodePrime = here->MOS2dNode;
            }

            if( ( (model->MOS2sourceResistance != 0) || 
                    ((here->MOS2sourceSquares != 0) &&
                     (model->MOS2sheetResistance != 0)) )) {
                if (here->MOS2sNodePrime == 0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->MOS2name,"internal#source");
                if(error) return(error);
                here->MOS2sNodePrime = tmp->number;
                
                if (ckt->CKTcopyNodesets) {
                  if (Shim::CKTinst2Node(ckt,here,3,&tmpNode,&tmpName)==OK) {
                     if (tmpNode->nsGiven) {
                       tmp->nodeset=tmpNode->nodeset; 
                       tmp->nsGiven=tmpNode->nsGiven; 
                     }
                  }
                }
                }
                
            } else {
                here->MOS2sNodePrime = here->MOS2sNode;
            }

/* macro to make elements with built in test for out of memory */
#define TSTALLOC(ptr,first,second) \
{ here->ptr = matrix->make_elt(here->first, here->second); }

            /* TODO(translator): TSTALLOC macro kept as-is; needs manual rewrite. */

            TSTALLOC(MOS2DdPtr, MOS2dNode, MOS2dNode);
            TSTALLOC(MOS2GgPtr, MOS2gNode, MOS2gNode);
            TSTALLOC(MOS2SsPtr, MOS2sNode, MOS2sNode);
            TSTALLOC(MOS2BbPtr, MOS2bNode, MOS2bNode);
            TSTALLOC(MOS2DPdpPtr, MOS2dNodePrime, MOS2dNodePrime);
            TSTALLOC(MOS2SPspPtr, MOS2sNodePrime, MOS2sNodePrime);
            TSTALLOC(MOS2DdpPtr, MOS2dNode, MOS2dNodePrime);
            TSTALLOC(MOS2GbPtr, MOS2gNode, MOS2bNode);
            TSTALLOC(MOS2GdpPtr, MOS2gNode, MOS2dNodePrime);
            TSTALLOC(MOS2GspPtr, MOS2gNode, MOS2sNodePrime);
            TSTALLOC(MOS2SspPtr, MOS2sNode, MOS2sNodePrime);
            TSTALLOC(MOS2BdpPtr, MOS2bNode, MOS2dNodePrime);
            TSTALLOC(MOS2BspPtr, MOS2bNode, MOS2sNodePrime);
            TSTALLOC(MOS2DPspPtr, MOS2dNodePrime, MOS2sNodePrime);
            TSTALLOC(MOS2DPdPtr, MOS2dNodePrime, MOS2dNode);
            TSTALLOC(MOS2BgPtr, MOS2bNode, MOS2gNode);
            TSTALLOC(MOS2DPgPtr, MOS2dNodePrime, MOS2gNode);
            TSTALLOC(MOS2SPgPtr, MOS2sNodePrime, MOS2gNode);
            TSTALLOC(MOS2SPsPtr, MOS2sNodePrime, MOS2sNode);
            TSTALLOC(MOS2DPbPtr, MOS2dNodePrime, MOS2bNode);
            TSTALLOC(MOS2SPbPtr, MOS2sNodePrime, MOS2bNode);
            TSTALLOC(MOS2SPdpPtr, MOS2sNodePrime, MOS2dNodePrime);

        }
    }
    return 0;
}

int
MOS2unsetup(MOS2Model *inModel, Shim::Ckt *ckt)
{
    MOS2Model *model;
    MOS2Instance *here;

    for (model = inModel; model != NULL;
	    model = MOS2nextModel(model))
    {
        for (here = MOS2instances(model); here != NULL;
                here=MOS2nextInstance(here))
	{
	    if (here->MOS2sNodePrime > 0
		    && here->MOS2sNodePrime != here->MOS2sNode)
		Shim::CKTdltNNum(ckt, here->MOS2sNodePrime);
            here->MOS2sNodePrime = 0;

	    if (here->MOS2dNodePrime > 0
		    && here->MOS2dNodePrime != here->MOS2dNode)
		Shim::CKTdltNNum(ckt, here->MOS2dNodePrime);
            here->MOS2dNodePrime = 0;
	}
    }
    return 0;
}

} // namespace neospice::mos2
