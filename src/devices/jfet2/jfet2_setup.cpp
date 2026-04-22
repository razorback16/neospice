/**********
Based on jfetset.c
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles

Modified to add PS model and new parameter definitions ( Anthony E. Parker )
   Copyright 1994  Macquarie University, Sydney Australia.
   10 Feb 1994: Added call to jfetparm.h, used JFET_STATE_COUNT
**********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/jfet2/jfet2_def.hpp"
#include "devices/jfet2/jfet2_shim.hpp"
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

namespace neospice::jfet2 {

using namespace Shim;

int
JFET2setup(Shim::Matrix *matrix, JFET2Model *inModel, Shim::Ckt *ckt, int *states)
        /* load the diode structure with those pointers needed later 
         * for fast matrix loading 
         */
{
    JFET2Model *model = inModel;
    JFET2Instance *here;
    int error;
    Shim::CKTnode *tmp;

    /*  loop through all the diode models */
    for( ; model != NULL; model = model->JFET2nextModel ) {

        if( (model->JFET2type != NJF) && (model->JFET2type != PJF) ) {
            model->JFET2type = NJF;
        }
#define  PARAM(code,id,flag,ref,default,descrip) \
              if(!model->flag) {model->ref = default;}
#include "jfet2parm.h"

        /* loop through all the instances of the model */
        for (here = model->JFET2instances; here != NULL ;
                here=here->JFET2nextInstance) {
            
            if(!here->JFET2areaGiven) {
                here->JFET2area = 1;
            }
  
            if(!here->JFET2mGiven) {
                here->JFET2m = 1;
            }

            here->JFET2state = *states;
            *states += JFET2_STATE_COUNT + 1;

            if(model->JFET2rs != 0) {
                if(here->JFET2sourcePrimeNode == 0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->JFET2name,"source");
                if(error) return(error);
                here->JFET2sourcePrimeNode = tmp->number;

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
                here->JFET2sourcePrimeNode = here->JFET2sourceNode;
            }
            if(model->JFET2rd != 0) {
                if(here->JFET2drainPrimeNode == 0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->JFET2name,"drain");
                if(error) return(error);
                here->JFET2drainPrimeNode = tmp->number;

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
                here->JFET2drainPrimeNode = here->JFET2drainNode;
            }

/* macro to make elements with built in test for out of memory */
#define TSTALLOC(ptr,first,second) \
{ here->ptr = matrix->make_elt(here->first, here->second); }

            /* TODO(translator): TSTALLOC macro kept as-is; needs manual rewrite. */

            TSTALLOC(JFET2drainDrainPrimePtr,JFET2drainNode,JFET2drainPrimeNode);
            TSTALLOC(JFET2gateDrainPrimePtr,JFET2gateNode,JFET2drainPrimeNode);
            TSTALLOC(JFET2gateSourcePrimePtr,JFET2gateNode,JFET2sourcePrimeNode);
            TSTALLOC(JFET2sourceSourcePrimePtr,JFET2sourceNode,JFET2sourcePrimeNode);
            TSTALLOC(JFET2drainPrimeDrainPtr,JFET2drainPrimeNode,JFET2drainNode);
            TSTALLOC(JFET2drainPrimeGatePtr,JFET2drainPrimeNode,JFET2gateNode);
            TSTALLOC(JFET2drainPrimeSourcePrimePtr,JFET2drainPrimeNode,JFET2sourcePrimeNode);
            TSTALLOC(JFET2sourcePrimeGatePtr,JFET2sourcePrimeNode,JFET2gateNode);
            TSTALLOC(JFET2sourcePrimeSourcePtr,JFET2sourcePrimeNode,JFET2sourceNode);
            TSTALLOC(JFET2sourcePrimeDrainPrimePtr,JFET2sourcePrimeNode,JFET2drainPrimeNode);
            TSTALLOC(JFET2drainDrainPtr,JFET2drainNode,JFET2drainNode);
            TSTALLOC(JFET2gateGatePtr,JFET2gateNode,JFET2gateNode);
            TSTALLOC(JFET2sourceSourcePtr,JFET2sourceNode,JFET2sourceNode);
            TSTALLOC(JFET2drainPrimeDrainPrimePtr,JFET2drainPrimeNode,JFET2drainPrimeNode);
            TSTALLOC(JFET2sourcePrimeSourcePrimePtr,JFET2sourcePrimeNode,JFET2sourcePrimeNode);
        }
    }
    return 0;
}

int
JFET2unsetup(JFET2Model *inModel, Shim::Ckt *ckt)
{
    JFET2Model *model;
    JFET2Instance *here;

    for (model = inModel; model != NULL;
	    model = model->JFET2nextModel)
    {
        for (here = model->JFET2instances; here != NULL;
                here=here->JFET2nextInstance)
	{
	    if (here->JFET2sourcePrimeNode
		    && here->JFET2sourcePrimeNode != here->JFET2sourceNode)
	    {
		Shim::CKTdltNNum(ckt, here->JFET2sourcePrimeNode);
		here->JFET2sourcePrimeNode = 0;
	    }
	    if (here->JFET2drainPrimeNode
		    && here->JFET2drainPrimeNode != here->JFET2drainNode)
	    {
		Shim::CKTdltNNum(ckt, here->JFET2drainPrimeNode);
		here->JFET2drainPrimeNode = 0;
	    }
	}
    }
    return 0;
}

} // namespace neospice::jfet2
