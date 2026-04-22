#pragma once

#include "core/matrix.hpp"

#ifndef NSTATVARS
#define NSTATVARS 3
#endif

namespace neospice::hfet2 {

namespace Shim { struct Ckt; class Matrix; }

struct HFET2Model;

struct HFET2Instance {
    HFET2Model *HFET2modPtr;
    HFET2Instance *HFET2nextInstance;
    const char *HFET2name;
    int HFET2state;   /* index into state table for this device */
     
    int HFET2drainNode;
    int HFET2gateNode;
    int HFET2sourceNode;
    int HFET2drainPrimeNode;
    int HFET2sourcePrimeNode;
    double HFET2length;
    double HFET2width;
    double HFET2m;
    double HFET2temp;
    double HFET2dtemp;
    double HFET2tLambda;
    double HFET2tMu;
    double HFET2tNmax;
    double HFET2tVto;
    double HFET2icVDS;
    double HFET2icVGS;
    neospice::MatrixOffset HFET2drainDrainPrimePtr{-1};
    neospice::MatrixOffset HFET2gateDrainPrimePtr{-1};
    neospice::MatrixOffset HFET2gateSourcePrimePtr{-1};
    neospice::MatrixOffset HFET2sourceSourcePrimePtr{-1};
    neospice::MatrixOffset HFET2drainPrimeDrainPtr{-1};
    neospice::MatrixOffset HFET2drainPrimeGatePtr{-1};
    neospice::MatrixOffset HFET2drainPriHFET2ourcePrimePtr{-1};
    neospice::MatrixOffset HFET2sourcePrimeGatePtr{-1};
    neospice::MatrixOffset HFET2sourcePriHFET2ourcePtr{-1};
    neospice::MatrixOffset HFET2sourcePrimeDrainPrimePtr{-1};
    neospice::MatrixOffset HFET2drainDrainPtr{-1};
    neospice::MatrixOffset HFET2gateGatePtr{-1};  
    neospice::MatrixOffset HFET2sourceSourcePtr{-1};
    neospice::MatrixOffset HFET2drainPrimeDrainPrimePtr{-1};
    neospice::MatrixOffset HFET2sourcePriHFET2ourcePrimePtr{-1};

   
#define HFET2vgs HFET2state 
#define HFET2vgd HFET2state+1 
#define HFET2cg HFET2state+2 
#define HFET2cd HFET2state+3 
#define HFET2cgd HFET2state+4 
#define HFET2gm HFET2state+5 
#define HFET2gds HFET2state+6 
#define HFET2ggs HFET2state+7 
#define HFET2ggd HFET2state+8 
#define HFET2qgs HFET2state+9 
#define HFET2cqgs HFET2state+10 
#define HFET2qgd HFET2state+11 
#define HFET2cqgd HFET2state+12 
    
    int HFET2mode;
    int HFET2off;
    
    unsigned HFET2icVDSGiven  : 1;
    unsigned HFET2icVGSGiven  : 1;
    unsigned HFET2lengthGiven : 1;
    unsigned HFET2widthGiven  : 1;
    unsigned HFET2mGiven      : 1;
    unsigned HFET2tempGiven   : 1;
    unsigned HFET2dtempGiven  : 1;
        
    double HFET2n0;
    double HFET2n01;
    double HFET2n02;
    double HFET2gchi0;
    double HFET2imax;
    double HFET2vcrit;
    double HFET2ggrlw;
    double HFET2jslw;
    
};



struct HFET2Model {
    int HFET2modType;
    HFET2Model *HFET2nextModel;
    HFET2Instance * HFET2instances;

    /* --- end of generic struct HFET2Model --- */

    const char *HFET2modName;
    int HFET2type;
    
    double HFET2cf;
    double HFET2d1;
    double HFET2d2;
    double HFET2del;
    double HFET2delta;
    double HFET2deltad;
    double HFET2di;
    double HFET2epsi;
    double HFET2eta;
    double HFET2eta1;
    double HFET2eta2;
    double HFET2gamma;
    double HFET2ggr;
    double HFET2js;
    double HFET2klambda;
    double HFET2kmu;
    double HFET2knmax;
    double HFET2kvto;
    double HFET2lambda;
    double HFET2m;
    double HFET2mc;
    double HFET2mu;
    double HFET2n;
    double HFET2nmax;
    double HFET2p;
    double HFET2rd;
    double HFET2rdi;    
    double HFET2rs;
    double HFET2rsi;
    double HFET2sigma0;
    double HFET2vs;    
    double HFET2vsigma;    
    double HFET2vsigmat;
    double HFET2vt1;
    double HFET2vt2;
    double HFET2vto;
    
    double HFET2drainConduct;
    double HFET2sourceConduct;
    double HFET2deltaSqr;

