#include "devices/dio/dio_parser.hpp"
#include "devices/dio/dio_model_card.hpp"
#include "core/types.hpp"
#include "parser/model_cards.hpp"
#include "parser/tokenizer.hpp"
#include <algorithm>
#include <cstdio>

namespace neospice {

namespace {
std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}
} // anonymous namespace

std::unique_ptr<ParsedElement> parse_diode_element(
    const std::vector<std::string>& tokens, ParseContext& ctx)
{
    // D name anode cathode modelname [area=.. pj=.. m=.. ic=..]
    if (tokens.size() < 4) {
        ctx.error("Diode requires name, anode, cathode, modelname");
        return nullptr;
    }
    auto dd = std::make_unique<ParsedDiode>();
    dd->name = tokens[0];
    dd->anode_str = tokens[1];
    dd->cathode_str = tokens[2];
    dd->model_name = tokens[3];
    dd->line_number = ctx.line_number;
    for (size_t i = 4; i < tokens.size(); ++i) {
        auto eq = tokens[i].find('=');
        if (eq == std::string::npos) {
            // Bare number after model name = area factor
            try {
                dd->geom.area = std::stod(tokens[i]);
            } catch (...) {}
            continue;
        }
        std::string key = to_lower(tokens[i].substr(0, eq));
        double v = std::stod(tokens[i].substr(eq + 1));
        if (key == "area")     dd->geom.area = v;
        else if (key == "pj")  dd->geom.pj = v;
        else if (key == "w")   dd->geom.w = v;
        else if (key == "l")   dd->geom.l = v;
        else if (key == "m")   dd->geom.m = v;
        else if (key == "ic")  { dd->ic_vd = v; dd->ic_vd_given = true; }
    }
    return dd;
}

void resolve_diodes(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx)
{
    std::unordered_map<std::string, std::unique_ptr<DIOModelCard>> dio_cards;
    for (const auto& elem : elements) {
        const auto& dd = static_cast<const ParsedDiode&>(*elem);
        auto it = models.find(dd.model_name);
        if (it == models.end()) {
            throw ParseError("Line " + std::to_string(dd.line_number) +
                ": Unknown model '" + dd.model_name + "' for diode '" + dd.name + "'");
        }
        auto card_it = dio_cards.find(dd.model_name);
        if (card_it == dio_cards.end()) {
            card_it = dio_cards.emplace(dd.model_name,
                                        to_dio_card(it->second)).first;
        }
        int32_t na = ctx.node(dd.anode_str);
        int32_t nc = ctx.node(dd.cathode_str);
        auto dev = DIODevice::make(dd.name, na, nc, dd.geom, *card_it->second);
        if (dd.ic_vd_given) {
            dev->set_ic(dd.ic_vd, dd.ic_vd_given);
        }
        ckt.add_device(std::move(dev));
    }
    for (auto& [name, card] : dio_cards) {
        ckt.add_model_card(std::move(card));
    }
}

void register_dio_parser(DeviceRegistry& reg) {
    DeviceRegistry::ElementParserEntry entry;
    entry.prefix = 'd';
    entry.parse = parse_diode_element;
    entry.resolve = resolve_diodes;
    reg.add_element_parser(std::move(entry));
}

} // namespace neospice
