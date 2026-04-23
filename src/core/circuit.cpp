#include "core/circuit.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"  // complete BSIM4v7ModelCard for unique_ptr
#include "devices/mos1/mos1_device.hpp"        // complete MOS1ModelCard for unique_ptr
#include "devices/mos3/mos3_device.hpp"        // complete MOS3ModelCard for unique_ptr
#include "devices/mos9/mos9_device.hpp"        // complete MOS9ModelCard for unique_ptr
#include "devices/bjt/bjt_device.hpp"           // complete BJTModelCard for unique_ptr
#include "devices/jfet/jfet_device.hpp"         // complete JFETModelCard for unique_ptr
#include "devices/jfet2/jfet2_device.hpp"       // complete JFET2ModelCard for unique_ptr
#include "devices/dio/dio_device.hpp"           // complete DIOModelCard for unique_ptr
#include "devices/vbic/vbic_device.hpp"         // complete VBICModelCard for unique_ptr
#include "devices/bsim3/bsim3_device.hpp"      // complete BSIM3ModelCard for unique_ptr
#include "devices/hfet1/hfet1_device.hpp"      // complete HFETAModelCard for unique_ptr
#include "devices/hfet2/hfet2_device.hpp"      // complete HFET2ModelCard for unique_ptr
#include "devices/bsim3v32/bsim3v32_device.hpp" // complete BSIM3v32ModelCard for unique_ptr
#include "devices/hisim2/hisim2_device.hpp"    // complete HSM2ModelCard for unique_ptr
#include "devices/hisimhv/hisimhv_device.hpp"  // complete HSMHVModelCard for unique_ptr
#include "devices/bsimsoi/bsimsoi_device.hpp"  // complete B4SOIModelCard for unique_ptr
#include "devices/resistor.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include "devices/asrc/asrc_device.hpp"
#include <cassert>
#include <stdexcept>

