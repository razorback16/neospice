/**** BSIM3v3.2.4, Released by Xuemei Xi 12/14/2001 ****/

/**********
 * Copyright 2001 Regents of the University of California. All rights reserved.
 * File: b3check.c of BSIM3v3.2.4
 * Author: 1995 Min-Chie Jeng
 * Author: 1997-1999 Weidong Liu.
 * Author: 2001 Xuemei Xi
 * Modified by Xuemei Xi, 10/05, 12/14, 2001.
 * Modified by Paolo Nenzi 2002 and Dietmar Warning 2003
 **********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/bsim3v32/bsim3v32_def.hpp"
#include "devices/bsim3v32/bsim3v32_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "devices/ucb_compat.hpp"

namespace neospice::bsim3v32 {

using namespace Shim;

int
BSIM3v32checkModel (BSIM3v32Model *model, BSIM3v32Instance *here, Shim::Ckt *ckt)
{
struct bsim3v32SizeDependParam *pParam;
int Fatal_Flag = 0;

    NG_IGNORE(ckt);

    pParam = here->pParam;

    fprintf(stderr,
             "BSIM3 Model (Supports: v3.2, v3.2.2, v3.2.3, v3.2.4)\n");
    fprintf(stderr, "Parameter Checking.\n");
    fprintf(stderr, "Model = %s\n", model->BSIM3v32modName);
    fprintf(stderr, "W = %g, L = %g, M = %g\n", here->BSIM3v32w,
             here->BSIM3v32l, here->BSIM3v32m);

    if ((strcmp(model->BSIM3v32version, "3.2.4")) && (strcmp(model->BSIM3v32version, "3.24"))
     && (strcmp(model->BSIM3v32version, "3.2.3")) && (strcmp(model->BSIM3v32version, "3.23"))
     && (strcmp(model->BSIM3v32version, "3.2.2")) && (strcmp(model->BSIM3v32version, "3.22"))
     && (strcmp(model->BSIM3v32version, "3.2")) && (strcmp(model->BSIM3v32version, "3.20")))
    {
        fprintf(stderr,
            "Warning: This model supports BSIM3v3.2, BSIM3v3.2.2, BSIM3v3.2.3, BSIM3v3.2.4\n");
        fprintf(stderr,
            "You specified a wrong version number. Working now with BSIM3v3.2.4.\n");
    }

    if (pParam->BSIM3v32nlx < -pParam->BSIM3v32leff)
    {   fprintf(stderr, "Fatal: Nlx = %g is less than -Leff.\n",
                    pParam->BSIM3v32nlx);
        Fatal_Flag = 1;
    }

    if (model->BSIM3v32tox <= 0.0)
    {   fprintf(stderr, "Fatal: Tox = %g is not positive.\n",
                model->BSIM3v32tox);
        Fatal_Flag = 1;
    }

    if (model->BSIM3v32toxm <= 0.0)
    {   fprintf(stderr, "Fatal: Toxm = %g is not positive.\n",
                model->BSIM3v32toxm);
        Fatal_Flag = 1;
    }

    if (pParam->BSIM3v32npeak <= 0.0)
    {   fprintf(stderr, "Fatal: Nch = %g is not positive.\n",
                pParam->BSIM3v32npeak);
        Fatal_Flag = 1;
    }
    if (pParam->BSIM3v32nsub <= 0.0)
    {   fprintf(stderr, "Fatal: Nsub = %g is not positive.\n",
                pParam->BSIM3v32nsub);
        Fatal_Flag = 1;
    }
    if (pParam->BSIM3v32ngate < 0.0)
    {   fprintf(stderr, "Fatal: Ngate = %g is not positive.\n",
                pParam->BSIM3v32ngate);
        Fatal_Flag = 1;
    }
    if (pParam->BSIM3v32ngate > 1.e25)
    {   fprintf(stderr, "Fatal: Ngate = %g is too high.\n",
                pParam->BSIM3v32ngate);
        Fatal_Flag = 1;
    }
    if (pParam->BSIM3v32xj <= 0.0)
    {   fprintf(stderr, "Fatal: Xj = %g is not positive.\n",
                pParam->BSIM3v32xj);
        Fatal_Flag = 1;
    }

    if (pParam->BSIM3v32dvt1 < 0.0)
    {   fprintf(stderr, "Fatal: Dvt1 = %g is negative.\n",
                pParam->BSIM3v32dvt1);
        Fatal_Flag = 1;
    }

    if (pParam->BSIM3v32dvt1w < 0.0)
    {   fprintf(stderr, "Fatal: Dvt1w = %g is negative.\n",
                pParam->BSIM3v32dvt1w);
        Fatal_Flag = 1;
    }

    if (pParam->BSIM3v32w0 == -pParam->BSIM3v32weff)
    {   fprintf(stderr, "Fatal: (W0 + Weff) = 0 causing divided-by-zero.\n");
        Fatal_Flag = 1;
    }

    if (pParam->BSIM3v32dsub < 0.0)
    {   fprintf(stderr, "Fatal: Dsub = %g is negative.\n", pParam->BSIM3v32dsub);
        Fatal_Flag = 1;
    }
    if (pParam->BSIM3v32b1 == -pParam->BSIM3v32weff)
    {   fprintf(stderr, "Fatal: (B1 + Weff) = 0 causing divided-by-zero.\n");
        Fatal_Flag = 1;
    }
    if (pParam->BSIM3v32u0temp <= 0.0)
    {   fprintf(stderr, "Fatal: u0 at current temperature = %g is not positive.\n", pParam->BSIM3v32u0temp);
        Fatal_Flag = 1;
    }

/* Check delta parameter */
    if (pParam->BSIM3v32delta < 0.0)
    {   fprintf(stderr, "Fatal: Delta = %g is less than zero.\n",
                pParam->BSIM3v32delta);
        Fatal_Flag = 1;
    }

    if (pParam->BSIM3v32vsattemp <= 0.0)
    {   fprintf(stderr, "Fatal: Vsat at current temperature = %g is not positive.\n", pParam->BSIM3v32vsattemp);
        Fatal_Flag = 1;
    }
