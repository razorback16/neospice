/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified: 2000 AlansFixes
**********/

/*
 * This routine should only be called when circuit topology
 * changes, since its computations do not depend on most
 * device or model parameters, only on topology (as
 * affected by emitter, collector, and base resistances)
 */

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/bjt/bjt_def.hpp"
#include "devices/bjt/bjt_shim.hpp"
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

namespace neospice::bjt {

using namespace Shim;

int
BJTsetup(Shim::Matrix *matrix, BJTModel *inModel, Shim::Ckt *ckt, int *states)
  /* load the BJT structure with those pointers needed later
  * for fast matrix loading
  */
{
    BJTModel *model = inModel;
    BJTInstance *here;
    int error;
    Shim::CKTnode *tmp;

    /*  loop through all the diode models */
    for( ; model != NULL; model = model->BJTnextModel ) {

        if(model->BJTtype != NPN && model->BJTtype != PNP) {
            model->BJTtype = NPN;
        }
        if(!model->BJTsubsGiven ||
           (model->BJTsubs != VERTICAL && model->BJTsubs != LATERAL)) {
            if (model->BJTtype == NPN) 
                model->BJTsubs = VERTICAL;  /* Vertical for NPN */
            else
                model->BJTsubs = LATERAL;   /* Lateral for PNP */
        }
        if(!model->BJTsatCurGiven) {
            model->BJTsatCur = 1e-16;
        }
        if(!model->BJTbetaFGiven) {
            model->BJTbetaF = 100;
        }
        if(!model->BJTemissionCoeffFGiven) {
            model->BJTemissionCoeffF = 1;
        }
        if(!model->BJTleakBEemissionCoeffGiven) {
            model->BJTleakBEemissionCoeff = 1.5;
        }
        if(!model->BJTbetaRGiven) {
            model->BJTbetaR = 1;
        }
        if(!model->BJTemissionCoeffRGiven) {
            model->BJTemissionCoeffR = 1;
        }
        if(!model->BJTleakBCemissionCoeffGiven) {
            model->BJTleakBCemissionCoeff = 2;
        }
        if(!model->BJTbaseResistGiven) {
            model->BJTbaseResist = 0;
        }
        if(!model->BJTemitterResistGiven) {
            model->BJTemitterResist = 0;
        }
        if(!model->BJTcollectorResistGiven) {
            model->BJTcollectorResist = 0;
        }
        if(!model->BJTdepletionCapBEGiven) {
            model->BJTdepletionCapBE = 0;
        }
        if(!model->BJTpotentialBEGiven) {
            model->BJTpotentialBE = .75;
        }
        if(!model->BJTjunctionExpBEGiven) {
            model->BJTjunctionExpBE = .33;
        }
        if(!model->BJTtransitTimeFGiven) {
            model->BJTtransitTimeF = 0;
        }
        if(!model->BJTtransitTimeBiasCoeffFGiven) {
            model->BJTtransitTimeBiasCoeffF = 0;
        }
        if(!model->BJTtransitTimeHighCurrentFGiven) {
            model->BJTtransitTimeHighCurrentF = 0;
        }
        if(!model->BJTexcessPhaseGiven) {
            model->BJTexcessPhase = 0;
        }
        if(!model->BJTdepletionCapBCGiven) {
            model->BJTdepletionCapBC = 0;
        }
        if(!model->BJTpotentialBCGiven) {
            model->BJTpotentialBC = .75;
        }
        if(!model->BJTjunctionExpBCGiven) {
            model->BJTjunctionExpBC = .33;
        }
        if(!model->BJTbaseFractionBCcapGiven) {
            model->BJTbaseFractionBCcap = 1;
        }
        if(!model->BJTtransitTimeRGiven) {
            model->BJTtransitTimeR = 0;
        }
        if(!model->BJTcapSubGiven) {
            model->BJTcapSub = 0;
        }
        if(!model->BJTpotentialSubstrateGiven) {
            model->BJTpotentialSubstrate = .75;
        }
        if(!model->BJTexponentialSubstrateGiven) {
            model->BJTexponentialSubstrate = 0;
        }
        if(!model->BJTbetaExpGiven) {
            model->BJTbetaExp = 0;
        }
        if(!model->BJTenergyGapGiven) {
            model->BJTenergyGap = 1.11;
        }
        if(!model->BJTtempExpISGiven) {
            model->BJTtempExpIS = 3;
        }
        if(!model->BJTfNcoefGiven) {
            model->BJTfNcoef = 0;
        }
        if(!model->BJTfNexpGiven) {
            model->BJTfNexp = 1;
        }
        if(!model->BJTsubSatCurGiven) {
            model->BJTsubSatCur = 0.0;
        }
        if(!model->BJTemissionCoeffSGiven) {
            model->BJTemissionCoeffS = 1.0;
        }
        if(!model->BJTtlevGiven) {
            model->BJTtlev = 0;
        }
        if(!model->BJTtlevcGiven) {
            model->BJTtlevc = 0;
        }
        if(!model->BJTtbf1Given) {
            model->BJTtbf1 = 0.0;
        }
        if(!model->BJTtbf2Given) {
            model->BJTtbf2 = 0.0;
        }
        if(!model->BJTtbr1Given) {
            model->BJTtbr1 = 0.0;
        }
        if(!model->BJTtbr2Given) {
            model->BJTtbr2 = 0.0;
        }
        if(!model->BJTtikf1Given) {
            model->BJTtikf1 = 0.0;
        }
        if(!model->BJTtikf2Given) {
            model->BJTtikf2 = 0.0;
        }
        if(!model->BJTtikr1Given) {
            model->BJTtikr1 = 0.0;
        }
        if(!model->BJTtikr2Given) {
            model->BJTtikr2 = 0.0;
        }
        if(!model->BJTtirb1Given) {
            model->BJTtirb1 = 0.0;
        }
        if(!model->BJTtirb2Given) {
            model->BJTtirb2 = 0.0;
        }
        if(!model->BJTtnc1Given) {
            model->BJTtnc1 = 0.0;
        }
        if(!model->BJTtnc2Given) {
            model->BJTtnc2 = 0.0;
        }
        if(!model->BJTtne1Given) {
            model->BJTtne1 = 0.0;
        }
        if(!model->BJTtne2Given) {
            model->BJTtne2 = 0.0;
        }
        if(!model->BJTtnf1Given) {
            model->BJTtnf1 = 0.0;
        }
        if(!model->BJTtnf2Given) {
            model->BJTtnf2 = 0.0;
        }
        if(!model->BJTtnr1Given) {
            model->BJTtnr1 = 0.0;
        }
        if(!model->BJTtnr2Given) {
            model->BJTtnr2 = 0.0;
        }
        if(!model->BJTtrb1Given) {
            model->BJTtrb1 = 0.0;
        }
        if(!model->BJTtrb2Given) {
            model->BJTtrb2 = 0.0;
        }
        if(!model->BJTtrc1Given) {
            model->BJTtrc1 = 0.0;
        }
        if(!model->BJTtrc2Given) {
            model->BJTtrc2 = 0.0;
        }
        if(!model->BJTtre1Given) {
            model->BJTtre1 = 0.0;
        }
        if(!model->BJTtre2Given) {
            model->BJTtre2 = 0.0;
        }
        if(!model->BJTtrm1Given) {
            model->BJTtrm1 = 0.0;
        }
        if(!model->BJTtrm2Given) {
            model->BJTtrm2 = 0.0;
        }
        if(!model->BJTtvaf1Given) {
            model->BJTtvaf1 = 0.0;
        }
        if(!model->BJTtvaf2Given) {
            model->BJTtvaf2 = 0.0;
        }
        if(!model->BJTtvar1Given) {
            model->BJTtvar1 = 0.0;
        }
        if(!model->BJTtvar2Given) {
            model->BJTtvar2 = 0.0;
        }
        if(!model->BJTctcGiven) {
            model->BJTctc = 0.0;
        }
        if(!model->BJTcteGiven) {
            model->BJTcte = 0.0;
        }
        if(!model->BJTctsGiven) {
            model->BJTcts = 0.0;
        }
        if(!model->BJTtvjeGiven) {
            model->BJTtvje = 0.0;
        }
        if(!model->BJTtvjcGiven) {
            model->BJTtvjc = 0.0;
        }
        if(!model->BJTtvjsGiven) {
            model->BJTtvjs = 0.0;
        }
        if(!model->BJTtitf1Given) {
            model->BJTtitf1 = 0.0;
        }
        if(!model->BJTtitf2Given) {
            model->BJTtitf2 = 0.0;
        }
        if(!model->BJTttf1Given) {
            model->BJTttf1 = 0.0;
        }
        if(!model->BJTttf2Given) {
            model->BJTttf2 = 0.0;
        }
        if(!model->BJTttr1Given) {
            model->BJTttr1 = 0.0;
        }
        if(!model->BJTttr2Given) {
            model->BJTttr2 = 0.0;
        }
        if(!model->BJTtmje1Given) {
            model->BJTtmje1 = 0.0;
        }
        if(!model->BJTtmje2Given) {
            model->BJTtmje2 = 0.0;
        }
        if(!model->BJTtmjc1Given) {
            model->BJTtmjc1 = 0.0;
        }
        if(!model->BJTtmjc2Given) {
            model->BJTtmjc2 = 0.0;
        }
        if(!model->BJTtmjs1Given) {
            model->BJTtmjs1 = 0.0;
        }
        if(!model->BJTtmjs2Given) {
            model->BJTtmjs2 = 0.0;
        }
        if(!model->BJTtns1Given) {
            model->BJTtns1 = 0.0;
        }
        if(!model->BJTtns2Given) {
            model->BJTtns2 = 0.0;
        }
        if(!model->BJTnkfGiven) {
            model->BJTnkf = 0.5;
        } else {
          if (model->BJTnkf > 1.0) {
            printf("Warning: NKF has been set to its maximum value: 1.0\n");
            model->BJTnkf = 1.0;
          } 
        }
        if(!model->BJTtis1Given) {
            model->BJTtis1 = 0.0;
        }
        if(!model->BJTtis2Given) {
            model->BJTtis2 = 0.0;
        }
        if(!model->BJTtise1Given) {
            model->BJTtise1 = 0.0;
        }
        if(!model->BJTtise2Given) {
            model->BJTtise2 = 0.0;
        }
        if(!model->BJTtisc1Given) {
            model->BJTtisc1 = 0.0;
        }
        if(!model->BJTtisc2Given) {
            model->BJTtisc2 = 0.0;
        }
        if(!model->BJTvbeMaxGiven) {
            model->BJTvbeMax = 1e99;
        }
        if(!model->BJTvbcMaxGiven) {
            model->BJTvbcMax = 1e99;
        }
        if(!model->BJTvceMaxGiven) {
            model->BJTvceMax = 1e99;
        }

/*
 * COMPATABILITY WARNING!
 * special note:  for backward compatability to much older models, spice 2G
 * implemented a special case which checked if B-E leakage saturation
 * current was >1, then it was instead a the B-E leakage saturation current
 * divided by IS, and multiplied it by IS at this point.  This was not
 * handled correctly in the 2G code, and there is some question on its
 * reasonability, since it is also undocumented, so it has been left out
 * here.  It could easily be added with 1 line.  (The same applies to the B-C
 * leakage saturation current).   TQ  6/29/84
 */

        /* loop through all the instances of the model */
        for (here = model->BJTinstances; here != NULL ;
                here=here->BJTnextInstance) {
            Shim::CKTnode *tmpNode;
            const char * tmpName;

            if(!here->BJTareaGiven) {
                here->BJTarea = 1.0;
            }
            if(!here->BJTareabGiven) {
                here->BJTareab = here->BJTarea;
            }
            if(!here->BJTareacGiven) {
                here->BJTareac = here->BJTarea;
            }
            if(!here->BJTmGiven) {
                here->BJTm = 1.0;
            }

            here->BJTstate = *states;
            *states += BJTnumStates;
            if(ckt->CKTsenInfo && (ckt->CKTsenInfo->SENmode & TRANSEN) ){
                *states += 8 * (ckt->CKTsenInfo->SENparms);
            }

            if(model->BJTcollectorResist == 0) {
                here->BJTcolPrimeNode = here->BJTcolNode;
            } else if(here->BJTcolPrimeNode == 0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->BJTname,"collector");
                if(error) return(error);
                here->BJTcolPrimeNode = tmp->number;
                if (ckt->CKTcopyNodesets) {
                  if (Shim::CKTinst2Node(ckt,here,1,&tmpNode,&tmpName)==OK) {
                     if (tmpNode->nsGiven) {
                       tmp->nodeset=tmpNode->nodeset;
                       tmp->nsGiven=tmpNode->nsGiven;
/*                     fprintf(stderr, "Nodeset copied from %s\n", tmpName);
                       fprintf(stderr, "                 to %s\n", tmp->name);
                       fprintf(stderr, "              value %g\n",
                                                                tmp->nodeset);*/
                     }
                  }
                }
            }
            if(model->BJTbaseResist == 0) {
                here->BJTbasePrimeNode = here->BJTbaseNode;
            } else if(here->BJTbasePrimeNode == 0){
                error = Shim::CKTmkVolt(ckt,&tmp,here->BJTname, "base");
                if(error) return(error);
                here->BJTbasePrimeNode = tmp->number;
                if (ckt->CKTcopyNodesets) {
                  if (Shim::CKTinst2Node(ckt,here,2,&tmpNode,&tmpName)==OK) {
                     if (tmpNode->nsGiven) {
                       tmp->nodeset=tmpNode->nodeset;
                       tmp->nsGiven=tmpNode->nsGiven;
/*                     fprintf(stderr, "Nodeset copied from %s\n", tmpName);
                       fprintf(stderr, "                 to %s\n", tmp->name);
                       fprintf(stderr, "              value %g\n",
                                                                tmp->nodeset);*/
                     }
                  }
                }
            }
            if(model->BJTemitterResist == 0) {
                here->BJTemitPrimeNode = here->BJTemitNode;
            } else if(here->BJTemitPrimeNode == 0) {
                error = Shim::CKTmkVolt(ckt,&tmp,here->BJTname, "emitter");
                if(error) return(error);
                here->BJTemitPrimeNode = tmp->number;
                if (ckt->CKTcopyNodesets) {
                  if (Shim::CKTinst2Node(ckt,here,3,&tmpNode,&tmpName)==OK) {
                     if (tmpNode->nsGiven) {
                       tmp->nodeset=tmpNode->nodeset;
                       tmp->nsGiven=tmpNode->nsGiven;
/*                     fprintf(stderr, "Nodeset copied from %s\n", tmpName);
                       fprintf(stderr, "                 to %s\n", tmp->name);
                       fprintf(stderr, "              value %g\n",
                                                                tmp->nodeset);*/
                     }
                  }
                }
            }

/* macro to make elements with built in test for out of memory */
#define TSTALLOC(ptr,first,second) \
{ here->ptr = matrix->make_elt(here->first, here->second); }

            TSTALLOC(BJTcolColPrimePtr,BJTcolNode,BJTcolPrimeNode);
            TSTALLOC(BJTbaseBasePrimePtr,BJTbaseNode,BJTbasePrimeNode);
            TSTALLOC(BJTemitEmitPrimePtr,BJTemitNode,BJTemitPrimeNode);
            TSTALLOC(BJTcolPrimeColPtr,BJTcolPrimeNode,BJTcolNode);
            TSTALLOC(BJTcolPrimeBasePrimePtr,BJTcolPrimeNode,BJTbasePrimeNode);
            TSTALLOC(BJTcolPrimeEmitPrimePtr,BJTcolPrimeNode,BJTemitPrimeNode);
            TSTALLOC(BJTbasePrimeBasePtr,BJTbasePrimeNode,BJTbaseNode);
            TSTALLOC(BJTbasePrimeColPrimePtr,BJTbasePrimeNode,BJTcolPrimeNode);
            TSTALLOC(BJTbasePrimeEmitPrimePtr,BJTbasePrimeNode,BJTemitPrimeNode);
            TSTALLOC(BJTemitPrimeEmitPtr,BJTemitPrimeNode,BJTemitNode);
            TSTALLOC(BJTemitPrimeColPrimePtr,BJTemitPrimeNode,BJTcolPrimeNode);
            TSTALLOC(BJTemitPrimeBasePrimePtr,BJTemitPrimeNode,BJTbasePrimeNode);
            TSTALLOC(BJTcolColPtr,BJTcolNode,BJTcolNode);
            TSTALLOC(BJTbaseBasePtr,BJTbaseNode,BJTbaseNode);
            TSTALLOC(BJTemitEmitPtr,BJTemitNode,BJTemitNode);
            TSTALLOC(BJTcolPrimeColPrimePtr,BJTcolPrimeNode,BJTcolPrimeNode);
            TSTALLOC(BJTbasePrimeBasePrimePtr,BJTbasePrimeNode,BJTbasePrimeNode);
            TSTALLOC(BJTemitPrimeEmitPrimePtr,BJTemitPrimeNode,BJTemitPrimeNode);
            TSTALLOC(BJTsubstSubstPtr,BJTsubstNode,BJTsubstNode);
            if (model -> BJTsubs == LATERAL) {
              here -> BJTsubstConNode = here -> BJTbasePrimeNode;
              here -> BJTsubstConSubstConPtr = here -> BJTbasePrimeBasePrimePtr;
            } else {
              here -> BJTsubstConNode = here -> BJTcolPrimeNode;
              here -> BJTsubstConSubstConPtr = here -> BJTcolPrimeColPrimePtr;
            }
            TSTALLOC(BJTsubstConSubstPtr,BJTsubstConNode,BJTsubstNode);
            TSTALLOC(BJTsubstSubstConPtr,BJTsubstNode,BJTsubstConNode);
            TSTALLOC(BJTbaseColPrimePtr,BJTbaseNode,BJTcolPrimeNode);
            TSTALLOC(BJTcolPrimeBasePtr,BJTcolPrimeNode,BJTbaseNode);
        }
    }
    return 0;
}

int
BJTunsetup(
    BJTModel *inModel,
    Shim::Ckt *ckt)
{
    BJTModel *model;
    BJTInstance *here;

    for (model = inModel; model != NULL;
        model = model->BJTnextModel)
    {
        for (here = model->BJTinstances; here != NULL;
                here=here->BJTnextInstance)
        {
           if (here->BJTcolPrimeNode
               && here->BJTcolPrimeNode != here->BJTcolNode)
           {
                Shim::CKTdltNNum(ckt, here->BJTcolPrimeNode);
                here->BJTcolPrimeNode = 0;
           }
           if (here->BJTbasePrimeNode
               && here->BJTbasePrimeNode != here->BJTbaseNode)
           {
                Shim::CKTdltNNum(ckt, here->BJTbasePrimeNode);
                here->BJTbasePrimeNode = 0;
           }
           if (here->BJTemitPrimeNode
               && here->BJTemitPrimeNode != here->BJTemitNode)
           {
                Shim::CKTdltNNum(ckt, here->BJTemitPrimeNode);
                here->BJTemitPrimeNode = 0;
            }
        }
    }
    return 0;
}

} // namespace neospice::bjt
