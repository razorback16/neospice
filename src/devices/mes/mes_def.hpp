#pragma once

#include "core/matrix.hpp"

#ifndef NSTATVARS
#define NSTATVARS 3
#endif

namespace neospice::mes {

namespace Shim { struct Ckt; class Matrix; }

struct MESModel;

/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 S. Hwang
**********/

#define MESnumStates	13

    /* structures used to describe MESFET Transistors */


/* information used to describe a single instance */

struct MESInstance {
    MESModel *MESmodPtr;    /* backpointer to model */
    MESInstance *MESnextInstance; /* pointer to next instance of 
                                             * current model*/
    const char *MESname; /* pointer to character string naming this instance */
    int MESstate; /* pointer to start of state vector for mesfet */
    int MESdrainNode;  /* number of drain node of mesfet */
    int MESgateNode;   /* number of gate node of mesfet */
    int MESsourceNode; /* number of source node of mesfet */
    int MESdrainPrimeNode; /* number of internal drain node of mesfet */
    int MESsourcePrimeNode;    /* number of internal source node of mesfet */

    double MESarea;    /* area factor for the mesfet */
    double MESm;       /* Parallel multiplier */
    double MESicVDS;   /* initial condition voltage D-S*/
    double MESicVGS;   /* initial condition voltage G-S*/
    neospice::MatrixOffset MESdrainDrainPrimePtr{-1}; /* pointer to sparse matrix at 
                                     * (drain,drain prime) */
    neospice::MatrixOffset MESgateDrainPrimePtr{-1};  /* pointer to sparse matrix at 
                                     * (gate,drain prime) */
    neospice::MatrixOffset MESgateSourcePrimePtr{-1}; /* pointer to sparse matrix at 
                                     * (gate,source prime) */
    neospice::MatrixOffset MESsourceSourcePrimePtr{-1};   /* pointer to sparse matrix at 
                                         * (source,source prime) */
    neospice::MatrixOffset MESdrainPrimeDrainPtr{-1}; /* pointer to sparse matrix at 
                                     * (drain prime,drain) */
    neospice::MatrixOffset MESdrainPrimeGatePtr{-1};  /* pointer to sparse matrix at 
                                     * (drain prime,gate) */
    neospice::MatrixOffset MESdrainPrimeSourcePrimePtr{-1};   /* pointer to sparse matrix
                                             * (drain prime,source prime) */
    neospice::MatrixOffset MESsourcePrimeGatePtr{-1}; /* pointer to sparse matrix at 
                                     * (source prime,gate) */
    neospice::MatrixOffset MESsourcePrimeSourcePtr{-1};   /* pointer to sparse matrix at 
                                         * (source prime,source) */
    neospice::MatrixOffset MESsourcePrimeDrainPrimePtr{-1};   /* pointer to sparse matrix
                                             * (source prime,drain prime) */
    neospice::MatrixOffset MESdrainDrainPtr{-1};  /* pointer to sparse matrix at 
                                 * (drain,drain) */
    neospice::MatrixOffset MESgateGatePtr{-1};    /* pointer to sparse matrix at 
                                 * (gate,gate) */
    neospice::MatrixOffset MESsourceSourcePtr{-1};    /* pointer to sparse matrix at 
                                     * (source,source) */
    neospice::MatrixOffset MESdrainPrimeDrainPrimePtr{-1};    /* pointer to sparse matrix
                                             * (drain prime,drain prime) */
    neospice::MatrixOffset MESsourcePrimeSourcePrimePtr{-1};  /* pointer to sparse matrix
                                             * (source prime,source prime) */

