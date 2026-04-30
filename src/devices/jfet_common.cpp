#include "devices/jfet_common.hpp"
#include "devices/jfet/jfet_model_card.hpp"
#include "devices/jfet2/jfet2_device.hpp"
#include "devices/jfet2/jfet2_model_card.hpp"
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

std::unique_ptr<ParsedElement> parse_jfet_element(
    const std::vector<std::string>& tokens, ParseContext& ctx)
{
    // J name drain gate source model [area] [off] [ic=VDS,VGS]
    if (tokens.size() < 5) {
        ctx.error("J card requires at least name, nd, ng, ns, model");
        return nullptr;
    }
    auto j = std::make_unique<ParsedJFET>();
    j->name = tokens[0];
    j->nd_str = tokens[1];
    j->ng_str = tokens[2];
    j->ns_str = tokens[3];
    j->model_name = tokens[4];
    j->line_number = ctx.line_number;
    // Parse remaining: area=, m=, ic=, off, or bare area number
    for (size_t i = 5; i < tokens.size(); ++i) {
        auto eq = tokens[i].find('=');
        if (eq != std::string::npos) {
            std::string key = to_lower(tokens[i].substr(0, eq));
            std::string valstr = tokens[i].substr(eq + 1);
            if (key == "area") {
                j->geom.area = parse_spice_number(valstr);
                j->geom.area_given = true;
            } else if (key == "m") {
                j->geom.m = parse_spice_number(valstr);
                j->geom.m_given = true;
            } else if (key == "ic") {
                // ic=VDS,VGS
                std::vector<double> icvals;
                size_t start = 0;
                while (start < valstr.size()) {
                    size_t comma = valstr.find(',', start);
                    if (comma == std::string::npos) comma = valstr.size();
                    std::string field = valstr.substr(start, comma - start);
                    if (!field.empty()) icvals.push_back(parse_spice_number(field));
                    start = comma + 1;
                }
                if (icvals.size() >= 1) { j->ic_vds = icvals[0]; j->ic_vds_given = true; }
                if (icvals.size() >= 2) { j->ic_vgs = icvals[1]; j->ic_vgs_given = true; }
            }
        } else {
            std::string lower = to_lower(tokens[i]);
            if (lower == "off") continue; // ignore OFF flag
            // Bare number = area factor (legacy SPICE2 syntax)
            if (!j->geom.area_given) {
                try {
                    j->geom.area = parse_spice_number(tokens[i]);
                    j->geom.area_given = true;
                } catch (...) {}
            }
        }
    }
    return j;
}

void resolve_jfets(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx)
{
    // Resolve deferred JFETs (J-cards dispatch to JFET or JFET2 based on LEVEL)
    // LEVEL=1 (default, or unspecified) -> JFET (Shichman-Hodges)
    // LEVEL=2                           -> JFET2 (Parker-Skellern)
    std::unordered_map<std::string, std::unique_ptr<JFETModelCard>> jfet_cards;
    std::unordered_map<std::string, std::unique_ptr<JFET2ModelCard>> jfet2_cards;

    for (const auto& elem : elements) {
        const auto& j = static_cast<const ParsedJFET&>(*elem);
        auto it = models.find(j.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(j.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(j.line_number) +
                                 ": Unknown model '" + j.model_name + "'");
            }
            it = it2;
        }
        // Check that the model type is NJF or PJF
        std::string model_type = to_lower(it->second.type);
        if (model_type != "njf" && model_type != "pjf") {
            throw ParseError("Line " + std::to_string(j.line_number) +
                             ": J card references non-JFET model '" + j.model_name + "'");
        }

        // Determine level: default=1 (JFET), 2 = JFET2 (Parker-Skellern)
        auto level_it = it->second.params.find("level");
        int level = (level_it == it->second.params.end()) ? 1
                    : static_cast<int>(level_it->second);

        if (level == 2) {
            // JFET2 (Parker-Skellern) model
            auto card_it = jfet2_cards.find(j.model_name);
            if (card_it == jfet2_cards.end()) {
                try {
                    card_it = jfet2_cards.emplace(j.model_name,
                                                   to_jfet2_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(j.line_number) +
                                     ": " + e.what());
                }
            }
            int32_t nd = ctx.node(j.nd_str);
            int32_t ng = ctx.node(j.ng_str);
            int32_t ns = ctx.node(j.ns_str);
            JFET2Device::Geom g2;
            g2.area = j.geom.area;
            g2.area_given = j.geom.area_given;
            g2.m = j.geom.m;
            g2.m_given = j.geom.m_given;
            auto dev = JFET2Device::make(j.name, nd, ng, ns,
                                          g2, *card_it->second);
            if (j.ic_vds_given || j.ic_vgs_given) {
                dev->set_ic(j.ic_vds, j.ic_vds_given, j.ic_vgs, j.ic_vgs_given);
            }
            ckt.add_device(std::move(dev));
        } else {
            // Standard JFET (level 1 or unspecified)
            auto card_it = jfet_cards.find(j.model_name);
            if (card_it == jfet_cards.end()) {
                try {
                    card_it = jfet_cards.emplace(j.model_name,
                                                  to_jfet_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(j.line_number) +
                                     ": " + e.what());
                }
            }
            int32_t nd = ctx.node(j.nd_str);
            int32_t ng = ctx.node(j.ng_str);
            int32_t ns = ctx.node(j.ns_str);
            auto dev = JFETDevice::make(j.name, nd, ng, ns,
                                         j.geom, *card_it->second);
            if (j.ic_vds_given || j.ic_vgs_given) {
                dev->set_ic(j.ic_vds, j.ic_vds_given, j.ic_vgs, j.ic_vgs_given);
            }
            ckt.add_device(std::move(dev));
        }
    }
    for (auto& [name, card] : jfet_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : jfet2_cards) {
        ckt.add_model_card(std::move(card));
    }
}

void register_jfet_parser(DeviceRegistry& reg) {
    DeviceRegistry::ElementParserEntry entry;
    entry.prefix = 'j';
    entry.parse = parse_jfet_element;
    entry.resolve = resolve_jfets;
    reg.add_element_parser(std::move(entry));
}

} // namespace neospice
