/***********************************************************************

 HiSIM (Hiroshima University STARC IGFET Model)
 Copyright (C) 2012 Hiroshima University & STARC

 MODEL NAME : HiSIM
 ( VERSION : 2  SUBVERSION : 7  REVISION : 0 ) Beta
 
 FILE : hsm2par.c

 Date : 2012.10.25

 released by 
                Hiroshima University &
                Semiconductor Technology Academic Research Center (STARC)
***********************************************************************/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/hisim2/hisim2_def.hpp"
#include "devices/hisim2/hisim2_shim.hpp"
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

namespace neospice::hisim2 {

using namespace Shim;

int HSM2param(
     int param,
     Shim::IfValue *value,
     HSM2Instance *inst,
     Shim::IfValue *select)
{
  double scale;

  HSM2Instance *here = (HSM2Instance*)inst;

  NG_IGNORE(select);

  if (!cp_getvar("scale", CP_REAL, &scale))
      scale = 1;

  switch (param) {
  case HSM2_W:
    here->HSM2_w = value->rValue * scale;
    here->HSM2_w_Given = TRUE;
    break;
  case HSM2_L:
    here->HSM2_l = value->rValue * scale;
    here->HSM2_l_Given = TRUE;
    break;
  case HSM2_AS:
    here->HSM2_as = value->rValue * scale * scale;
    here->HSM2_as_Given = TRUE;
    break;
  case HSM2_AD:
    here->HSM2_ad = value->rValue * scale * scale;
    here->HSM2_ad_Given = TRUE;
    break;
  case HSM2_PS:
    here->HSM2_ps = value->rValue * scale;
    here->HSM2_ps_Given = TRUE;
    break;
  case HSM2_PD:
    here->HSM2_pd = value->rValue * scale;
    here->HSM2_pd_Given = TRUE;
    break;
  case HSM2_NRS:
    here->HSM2_nrs = value->rValue;
    here->HSM2_nrs_Given = TRUE;
    break;
  case HSM2_NRD:
    here->HSM2_nrd = value->rValue;
    here->HSM2_nrd_Given = TRUE;
    break;
  case HSM2_TEMP:
    here->HSM2_temp = value->rValue;
    here->HSM2_temp_Given = TRUE;
    break;
  case HSM2_DTEMP:
    here->HSM2_dtemp = value->rValue;
    here->HSM2_dtemp_Given = TRUE;
    break;
  case HSM2_OFF:
    here->HSM2_off = value->iValue;
    break;
  case HSM2_IC_VBS:
    here->HSM2_icVBS = value->rValue;
    here->HSM2_icVBS_Given = TRUE;
    break;
  case HSM2_IC_VDS:
    here->HSM2_icVDS = value->rValue;
    here->HSM2_icVDS_Given = TRUE;
    break;
  case HSM2_IC_VGS:
    here->HSM2_icVGS = value->rValue;
    here->HSM2_icVGS_Given = TRUE;
    break;
  case HSM2_IC:
    switch (value->v.numValue) {
    case 3:
      here->HSM2_icVBS = *(value->v.vec.rVec + 2);
      here->HSM2_icVBS_Given = TRUE;
    case 2:
      here->HSM2_icVGS = *(value->v.vec.rVec + 1);
      here->HSM2_icVGS_Given = TRUE;
    case 1:
      here->HSM2_icVDS = *(value->v.vec.rVec);
      here->HSM2_icVDS_Given = TRUE;
      break;
    default:
      return Shim::E_BADPARM;
    }
    break;
  case  HSM2_CORBNET: 
    here->HSM2_corbnet = value->iValue;
    here->HSM2_corbnet_Given = TRUE;
    break;
  case  HSM2_RBPB:
    here->HSM2_rbpb = value->rValue;
    here->HSM2_rbpb_Given = TRUE;
    break;
  case  HSM2_RBPD:
    here->HSM2_rbpd = value->rValue;
    here->HSM2_rbpd_Given = TRUE;
    break;
  case  HSM2_RBPS:
    here->HSM2_rbps = value->rValue;
    here->HSM2_rbps_Given = TRUE;
    break;
  case  HSM2_RBDB:
    here->HSM2_rbdb = value->rValue;
    here->HSM2_rbdb_Given = TRUE;
    break;
  case  HSM2_RBSB:
    here->HSM2_rbsb = value->rValue;
    here->HSM2_rbsb_Given = TRUE;
    break;
  case  HSM2_CORG: 
    here->HSM2_corg = value->iValue;
    here->HSM2_corg_Given = TRUE;
    break;
/*   case  HSM2_RSHG: */
/*     here->HSM2_rshg = value->rValue; */
/*     here->HSM2_rshg_Given = TRUE; */
/*     break; */
  case  HSM2_NGCON:
    here->HSM2_ngcon = value->rValue;
    here->HSM2_ngcon_Given = TRUE;
    break;
  case  HSM2_XGW:
    here->HSM2_xgw = value->rValue;
    here->HSM2_xgw_Given = TRUE;
    break;
  case  HSM2_XGL:
    here->HSM2_xgl = value->rValue;
    here->HSM2_xgl_Given = TRUE;
    break;
  case  HSM2_NF:
    here->HSM2_nf = value->rValue;
    here->HSM2_nf_Given = TRUE;
    break;
  case  HSM2_SA:
    here->HSM2_sa = value->rValue;
    here->HSM2_sa_Given = TRUE;
    break;
  case  HSM2_SB:
    here->HSM2_sb = value->rValue;
    here->HSM2_sb_Given = TRUE;
    break;
  case  HSM2_SD:
    here->HSM2_sd = value->rValue;
    here->HSM2_sd_Given = TRUE;
    break;
  case  HSM2_NSUBCDFM:
    here->HSM2_nsubcdfm = value->rValue;
    here->HSM2_nsubcdfm_Given = TRUE;
    break;
  case  HSM2_MPHDFM:
    here->HSM2_mphdfm = value->rValue;
    here->HSM2_mphdfm_Given = TRUE;
    break;
  case  HSM2_M:
    here->HSM2_m = value->rValue;
    here->HSM2_m_Given = TRUE;
    break;

/* WPE */
  case HSM2_SCA:
    here->HSM2_sca = value->rValue;
	here->HSM2_sca_Given = TRUE;
	break;
  case HSM2_SCB:
    here->HSM2_scb = value->rValue;
	here->HSM2_scb_Given = TRUE;
	break;
  case HSM2_SCC:
    here->HSM2_scc= value->rValue;
	here->HSM2_scc_Given = TRUE;
	break;
  default:
    return Shim::E_BADPARM;
  }
  return 0;
}

} // namespace neospice::hisim2
