#pragma once
#include "devices/device_registry.hpp"
#include "devices/jfet/jfet_device.hpp"

namespace neospice {

struct ParsedJFET : ParsedElement {
    int32_t nd = GROUND_INTERNAL;
    int32_t ng = GROUND_INTERNAL;
    int32_t ns = GROUND_INTERNAL;
    JFETDevice::Geom geom;
    double ic_vds = 0, ic_vgs = 0;
    bool ic_vds_given = false, ic_vgs_given = false;
};

std::unique_ptr<ParsedElement> parse_jfet_element(
    const std::vector<std::string>& tokens, ParseContext& ctx);

void resolve_jfets(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx);

void register_jfet_parser(DeviceRegistry& reg);

} // namespace neospice
