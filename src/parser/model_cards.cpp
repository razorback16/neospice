#include "parser/model_cards.hpp"
#include "core/types.hpp"
#include "devices/switch.hpp"
#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include "devices/bjt/bjt_def.hpp"
#include "devices/bjt/bjt_shim.hpp"
#include "devices/jfet/jfet_def.hpp"
#include "devices/jfet/jfet_shim.hpp"
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

        // Parse key=value pairs: expect key, =, value
        for (size_t i = 0; i + 2 < ptokens.size(); i += 3) {
            std::string key = to_lower(ptokens[i]);
            // ptokens[i+1] should be "="
            if (ptokens[i + 1] != "=") {
                throw ParseError(".model: expected '=' after key '" + ptokens[i] + "'");
            }
            double val = parse_spice_number(ptokens[i + 2]);
            card.params[key] = val;
        }
    }

    return card;
}

DiodeModel to_diode_model(const ModelCard& card) {
    DiodeModel model;
    model.name = card.name;

    for (const auto& [key, val] : card.params) {
        if (key == "is")       model.Is  = val;
        else if (key == "n")   model.N   = val;
        else if (key == "cj0" || key == "cjo") model.Cj0 = val;
        else if (key == "vj")  model.Vj  = val;
        else if (key == "m")   model.M   = val;
        else if (key == "tt")  model.Tt  = val;
        else if (key == "bv")  model.Bv  = val;
        else if (key == "ibv") model.Ibv = val;
        // Flicker (1/f) noise parameters
        else if (key == "kf")  model.Kf  = val;
        else if (key == "af")  model.Af  = val;
        else if (key == "ef")  model.Ef  = val;
    }

    return model;
}

