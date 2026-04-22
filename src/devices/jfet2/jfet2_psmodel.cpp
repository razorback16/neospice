// jfet2_psmodel.cpp — Parker-Skellern MESFET model functions.
// Translated from ngspice psmodel.c to C++ for neospice.
//
// Copyright (C) 1994, 1995, 1996  Macquarie University
// All Rights Reserved
// Author: Anthony Parker

#include "devices/jfet2/jfet2_psmodel.hpp"
#include <cmath>

namespace neospice::jfet2 {

using namespace Shim;

// --- Simulator mode flags ---
#define TRAN_ANAL          (ckt->CKTmode & MODETRAN)
#define TRAN_INIT          (ckt->CKTmode & MODEINITTRAN)

// --- State variables ---
#define VGSTRAP_BEFORE     (*(ckt->CKTstate1 + here->JFET2vgstrap))
#define VGSTRAP_NOW        (*(ckt->CKTstate0 + here->JFET2vgstrap))
#define VGDTRAP_BEFORE     (*(ckt->CKTstate1 + here->JFET2vtrap))
#define VGDTRAP_NOW        (*(ckt->CKTstate0 + here->JFET2vtrap))
#define POWR_BEFORE        (*(ckt->CKTstate1 + here->JFET2pave))
#define POWR_NOW           (*(ckt->CKTstate0 + here->JFET2pave))

#define QGS_BEFORE         (*(ckt->CKTstate1 + here->JFET2qgs))
#define QGS_NOW            (*(ckt->CKTstate0 + here->JFET2qgs))
#define QGD_BEFORE         (*(ckt->CKTstate1 + here->JFET2qgd))
#define QGD_NOW            (*(ckt->CKTstate0 + here->JFET2qgd))

#define VGS1               (*(ckt->CKTstate1 + here->JFET2vgs))
#define VGD1               (*(ckt->CKTstate1 + here->JFET2vgd))

// --- Simulator parameters ---
#define GMIN    ckt->CKTgmin
#define NVT     here->JFET2temp*CONSTKoverQ*model->JFET2n
#define STEP    ckt->CKTdelta
#define FOURTH  0.25

// --- DC model parameters ---
#define BETA    model->JFET2beta
#define DELT    model->JFET2delta
#define IBD     model->JFET2ibd
#define IS      here->JFET2tSatCur
#define LAM     model->JFET2lambda
#define LFGAM   model->JFET2lfgam
#define LFG1    model->JFET2lfg1
#define LFG2    model->JFET2lfg2
#define MVST    model->JFET2mvst
#define MXI     model->JFET2mxi
#define P       model->JFET2p
#define Q       model->JFET2q
#define VBD     model->JFET2vbd
#define VBI     here->JFET2tGatePot
#define VSUB    model->JFET2vst
#define VTO     model->JFET2vto
#define XI      model->JFET2xi
#define Z       model->JFET2z

// --- AC model parameters ---
#define ACGAM   model->JFET2acgam
#define CGS     here->JFET2tCGS
#define CGD     here->JFET2tCGD
#define HFETA   model->JFET2hfeta
#define HFE1    model->JFET2hfe1
#define HFE2    model->JFET2hfe2
#define HFGAM   model->JFET2hfgam
#define HFG1    model->JFET2hfg1
#define HFG2    model->JFET2hfg2
#define TAUD    model->JFET2taud
#define TAUG    model->JFET2taug
#define XC      model->JFET2xc

// --- Device instance ---
#define AREA       here->JFET2area

// --- Internally derived model parameters ---
#define ALPHA   here->JFET2alpha
#define D3      here->JFET2d3
#define VMAX    here->JFET2corDepCap
#define XI_WOO  here->JFET2xiwoo
#define ZA      model->JFET2za

// =========================================================================
// PSids — DC current and conductance calculation
// =========================================================================
double
PSids(Shim::Ckt *ckt, JFET2Model *model, JFET2Instance *here,
      double vgs, double vgd,
      double *igs, double *igd, double *ggs, double *ggd,
      double *Gm, double *Gds)
{
#define FX  -10.0
#define MX  40.0
#define EMX 2.353852668370199842e17

   double idrain, arg;
   double area = AREA;

   { /* gate junction diodes */
      double zz;
      { /* gate-junction forward conduction */
         double Gmin   = GMIN;
         double Vt     = NVT;
         double isat   = IS   * area;
         if ((arg=vgs/Vt) > FX) {
            if(arg < MX) {
               *ggs=(zz=isat*exp(arg))/Vt+Gmin; *igs= zz -isat +Gmin*vgs;
            } else {
               *ggs=(zz=isat*EMX)/Vt+Gmin;     *igs=zz*(arg-MX+1)-isat+Gmin*vgs;
            }
         } else {
            *ggs = Gmin;                       *igs = -isat + Gmin * vgs;
         }
         if ((arg=vgd/Vt) > FX) {
            if(arg < MX) {
               *ggd=(zz=isat*exp(arg))/Vt+Gmin; *igd= zz -isat +Gmin*vgd;
            } else {
               *ggd=(zz=isat*EMX)/Vt+Gmin;     *igd=zz*(arg-MX+1)-isat+Gmin*vgd;
            }
         } else {
            *ggd = Gmin;                       *igd = -isat + Gmin * vgd;
         }
      }
      { /* gate-junction reverse 'breakdown' conduction */
         double Vbd    = VBD;
         double ibd    = IBD  * area;
         if ((arg=-vgs/Vbd) > FX) {
            if(arg < MX) {
               *ggs += (zz=ibd*exp(arg))/Vbd;  *igs -= zz-ibd;
            } else {
               *ggs += (zz=ibd*EMX)/Vbd;       *igs -= zz*((arg-MX)+1) - ibd;
            }
         } else                                *igs += ibd;
         if ((arg=-vgd/Vbd) > FX) {
            if(arg < MX) {
               *ggd += (zz=ibd*exp(arg))/Vbd;  *igd -= zz-ibd;
            } else {
               *ggd += (zz=ibd*EMX)/Vbd;       *igd -= zz*((arg-MX)+1) - ibd;
            }
         } else                                *igd += ibd;
      }
   }

   { /*  compute drain current and derivatives */
      double gm, gds;
      double vdst = vgs - vgd;
      double stepofour = STEP * FOURTH;
      { /* Include rate dependent threshold modulation */
         double vgst, dvgd, dvgs, h, vgdtrap, vgstrap, eta, gam;
         double vto = VTO;
         double LFg = LFGAM,   LFg1 = LFG1,  LFg2 = LFG2;
         double HFg = HFGAM,   HFg1 = HFG1,  HFg2 = HFG2;
         double HFe = HFETA,   HFe1 = HFE1,  HFe2 = HFE2;
         if(TRAN_ANAL) {
            double taug = TAUG;
            h = taug/(taug + stepofour);  h*=h; h*=h; /*4th power*/
            VGDTRAP_NOW = vgdtrap = h*VGDTRAP_BEFORE + (1-h) * vgd;
            VGSTRAP_NOW = vgstrap = h*VGSTRAP_BEFORE + (1-h) * vgs;
         } else {
            h = 0;
            VGDTRAP_NOW = vgdtrap = vgd;
            VGSTRAP_NOW = vgstrap = vgs;
         }
         vgst = vgs - vto;
         vgst -= (      LFg - LFg1*vgstrap + LFg2*vgdtrap)*vgdtrap;
         vgst += (eta = HFe - HFe1*vgdtrap + HFe2*vgstrap)*(dvgs = vgstrap-vgs);
         vgst += (gam = HFg - HFg1*vgstrap + HFg2*vgdtrap)*(dvgd = vgdtrap-vgd);
         { /* Exponential Subthreshold effect ids(vgst,vdst) */
            double vgt, subfac;
            double mvst = MVST;
            double vst = VSUB * (1 + mvst*vdst);
            if (vgst > FX*vst) {
               if (vgst > (arg=MX*vst)) { /* numerically large */
                  vgt = (EMX/(subfac = EMX+1))*(vgst-arg) + arg;
               } else  /* limit gate bias exponentially */
                  vgt = vst * log( subfac=(1 + exp(vgst/vst)) );
               { /* Dual Power-law ids(vgt,vdst) */
                  double mQ  = Q;
                  double PmQ = P - mQ;
                  double dvpd_dvdst=(double)D3*pow(vgt,PmQ);
                  double vdp = vdst*dvpd_dvdst; /*D3=P/Q/((VBI-vto)^PmQ)*/
                  { /* Early saturation effect ids(vgt,vdp) */
                     double za  = (double)ZA; /* sqrt(1 + Z)/2 */
                     double mxi = MXI;
                     double vsatFac = vgt/(mxi*vgt  + (double)XI_WOO);
                     double vsat=vgt/(1 + vsatFac);
                     double  aa = za*vdp+vsat/2.0;
                     double a_aa = aa-vsat;
                     double  rpt = sqrt( aa * aa + (arg=vsat*vsat*Z/4.0));
                     double a_rpt = sqrt(a_aa * a_aa + arg);
                     double vdt = (rpt - a_rpt);
                     double dvdt_dvdp = za * (aa/rpt - a_aa/a_rpt);
                     double dvdt_dvgt = (vdt - vdp*dvdt_dvdp)
                           *(1 + mxi*vsatFac*vsatFac)/(1 + vsatFac)/vgt;
                     { /* Intrinsic Q-law FET equation ids(vgt,vdt) */
                        gds=pow(vgt-vdt,mQ-1);
                        idrain = vdt*gds + vgt*(gm=pow(vgt,mQ-1)-gds);
                        gds *= mQ;
                        gm *= mQ;
                     }
                     gm += gds*dvdt_dvgt;
                     gds *= dvdt_dvdp;
                  }
                  gm += gds*PmQ*vdp/vgt;
                  gds *= dvpd_dvdst;
               }
               arg = 1 - 1/subfac;
               if(vst != 0) gds += gm*VSUB*mvst*(vgt-vgst*arg)/vst;
               gm *= arg;
            } else { /* in extreme cut-off (numerically) */
               idrain = gm = gds = 0.0e0;
            }
         }
         gds += gm*(arg = h*gam +
                   (1-h)*(HFe1*dvgs-HFg2*dvgd+2*LFg2*vgdtrap-LFg1*vgstrap+LFg));
         gm *= 1 - h*eta + (1-h)*(HFe2*dvgs -HFg1*dvgd + LFg1*vgdtrap) - arg;
      }
      { /* apply channel length modulation and beta scaling */
         double lambda = LAM;
         double beta   = BETA  * area;
         gm *= (arg = beta*(1 + lambda*vdst));
         gds = beta*lambda*idrain + gds*arg;
         idrain *= arg;
      }

      { /* apply thermal reduction of drain current */
        double h, pfac, pAverage;
        double delta = DELT / area;
        if(TRAN_ANAL) {
           double taud = TAUD;
           h = taud/(taud + stepofour);    h*=h; h*=h;
           POWR_NOW = pAverage = h*POWR_BEFORE + (1-h)*vdst*idrain;
        } else {
           POWR_NOW = POWR_BEFORE = pAverage = vdst*idrain;  h = 0;
        }
        idrain /= (pfac = 1+pAverage*delta);
        *Gm  = gm * (arg = (h*delta*POWR_BEFORE + 1)/pfac/pfac);
        *Gds = gds * arg - (1-h) * delta*idrain*idrain;
      }
   }
   return(idrain);

#undef FX
#undef MX
#undef EMX
}

// =========================================================================
// qgg — static helper for gate charge calculation
// =========================================================================
static double
qgg(double vgs, double vgd, double gamma, double pb, double alpha,
    double vto, double vmax, double xc, double cgso, double cgdo,
    double *cgs, double *cgd)
{
   double qrt, ext, Cgso, cpm, cplus, cminus;
   double vds   = vgs-vgd;
   double d1_xc = 1-xc;
   double vert  = sqrt( vds * vds + alpha );
   double veff  = 0.5*(vgs + vgd + vert) + gamma*vds;
   double vnr   = d1_xc*(veff-vto);
   double vnrt  = sqrt( vnr*vnr + 0.04 );
   double vnew  = veff + 0.5*(vnrt - vnr);
   if ( vnew < vmax ) {
      ext  = 0;
      qrt  = sqrt(1 - vnew/pb);
      Cgso = 0.5*cgso/qrt*(1+xc + d1_xc*vnr/vnrt);
   } else {
      double vx  = 0.5*(vnew-vmax);
      double par = 1+vx/(pb-vmax);
      qrt  = sqrt(1 - vmax/pb);
      ext  = vx*(1 + par)/qrt;
      Cgso = 0.5*cgso/qrt*(1+xc + d1_xc*vnr/vnrt) * par;
   }
   cplus = 0.5*(1 + (cpm = vds/vert)); cminus = cplus - cpm;
   *cgs = Cgso*(cplus +gamma) + cgdo*(cminus+gamma);
   *cgd = Cgso*(cminus-gamma) + cgdo*(cplus -gamma);
   return(cgso*((pb+pb)*(1-qrt) + ext) + cgdo*(veff - vert));
}

// =========================================================================
// PScharge — charge calculation for transient/AC
// =========================================================================
void
PScharge(Shim::Ckt *ckt, JFET2Model *model, JFET2Instance *here,
         double vgs, double vgd, double *capgs, double *capgd)
{
#define QGG(a,b,c,d) qgg(a,b,gac,phib,alpha,vto,vmax,xc,czgs,czgd,c,d)

   double czgs = CGS * AREA;
   double czgd = CGD * AREA;
   double vto   = VTO;
   double alpha = (double)ALPHA;
   double xc    = XC;
   double vmax  = VMAX;
   double phib  = VBI;
   double gac   = ACGAM;

   if(!TRAN_ANAL)
       QGS_NOW = QGD_NOW = QGS_BEFORE = QGD_BEFORE
          = QGG(vgs,vgd,capgs,capgd);
   else {
      double cgsna,cgsnc;
      double cgdna,cgdnb, a_cap;
      double vgs1  = VGS1;
      double vgd1  = VGD1;
      double qgga=QGG(vgs ,vgd ,&cgsna,&cgdna);
      double qggb=QGG(vgs1,vgd ,&a_cap,&cgdnb);
      double qggc=QGG(vgs ,vgd1,&cgsnc,&a_cap);
      double qggd=QGG(vgs1,vgd1,&a_cap,&a_cap);
      QGS_NOW = QGS_BEFORE + 0.5 * (qgga-qggb + qggc-qggd);
      QGD_NOW = QGD_BEFORE + 0.5 * (qgga-qggc + qggb-qggd);
      *capgs = 0.5 * (cgsna + cgsnc);
      *capgd = 0.5 * (cgdna + cgdnb);
   }

#undef QGG
}

// =========================================================================
// PSacload — small-signal AC model
// =========================================================================
void
PSacload(Shim::Ckt *ckt, JFET2Model *model, JFET2Instance *here,
         double vgs, double vgd, double ids, double omega,
         double *Gm, double *xGm, double *Gds, double *xGds)
{
    (void)ckt;

    double arg;
    double vds = vgs - vgd;
    double LFgam = LFGAM;
    double LFg1 = LFG1;
    double LFg2 = LFG2*vgd;
    double HFg1 = HFG1;
    double HFg2 = HFG2*vgd;
    double HFeta  = HFETA;
    double HFe1 = HFE1;
    double HFe2 = HFE2*vgs;
    double hfgam= HFGAM - HFg1*vgs + HFg2;
    double eta  = HFeta - HFe1*vgd + HFe2;
    double lfga = LFgam - LFg1*vgs + LFg2 + LFg2;
    double gmo  = *Gm/(1 - lfga + LFg1*vgd);

    double wtg = TAUG * omega;
    double wtgdet = 1 + wtg*wtg;
    double gwtgdet = gmo/wtgdet;

    double gdsi = (arg=hfgam - lfga)*gwtgdet;
    double gdsr = arg*gmo - gdsi;
    double gmi  = (eta + LFg1*vgd)*gwtgdet + gdsi;

    double xgds = wtg*gdsi;
    double  gds = *Gds + gdsr;
    double  xgm = -wtg*gmi;
    double   gm = gmi + gmo*(1 -eta - hfgam);

    double delta = DELT / AREA;
    double wtd = TAUD * omega ;
    double wtddet = 1 + wtd * wtd;
    double fac = delta * ids;
    double del = 1/(1 - fac * vds);
    double dd = (del-1) / wtddet;
    double dr = del - dd;
    double di = wtd * dd;

    double cdsqr = fac * ids * del * wtd/wtddet;

    *Gm   = dr*gm  - di*xgm;
    *xGm  = di*gm  + dr*xgm;

    *Gds  = dr*gds - di*xgds + cdsqr*wtd;
    *xGds = di*gds + dr*xgds + cdsqr;
}

// =========================================================================
// PSinstanceinit — initialize derived instance parameters
// =========================================================================
void
PSinstanceinit(JFET2Model *model, JFET2Instance *here)
{
    double woo = (VBI - VTO);
    XI_WOO = (double)(XI * woo);
    ZA     = (double)(sqrt(1 + Z)/2);
    ALPHA  = (double)(XI_WOO*XI_WOO/(XI+1)/(XI+1)/ 4);
    D3     = (double)(P/Q/pow(woo,(P - Q)));
}

// Undefine all psmodel macros to avoid leaking into other TUs
#undef TRAN_ANAL
#undef TRAN_INIT
#undef VGSTRAP_BEFORE
#undef VGSTRAP_NOW
#undef VGDTRAP_BEFORE
#undef VGDTRAP_NOW
#undef POWR_BEFORE
#undef POWR_NOW
#undef QGS_BEFORE
#undef QGS_NOW
#undef QGD_BEFORE
#undef QGD_NOW
#undef VGS1
#undef VGD1
#undef GMIN
#undef NVT
#undef STEP
#undef FOURTH
#undef BETA
#undef DELT
#undef IBD
#undef IS
#undef LAM
#undef LFGAM
#undef LFG1
#undef LFG2
#undef MVST
#undef MXI
#undef P
#undef Q
#undef VBD
#undef VBI
#undef VSUB
#undef VTO
#undef XI
#undef Z
#undef ACGAM
#undef CGS
#undef CGD
#undef HFETA
#undef HFE1
#undef HFE2
#undef HFGAM
#undef HFG1
#undef HFG2
#undef TAUD
#undef TAUG
#undef XC
#undef AREA
#undef ALPHA
#undef D3
#undef VMAX
#undef XI_WOO
#undef ZA

} // namespace neospice::jfet2
