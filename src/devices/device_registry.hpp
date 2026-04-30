#pragma once
#include "core/circuit.hpp"
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace neospice {

class Device;
struct ModelCard;

struct ParsedElement {
    virtual ~ParsedElement() = default;
    std::string name;
    std::string model_name;
    int line_number = 0;
};

struct ParseContext {
    Circuit& ckt;
    std::function<int32_t(const std::string&)> node;
    const std::unordered_map<std::string, ModelCard>& models;
    int line_number = 0;
    std::function<void(const std::string&)> error;
};

class DeviceRegistry {
public:
    struct ModelFactoryEntry {
        std::string type_group;  // "d", "bjt", "jfet", "mos", "hfet", "mes"
        int level = 0;           // 0 = default/any
        int priority = 0;
        std::function<std::unique_ptr<Circuit::ModelCardHolder>(const ModelCard&)> create;
    };

    struct DeviceBuilderEntry {
        char prefix;
        int priority = 0;
        std::function<std::unique_ptr<Device>(
            std::string_view name,
            std::span<const int32_t> nodes,
            const std::unordered_map<std::string, double>& params,
            Circuit::ModelCardHolder& card)> build;
    };

    struct ElementParserEntry {
        char prefix;
        std::function<std::unique_ptr<ParsedElement>(
            const std::vector<std::string>& tokens, ParseContext& ctx)> parse;
        std::function<void(
            std::vector<std::unique_ptr<ParsedElement>>& elements,
            const std::unordered_map<std::string, ModelCard>& models,
            Circuit& ckt, ParseContext& ctx)> resolve;
    };

    void add_model_factory(ModelFactoryEntry entry);
    void add_device_builder(DeviceBuilderEntry entry);
    void add_element_parser(ElementParserEntry entry);

    std::unique_ptr<Circuit::ModelCardHolder> create_model_card(
        const std::string& type, int level, const ModelCard& card) const;

    std::unique_ptr<Device> build_device(
        char prefix, std::string_view name,
        std::span<const int32_t> nodes,
        const std::unordered_map<std::string, double>& params,
        Circuit::ModelCardHolder& card) const;

    const ElementParserEntry* find_parser(char prefix) const;
    const std::unordered_map<char, ElementParserEntry>& element_parsers() const;

    void register_all();
    static DeviceRegistry& get_default();

private:
    // Model factories indexed by type_group
    std::unordered_map<std::string, std::vector<ModelFactoryEntry>> model_factories_;
    // Device builders indexed by prefix
    std::unordered_map<char, std::vector<DeviceBuilderEntry>> device_builders_;
    // Element parsers indexed by prefix
    std::unordered_map<char, ElementParserEntry> element_parsers_;
};

} // namespace neospice
