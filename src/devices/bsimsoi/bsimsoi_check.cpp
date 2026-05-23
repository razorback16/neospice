/***  B4SOI 12/16/2010 Released by Tanvir Morshed   ***/


/**********
 * Copyright 2010 Regents of the University of California.  All rights reserved.
 * Authors: 1998 Samuel Fung, Dennis Sinitsky and Stephen Tang
 * Authors: 1999-2004 Pin Su, Hui Wan, Wei Jin, b3soicheck.c
 * Authors: 2005- Hui Wan, Xuemei Xi, Ali Niknejad, Chenming Hu.
 * Authors: 2009- Wenwei Yang, Chung-Hsun Lin, Ali Niknejad, Chenming Hu.
 * Authors: 2010- Tanvir Morshed, Ali Niknejad, Chenming Hu.
 * File: b4soicheck.c
 * Modified by Hui Wan, Xuemei Xi 11/30/2005
 * Modified by Wenwei Yang, Chung-Hsun Lin, Darsen Lu 03/06/2009
 * Modified by Tanvir Morshed 09/22/2009
 * Modified by Tanvir Morshed 12/31/2009
 * Modified by Tanvir Morshed 12/16/2010
 **********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/bsimsoi/bsimsoi_def.hpp"
#include "devices/bsimsoi/bsimsoi_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "devices/ucb_compat.hpp"

namespace neospice::bsimsoi {

using namespace Shim;

int
B4SOIcheckModel(
B4SOIModel *model,
B4SOIInstance *here,
Shim::Ckt *ckt)
{
struct b4soiSizeDependParam *pParam;
int Fatal_Flag = 0;

    NG_IGNORE(ckt);

    pParam = here->pParam;
    fprintf(stderr, "B4SOIV3 Parameter Check\n");
    fprintf(stderr, "Model = %s\n", model->B4SOImodName);
    fprintf(stderr, "W = %g, L = %g\n", here->B4SOIw, here->B4SOIl);

    if (pParam->B4SOIlpe0 < -pParam->B4SOIleff)
    {   fprintf(stderr, "Fatal: Lpe0 = %g is less than -Leff.\n",
                pParam->B4SOIlpe0);
        Fatal_Flag = 1;
    }

    if((here->B4SOIsa > 0.0) && (here->B4SOIsb > 0.0) &&
    ((here->B4SOInf == 1.0) || ((here->B4SOInf > 1.0) &&
    (here->B4SOIsd > 0.0))) )
    {   if (model->B4SOIsaref <= 0.0)
        {   fprintf(stderr, "Fatal: SAref = %g is not positive.\n",
                    model->B4SOIsaref);
            Fatal_Flag = 1;
        }
        if (model->B4SOIsbref <= 0.0)
        {   fprintf(stderr, "Fatal: SBref = %g is not positive.\n",
                    model->B4SOIsbref);
            Fatal_Flag = 1;
        }
    }

    if (pParam->B4SOIlpeb < -pParam->B4SOIleff) /* v4.0 for Vth */
    {   fprintf(stderr, "Fatal: Lpeb = %g is less than -Leff.\n",
                pParam->B4SOIlpeb);
        Fatal_Flag = 1;
    }

    if (pParam->B4SOIfprout < 0.0) /* v4.0 for DITS */
    {   fprintf(stderr, "Fatal: fprout = %g is negative.\n",
                pParam->B4SOIfprout);
        Fatal_Flag = 1;
    }
    if (pParam->B4SOIpdits < 0.0)  /* v4.0 for DITS */
    {   fprintf(stderr, "Fatal: pdits = %g is negative.\n",
                pParam->B4SOIpdits);
        Fatal_Flag = 1;
    }
    if (model->B4SOIpditsl < 0.0)  /* v4.0 for DITS */
    {   fprintf(stderr, "Fatal: pditsl = %g is negative.\n",
                model->B4SOIpditsl);
        Fatal_Flag = 1;
    }

    if (model->B4SOItox <= 0.0)
    {   fprintf(stderr, "Fatal: Tox = %g is not positive.\n",
                model->B4SOItox);
        Fatal_Flag = 1;
    }
            if (model->B4SOIleffeot <= 0.0)
    {   fprintf(stderr, "Fatal: leffeot = %g is not positive.\n",
                model->B4SOIleffeot);
        Fatal_Flag = 1;
    }
                    if (model->B4SOIweffeot <= 0.0)
    {   fprintf(stderr, "Fatal: weffeot = %g is not positive.\n",
                model->B4SOIweffeot);
        Fatal_Flag = 1;
    }
    if (model->B4SOItoxp <= 0.0)
    {   fprintf(stderr, "Fatal: Toxp = %g is not positive.\n",
                model->B4SOItoxp);
        Fatal_Flag = 1;
    }
    if (model->B4SOIepsrgate < 0.0)
    {   fprintf(stderr, "Fatal: Epsrgate = %g is not positive.\n",
                model->B4SOIepsrgate);
        Fatal_Flag = 1;
    }

    if (model->B4SOItoxm <= 0.0)
    {   fprintf(stderr, "Fatal: Toxm = %g is not positive.\n",
                model->B4SOItoxm);
        Fatal_Flag = 1;
    } /* v3.2 */

    if (here->B4SOInf < 1.0)
    {   fprintf(stderr, "Fatal: Number of finger = %g is smaller than one.\n", here->B4SOInf);
        Fatal_Flag = 1;
    }


