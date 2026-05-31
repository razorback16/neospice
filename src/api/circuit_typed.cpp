#include "core/circuit.hpp"
#include "neospice/types.hpp"
#include "devices/resistor.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include "devices/coupled_inductor.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/vcvs.hpp"
#include "devices/vccs.hpp"
#include "devices/cccs.hpp"
#include "devices/ccvs.hpp"
#include "devices/device_registry.hpp"
#include "devices/asrc/asrc_device.hpp"
#include "devices/asrc/expression_ast.hpp"
#include "parser/model_cards.hpp"
#include "parser/tokenizer.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace neospice {

namespace {

std::string lower_copy(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

int32_t raw(NodeId id) {
    return static_cast<int32_t>(id);
}

int model_level(const ModelCard& card, int default_level) {
    auto it = card.params.find("level");
    return it == card.params.end() ? default_level : static_cast<int>(it->second);
}

std::invalid_argument missing_model(std::string_view device,
                                    std::string_view model) {
    return std::invalid_argument(std::string(device) +
                                 " device: model not found or wrong type: " +
                                 std::string(model));
}

int32_t resolve_b_node(Circuit& ckt, const std::string& name) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "0" || lower == "gnd") return GROUND_INTERNAL;
    return static_cast<int32_t>(ckt.node(lower));
}

} // namespace

ModelId Circuit::model(std::string_view model_text) {
    std::string text(model_text);
    auto lines = tokenize(text);
    if (lines.empty())
        throw std::invalid_argument("Circuit::model: empty model text");
    if (lines.size() != 1)
        throw std::invalid_argument("Circuit::model: expected exactly one .model card");

    auto tokens = lines.front().tokens;
    if (tokens.empty())
        throw std::invalid_argument("Circuit::model: empty model text");
    if (lower_copy(tokens.front()) != ".model")
        tokens.insert(tokens.begin(), ".model");

    auto card = parse_model_card(tokens);
    auto type = lower_copy(card.type);

    // Determine level for semiconductor types
    auto& reg = DeviceRegistry::get_default();
    int level = 0;
    if (type == "nmos" || type == "pmos") {
        level = detect_mosfet_level(card);
    } else if (type == "npn" || type == "pnp") {
        level = model_level(card, 1);
    } else if (type == "njf" || type == "pjf") {
        level = model_level(card, 1);
    }
    // d, nhfet, phfet, nmf, pmf use level=0

    auto holder = reg.create_model_card(type, level, card);
    if (holder)
        return add_model_card_raw(std::move(holder), card.name, type);

    throw std::invalid_argument("Circuit::model: unsupported model type '" +
                                card.type + "'");
}

// --- Passives ---

DevId Circuit::R(std::string_view name, NodeId a, NodeId b, double ohms) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<Resistor>(std::string(name),
                                          static_cast<int32_t>(a),
                                          static_cast<int32_t>(b), ohms));
    return id;
}

DevId Circuit::C(std::string_view name, NodeId a, NodeId b, double farads,
                 std::optional<double> ic) {
    DevId id{static_cast<int32_t>(devices_.size())};
    auto cap = std::make_unique<Capacitor>(std::string(name),
                                           static_cast<int32_t>(a),
                                           static_cast<int32_t>(b), farads);
    if (ic) cap->set_ic(*ic);
    add_device(std::move(cap));
    return id;
}

DevId Circuit::L(std::string_view name, NodeId a, NodeId b, double henries) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<Inductor>(std::string(name),
                                          static_cast<int32_t>(a),
                                          static_cast<int32_t>(b), henries));
    return id;
}

DevId Circuit::K(std::string_view name, DevId L1, DevId L2, double coupling) {
    auto idx1 = static_cast<int32_t>(L1);
    auto idx2 = static_cast<int32_t>(L2);
    if (idx1 < 0 || idx1 >= static_cast<int32_t>(devices_.size()))
        throw std::invalid_argument("K device: L1 DevId out of range");
    if (idx2 < 0 || idx2 >= static_cast<int32_t>(devices_.size()))
        throw std::invalid_argument("K device: L2 DevId out of range");
    auto* ind1 = dynamic_cast<Inductor*>(devices_[idx1].get());
    auto* ind2 = dynamic_cast<Inductor*>(devices_[idx2].get());
    if (!ind1 || !ind2)
        throw std::invalid_argument("K device requires two inductor DevIds");
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<CoupledInductor>(std::string(name), ind1, ind2, coupling));
    return id;
}

// --- Independent sources ---

DevId Circuit::V(std::string_view name, NodeId p, NodeId n, double dc) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<VSource>(std::string(name),
                                         static_cast<int32_t>(p),
                                         static_cast<int32_t>(n), dc));
    return id;
}

