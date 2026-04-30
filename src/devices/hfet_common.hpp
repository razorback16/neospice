#pragma once
#include "devices/device_registry.hpp"

namespace neospice {

struct ParsedHFET : ParsedElement {
    std::string nd_str, ng_str, ns_str;  // drain, gate, source node names
    double length = 1e-6, width = 20e-6, m = 1.0, area = 1.0;
    bool length_given = false, width_given = false, m_given = false, area_given = false;
    double ic_vds = 0, ic_vgs = 0;
    bool ic_vds_given = false, ic_vgs_given = false;
};

std::unique_ptr<ParsedElement> parse_hfet_element(
    const std::vector<std::string>& tokens, ParseContext& ctx);

void resolve_hfets(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx);

void register_hfet_parser(DeviceRegistry& reg);

} // namespace neospice
