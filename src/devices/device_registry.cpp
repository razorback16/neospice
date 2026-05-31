#include "devices/device_registry.hpp"
#include <algorithm>

namespace neospice {

void DeviceRegistry::add_model_factory(ModelFactoryEntry entry) {
    model_factories_[entry.type_group].push_back(std::move(entry));
}

void DeviceRegistry::add_device_builder(DeviceBuilderEntry entry) {
    device_builders_[entry.prefix].push_back(std::move(entry));
}

void DeviceRegistry::add_element_parser(ElementParserEntry entry) {
    char prefix = entry.prefix;
    element_parsers_[prefix] = std::move(entry);
}

static std::string normalize_type_group(const std::string& type) {
    if (type == "nmos" || type == "pmos") return "mos";
    if (type == "npn"  || type == "pnp" || type == "lpnp")  return "bjt";
    if (type == "njf"  || type == "pjf")  return "jfet";
    if (type == "nhfet"|| type == "phfet") return "hfet";
    if (type == "nmf"  || type == "pmf")  return "mes";
    if (type == "d")                      return "d";
    return type;
}

std::unique_ptr<Circuit::ModelCardHolder> DeviceRegistry::create_model_card(
    const std::string& type, int level, const ModelCard& card) const
{
    auto group = normalize_type_group(type);
    auto it = model_factories_.find(group);
    if (it == model_factories_.end()) return nullptr;

    const auto& entries = it->second;

    // Find best match: exact level first, then level=0 as fallback.
    // Among matches, use highest priority.
    const ModelFactoryEntry* best = nullptr;
    bool best_exact = false;

    for (const auto& e : entries) {
        bool exact = (e.level == level);
        bool fallback = (e.level == 0);
        if (!exact && !fallback) continue;

        if (!best) {
            best = &e;
            best_exact = exact;
        } else if (exact && !best_exact) {
            // Exact match beats fallback
            best = &e;
            best_exact = true;
        } else if (exact == best_exact && e.priority > best->priority) {
            // Same match type — prefer higher priority
            best = &e;
        }
    }

    if (!best) return nullptr;
    return best->create(card);
}

std::unique_ptr<Device> DeviceRegistry::build_device(
    char prefix, std::string_view name,
    std::span<const int32_t> nodes,
    const std::unordered_map<std::string, double>& params,
    Circuit::ModelCardHolder& card) const
{
    auto it = device_builders_.find(prefix);
    if (it == device_builders_.end()) return nullptr;

    // Sort by priority descending, try each builder
    auto entries = it->second;  // copy so we can sort
    std::sort(entries.begin(), entries.end(),
              [](const DeviceBuilderEntry& a, const DeviceBuilderEntry& b) {
                  return a.priority > b.priority;
              });

    for (const auto& e : entries) {
        auto dev = e.build(name, nodes, params, card);
        if (dev) return dev;
    }
    return nullptr;
}

const DeviceRegistry::ElementParserEntry*
DeviceRegistry::find_parser(char prefix) const {
    auto it = element_parsers_.find(prefix);
    if (it == element_parsers_.end()) return nullptr;
    return &it->second;
}

const std::unordered_map<char, DeviceRegistry::ElementParserEntry>&
DeviceRegistry::element_parsers() const {
    return element_parsers_;
}

// Forward declarations for per-device registration functions (model + device)
void register_dio(DeviceRegistry&);
void register_bjt(DeviceRegistry&);
void register_vbic(DeviceRegistry&);
void register_jfet(DeviceRegistry&);
void register_jfet2(DeviceRegistry&);
void register_mos1(DeviceRegistry&);
void register_mos3(DeviceRegistry&);
void register_mos9(DeviceRegistry&);
void register_bsim3(DeviceRegistry&);
void register_bsim3v32(DeviceRegistry&);
void register_bsim4v7(DeviceRegistry&);
void register_bsimsoi(DeviceRegistry&);
void register_hisim2(DeviceRegistry&);
void register_hisimhv(DeviceRegistry&);
void register_hfet1(DeviceRegistry&);
void register_hfet2(DeviceRegistry&);
void register_mes(DeviceRegistry&);

// Forward declarations for element parser registration functions
void register_dio_parser(DeviceRegistry&);
void register_mosfet_parser(DeviceRegistry&);
void register_bjt_parser(DeviceRegistry&);
void register_jfet_parser(DeviceRegistry&);
void register_hfet_parser(DeviceRegistry&);

void DeviceRegistry::register_all() {
    register_dio(*this);
    register_bjt(*this);
    register_vbic(*this);
    register_jfet(*this);
    register_jfet2(*this);
    register_mos1(*this);
    register_mos3(*this);
    register_mos9(*this);
    register_bsim3(*this);
    register_bsim3v32(*this);
    register_bsim4v7(*this);
    register_bsimsoi(*this);
    register_hisim2(*this);
    register_hisimhv(*this);
    register_hfet1(*this);
    register_hfet2(*this);
    register_mes(*this);

    // Register element parsers for semiconductor device families
    register_dio_parser(*this);
    register_mosfet_parser(*this);
    register_bjt_parser(*this);
    register_jfet_parser(*this);
    register_hfet_parser(*this);
}

DeviceRegistry& DeviceRegistry::get_default() {
    static DeviceRegistry instance = []() {
        DeviceRegistry reg;
        reg.register_all();
        return reg;
    }();
    return instance;
}

} // namespace neospice