/* v2.2.3 */
    if (model->B4SOItox - model->B4SOIdtoxcv <= 0.0)
    {   fprintf(stderr, "Fatal: Tox - dtoxcv = %g is not positive.\n",
                model->B4SOItox - model->B4SOIdtoxcv);
        Fatal_Flag = 1;
    }


    if (model->B4SOItbox <= 0.0)
    {   fprintf(stderr, "Fatal: Tbox = %g is not positive.\n",
                model->B4SOItbox);
        Fatal_Flag = 1;
    }

    if (pParam->B4SOInpeak <= 0.0)
    {   fprintf(stderr, "Fatal: Nch = %g is not positive.\n",
                pParam->B4SOInpeak);
        Fatal_Flag = 1;
    }
    if (pParam->B4SOIngate < 0.0)
    {   fprintf(stderr, "Fatal: Ngate = %g is not positive.\n",
                pParam->B4SOIngate);
        Fatal_Flag = 1;
    }
    if (pParam->B4SOIngate > 1.e25)
    {   fprintf(stderr, "Fatal: Ngate = %g is too high.\n",
                pParam->B4SOIngate);
        Fatal_Flag = 1;
    }

    if (pParam->B4SOIdvt1 < 0.0)
    {   fprintf(stderr, "Fatal: Dvt1 = %g is negative.\n",
                pParam->B4SOIdvt1);
        Fatal_Flag = 1;
    }

    if (pParam->B4SOIdvt1w < 0.0)
    {   fprintf(stderr, "Fatal: Dvt1w = %g is negative.\n",
                pParam->B4SOIdvt1w);
        Fatal_Flag = 1;
    }

    if (pParam->B4SOIw0 == -pParam->B4SOIweff)
    {   fprintf(stderr, "Fatal: (W0 + Weff) = 0 cauing divided-by-zero.\n");
        Fatal_Flag = 1;
    }

    if (pParam->B4SOIdsub < 0.0)
    {   fprintf(stderr, "Fatal: Dsub = %g is negative.\n", pParam->B4SOIdsub);
        Fatal_Flag = 1;
    }
    if (pParam->B4SOIb1 == -pParam->B4SOIweff)
    {   fprintf(stderr, "Fatal: (B1 + Weff) = 0 causing divided-by-zero.\n");
        Fatal_Flag = 1;
    }
    if (pParam->B4SOIu0temp <= 0.0)
    {   fprintf(stderr, "Fatal: u0 at current temperature = %g is not positive.\n", pParam->B4SOIu0temp);
        Fatal_Flag = 1;
    }

/* Check delta parameter */
    if (pParam->B4SOIdelta < 0.0)
    {   fprintf(stderr, "Fatal: Delta = %g is less than zero.\n",
                pParam->B4SOIdelta);
        Fatal_Flag = 1;
    }

    if (pParam->B4SOIvsattemp <= 0.0)
    {   fprintf(stderr, "Fatal: Vsat at current temperature = %g is not positive.\n", pParam->B4SOIvsattemp);
        Fatal_Flag = 1;
    }
