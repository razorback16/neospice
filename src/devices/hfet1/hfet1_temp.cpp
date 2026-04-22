/**********
Imported from MacSpice3f4 - Antony Wilson
Modified: Paolo Nenzi
**********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/hfet1/hfet1_def.hpp"
#include "devices/hfet1/hfet1_shim.hpp"
#include <cmath>
#include <cfloat>
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

namespace neospice::hfet1 {

using namespace Shim;

#include "devices/hfet1/hfet1_macros.hpp"
/* ARGSUSED */
int
HFETAtemp(HFETAModel *inModel, Shim::Ckt *ckt)
{
    HFETAModel *model = inModel;
    HFETAInstance *here;
    double vt;
    double temp;

    /*  loop through all the diode models */
    for( ; model != NULL; model = model->HFETAnextModel ) {
        if(model->HFETArd != 0) {
            model->HFETAdrainConduct = 1/model->HFETArd;
        } else {
            model->HFETAdrainConduct = 0;
        }
        if(model->HFETArs != 0) {
            model->HFETAsourceConduct = 1/model->HFETArs;
        } else {
            model->HFETAsourceConduct = 0;
        }
        if(model->HFETArg != 0) {
            model->HFETAgateConduct = 1/model->HFETArg;
        } else {
            model->HFETAgateConduct = 0;
        }
        if(model->HFETAri != 0) {
            model->HFETAgi = 1/model->HFETAri;
        } else {
            model->HFETAgi = 0;
        }
        if(model->HFETArf != 0) {
            model->HFETAgf = 1/model->HFETArf;
        } else {
            model->HFETAgf = 0;
        }
        model->HFETAdeltaSqr = model->HFETAdelta*model->HFETAdelta;
        model->HFETAthreshold *= model->HFETAtype;

        if(!model->HFETAvt2Given)
          VT2 = VTO;
        if(!model->HFETAvt1Given)
          IN_VT1 = VTO+CHARGE*NMAX*DI/EPSI;
          
        for (here = model->HFETAinstances; here != NULL ;
                here=here->HFETAnextInstance) {

            if(!here->HFETAdtempGiven) {
                here->HFETAdtemp = 0.0;
            }

            if(!here->HFETAtempGiven) {
                here->HFETAtemp = ckt->CKTtemp + here->HFETAdtemp;
            }

            vt      = CONSTKoverQ*TEMP;
            TLAMBDA = LAMBDA + KLAMBDA*(TEMP-ckt->CKTnomTemp);            
            TMU     = MU - KMU*(TEMP-ckt->CKTnomTemp);
            TVTO    = VTO - KVTO*(TEMP-ckt->CKTnomTemp);
            N0      = EPSI*ETA*vt/2/CHARGE/(DI+DELTAD);
            N01     = EPSI*ETA1*vt/2/CHARGE/D1;
            if(model->HFETAeta2Given)
              N02   = EPSI*ETA2*vt/2/CHARGE/D2;
            else
              N02   = 0.0;  
            GCHI0   = CHARGE*W*TMU/L;
            CF      = 0.5*EPSI*W;
            IMAX    = CHARGE*NMAX*VS*W;
            IS1D    = JS1D*W*L/2;
            IS2D    = JS2D*W*L/2;
            IS1S    = JS1S*W*L/2;
            IS2S    = JS2S*W*L/2;
            ISO     = ASTAR*W*L/2;
            GGRWL   = GGR*L*W/2;
            temp    = exp(TEMP/model->HFETAtf);
            FGDS    = model->HFETAfgds*temp;
            DELF    = model->HFETAdelf*temp;
            if(model->HFETAgatemod == 0) {
              if(IS1S != 0)
                here->HFETAvcrit  = vt*log(vt/(CONSTroot2*IS1S));
              else
                here->HFETAvcrit = DBL_MAX;
            } else {
              if(ISO != 0.0)            
                here->HFETAvcrit = vt*log(vt/(CONSTroot2*ISO));
              else
                here->HFETAvcrit = DBL_MAX;
            }    
        }
    }
    return 0;
}

} // namespace neospice::hfet1
