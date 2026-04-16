#include "core/circuit.hpp"
#include "devices/vsource.hpp"
#include "devices/inductor.hpp"
#include <stdexcept>

namespace cudaspice {

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
    return idx;
}

std::string Circuit::node_name(int32_t idx) const {
    if (idx < 0 || idx >= static_cast<int32_t>(node_names_.size())) {
        throw std::out_of_range("Circuit::node_name: index out of range");
    }
    return node_names_[idx];
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

void Circuit::finalize() {
    // 1. Assign branch indices for devices that carry extra MNA variables.
    //    Branch indices start right after the node variables.
    int32_t branch_idx = next_node_;

    for (auto& dev : devices_) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            vs->set_branch_index(branch_idx);
            branch_idx += vs->extra_vars();
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->set_branch_index(branch_idx);
            branch_idx += ind->extra_vars();
        }
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
}

} // namespace cudaspice
