/**********
Imported from MacSpice3f4 - Antony Wilson
Modified: Paolo Nenzi
**********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/hfet2/hfet2_def.hpp"
#include "devices/hfet2/hfet2_shim.hpp"
#include "devices/hfet2/hfet2_macros.hpp"
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

namespace neospice::hfet2 {

using namespace Shim;

int HFET2temp(
HFET2Model *inModel,
Shim::Ckt *ckt)
{

  HFET2Instance *here;
  HFET2Model *model = inModel;
  double vt;
  double tdiff;

  for( ; model != NULL; model = model->HFET2nextModel ) {
    if(model->HFET2rd != 0)
      model->HFET2drainConduct = 1/model->HFET2rd;
    else
      model->HFET2drainConduct = 0;
    if(model->HFET2rs != 0)
      model->HFET2sourceConduct = 1/model->HFET2rs;
    else
      model->HFET2sourceConduct = 0;
    if(!model->HFET2vt1Given)
      HFET2_VT1 = VTO+CHARGE*NMAX*DI/EPSI;
    if(!model->HFET2vt2Given)
      VT2 = VTO;
    DELTA2 = DELTA*DELTA;
    for (here = model->HFET2instances; here != NULL; 
         here=here->HFET2nextInstance) {

    if(!here->HFET2dtempGiven)
       here->HFET2dtemp = 0.0;
    if(!here->HFET2tempGiven)
       TEMP = ckt->CKTtemp + here->HFET2dtemp;

      vt    = CONSTKoverQ*TEMP;
      tdiff = TEMP - ckt->CKTnomTemp;

      TLAMBDA = LAMBDA + KLAMBDA*tdiff;
      TMU     = MU - KMU*tdiff;
      TNMAX   = NMAX - KNMAX*tdiff;
      TVTO    = TYPE*VTO - KVTO*tdiff;
      JSLW    = JS*L*W/2;
      GGRLW   = GGR*L*W/2;
      N0      = EPSI*ETA*vt/2/CHARGE/(DI+DELTAD);
      N01     = EPSI*ETA1*vt/2/CHARGE/D1;
      if(model->HFET2eta2Given)
        N02 = EPSI*ETA2*vt/2/CHARGE/D2;
      else
        N02 = 0.0;  
      GCHI0 = CHARGE*W*TMU/L;
      IMAX  = CHARGE*TNMAX*VS*W;
      VCRIT = vt*log(vt/(CONSTroot2 * 1e-11));
    }
  }
  return 0;
  
}

} // namespace neospice::hfet2
