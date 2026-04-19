#define PREDICTOR
/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified: 2000 AlansFixes
Modified by Paolo Nenzi 2003 and Dietmar Warning 2012
**********/

/* load the diode structure with those pointers needed later
 * for fast matrix loading
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/dio/dio_def.hpp"
#include "devices/dio/dio_shim.hpp"
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

namespace neospice::dio {

using namespace Shim;

int
DIOsetup(Shim::Matrix *matrix, DIOModel *inModel, Shim::Ckt *ckt, int *states)
{
    DIOModel *model = inModel;
    DIOInstance *here;
    int error;
    Shim::CKTnode *tmp;

    /*  loop through all the diode models */
    for( ; model != NULL; model = model->DIOnextModel ) {

        if(!model->DIOlevelGiven) {
            model->DIOlevel = 1;
        }
        if(!model->DIOemissionCoeffGiven) {
            model->DIOemissionCoeff = 1;
        }
        if(!model->DIOsatCurGiven) {
            model->DIOsatCur = 1e-14;
        }
        if(!model->DIOsatSWCurGiven) {
            model->DIOsatSWCur = 0.0;
        }
        if(!model->DIOswEmissionCoeffGiven) {
            model->DIOswEmissionCoeff = 1;
        }
        if(!model->DIObreakdownCurrentGiven) {
            model->DIObreakdownCurrent = 1e-3;
        }
        if(!model->DIOjunctionPotGiven){
            model->DIOjunctionPot = 1;
        }
        if(!model->DIOgradingCoeffGiven) {
            model->DIOgradingCoeff = .5;
        }
        if(!model->DIOgradCoeffTemp1Given) {
            model->DIOgradCoeffTemp1 = 0.0;
        }
        if(!model->DIOgradCoeffTemp2Given) {
            model->DIOgradCoeffTemp2 = 0.0;
        }
        if(!model->DIOdepletionCapCoeffGiven) {
            model->DIOdepletionCapCoeff = .5;
        }
        if(!model->DIOdepletionSWcapCoeffGiven) {
            model->DIOdepletionSWcapCoeff = .5;
        }
        if(!model->DIOtransitTimeGiven) {
            model->DIOtransitTime = 0;
        }
        if(!model->DIOtranTimeTemp1Given) {
            model->DIOtranTimeTemp1 = 0.0;
        }
        if(!model->DIOtranTimeTemp2Given) {
            model->DIOtranTimeTemp2 = 0.0;
        }
        if(!model->DIOjunctionCapGiven) {
            model->DIOjunctionCap = 0;
        }
        if(!model->DIOjunctionSWCapGiven) {
            model->DIOjunctionSWCap = 0;
        }
        if(!model->DIOjunctionSWPotGiven){
            model->DIOjunctionSWPot = 1;
        }
        if(!model->DIOgradingSWCoeffGiven) {
            model->DIOgradingSWCoeff = .33;
        }
        if(!model->DIOforwardKneeCurrentGiven) {
            model->DIOforwardKneeCurrent = 0.0;
        }
        if(!model->DIOreverseKneeCurrentGiven) {
            model->DIOreverseKneeCurrent = 0.0;
        }
        if(!model->DIObrkdEmissionCoeffGiven) {
            model->DIObrkdEmissionCoeff = model->DIOemissionCoeff;
        }
        if(!model->DIOtlevGiven) {
            model->DIOtlev = 0;
        }
        if(!model->DIOtlevcGiven) {
            model->DIOtlevc = 0;
        }
        if(!model->DIOactivationEnergyGiven) {
            model->DIOactivationEnergy = 1.11;
        }
        if(!model->DIOsaturationCurrentExpGiven) {
            model->DIOsaturationCurrentExp = 3;
        }
        if(!model->DIOctaGiven) {
            model->DIOcta = 0.0;
        }
        if(!model->DIOctpGiven) {
            model->DIOctp = 0.0;
        }
        if(!model->DIOtpbGiven) {
            model->DIOtpb = 0.0;
        }
        if(!model->DIOtphpGiven) {
            model->DIOtphp = 0.0;
        }
        if(!model->DIOfNcoefGiven) {
            model->DIOfNcoef = 0.0;
        }
        if(!model->DIOfNexpGiven) {
            model->DIOfNexp = 1.0;
        }
        if(!model->DIOresistTemp1Given) {
            model->DIOresistTemp1 = 0.0;
        }
        if(!model->DIOresistTemp2Given) {
            model->DIOresistTemp2 = 0.0;
        }
        if(!model->DIOtcvGiven) {
            model->DIOtcv = 0.0;
        }
        if(!model->DIOareaGiven) {
            model->DIOarea = 1.0;
        }
        if(!model->DIOpjGiven) {
            model->DIOpj = 0.0;
        }
        if(!model->DIOtunSatCurGiven) {
            model->DIOtunSatCur = 0.0;
        }
        if(!model->DIOtunSatSWCurGiven) {
            model->DIOtunSatSWCur = 0.0;
        }
        if(!model->DIOtunEmissionCoeffGiven) {
            model->DIOtunEmissionCoeff = 30.0;
        }
        if(!model->DIOtunSaturationCurrentExpGiven) {
            model->DIOtunSaturationCurrentExp = 3.0;
        }
        if(!model->DIOtunEGcorrectionFactorGiven) {
            model->DIOtunEGcorrectionFactor = 1.0;
        }
        if(!model->DIOfv_maxGiven) {
            model->DIOfv_max = 1e99;
        }
        if(!model->DIObv_maxGiven) {
            model->DIObv_max = 1e99;
        }

        /* loop through all the instances of the model */
        for (here = model->DIOinstances; here != NULL ;
                here=here->DIOnextInstance) {

            if(!here->DIOareaGiven) {
                if((!here->DIOwGiven) && (!here->DIOlGiven))  {
                    here->DIOarea = model->DIOarea;
                } else {
                    here->DIOarea = 1;
                }
            }
            if(!here->DIOpjGiven) {
                if((!here->DIOwGiven) && (!here->DIOlGiven))  {
                    here->DIOpj = model->DIOpj;
                } else {
                    here->DIOpj = 0;
                }
            }
            if(!here->DIOmGiven) {
                here->DIOm = 1;
            }

            here->DIOarea = here->DIOarea * here->DIOm;
            here->DIOpj = here->DIOpj * here->DIOm;
            if (model->DIOlevel == 3) {
                if((here->DIOwGiven) && (here->DIOlGiven))  {
                    here->DIOarea = here->DIOw * here->DIOl * here->DIOm;
                    here->DIOpj = (2 * here->DIOw + 2 * here->DIOl) * here->DIOm;
                }
            }
            here->DIOforwardKneeCurrent = model->DIOforwardKneeCurrent * here->DIOarea;
            here->DIOreverseKneeCurrent = model->DIOreverseKneeCurrent * here->DIOarea;
            here->DIOjunctionCap = model->DIOjunctionCap * here->DIOarea;
            here->DIOjunctionSWCap = model->DIOjunctionSWCap * here->DIOpj;

            here->DIOstate = *states;
            *states += 5;

            if(model->DIOresist == 0) {

                here->DIOposPrimeNode = here->DIOposNode;

            } else if(here->DIOposPrimeNode == 0) {

               Shim::CKTnode *tmpNode;
               const char * tmpName;

                error = Shim::CKTmkVolt(ckt,&tmp,here->DIOname,"internal");
                if(error) return(error);
                here->DIOposPrimeNode = tmp->number;
                if (ckt->CKTcopyNodesets) {
                  if (Shim::CKTinst2Node(ckt,here,1,&tmpNode,&tmpName)==OK) {
                     if (tmpNode->nsGiven) {
                       tmp->nodeset=tmpNode->nodeset;
                       tmp->nsGiven=tmpNode->nsGiven;
                     }
                  }
                }
            }

/* macro to make elements with built in test for out of memory */
#define TSTALLOC(ptr,first,second) \
{ here->ptr = matrix->make_elt(here->first, here->second); }

            /* TODO(translator): TSTALLOC macro kept as-is; needs manual rewrite. */

            TSTALLOC(DIOposPosPrimePtr,DIOposNode,DIOposPrimeNode);
            TSTALLOC(DIOnegPosPrimePtr,DIOnegNode,DIOposPrimeNode);
            TSTALLOC(DIOposPrimePosPtr,DIOposPrimeNode,DIOposNode);
            TSTALLOC(DIOposPrimeNegPtr,DIOposPrimeNode,DIOnegNode);
            TSTALLOC(DIOposPosPtr,DIOposNode,DIOposNode);
            TSTALLOC(DIOnegNegPtr,DIOnegNode,DIOnegNode);
            TSTALLOC(DIOposPrimePosPrimePtr,DIOposPrimeNode,DIOposPrimeNode);
        }
    }
    return 0;
}

int
DIOunsetup(
    DIOModel *inModel,
    Shim::Ckt *ckt)
{
    DIOModel *model;
    DIOInstance *here;

    for (model = inModel; model != NULL;
        model = model->DIOnextModel)
    {
        for (here = model->DIOinstances; here != NULL;
                here=here->DIOnextInstance)
        {

            if (here->DIOposPrimeNode
              && here->DIOposPrimeNode != here->DIOposNode)
            {
                Shim::CKTdltNNum(ckt, here->DIOposPrimeNode);
                here->DIOposPrimeNode = 0;
            }
        }
    }
    return 0;
}

} // namespace neospice::dio
