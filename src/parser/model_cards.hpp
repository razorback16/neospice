#pragma once
#include "devices/diode.hpp"
#include "parser/tokenizer.hpp"
#include <string>
#include <unordered_map>

namespace cudaspice {

struct ModelCard {
    std::string name;
    std::string type; // "d", "nmos", "pmos" (lowercase)
    std::unordered_map<std::string, double> params;
};

ModelCard parse_model_card(const std::vector<std::string>& tokens);
DiodeModel to_diode_model(const ModelCard& card);

} // namespace cudaspice