DevId Circuit::V(std::string_view name, NodeId p, NodeId n,
                 double dc, double ac_mag, double ac_phase) {
    DevId id{static_cast<int32_t>(devices_.size())};
    auto vs = std::make_unique<VSource>(std::string(name),
                                        static_cast<int32_t>(p),
                                        static_cast<int32_t>(n), dc);
    vs->set_ac(ac_mag, ac_phase);
    add_device(std::move(vs));
    return id;
}

DevId Circuit::I(std::string_view name, NodeId p, NodeId n, double dc) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<ISource>(std::string(name),
                                         static_cast<int32_t>(p),
                                         static_cast<int32_t>(n), dc));
    return id;
}

DevId Circuit::I(std::string_view name, NodeId p, NodeId n,
                 double dc, double ac_mag, double ac_phase) {
    DevId id{static_cast<int32_t>(devices_.size())};
    auto is = std::make_unique<ISource>(std::string(name),
                                        static_cast<int32_t>(p),
                                        static_cast<int32_t>(n), dc);
    is->set_ac(ac_mag, ac_phase);
    add_device(std::move(is));
    return id;
}

// --- Dependent sources ---

DevId Circuit::E(std::string_view name, NodeId op, NodeId on,
                 NodeId cp, NodeId cn, double gain) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<VCVS>(std::string(name),
                                      static_cast<int32_t>(op),
                                      static_cast<int32_t>(on),
                                      static_cast<int32_t>(cp),
                                      static_cast<int32_t>(cn), gain));
    return id;
}

DevId Circuit::G(std::string_view name, NodeId op, NodeId on,
                 NodeId cp, NodeId cn, double gm) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<VCCS>(std::string(name),
                                      static_cast<int32_t>(op),
                                      static_cast<int32_t>(on),
                                      static_cast<int32_t>(cp),
                                      static_cast<int32_t>(cn), gm));
    return id;
}

DevId Circuit::F(std::string_view name, NodeId op, NodeId on,
                 DevId vsense, double gain) {
    auto idx = static_cast<int32_t>(vsense);
    if (idx < 0 || idx >= static_cast<int32_t>(devices_.size()))
        throw std::invalid_argument("F device: vsense DevId out of range");
    auto* vs = dynamic_cast<VSource*>(devices_[idx].get());
    if (!vs)
        throw std::invalid_argument("F device requires a VSource DevId for sensing");
    DevId id{static_cast<int32_t>(devices_.size())};
    // CCCS constructor: (name, np, nn, gain, vsense)
    add_device(std::make_unique<CCCS>(std::string(name),
                                      static_cast<int32_t>(op),
                                      static_cast<int32_t>(on), gain, vs));
    return id;
}

DevId Circuit::H(std::string_view name, NodeId op, NodeId on,
                 DevId vsense, double transresistance) {
    auto idx = static_cast<int32_t>(vsense);
    if (idx < 0 || idx >= static_cast<int32_t>(devices_.size()))
        throw std::invalid_argument("H device: vsense DevId out of range");
    auto* vs = dynamic_cast<VSource*>(devices_[idx].get());
    if (!vs)
        throw std::invalid_argument("H device requires a VSource DevId for sensing");
    DevId id{static_cast<int32_t>(devices_.size())};
    // CCVS constructor: (name, np, nn, rm, vsense)
    add_device(std::make_unique<CCVS>(std::string(name),
                                      static_cast<int32_t>(op),
                                      static_cast<int32_t>(on),
                                      transresistance, vs));
    return id;
}

// --- Behavioral source ---

