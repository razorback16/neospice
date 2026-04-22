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

int HFET2mParam(int param, Shim::IfValue *value, HFET2Model *inModel)
{
  
  HFET2Model *model = inModel;
  switch(param) {
    case HFET2_MOD_CF:
      model->HFET2cfGiven = TRUE;
      CF = value->rValue;
      break;
    case HFET2_MOD_D1:
      model->HFET2d1Given = TRUE;
      D1 = value->rValue;
      break;
    case HFET2_MOD_D2:
      model->HFET2d2Given = TRUE;
      D2 = value->rValue;
      break;
    case HFET2_MOD_DEL:
      model->HFET2delGiven = TRUE;
      DEL = value->rValue;
      break;      
    case HFET2_MOD_DELTA:
      model->HFET2deltaGiven = TRUE;
      DELTA = value->rValue;
      break;      
    case HFET2_MOD_DELTAD:
      model->HFET2deltadGiven = TRUE;
      DELTAD = value->rValue;
      break;
    case HFET2_MOD_DI:
      model->HFET2diGiven = TRUE;
      DI = value->rValue;
      break;
    case HFET2_MOD_EPSI:
      model->HFET2epsiGiven = TRUE;
      EPSI = value->rValue;
      break;
    case HFET2_MOD_ETA:
      model->HFET2etaGiven = TRUE;
      ETA = value->rValue;
      break;
    case HFET2_MOD_ETA1:
      model->HFET2eta1Given = TRUE;
      ETA1 = value->rValue;
      break;
    case HFET2_MOD_ETA2:
      model->HFET2eta2Given = TRUE;
      ETA2 = value->rValue;
      break;
    case HFET2_MOD_GAMMA:
      model->HFET2gammaGiven = TRUE;
      GAMMA = value->rValue;
      break;
    case HFET2_MOD_GGR:
      model->HFET2ggrGiven = TRUE;
      GGR = value->rValue;
      break;
    case HFET2_MOD_JS:
      model->HFET2jsGiven = TRUE;
      JS = value->rValue;
      break;      
    case HFET2_MOD_KLAMBDA:
      model->HFET2klambdaGiven = TRUE;
      KLAMBDA = value->rValue;
      break;
    case HFET2_MOD_KMU:
      model->HFET2kmuGiven = TRUE;
      KMU = value->rValue;
      break;
    case HFET2_MOD_KNMAX:
      model->HFET2knmaxGiven = TRUE;
      KNMAX = value->rValue;
      break;
    case HFET2_MOD_KVTO:
      model->HFET2kvtoGiven = TRUE;
      KVTO = value->rValue;
      break;
    case HFET2_MOD_LAMBDA:
      model->HFET2lambdaGiven = TRUE;
      LAMBDA = value->rValue;
      break;
    case HFET2_MOD_M:
      model->HFET2mGiven = TRUE;
      M = value->rValue;
      break;
    case HFET2_MOD_MC:
      model->HFET2mcGiven = TRUE;
      MC = value->rValue;
      break;
    case HFET2_MOD_MU:
      model->HFET2muGiven = TRUE;
      MU = value->rValue;
      break;
    case HFET2_MOD_N:
      model->HFET2nGiven = TRUE;
      N = value->rValue;
      break;      
    case HFET2_MOD_NMAX:
      model->HFET2nmaxGiven = TRUE;
      NMAX = value->rValue;
      break;
    case HFET2_MOD_P:
      model->HFET2pGiven = TRUE;
      PP = value->rValue;
      break;
    case HFET2_MOD_RD:
      model->HFET2rdGiven = TRUE;
      RD = value->rValue;
      break;
    case HFET2_MOD_RDI:
      model->HFET2rdiGiven = TRUE;
      RDI = value->rValue;
      break;
    case HFET2_MOD_RS:
      model->HFET2rsGiven = TRUE;
      RS = value->rValue;
      break;
    case HFET2_MOD_RSI:
      model->HFET2rsiGiven = TRUE;
      RSI = value->rValue;
      break;
    case HFET2_MOD_SIGMA0:
      model->HFET2sigma0Given = TRUE;
      SIGMA0 = value->rValue;
      break;
    case HFET2_MOD_VS:
      model->HFET2vsGiven = TRUE;
      VS = value->rValue;
      break;
    case HFET2_MOD_VSIGMA:
      model->HFET2vsigmaGiven = TRUE;
      VSIGMA = value->rValue;
      break;
    case HFET2_MOD_VSIGMAT:
      model->HFET2vsigmatGiven = TRUE;
      VSIGMAT = value->rValue;
      break;
    case HFET2_MOD_VT1:
      model->HFET2vt1Given = TRUE;
      HFET2_VT1 = value->rValue;
      break;
    case HFET2_MOD_VT2:
      model->HFET2vt2Given = TRUE;
      VT2 = value->rValue;
      break;
    case HFET2_MOD_VTO:
      model->HFET2vtoGiven = TRUE;
      VTO = value->rValue;
      break;
    case HFET2_MOD_NHFET:
      if(value->iValue) {
        TYPE = NHFET;
      }
      break;
    case HFET2_MOD_PHFET:
      if(value->iValue) {
        TYPE = PHFET;
      }
      break;
    default:
      return Shim::E_BADPARM;
  }
  return 0;
  
}

} // namespace neospice::hfet2
