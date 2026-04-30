#pragma once
#include "devices/device_registry.hpp"
#include "devices/dio/dio_device.hpp"

namespace neospice {

struct ParsedDiode : ParsedElement {
    std::string anode_str, cathode_str;
    DIODevice::Geom geom;
    double ic_vd = 0.0;
    bool ic_vd_given = false;
};

std::unique_ptr<ParsedElement> parse_diode_element(
    const std::vector<std::string>& tokens, ParseContext& ctx);

void resolve_diodes(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx);

void register_dio_parser(DeviceRegistry& reg);

} // namespace neospice
