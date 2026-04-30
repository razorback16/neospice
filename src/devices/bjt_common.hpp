#pragma once
#include "devices/device_registry.hpp"
#include "devices/bjt/bjt_device.hpp"

namespace neospice {

struct ParsedBJT : ParsedElement {
    int32_t nc, nb, ne, ns;  // collector, base, emitter, substrate
    BJTDevice::Geom geom;
    double ic_vbe = 0.0, ic_vce = 0.0;
    bool ic_vbe_given = false, ic_vce_given = false;
};

std::unique_ptr<ParsedElement> parse_bjt_element(
    const std::vector<std::string>& tokens, ParseContext& ctx);

void resolve_bjts(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx);

void register_bjt_parser(DeviceRegistry& reg);

} // namespace neospice
