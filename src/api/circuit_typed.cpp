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
#include <stdexcept>

namespace neospice {

// --- Passives ---

DevId Circuit::R(std::string_view name, int32_t a, int32_t b, double ohms) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<Resistor>(std::string(name), a, b, ohms));
    return id;
}

DevId Circuit::C(std::string_view name, int32_t a, int32_t b, double farads) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<Capacitor>(std::string(name), a, b, farads));
    return id;
}

DevId Circuit::L(std::string_view name, int32_t a, int32_t b, double henries) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<Inductor>(std::string(name), a, b, henries));
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

DevId Circuit::V(std::string_view name, int32_t p, int32_t n, double dc) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<VSource>(std::string(name), p, n, dc));
    return id;
}

DevId Circuit::V(std::string_view name, int32_t p, int32_t n,
                 double dc, double ac_mag, double ac_phase) {
    DevId id{static_cast<int32_t>(devices_.size())};
    auto vs = std::make_unique<VSource>(std::string(name), p, n, dc);
    vs->set_ac(ac_mag, ac_phase);
    add_device(std::move(vs));
    return id;
}

DevId Circuit::I(std::string_view name, int32_t p, int32_t n, double dc) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<ISource>(std::string(name), p, n, dc));
    return id;
}

DevId Circuit::I(std::string_view name, int32_t p, int32_t n,
                 double dc, double ac_mag, double ac_phase) {
    DevId id{static_cast<int32_t>(devices_.size())};
    auto is = std::make_unique<ISource>(std::string(name), p, n, dc);
    is->set_ac(ac_mag, ac_phase);
    add_device(std::move(is));
    return id;
}

// --- Dependent sources ---

DevId Circuit::E(std::string_view name, int32_t op, int32_t on,
                 int32_t cp, int32_t cn, double gain) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<VCVS>(std::string(name), op, on, cp, cn, gain));
    return id;
}

DevId Circuit::G(std::string_view name, int32_t op, int32_t on,
                 int32_t cp, int32_t cn, double gm) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::make_unique<VCCS>(std::string(name), op, on, cp, cn, gm));
    return id;
}

DevId Circuit::F(std::string_view name, int32_t op, int32_t on,
                 DevId vsense, double gain) {
    auto idx = static_cast<int32_t>(vsense);
    if (idx < 0 || idx >= static_cast<int32_t>(devices_.size()))
        throw std::invalid_argument("F device: vsense DevId out of range");
    auto* vs = dynamic_cast<VSource*>(devices_[idx].get());
    if (!vs)
        throw std::invalid_argument("F device requires a VSource DevId for sensing");
    DevId id{static_cast<int32_t>(devices_.size())};
    // CCCS constructor: (name, np, nn, gain, vsense)
    add_device(std::make_unique<CCCS>(std::string(name), op, on, gain, vs));
    return id;
}

DevId Circuit::H(std::string_view name, int32_t op, int32_t on,
                 DevId vsense, double transresistance) {
    auto idx = static_cast<int32_t>(vsense);
    if (idx < 0 || idx >= static_cast<int32_t>(devices_.size()))
        throw std::invalid_argument("H device: vsense DevId out of range");
    auto* vs = dynamic_cast<VSource*>(devices_[idx].get());
    if (!vs)
        throw std::invalid_argument("H device requires a VSource DevId for sensing");
    DevId id{static_cast<int32_t>(devices_.size())};
    // CCVS constructor: (name, np, nn, rm, vsense)
    add_device(std::make_unique<CCVS>(std::string(name), op, on, transresistance, vs));
    return id;
}

// --- Custom device injection ---

DevId Circuit::add_dev(std::unique_ptr<Device> dev) {
    DevId id{static_cast<int32_t>(devices_.size())};
    add_device(std::move(dev));
    return id;
}

} // namespace neospice
