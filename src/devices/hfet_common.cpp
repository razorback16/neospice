#include "devices/hfet_common.hpp"
#include "devices/hfet1/hfet1_device.hpp"
#include "devices/hfet1/hfet1_model_card.hpp"
#include "devices/hfet2/hfet2_device.hpp"
#include "devices/hfet2/hfet2_model_card.hpp"
#include "devices/mes/mes_device.hpp"
#include "devices/mes/mes_model_card.hpp"
#include "parser/model_cards.hpp"
#include "parser/tokenizer.hpp"
#include <algorithm>

namespace neospice {

namespace {
std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}
} // anonymous namespace

std::unique_ptr<ParsedElement> parse_hfet_element(
    const std::vector<std::string>& tokens, ParseContext& ctx)
{
    // Z name drain gate source model [L=val] [W=val] [M=val] [off] [ic=VDS,VGS]
    if (tokens.size() < 5) {
        ctx.error("Z card requires at least name, nd, ng, ns, model");
        return nullptr;
    }
    auto z = std::make_unique<ParsedHFET>();
    z->name = tokens[0];
    z->nd_str = tokens[1];
    z->ng_str = tokens[2];
    z->ns_str = tokens[3];
    z->model_name = tokens[4];
    z->line_number = ctx.line_number;
    for (size_t i = 5; i < tokens.size(); ++i) {
        auto eq = tokens[i].find('=');
        if (eq != std::string::npos) {
            std::string key = to_lower(tokens[i].substr(0, eq));
            std::string valstr = tokens[i].substr(eq + 1);
            if (key == "l") {
                z->length = parse_spice_number(valstr); z->length_given = true;
            } else if (key == "w") {
                z->width = parse_spice_number(valstr); z->width_given = true;
            } else if (key == "m") {
                z->m = parse_spice_number(valstr); z->m_given = true;
            } else if (key == "area") {
                z->area = parse_spice_number(valstr); z->area_given = true;
            } else if (key == "ic") {
                std::vector<double> icvals;
                size_t start = 0;
                while (start < valstr.size()) {
                    size_t comma = valstr.find(',', start);
                    if (comma == std::string::npos) comma = valstr.size();
                    std::string field = valstr.substr(start, comma - start);
                    if (!field.empty()) icvals.push_back(parse_spice_number(field));
                    start = comma + 1;
                }
                if (icvals.size() >= 1) { z->ic_vds = icvals[0]; z->ic_vds_given = true; }
                if (icvals.size() >= 2) { z->ic_vgs = icvals[1]; z->ic_vgs_given = true; }
            }
        } else {
            std::string lower = to_lower(tokens[i]);
            if (lower == "off") continue;
            if (!z->area_given) {
                try {
                    z->area = parse_spice_number(tokens[i]);
                    z->area_given = true;
                } catch (...) {}
            }
        }
    }
    return z;
}

void resolve_hfets(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx)
{
    // Resolve deferred HFETs (Z elements with nhfet/phfet)
    // LEVEL=5 or default -> HFET1, LEVEL=6 -> HFET2
    std::unordered_map<std::string, std::unique_ptr<MESModelCard>> mes_cards;
    std::unordered_map<std::string, std::unique_ptr<HFETAModelCard>> hfet1_cards;
    std::unordered_map<std::string, std::unique_ptr<HFET2ModelCard>> hfet2_cards;

    for (const auto& elem : elements) {
        const auto& z = static_cast<const ParsedHFET&>(*elem);
        auto it = models.find(z.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(z.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(z.line_number) +
                                 ": Unknown model '" + z.model_name + "'");
            }
            it = it2;
        }
        std::string model_type = to_lower(it->second.type);
        if (model_type != "nhfet" && model_type != "phfet" &&
            model_type != "nmf" && model_type != "pmf") {
            throw ParseError("Line " + std::to_string(z.line_number) +
                             ": Z card references unknown model type '" + it->second.type + "'");
        }
        int level = 5; // default: HFET1
        auto lvl_it = it->second.params.find("level");
        if (lvl_it != it->second.params.end()) {
            level = static_cast<int>(lvl_it->second);
        }
        int32_t nd = ctx.node(z.nd_str);
        int32_t ng = ctx.node(z.ng_str);
        int32_t ns = ctx.node(z.ns_str);

        if (model_type == "nmf" || model_type == "pmf") {
            auto card_it = mes_cards.find(z.model_name);
            if (card_it == mes_cards.end()) {
                try {
                    card_it = mes_cards.emplace(z.model_name,
                                                to_mes_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(z.line_number) +
                                     ": " + e.what());
                }
            }
            MESDevice::Geom geom;
            geom.area = z.area_given ? z.area : 1.0;
            geom.m = z.m_given ? z.m : 1.0;
            auto dev = MESDevice::make(z.name, nd, ng, ns,
                                       geom, *card_it->second);
            if (z.ic_vds_given || z.ic_vgs_given) {
                dev->set_ic(z.ic_vds, z.ic_vds_given, z.ic_vgs, z.ic_vgs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 6) {
            auto card_it = hfet2_cards.find(z.model_name);
            if (card_it == hfet2_cards.end()) {
                try {
                    card_it = hfet2_cards.emplace(z.model_name,
                                                  to_hfet2_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(z.line_number) +
                                     ": " + e.what());
                }
            }
            HFET2Device::Geom geom;
            geom.L = z.length; geom.W = z.width; geom.M = z.m;
            auto dev = HFET2Device::make(z.name, nd, ng, ns,
                                         geom, *card_it->second);
            if (z.ic_vds_given || z.ic_vgs_given) {
                dev->set_ic(z.ic_vds, z.ic_vds_given, z.ic_vgs, z.ic_vgs_given);
            }
            ckt.add_device(std::move(dev));
        } else {
            // LEVEL=5 or default -> HFET1
            auto card_it = hfet1_cards.find(z.model_name);
            if (card_it == hfet1_cards.end()) {
                try {
                    card_it = hfet1_cards.emplace(z.model_name,
                                                  to_hfet1_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(z.line_number) +
                                     ": " + e.what());
                }
            }
            HFETADevice::Geom geom;
            geom.length = z.length; geom.width = z.width; geom.m = z.m;
            geom.length_given = z.length_given;
            geom.width_given = z.width_given;
            geom.m_given = z.m_given;
            auto dev = HFETADevice::make(z.name, nd, ng, ns,
                                         geom, *card_it->second);
            if (z.ic_vds_given || z.ic_vgs_given) {
                dev->set_ic(z.ic_vds, z.ic_vds_given, z.ic_vgs, z.ic_vgs_given);
            }
            ckt.add_device(std::move(dev));
        }
    }
    for (auto& [name, card] : mes_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : hfet1_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : hfet2_cards) {
        ckt.add_model_card(std::move(card));
    }
}

void register_hfet_parser(DeviceRegistry& reg) {
    DeviceRegistry::ElementParserEntry entry;
    entry.prefix = 'z';
    entry.parse = parse_hfet_element;
    entry.resolve = resolve_hfets;
    reg.add_element_parser(std::move(entry));
}

} // namespace neospice
