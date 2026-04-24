#pragma once
// Template-based model card parameter conversion, shared by all UCB device
// model card files.  Eliminates ~30 lines of identical boilerplate per device.

#include "core/types.hpp"        // ParseError
#include "parser/model_cards.hpp" // ModelCard
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

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
    const char* device_label)
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
}

} // namespace neospice
