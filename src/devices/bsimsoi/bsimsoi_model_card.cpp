#include "devices/bsimsoi/bsimsoi_model_card.hpp"
#include "devices/bsimsoi/bsimsoi_def.hpp"
#include "devices/bsimsoi/bsimsoi_shim.hpp"
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

std::unique_ptr<B4SOIModelCard> to_bsimsoi_card(const ModelCard& card) {
    auto out = std::make_unique<B4SOIModelCard>();
    auto& ucb = out->ucb;

    if (card.type == "nmos") {
        ucb.B4SOItype = 1;
        ucb.B4SOItypeGiven = 1;
    } else if (card.type == "pmos") {
        ucb.B4SOItype = -1;
        ucb.B4SOItypeGiven = 1;
    } else {
        throw ParseError("Model '" + card.name + "': unsupported type '" + card.type + "' (expected NMOS/PMOS)");
    }

    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const bsimsoi::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < bsimsoi::B4SOImPTSize; ++i) {
            if (std::strcmp(bsimsoi::B4SOImPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &bsimsoi::B4SOImPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr, "Warning: model '%s': unknown B4SOI parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        bsimsoi::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & bsimsoi::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & bsimsoi::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & bsimsoi::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & bsimsoi::Shim::IF_STRING) {
            std::fprintf(stderr, "Warning: model '%s': string parameter '%s' not supported; using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = bsimsoi::B4SOImParam(entry->id, &v, &ucb);
        if (rc != bsimsoi::Shim::OK) {
            throw ParseError("Model '" + card.name + "': B4SOImParam failed for '" + lkey + "'");
        }
    }

    return out;
}

} // namespace neospice