/* Check Rout parameters */
    if (pParam->BSIM3v32pclm <= 0.0)
    {   fprintf(stderr, "Fatal: Pclm = %g is not positive.\n", pParam->BSIM3v32pclm);
        Fatal_Flag = 1;
    }

    if (pParam->BSIM3v32drout < 0.0)
    {   fprintf(stderr, "Fatal: Drout = %g is negative.\n", pParam->BSIM3v32drout);
        Fatal_Flag = 1;
    }

    if (pParam->BSIM3v32pscbe2 <= 0.0)
    {   fprintf(stderr, "Warning: Pscbe2 = %g is not positive.\n",
                pParam->BSIM3v32pscbe2);
    }

    /* ACM model */
    if (model->BSIM3v32acmMod == 0) {
        if (model->BSIM3v32unitLengthSidewallJctCap > 0.0 ||
              model->BSIM3v32unitLengthGateSidewallJctCap > 0.0)
        {
            if (here->BSIM3v32drainPerimeter < pParam->BSIM3v32weff)
            {   fprintf(stderr, "Warning: Pd = %g is less than W.\n",
                     here->BSIM3v32drainPerimeter);
            }
            if (here->BSIM3v32sourcePerimeter < pParam->BSIM3v32weff)
            {   fprintf(stderr, "Warning: Ps = %g is less than W.\n",
                     here->BSIM3v32sourcePerimeter);
            }
        }
    }

    if ((model->BSIM3v32calcacm > 0) && (model->BSIM3v32acmMod != 12))
    {   fprintf(stderr, "Warning: CALCACM = %d is wrong. Set back to 0.\n",
            model->BSIM3v32calcacm);
        model->BSIM3v32calcacm = 0;
    }

    if (pParam->BSIM3v32noff < 0.1)
    {   fprintf(stderr, "Warning: Noff = %g is too small.\n",
                pParam->BSIM3v32noff);
    }
    if (pParam->BSIM3v32noff > 4.0)
    {   fprintf(stderr, "Warning: Noff = %g is too large.\n",
                pParam->BSIM3v32noff);
    }

    if (pParam->BSIM3v32voffcv < -0.5)
    {   fprintf(stderr, "Warning: Voffcv = %g is too small.\n",
                pParam->BSIM3v32voffcv);
    }
    if (pParam->BSIM3v32voffcv > 0.5)
    {   fprintf(stderr, "Warning: Voffcv = %g is too large.\n",
                pParam->BSIM3v32voffcv);
    }

    if (model->BSIM3v32ijth < 0.0)
    {   fprintf(stderr, "Fatal: Ijth = %g cannot be negative.\n",
                model->BSIM3v32ijth);
        Fatal_Flag = 1;
    }

