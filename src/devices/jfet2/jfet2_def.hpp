#pragma once

#include "core/matrix.hpp"

#ifndef NSTATVARS
#define NSTATVARS 3
#endif

namespace neospice::jfet2 {

namespace Shim { struct Ckt; class Matrix; }

struct JFET2Model;

/**********
Based on jfetdefs.h
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles

Modified to add PS model and new parameter definitions ( Anthony E. Parker )
   Copyright 1994  Macquarie University, Sydney Australia.
   10 Feb 1994: Added xiwoo, d3 and alpha to JFET2Instance
                JFET2pave, JFET2vtrap ad JFET2_STATE_COUNT
                Changed model to call jfetparm.h, added JFET2za to model struct
                Defined JFET2_VTRAP and JFET2_PAVE
**********/

    /* structures used to describe Junction Field Effect Transistors */


/* information used to describe a single instance */

struct JFET2Instance {
    JFET2Model *JFET2modPtr;  /* backpointer to model */
    JFET2Instance *JFET2nextInstance; /* pointer to next instance of 
                                             * current model*/
    const char *JFET2name; /* pointer to character string naming this instance */
    int JFET2state; /* pointer to start of state vector for jfet */
    int JFET2drainNode;  /* number of drain node of jfet */
    int JFET2gateNode;   /* number of gate node of jfet */
    int JFET2sourceNode; /* number of source node of jfet */
    int JFET2drainPrimeNode; /* number of internal drain node of jfet */
    int JFET2sourcePrimeNode;    /* number of internal source node of jfet */

    neospice::MatrixOffset JFET2drainDrainPrimePtr{-1}; /* pointer to sparse matrix at 
                                     * (drain,drain prime) */
    neospice::MatrixOffset JFET2gateDrainPrimePtr{-1};  /* pointer to sparse matrix at 
                                     * (gate,drain prime) */
    neospice::MatrixOffset JFET2gateSourcePrimePtr{-1}; /* pointer to sparse matrix at 
                                     * (gate,source prime) */
    neospice::MatrixOffset JFET2sourceSourcePrimePtr{-1};   /* pointer to sparse matrix at 
                                         * (source,source prime) */
    neospice::MatrixOffset JFET2drainPrimeDrainPtr{-1}; /* pointer to sparse matrix at 
                                     * (drain prime,drain) */
    neospice::MatrixOffset JFET2drainPrimeGatePtr{-1};  /* pointer to sparse matrix at 
                                     * (drain prime,gate) */
    neospice::MatrixOffset JFET2drainPrimeSourcePrimePtr{-1};   /* pointer to sparse matrix
                                             * (drain prime,source prime) */
    neospice::MatrixOffset JFET2sourcePrimeGatePtr{-1}; /* pointer to sparse matrix at 
                                     * (source prime,gate) */
    neospice::MatrixOffset JFET2sourcePrimeSourcePtr{-1};   /* pointer to sparse matrix at 
                                         * (source prime,source) */
    neospice::MatrixOffset JFET2sourcePrimeDrainPrimePtr{-1};   /* pointer to sparse matrix
                                             * (source prime,drain prime) */
    neospice::MatrixOffset JFET2drainDrainPtr{-1};  /* pointer to sparse matrix at 
                                 * (drain,drain) */
    neospice::MatrixOffset JFET2gateGatePtr{-1};    /* pointer to sparse matrix at 
                                 * (gate,gate) */
    neospice::MatrixOffset JFET2sourceSourcePtr{-1};    /* pointer to sparse matrix at 
                                     * (source,source) */
    neospice::MatrixOffset JFET2drainPrimeDrainPrimePtr{-1};    /* pointer to sparse matrix
                                             * (drain prime,drain prime) */
    neospice::MatrixOffset JFET2sourcePrimeSourcePrimePtr{-1};  /* pointer to sparse matrix
                                             * (source prime,source prime) */

	int JFET2mode;
	/* distortion analysis Taylor coeffs. */

/*
 * naming convention:
 * x = vgs
 * y = vds
 * cdr = cdrain
 */

#define JFET2NDCOEFFS	21

#ifndef NODISTO
	double JFET2dCoeffs[JFET2NDCOEFFS];
#else /* NODISTO */
	double *JFET2dCoeffs;
#endif /* NODISTO */

#ifndef CONFIG

#define	cdr_x		JFET2dCoeffs[0]
#define	cdr_y		JFET2dCoeffs[1]
#define	cdr_x2		JFET2dCoeffs[2]
#define	cdr_y2		JFET2dCoeffs[3]
#define	cdr_xy		JFET2dCoeffs[4]
#define	cdr_x3		JFET2dCoeffs[5]
#define	cdr_y3		JFET2dCoeffs[6]
#define	cdr_x2y		JFET2dCoeffs[7]
#define	cdr_xy2		JFET2dCoeffs[8]

#define	ggs1		JFET2dCoeffs[9]
#define	ggd1		JFET2dCoeffs[10]
#define	ggs2		JFET2dCoeffs[11]
#define	ggd2		JFET2dCoeffs[12]
#define	ggs3		JFET2dCoeffs[13]
#define	ggd3		JFET2dCoeffs[14]
#define	capgs1		JFET2dCoeffs[15]
#define	capgd1		JFET2dCoeffs[16]
#define	capgs2		JFET2dCoeffs[17]
#define	capgd2		JFET2dCoeffs[18]
#define	capgs3		JFET2dCoeffs[19]
#define	capgd3		JFET2dCoeffs[20]

#endif

/* indices to an array of JFET2 noise sources */

#define JFET2RDNOIZ       0
#define JFET2RSNOIZ       1
#define JFET2IDNOIZ       2
#define JFET2FLNOIZ 3
#define JFET2TOTNOIZ    4

#define JFET2NSRCS     5

#ifndef NONOISE
    double JFET2nVar[NSTATVARS][JFET2NSRCS];
#else /* NONOISE */
	double **JFET2nVar;
#endif /* NONOISE */

