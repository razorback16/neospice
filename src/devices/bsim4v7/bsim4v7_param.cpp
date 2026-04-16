/**** BSIM4.7.0 Released by Darsen Lu 04/08/2011 ****/

/**********
 * Copyright 2006 Regents of the University of California. All rights reserved.
 * File: b4par.c of BSIM4.7.0.
 * Author: 2000 Weidong Liu
 * Authors: 2001- Xuemei Xi, Mohan Dunga, Ali Niknejad, Chenming Hu.
 * Authors: 2006- Mohan Dunga, Ali Niknejad, Chenming Hu
 * Authors: 2007- Mohan Dunga, Wenwei Yang, Ali Niknejad, Chenming Hu
 * Project Director: Prof. Chenming Hu.
 * Modified by Xuemei Xi, 04/06/2001.
 * Modified by Xuemei Xi, 11/15/2002.
 * Modified by Xuemei Xi, 05/09/2003.
 * Modified by Xuemei Xi, Mohan Dunga, 07/29/2005.
 *
 * Mechanical translation to C++ by neospice Z-port Task 6.
 * Original: third_party/bsim4_4.7.0/code/b4par.c  (instance-parameter setter)
 * Original: third_party/bsim4_4.7.0/code/b4.c     (BSIM4pTable[] initializer)
**********/

#include "devices/bsim4v7/bsim4v7_def.hpp"

// UCB SPICE3 ifsim.h compatibility macros for pTable initializer:
// IOP = input/output parameter, IP = input-only, OP = output-only.
#define IOP(kw,id,type,desc) { kw, id, (type) | Shim::IF_SET | Shim::IF_ASK, desc }
#define IP(kw,id,type,desc)  { kw, id, (type) | Shim::IF_SET,                 desc }
#define OP(kw,id,type,desc)  { kw, id, (type) | Shim::IF_ASK,                 desc }

