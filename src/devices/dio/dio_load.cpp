/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
Modified: 2000 AlansFixes
Modified by Paolo Nenzi 2003 and Dietmar Warning 2012
**********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/dio/dio_def.hpp"
#include "devices/dio/dio_shim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "devices/ucb_compat.hpp"

namespace neospice::dio {

using namespace Shim;

int
DIOload(DIOModel *inModel, Shim::Ckt *ckt)
        /* actually load the current resistance value into the
         * sparse matrix previously provided
         */
{
    DIOModel *model = inModel;
    DIOInstance *here;
    double arg;
    double argsw;
    double capd;
    double cd, cdb, cdsw;
    double cdeq;
    double cdhat;
    double ceq;
    double csat;    /* area-scaled saturation current */
    double csatsw;  /* perimeter-scaled saturation current */
    double czero;
    double czof2;
    double argSW;
    double czeroSW;
    double czof2SW;
    double sargSW;
    double sqrt_ikr;
    double sqrt_ikf;
    double ikf_area_m;
    double ikr_area_m;

    double delvd;   /* change in diode voltage temporary */
    double evd;
    double evrev;
    double gd, gdb, gdsw;
    double geq;
    double gspr;    /* area-scaled conductance */
    double sarg;
#ifndef NOBYPASS
    double tol;     /* temporary for tolerence calculations */
#endif
    double vd;      /* current diode voltage */
    double vdtemp;
    double vt;      /* K t / Q */
    double vte, vtesw, vtetun;
    double vtebrk;
    double vterec;
    double evd_rec, cdb_rec, gdb_rec;
    double gen_fac, gen_fac_vd, t1;
    int Check;
    int error;
    double diffcharge, diffchargeSW, deplcharge, deplchargeSW, diffcap, diffcapSW, deplcap, deplcapSW;

    /*  loop through all the diode models */
    for( ; model != NULL; model = model->DIOnextModel ) {

        /* loop through all the instances of the model */
        for (here = model->DIOinstances; here != NULL ;
                here=here->DIOnextInstance) {

            /*
             *     this routine loads diodes for dc and transient analyses.
             */


            cd = 0.0;
            cdb = 0.0;
            cdsw = 0.0;
            gd = 0.0;
            gdb = 0.0;
            gdsw = 0.0;
            csat = here->DIOtSatCur;
            csatsw = here->DIOtSatSWCur;
            gspr = here->DIOtConductance * here->DIOarea;
            vt = CONSTKoverQ * here->DIOtemp;
            vte = model->DIOemissionCoeff * vt;
            vtebrk = model->DIObrkdEmissionCoeff * vt;
            vterec = model->DIOrecEmissionCoeff * vt;
            /*
             *   initialization
             */

            Check=1;
            if(ckt->CKTmode & MODEINITSMSIG) {
                vd= *(ckt->CKTstate0 + here->DIOvoltage);
            } else if (ckt->CKTmode & MODEINITTRAN) {
                vd= *(ckt->CKTstate1 + here->DIOvoltage);
            } else if ( (ckt->CKTmode & MODEINITJCT) &&
                    (ckt->CKTmode & MODETRANOP) && (ckt->CKTmode & MODEUIC) ) {
                vd=here->DIOinitCond;
            } else if ( (ckt->CKTmode & MODEINITJCT) && here->DIOoff) {
                vd=0;
            } else if ( ckt->CKTmode & MODEINITJCT) {
                vd=here->DIOtVcrit;
            } else if ( ckt->CKTmode & MODEINITFIX && here->DIOoff) {
                vd=0;
            } else {
#ifndef PREDICTOR
                if (ckt->CKTmode & MODEINITPRED) {
                    *(ckt->CKTstate0 + here->DIOvoltage) =
                            *(ckt->CKTstate1 + here->DIOvoltage);
                    vd = DEVpred(ckt,here->DIOvoltage);
                    *(ckt->CKTstate0 + here->DIOcurrent) =
                            *(ckt->CKTstate1 + here->DIOcurrent);
                    *(ckt->CKTstate0 + here->DIOconduct) =
                            *(ckt->CKTstate1 + here->DIOconduct);
                } else {
#endif /* PREDICTOR */
                    vd = *(ckt->CKTrhsOld+here->DIOposPrimeNode)-
                            *(ckt->CKTrhsOld + here->DIOnegNode);
#ifndef PREDICTOR
                }
#endif /* PREDICTOR */
                delvd=vd- *(ckt->CKTstate0 + here->DIOvoltage);
                cdhat= *(ckt->CKTstate0 + here->DIOcurrent) +
                        *(ckt->CKTstate0 + here->DIOconduct) * delvd;
                /*
                 *   bypass if solution has not changed
                 */
#ifndef NOBYPASS
                if ((!(ckt->CKTmode & MODEINITPRED)) && (ckt->CKTbypass)) {
                    tol=ckt->CKTvoltTol + ckt->CKTreltol*
                        MAX(fabs(vd),fabs(*(ckt->CKTstate0 +here->DIOvoltage)));
                    if (fabs(delvd) < tol){
                        tol=ckt->CKTreltol* MAX(fabs(cdhat),
                                fabs(*(ckt->CKTstate0 + here->DIOcurrent)))+
                                ckt->CKTabstol;
                        if (fabs(cdhat- *(ckt->CKTstate0 + here->DIOcurrent))
                                < tol) {
                            vd= *(ckt->CKTstate0 + here->DIOvoltage);
                            cd= *(ckt->CKTstate0 + here->DIOcurrent);
                            gd= *(ckt->CKTstate0 + here->DIOconduct);
                            goto load;
                        }
                    }
                }
#endif /* NOBYPASS */
                /*
                 *   limit new junction voltage
                 */
                if ( (model->DIObreakdownVoltageGiven) &&
                        (vd < MIN(0,-here->DIOtBrkdwnV+10*vtebrk))) {
                    vdtemp = -(vd+here->DIOtBrkdwnV);
                    vdtemp = DEVpnjlim(vdtemp,
                            -(*(ckt->CKTstate0 + here->DIOvoltage) +
                            here->DIOtBrkdwnV),vtebrk,
                            here->DIOtVcrit,&Check);
                    vd = -(vdtemp+here->DIOtBrkdwnV);
                } else {
                    vd = DEVpnjlim(vd,*(ckt->CKTstate0 + here->DIOvoltage),
                            vte,here->DIOtVcrit,&Check);
                }
            }
            /*
             *   compute dc current and derivitives
             */
            if (model->DIOsatSWCurGiven) {              /* sidewall current */

                if (model->DIOswEmissionCoeffGiven) {   /* current with own characteristic */

                    vtesw = model->DIOswEmissionCoeff * vt;

                    if (vd >= -3*vtesw) {               /* forward */

                        evd = exp(vd/vtesw);
                        cdsw = csatsw*(evd-1);
                        gdsw = csatsw*evd/vtesw;

                    } else if((!(model->DIObreakdownVoltageGiven)) ||
                            vd >= -here->DIOtBrkdwnV) { /* reverse */

                        argsw = 3*vtesw/(vd*CONSTe);
                        argsw = argsw * argsw * argsw;
                        cdsw = -csatsw*(1+argsw);
                        gdsw = csatsw*3*argsw/vd;

                    } else {                            /* breakdown */

                        evrev = exp(-(here->DIOtBrkdwnV+vd)/vtebrk);
                        cdsw = -csatsw*evrev;
                        gdsw = csatsw*evrev/vtebrk;

                    }

                } else { /* merge saturation currents and use same characteristic as bottom diode */

                    csat = csat + csatsw;

                }

            }

            if (vd >= -3*vte) {                 /* bottom current forward */

                evd = exp(vd/vte);
                cdb = csat*(evd-1);
                gdb = csat*evd/vte;
                if (model->DIOrecSatCurGiven) {
                    evd_rec = exp(vd/vterec);
                    cdb_rec = here->DIOtRecSatCur*(evd_rec-1);
                    gdb_rec = here->DIOtRecSatCur*evd_rec/vterec;
                    t1 = pow((1-vd/here->DIOtJctPot), 2) + 0.005;
                    gen_fac = pow(t1, here->DIOtGradingCoeff/2);
                    gen_fac_vd = -here->DIOtGradingCoeff * (1-vd/here->DIOtJctPot)
                                 / here->DIOtJctPot * pow(t1, (here->DIOtGradingCoeff/2-1));
                    cdb_rec = cdb_rec * gen_fac;
                    gdb_rec = gdb_rec * gen_fac + cdb_rec * gen_fac_vd;
                    cdb = cdb + cdb_rec;
                    gdb = gdb + gdb_rec;
                }

            } else if((!(model->DIObreakdownVoltageGiven)) ||
                    vd >= -here->DIOtBrkdwnV) { /* reverse */

                arg = 3*vte/(vd*CONSTe);
                arg = arg * arg * arg;
                cdb = -csat*(1+arg);
                gdb = csat*3*arg/vd;
                if (model->DIOrecSatCurGiven) {
                    evd_rec = exp((-3*vte)/vterec);
                    cdb_rec = here->DIOtRecSatCur*(evd_rec-1);
                    t1 = pow((1-(-3*vte)/here->DIOtJctPot), 2) + 0.005;
                    gen_fac = pow(t1, here->DIOtGradingCoeff/2);
                    cdb = cdb + gen_fac*cdb_rec;
                }

            } else {                            /* breakdown */

                evrev = exp(-(here->DIOtBrkdwnV+vd)/vtebrk);
                if (model->DIOrecSatCurGiven) {
                    evd_rec = exp((-3*vte)/vterec);
                    cdb_rec = here->DIOtRecSatCur*(evd_rec-1);
                    t1 = pow((1-(-3*vte)/here->DIOtJctPot), 2) + 0.005;
                    gen_fac = pow(t1, here->DIOtGradingCoeff/2);
                    cdb = -csat*evrev + gen_fac*cdb_rec;
                } else {
                    cdb = -csat*evrev;
                }
                gdb = csat*evrev/vtebrk;

            }

            if (model->DIOtunSatSWCurGiven) {    /* tunnel sidewall current */

                vtetun = model->DIOtunEmissionCoeff * vt;
                evd = exp(-vd/vtetun);

                cdsw = cdsw - here->DIOtTunSatSWCur * (evd - 1);
                gdsw = gdsw + here->DIOtTunSatSWCur * evd / vtetun;

            }

            if (model->DIOtunSatCurGiven) {      /* tunnel bottom current */

                vtetun = model->DIOtunEmissionCoeff * vt;
                evd = exp(-vd/vtetun);

                cdb = cdb - here->DIOtTunSatCur * (evd - 1);
                gdb = gdb + here->DIOtTunSatCur * evd / vtetun;

            }

            cd = cdb + cdsw;
            gd = gdb + gdsw;

            if (vd >= -3*vte) { /* limit forward */

                if( (model->DIOforwardKneeCurrent > 0.0) && (cd > 1.0e-18) ) {
                    ikf_area_m = here->DIOforwardKneeCurrent;
                    sqrt_ikf = sqrt(cd/ikf_area_m);
                    gd = ((1+sqrt_ikf)*gd - cd*gd/(2*sqrt_ikf*ikf_area_m))/(1+2*sqrt_ikf + cd/ikf_area_m) + ckt->CKTgmin;
                    cd = cd/(1+sqrt_ikf) + ckt->CKTgmin*vd;
                } else {
                    gd = gd + ckt->CKTgmin;
                    cd = cd + ckt->CKTgmin*vd;
                }

            } else {            /* limit reverse */

                if( (model->DIOreverseKneeCurrent > 0.0) && (cd < -1.0e-18) ) {
                    ikr_area_m = here->DIOreverseKneeCurrent;
                    sqrt_ikr = sqrt(cd/(-ikr_area_m));
                    gd = ((1+sqrt_ikr)*gd + cd*gd/(2*sqrt_ikr*ikr_area_m))/(1+2*sqrt_ikr - cd/ikr_area_m) + ckt->CKTgmin;
                    cd = cd/(1+sqrt_ikr) + ckt->CKTgmin*vd;
                } else {
                    gd = gd + ckt->CKTgmin;
                    cd = cd + ckt->CKTgmin*vd;
                }

            }

            if ((ckt->CKTmode & (MODETRAN | MODEAC | MODEINITSMSIG)) ||
                     ((ckt->CKTmode & MODETRANOP) && (ckt->CKTmode & MODEUIC))) {
              /*
               *   charge storage elements
               */
                czero=here->DIOtJctCap;
                if (vd < here->DIOtDepCap){
                    arg=1-vd/here->DIOtJctPot;
                    sarg=exp(-here->DIOtGradingCoeff*log(arg));
                    deplcharge = here->DIOtJctPot*czero*(1-arg*sarg)/(1-here->DIOtGradingCoeff);
                    deplcap = czero*sarg;
                } else {
                    czof2=czero/here->DIOtF2;
                    deplcharge = czero*here->DIOtF1+czof2*(here->DIOtF3*(vd-here->DIOtDepCap)+
                                 (here->DIOtGradingCoeff/(here->DIOtJctPot+here->DIOtJctPot))*(vd*vd-here->DIOtDepCap*here->DIOtDepCap));
                    deplcap = czof2*(here->DIOtF3+here->DIOtGradingCoeff*vd/here->DIOtJctPot);
                }
                czeroSW=here->DIOtJctSWCap;
                if (vd < here->DIOtDepSWCap){
                    argSW=1-vd/here->DIOtJctSWPot;
                    sargSW=exp(-model->DIOgradingSWCoeff*log(argSW));
                    deplchargeSW = here->DIOtJctSWPot*czeroSW*(1-argSW*sargSW)/(1-model->DIOgradingSWCoeff);
                    deplcapSW = czeroSW*sargSW;
                } else {
                    czof2SW=czeroSW/here->DIOtF2SW;
                    deplchargeSW = czeroSW*here->DIOtF1+czof2SW*(here->DIOtF3SW*(vd-here->DIOtDepSWCap)+
                                   (model->DIOgradingSWCoeff/(here->DIOtJctSWPot+here->DIOtJctSWPot))*(vd*vd-here->DIOtDepSWCap*here->DIOtDepSWCap));
                    deplcapSW = czof2SW*(here->DIOtF3SW+model->DIOgradingSWCoeff*vd/here->DIOtJctSWPot);
                }

                diffcharge = here->DIOtTransitTime*cdb;
                diffchargeSW = here->DIOtTransitTime*cdsw;
                *(ckt->CKTstate0 + here->DIOcapCharge) =
                        diffcharge + diffchargeSW + deplcharge + deplchargeSW;

                diffcap = here->DIOtTransitTime*gdb;
                diffcapSW = here->DIOtTransitTime*gdsw;
                capd = diffcap + diffcapSW + deplcap + deplcapSW;

                here->DIOcap = capd;

                /*
                 *   store small-signal parameters
                 */
                if( (!(ckt->CKTmode & MODETRANOP)) ||
                        (!(ckt->CKTmode & MODEUIC)) ) {
                    if (ckt->CKTmode & MODEINITSMSIG){
                        *(ckt->CKTstate0 + here->DIOcapCurrent) = capd;
                        continue;
                    }

                    if (ckt->CKTmode & MODEINITTRAN) {
                        *(ckt->CKTstate1 + here->DIOcapCharge) =
                                *(ckt->CKTstate0 + here->DIOcapCharge);
                    }
                    error = NIintegrate(ckt,&geq,&ceq,capd,here->DIOcapCharge);
                    if(error) return(error);
                    gd=gd+geq;
                    cd=cd+*(ckt->CKTstate0 + here->DIOcapCurrent);
                    if (ckt->CKTmode & MODEINITTRAN) {
                        *(ckt->CKTstate1 + here->DIOcapCurrent) =
                                *(ckt->CKTstate0 + here->DIOcapCurrent);
                    }
                }
            }

            /*
             *   check convergence
             */
            if ( (!(ckt->CKTmode & MODEINITFIX)) || (!(here->DIOoff))  ) {
                if (Check == 1)  {
                    ckt->CKTnoncon++;
                    ckt->CKTtroubleElt = (DIOInstance *) here;
                }
            }
            *(ckt->CKTstate0 + here->DIOvoltage) = vd;
            *(ckt->CKTstate0 + here->DIOcurrent) = cd;
            *(ckt->CKTstate0 + here->DIOconduct) = gd;

#ifndef NOBYPASS
            load:
#endif
            /*
             *   load current vector
             */
            cdeq=cd-gd*vd;
            *(ckt->CKTrhs + here->DIOnegNode) += cdeq;
            *(ckt->CKTrhs + here->DIOposPrimeNode) -= cdeq;
            /*
             *   load matrix
             */
            ckt->mat->add(here->DIOposPosPtr, gspr);
            ckt->mat->add(here->DIOnegNegPtr, gd);
            ckt->mat->add(here->DIOposPrimePosPrimePtr, (gd + gspr));
            ckt->mat->add(here->DIOposPosPrimePtr, -(gspr));
            ckt->mat->add(here->DIOnegPosPrimePtr, -(gd));
            ckt->mat->add(here->DIOposPrimePosPtr, -(gspr));
            ckt->mat->add(here->DIOposPrimeNegPtr, -(gd));
        }
    }
    return 0;
}

} // namespace neospice::dio