    unsigned JFET2off :1;            /* 'off' flag for jfet */
    unsigned JFET2areaGiven  : 1;    /* flag to indicate area was specified */
    unsigned JFET2mGiven     : 1;    /* flag to indicate multiplier given */
    unsigned JFET2icVDSGiven : 1;    /* initial condition given flag for V D-S*/
    unsigned JFET2icVGSGiven : 1;    /* initial condition given flag for V G-S*/
    unsigned JFET2tempGiven  : 1;    /* flag to indicate instance temp given */
    unsigned JFET2dtempGiven : 1;    /* flag to indicate temperature difference given */


    double JFET2area;    /* area factor for the jfet */
    double JFET2m;       /* parallel multiplier for the diode */
    double JFET2icVDS;   /* initial condition voltage D-S*/
    double JFET2icVGS;   /* initial condition voltage G-S*/
    double JFET2temp;    /* operating temperature */
    double JFET2dtemp;   /* Instance temperature difference */
    double JFET2tSatCur; /* temperature adjusted saturation current */
    double JFET2tGatePot;    /* temperature adjusted gate potential */
    double JFET2tCGS;    /* temperature corrected G-S capacitance */
    double JFET2tCGD;    /* temperature corrected G-D capacitance */
    double JFET2corDepCap;   /* joining point of the fwd bias dep. cap eq.s */
    double JFET2vcrit;   /* critical voltage for the instance */
    double JFET2f1;      /* coefficient of capacitance polynomial exp */
    double JFET2xiwoo;       /* velocity saturation potential */
    double JFET2d3;          /* Dual Power-law parameter */
    double JFET2alpha;       /* capacitance model transition parameter */

};

#define JFET2vgs      JFET2state 
#define JFET2vgd      JFET2state+1 
#define JFET2cg       JFET2state+2 
#define JFET2cd       JFET2state+3 
#define JFET2cgd      JFET2state+4 
#define JFET2gm       JFET2state+5 
#define JFET2gds      JFET2state+6 
#define JFET2ggs      JFET2state+7 
#define JFET2ggd      JFET2state+8 
#define JFET2qgs      JFET2state+9 
#define JFET2cqgs     JFET2state+10 
#define JFET2qgd      JFET2state+11 
#define JFET2cqgd     JFET2state+12 
#define JFET2qds      JFET2state+13
#define JFET2cqds     JFET2state+14
#define JFET2pave     JFET2state+15
#define JFET2vtrap    JFET2state+16
#define JFET2vgstrap  JFET2state+17
#define JFET2_STATE_COUNT    18

/* per model data */

struct JFET2Model {       /* model structure for a jfet */
    int JFET2modType;    /* type index of this device type */
    JFET2Model *JFET2nextModel;   /* pointer to next possible model in 
                                         * linked list */
    JFET2Instance * JFET2instances; /* pointer to list of instances 
                                   * that have this model */
    const char *JFET2modName; /* pointer to character string naming this model */

    /* --- end of generic struct JFET2Model --- */

    int JFET2type;

    // --- Model parameters (from jfet2parm.h PARAM expansion) ---
    double JFET2acgam;
    double JFET2fNexp;
    double JFET2beta;
    double JFET2capds;
    double JFET2capgd;
    double JFET2capgs;
    double JFET2delta;
    double JFET2hfeta;
    double JFET2hfe1;
    double JFET2hfe2;
    double JFET2hfg1;
    double JFET2hfg2;
    double JFET2mvst;
    double JFET2mxi;
    double JFET2fc;
    double JFET2ibd;
    double JFET2is;
    double JFET2fNcoef;
    double JFET2lambda;
    double JFET2lfgam;
    double JFET2lfg1;
    double JFET2lfg2;
    double JFET2n;
    double JFET2p;
    double JFET2phi;
    double JFET2q;
    double JFET2rd;
    double JFET2rs;
    double JFET2taud;
    double JFET2taug;
    double JFET2vbd;
    double JFET2ver;
    double JFET2vst;
    double JFET2vto;
    double JFET2xc;
    double JFET2xi;
    double JFET2z;
    double JFET2hfgam;

