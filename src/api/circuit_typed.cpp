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
#include "devices/dio/dio_device.hpp"
#include "devices/bjt/bjt_device.hpp"
#include "devices/vbic/vbic_device.hpp"
#include "devices/jfet/jfet_device.hpp"
#include "devices/jfet2/jfet2_device.hpp"
#include "devices/jfet2/jfet2_model_card.hpp"
#include "devices/mos1/mos1_device.hpp"
#include "devices/mos3/mos3_device.hpp"
#include "devices/mos9/mos9_device.hpp"
#include "devices/bsim3/bsim3_device.hpp"
#include "devices/bsim3v32/bsim3v32_device.hpp"
#include "devices/bsim3v32/bsim3v32_model_card.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "devices/bsimsoi/bsimsoi_device.hpp"
#include "devices/bsimsoi/bsimsoi_model_card.hpp"
#include "devices/hisim2/hisim2_device.hpp"
#include "devices/hisim2/hisim2_model_card.hpp"
#include "devices/hisimhv/hisimhv_device.hpp"
#include "devices/hisimhv/hisimhv_model_card.hpp"
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

    if (type == "d") {
        return add_model_card(to_dio_card(card), card.name, type);
    }
    if (type == "npn" || type == "pnp") {
        int level = model_level(card, 1);
        if (level == 4 || level == 9 || level == 12 || level == 13)
            return add_model_card(to_vbic_card(card), card.name, type);
        return add_model_card(to_bjt_card(card), card.name, type);
    }
    if (type == "njf" || type == "pjf") {
        int level = model_level(card, 1);
        if (level == 2)
            return add_model_card(to_jfet2_card(card), card.name, type);
        return add_model_card(to_jfet_card(card), card.name, type);
    }
    if (type == "nmos" || type == "pmos") {
        int level = detect_mosfet_level(card);
        if (level == 1)
            return add_model_card(to_mos1_card(card), card.name, type);
        if (level == 3)
            return add_model_card(to_mos3_card(card), card.name, type);
        if (level == 8 || level == 49) {
            auto ver_it = card.params.find("version");
            if (ver_it != card.params.end() && ver_it->second < 3.3)
                return add_model_card(to_bsim3v32_card(card), card.name, type);
            return add_model_card(to_bsim3_card(card), card.name, type);
        }
        if (level == 9)
            return add_model_card(to_mos9_card(card), card.name, type);
        if (level == 14)
            return add_model_card(to_bsim4_card(card), card.name, type);
        if (level == 61 || level == 68)
            return add_model_card(to_hisim2_card(card), card.name, type);
        if (level == 73)
            return add_model_card(to_hisimhv_card(card), card.name, type);
        if (level == 10 || level == 58)
            return add_model_card(to_bsimsoi_card(card), card.name, type);
        throw std::invalid_argument("Circuit::model: unsupported MOSFET LEVEL=" +
                                    std::to_string(level));
    }

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

DevId Circuit::C(std::string_view name, NodeId a, NodeId b, double farads) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<Capacitor>(std::string(name),
                                           static_cast<int32_t>(a),
                                           static_cast<int32_t>(b), farads));
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

// --- Semiconductors ---

DevId Circuit::D(std::string_view name, NodeId anode, NodeId cathode,
                 std::string_view model_name) {
    auto* card = find_model<DIOModelCard>(model_name);
    if (!card) throw missing_model("D", model_name);

    DevId id{static_cast<int32_t>(devices_.size())};
    DIODevice::Geom geom;
    add_device(DIODevice::make(std::string(name), raw(anode), raw(cathode),
                               geom, *card));
    return id;
}

DevId Circuit::Q(std::string_view name, NodeId c, NodeId b, NodeId e,
                 std::string_view model_name) {
    return Q(name, c, b, e, GND, model_name);
}

DevId Circuit::Q(std::string_view name, NodeId c, NodeId b, NodeId e, NodeId s,
                 std::string_view model_name) {
    DevId id{static_cast<int32_t>(devices_.size())};
    if (auto* card = find_model<BJTModelCard>(model_name)) {
        BJTDevice::Geom geom;
        add_device(BJTDevice::make(std::string(name), raw(c), raw(b), raw(e), raw(s),
                                   geom, *card));
        return id;
    }
    if (auto* card = find_model<VBICModelCard>(model_name)) {
        VBICDevice::Geom geom;
        add_device(VBICDevice::make(std::string(name), raw(c), raw(b), raw(e), raw(s),
                                    geom, *card));
        return id;
    }
    throw missing_model("Q", model_name);
}

