// SPDX-License-Identifier: BSD-3-Clause
// See NOTICE and CREDITS.md for full attribution.
//
// ckt_mode.hpp — ngspice CKTmode bit definitions.
//
// Mirrors src/include/ngspice/cktdefs.h.  These bits are passed through
// IntegratorCtx::mode and inspected by the analysis drivers and by translated
// device code, so the values must stay identical to the reference simulator.
// Keep this as the single source of truth: diffing one header against
// cktdefs.h is tractable, hand-copied constants in every translation unit are
// not.
#pragma once

namespace neospice {

inline constexpr int MODE_BIT              = 0x3;
inline constexpr int MODETRAN_BIT          = 0x1;
inline constexpr int MODEAC_BIT            = 0x2;
inline constexpr int MODEACNOISE_BIT       = 0x8;

inline constexpr int MODEDC_BIT            = 0x70;
inline constexpr int MODEDCOP_BIT          = 0x10;
inline constexpr int MODETRANOP_BIT        = 0x20;
inline constexpr int MODEDCTRANCURVE_BIT   = 0x40;

/// Covers every MODEINIT* bit; cleared before setting a new init phase.
inline constexpr int INITF_MASK             = 0x3F00;

inline constexpr int MODEINITFLOAT_BIT     = 0x100;
inline constexpr int MODEINITJCT_BIT       = 0x200;
inline constexpr int MODEINITFIX_BIT       = 0x400;
inline constexpr int MODEINITSMSIG_BIT     = 0x800;
inline constexpr int MODEINITTRAN_BIT      = 0x1000;
inline constexpr int MODEINITPRED_BIT      = 0x2000;

inline constexpr int MODESP_BIT            = 0x4000;
inline constexpr int MODESPNOISE_BIT       = 0x8000;

inline constexpr int MODEUIC_BIT           = 0x10000;

} // namespace neospice
