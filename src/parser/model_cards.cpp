#include "parser/model_cards.hpp"
#include "core/types.hpp"
#include <algorithm>
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
    }

    return model;
}

BSIM4v7Params to_bsim4v7_params(const ModelCard& card) {
    BSIM4v7Params p;
    p.name = card.name;
    p.is_pmos = (card.type == "pmos");

    for (const auto& [key, val] : card.params) {
        std::string k = key;  // already lowercase from parser
        if (k == "vth0") p.VTH0 = val;
        else if (k == "k1") p.K1 = val;
        else if (k == "k2") p.K2 = val;
        else if (k == "u0") p.U0 = val;
        else if (k == "ua") p.UA = val;
        else if (k == "ub") p.UB = val;
        else if (k == "uc") p.UC = val;
        else if (k == "vsat") p.VSAT = val;
        else if (k == "toxe") p.TOXE = val;
        else if (k == "toxp") p.TOXP = val;
        else if (k == "toxm") p.TOXM = val;
        else if (k == "ndep") p.NDEP = val;
        else if (k == "nfactor") p.NFACTOR = val;
        else if (k == "eta0") p.ETA0 = val;
        else if (k == "dsub") p.DSUB = val;
        else if (k == "pclm") p.PCLM = val;
        else if (k == "pdiblc1") p.PDIBLC1 = val;
        else if (k == "pdiblc2") p.PDIBLC2 = val;
        else if (k == "delta") p.DELTA = val;
        else if (k == "rdsw") p.RDSW = val;
        else if (k == "cgso") p.CGSO = val;
        else if (k == "cgdo") p.CGDO = val;
        else if (k == "cgbo") p.CGBO = val;
        else if (k == "cj") p.CJ = val;
        else if (k == "cjsw") p.CJSW = val;
        else if (k == "cjswg") p.CJSWG = val;
        else if (k == "mj") p.MJ = val;
        else if (k == "mjsw") p.MJSW = val;
        else if (k == "pb") p.PB = val;
        else if (k == "pbsw") p.PBSW = val;
        else if (k == "tnom") p.TNOM = val;
        else if (k == "a0") p.A0 = val;
        else if (k == "ags") p.AGS = val;
        else if (k == "b0") p.B0 = val;
        else if (k == "b1") p.B1 = val;
        else if (k == "k3b") p.K3B = val;
        else if (k == "keta") p.KETA = val;
        else if (k == "prwb") p.PRWB = val;
        else if (k == "prwg") p.PRWG = val;
        else if (k == "rdswmin") p.RDSWMIN = val;
        else if (k == "xj") p.XJ = val;
        else if (k == "eu") p.EU = val;
        // ... additional params can be added as needed
    }
    return p;
}

} // namespace neospice
