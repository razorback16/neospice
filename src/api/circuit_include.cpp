#include "core/circuit.hpp"
#include "core/circuit_defs.hpp"   // DefinitionStore complete type
#include "parser/netlist_parser.hpp"
#include "parser/parse_state.hpp"
#include "devices/device_registry.hpp"
#include "parser/model_cards.hpp"
#include "parser/tokenizer.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace neospice {

namespace {

std::string lower_copy(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

int model_level(const ModelCard& card, int default_level) {
    auto it = card.params.find("level");
    return it == card.params.end() ? default_level : static_cast<int>(it->second);
}

} // namespace

void Circuit::include(const std::string& filepath) {
    if (finalized_)
        throw std::logic_error("Cannot include after circuit is finalized");
    if (!def_store_)
        def_store_ = std::make_unique<DefinitionStore>();

    NetlistParser parser;
    auto loaded = parser.load_definitions(filepath);

    // Merge into stored defs
    for (auto& [k, v] : loaded.subcircuit_defs)
        def_store_->defs.subcircuit_defs[k] = std::move(v);
    for (auto& [k, v] : loaded.models)
        def_store_->defs.models[k] = std::move(v);
    for (auto& [k, v] : loaded.func_defs)
        def_store_->defs.func_defs[k] = std::move(v);
    for (auto& [k, v] : loaded.params)
        def_store_->defs.params[k] = v;

    // Register model cards immediately so ckt.D(), ckt.M() etc. can find them.
    auto& reg = DeviceRegistry::get_default();
    for (auto& [name, card] : loaded.models) {
        auto type = lower_copy(card.type);
        int level = 0;
        if (type == "nmos" || type == "pmos") {
            level = detect_mosfet_level(card);
        } else if (type == "npn" || type == "pnp") {
            level = model_level(card, 1);
        } else if (type == "njf" || type == "pjf") {
            level = model_level(card, 1);
        }
        // d, nhfet, phfet, nmf, pmf use level=0

        // Only register if not already present
        if (find_model_holder(card.name) == nullptr) {
            auto holder = reg.create_model_card(type, level, card);
            if (holder)
                add_model_card_raw(std::move(holder), card.name, type);
        }
    }
}

void Circuit::X(const std::string& instance_name,
                const std::string& subckt_name,
                const std::vector<std::string>& port_nodes,
                const std::unordered_map<std::string, std::string>& params) {
    if (finalized_)
        throw std::logic_error("Cannot add subcircuit instance after circuit is finalized");
    if (!def_store_)
        throw std::invalid_argument("Circuit::X: no definitions loaded (call include() first)");

    NetlistParser parser;
    parser.expand_subcircuit_into(*this, instance_name, subckt_name,
                                  port_nodes, def_store_->defs, params);
}

} // namespace neospice
