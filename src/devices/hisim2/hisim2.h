// HiSIM2 local header — neospice port of ngspice hisim2.h
// NOTE: This header is #include'd from within namespace neospice::hisim2 {}
#pragma once

// hisim2_def.hpp is already included by the translation unit before this header

/* return value */
#ifndef OK
#define HiSIM_OK        0
#define HiSIM_ERROR     1
#else
#define HiSIM_OK        OK
#define HiSIM_ERROR     E_PANIC
#endif

/* MOS type */
#ifndef NMOS
#define NMOS     1
#define PMOS    -1
#endif

/* device working mode */
#ifndef CMI_NORMAL_MODE
#define HiSIM_NORMAL_MODE    1
#define HiSIM_REVERSE_MODE  -1
#else
#define HiSIM_NORMAL_MODE  CMI_NORMAL_MODE
#define HiSIM_REVERSE_MODE CMI_REVERSE_MODE
#endif

#define HiSIM_FALSE     0
#define HiSIM_TRUE      1

#ifndef return_if_error
#define return_if_error(s) { int error = s; if(error) return(error); }
#endif

// HSM2evaluate is declared within the neospice::hisim2 namespace
// (this header is included inside that namespace)
extern int HSM2evaluate(
    double ivds,
    double ivgs,
    double ivbs,
    double vbs_jct,
    double vbd_jct,
    HSM2Instance *here,
    HSM2Model    *model,
    Shim::Ckt    *ckt
);
