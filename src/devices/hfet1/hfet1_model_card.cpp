#include "devices/hfet1/hfet1_model_card.hpp"
#include "devices/hfet1/hfet1_def.hpp"
#include "devices/hfet1/hfet1_shim.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace neospice {

static std::string to_lower_mc(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

std::unique_ptr<HFETAModelCard> to_hfet1_card(const ModelCard& card) {
    auto out = std::make_unique<HFETAModelCard>();
    auto& ucb = out->ucb;

    if (card.type == "nhfet") {
        ucb.HFETAtype = 1;   // NHFET
    } else if (card.type == "phfet") {
        ucb.HFETAtype = -1;  // PHFET
    } else {
        throw ParseError("Model '" + card.name + "': unsupported type '" + card.type + "' (expected NHFET/PHFET)");
    }

    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const hfet1::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < hfet1::HFETAmPTSize; ++i) {
            if (std::strcmp(hfet1::HFETAmPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &hfet1::HFETAmPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr, "Warning: model '%s': unknown HFETA parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        hfet1::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & hfet1::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & hfet1::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & hfet1::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & hfet1::Shim::IF_STRING) {
            std::fprintf(stderr, "Warning: model '%s': string parameter '%s' not supported; using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = hfet1::HFETAmParam(entry->id, &v, &ucb);
        if (rc != hfet1::Shim::OK) {
            throw ParseError("Model '" + card.name + "': HFETAmParam failed for '" + lkey + "'");
        }
    }

    return out;
}

} // namespace neospice