    int MESoff;   /* 'off' flag for mesfet */
    unsigned MESareaGiven  : 1;   /* flag to indicate area was specified */
    unsigned MESmGiven     : 1;   /* flag to indicate multiplier specified*/
    unsigned MESicVDSGiven : 1;   /* initial condition given flag for V D-S*/
    unsigned MESicVGSGiven : 1;   /* initial condition given flag for V G-S*/

int MESmode;
	
/*
 * naming convention:
 * x = vgs
 * y = vgd
 * z = vds
 * cdr = cdrain
 */

#define MESNDCOEFFS	27

#ifndef NODISTO
	double MESdCoeffs[MESNDCOEFFS];
#else /* NODISTO */
	double *MESdCoeffs;
#endif /* NODISTO */

#ifndef CONFIG

#undef	cdr_x
#define	cdr_x		MESdCoeffs[0]
#undef	cdr_z
#define	cdr_z		MESdCoeffs[1]
#undef	cdr_x2
#define	cdr_x2		MESdCoeffs[2]
#undef	cdr_z2
#define	cdr_z2		MESdCoeffs[3]
#undef	cdr_xz
#define	cdr_xz		MESdCoeffs[4]
#undef	cdr_x3
#define	cdr_x3		MESdCoeffs[5]
#undef	cdr_z3
#define	cdr_z3		MESdCoeffs[6]
#undef	cdr_x2z
#define	cdr_x2z		MESdCoeffs[7]
#undef	cdr_xz2
#define	cdr_xz2		MESdCoeffs[8]

#undef	ggs3
#define	ggs3		MESdCoeffs[9]
#undef	ggd3
#define	ggd3		MESdCoeffs[10]
#undef	ggs2
#define	ggs2		MESdCoeffs[11]
#undef	ggd2
#define	ggd2		MESdCoeffs[12]

#undef	qgs_x2
#define	qgs_x2		MESdCoeffs[13]
#undef	qgs_y2
#define	qgs_y2		MESdCoeffs[14]
#undef	qgs_xy
#define	qgs_xy		MESdCoeffs[15]
#undef	qgs_x3
#define	qgs_x3		MESdCoeffs[16]
#undef	qgs_y3
#define	qgs_y3		MESdCoeffs[17]
#undef	qgs_x2y
#define	qgs_x2y		MESdCoeffs[18]
#undef	qgs_xy2
#define	qgs_xy2		MESdCoeffs[19]

#undef	qgd_x2
#define	qgd_x2		MESdCoeffs[20]
#undef	qgd_y2
#define	qgd_y2		MESdCoeffs[21]
#undef	qgd_xy
#define	qgd_xy		MESdCoeffs[22]
#undef	qgd_x3
#define	qgd_x3		MESdCoeffs[23]
#undef	qgd_y3
#define	qgd_y3		MESdCoeffs[24]
#undef	qgd_x2y
#define	qgd_x2y		MESdCoeffs[25]
#undef	qgd_xy2
#define	qgd_xy2		MESdCoeffs[26]

#endif

/* indices to the array of MESFET noise sources */

#define MESRDNOIZ       0
#define MESRSNOIZ       1
#define MESIDNOIZ       2
#define MESFLNOIZ 3
#define MESTOTNOIZ    4

#define MESNSRCS     5     /* the number of MESFET noise sources */

#ifndef NONOISE
    double MESnVar[NSTATVARS][MESNSRCS];
#else /* NONOISE */
	double **MESnVar;
#endif /* NONOISE */

};

#define MESvgs MESstate 
#define MESvgd MESstate+1 
#define MEScg MESstate+2 
#define MEScd MESstate+3 
#define MEScgd MESstate+4 
#define MESgm MESstate+5 
#define MESgds MESstate+6 
#define MESggs MESstate+7 
#define MESggd MESstate+8 
#define MESqgs MESstate+9 
#define MEScqgs MESstate+10 
#define MESqgd MESstate+11 
#define MEScqgd MESstate+12 


/* per model data */

struct MESModel {       /* model structure for a mesfet */
    int MESmodType; /* type index of this device type */
    MESModel *MESnextModel;   /* pointer to next possible model in 
                                         * linked list */
    MESInstance * MESinstances; /* pointer to list of instances 
                                   * that have this model */
    const char *MESmodName; /* pointer to character string naming this model */