/* Check capacitance parameters */
    if (pParam->BSIM3v32clc < 0.0)
    {   fprintf(stderr, "Fatal: Clc = %g is negative.\n", pParam->BSIM3v32clc);
        Fatal_Flag = 1;
    }

    if (pParam->BSIM3v32moin < 5.0)
    {   fprintf(stderr, "Warning: Moin = %g is too small.\n",
                pParam->BSIM3v32moin);
    }
    if (pParam->BSIM3v32moin > 25.0)
    {   fprintf(stderr, "Warning: Moin = %g is too large.\n",
                pParam->BSIM3v32moin);
    }

    if(model->BSIM3v32capMod ==3) {
            if (pParam->BSIM3v32acde < 0.4)
            {   fprintf(stderr, "Warning:  Acde = %g is too small.\n",
                        pParam->BSIM3v32acde);
            }
            if (pParam->BSIM3v32acde > 1.6)
            {   fprintf(stderr, "Warning:  Acde = %g is too large.\n",
                        pParam->BSIM3v32acde);
            }
    }

    if (model->BSIM3v32paramChk ==1)
    {
    /* Check L and W parameters */
          if (pParam->BSIM3v32leff <= 5.0e-8)
          {   fprintf(stderr, "Warning: Leff = %g may be too small.\n",
                      pParam->BSIM3v32leff);
          }

          if (pParam->BSIM3v32leffCV <= 5.0e-8)
          {   fprintf(stderr, "Warning: Leff for CV = %g may be too small.\n",
                      pParam->BSIM3v32leffCV);
          }

          if (pParam->BSIM3v32weff <= 1.0e-7)
          {   fprintf(stderr, "Warning: Weff = %g may be too small.\n",
                      pParam->BSIM3v32weff);
          }

          if (pParam->BSIM3v32weffCV <= 1.0e-7)
          {   fprintf(stderr, "Warning: Weff for CV = %g may be too small.\n",
                      pParam->BSIM3v32weffCV);
          }

    /* Check threshold voltage parameters */
          if (pParam->BSIM3v32nlx < 0.0)
          {   fprintf(stderr, "Warning: Nlx = %g is negative.\n", pParam->BSIM3v32nlx);
          }
           if (model->BSIM3v32tox < 1.0e-9)
          {   fprintf(stderr, "Warning: Tox = %g is less than 10A.\n",
                      model->BSIM3v32tox);
          }

          if (pParam->BSIM3v32npeak <= 1.0e15)
          {   fprintf(stderr, "Warning: Nch = %g may be too small.\n",
                      pParam->BSIM3v32npeak);
          }
          else if (pParam->BSIM3v32npeak >= 1.0e21)
          {   fprintf(stderr, "Warning: Nch = %g may be too large.\n",
                      pParam->BSIM3v32npeak);
          }

          if (pParam->BSIM3v32nsub <= 1.0e14)
          {   fprintf(stderr, "Warning: Nsub = %g may be too small.\n",
                      pParam->BSIM3v32nsub);
          }
          else if (pParam->BSIM3v32nsub >= 1.0e21)
          {   fprintf(stderr, "Warning: Nsub = %g may be too large.\n",
                      pParam->BSIM3v32nsub);
          }

          if ((pParam->BSIM3v32ngate > 0.0) &&
              (pParam->BSIM3v32ngate <= 1.e18))
          {   fprintf(stderr, "Warning: Ngate = %g is less than 1.E18cm^-3.\n",
                      pParam->BSIM3v32ngate);
          }

          if (pParam->BSIM3v32dvt0 < 0.0)
          {   fprintf(stderr, "Warning: Dvt0 = %g is negative.\n",
                      pParam->BSIM3v32dvt0);
          }

          if (fabs(1.0e-6 / (pParam->BSIM3v32w0 + pParam->BSIM3v32weff)) > 10.0)
          {   fprintf(stderr, "Warning: (W0 + Weff) may be too small.\n");
          }

    /* Check subthreshold parameters */
          if (pParam->BSIM3v32nfactor < 0.0)
          {   fprintf(stderr, "Warning: Nfactor = %g is negative.\n",
                      pParam->BSIM3v32nfactor);
          }
          if (pParam->BSIM3v32cdsc < 0.0)
          {   fprintf(stderr, "Warning: Cdsc = %g is negative.\n",
                      pParam->BSIM3v32cdsc);
          }
          if (pParam->BSIM3v32cdscd < 0.0)
          {   fprintf(stderr, "Warning: Cdscd = %g is negative.\n",
                      pParam->BSIM3v32cdscd);
          }
    /* Check DIBL parameters */
          if (pParam->BSIM3v32eta0 < 0.0)
          {   fprintf(stderr, "Warning: Eta0 = %g is negative.\n",
                      pParam->BSIM3v32eta0);
          }

    /* Check Abulk parameters */
           if (fabs(1.0e-6 / (pParam->BSIM3v32b1 + pParam->BSIM3v32weff)) > 10.0)
                 {   fprintf(stderr, "Warning: (B1 + Weff) may be too small.\n");
          }


    /* Check Saturation parameters */
          if (pParam->BSIM3v32a2 < 0.01)
          {   fprintf(stderr, "Warning: A2 = %g is too small. Set to 0.01.\n", pParam->BSIM3v32a2);
              pParam->BSIM3v32a2 = 0.01;
          }
          else if (pParam->BSIM3v32a2 > 1.0)
          {   fprintf(stderr, "Warning: A2 = %g is larger than 1. A2 is set to 1 and A1 is set to 0.\n",
                      pParam->BSIM3v32a2);
              pParam->BSIM3v32a2 = 1.0;
              pParam->BSIM3v32a1 = 0.0;

          }

          if (pParam->BSIM3v32rdsw < 0.0)
          {   fprintf(stderr, "Warning: Rdsw = %g is negative. Set to zero.\n",
                      pParam->BSIM3v32rdsw);
              pParam->BSIM3v32rdsw = 0.0;
              pParam->BSIM3v32rds0 = 0.0;
          }
          else if ((pParam->BSIM3v32rds0 > 0.0) && (pParam->BSIM3v32rds0 < 0.001))
          {   fprintf(stderr, "Warning: Rds at current temperature = %g is less than 0.001 ohm. Set to zero.\n",
                      pParam->BSIM3v32rds0);
              pParam->BSIM3v32rds0 = 0.0;
          }
          if (pParam->BSIM3v32vsattemp < 1.0e3)
          {   fprintf(stderr, "Warning: Vsat at current temperature = %g may be too small.\n", pParam->BSIM3v32vsattemp);
          }

          if (pParam->BSIM3v32pdibl1 < 0.0)
          {   fprintf(stderr, "Warning: Pdibl1 = %g is negative.\n",
                      pParam->BSIM3v32pdibl1);
          }
          if (pParam->BSIM3v32pdibl2 < 0.0)
          {   fprintf(stderr, "Warning: Pdibl2 = %g is negative.\n",
                      pParam->BSIM3v32pdibl2);
          }
    /* Check overlap capacitance parameters */
          if (model->BSIM3v32cgdo < 0.0)
          {   fprintf(stderr, "Warning: cgdo = %g is negative. Set to zero.\n", model->BSIM3v32cgdo);
              model->BSIM3v32cgdo = 0.0;
          }
          if (model->BSIM3v32cgso < 0.0)
          {   fprintf(stderr, "Warning: cgso = %g is negative. Set to zero.\n", model->BSIM3v32cgso);
              model->BSIM3v32cgso = 0.0;
          }
          if (model->BSIM3v32cgbo < 0.0)
          {   fprintf(stderr, "Warning: cgbo = %g is negative. Set to zero.\n", model->BSIM3v32cgbo);
              model->BSIM3v32cgbo = 0.0;
          }

    }/* loop for the parameter check for warning messages */

    return(Fatal_Flag);
}


} // namespace neospice::bsim3v32
