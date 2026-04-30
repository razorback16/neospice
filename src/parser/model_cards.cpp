#include "parser/model_cards.hpp"
#include "core/types.hpp"
#include "devices/switch.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cctype>

namespace neospice {

static std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

ModelCard parse_model_card(const std::vector<std::string>& tokens) {
    // tokens[0] = ".model", tokens[1] = name, tokens[2..] = TYPE(key=val ...)
    if (tokens.size() < 3) {
        throw ParseError(".model: insufficient tokens");
    }

    ModelCard card;
    card.name = tokens[1];

    // Join remaining tokens to handle TYPE(k=v k=v) or TYPE ( k=v )
    std::string rest;
    for (size_t i = 2; i < tokens.size(); ++i) {
        if (!rest.empty()) rest += ' ';
        rest += tokens[i];
    }

    // Find the type name. In SPICE, the .model line is either
    //   .model NAME TYPE(k=v ...)       (parenthesized)
    //   .model NAME TYPE k=v k=v ...    (bare — paren-less form)
    // In both cases, TYPE is the first whitespace-delimited token of `rest`.
    size_t paren_pos = rest.find('(');
    std::string type_str;
    std::string params_str;
    if (paren_pos == std::string::npos) {
        // No parens: first token is type, everything after is params
        size_t first_space = rest.find_first_of(" \t");
        if (first_space == std::string::npos) {
            type_str = rest;
            params_str = "";
        } else {
            type_str = rest.substr(0, first_space);
            params_str = rest.substr(first_space + 1);
        }
    } else {
        // Parens: type is everything before '(' (trimmed)
        type_str = rest.substr(0, paren_pos);
        size_t close_paren = rest.rfind(')');
        if (close_paren != std::string::npos && close_paren > paren_pos) {
            params_str = rest.substr(paren_pos + 1, close_paren - paren_pos - 1);
        } else {
            params_str = rest.substr(paren_pos + 1);
        }
    }
    // Trim whitespace from type
    while (!type_str.empty() && std::isspace(static_cast<unsigned char>(type_str.back())))
        type_str.pop_back();
    card.type = to_lower(type_str);

    // Parse parameters (from paren block or bare params)
    if (!params_str.empty()) {

        // Replace '=' with ' = ' for easier parsing, then split
        std::string normalized;
        for (char c : params_str) {
            if (c == '=') {
                normalized += " = ";
            } else {
                normalized += c;
            }
        }

        std::istringstream iss(normalized);
        std::string tok;
        std::vector<std::string> ptokens;
        while (iss >> tok) {
            ptokens.push_back(tok);
        }

        // Parse key=value pairs: expect key, =, value.
        // Bare tokens without '=' are treated as flag parameters (value = 1.0).
        // This handles LTRA flags like nocontrol, steplimit, lininterp, etc.
        for (size_t i = 0; i < ptokens.size(); ) {
            std::string key = to_lower(ptokens[i]);
            if (i + 2 < ptokens.size() && ptokens[i + 1] == "=") {
                // key = value triplet
                double val = parse_spice_number(ptokens[i + 2]);
                card.params[key] = val;
                i += 3;
            } else {
                // Bare token — flag parameter (e.g., nocontrol, steplimit)
                card.params[key] = 1.0;
                i += 1;
            }
        }
    }

    return card;
}

// ---------------------------------------------------------------------------
// detect_mosfet_level — return the LEVEL from a NMOS/PMOS .model card.
// ---------------------------------------------------------------------------
int detect_mosfet_level(const ModelCard& card) {
    auto it = card.params.find("level");
    if (it == card.params.end()) return 1;  // default MOS1 (matches ngspice)
    return static_cast<int>(it->second);
}

// ---------------------------------------------------------------------------
// to_switch_model — parse a .model SW or CSW card into a SwitchModel.
// ---------------------------------------------------------------------------
SwitchModel to_switch_model(const ModelCard& card) {
    SwitchModel model;
    model.name = card.name;

    if (card.type == "sw") {
        model.is_voltage_controlled = true;
    } else if (card.type == "csw") {
        model.is_voltage_controlled = false;
    } else {
        throw ParseError(
            "Model '" + card.name + "': unsupported switch type '" + card.type +
            "' (only SW/CSW supported)");
    }

    for (const auto& [key, val] : card.params) {
        if (model.is_voltage_controlled) {
            if      (key == "vt")   model.Vt   = val;
            else if (key == "vh")   model.Vh   = val;
            else if (key == "ron")  model.Ron  = val;
            else if (key == "roff") model.Roff = val;
        } else {
            // CSW uses It/Ih instead of Vt/Vh
            if      (key == "it")   model.Vt   = val;
            else if (key == "ih")   model.Vh   = val;
            else if (key == "ron")  model.Ron  = val;
            else if (key == "roff") model.Roff = val;
        }
    }

    return model;
}

// ---------------------------------------------------------------------------
// to_resistor_model — parse a .model R card into a ResistorModel.
// ---------------------------------------------------------------------------
ResistorModel to_resistor_model(const ModelCard& card) {
    ResistorModel m;
    for (const auto& [key, val] : card.params) {
        if (key == "tc1") m.tc1 = val;
        else if (key == "tc2") m.tc2 = val;
        else if (key == "rac") m.rac = val;
        else if (key == "kf") m.kf = val;
        else if (key == "af") m.af = val;
        else if (key == "tnom") m.tnom = val + 273.15;
    }
    return m;
}

// ---------------------------------------------------------------------------
// to_capacitor_model — parse a .model C card into a CapacitorModel.
// ---------------------------------------------------------------------------
CapacitorModel to_capacitor_model(const ModelCard& card) {
    CapacitorModel m;
    for (const auto& [key, val] : card.params) {
        if (key == "tc1") m.tc1 = val;
        else if (key == "tc2") m.tc2 = val;
        else if (key == "vc1") m.vc1 = val;
        else if (key == "vc2") m.vc2 = val;
        else if (key == "tnom") m.tnom = val + 273.15;
    }
    return m;
}

// ---------------------------------------------------------------------------
// to_inductor_model — parse a .model L card into an InductorModel.
// ---------------------------------------------------------------------------
InductorModel to_inductor_model(const ModelCard& card) {
    InductorModel m;
    for (const auto& [key, val] : card.params) {
        if (key == "tc1") m.tc1 = val;
        else if (key == "tc2") m.tc2 = val;
        else if (key == "tnom") m.tnom = val + 273.15;
    }
    return m;
}

} // namespace neospice
