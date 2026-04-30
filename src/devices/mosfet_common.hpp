#pragma once
#include "devices/device_registry.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"

namespace neospice {

struct ParsedMosfet : ParsedElement {
    int32_t nd, ng, ns, nb;
    int32_t nsub = GROUND_INTERNAL;
    bool nsub_given = false;
    int32_t npnode = GROUND_INTERNAL;
    int32_t nbulk = GROUND_INTERNAL;
    bool nbulk_given = false;
    BSIM4v7Device::Geom geom;
    // Instance initial conditions from ic=VDS,VGS,VBS
    double ic_vds = 0.0, ic_vgs = 0.0, ic_vbs = 0.0;
    bool ic_vds_given = false, ic_vgs_given = false, ic_vbs_given = false;
};

std::unique_ptr<ParsedElement> parse_mosfet_element(
    const std::vector<std::string>& tokens, ParseContext& ctx);

void resolve_mosfets(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx);

void register_mosfet_parser(DeviceRegistry& reg);

} // namespace neospice
