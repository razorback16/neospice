#include "devices/bjt_common.hpp"
#include "devices/bjt/bjt_model_card.hpp"
#include "devices/vbic/vbic_device.hpp"
#include "devices/vbic/vbic_model_card.hpp"
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

std::unique_ptr<ParsedElement> parse_bjt_element(
    const std::vector<std::string>& tokens, ParseContext& ctx)
{
    // Q name nc nb ne [ns] modelname [area=X] [areab=X] [areac=X] [m=X] [ic=VBE,VCE] [off]
    if (tokens.size() < 5) {
        ctx.error("Q card requires at least name, nc, nb, ne, model");
        return nullptr;
    }
    auto q = std::make_unique<ParsedBJT>();
    q->name = tokens[0];
    q->nb = ctx.node(tokens[2]);
    q->ne = ctx.node(tokens[3]);
    q->line_number = ctx.line_number;

    // Determine if token[4] is a substrate node or model name.
    // Heuristic: if token[4] matches a known .model name, treat it as model.
    // Otherwise treat as substrate node and look for model as next token.
    std::string tok4 = tokens[4];
    std::string tok4_lower = to_lower(tok4);
    // Case-insensitive model name lookup
    bool is_model_name = false;
    for (const auto& [mname, _] : ctx.models) {
        if (to_lower(mname) == tok4_lower) {
            is_model_name = true;
            break;
        }
    }
    size_t param_start;
    if (is_model_name) {
        q->nc = ctx.node(tokens[1]);
        q->ns = ctx.node("0");  // default substrate = ground
        q->model_name = tok4;
        param_start = 5;
    } else if (tokens.size() >= 6) {
        q->nc = ctx.node(tokens[1]);
        q->ns = ctx.node(tok4);
        q->model_name = tokens[5];
        param_start = 6;
    } else {
        ctx.error("Q card: cannot determine model name");
        return nullptr;
    }

    // Parse optional parameters: area, areab, areac, m, ic, off
    for (size_t i = param_start; i < tokens.size(); ++i) {
        auto eq = tokens[i].find('=');
        if (eq == std::string::npos) {
            std::string lower = to_lower(tokens[i]);
            if (lower == "off") continue; // ignore OFF flag
            // Bare number = area factor (legacy SPICE2 syntax)
            try {
                q->geom.area = parse_spice_number(tokens[i]);
                q->geom.area_given = true;
            } catch (...) {}
            continue;
        }
        std::string key = to_lower(tokens[i].substr(0, eq));
        std::string valstr = tokens[i].substr(eq + 1);
        if (key == "ic") {
            // ic=VBE,VCE
            std::vector<double> icvals;
            size_t start = 0;
            while (start < valstr.size()) {
                size_t comma = valstr.find(',', start);
                if (comma == std::string::npos) comma = valstr.size();
                std::string field = valstr.substr(start, comma - start);
                if (!field.empty()) icvals.push_back(parse_spice_number(field));
                start = comma + 1;
            }
            if (icvals.size() >= 1) { q->ic_vbe = icvals[0]; q->ic_vbe_given = true; }
            if (icvals.size() >= 2) { q->ic_vce = icvals[1]; q->ic_vce_given = true; }
            continue;
        }
        double val = parse_spice_number(valstr);
        if (key == "area") { q->geom.area = val; q->geom.area_given = true; }
        else if (key == "areab") { q->geom.areab = val; q->geom.areab_given = true; }
        else if (key == "areac") { q->geom.areac = val; q->geom.areac_given = true; }
        else if (key == "m") { q->geom.m = val; q->geom.m_given = true; }
    }
    return q;
}

void resolve_bjts(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx)
{
    // Resolve deferred BJTs (Q-cards dispatch to BJT or VBIC based on LEVEL)
    // LEVEL=1 (default, or unspecified) -> BJT (Gummel-Poon)
    // LEVEL=4 or LEVEL=9               -> VBIC
    std::unordered_map<std::string, std::unique_ptr<BJTModelCard>> bjt_cards;
    std::unordered_map<std::string, std::unique_ptr<VBICModelCard>> vbic_cards;

    for (const auto& elem : elements) {
        const auto& q = static_cast<const ParsedBJT&>(*elem);
        auto it = models.find(q.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(q.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(q.line_number) +
                                 ": Unknown model '" + q.model_name + "'");
            }
            it = it2;
        }
        // Check that the model type is NPN or PNP
        std::string model_type = to_lower(it->second.type);
        if (model_type != "npn" && model_type != "pnp") {
            throw ParseError("Line " + std::to_string(q.line_number) +
                             ": Q card references non-BJT model '" + q.model_name + "'");
        }

        // Determine level: default=1 (BJT), 4 or 9 = VBIC
        auto level_it = it->second.params.find("level");
        int level = (level_it == it->second.params.end()) ? 1
                    : static_cast<int>(level_it->second);

        if (level == 4 || level == 9 || level == 12 || level == 13) {
            // VBIC model
            auto card_it = vbic_cards.find(q.model_name);
            if (card_it == vbic_cards.end()) {
                try {
                    card_it = vbic_cards.emplace(q.model_name,
                                                  to_vbic_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(q.line_number) +
                                     ": " + e.what());
                }
            }
            VBICDevice::Geom vgeom;
            vgeom.area = q.geom.area;
            vgeom.area_given = q.geom.area_given;
            vgeom.m = q.geom.m;
            vgeom.m_given = q.geom.m_given;
            auto dev = VBICDevice::make(q.name, q.nc, q.nb, q.ne, q.ns,
                                        vgeom, *card_it->second);
            if (q.ic_vbe_given || q.ic_vce_given) {
                dev->set_ic(q.ic_vbe, q.ic_vbe_given, q.ic_vce, q.ic_vce_given);
            }
            ckt.add_device(std::move(dev));
        } else {
            // Standard BJT (level 1 or unspecified)
            auto card_it = bjt_cards.find(q.model_name);
            if (card_it == bjt_cards.end()) {
                try {
                    card_it = bjt_cards.emplace(q.model_name,
                                                to_bjt_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(q.line_number) +
                                     ": " + e.what());
                }
            }
            auto dev = BJTDevice::make(q.name, q.nc, q.nb, q.ne, q.ns,
                                       q.geom, *card_it->second);
            if (q.ic_vbe_given || q.ic_vce_given) {
                dev->set_ic(q.ic_vbe, q.ic_vbe_given, q.ic_vce, q.ic_vce_given);
            }
            ckt.add_device(std::move(dev));
        }
    }
    for (auto& [name, card] : bjt_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : vbic_cards) {
        ckt.add_model_card(std::move(card));
    }
}

void register_bjt_parser(DeviceRegistry& reg) {
    DeviceRegistry::ElementParserEntry entry;
    entry.prefix = 'q';
    entry.parse = parse_bjt_element;
    entry.resolve = resolve_bjts;
    reg.add_element_parser(std::move(entry));
}

} // namespace neospice