/* Check Rout parameters */
    if (pParam->B4SOIpclm <= 0.0)
    {   fprintf(stderr, "Fatal: Pclm = %g is not positive.\n", pParam->B4SOIpclm);
        Fatal_Flag = 1;
    }

    if (pParam->B4SOIdrout < 0.0)
    {   fprintf(stderr, "Fatal: Drout = %g is negative.\n", pParam->B4SOIdrout);
        Fatal_Flag = 1;
    }
  if ( model->B4SOIunitLengthGateSidewallJctCapD > 0.0)        /* v4.0 */
  {
    if (here->B4SOIdrainPerimeter < pParam->B4SOIweff)
    {   fprintf(stderr, "Warning: Pd = %g is less than W.\n",
                here->B4SOIdrainPerimeter);
        here->B4SOIdrainPerimeter =pParam->B4SOIweff;
    }
  }
  if ( model->B4SOIunitLengthGateSidewallJctCapS > 0.0)        /* v4.0 */
  {
    if (here->B4SOIsourcePerimeter < pParam->B4SOIweff)
    {   fprintf(stderr, "Warning: Ps = %g is less than W.\n",
                here->B4SOIsourcePerimeter);
        here->B4SOIsourcePerimeter =pParam->B4SOIweff;
    }
  }
/* Check capacitance parameters */
    if (pParam->B4SOIclc < 0.0)
    {   fprintf(stderr, "Fatal: Clc = %g is negative.\n", pParam->B4SOIclc);
        Fatal_Flag = 1;
    }


/* v3.2 */
    if (pParam->B4SOInoff < 0.1)
    {   fprintf(stderr, "Warning: Noff = %g is too small.\n",
                pParam->B4SOInoff);
    }
    if (pParam->B4SOInoff > 4.0)
    {   fprintf(stderr, "Warning: Noff = %g is too large.\n",
                pParam->B4SOInoff);
    }

/* added for stress */

/* Check stress effect parameters */
    if( (here->B4SOIsa > 0.0) && (here->B4SOIsb > 0.0) &&
    ((here->B4SOInf == 1.0) || ((here->B4SOInf > 1.0) &&
    (here->B4SOIsd > 0.0))) )
    {   if (model->B4SOIlodk2 <= 0.0)
        {   fprintf(stderr, "Warning: LODK2 = %g is not positive.\n",model->B4SOIlodk2);
        }
        if (model->B4SOIlodeta0 <= 0.0)
        {   fprintf(stderr, "Warning: LODETA0 = %g is not positive.\n",model->B4SOIlodeta0);
        }
    }

/* added for stress end */

/* v2.2.3 */
    if (pParam->B4SOImoin < 5.0)
    {   fprintf(stderr, "Warning: Moin = %g is too small.\n",
                pParam->B4SOImoin);
    }
    if (pParam->B4SOImoin > 25.0)
    {   fprintf(stderr, "Warning: Moin = %g is too large.\n",
                pParam->B4SOImoin);
    }

/* v3.0 */
    if (model->B4SOImoinFD < 5.0)
    {   fprintf(stderr, "Warning: MoinFD = %g is too small.\n",
                model->B4SOImoinFD);
    }


    if (model->B4SOIcapMod == 3) {
            if (pParam->B4SOIacde < 0.1) /* v3.1.1 */
            {  fprintf(stderr, "Warning: Acde = %g is too small.\n",
                           pParam->B4SOIacde);
            }
            if (pParam->B4SOIacde > 1.6)
            {  fprintf(stderr, "Warning: Acde = %g is too large.\n",
                           pParam->B4SOIacde);
            }
    }

/* v4.2 always perform Fatal checks */
  if (pParam->B4SOInigc <= 0.0)
  {   fprintf(stderr, "Fatal: nigc = %g is non-positive.\n",
              pParam->B4SOInigc);
      Fatal_Flag = 1;
  }
  if (pParam->B4SOIpoxedge <= 0.0)
  {   fprintf(stderr, "Fatal: poxedge = %g is non-positive.\n",
              pParam->B4SOIpoxedge);
      Fatal_Flag = 1;
  }
  if (pParam->B4SOIpigcd <= 0.0)
  {   fprintf(stderr, "Fatal: pigcd = %g is non-positive.\n",
              pParam->B4SOIpigcd);
      Fatal_Flag = 1;
  }

  if (model->B4SOItoxref < 0.0)
  {   fprintf(stderr, "Warning: TOXREF = %g is negative.\n",
              model->B4SOItoxref);
      Fatal_Flag = 1;
  }

  if (model->B4SOItoxqm <= 0.0)
  {   fprintf(stderr, "Fatal: Toxqm = %g is not positive.\n",
              model->B4SOItoxqm);
      Fatal_Flag = 1;
  }

  if (model->B4SOIdeltavox <= 0.0)
  {   fprintf(stderr, "Fatal: Deltavox = %g is not positive.\n",
              model->B4SOIdeltavox);
  }