    unsigned HFET2cfGiven : 1;
    unsigned HFET2d1Given : 1;
    unsigned HFET2d2Given : 1;
    unsigned HFET2delGiven : 1;
    unsigned HFET2deltaGiven : 1;
    unsigned HFET2deltadGiven : 1;
    unsigned HFET2diGiven : 1;
    unsigned HFET2epsiGiven : 1;
    unsigned HFET2etaGiven : 1;
    unsigned HFET2eta1Given : 1;
    unsigned HFET2eta2Given : 1;
    unsigned HFET2gammaGiven : 1;
    unsigned HFET2ggrGiven : 1;
    unsigned HFET2jsGiven : 1;
    unsigned HFET2klambdaGiven : 1;
    unsigned HFET2kmuGiven : 1;
    unsigned HFET2knmaxGiven : 1;
    unsigned HFET2kvtoGiven : 1;
    unsigned HFET2lambdaGiven : 1;
    unsigned HFET2mGiven : 1;
    unsigned HFET2mcGiven : 1;
    unsigned HFET2muGiven : 1;
    unsigned HFET2nGiven : 1;
    unsigned HFET2nmaxGiven : 1;
    unsigned HFET2pGiven : 1;
    unsigned HFET2rdGiven : 1;
    unsigned HFET2rdiGiven : 1;
    unsigned HFET2rsGiven : 1;
    unsigned HFET2rsiGiven : 1;
    unsigned HFET2sigma0Given : 1;
    unsigned HFET2vsGiven : 1;
    unsigned HFET2vsigmaGiven : 1;
    unsigned HFET2vsigmatGiven : 1;
    unsigned HFET2vt1Given : 1;
    unsigned HFET2vt2Given : 1;
    unsigned HFET2vtoGiven : 1;

};


#ifndef NHFET
#define NHFET 1
#define PHFET -1
#endif /*NMF*/

/* device parameters */
#define HFET2_LENGTH 1
#define HFET2_WIDTH  2
#define HFET2_IC_VDS 3
#define HFET2_IC_VGS 4
#define HFET2_IC     5
#define HFET2_OFF    6
#define HFET2_CS     7
#define HFET2_POWER  8
#define HFET2_TEMP   9
#define HFET2_DTEMP 10
#define HFET2_M     11

/* model parameters */
#define HFET2_MOD_NHFET   101
#define HFET2_MOD_PHFET   102
#define HFET2_MOD_CF      103
#define HFET2_MOD_D1      104
#define HFET2_MOD_D2      105
#define HFET2_MOD_DEL     106
#define HFET2_MOD_DELTA   107
#define HFET2_MOD_DELTAD  108
#define HFET2_MOD_DI      109
#define HFET2_MOD_EPSI    110
#define HFET2_MOD_ETA     111
#define HFET2_MOD_ETA1    112
#define HFET2_MOD_ETA2    113
#define HFET2_MOD_GAMMA   114
#define HFET2_MOD_GGR     115
#define HFET2_MOD_JS      116
#define HFET2_MOD_KLAMBDA 117
#define HFET2_MOD_KMU     118
#define HFET2_MOD_KNMAX   119
#define HFET2_MOD_KVTO    120
#define HFET2_MOD_LAMBDA  121
#define HFET2_MOD_M       122
#define HFET2_MOD_MC      123
#define HFET2_MOD_MU      124
#define HFET2_MOD_N       125
#define HFET2_MOD_NMAX    126
#define HFET2_MOD_P       127
#define HFET2_MOD_RD      128
#define HFET2_MOD_RDI     129
#define HFET2_MOD_RS      130
#define HFET2_MOD_RSI     131
#define HFET2_MOD_SIGMA0  132
#define HFET2_MOD_VS      133
#define HFET2_MOD_VSIGMA  134
#define HFET2_MOD_VSIGMAT 135
#define HFET2_MOD_VT1     136
#define HFET2_MOD_VT2     137
#define HFET2_MOD_VTO     138
#define HFET2_MOD_TYPE    139

/* device questions */

#define HFET2_DRAINNODE       201
#define HFET2_GATENODE        202
#define HFET2_SOURCENODE      203
#define HFET2_DRAINPRIMENODE  204
#define HFET2_SOURCEPRIMENODE 205

#define HFET2_VGS         206
#define HFET2_VGD         207
#define HFET2_CG          208
#define HFET2_CD          209
#define HFET2_CGD         210
#define HFET2_GM          211
#define HFET2_GDS         212
#define HFET2_GGS         213
#define HFET2_GGD         214
#define HFET2_QGS         215
#define HFET2_CQGS        216
#define HFET2_QGD         217
#define HFET2_CQGD        218

/* model questions */

#define HFET2_MOD_DRAINCONDUCT    301
#define HFET2_MOD_SOURCECONDUCT   302 
#define HFET2_MOD_DEPLETIONCAP    303
#define HFET2_MOD_VCRIT           304

// Convenience macros (CF, D1, L, W, M, N, TEMP, etc.) are in hfet2_macros.hpp.
// They are only included by translated UCB .cpp files, NOT by device adapter headers.

// --- Parameter tables and dispatchers (defined in devsup/mpar) ------
namespace Shim { struct IfParm; struct IfValue; }
extern Shim::IfParm HFET2pTable[];
extern int HFET2pTSize;
extern Shim::IfParm HFET2mPTable[];
extern int HFET2mPTSize;
int HFET2mParam(int param, Shim::IfValue *value, HFET2Model *model);
int HFET2param(int param, Shim::IfValue *value, HFET2Instance *inst, Shim::IfValue *select);

// --- UCB entry points (defined in setup/temp/load .cpp) ------
int HFET2setup(Shim::Matrix*, HFET2Model*, Shim::Ckt*, int*);
int HFET2temp(HFET2Model*, Shim::Ckt*);
int HFET2load(HFET2Model*, Shim::Ckt*);

} // namespace neospice::hfet2
