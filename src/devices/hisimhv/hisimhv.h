/* HiSIM_HV model header — adapted for neospice.
 * Original: (C) 2014 Hiroshima University & STARC, HiSIM_HV v2.20 */

#ifndef _HiSIMHV_H
#define _HiSIMHV_H

#include "hisimhv_def.hpp"

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

/* Function declarations are in hisimhv_def.hpp via the namespace block. */

#endif /* _HiSIMHV_H */
