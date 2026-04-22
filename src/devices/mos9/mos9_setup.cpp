/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified: Alan Gillespie
**********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/mos9/mos9_def.hpp"
#include "devices/mos9/mos9_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef CONSTvt0
#define CONSTvt0 0.025852037
#endif
#ifndef CONSTroot2
#define CONSTroot2 1.4142135623730950488
#endif
#ifndef CONSTCtoK
#define CONSTCtoK 273.15
#endif
#ifndef CHARGE
#define CHARGE 1.6021918e-19
#endif
#ifndef FABS
#define FABS(x) std::fabs(x)
#endif
#ifndef ABS
#define ABS(x) std::fabs(x)
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef TMALLOC
#define TMALLOC(type, num) (new type[num]())
#endif
#ifndef NG_IGNORE
#define NG_IGNORE(x) (void)(x)
#endif
#ifndef cp_getvar
#define cp_getvar(name, type, ptr) 0
#endif
#ifndef CP_REAL
#define CP_REAL 0
#endif
#ifndef NUMELEMS
#define NUMELEMS(ARRAY) (sizeof(ARRAY)/sizeof(*(ARRAY)))
#endif
#ifndef IOP
#define IOP(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}
#endif
#ifndef IOPU
#define IOPU(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}
#endif
#ifndef IP
#define IP(a,b,c,d) {a, b, (Shim::IF_SET|c), d}
#endif
#ifndef OP
#define OP(a,b,c,d) {a, b, (Shim::IF_ASK|c), d}
#endif
#ifndef OPU
#define OPU(a,b,c,d) {a, b, (Shim::IF_ASK|c), d}
#endif
#ifndef IOPA
#define IOPA(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}
#endif
#ifndef IOPR
#define IOPR(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|Shim::IF_REDUNDANT|c), d}
#endif
#ifndef IOPAU
#define IOPAU(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}
#endif
#ifndef IPR
#define IPR(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_REDUNDANT|c), d}
#endif
#ifndef OPR
#define OPR(a,b,c,d) {a, b, (Shim::IF_ASK|Shim::IF_REDUNDANT|c), d}
#endif

