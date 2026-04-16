#pragma once
#include "devices/diode.hpp"
#include "devices/bsim4v7/bsim4v7_params.hpp"
#include "parser/tokenizer.hpp"
#include <string>
#include <unordered_map>

namespace neospice {

struct ModelCard {
    std::string name;
    std::string type; // "d", "nmos", "pmos" (lowercase)
    std::unordered_map<std::string, double> params;
};

ModelCard parse_model_card(const std::vector<std::string>& tokens);
DiodeModel to_diode_model(const ModelCard& card);
BSIM4v7Params to_bsim4v7_params(const ModelCard& card);

} // namespace neospice
