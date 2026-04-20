#pragma once
#include "devices/switch.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"   // BSIM4v7ModelCard
#include "devices/bjt/bjt_device.hpp"            // BJTModelCard
#include "devices/jfet/jfet_device.hpp"          // JFETModelCard
#include "devices/dio/dio_device.hpp"            // DIOModelCard
#include "devices/vbic/vbic_device.hpp"          // VBICModelCard
#include "parser/tokenizer.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

struct ModelCard {
    std::string name;
    std::string type; // "d", "nmos", "pmos", "npn", "pnp" (lowercase)
    // Stored lowercase for case-insensitive lookup.  Values are parsed as
    // doubles; integer/flag BSIM4 parameters cast from double at dispatch.
    std::unordered_map<std::string, double> params;
};

ModelCard parse_model_card(const std::vector<std::string>& tokens);
/// Translate a parsed .model card (LEVEL=14 NMOS/PMOS) into a
/// BSIM4v7ModelCard using the UCB BSIM4mParam dispatcher.  The returned
/// card is heap-allocated so the parser can hand ownership to the Circuit
/// (the BSIM4v7Device holds a non-owning pointer back).
///
/// Throws ParseError for:
///   * non-NMOS/PMOS type
///   * LEVEL != 14
///   * unknown BSIM4 parameter keys are WARNED on stderr, not thrown.
std::unique_ptr<BSIM4v7ModelCard> to_bsim4_card(const ModelCard& card);

/// Translate a parsed .model card (NPN/PNP) into a BJTModelCard using
/// the UCB BJTmParam dispatcher.  Ownership semantics are the same as
/// for BSIM4v7 — the Circuit owns the card and BJTDevice holds a
/// non-owning pointer.
std::unique_ptr<BJTModelCard> to_bjt_card(const ModelCard& card);

/// Translate a parsed .model card (NJF/PJF) into a JFETModelCard using
/// the UCB JFETmParam dispatcher.
std::unique_ptr<JFETModelCard> to_jfet_card(const ModelCard& card);

/// Translate a parsed .model card (D) into a DIOModelCard using
/// the UCB DIOmParam dispatcher.
std::unique_ptr<DIOModelCard> to_dio_card(const ModelCard& card);

/// Translate a parsed .model card (NPN/PNP) into a VBICModelCard using
/// the UCB VBICmParam dispatcher.  Used for VBIC model levels (4, 9, 12, 13).
std::unique_ptr<VBICModelCard> to_vbic_card(const ModelCard& card);

/// Translate a parsed .model card (SW or CSW) into a SwitchModel.
/// card.type must be "sw" (voltage-controlled) or "csw" (current-controlled).
/// Throws ParseError for unsupported model types.
SwitchModel to_switch_model(const ModelCard& card);

} // namespace neospice
