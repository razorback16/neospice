#include "devices/hfet2/hfet2_model_card.hpp"
#include "devices/hfet2/hfet2_def.hpp"
#include "devices/hfet2/hfet2_shim.hpp"
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

std::unique_ptr<HFET2ModelCard> to_hfet2_card(const ModelCard& card) {
    auto out = std::make_unique<HFET2ModelCard>();
    auto& ucb = out->ucb;

    if (card.type == "nhfet") {
        ucb.HFET2type = 1;
    } else if (card.type == "phfet") {
        ucb.HFET2type = -1;
    } else {
        throw ParseError("Model '" + card.name + "': unsupported type '" + card.type + "' (expected NHFET/PHFET)");
    }

    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const hfet2::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < hfet2::HFET2mPTSize; ++i) {
            if (std::strcmp(hfet2::HFET2mPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &hfet2::HFET2mPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr, "Warning: model '%s': unknown HFET2 parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        hfet2::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & hfet2::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & hfet2::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & hfet2::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & hfet2::Shim::IF_STRING) {
            std::fprintf(stderr, "Warning: model '%s': string parameter '%s' not supported; using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = hfet2::HFET2mParam(entry->id, &v, &ucb);
        if (rc != hfet2::Shim::OK) {
            throw ParseError("Model '" + card.name + "': HFET2mParam failed for '" + lkey + "'");
        }
    }

    return out;
}

} // namespace neospice
