/***  B4SOI 12/16/2010 Released by Tanvir Morshed   ***/


/**********
 * Copyright 2010 Regents of the University of California.  All rights reserved.
 * Authors: 1998 Samuel Fung, Dennis Sinitsky and Stephen Tang
 * Authors: 1999-2004 Pin Su, Hui Wan, Wei Jin, b3soipar.c
 * Authors: 2005- Hui Wan, Xuemei Xi, Ali Niknejad, Chenming Hu.
 * Authors: 2009- Wenwei Yang, Chung-Hsun Lin, Ali Niknejad, Chenming Hu.
 * File: b4soipar.c
 * Modified by Hui Wan, Xuemei Xi 11/30/2005
 * Modified by Wenwei Yang, Chung-Hsun Lin, Darsen Lu 03/06/2009
 * Modified by Tanvir Morshed 09/22/2009
 * Modified by Tanvir Morshed 12/31/2009
 **********/

// Translated to C++ for neospice by tools/ngspice_migrate.

#include "devices/bsimsoi/bsimsoi_def.hpp"
#include "devices/bsimsoi/bsimsoi_shim.hpp"
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

namespace neospice::bsimsoi {

using namespace Shim;

int
B4SOIparam(
int param,
Shim::IfValue *value,
B4SOIInstance *inst,
Shim::IfValue *select)
{
    double scale;

    B4SOIInstance *here = (B4SOIInstance*)inst;
    
    NG_IGNORE(select);
    
    if (!cp_getvar("scale", CP_REAL, &scale))
        scale = 1;

    switch(param) 
    {   case B4SOI_W:
            here->B4SOIw = value->rValue * scale;
            here->B4SOIwGiven = TRUE;
            break;
        case B4SOI_L:
            here->B4SOIl = value->rValue * scale;
            here->B4SOIlGiven = TRUE;
            break;
        case B4SOI_M:
            here->B4SOIm = value->rValue;
            here->B4SOImGiven = TRUE;
            break;
        case B4SOI_AS:
            here->B4SOIsourceArea = value->rValue * scale * scale;
            here->B4SOIsourceAreaGiven = TRUE;
            break;
        case B4SOI_AD:
            here->B4SOIdrainArea = value->rValue * scale * scale;
            here->B4SOIdrainAreaGiven = TRUE;
            break;
        case B4SOI_PS:
            here->B4SOIsourcePerimeter = value->rValue * scale;
            here->B4SOIsourcePerimeterGiven = TRUE;
            break;
        case B4SOI_PD:
            here->B4SOIdrainPerimeter = value->rValue * scale;
            here->B4SOIdrainPerimeterGiven = TRUE;
            break;
        case B4SOI_NRS:
            here->B4SOIsourceSquares = value->rValue;
            here->B4SOIsourceSquaresGiven = TRUE;
            break;
        case B4SOI_NRD:
            here->B4SOIdrainSquares = value->rValue;
            here->B4SOIdrainSquaresGiven = TRUE;
            break;
        case B4SOI_OFF:
            here->B4SOIoff = value->iValue;
            here->B4SOIoffGiven = TRUE;
            break;
        case B4SOI_IC_VBS:
            here->B4SOIicVBS = value->rValue;
            here->B4SOIicVBSGiven = TRUE;
            break;
        case B4SOI_IC_VDS:
            here->B4SOIicVDS = value->rValue;
            here->B4SOIicVDSGiven = TRUE;
            break;
        case B4SOI_IC_VGS:
            here->B4SOIicVGS = value->rValue;
            here->B4SOIicVGSGiven = TRUE;
            break;
        case B4SOI_IC_VES:
            here->B4SOIicVES = value->rValue;
            here->B4SOIicVESGiven = TRUE;
            break;
        case B4SOI_IC_VPS:
            here->B4SOIicVPS = value->rValue;
            here->B4SOIicVPSGiven = TRUE;
            break;
        case B4SOI_BJTOFF:
            here->B4SOIbjtoff = value->iValue;
            here->B4SOIbjtoffGiven= TRUE;
            break;
        case B4SOI_DEBUG:
            here->B4SOIdebugMod = value->iValue;
            here->B4SOIdebugModGiven= TRUE;
            break;
        case B4SOI_RTH0:
            here->B4SOIrth0= value->rValue;
            here->B4SOIrth0Given = TRUE;
            break;
        case B4SOI_CTH0:
            here->B4SOIcth0= value->rValue;
            here->B4SOIcth0Given = TRUE;
            break;
        case B4SOI_NRB:
            here->B4SOIbodySquares = value->rValue;
            here->B4SOIbodySquaresGiven = TRUE;
            break;
        case B4SOI_FRBODY:
            here->B4SOIfrbody = value->rValue;
            here->B4SOIfrbodyGiven = TRUE;
            break;

/* v4.0 added */
        case B4SOI_RBSB:
            here->B4SOIrbsb = value->rValue;
            here->B4SOIrbsbGiven = TRUE;
            break;
        case B4SOI_RBDB:
            here->B4SOIrbdb = value->rValue;
            here->B4SOIrbdbGiven = TRUE;
            break;
        case B4SOI_SA:
            here->B4SOIsa = value->rValue;
            here->B4SOIsaGiven = TRUE;
            break;
        case B4SOI_SB:
            here->B4SOIsb = value->rValue;
            here->B4SOIsbGiven = TRUE;
            break;
        case B4SOI_SD:
            here->B4SOIsd = value->rValue;
            here->B4SOIsdGiven = TRUE;
            break;
        case B4SOI_RBODYMOD:
            here->B4SOIrbodyMod = value->iValue;
            here->B4SOIrbodyModGiven = TRUE;
            break;
        case B4SOI_NF:
            here->B4SOInf = value->rValue;
            here->B4SOInfGiven = TRUE;
            break;
        case B4SOI_DELVTO:
            here->B4SOIdelvto = value->rValue;
            here->B4SOIdelvtoGiven = TRUE;
            break;

/* v4.0 added end */

        case B4SOI_SOIMOD:
            here->B4SOIsoiMod = value->iValue;
            here->B4SOIsoiModGiven = TRUE;
            break; /* v3.2 */

/* v3.1 added rgate */
        case B4SOI_RGATEMOD:
            here->B4SOIrgateMod = value->iValue;
            here->B4SOIrgateModGiven = TRUE;
            break;
/* v3.1 added rgate end */


/* v2.0 release */
        case B4SOI_NBC:
            here->B4SOInbc = value->rValue;
            here->B4SOInbcGiven = TRUE;
            break;
        case B4SOI_NSEG:
            here->B4SOInseg = value->rValue;
            here->B4SOInsegGiven = TRUE;
            break;
        case B4SOI_PDBCP:
            here->B4SOIpdbcp = value->rValue;
            here->B4SOIpdbcpGiven = TRUE;
            break;
        case B4SOI_PSBCP:
            here->B4SOIpsbcp = value->rValue;
            here->B4SOIpsbcpGiven = TRUE;
            break;
        case B4SOI_AGBCP:
            here->B4SOIagbcp = value->rValue;
            here->B4SOIagbcpGiven = TRUE;
            break;
        case B4SOI_AGBCP2:
            here->B4SOIagbcp2 = value->rValue;
            here->B4SOIagbcp2Given = TRUE;
            break;  /* v4.1 */
        case B4SOI_AGBCPD:
            here->B4SOIagbcpd = value->rValue;
            here->B4SOIagbcpdGiven = TRUE;
            break;
        case B4SOI_AEBCP:
            here->B4SOIaebcp = value->rValue;
            here->B4SOIaebcpGiven = TRUE;
            break;
        case B4SOI_VBSUSR:
            here->B4SOIvbsusr = value->rValue;
            here->B4SOIvbsusrGiven = TRUE;
            break;
        case B4SOI_TNODEOUT:
            here->B4SOItnodeout = value->iValue;
            here->B4SOItnodeoutGiven = TRUE;
            break;


        case B4SOI_IC:
            switch(value->v.numValue){
                case 5:
                    here->B4SOIicVPS = *(value->v.vec.rVec+4);
                    here->B4SOIicVPSGiven = TRUE;
                                        break; /* v4.2 bugfix */
                case 4:
                    here->B4SOIicVES = *(value->v.vec.rVec+3);
                    here->B4SOIicVESGiven = TRUE;
                                        break; /* v4.2 bugfix */
                case 3:
                    here->B4SOIicVBS = *(value->v.vec.rVec+2);
                    here->B4SOIicVBSGiven = TRUE;
                                        break; /* v4.2 bugfix */
                case 2:
                    here->B4SOIicVGS = *(value->v.vec.rVec+1);
                    here->B4SOIicVGSGiven = TRUE;
                                        break; /* v4.2 bugfix */
                case 1:
                    here->B4SOIicVDS = *(value->v.vec.rVec);
                    here->B4SOIicVDSGiven = TRUE;
                    break;
                default:
                    return Shim::E_BADPARM;
            }
            break;
        default:
            return Shim::E_BADPARM;
    }
    return 0;
}




} // namespace neospice::bsimsoi