namespace neospice::bsim4v7 {

const Shim::IfParm BSIM4pTable[] = { /* parameters */
IOP( "l",   BSIM4_L,      Shim::IF_REAL   , "Length"),
IOP( "w",   BSIM4_W,      Shim::IF_REAL   , "Width"),
IOP( "nf",  BSIM4_NF,     Shim::IF_REAL   , "Number of fingers"),
IOP( "sa",  BSIM4_SA,     Shim::IF_REAL   , "distance between  OD edge to poly of one side "),
IOP( "sb",  BSIM4_SB,     Shim::IF_REAL   , "distance between  OD edge to poly of the other side"),
IOP( "sd",  BSIM4_SD,     Shim::IF_REAL   , "distance between neighbour fingers"),
IOP( "sca",  BSIM4_SCA,     Shim::IF_REAL   , "Integral of the first distribution function for scattered well dopant"),
IOP( "scb",  BSIM4_SCB,     Shim::IF_REAL   , "Integral of the second distribution function for scattered well dopant"),
IOP( "scc",  BSIM4_SCC,     Shim::IF_REAL   , "Integral of the third distribution function for scattered well dopant"),
IOP( "sc",  BSIM4_SC,     Shim::IF_REAL   , "Distance to a single well edge "),
IOP( "min",  BSIM4_MIN,   Shim::IF_INTEGER , "Minimize either D or S"),
IOP( "ad",  BSIM4_AD,     Shim::IF_REAL   , "Drain area"),
IOP( "as",  BSIM4_AS,     Shim::IF_REAL   , "Source area"),
IOP( "pd",  BSIM4_PD,     Shim::IF_REAL   , "Drain perimeter"),
IOP( "ps",  BSIM4_PS,     Shim::IF_REAL   , "Source perimeter"),
IOP( "nrd", BSIM4_NRD,    Shim::IF_REAL   , "Number of squares in drain"),
IOP( "nrs", BSIM4_NRS,    Shim::IF_REAL   , "Number of squares in source"),
IOP( "off", BSIM4_OFF,    Shim::IF_FLAG   , "Device is initially off"),
IOP( "rbdb", BSIM4_RBDB,  Shim::IF_REAL   , "Body resistance"),
IOP( "rbsb", BSIM4_RBSB,  Shim::IF_REAL   , "Body resistance"),
IOP( "rbpb", BSIM4_RBPB,  Shim::IF_REAL   , "Body resistance"),
IOP( "rbps", BSIM4_RBPS,  Shim::IF_REAL   , "Body resistance"),
IOP( "rbpd", BSIM4_RBPD,  Shim::IF_REAL   , "Body resistance"),
IOP( "delvto", BSIM4_DELVTO,  Shim::IF_REAL   , "Zero bias threshold voltage variation"),
IOP( "xgw",  BSIM4_XGW, Shim::IF_REAL, "Distance from gate contact center to device edge"),
IOP( "ngcon", BSIM4_NGCON, Shim::IF_REAL, "Number of gate contacts"),


IOP( "trnqsmod", BSIM4_TRNQSMOD, Shim::IF_INTEGER, "Transient NQS model selector"),
IOP( "acnqsmod", BSIM4_ACNQSMOD, Shim::IF_INTEGER, "AC NQS model selector"),
IOP( "rbodymod", BSIM4_RBODYMOD, Shim::IF_INTEGER, "Distributed body R model selector"),
IOP( "rgatemod", BSIM4_RGATEMOD, Shim::IF_INTEGER, "Gate resistance model selector"),
IOP( "geomod", BSIM4_GEOMOD, Shim::IF_INTEGER, "Geometry dependent parasitics model selector"),
IOP( "rgeomod", BSIM4_RGEOMOD, Shim::IF_INTEGER, "S/D resistance and contact model selector"),
IP( "ic",  BSIM4_IC,     Shim::IF_REALVEC , "Vector of DS,GS,BS initial voltages"),
OP( "gmbs",         BSIM4_GMBS,       Shim::IF_REAL,    "Gmb"),
OP( "gm",           BSIM4_GM,         Shim::IF_REAL,    "Gm"),
OP( "gds",          BSIM4_GDS,        Shim::IF_REAL,    "Gds"),
OP( "vdsat",        BSIM4_VDSAT,      Shim::IF_REAL,    "Vdsat"),
OP( "vth",          BSIM4_VON,        Shim::IF_REAL,    "Vth"),
OP( "id",           BSIM4_CD,         Shim::IF_REAL,    "Ids"),
OP( "ibd",          BSIM4_CBD,        Shim::IF_REAL,    "Ibd"),
OP( "ibs",          BSIM4_CBS,        Shim::IF_REAL,    "Ibs"),
OP( "gbd",          BSIM4_GBD,        Shim::IF_REAL,    "gbd"),
OP( "gbs",          BSIM4_GBS,        Shim::IF_REAL,    "gbs"),
OP( "isub",         BSIM4_CSUB,       Shim::IF_REAL,    "Isub"),
OP( "igidl",        BSIM4_IGIDL,      Shim::IF_REAL,    "Igidl"),
OP( "igisl",        BSIM4_IGISL,      Shim::IF_REAL,    "Igisl"),
OP( "igs",          BSIM4_IGS,        Shim::IF_REAL,    "Igs"),
OP( "igd",          BSIM4_IGD,        Shim::IF_REAL,    "Igd"),
OP( "igb",          BSIM4_IGB,        Shim::IF_REAL,    "Igb"),
OP( "igcs",         BSIM4_IGCS,       Shim::IF_REAL,    "Igcs"),
OP( "igcd",         BSIM4_IGCD,       Shim::IF_REAL,    "Igcd"),
OP( "vbs",          BSIM4_VBS,        Shim::IF_REAL,    "Vbs"),
OP( "vgs",          BSIM4_VGS,        Shim::IF_REAL,    "Vgs"),
OP( "vds",          BSIM4_VDS,        Shim::IF_REAL,    "Vds"),
OP( "cgg",         BSIM4_CGGB,       Shim::IF_REAL,    "Cggb"),
OP( "cgs",         BSIM4_CGSB,       Shim::IF_REAL,    "Cgsb"),
OP( "cgd",         BSIM4_CGDB,       Shim::IF_REAL,    "Cgdb"),
OP( "cbg",         BSIM4_CBGB,       Shim::IF_REAL,    "Cbgb"),
OP( "cbd",         BSIM4_CBDB,       Shim::IF_REAL,    "Cbdb"),
OP( "cbs",         BSIM4_CBSB,       Shim::IF_REAL,    "Cbsb"),
OP( "cdg",         BSIM4_CDGB,       Shim::IF_REAL,    "Cdgb"),
OP( "cdd",         BSIM4_CDDB,       Shim::IF_REAL,    "Cddb"),
OP( "cds",         BSIM4_CDSB,       Shim::IF_REAL,    "Cdsb"),
OP( "csg",         BSIM4_CSGB,       Shim::IF_REAL,    "Csgb"),
OP( "csd",         BSIM4_CSDB,       Shim::IF_REAL,    "Csdb"),
OP( "css",         BSIM4_CSSB,       Shim::IF_REAL,    "Cssb"),
OP( "cgb",         BSIM4_CGBB,       Shim::IF_REAL,    "Cgbb"),
OP( "cdb",         BSIM4_CDBB,       Shim::IF_REAL,    "Cdbb"),
OP( "csb",         BSIM4_CSBB,       Shim::IF_REAL,    "Csbb"),
OP( "cbb",         BSIM4_CBBB,       Shim::IF_REAL,    "Cbbb"),
OP( "capbd",       BSIM4_CAPBD,      Shim::IF_REAL,    "Capbd"),
OP( "capbs",       BSIM4_CAPBS,      Shim::IF_REAL,    "Capbs"),
OP( "qg",          BSIM4_QG,         Shim::IF_REAL,    "Qgate"),
OP( "qb",          BSIM4_QB,         Shim::IF_REAL,    "Qbulk"),
OP( "qd",          BSIM4_QD,         Shim::IF_REAL,    "Qdrain"),
OP( "qs",          BSIM4_QS,         Shim::IF_REAL,    "Qsource"),
OP( "qinv",        BSIM4_QINV,       Shim::IF_REAL,    "Qinversion"),
OP( "qdef",        BSIM4_QDEF,       Shim::IF_REAL,    "Qdef"),
OP( "gcrg",        BSIM4_GCRG,       Shim::IF_REAL,    "Gcrg"),
OP( "gtau",        BSIM4_GTAU,       Shim::IF_REAL,    "Gtau"),
};

const int BSIM4pTSize = sizeof(BSIM4pTable) / sizeof(BSIM4pTable[0]);

int BSIM4param(int param, Shim::IfValue *value, BSIM4v7Instance *here, Shim::IfValue * /*select*/)
{
    switch(param)
    {   case BSIM4_W:
            here->BSIM4w = value->rValue;
            here->BSIM4wGiven = TRUE;
            break;
        case BSIM4_L:
            here->BSIM4l = value->rValue;
            here->BSIM4lGiven = TRUE;
            break;
        case BSIM4_NF:
            here->BSIM4nf = value->rValue;
            here->BSIM4nfGiven = TRUE;
            break;
        case BSIM4_MIN:
            here->BSIM4min = value->iValue;
            here->BSIM4minGiven = TRUE;
            break;
        case BSIM4_AS:
            here->BSIM4sourceArea = value->rValue;
            here->BSIM4sourceAreaGiven = TRUE;
            break;
        case BSIM4_AD:
            here->BSIM4drainArea = value->rValue;
            here->BSIM4drainAreaGiven = TRUE;
            break;
        case BSIM4_PS:
            here->BSIM4sourcePerimeter = value->rValue;
            here->BSIM4sourcePerimeterGiven = TRUE;
            break;
        case BSIM4_PD:
            here->BSIM4drainPerimeter = value->rValue;
            here->BSIM4drainPerimeterGiven = TRUE;
            break;
        case BSIM4_NRS:
            here->BSIM4sourceSquares = value->rValue;
            here->BSIM4sourceSquaresGiven = TRUE;
            break;
        case BSIM4_NRD:
            here->BSIM4drainSquares = value->rValue;
            here->BSIM4drainSquaresGiven = TRUE;
            break;
        case BSIM4_OFF:
            here->BSIM4off = value->iValue;
            break;
        case BSIM4_SA:
            here->BSIM4sa = value->rValue;
            here->BSIM4saGiven = TRUE;
            break;
        case BSIM4_SB:
            here->BSIM4sb = value->rValue;
            here->BSIM4sbGiven = TRUE;
            break;
        case BSIM4_SD:
            here->BSIM4sd = value->rValue;
            here->BSIM4sdGiven = TRUE;
            break;
        case BSIM4_SCA:
            here->BSIM4sca = value->rValue;
            here->BSIM4scaGiven = TRUE;
            break;
        case BSIM4_SCB:
            here->BSIM4scb = value->rValue;
            here->BSIM4scbGiven = TRUE;
            break;
        case BSIM4_SCC:
            here->BSIM4scc = value->rValue;
            here->BSIM4sccGiven = TRUE;
            break;
        case BSIM4_SC:
            here->BSIM4sc = value->rValue;
            here->BSIM4scGiven = TRUE;
            break;
        case BSIM4_RBSB:
            here->BSIM4rbsb = value->rValue;
            here->BSIM4rbsbGiven = TRUE;
            break;
        case BSIM4_RBDB:
            here->BSIM4rbdb = value->rValue;
            here->BSIM4rbdbGiven = TRUE;
            break;
        case BSIM4_RBPB:
            here->BSIM4rbpb = value->rValue;
            here->BSIM4rbpbGiven = TRUE;
            break;
        case BSIM4_RBPS:
            here->BSIM4rbps = value->rValue;
            here->BSIM4rbpsGiven = TRUE;
            break;
        case BSIM4_RBPD:
            here->BSIM4rbpd = value->rValue;
            here->BSIM4rbpdGiven = TRUE;
            break;
        case BSIM4_DELVTO:
            here->BSIM4delvto = value->rValue;
            here->BSIM4delvtoGiven = TRUE;
            break;
        case BSIM4_XGW:
            here->BSIM4xgw = value->rValue;
            here->BSIM4xgwGiven = TRUE;
            break;
        case BSIM4_NGCON:
            here->BSIM4ngcon = value->rValue;
            here->BSIM4ngconGiven = TRUE;
            break;
        case BSIM4_TRNQSMOD:
            here->BSIM4trnqsMod = value->iValue;
            here->BSIM4trnqsModGiven = TRUE;
            break;
        case BSIM4_ACNQSMOD:
            here->BSIM4acnqsMod = value->iValue;
            here->BSIM4acnqsModGiven = TRUE;
            break;
        case BSIM4_RBODYMOD:
            here->BSIM4rbodyMod = value->iValue;
            here->BSIM4rbodyModGiven = TRUE;
            break;
        case BSIM4_RGATEMOD:
            here->BSIM4rgateMod = value->iValue;
            here->BSIM4rgateModGiven = TRUE;
            break;
        case BSIM4_GEOMOD:
            here->BSIM4geoMod = value->iValue;
            here->BSIM4geoModGiven = TRUE;
            break;
        case BSIM4_RGEOMOD:
            here->BSIM4rgeoMod = value->iValue;
            here->BSIM4rgeoModGiven = TRUE;
            break;
        case BSIM4_IC_VDS:
            here->BSIM4icVDS = value->rValue;
            here->BSIM4icVDSGiven = TRUE;
            break;
        case BSIM4_IC_VGS:
            here->BSIM4icVGS = value->rValue;
            here->BSIM4icVGSGiven = TRUE;
            break;
        case BSIM4_IC_VBS:
            here->BSIM4icVBS = value->rValue;
            here->BSIM4icVBSGiven = TRUE;
            break;
        case BSIM4_IC:
            switch(value->v.numValue)
            {   case 3:
                    here->BSIM4icVBS = *(value->v.vec.rVec+2);
                    here->BSIM4icVBSGiven = TRUE;
                case 2:
                    here->BSIM4icVGS = *(value->v.vec.rVec+1);
                    here->BSIM4icVGSGiven = TRUE;
                case 1:
                    here->BSIM4icVDS = *(value->v.vec.rVec);
                    here->BSIM4icVDSGiven = TRUE;
                    break;
                default:
                    return Shim::E_BADPARM;
            }
            break;
        default:
            return Shim::E_BADPARM;
    }
    return Shim::OK;
}

} // namespace neospice::bsim4v7