namespace neospice {

// Out-of-line special members so BSIM4v7ModelCard can be incomplete in the header.
Circuit::Circuit() = default;
Circuit::~Circuit() = default;
Circuit::Circuit(Circuit&&) noexcept = default;
Circuit& Circuit::operator=(Circuit&&) noexcept = default;

// Definition of the thread-local integrator-context pointer declared in
// circuit.hpp.  newton_solve sets this before the per-device stamping
// loop and clears it (via RAII) on the way out.
thread_local const IntegratorCtx* tls_integrator_ctx = nullptr;

int32_t Circuit::node(const std::string& name) {
    // Treat "0", "gnd", "GND" as ground
    if (name == "0" || name == "gnd" || name == "GND") {
        return GROUND_INTERNAL;
    }

    auto it = node_map_.find(name);
    if (it != node_map_.end()) {
        return it->second;
    }

    int32_t idx = next_node_++;
    node_map_[name] = idx;
    node_names_.push_back(name);
    internal_nodes_.push_back(false);
    return idx;
}

std::string Circuit::node_name(int32_t idx) const {
    if (idx < 0 || idx >= static_cast<int32_t>(node_names_.size())) {
        throw std::out_of_range("Circuit::node_name: index out of range");
    }
    return node_names_[idx];
}

void Circuit::mark_internal_node(int32_t idx) {
    if (idx >= 0 && idx < static_cast<int32_t>(internal_nodes_.size()))
        internal_nodes_[idx] = true;
}

bool Circuit::is_internal_node(int32_t idx) const {
    if (idx < 0 || idx >= static_cast<int32_t>(internal_nodes_.size()))
        return false;
    return internal_nodes_[idx];
}

int32_t Circuit::node_index(const std::string& name) const {
    if (name == "0" || name == "gnd" || name == "GND") {
        return GROUND_INTERNAL;
    }
    auto it = node_map_.find(name);
    if (it == node_map_.end()) {
        throw std::out_of_range("Circuit::node_index: unknown node '" + name + "'");
    }
    return it->second;
}

void Circuit::add_device(std::unique_ptr<Device> dev) {
    devices_.push_back(std::move(dev));
}

void Circuit::add_bsim4_model_card(std::unique_ptr<BSIM4v7ModelCard> card) {
    bsim4_model_cards_.push_back(std::move(card));
}

void Circuit::add_bjt_model_card(std::unique_ptr<BJTModelCard> card) {
    bjt_model_cards_.push_back(std::move(card));
}

void Circuit::add_jfet_model_card(std::unique_ptr<JFETModelCard> card) {
    jfet_model_cards_.push_back(std::move(card));
}

void Circuit::add_jfet2_model_card(std::unique_ptr<JFET2ModelCard> card) {
    jfet2_model_cards_.push_back(std::move(card));
}

void Circuit::add_mos1_model_card(std::unique_ptr<MOS1ModelCard> card) {
    mos1_model_cards_.push_back(std::move(card));
}

void Circuit::add_mos3_model_card(std::unique_ptr<MOS3ModelCard> card) {
    mos3_model_cards_.push_back(std::move(card));
}

void Circuit::add_mos9_model_card(std::unique_ptr<MOS9ModelCard> card) {
    mos9_model_cards_.push_back(std::move(card));
}

void Circuit::add_dio_model_card(std::unique_ptr<DIOModelCard> card) {
    dio_model_cards_.push_back(std::move(card));
}

void Circuit::add_vbic_model_card(std::unique_ptr<VBICModelCard> card) {
    vbic_model_cards_.push_back(std::move(card));
}

void Circuit::add_bsim3_model_card(std::unique_ptr<BSIM3ModelCard> card) {
    bsim3_model_cards_.push_back(std::move(card));
}

void Circuit::add_hfet1_model_card(std::unique_ptr<HFETAModelCard> card) {
    hfet1_model_cards_.push_back(std::move(card));
}

void Circuit::add_hfet2_model_card(std::unique_ptr<HFET2ModelCard> card) {
    hfet2_model_cards_.push_back(std::move(card));
}

void Circuit::add_bsim3v32_model_card(std::unique_ptr<BSIM3v32ModelCard> card) {
    bsim3v32_model_cards_.push_back(std::move(card));
}

void Circuit::add_hisim2_model_card(std::unique_ptr<HSM2ModelCard> card) {
    hisim2_model_cards_.push_back(std::move(card));
}

void Circuit::add_hisimhv_model_card(std::unique_ptr<HSMHVModelCard> card) {
    hisimhv_model_cards_.push_back(std::move(card));
}

void Circuit::add_bsimsoi_model_card(std::unique_ptr<B4SOIModelCard> card) {
    bsimsoi_model_cards_.push_back(std::move(card));
}

void Circuit::finalize() {
    assert(!pattern_ && "Circuit::finalize() called twice");

    // 0. Let devices declare internal nodes. These get allocated from
    //    next_node_ via the normal Circuit::node() path, so they appear
    //    before branch indices in the MNA variable numbering.
    for (auto& dev : devices_) {
        dev->declare_internal_nodes(*this);
    }

    // 1. Assign branch indices for devices that carry extra MNA variables.
    //    Branch indices start right after ALL node variables (external +
    //    internal, since internal nodes were just allocated above).
    int32_t branch_idx = next_node_;

    for (auto& dev : devices_) {
        dev->assign_branch_index(branch_idx);
    }

    // 2. Total number of MNA variables.
    num_vars_ = branch_idx;

    // 3. Build sparsity pattern.
    SparsityBuilder builder(num_vars_);
    for (auto& dev : devices_) {
        dev->stamp_pattern(builder);
    }
    pattern_ = std::make_unique<SparsityPattern>(builder.build());

    // 4. Assign offsets into the pattern for each device.
    for (auto& dev : devices_) {
        dev->assign_offsets(*pattern_);
    }

    // 5. Process temperature-dependent parameters for R/C/L devices.
    //    This mirrors ngspice's REStemp/CAPtemp/INDtemp calls.
    for (auto& dev : devices_) {
        if (auto* r = dynamic_cast<Resistor*>(dev.get()))
            r->process_temperature(options.temp, options.tnom);
        else if (auto* c = dynamic_cast<Capacitor*>(dev.get()))
            c->process_temperature(options.temp, options.tnom);
        else if (auto* l = dynamic_cast<Inductor*>(dev.get()))
            l->process_temperature(options.temp, options.tnom);
        else if (auto* b = dynamic_cast<ASRCDevice*>(dev.get()))
            b->process_temperature(options.temp, options.tnom);
    }

    // 6. Allocate state ring buffers and bind per-device base offsets.
    num_states_ = 0;
    for (auto& dev : devices_) num_states_ += dev->state_vars();
    state0_.assign(num_states_, 0.0);
    state1_.assign(num_states_, 0.0);
    state2_.assign(num_states_, 0.0);

    rebind_device_states();
}

void Circuit::rebind_device_states() {
    int32_t base = 0;
    for (auto& dev : devices_) {
        int32_t n = dev->state_vars();
        if (n > 0) {
            dev->set_state_ptrs(state0_.data(), state1_.data(),
                                state2_.data(), base);
            base += n;
        }
    }
}

void Circuit::reset_state() {
    std::fill(state0_.begin(), state0_.end(), 0.0);
    std::fill(state1_.begin(), state1_.end(), 0.0);
    std::fill(state2_.begin(), state2_.end(), 0.0);
    for (auto& dev : devices_) {
        dev->reset();
    }
}

void Circuit::rotate_state() {
    // Rotate state history: state2 <- state1 <- state0.
    //
    // We use swap(state2_, state1_) then value-copy state0_ into state1_.
    // swap() leaves state2_ pointing at the buffer that used to back
    // state1_ (the device's cached state1 pointer now aliases stale data);
    // the value-copy into state1_ also keeps its backing buffer address
    // stable but its CONTENTS change.
    //
    // Either way, any device that cached raw double* during set_state_ptrs
    // must be re-bound so its cached state1/state2 pointers reflect the
    // new ring contents.  rebind_device_states() walks every device and
    // re-calls set_state_ptrs with fresh data() pointers from the (now
    // rotated) vectors.
    state2_.swap(state1_);
    state1_ = state0_;   // value copy; same backing buffer
    rebind_device_states();
}

} // namespace neospice
