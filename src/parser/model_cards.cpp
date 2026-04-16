#include "parser/model_cards.hpp"
#include "core/types.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>

namespace cudaspice {

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

    // Find the type name (everything before optional '(')
    size_t paren_pos = rest.find('(');
    std::string type_str;
    if (paren_pos == std::string::npos) {
        type_str = rest;
    } else {
        type_str = rest.substr(0, paren_pos);
    }
    // Trim whitespace from type
    while (!type_str.empty() && std::isspace(static_cast<unsigned char>(type_str.back())))
        type_str.pop_back();
    card.type = to_lower(type_str);

    // Parse parameters inside parentheses
    if (paren_pos != std::string::npos) {
        size_t close_paren = rest.rfind(')');
        std::string params_str;
        if (close_paren != std::string::npos && close_paren > paren_pos) {
            params_str = rest.substr(paren_pos + 1, close_paren - paren_pos - 1);
        } else {
            params_str = rest.substr(paren_pos + 1);
        }

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

} // namespace cudaspice