namespace neospice::mos9 {

using namespace Shim;

/* assuming silicon - make definition for epsilon of silicon */
#define EPSSIL (11.7 * 8.854214871e-12)

int
MOS9setup(Shim::Matrix *matrix, MOS9Model *inModel, Shim::Ckt *ckt, int *states)
        /* load the MOS9 device structure with those pointers needed later 
         * for fast matrix loading 
         */

{
    register MOS9Model *model = inModel;
    register MOS9Instance *here;
    int error;
    Shim::CKTnode *tmp;

    /*  loop through all the MOS9 device models */
    for( ; model != NULL; model = model->MOS9nextModel ) {

        /* perform model defaulting */
        if(!model->MOS9typeGiven) {
            model->MOS9type = NMOS;
        }
        if(!model->MOS9latDiffGiven) {
            model->MOS9latDiff = 0;
        }
        if(!model->MOS9lengthAdjustGiven) {
            model->MOS9lengthAdjust = 0;
        }
        if(!model->MOS9widthNarrowGiven) {
            model->MOS9widthNarrow = 0;
        }
        if(!model->MOS9widthAdjustGiven) {
            model->MOS9widthAdjust = 0;
        }
        if(!model->MOS9delvt0Given) {
            model->MOS9delvt0 = 0;
        }
        if(!model->MOS9jctSatCurDensityGiven) {
            model->MOS9jctSatCurDensity = 0;
        }
        if(!model->MOS9jctSatCurGiven) {
            model->MOS9jctSatCur = 1e-14;
        }
        if(!model->MOS9drainResistanceGiven) {
            model->MOS9drainResistance = 0;
        }
        if(!model->MOS9sourceResistanceGiven) {
            model->MOS9sourceResistance = 0;
        }
        if(!model->MOS9sheetResistanceGiven) {
            model->MOS9sheetResistance = 0;
        }
        if(!model->MOS9transconductanceGiven) {
            model->MOS9transconductance = 2e-5;
        }
        if(!model->MOS9gateSourceOverlapCapFactorGiven) {
            model->MOS9gateSourceOverlapCapFactor = 0;
        }
        if(!model->MOS9gateDrainOverlapCapFactorGiven) {
            model->MOS9gateDrainOverlapCapFactor = 0;
        }
        if(!model->MOS9gateBulkOverlapCapFactorGiven) {
            model->MOS9gateBulkOverlapCapFactor = 0;
        }
        if(!model->MOS9vt0Given) {
            model->MOS9vt0 = 0;
        }
        if(!model->MOS9capBDGiven) {
            model->MOS9capBD = 0;
        }
        if(!model->MOS9capBSGiven) {
            model->MOS9capBS = 0;
        }
        if(!model->MOS9bulkCapFactorGiven) {
            model->MOS9bulkCapFactor = 0;
        }
        if(!model->MOS9sideWallCapFactorGiven) {
            model->MOS9sideWallCapFactor = 0;
        }
        if(!model->MOS9bulkJctPotentialGiven) {
            model->MOS9bulkJctPotential = .8;
        }
        if(!model->MOS9bulkJctBotGradingCoeffGiven) {
            model->MOS9bulkJctBotGradingCoeff = .5;
        }
        if(!model->MOS9bulkJctSideGradingCoeffGiven) {
            model->MOS9bulkJctSideGradingCoeff = .33;
        }
        if(!model->MOS9fwdCapDepCoeffGiven) {
            model->MOS9fwdCapDepCoeff = .5;
        }
        if(!model->MOS9phiGiven) {
            model->MOS9phi = .6;
        }
        if(!model->MOS9gammaGiven) {
            model->MOS9gamma = 0;
        }
        if(!model->MOS9deltaGiven) {
            model->MOS9delta = 0;
        }
        if(!model->MOS9maxDriftVelGiven) {
            model->MOS9maxDriftVel = 0;
        }
        if(!model->MOS9junctionDepthGiven) {
            model->MOS9junctionDepth = 0;
        }
        if(!model->MOS9fastSurfaceStateDensityGiven) {
            model->MOS9fastSurfaceStateDensity = 0;
        }
        if(!model->MOS9etaGiven) {
            model->MOS9eta = 0;
        }
        if(!model->MOS9thetaGiven) {
            model->MOS9theta = 0;
        }
        if(!model->MOS9kappaGiven) {
            model->MOS9kappa = .2;
        }
        if(!model->MOS9oxideThicknessGiven) {
            model->MOS9oxideThickness = 1e-7;
        } 
	if(!model->MOS9fNcoefGiven) {
	    model->MOS9fNcoef = 0;
	}
	if(!model->MOS9fNexpGiven) {
	    model->MOS9fNexp = 1;
	}

        /* loop through all the instances of the model */
        for (here = model->MOS9instances; here != NULL ;
                here=here->MOS9nextInstance) {

            Shim::CKTnode *tmpNode;
            const char * tmpName;

            /* allocate a chunk of the state vector */
            here->MOS9states = *states;
            *states += MOS9NUMSTATES;

            if(!here->MOS9drainAreaGiven) {
                here->MOS9drainArea = ckt->CKTdefaultMosAD;
            }
            if(!here->MOS9drainPerimiterGiven) {
                here->MOS9drainPerimiter = 0;
            }
            if(!here->MOS9drainSquaresGiven) {
                here->MOS9drainSquares = 1;
            }
            if(!here->MOS9icVBSGiven) {
                here->MOS9icVBS = 0;
            }
            if(!here->MOS9icVDSGiven) {
                here->MOS9icVDS = 0;
            }
            if(!here->MOS9icVGSGiven) {
                here->MOS9icVGS = 0;
            }
            if(!here->MOS9sourcePerimiterGiven) {
                here->MOS9sourcePerimiter = 0;
            }
            if(!here->MOS9sourceSquaresGiven) {
                here->MOS9sourceSquares = 1;
            }
            if(!here->MOS9vdsatGiven) {
                here->MOS9vdsat = 0;
            }
            if(!here->MOS9vonGiven) {
                here->MOS9von = 0;
            }
            if(!here->MOS9modeGiven) {
                here->MOS9mode = 1;
            }

            if((model->MOS9drainResistance != 0 ||
                    (model->MOS9sheetResistance != 0 &&
                     here->MOS9drainSquares != 0      ) )) {
                if (here->MOS9dNodePrime==0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->MOS9name,"internal#drain");
                if(error) return(error);
                here->MOS9dNodePrime = tmp->number;
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
                here->MOS9dNodePrime = here->MOS9dNode;
            }

            if((model->MOS9sourceResistance != 0 ||
                    (model->MOS9sheetResistance != 0 && 
                     here->MOS9sourceSquares != 0     ) )) {
                if (here->MOS9sNodePrime == 0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->MOS9name,"internal#source");
                if(error) return(error);
                here->MOS9sNodePrime = tmp->number;
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
                here->MOS9sNodePrime = here->MOS9sNode;
            }

/* macro to make elements with built in test for out of memory */
#define TSTALLOC(ptr,first,second) \
{ here->ptr = matrix->make_elt(here->first, here->second); }

            /* TODO(translator): TSTALLOC macro kept as-is; needs manual rewrite. */

            TSTALLOC(MOS9DdPtr, MOS9dNode, MOS9dNode);
            TSTALLOC(MOS9GgPtr, MOS9gNode, MOS9gNode);
            TSTALLOC(MOS9SsPtr, MOS9sNode, MOS9sNode);
            TSTALLOC(MOS9BbPtr, MOS9bNode, MOS9bNode);
            TSTALLOC(MOS9DPdpPtr, MOS9dNodePrime, MOS9dNodePrime);
            TSTALLOC(MOS9SPspPtr, MOS9sNodePrime, MOS9sNodePrime);
            TSTALLOC(MOS9DdpPtr, MOS9dNode, MOS9dNodePrime);
            TSTALLOC(MOS9GbPtr, MOS9gNode, MOS9bNode);
            TSTALLOC(MOS9GdpPtr, MOS9gNode, MOS9dNodePrime);
            TSTALLOC(MOS9GspPtr, MOS9gNode, MOS9sNodePrime);
            TSTALLOC(MOS9SspPtr, MOS9sNode, MOS9sNodePrime);
            TSTALLOC(MOS9BdpPtr, MOS9bNode, MOS9dNodePrime);
            TSTALLOC(MOS9BspPtr, MOS9bNode, MOS9sNodePrime);
            TSTALLOC(MOS9DPspPtr, MOS9dNodePrime, MOS9sNodePrime);
            TSTALLOC(MOS9DPdPtr, MOS9dNodePrime, MOS9dNode);
            TSTALLOC(MOS9BgPtr, MOS9bNode, MOS9gNode);
            TSTALLOC(MOS9DPgPtr, MOS9dNodePrime, MOS9gNode);
            TSTALLOC(MOS9SPgPtr, MOS9sNodePrime, MOS9gNode);
            TSTALLOC(MOS9SPsPtr, MOS9sNodePrime, MOS9sNode);
            TSTALLOC(MOS9DPbPtr, MOS9dNodePrime, MOS9bNode);
            TSTALLOC(MOS9SPbPtr, MOS9sNodePrime, MOS9bNode);
            TSTALLOC(MOS9SPdpPtr, MOS9sNodePrime, MOS9dNodePrime);

        }
    }
    return 0;
}

int
MOS9unsetup(MOS9Model *inModel, Shim::Ckt *ckt)
{
    MOS9Model *model;
    MOS9Instance *here;

    for (model = inModel; model != NULL;
	    model = model->MOS9nextModel)
    {
        for (here = model->MOS9instances; here != NULL;
                here=here->MOS9nextInstance)
	{
	    if (here->MOS9dNodePrime
		    && here->MOS9dNodePrime != here->MOS9dNode)
	    {
		Shim::CKTdltNNum(ckt, here->MOS9dNodePrime);
		here->MOS9dNodePrime= 0;
	    }
	    if (here->MOS9sNodePrime
		    && here->MOS9sNodePrime != here->MOS9sNode)
	    {
		Shim::CKTdltNNum(ckt, here->MOS9sNodePrime);
		here->MOS9sNodePrime= 0;
	    }
	}
    }
    return 0;
}

} // namespace neospice::mos9
