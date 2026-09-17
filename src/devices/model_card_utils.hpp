#pragma once
// Template-based model card parameter conversion, shared by all UCB device
// model card files.  Eliminates ~30 lines of identical boilerplate per device.

#include "core/types.hpp"        // ParseError
#include "parser/model_cards.hpp" // ModelCard
#include "parser/expression.hpp"
#include "devices/model_card_runtime.hpp"
#include <cmath>
#include <limits>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace neospice {

// ---------------------------------------------------------------------------
// UCB IF_* data-type bit masks (identical across all device Shim namespaces).
// ---------------------------------------------------------------------------
namespace ucb_if {
    constexpr int REAL    = 0x01;
    constexpr int INTEGER = 0x02;
    constexpr int STRING  = 0x04;
    constexpr int FLAG    = 0x08;
    constexpr int OK      = 0;
} // namespace ucb_if

// ---------------------------------------------------------------------------
// ModelCardTypeEntry — describes a single accepted SPICE type string.
// ---------------------------------------------------------------------------
struct ModelCardTypeEntry {
    const char* spice_name;  // e.g. "nmos", "pmos", "njf", "nhfet"
    int         value;       // e.g. 1 for N-type, -1 for P-type
};

// Runtime updates must reach a device's temperature preprocessing without
// changing its allocated nodes or leaving setup-derived coefficients stale.
// Keep this scope explicit until additional model parameters have that path.
inline bool supports_model_temperature_expression(std::string_view device,
                                                  std::string_view parameter) {
    if (device == "VDMOS")
        return parameter == "vto" || parameter == "kp" || parameter == "bv";
    if (device == "MOS1" || device == "MOS3")
        return parameter == "vto" || parameter == "kp";
    if (device == "BJT") return parameter == "bf";
    if (device == "DIO") return parameter == "is" || parameter == "bv";
    return false;
}

// Validate the card.type string against an array of accepted type entries.
// Returns the matching entry's value on success; throws ParseError on failure.
template <std::size_t N>
int validate_model_type(const ModelCard& card,
                        const ModelCardTypeEntry (&entries)[N])
{
    for (const auto& e : entries) {
        if (card.type == e.spice_name) return e.value;
    }
    // Build expected-types string for the error message.
    std::string expected;
    for (std::size_t i = 0; i < N; ++i) {
        if (i > 0) expected += '/';
        for (const char* p = entries[i].spice_name; *p; ++p)
            expected += static_cast<char>(
                std::toupper(static_cast<unsigned char>(*p)));
    }
    throw ParseError("Model '" + card.name + "': unsupported type '" +
                     card.type + "' (expected " + expected + ")");
}

// ---------------------------------------------------------------------------
// convert_model_card_params — generic parameter conversion loop.
//
// Template parameters:
//   IfParm    — parameter-table entry type   (e.g. ns::Shim::IfParm)
//   IfValue   — parameter-value union type    (e.g. ns::Shim::IfValue)
//   UCBModel  — UCB model struct type
//   MParamFn  — callable: int(int, IfValue*, UCBModel*)
// ---------------------------------------------------------------------------
template <typename IfParm, typename IfValue, typename UCBModel, typename MParamFn>
void convert_model_card_params(
    const ModelCard& card,
    UCBModel& ucb,
    const IfParm* ptable,
    int ptable_size,
    MParamFn mparam_fn,
    const char* device_label,
    ModelCardRuntime* runtime = nullptr)
{
    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const IfParm* entry = nullptr;
        for (int i = 0; i < ptable_size; ++i) {
            if (std::strcmp(ptable[i].keyword, lkey.c_str()) == 0) {
                entry = &ptable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr,
                "Warning: model '%s': unknown %s parameter '%s' (ignored)\n",
                card.name.c_str(), device_label, lkey.c_str());
            continue;
        }

        IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & ucb_if::REAL) {
            v.rValue = val;
        } else if (dtype & ucb_if::INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & ucb_if::FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & ucb_if::STRING) {
            std::fprintf(stderr,
                "Warning: model '%s': string parameter '%s' not supported; "
                "using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = mparam_fn(entry->id, &v, &ucb);
        if (rc != ucb_if::OK) {
            throw ParseError("Model '" + card.name + "': " + device_label +
                             "mParam failed for '" + lkey + "'");
        }
    }

    if (!card.temperature_expressions.empty()) {
        if (!runtime)
            throw ParseError("Model '" + card.name + "': temperature expressions require a model runtime");
        struct Update {
            const IfParm* entry;
            std::string key;
            std::string expression;
        };
        std::vector<Update> updates;
        for (auto it = card.temperature_expressions.rbegin();
             it != card.temperature_expressions.rend(); ++it) {
            const auto& [key, expression] = *it;
            const IfParm* entry = nullptr;
            for (int i = 0; i < ptable_size; ++i)
                if (key == ptable[i].keyword) { entry = &ptable[i]; break; }
            if (!entry) {
                std::fprintf(stderr, "Warning: model '%s': unknown %s parameter '%s' (ignored)\n",
                             card.name.c_str(), device_label, key.c_str());
                continue;
            }
            if (!supports_model_temperature_expression(device_label, key))
                throw ParseError("Model '" + card.name + "': temperature-dependent " +
                                 device_label + " parameter '" + key + "' is not implemented");
            const int dtype = entry->dataType & 0x1F;
            if (!(dtype & (ucb_if::REAL | ucb_if::INTEGER | ucb_if::FLAG)))
                throw ParseError("Model '" + card.name + "': non-numeric temperature expression for '" + key + "'");
            updates.push_back({entry, key, expression});
        }
        runtime->update_model_temperature =
            [model = &ucb, mparam_fn, updates = std::move(updates), name = card.name](double temperature) {
                const std::unordered_map<std::string, double> parameters{{"temper", temperature}};
                for (const auto& update : updates) {
                    const double val = eval_expression(update.expression, parameters, true);
                    if (!std::isfinite(val))
                        throw ParseError("Model '" + name + "': non-finite temperature expression for '" + update.key + "'");
                    IfValue value{};
                    const int dtype = update.entry->dataType & 0x1F;
                    if (dtype & ucb_if::REAL) value.rValue = val;
                    else if (dtype & ucb_if::INTEGER) {
                        if (val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max())
                            throw ParseError("Model '" + name + "': integer temperature expression out of range for '" + update.key + "'");
                        value.iValue = static_cast<int>(val);
                    }
                    else value.iValue = val != 0.0;
                    if (mparam_fn(update.entry->id, &value, model) != ucb_if::OK)
                        throw ParseError("Model '" + name + "': temperature update failed for '" + update.key + "'");
                }
            };
    }
}

} // namespace neospice
