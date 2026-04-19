/**** BSIM4.7.0 Released by Darsen Lu 04/08/2011 ****/

/**********
 * Copyright 2006 Regents of the University of California. All rights reserved.
 * File: b4geo.c of BSIM4.7.0.
 * Author: 2000 Weidong Liu
 * Authors: 2001- Xuemei Xi, Mohan Dunga, Ali Niknejad, Chenming Hu.
 * Authors: 2006- Mohan Dunga, Ali Niknejad, Chenming Hu
 * Authors: 2007- Mohan Dunga, Wenwei Yang, Ali Niknejad, Chenming Hu
 * Project Director: Prof. Chenming Hu.
 **********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_shim.hpp"
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

namespace neospice::bsim4v7 {

using namespace Shim;

/*
 * WDLiu:
 * This subrutine is a special module to process the geometry dependent
 * parasitics for BSIM4v7, which calculates Ps, Pd, As, Ad, and Rs and  Rd
 * for multi-fingers and varous GEO and RGEO options.
 */

static int
BSIM4v7NumFingerDiff(
double nf,
int minSD,
double *nuIntD, double *nuEndD, double *nuIntS, double *nuEndS)
{
int NF;
        NF = (int)nf;
	if ((NF%2) != 0)
	{   *nuEndD = *nuEndS = 1.0;
	    *nuIntD = *nuIntS = 2.0 * MAX((nf - 1.0) / 2.0, 0.0);
	}
	else
	{   if (minSD == 1) /* minimize # of source */
	    {   *nuEndD = 2.0;
		*nuIntD = 2.0 * MAX((nf / 2.0 - 1.0), 0.0);
		*nuEndS = 0.0;
		*nuIntS = nf;
	    }
	    else
	    {   *nuEndD = 0.0;
                *nuIntD = nf;
                *nuEndS = 2.0;
                *nuIntS = 2.0 * MAX((nf / 2.0 - 1.0), 0.0);
	    }
	}
return 0;
}


int
BSIM4v7PAeffGeo(
double nf,
int geo, int minSD,
double Weffcj, double DMCG, double DMCI, double DMDG,
double *Ps, double *Pd, double *As, double *Ad)
{
double T0, T1, T2;
double ADiso, ADsha, ADmer, ASiso, ASsha, ASmer;
double PDiso, PDsha, PDmer, PSiso, PSsha, PSmer;
double nuIntD = 0.0, nuEndD = 0.0, nuIntS = 0.0, nuEndS = 0.0;

	if (geo < 9) /* For geo = 9 and 10, the numbers of S/D diffusions already known */
	BSIM4v7NumFingerDiff(nf, minSD, &nuIntD, &nuEndD, &nuIntS, &nuEndS);

	T0 = DMCG + DMCI;
	T1 = DMCG + DMCG;
	T2 = DMDG + DMDG;

	PSiso = PDiso = T0 + T0 + Weffcj;
	PSsha = PDsha = T1;
	PSmer = PDmer = T2;

	ASiso = ADiso = T0 * Weffcj;
	ASsha = ADsha = DMCG * Weffcj;
	ASmer = ADmer = DMDG * Weffcj;

	switch(geo)
	{   case 0:
		*Ps = nuEndS * PSiso + nuIntS * PSsha;
		*Pd = nuEndD * PDiso + nuIntD * PDsha;
		*As = nuEndS * ASiso + nuIntS * ASsha;
		*Ad = nuEndD * ADiso + nuIntD * ADsha;
		break;
	    case 1:
                *Ps = nuEndS * PSiso + nuIntS * PSsha;
                *Pd = (nuEndD + nuIntD) * PDsha;
                *As = nuEndS * ASiso + nuIntS * ASsha;
                *Ad = (nuEndD + nuIntD) * ADsha;
                break;
            case 2:
                *Ps = (nuEndS + nuIntS) * PSsha;
                *Pd = nuEndD * PDiso + nuIntD * PDsha;
                *As = (nuEndS + nuIntS) * ASsha;
                *Ad = nuEndD * ADiso + nuIntD * ADsha;
                break;
            case 3:
                *Ps = (nuEndS + nuIntS) * PSsha;
                *Pd = (nuEndD + nuIntD) * PDsha;
                *As = (nuEndS + nuIntS) * ASsha;
                *Ad = (nuEndD + nuIntD) * ADsha;
                break;
            case 4:
                *Ps = nuEndS * PSiso + nuIntS * PSsha;
                *Pd = nuEndD * PDmer + nuIntD * PDsha;
                *As = nuEndS * ASiso + nuIntS * ASsha;
                *Ad = nuEndD * ADmer + nuIntD * ADsha;
                break;
            case 5:
                *Ps = (nuEndS + nuIntS) * PSsha;
                *Pd = nuEndD * PDmer + nuIntD * PDsha;
                *As = (nuEndS + nuIntS) * ASsha;
                *Ad = nuEndD * ADmer + nuIntD * ADsha;
                break;
            case 6:
                *Ps = nuEndS * PSmer + nuIntS * PSsha;
                *Pd = nuEndD * PDiso + nuIntD * PDsha;
                *As = nuEndS * ASmer + nuIntS * ASsha;
                *Ad = nuEndD * ADiso + nuIntD * ADsha;
                break;
            case 7:
                *Ps = nuEndS * PSmer + nuIntS * PSsha;
                *Pd = (nuEndD + nuIntD) * PDsha;
                *As = nuEndS * ASmer + nuIntS * ASsha;
                *Ad = (nuEndD + nuIntD) * ADsha;
                break;
            case 8:
                *Ps = nuEndS * PSmer + nuIntS * PSsha;
                *Pd = nuEndD * PDmer + nuIntD * PDsha;
                *As = nuEndS * ASmer + nuIntS * ASsha;
                *Ad = nuEndD * ADmer + nuIntD * ADsha;
                break;
            case 9: /* geo = 9 and 10 happen only when nf = even */
                *Ps = PSiso + (nf - 1.0) * PSsha;
                *Pd = nf * PDsha;
                *As = ASiso + (nf - 1.0) * ASsha;
                *Ad = nf * ADsha;
                break;
            case 10:
                *Ps = nf * PSsha;
                *Pd = PDiso + (nf - 1.0) * PDsha;
                *As = nf * ASsha;
                *Ad = ADiso + (nf - 1.0) * ADsha;
                break;
	    default:
		printf("Warning: Specified GEO = %d not matched\n", geo); 
	}
return 0;
}