    // --- Derived model parameters ---
    double JFET2drainConduct;
    double JFET2sourceConduct;
    double JFET2f2;
    double JFET2f3;
    double JFET2za;      /* saturation index parameter */
    double JFET2tnom;    /* temperature at which parameters were measured */

    // --- Given flags (from jfet2parm.h PARAM expansion) ---
    unsigned JFET2acgamGiven : 1;
    unsigned JFET2fNexpGiven : 1;
    unsigned JFET2betaGiven : 1;
    unsigned JFET2capDSGiven : 1;
    unsigned JFET2capGDGiven : 1;
    unsigned JFET2capGSGiven : 1;
    unsigned JFET2deltaGiven : 1;
    unsigned JFET2hfetaGiven : 1;
    unsigned JFET2hfe1Given : 1;
    unsigned JFET2hfe2Given : 1;
    unsigned JFET2hfg1Given : 1;
    unsigned JFET2hfg2Given : 1;
    unsigned JFET2mvstGiven : 1;
    unsigned JFET2mxiGiven : 1;
    unsigned JFET2fcGiven : 1;
    unsigned JFET2ibdGiven : 1;
    unsigned JFET2isGiven : 1;
    unsigned JFET2kfGiven : 1;
    unsigned JFET2lamGiven : 1;
    unsigned JFET2lfgamGiven : 1;
    unsigned JFET2lfg1Given : 1;
    unsigned JFET2lfg2Given : 1;
    unsigned JFET2nGiven : 1;
    unsigned JFET2pGiven : 1;
    unsigned JFET2phiGiven : 1;
    unsigned JFET2qGiven : 1;
    unsigned JFET2rdGiven : 1;
    unsigned JFET2rsGiven : 1;
    unsigned JFET2taudGiven : 1;
    unsigned JFET2taugGiven : 1;
    unsigned JFET2vbdGiven : 1;
    unsigned JFET2verGiven : 1;
    unsigned JFET2vstGiven : 1;
    unsigned JFET2vtoGiven : 1;
    unsigned JFET2xcGiven : 1;
    unsigned JFET2xiGiven : 1;
    unsigned JFET2zGiven : 1;
    unsigned JFET2hfgGiven : 1;
    unsigned JFET2tnomGiven : 1; /* user specified Tnom for model */

};

#ifndef NJF

#define NJF 1
#define PJF -1

#endif /*NJF*/

/* model parameter IDs (from jfet2parm.h) */
#define JFET2_MOD_VTO 141
#define JFET2_MOD_NJF 102
#define JFET2_MOD_PJF 103
#define JFET2_MOD_TNOM 104
#define JFET2_MOD_PB 131

/* device parameters */
#define JFET2_AREA 1
#define JFET2_IC_VDS 2
#define JFET2_IC_VGS 3
#define JFET2_IC 4
#define JFET2_OFF 5
#define JFET2_TEMP 6
#define JFET2_DTEMP 7
#define JFET2_M 8

/* device questions */
#define JFET2_DRAINNODE        301
#define JFET2_GATENODE         302
#define JFET2_SOURCENODE       303
#define JFET2_DRAINPRIMENODE   304
#define JFET2_SOURCEPRIMENODE  305
#define JFET2_VGS              306
#define JFET2_VGD              307
#define JFET2_CG               308
#define JFET2_CD               309
#define JFET2_CGD              310
#define JFET2_GM               311
#define JFET2_GDS              312
#define JFET2_GGS              313
#define JFET2_GGD              314
#define JFET2_QGS              315
#define JFET2_CQGS             316
#define JFET2_QGD              317
#define JFET2_CQGD             318
#define JFET2_CS               319
#define JFET2_POWER            320
#define JFET2_VTRAP            321
#define JFET2_PAVE             322

/* model questions */
#define JFET2_MOD_DRAINCONDUCT   301
#define JFET2_MOD_SOURCECONDUCT  302
#define JFET2_MOD_DEPLETIONCAP   303
#define JFET2_MOD_VCRIT          304
#define JFET2_MOD_TYPE           305

/* function definitions */

// --- Parameter tables and dispatchers (defined in devsup/mpar) ------
namespace Shim { struct IfParm; struct IfValue; }
extern Shim::IfParm JFET2pTable[];
extern int JFET2pTSize;
extern Shim::IfParm JFET2mPTable[];
extern int JFET2mPTSize;
int JFET2mParam(int param, Shim::IfValue *value, JFET2Model *model);
int JFET2param(int param, Shim::IfValue *value, JFET2Instance *inst, Shim::IfValue *select);

// --- UCB entry points (defined in setup/temp/load .cpp) ------
int JFET2setup(Shim::Matrix*, JFET2Model*, Shim::Ckt*, int*);
int JFET2temp(JFET2Model*, Shim::Ckt*);
int JFET2load(JFET2Model*, Shim::Ckt*);

} // namespace neospice::jfet2