DevId Circuit::B(std::string_view name, NodeId np, NodeId nn,
                 const std::string& expr_spec,
                 double tc1, double tc2, double temp, double dtemp) {
    // Parse V=/I= prefix
    std::string spec_lower = lower_copy(expr_spec);
    ASRCDevice::Mode mode;
    std::string expr_str;
    if (spec_lower.size() >= 2 && spec_lower[0] == 'v' && spec_lower[1] == '=') {
        mode = ASRCDevice::Mode::VOLTAGE;
        expr_str = expr_spec.substr(2);
    } else if (spec_lower.size() >= 2 && spec_lower[0] == 'i' && spec_lower[1] == '=') {
        mode = ASRCDevice::Mode::CURRENT;
        expr_str = expr_spec.substr(2);
    } else {
        throw std::invalid_argument(
            "B device '" + std::string(name) + "' requires 'V={expr}' or 'I={expr}', got: " + expr_spec);
    }

    // Strip optional surrounding braces and whitespace
    while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.front())))
        expr_str.erase(0, 1);
    while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.back())))
        expr_str.pop_back();
    if (expr_str.size() >= 2 && expr_str.front() == '{' && expr_str.back() == '}')
        expr_str = expr_str.substr(1, expr_str.size() - 2);

    // Compile expression
    auto compiled = asrc::CompiledExpression::compile(expr_str);

    // Resolve variable references
    const auto& refs = compiled.var_refs();
    int nv = compiled.num_vars();
    std::vector<int32_t> node_indices(nv, -1);
    std::vector<int32_t> node_indices2(nv, -1);
    std::vector<const Device*> vsource_ptrs(nv, nullptr);

    for (int i = 0; i < nv; ++i) {
        const auto& ref = refs[i];

        // Handle sentinels (TIME, TEMPER, HERTZ)
        if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
            (ref.name1 == "__time__" || ref.name1 == "__temper__" || ref.name1 == "__hertz__")) {
            node_indices[i] = -2;
            continue;
        }

        switch (ref.kind) {
        case asrc::VarKind::NODE_VOLTAGE:
            node_indices[i] = resolve_b_node(*this, ref.name1);
            break;
        case asrc::VarKind::DIFF_VOLTAGE:
            node_indices[i]  = resolve_b_node(*this, ref.name1);
            node_indices2[i] = resolve_b_node(*this, ref.name2);
            break;
        case asrc::VarKind::BRANCH_CURRENT: {
            // Bind I() to an independent V source, or any branch-owning device
            // (E/VCVS, H/CCVS, inductor, voltage-mode behavioral source).
            auto* dev = find_device_ptr(ref.name1);
            const Device* vs = nullptr;
            if (dev && (dynamic_cast<const VSource*>(dev) || dev->extra_vars() > 0))
                vs = dev;
            if (!vs)
                throw std::invalid_argument(
                    "B device '" + std::string(name) +
                    "': I() references unknown voltage source '" + ref.name1 +
                    "' (voltage source must be added before B())");
            vsource_ptrs[i] = vs;
            break;
        }
        }
    }

    // Construct ASRCDevice
    auto asrc_dev = std::make_unique<ASRCDevice>(
        std::string(name), raw(np), raw(nn), mode,
        std::move(compiled), std::move(node_indices),
        std::move(node_indices2), std::move(vsource_ptrs));

    asrc_dev->set_tc1(tc1);
    asrc_dev->set_tc2(tc2);
    if (temp > 0) asrc_dev->set_temp(temp);
    asrc_dev->set_dtemp(dtemp);

    return add_dev(std::move(asrc_dev));
}

// --- Semiconductors ---

DevId Circuit::D(std::string_view name, NodeId anode, NodeId cathode,
                 std::string_view model_name) {
    auto* holder = find_model_holder(model_name);
    if (!holder) throw missing_model("D", model_name);

    int32_t nodes[] = {raw(anode), raw(cathode)};
    std::unordered_map<std::string, double> params;
    auto dev = DeviceRegistry::get_default().build_device('d', name, nodes, params, *holder);
    if (!dev) throw missing_model("D", model_name);
    return add_dev(std::move(dev));
}

DevId Circuit::Q(std::string_view name, NodeId c, NodeId b, NodeId e,
                 std::string_view model_name) {
    return Q(name, c, b, e, GND, model_name);
}

DevId Circuit::Q(std::string_view name, NodeId c, NodeId b, NodeId e, NodeId s,
                 std::string_view model_name) {
    auto* holder = find_model_holder(model_name);
    if (!holder) throw missing_model("Q", model_name);

    int32_t nodes[] = {raw(c), raw(b), raw(e), raw(s)};
    std::unordered_map<std::string, double> params;
    auto dev = DeviceRegistry::get_default().build_device('q', name, nodes, params, *holder);
    if (!dev) throw missing_model("Q", model_name);
    return add_dev(std::move(dev));
}

DevId Circuit::J(std::string_view name, NodeId d, NodeId g, NodeId s,
                 std::string_view model_name) {
    auto* holder = find_model_holder(model_name);
    if (!holder) throw missing_model("J", model_name);

    int32_t nodes[] = {raw(d), raw(g), raw(s)};
    std::unordered_map<std::string, double> params;
    auto dev = DeviceRegistry::get_default().build_device('j', name, nodes, params, *holder);
    if (!dev) throw missing_model("J", model_name);
    return add_dev(std::move(dev));
}

DevId Circuit::M(std::string_view name, NodeId d, NodeId g, NodeId s, NodeId b,
                 std::string_view model_name) {
    return M(name, d, g, s, b, model_name, 1e-6, 1e-7);
}

DevId Circuit::M(std::string_view name, NodeId d, NodeId g, NodeId s, NodeId b,
                 std::string_view model_name, double w, double l) {
    auto* holder = find_model_holder(model_name);
    if (!holder) throw missing_model("M", model_name);

    int32_t nodes[] = {raw(d), raw(g), raw(s), raw(b)};
    std::unordered_map<std::string, double> params{{"w", w}, {"l", l}};
    auto dev = DeviceRegistry::get_default().build_device('m', name, nodes, params, *holder);
    if (!dev) throw missing_model("M", model_name);
    return add_dev(std::move(dev));
}

// --- Custom device injection ---

DevId Circuit::add_dev(std::unique_ptr<Device> dev) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::move(dev));
    return id;
}

} // namespace neospice