int
BSIM4v7RdseffGeo(
double nf,
int geo, int rgeo, int minSD,
double Weffcj, double Rsh, double DMCG, double DMCI, double DMDG,
int Type,
double *Rtot)
{
double Rint=0.0, Rend = 0.0;
double nuIntD = 0.0, nuEndD = 0.0, nuIntS = 0.0, nuEndS = 0.0;

        if (geo < 9) /* since geo = 9 and 10 only happen when nf = even */
        {   BSIM4v7NumFingerDiff(nf, minSD, &nuIntD, &nuEndD, &nuIntS, &nuEndS);

            /* Internal S/D resistance -- assume shared S or D and all wide contacts */
	    if (Type == 1)
	    {   if (nuIntS == 0.0)
		    Rint = 0.0;
	        else
		    Rint = Rsh * DMCG / ( Weffcj * nuIntS); 
	    }
	    else
	    {  if (nuIntD == 0.0)
                   Rint = 0.0;
               else        
                   Rint = Rsh * DMCG / ( Weffcj * nuIntD);
	    }
	}

        /* End S/D resistance  -- geo dependent */
        switch(geo)
        {   case 0:
		if (Type == 1) BSIM4v7RdsEndIso(Weffcj, Rsh, DMCG, DMCI, DMDG,
					      nuEndS, rgeo, 1, &Rend);
		else           BSIM4v7RdsEndIso(Weffcj, Rsh, DMCG, DMCI, DMDG,
			     		      nuEndD, rgeo, 0, &Rend);
                break;
            case 1:
                if (Type == 1) BSIM4v7RdsEndIso(Weffcj, Rsh, DMCG, DMCI, DMDG,
                                              nuEndS, rgeo, 1, &Rend);
                else           BSIM4v7RdsEndSha(Weffcj, Rsh, DMCG, DMCI, DMDG,
					      nuEndD, rgeo, 0, &Rend);
                break;
            case 2:
                if (Type == 1) BSIM4v7RdsEndSha(Weffcj, Rsh, DMCG, DMCI, DMDG,
					      nuEndS, rgeo, 1, &Rend);
                else           BSIM4v7RdsEndIso(Weffcj, Rsh, DMCG, DMCI, DMDG,
					      nuEndD, rgeo, 0, &Rend);
                break;
            case 3:
                if (Type == 1) BSIM4v7RdsEndSha(Weffcj, Rsh, DMCG, DMCI, DMDG,
                                              nuEndS, rgeo, 1, &Rend);
                else           BSIM4v7RdsEndSha(Weffcj, Rsh, DMCG, DMCI, DMDG,
                                              nuEndD, rgeo, 0, &Rend);
                break;
            case 4:
                if (Type == 1) BSIM4v7RdsEndIso(Weffcj, Rsh, DMCG, DMCI, DMDG,
                                              nuEndS, rgeo, 1, &Rend);
                else           Rend = Rsh * DMDG / Weffcj;
                break;
            case 5:
                if (Type == 1) BSIM4v7RdsEndSha(Weffcj, Rsh, DMCG, DMCI, DMDG,
                                              nuEndS, rgeo, 1, &Rend);
                else           Rend = Rsh * DMDG / (Weffcj * nuEndD);
                break;
            case 6:
                if (Type == 1) Rend = Rsh * DMDG / Weffcj;
                else           BSIM4v7RdsEndIso(Weffcj, Rsh, DMCG, DMCI, DMDG,
                                              nuEndD, rgeo, 0, &Rend);
                break;
            case 7:
                if (Type == 1) Rend = Rsh * DMDG / (Weffcj * nuEndS);
                else           BSIM4v7RdsEndSha(Weffcj, Rsh, DMCG, DMCI, DMDG,
                                              nuEndD, rgeo, 0, &Rend);
                break;
            case 8:
                Rend = Rsh * DMDG / Weffcj;	
                break;
            case 9: /* all wide contacts assumed for geo = 9 and 10 */
		if (Type == 1)
		{   Rend = 0.5 * Rsh * DMCG / Weffcj;
		    if (nf == 2.0)
		        Rint = 0.0;
		    else
		        Rint = Rsh * DMCG / (Weffcj * (nf - 2.0));
		}
		else
		{   Rend = 0.0;
                    Rint = Rsh * DMCG / (Weffcj * nf);
		}
                break;
            case 10:
                if (Type == 1)
                {   Rend = 0.0;
                    Rint = Rsh * DMCG / (Weffcj * nf);
                }
                else
                {   Rend = 0.5 * Rsh * DMCG / Weffcj;;
                    if (nf == 2.0)
                        Rint = 0.0;
                    else
                        Rint = Rsh * DMCG / (Weffcj * (nf - 2.0));
                }
                break;
            default:
                printf("Warning: Specified GEO = %d not matched\n", geo);
        }

	if (Rint <= 0.0)
	    *Rtot = Rend;
	else if (Rend <= 0.0)
	    *Rtot = Rint;
	else
	    *Rtot = Rint * Rend / (Rint + Rend);
if(*Rtot==0.0)
	printf("Warning: Zero resistance returned from RdseffGeo\n");
return 0;
}


