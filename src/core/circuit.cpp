#include "core/circuit.hpp"
#include "core/circuit_defs.hpp"  // DefinitionStore must be complete for defaulted dtor/move
#include <algorithm>
#include <cassert>
#include <cctype>
#include <stdexcept>

namespace neospice {

std::string Circuit::model_key(std::string_view name) {
    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return key;
}

// Out-of-line special members so ModelCardHolder's vtable is emitted here.
Circuit::Circuit() = default;
Circuit::~Circuit() = default;
Circuit::Circuit(Circuit&&) noexcept = default;
Circuit& Circuit::operator=(Circuit&&) noexcept = default;

Circuit::ModelCardHolder* Circuit::find_model_holder(std::string_view name) const {
    auto it = model_card_index_.find(model_key(name));
    if (it == model_card_index_.end()) return nullptr;
    auto idx = it->second;
    if (idx < 0 || idx >= static_cast<int32_t>(model_cards_.size()))
        return nullptr;
    return model_cards_[idx].get();
}

ModelId Circuit::add_model_card_raw(std::unique_ptr<ModelCardHolder> holder,
                                     std::string name, std::string model_type) {
    ModelId id{static_cast<int32_t>(model_cards_.size())};
    if (!name.empty()) {
        auto key = model_key(name);
        if (model_card_index_.find(key) != model_card_index_.end())
            throw std::invalid_argument("Model already exists: " + name);
        model_card_index_[std::move(key)] = static_cast<int32_t>(model_cards_.size());
    }
    holder->name = std::move(name);
    holder->model_type = std::move(model_type);
    model_cards_.push_back(std::move(holder));
    return id;
}

// Definition of the thread-local integrator-context pointer declared in
// circuit.hpp.  newton_solve sets this before the per-device stamping
// loop and clears it (via RAII) on the way out.
thread_local const IntegratorCtx* tls_integrator_ctx = nullptr;

NodeId Circuit::node(const std::string& name) {
    // Treat "0", "gnd", "GND" as ground
    if (name == "0" || name == "gnd" || name == "GND") {
        return GND;
    }

    auto it = node_map_.find(name);
    if (it != node_map_.end()) {
        return NodeId{it->second};
    }

    if (finalized_)
        throw std::logic_error("Cannot add node after circuit is finalized");

    int32_t idx = next_node_++;
    node_map_[name] = idx;
    node_names_.push_back(name);
    internal_nodes_.push_back(false);
    return NodeId{idx};
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

bool Circuit::has_organic_diagonal(int32_t idx) const {
    if (idx < 0 || idx >= static_cast<int32_t>(organic_diagonal_.size()))
        return false;
    return organic_diagonal_[idx];
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
    if (finalized_)
        throw std::logic_error("Cannot add device after circuit is finalized");
    devices_.push_back(std::move(dev));
}

void Circuit::finalize_if_needed() {
    if (finalized_) return;
    finalize();
}

void Circuit::finalize() {
    assert(!finalized_ && "Circuit::finalize() called twice");

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

    // Record which node diagonals are "organic" (stamped by devices) before
    // adding blanket structural diagonals.  newton_solve uses this to inject
    // gmin only where devices contribute — V-source-driven nodes stay at
    // zero, avoiding spurious gmin current artifacts.
    organic_diagonal_.assign(next_node_, false);
    for (int32_t i = 0; i < next_node_; ++i) {
        organic_diagonal_[i] = builder.has_diagonal(i);
        if (!organic_diagonal_[i])
            builder.add(i, i);
    }
    pattern_ = std::make_unique<SparsityPattern>(builder.build());

    // 4. Assign offsets into the pattern for each device.
    for (auto& dev : devices_) {
        dev->assign_offsets(*pattern_);
    }

    // 5. Process temperature-dependent parameters for R/C/L/B devices.
    //    This mirrors ngspice's REStemp/CAPtemp/INDtemp calls.
    //    Device::process_temperature() is a no-op by default; subclasses
    //    with temperature coefficients override it.
    for (auto& dev : devices_) {
        dev->process_temperature(options.temp, options.tnom);
    }

    // 6. Allocate state ring buffers and bind per-device base offsets.
    num_states_ = 0;
    for (auto& dev : devices_) num_states_ += dev->state_vars();
    state0_.assign(num_states_, 0.0);
    state1_.assign(num_states_, 0.0);
    state2_.assign(num_states_, 0.0);

    rebind_device_states();

    finalized_ = true;
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
    clear_operating_point();
    for (auto& dev : devices_) {
        dev->reset();
    }
}

void Circuit::set_operating_point(const std::vector<double>& solution) {
    operating_point_ = solution;
}

const std::vector<double>* Circuit::operating_point() const {
    return operating_point_.empty() ? nullptr : &operating_point_;
}

void Circuit::clear_operating_point() {
    operating_point_.clear();
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

std::vector<std::string> Circuit::node_names() const {
    std::vector<std::string> names;
    for (int32_t i = 0; i < next_node_; ++i) {
        if (!is_internal_node(i))
            names.push_back(node_names_[i]);
    }
    return names;
}

std::vector<std::string> Circuit::device_names() const {
    std::vector<std::string> names;
    names.reserve(devices_.size());
    for (const auto& dev : devices_)
        names.push_back(dev->name());
    return names;
}

Device* Circuit::find_device_ptr(const std::string& name) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (auto& dev : devices_) {
        std::string dev_lower = dev->name();
        std::transform(dev_lower.begin(), dev_lower.end(), dev_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (dev_lower == lower_name) return dev.get();
    }
    return nullptr;
}

const Device* Circuit::find_device_ptr(const std::string& name) const {
    return const_cast<Circuit*>(this)->find_device_ptr(name);
}

DeviceInfo Circuit::device_info(const std::string& name) const {
    const Device* dev = find_device_ptr(name);
    if (!dev) throw std::out_of_range("Device not found: " + name);

    DeviceInfo info;
    info.name = dev->name();
    info.type = dev->device_type();
    info.value = dev->primary_value();
    for (int32_t idx : dev->external_nodes()) {
        if (idx == GROUND_INTERNAL)
            info.nodes.push_back("0");
        else if (idx >= 0 && idx < next_node_)
            info.nodes.push_back(node_names_[idx]);
    }
    return info;
}

std::vector<std::string> Circuit::devices_at_node(const std::string& node) const {
    int32_t target;
    if (node == "0" || node == "gnd" || node == "GND")
        target = GROUND_INTERNAL;
    else {
        auto it = node_map_.find(node);
        if (it == node_map_.end()) return {};
        target = it->second;
    }

    std::vector<std::string> result;
    for (const auto& dev : devices_) {
        auto nodes = dev->external_nodes();
        for (int32_t n : nodes) {
            if (n == target) {
                result.push_back(dev->name());
                break;
            }
        }
    }
    return result;
}

bool Circuit::set_param(const std::string& device_name, double value) {
    Device* dev = find_device_ptr(device_name);
    if (!dev) return false;
    bool changed = dev->set_value(value);
    if (changed) clear_operating_point();
    return changed;
}

NodeId Circuit::find_node(std::string_view name) const {
    std::string sname(name);
    if (sname == "0" || sname == "gnd" || sname == "GND")
        return GND;
    auto it = node_map_.find(sname);
    if (it == node_map_.end())
        throw std::out_of_range("Node not found: " + sname);
    return NodeId{it->second};
}

DevId Circuit::find_device(std::string_view name) const {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (std::size_t i = 0; i < devices_.size(); ++i) {
        std::string dev_lower = devices_[i]->name();
        std::transform(dev_lower.begin(), dev_lower.end(), dev_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (dev_lower == lower) return DevId{static_cast<int32_t>(i)};
    }
    throw std::out_of_range("Device not found: " + std::string(name));
}

std::string_view Circuit::name(NodeId node) const {
    auto idx = static_cast<int32_t>(node);
    if (idx < 0 || idx >= static_cast<int32_t>(node_names_.size()))
        throw std::out_of_range("Invalid NodeId");
    return node_names_[idx];
}

std::string_view Circuit::name(DevId dev) const {
    auto idx = static_cast<int32_t>(dev);
    if (idx < 0 || idx >= static_cast<int32_t>(devices_.size()))
        throw std::out_of_range("Invalid DevId");
    return devices_[idx]->name();
}

DeviceInfo Circuit::device_info(DevId dev) const {
    auto idx = static_cast<int32_t>(dev);
    if (idx < 0 || idx >= static_cast<int32_t>(devices_.size()))
        throw std::out_of_range("Invalid DevId");
    return device_info(devices_[idx]->name());
}

} // namespace neospice
