#include "devices/mos9/mos9_model_card.hpp"
#include "devices/mos9/mos9_def.hpp"
#include "devices/mos9/mos9_shim.hpp"
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

std::unique_ptr<MOS9ModelCard> to_mos9_card(const ModelCard& card) {
    auto out = std::make_unique<MOS9ModelCard>();
    auto& ucb = out->ucb;

    if (card.type == "nmos") {
        ucb.MOS9type = 1;
        ucb.MOS9typeGiven = 1;
    } else if (card.type == "pmos") {
        ucb.MOS9type = -1;
        ucb.MOS9typeGiven = 1;
    } else {
        throw ParseError("Model '" + card.name + "': unsupported type '" + card.type + "' (expected NMOS/PMOS)");
    }

    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const mos9::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < mos9::MOS9mPTSize; ++i) {
            if (std::strcmp(mos9::MOS9mPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &mos9::MOS9mPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr, "Warning: model '%s': unknown MOS9 parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        mos9::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & mos9::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & mos9::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & mos9::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & mos9::Shim::IF_STRING) {
            std::fprintf(stderr, "Warning: model '%s': string parameter '%s' not supported; using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = mos9::MOS9mParam(entry->id, &v, &ucb);
        if (rc != mos9::Shim::OK) {
            throw ParseError("Model '" + card.name + "': MOS9mParam failed for '" + lkey + "'");
        }
    }

    return out;
}

} // namespace neospice