int
BSIM4v7RdsEndIso(
double Weffcj, double Rsh, double DMCG, double DMCI, double DMDG,
double nuEnd,
int rgeo, int Type,
double *Rend)
{	
        NG_IGNORE(DMDG);

	if (Type == 1)
	{   switch(rgeo)
            {	case 1:
		case 2:
		case 5:
		    if (nuEnd == 0.0)
		        *Rend = 0.0;
		    else
                        *Rend = Rsh * DMCG / (Weffcj * nuEnd);
		    break;
                case 3:
                case 4:
                case 6:
		    if ((DMCG + DMCI) == 0.0)
                         printf("(DMCG + DMCI) can not be equal to zero\n");
                    if ((nuEnd == 0.0)||((DMCG+DMCI)==0.0))
                        *Rend = 0.0;
                    else
                        *Rend = Rsh * Weffcj / (3.0 * nuEnd * (DMCG + DMCI));
                    break;
		default:
		    printf("Warning: Specified RGEO = %d not matched\n", rgeo);
            }
	}
	else
	{  switch(rgeo)
            {   case 1:
                case 3:
                case 7:
                    if (nuEnd == 0.0)
                        *Rend = 0.0;
                    else
                        *Rend = Rsh * DMCG / (Weffcj * nuEnd);
                    break;
                case 2:
                case 4:
                case 8:
                    if ((DMCG + DMCI) == 0.0)
                         printf("(DMCG + DMCI) can not be equal to zero\n");
                    if ((nuEnd == 0.0)||((DMCG + DMCI)==0.0))
                        *Rend = 0.0;
                    else
                        *Rend = Rsh * Weffcj / (3.0 * nuEnd * (DMCG + DMCI));
                    break;
                default:
                    printf("Warning: Specified RGEO = %d not matched\n", rgeo);
            }
	}
return 0;
}


int
BSIM4v7RdsEndSha(
double Weffcj, double Rsh, double DMCG, double DMCI, double DMDG,
double nuEnd,
int rgeo, int Type,
double *Rend)
{
        NG_IGNORE(DMCI);
        NG_IGNORE(DMDG);

        if (Type == 1)
        {   switch(rgeo)
            {   case 1:
                case 2:
                case 5:
                    if (nuEnd == 0.0)
                        *Rend = 0.0;
                    else
                        *Rend = Rsh * DMCG / (Weffcj * nuEnd);
                    break;
                case 3:
                case 4:
                case 6:
                    if (DMCG == 0.0)
                        printf("DMCG can not be equal to zero\n");
                    if (nuEnd == 0.0)
                        *Rend = 0.0;
                    else
                        *Rend = Rsh * Weffcj / (6.0 * nuEnd * DMCG);
                    break;
                default:
                    printf("Warning: Specified RGEO = %d not matched\n", rgeo);
            }
        }
        else
        {  switch(rgeo)
            {   case 1:
                case 3:
                case 7:
                    if (nuEnd == 0.0)
                        *Rend = 0.0;
                    else
                        *Rend = Rsh * DMCG / (Weffcj * nuEnd);
                    break;
                case 2:
                case 4:
                case 8:
                    if (DMCG == 0.0)
                        printf("DMCG can not be equal to zero\n");
                    if (nuEnd == 0.0)
                        *Rend = 0.0;
                    else
                        *Rend = Rsh * Weffcj / (6.0 * nuEnd * DMCG);
                    break;
                default:
                    printf("Warning: Specified RGEO = %d not matched\n", rgeo);
            }
        }
return 0;
}

} // namespace neospice::bsim4v7