// ---------------------------------------------------------------------------
// to_bsim4_card — parse a .model LEVEL=14 card into a BSIM4v7ModelCard via
// the UCB BSIM4mParam dispatcher.
//
// Lifetime note: BSIM4mParam copies IfValue::iValue / rValue by value, so
// we can safely stack-allocate IfValue per-parameter.  The only escape is
// IF_STRING (version): UCB stores the char* by reference, so we hand it a
// pointer into the parser's long-lived string literals table (there's
// exactly one such key — "version" — and we set it to "4.7.0" unconditionally
// from the adapter's make() path, so .model VERSION=... is unsupported here).
// ---------------------------------------------------------------------------
std::unique_ptr<BSIM4v7ModelCard> to_bsim4_card(const ModelCard& card) {
    auto out = std::make_unique<BSIM4v7ModelCard>();
    auto& ucb = out->ucb;

    // Type check — .model TYPE field is lowercased at parse time.
    if (card.type == "nmos") {
        ucb.BSIM4v7type      = 1;
        ucb.BSIM4v7typeGiven = 1;
    } else if (card.type == "pmos") {
        ucb.BSIM4v7type      = -1;
        ucb.BSIM4v7typeGiven = 1;
    } else {
        throw ParseError(
            "Model '" + card.name + "': unsupported MOS type '" + card.type +
            "' (only NMOS/PMOS supported)");
    }

    // LEVEL check — LEVEL is a SPICE frontend parameter, not in BSIM4mPTable.
    // Default to 14 when not specified (this matches the T8 plan semantics:
    // the M-card parser only routes to BSIM4v7 when the .model card
    // declares NMOS/PMOS, so a missing LEVEL is interpreted as "user
    // intends BSIM4v7").
    auto level_it = card.params.find("level");
    int level = (level_it == card.params.end()) ? 14
                                                : static_cast<int>(level_it->second);
    if (level != 14) {
        throw ParseError(
            "Model '" + card.name +
            "': only LEVEL=14 (BSIM4v7) is supported; got LEVEL=" +
            std::to_string(level));
    }

    // Walk the BSIM4 model parameter table, dispatching each matching
    // parsed key through BSIM4mParam.  Unknown keys are warned on stderr
    // (BSIM4 has >300 params; parser permissiveness beats hard failure).
    for (const auto& [lkey, val] : card.params) {
        // LEVEL handled above; BSIM4 has no LEVEL entry in mPTable.
        if (lkey == "level") continue;

        // Linear search through BSIM4mPTable.  The table is ~500 entries;
        // parser code runs once per circuit so this is not hot.
        // mPTable keywords are stored lowercase — matches ModelCard::params.
        const bsim4v7::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < bsim4v7::BSIM4v7mPTSize; ++i) {
            if (std::strcmp(bsim4v7::BSIM4v7mPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &bsim4v7::BSIM4v7mPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr,
                "Warning: model '%s': unknown BSIM4 parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        bsim4v7::Shim::IfValue v{};
        // Mask the data-type bits so IF_SET/IF_ASK flags don't confuse us.
        int dtype = entry->dataType & 0x1F;  // IF_REAL|INTEGER|STRING|FLAG|REALVEC
        if (dtype & bsim4v7::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & bsim4v7::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & bsim4v7::Shim::IF_FLAG) {
            // Parser got e.g. "NMOS=1" — flag is "present" when nonzero.
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & bsim4v7::Shim::IF_STRING) {
            // ModelCard stores params as doubles; a string parameter (VERSION)
            // cannot round-trip through that representation.  Skip it — the
            // adapter stamps VERSION="4.7.0" in BSIM4v7Device::make() anyway.
            std::fprintf(stderr,
                "Warning: model '%s': string parameter '%s' not supported via .model; "
                "using adapter default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else if (dtype & bsim4v7::Shim::IF_REALVEC) {
            throw ParseError(
                "Model '" + card.name + "': real-vector parameter '" + lkey +
                "' not supported in Phase-1b");
        } else {
            throw ParseError(
                "Model '" + card.name + "': parameter '" + lkey +
                "' has unrecognized data type in BSIM4mPTable");
        }

        int rc = bsim4v7::BSIM4v7mParam(entry->id, &v, &ucb);
        if (rc != bsim4v7::Shim::OK) {
            throw ParseError(
                "Model '" + card.name + "': BSIM4mParam failed for '" +
                lkey + "' (rc=" + std::to_string(rc) + ")");
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// to_bjt_card — parse a .model NPN/PNP card into a BJTModelCard via
// the UCB BJTmParam dispatcher.
// ---------------------------------------------------------------------------
std::unique_ptr<BJTModelCard> to_bjt_card(const ModelCard& card) {
    auto out = std::make_unique<BJTModelCard>();
    auto& ucb = out->ucb;

    // Type check — card.type is lowercased at parse time.
    if (card.type == "npn") {
        ucb.BJTtype = 1;   // NPN
    } else if (card.type == "pnp") {
        ucb.BJTtype = -1;  // PNP
    } else {
        throw ParseError(
            "Model '" + card.name + "': unsupported BJT type '" + card.type +
            "' (only NPN/PNP supported)");
    }

    // Walk the BJT model parameter table, dispatching each matching
    // parsed key through BJTmParam.
    for (const auto& [lkey, val] : card.params) {
        // Skip "level" — not a BJT model parameter.
        if (lkey == "level") continue;

        const bjt::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < bjt::BJTmPTSize; ++i) {
            if (std::strcmp(bjt::BJTmPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &bjt::BJTmPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr,
                "Warning: model '%s': unknown BJT parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        bjt::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & bjt::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & bjt::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & bjt::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & bjt::Shim::IF_STRING) {
            // Skip string params — BJT model has "type" as string which
            // we handle via the NPN/PNP type field above.
            continue;
        } else {
            std::fprintf(stderr,
                "Warning: model '%s': parameter '%s' has unrecognized data type (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        int rc = bjt::BJTmParam(entry->id, &v, &ucb);
        if (rc != bjt::Shim::OK) {
            throw ParseError(
                "Model '" + card.name + "': BJTmParam failed for '" +
                lkey + "' (rc=" + std::to_string(rc) + ")");
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// to_jfet_card — parse a .model NJF/PJF card into a JFETModelCard via
// the UCB JFETmParam dispatcher.
// ---------------------------------------------------------------------------
std::unique_ptr<JFETModelCard> to_jfet_card(const ModelCard& card) {
    auto out = std::make_unique<JFETModelCard>();
    auto& ucb = out->ucb;

    // Type check — card.type is lowercased at parse time.
    if (card.type == "njf") {
        ucb.JFETtype = 1;   // NJF
    } else if (card.type == "pjf") {
        ucb.JFETtype = -1;  // PJF
    } else {
        throw ParseError(
            "Model '" + card.name + "': unsupported JFET type '" + card.type +
            "' (only NJF/PJF supported)");
    }

    // Walk the JFET model parameter table, dispatching each matching
    // parsed key through JFETmParam.
    for (const auto& [lkey, val] : card.params) {
        // Skip "level" — not a JFET model parameter.
        if (lkey == "level") continue;

        const jfet::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < jfet::JFETmPTSize; ++i) {
            if (std::strcmp(jfet::JFETmPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &jfet::JFETmPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr,
                "Warning: model '%s': unknown JFET parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        jfet::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & jfet::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & jfet::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & jfet::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & jfet::Shim::IF_STRING) {
            // Skip string params
            continue;
        } else {
            std::fprintf(stderr,
                "Warning: model '%s': parameter '%s' has unrecognized data type (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        int rc = jfet::JFETmParam(entry->id, &v, &ucb);
        if (rc != jfet::Shim::OK) {
            throw ParseError(
                "Model '" + card.name + "': JFETmParam failed for '" +
                lkey + "' (rc=" + std::to_string(rc) + ")");
        }
    }

    return out;
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

} // namespace neospice
