#include "devices/jfet2/jfet2_model_card.hpp"
#include "devices/jfet2/jfet2_def.hpp"
#include "devices/jfet2/jfet2_shim.hpp"
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

std::unique_ptr<JFET2ModelCard> to_jfet2_card(const ModelCard& card) {
    auto out = std::make_unique<JFET2ModelCard>();
    auto& ucb = out->ucb;

    if (card.type == "njf") {
        ucb.JFET2type = 1;
        /* type is always given */
    } else if (card.type == "pjf") {
        ucb.JFET2type = -1;
        /* type is always given */
    } else {
        throw ParseError("Model '" + card.name + "': unsupported type '" + card.type + "' (expected NJF/PJF)");
    }

    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const jfet2::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < jfet2::JFET2mPTSize; ++i) {
            if (std::strcmp(jfet2::JFET2mPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &jfet2::JFET2mPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr, "Warning: model '%s': unknown JFET2 parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        jfet2::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & jfet2::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & jfet2::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & jfet2::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & jfet2::Shim::IF_STRING) {
            std::fprintf(stderr, "Warning: model '%s': string parameter '%s' not supported; using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = jfet2::JFET2mParam(entry->id, &v, &ucb);
        if (rc != jfet2::Shim::OK) {
            throw ParseError("Model '" + card.name + "': JFET2mParam failed for '" + lkey + "'");
        }
    }

    return out;
}

} // namespace neospice
