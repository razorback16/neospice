// jfet2_psmodel.hpp — Parker-Skellern model functions adapted for neospice.
// Translated from ngspice psmodel.h / psmodel.c.
#pragma once

#include "devices/jfet2/jfet2_def.hpp"
#include "devices/jfet2/jfet2_shim.hpp"

namespace neospice::jfet2 {

// Forward declarations of PS model functions.
// These replace the original psmodel.h extern declarations.
void PSinstanceinit(JFET2Model *model, JFET2Instance *here);

double PSids(Shim::Ckt *ckt, JFET2Model *model, JFET2Instance *here,
             double vgs, double vgd,
             double *igs, double *igd, double *ggs, double *ggd,
             double *Gm, double *Gds);

void PScharge(Shim::Ckt *ckt, JFET2Model *model, JFET2Instance *here,
              double vgs, double vgd, double *capgs, double *capgd);

void PSacload(Shim::Ckt *ckt, JFET2Model *model, JFET2Instance *here,
              double vgs, double vgd, double ids, double omega,
              double *Gm, double *xGm, double *Gds, double *xGds);

} // namespace neospice::jfet2