DevId Circuit::J(std::string_view name, NodeId d, NodeId g, NodeId s,
                 std::string_view model_name) {
    DevId id{static_cast<int32_t>(devices_.size())};
    if (auto* card = find_model<JFETModelCard>(model_name)) {
        JFETDevice::Geom geom;
        add_device(JFETDevice::make(std::string(name), raw(d), raw(g), raw(s),
                                    geom, *card));
        return id;
    }
    if (auto* card = find_model<JFET2ModelCard>(model_name)) {
        JFET2Device::Geom geom;
        add_device(JFET2Device::make(std::string(name), raw(d), raw(g), raw(s),
                                     geom, *card));
        return id;
    }
    throw missing_model("J", model_name);
}

DevId Circuit::M(std::string_view name, NodeId d, NodeId g, NodeId s, NodeId b,
                 std::string_view model_name) {
    return M(name, d, g, s, b, model_name, 1e-6, 1e-7);
}

DevId Circuit::M(std::string_view name, NodeId d, NodeId g, NodeId s, NodeId b,
                 std::string_view model_name, double w, double l) {
    DevId id{static_cast<int32_t>(devices_.size())};
    const int32_t nd = raw(d);
    const int32_t ng = raw(g);
    const int32_t ns = raw(s);
    const int32_t nb = raw(b);

    if (auto* card = find_model<MOS1ModelCard>(model_name)) {
        MOS1Device::Geom geom;
        geom.W = w;
        geom.L = l;
        add_device(MOS1Device::make(std::string(name), nd, ng, ns, nb, geom, *card));
        return id;
    }
    if (auto* card = find_model<MOS3ModelCard>(model_name)) {
        MOS3Device::Geom geom;
        geom.W = w;
        geom.L = l;
        add_device(MOS3Device::make(std::string(name), nd, ng, ns, nb, geom, *card));
        return id;
    }
    if (auto* card = find_model<MOS9ModelCard>(model_name)) {
        MOS9Device::Geom geom;
        geom.W = w;
        geom.L = l;
        add_device(MOS9Device::make(std::string(name), nd, ng, ns, nb, geom, *card));
        return id;
    }
    if (auto* card = find_model<BSIM3ModelCard>(model_name)) {
        BSIM3Device::Geom geom;
        geom.W = w;
        geom.L = l;
        add_device(BSIM3Device::make(std::string(name), nd, ng, ns, nb, geom, *card));
        return id;
    }
    if (auto* card = find_model<BSIM3v32ModelCard>(model_name)) {
        BSIM3v32Device::Geom geom;
        geom.W = w;
        geom.L = l;
        add_device(BSIM3v32Device::make(std::string(name), nd, ng, ns, nb, geom, *card));
        return id;
    }
    if (auto* card = find_model<BSIM4v7ModelCard>(model_name)) {
        BSIM4v7Device::Geom geom;
        geom.W = w;
        geom.L = l;
        add_device(BSIM4v7Device::make(std::string(name), nd, ng, ns, nb, geom, *card));
        return id;
    }
    if (auto* card = find_model<B4SOIModelCard>(model_name)) {
        B4SOIDevice::Geom geom;
        geom.W = w;
        geom.L = l;
        add_device(B4SOIDevice::make(std::string(name), nd, ng, ns,
                                     nb, GROUND_INTERNAL, GROUND_INTERNAL,
                                     geom, *card));
        return id;
    }
    if (auto* card = find_model<HSM2ModelCard>(model_name)) {
        HSM2Device::Geom geom;
        geom.W = w;
        geom.L = l;
        add_device(HSM2Device::make(std::string(name), nd, ng, ns, nb, geom, *card));
        return id;
    }
    if (auto* card = find_model<HSMHVModelCard>(model_name)) {
        HSMHVDevice::Geom geom;
        geom.W = w;
        geom.L = l;
        add_device(HSMHVDevice::make(std::string(name), nd, ng, ns, nb,
                                     GROUND_INTERNAL - 1, geom, *card));
        return id;
    }
    throw missing_model("M", model_name);
}

// --- Custom device injection ---

DevId Circuit::add_dev(std::unique_ptr<Device> dev) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::move(dev));
    return id;
}

} // namespace neospice
