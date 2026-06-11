#pragma once
#include "devices/switch.hpp"
#include "devices/resistor_model.hpp"
#include "devices/capacitor_model.hpp"
#include "devices/inductor_model.hpp"
#include "parser/tokenizer.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace neospice {

/// PSpice DEV/LOT tolerance annotation on a .MODEL parameter.
/// Preserved for round-trip fidelity; not used in simulation.
struct ToleranceAnnotation {
    std::string param_name;
    std::string kind;          // "dev" or "lot"
    std::string distribution;  // "gauss", "uniform", or custom name (empty = default)
    double value = 0.0;
    bool is_percent = false;
};

struct ModelCard {
    std::string name;
    std::string type; // "d", "nmos", "pmos", "npn", "pnp" (lowercase)
    int source_order = 0; // Flat .model card order after subcircuit expansion.
    std::string ako_base; // PSpice AKO: base model name (empty if not AKO)
    // Stored lowercase for case-insensitive lookup.  Values are parsed as
    // doubles; integer/flag BSIM4 parameters cast from double at dispatch.
    std::unordered_map<std::string, double> params;

    // PSpice tolerance annotations (DEV/LOT on parameters)
    std::vector<ToleranceAnnotation> tolerances;

    // PSpice temperature metadata
    std::optional<double> t_measured;
    std::optional<double> t_abs;
    std::optional<double> t_rel_global;
    std::optional<double> t_rel_local;
};

ModelCard parse_model_card(const std::vector<std::string>& tokens);

/// Detect the MOSFET level from a parsed .model card.
/// Returns 1 for MOS1, 14 (default) for BSIM4v7.
/// Only valid for NMOS/PMOS type cards.
int detect_mosfet_level(const ModelCard& card);

/// Translate a parsed .model card (SW or CSW) into a SwitchModel.
/// card.type must be "sw" (voltage-controlled) or "csw" (current-controlled).
/// Throws ParseError for unsupported model types.
SwitchModel to_switch_model(const ModelCard& card);

/// Translate a parsed .model card (R) into a ResistorModel.
ResistorModel to_resistor_model(const ModelCard& card);

/// Translate a parsed .model card (C) into a CapacitorModel.
CapacitorModel to_capacitor_model(const ModelCard& card);

/// Translate a parsed .model card (L) into an InductorModel.
InductorModel to_inductor_model(const ModelCard& card);

// ---------------------------------------------------------------------------
// Backward-compatible forward declarations for per-device to_xxx_card()
// functions.  The implementations now live in each device's *_model_card.cpp
// file; callers that only need the declaration can include this header
// without pulling in the full device headers.
// ---------------------------------------------------------------------------
struct BSIM4v7ModelCard;
struct MOS1ModelCard;
struct MOS2ModelCard;
struct MOS3ModelCard;
struct MOS9ModelCard;
struct BSIM3ModelCard;
struct BSIM3v32ModelCard;
struct BJTModelCard;
struct JFETModelCard;
struct DIOModelCard;
struct VBICModelCard;
struct HFET2ModelCard;

std::unique_ptr<BSIM4v7ModelCard> to_bsim4_card(const ModelCard& card);
std::unique_ptr<MOS1ModelCard> to_mos1_card(const ModelCard& card);
std::unique_ptr<MOS2ModelCard> to_mos2_card(const ModelCard& card);
std::unique_ptr<MOS3ModelCard> to_mos3_card(const ModelCard& card);
std::unique_ptr<MOS9ModelCard> to_mos9_card(const ModelCard& card);
std::unique_ptr<BSIM3ModelCard> to_bsim3_card(const ModelCard& card);
std::unique_ptr<BSIM3v32ModelCard> to_bsim3v32_card(const ModelCard& card);
std::unique_ptr<BJTModelCard> to_bjt_card(const ModelCard& card);
std::unique_ptr<JFETModelCard> to_jfet_card(const ModelCard& card);
std::unique_ptr<DIOModelCard> to_dio_card(const ModelCard& card);
std::unique_ptr<VBICModelCard> to_vbic_card(const ModelCard& card);
std::unique_ptr<HFET2ModelCard> to_hfet2_card(const ModelCard& card);

} // namespace neospice