/* v4.4 Tanvir */
    if (pParam->B4SOIrdsw < 0.0)
    {   fprintf(stderr, "Warning: Rdsw = %g is negative. Set to zero.\n",
                pParam->B4SOIrdsw);
        pParam->B4SOIrdsw = 0.0;
        pParam->B4SOIrds0 = 0.0;
    }
    else if (pParam->B4SOIrds0 < 0.001)
    {   fprintf(stderr, "Warning: Rds at current temperature = %g is less than 0.001 ohm. Set to zero.\n",
                pParam->B4SOIrds0);
        pParam->B4SOIrds0 = 0.0;
    } /* v4.4 */
    if ((model->B4SOIcfrcoeff < 1.0)||(model->B4SOIcfrcoeff > 2.0))
    {   fprintf(stderr, "Warning: CfrCoeff = %g is out of range.\n",
                model->B4SOIcfrcoeff);
        model->B4SOIcfrcoeff = 1;
    } /* v4.4 */
  if (model->B4SOIparamChk ==1)
  {
/* Check L and W parameters */
    if (pParam->B4SOIleff <= 5.0e-8)
    {   fprintf(stderr, "Warning: Leff = %g may be too small.\n",
                pParam->B4SOIleff);
    }

    if (pParam->B4SOIleffCV <= 5.0e-8)
    {   fprintf(stderr, "Warning: Leff for CV = %g may be too small.\n",
                pParam->B4SOIleffCV);
    }

    if (pParam->B4SOIweff <= 1.0e-7)
    {   fprintf(stderr, "Warning: Weff = %g may be too small.\n",
                pParam->B4SOIweff);
    }

    if (pParam->B4SOIweffCV <= 1.0e-7)
    {   fprintf(stderr, "Warning: Weff for CV = %g may be too small.\n",
                pParam->B4SOIweffCV);
    }

/* Check threshold voltage parameters */
    if (pParam->B4SOIlpe0 < 0.0)
    {   fprintf(stderr, "Warning: Lpe0 = %g is negative.\n", pParam->B4SOIlpe0);
    }
    if (model->B4SOItox < 1.0e-9)
    {   fprintf(stderr, "Warning: Tox = %g is less than 10A.\n",
                model->B4SOItox);
    }

    if (pParam->B4SOInpeak <= 1.0e15)
    {   fprintf(stderr, "Warning: Nch = %g may be too small.\n",
                pParam->B4SOInpeak);
    }
    else if (pParam->B4SOInpeak >= 1.0e21)
    {   fprintf(stderr, "Warning: Nch = %g may be too large.\n",
                pParam->B4SOInpeak);
    }

    if (fabs(pParam->B4SOInsub) >= 1.0e21)
    {   fprintf(stderr, "Warning: Nsub = %g may be too large.\n",
                pParam->B4SOInsub);
    }

    if ((pParam->B4SOIngate > 0.0) &&
        (pParam->B4SOIngate <= 1.e18))
    {   fprintf(stderr, "Warning: Ngate = %g is less than 1.E18cm^-3.\n",
                pParam->B4SOIngate);
    }

    if (pParam->B4SOIdvt0 < 0.0)
    {   fprintf(stderr, "Warning: Dvt0 = %g is negative.\n",
                pParam->B4SOIdvt0);
    }

    if (fabs(1.0e-6 / (pParam->B4SOIw0 + pParam->B4SOIweff)) > 10.0)
    {   fprintf(stderr, "Warning: (W0 + Weff) may be too small.\n");
    }
/* Check Nsd, Ngate and Npeak parameters*/        /* Bug Fix # 22 Jul09*/
if (model->B4SOInsd > 1.0e23)
    {   fprintf(stderr, "Warning: Nsd = %g is too large, should be specified in cm^-3.\n",
                model->B4SOInsd);
    }
if (model->B4SOIngate > 1.0e23)
    {   fprintf(stderr, "Warning: Ngate = %g is too large, should be specified in cm^-3.\n",
                model->B4SOIngate);
    }
if (model->B4SOInpeak > 1.0e20)
    {   fprintf(stderr, "Warning: Npeak = %g is too large, should be less than 1.0e20, specified in cm^-3.\n",
                model->B4SOInpeak);
    }
/* Check subthreshold parameters */
    if (pParam->B4SOInfactor < 0.0)
    {   fprintf(stderr, "Warning: Nfactor = %g is negative.\n",
                pParam->B4SOInfactor);
    }
    if (pParam->B4SOIcdsc < 0.0)
    {   fprintf(stderr, "Warning: Cdsc = %g is negative.\n",
                pParam->B4SOIcdsc);
    }
    if (pParam->B4SOIcdscd < 0.0)
    {   fprintf(stderr, "Warning: Cdscd = %g is negative.\n",
                pParam->B4SOIcdscd);
    }
/* Check DIBL parameters */
    if (pParam->B4SOIeta0 < 0.0)
    {   fprintf(stderr, "Warning: Eta0 = %g is negative.\n",
                pParam->B4SOIeta0);
    }

/* Check Abulk parameters */
    if (fabs(1.0e-6 / (pParam->B4SOIb1 + pParam->B4SOIweff)) > 10.0)
           {   fprintf(stderr, "Warning: (B1 + Weff) may be too small.\n");
    }

/* Check Saturation parameters */
         if (pParam->B4SOIa2 < 0.01)
    {   fprintf(stderr, "Warning: A2 = %g is too small. Set to 0.01.\n", pParam->B4SOIa2);
        pParam->B4SOIa2 = 0.01;
    }
    else if (pParam->B4SOIa2 > 1.0)
    {   fprintf(stderr, "Warning: A2 = %g is larger than 1. A2 is set to 1 and A1 is set to 0.\n",
                pParam->B4SOIa2);
        pParam->B4SOIa2 = 1.0;
        pParam->B4SOIa1 = 0.0;

    }
/*
    if (pParam->B4SOIrdsw < 0.0)
    {   fprintf(stderr, "Warning: Rdsw = %g is negative. Set to zero.\n",
                pParam->B4SOIrdsw);
        pParam->B4SOIrdsw = 0.0;
        pParam->B4SOIrds0 = 0.0;
    }
    else if (pParam->B4SOIrds0 < 0.001)
    {   fprintf(stderr, "Warning: Rds at current temperature = %g is less than 0.001 ohm. Set to zero.\n",
                pParam->B4SOIrds0);
        pParam->B4SOIrds0 = 0.0;
    } v4.4 */
     if (pParam->B4SOIvsattemp < 1.0e3)
    {   fprintf(stderr, "Warning: Vsat at current temperature = %g may be too small.\n", pParam->B4SOIvsattemp);
    }

    if (pParam->B4SOIpdibl1 < 0.0)
    {   fprintf(stderr, "Warning: Pdibl1 = %g is negative.\n",
                pParam->B4SOIpdibl1);
    }
    if (pParam->B4SOIpdibl2 < 0.0)
    {   fprintf(stderr, "Warning: Pdibl2 = %g is negative.\n",
                pParam->B4SOIpdibl2);
    }
/* Check overlap capacitance parameters */
    if (model->B4SOIcgdo < 0.0)
    {   fprintf(stderr, "Warning: cgdo = %g is negative. Set to zero.\n", model->B4SOIcgdo);
        model->B4SOIcgdo = 0.0;
    }
    if (model->B4SOIcgso < 0.0)
    {   fprintf(stderr, "Warning: cgso = %g is negative. Set to zero.\n", model->B4SOIcgso);
        model->B4SOIcgso = 0.0;
    }
    if (model->B4SOIcgeo < 0.0)
    {   fprintf(stderr, "Warning: cgeo = %g is negative. Set to zero.\n", model->B4SOIcgeo);
        model->B4SOIcgeo = 0.0;
    }

    if (model->B4SOIntun < 0.0)
    {   fprintf(stderr, "Warning: Ntuns = %g is negative.\n",
                model->B4SOIntun);
    }

    if (model->B4SOIntund < 0.0)
    {   fprintf(stderr, "Warning: Ntund = %g is negative.\n",
                model->B4SOIntund);
    }

    if (model->B4SOIndiode < 0.0)
    {   fprintf(stderr, "Warning: Ndiode = %g is negative.\n",
                model->B4SOIndiode);
    }

    if (model->B4SOIndioded < 0.0)
    {   fprintf(stderr, "Warning: Ndioded = %g is negative.\n",
                model->B4SOIndioded);
    }

    if (model->B4SOIisbjt < 0.0)
    {   fprintf(stderr, "Warning: Isbjt = %g is negative.\n",
                model->B4SOIisbjt);
    }
    if (model->B4SOIidbjt < 0.0)
    {   fprintf(stderr, "Warning: Idbjt = %g is negative.\n",
                model->B4SOIidbjt);
    }

    if (model->B4SOIisdif < 0.0)
    {   fprintf(stderr, "Warning: Isdif = %g is negative.\n",
                model->B4SOIisdif);
    }
    if (model->B4SOIiddif < 0.0)
    {   fprintf(stderr, "Warning: Iddif = %g is negative.\n",
                model->B4SOIiddif);
    }

    if (model->B4SOIisrec < 0.0)
    {   fprintf(stderr, "Warning: Isrec = %g is negative.\n",
                model->B4SOIisrec);
    }
    if (model->B4SOIidrec < 0.0)
    {   fprintf(stderr, "Warning: Idrec = %g is negative.\n",
                model->B4SOIidrec);
    }

    if (model->B4SOIistun < 0.0)
    {   fprintf(stderr, "Warning: Istun = %g is negative.\n",
                model->B4SOIistun);
    }
    if (model->B4SOIidtun < 0.0)
    {   fprintf(stderr, "Warning: Idtun = %g is negative.\n",
                model->B4SOIidtun);
    }

    if (model->B4SOItt < 0.0)
    {   fprintf(stderr, "Warning: Tt = %g is negative.\n",
                model->B4SOItt);
    }

    if (model->B4SOIcsdmin < 0.0)
    {   fprintf(stderr, "Warning: Csdmin = %g is negative.\n",
                model->B4SOIcsdmin);
    }

    if (model->B4SOIcsdesw < 0.0)
    {   fprintf(stderr, "Warning: Csdesw = %g is negative.\n",
                model->B4SOIcsdesw);
    }

    if (model->B4SOIasd < 0.0)
    {   fprintf(stderr, "Warning: Asd = %g should be within (0, 1).\n",
                model->B4SOIasd);
    }

    if (model->B4SOIrth0 < 0.0)
    {   fprintf(stderr, "Warning: Rth0 = %g is negative.\n",
                model->B4SOIrth0);
    }

    if (model->B4SOIcth0 < 0.0)
    {   fprintf(stderr, "Warning: Cth0 = %g is negative.\n",
                model->B4SOIcth0);
    }

    if (model->B4SOIrbody < 0.0)
    {   fprintf(stderr, "Warning: Rbody = %g is negative.\n",
                model->B4SOIrbody);
    }

    if (model->B4SOIrbsh < 0.0)
    {   fprintf(stderr, "Warning: Rbsh = %g is negative.\n",
                model->B4SOIrbsh);
    }


/* v2.2 release */
    if (model->B4SOIwth0 < 0.0)
    {   fprintf(stderr, "Warning: WTH0 = %g is negative.\n",
                model->B4SOIwth0);
    }
    if (model->B4SOIrhalo < 0.0)
    {   fprintf(stderr, "Warning: RHALO = %g is negative.\n",
                model->B4SOIrhalo);
    }
    if (model->B4SOIntox < 0.0)
    {   fprintf(stderr, "Warning: NTOX = %g is negative.\n",
                model->B4SOIntox);
    }
    if (model->B4SOIebg < 0.0)
    {   fprintf(stderr, "Warning: EBG = %g is negative.\n",
                model->B4SOIebg);
    }
    if (model->B4SOIvevb < 0.0)
    {   fprintf(stderr, "Warning: VEVB = %g is negative.\n",
                model->B4SOIvevb);
    }
    if (pParam->B4SOIalphaGB1 < 0.0)
    {   fprintf(stderr, "Warning: ALPHAGB1 = %g is negative.\n",
                pParam->B4SOIalphaGB1);
    }
    if (pParam->B4SOIbetaGB1 < 0.0)
    {   fprintf(stderr, "Warning: BETAGB1 = %g is negative.\n",
                pParam->B4SOIbetaGB1);
    }
    if (model->B4SOIvgb1 < 0.0)
    {   fprintf(stderr, "Warning: VGB1 = %g is negative.\n",
                model->B4SOIvgb1);
    }
    if (model->B4SOIvecb < 0.0)
    {   fprintf(stderr, "Warning: VECB = %g is negative.\n",
                model->B4SOIvecb);
    }
    if (pParam->B4SOIalphaGB2 < 0.0)
    {   fprintf(stderr, "Warning: ALPHAGB2 = %g is negative.\n",
                pParam->B4SOIalphaGB2);
    }
    if (pParam->B4SOIbetaGB2 < 0.0)
    {   fprintf(stderr, "Warning: BETAGB2 = %g is negative.\n",
                pParam->B4SOIbetaGB2);
    }
    if (model->B4SOIvgb2 < 0.0)
    {   fprintf(stderr, "Warning: VGB2 = %g is negative.\n",
                model->B4SOIvgb2);
    }
    if (model->B4SOIvoxh < 0.0)
    {   fprintf(stderr, "Warning: Voxh = %g is negative.\n",
                model->B4SOIvoxh);
    }

/* v2.0 release */
    if (model->B4SOIk1w1 < 0.0)
    {   fprintf(stderr, "Warning: K1W1 = %g is negative.\n",
                model->B4SOIk1w1);
    }
    if (model->B4SOIk1w2 < 0.0)
    {   fprintf(stderr, "Warning: K1W2 = %g is negative.\n",
                model->B4SOIk1w2);
    }
    if (model->B4SOIketas < 0.0)
    {   fprintf(stderr, "Warning: KETAS = %g is negative.\n",
                model->B4SOIketas);
    }
    if (model->B4SOIdwbc < 0.0)
    {   fprintf(stderr, "Warning: DWBC = %g is negative.\n",
                model->B4SOIdwbc);
    }
    if (model->B4SOIbeta0 < 0.0)
    {   fprintf(stderr, "Warning: BETA0 = %g is negative.\n",
                model->B4SOIbeta0);
    }
    if (model->B4SOIbeta1 < 0.0)
    {   fprintf(stderr, "Warning: BETA1 = %g is negative.\n",
                model->B4SOIbeta1);
    }
    if (model->B4SOIbeta2 < 0.0)
    {   fprintf(stderr, "Warning: BETA2 = %g is negative.\n",
                model->B4SOIbeta2);
    }
    if (model->B4SOItii < 0.0)
    {   fprintf(stderr, "Warning: TII = %g is negative.\n",
                model->B4SOItii);
    }
    if (model->B4SOIlii < 0.0)
    {   fprintf(stderr, "Warning: LII = %g is negative.\n",
                model->B4SOIlii);
    }
    if (model->B4SOIsii1 < 0.0)
    {   fprintf(stderr, "Warning: SII1 = %g is negative.\n",
                model->B4SOIsii1);
    }
    if (model->B4SOIsii2 < 0.0)
    {   fprintf(stderr, "Warning: SII2 = %g is negative.\n",
                model->B4SOIsii2);
    }
    if (model->B4SOIsiid < 0.0)
    {   fprintf(stderr, "Warning: SIID = %g is negative.\n",
                model->B4SOIsiid);
    }
    if (model->B4SOIfbjtii < 0.0)
    {   fprintf(stderr, "Warning: FBJTII = %g is negative.\n",
                model->B4SOIfbjtii);
    }
    if (model->B4SOIvrec0 < 0.0)        /* v4.0 */
    {   fprintf(stderr, "Warning: VREC0S = %g is negative.\n",
                model->B4SOIvrec0);
    }
    if (model->B4SOIvrec0d < 0.0)        /* v4.0 */
    {   fprintf(stderr, "Warning: VREC0D = %g is negative.\n",
                model->B4SOIvrec0d);
    }
    if (model->B4SOIvtun0 < 0.0)        /* v4.0 */
    {   fprintf(stderr, "Warning: VTUN0S = %g is negative.\n",
                model->B4SOIvtun0);
    }
    if (model->B4SOIvtun0d < 0.0)        /* v4.0 */
    {   fprintf(stderr, "Warning: VTUN0D = %g is negative.\n",
                model->B4SOIvtun0d);
    }
    if (model->B4SOInbjt < 0.0)
    {   fprintf(stderr, "Warning: NBJT = %g is negative.\n",
                model->B4SOInbjt);
    }
    if (model->B4SOIaely < 0.0)
    {   fprintf(stderr, "Warning: AELY = %g is negative.\n",
                model->B4SOIaely);
    }
    if (model->B4SOIahli < 0.0)
    {   fprintf(stderr, "Warning: AHLIS = %g is negative.\n",
                model->B4SOIahli);
    }
    if (model->B4SOIahlid < 0.0)
    {   fprintf(stderr, "Warning: AHLID = %g is negative.\n",
                model->B4SOIahlid);
    }
    if (model->B4SOIrbody < 0.0)
    {   fprintf(stderr, "Warning: RBODY = %g is negative.\n",
                model->B4SOIrbody);
    }
    if (model->B4SOIrbsh < 0.0)
    {   fprintf(stderr, "Warning: RBSH = %g is negative.\n",
                model->B4SOIrbsh);
    }
 /*       if (pParam->B4SOIntrecf < 0.0)
    {   fprintf(stderr, "Warning: NTRECF = %g is negative.\n",
                pParam->B4SOIntrecf);
    }
    if (pParam->B4SOIntrecr < 0.0)
    {   fprintf(stderr, "Warning: NTRECR = %g is negative.\n",
                pParam->B4SOIntrecr);
    } v4.2 bugfix: QA Test uses negative temp co-efficients*/

/* v3.0 bug fix */
/*
    if (model->B4SOIndif < 0.0)
    {   fprintf(stderr, "Warning: NDIF = %g is negative.\n",
                model->B4SOIndif);
    }
*/

/*        if (model->B4SOItcjswg < 0.0)
    {   fprintf(stderr, "Warning: TCJSWGS = %g is negative.\n",
                model->B4SOItcjswg);
   }
    if (model->B4SOItpbswg < 0.0)
    {   fprintf(stderr, "Warning: TPBSWGS = %g is negative.\n",
                model->B4SOItpbswg);
   }
    if (model->B4SOItcjswgd < 0.0)
    {   fprintf(stderr, "Warning: TCJSWGD = %g is negative.\n",
                model->B4SOItcjswgd);
   }
    if (model->B4SOItpbswgd < 0.0)
    {   fprintf(stderr, "Warning: TPBSWGD = %g is negative.\n",
                model->B4SOItpbswgd);
   }   v4.2 bugfix: QA Test uses negative temp co-efficients*/
    if ((model->B4SOIacde < 0.1) || (model->B4SOIacde > 1.6))
    {   fprintf(stderr, "Warning: ACDE = %g is out of range.\n",
                model->B4SOIacde);
    }
    if ((model->B4SOImoin < 5.0)||(model->B4SOImoin > 25.0))
    {   fprintf(stderr, "Warning: MOIN = %g is out of range.\n",
                model->B4SOImoin);
    }
    if (model->B4SOIdlbg < 0.0)
    {   fprintf(stderr, "Warning: DLBG = %g is negative.\n",
                model->B4SOIdlbg);
    }
    if (model->B4SOIagidl < 0.0)
    {   fprintf(stderr, "Warning: AGIDL = %g is negative.\n",
                model->B4SOIagidl);
    }
    if (model->B4SOIbgidl < 0.0)
    {   fprintf(stderr, "Warning: BGIDL = %g is negative.\n",
                model->B4SOIbgidl);
    }
    if (fabs(model->B4SOIcgidl) < 1e-9)
    {   fprintf(stderr, "Warning: CGIDL = %g is smaller than 1e-9.\n",
                model->B4SOIcgidl);
    }
    if (model->B4SOIegidl < 0.0)
    {   fprintf(stderr, "Warning: EGIDL = %g is negative.\n",
                model->B4SOIegidl);
    }

            if (model->B4SOIagisl < 0.0)
    {   fprintf(stderr, "Warning: AGISL = %g is negative.\n",
                model->B4SOIagisl);
    }
    if (model->B4SOIbgisl < 0.0)
    {   fprintf(stderr, "Warning: BGISL = %g is negative.\n",
                model->B4SOIbgisl);
    }
    if (fabs(model->B4SOIcgisl) < 1e-9)
    {   fprintf(stderr, "Warning: CGISL = %g is smaller than 1e-9.\n",
                model->B4SOIcgisl);
    }
    if (model->B4SOIegisl < 0.0)
    {   fprintf(stderr, "Warning: EGISL = %g is negative.\n",
                model->B4SOIegisl);
    }

    if (model->B4SOIesatii < 0.0)
    {   fprintf(stderr, "Warning: Esatii = %g should be within positive.\n",
                model->B4SOIesatii);
    }

            if (!model->B4SOIvgstcvModGiven)
    {   fprintf(stderr, "Warning: The default vgstcvMod is changed in v4.2 from '0' to '1'.\n");
    }
    if (pParam->B4SOIxj > model->B4SOItsi)
    {   fprintf(stderr, "Warning: Xj = %g is thicker than Tsi = %g.\n",
                pParam->B4SOIxj, model->B4SOItsi);
    }

    if (model->B4SOIcapMod < 2)
    {   fprintf(stderr, "Warning: capMod < 2 is not supported by BSIM3SOI.\n");
    }
        if (model->B4SOIcapMod > 3)
    {   fprintf(stderr, "Warning: capMod > 3 is not supported by BSIMSOI4.2.\n");
    }

  }/* loop for the parameter check for warning messages */

    return(Fatal_Flag);
}


} // namespace neospice::bsimsoi