    /* --- end of generic struct MESModel --- */

    int MEStype;

    double MESthreshold;
    double MESalpha;
    double MESbeta;
    double MESlModulation;
    double MESb;
    double MESdrainResist;
    double MESsourceResist;
    double MEScapGS;
    double MEScapGD;
    double MESgatePotential;
    double MESgateSatCurrent;
    double MESdepletionCapCoeff;
    double MESfNcoef;
    double MESfNexp;

    double MESdrainConduct;
    double MESsourceConduct;
    double MESdepletionCap;
    double MESf1;
    double MESf2;
    double MESf3;
    double MESvcrit;

    unsigned MESthresholdGiven : 1;
    unsigned MESalphaGiven : 1;
    unsigned MESbetaGiven : 1;
    unsigned MESlModulationGiven : 1;
    unsigned MESbGiven : 1;
    unsigned MESdrainResistGiven : 1;
    unsigned MESsourceResistGiven : 1;
    unsigned MEScapGSGiven : 1;
    unsigned MEScapGDGiven : 1;
    unsigned MESgatePotentialGiven : 1;
    unsigned MESgateSatCurrentGiven : 1;
    unsigned MESdepletionCapCoeffGiven : 1;
    unsigned MESfNcoefGiven : 1;
    unsigned MESfNexpGiven : 1;


};

#ifndef NMF

#define NMF 1
#define PMF -1

#endif /*NMF*/

/* device parameters */
#define MES_AREA 1
#define MES_IC_VDS 2
#define MES_IC_VGS 3
#define MES_IC 4
#define MES_OFF 5
#define MES_CS 6
#define MES_POWER 7
#define MES_M 8

/* model parameters */
#define MES_MOD_VTO 101
#define MES_MOD_ALPHA 102
#define MES_MOD_BETA 103
#define MES_MOD_LAMBDA 104
#define MES_MOD_B 105
#define MES_MOD_RD 106
#define MES_MOD_RS 107
#define MES_MOD_CGS 108
#define MES_MOD_CGD 109
#define MES_MOD_PB 110
#define MES_MOD_IS 111
#define MES_MOD_FC 112
#define MES_MOD_NMF 113
#define MES_MOD_PMF 114
#define MES_MOD_KF 115
#define MES_MOD_AF 116

/* device questions */

#define MES_DRAINNODE       201
#define MES_GATENODE        202
#define MES_SOURCENODE      203
#define MES_DRAINPRIMENODE  204
#define MES_SOURCEPRIMENODE 205

#define MES_VGS         206
#define MES_VGD         207
#define MES_CG          208
#define MES_CD          209
#define MES_CGD         210
#define MES_GM          211
#define MES_GDS         212
#define MES_GGS         213
#define MES_GGD         214
#define MES_QGS         215
#define MES_CQGS        216
#define MES_QGD         217
#define MES_CQGD        218

/* model questions */

#define MES_MOD_DRAINCONDUCT    301
#define MES_MOD_SOURCECONDUCT   302 
#define MES_MOD_DEPLETIONCAP    303
#define MES_MOD_VCRIT       304
#define MES_MOD_TYPE       305

// --- Parameter tables and dispatchers (defined in devsup/mpar) ------
namespace Shim { struct IfParm; struct IfValue; }
extern Shim::IfParm MESpTable[];
extern int MESpTSize;
extern Shim::IfParm MESmPTable[];
extern int MESmPTSize;
int MESmParam(int param, Shim::IfValue *value, MESModel *model);
int MESparam(int param, Shim::IfValue *value, MESInstance *inst, Shim::IfValue *select);

// --- UCB entry points (defined in setup/temp/load .cpp) ------
int MESsetup(Shim::Matrix*, MESModel*, Shim::Ckt*, int*);
int MEStemp(MESModel*, Shim::Ckt*);
int MESload(MESModel*, Shim::Ckt*);

} // namespace neospice::mes
