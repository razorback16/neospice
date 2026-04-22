#include "devices/mos3/mos3_model_card.hpp"
#include "devices/mos3/mos3_def.hpp"
#include "devices/mos3/mos3_shim.hpp"
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

std::unique_ptr<MOS3ModelCard> to_mos3_card(const ModelCard& card) {
    auto out = std::make_unique<MOS3ModelCard>();
    auto& ucb = out->ucb;

    if (card.type == "nmos") {
        ucb.MOS3type = 1;
        ucb.MOS3typeGiven = 1;
    } else if (card.type == "pmos") {
        ucb.MOS3type = -1;
        ucb.MOS3typeGiven = 1;
    } else {
        throw ParseError("Model '" + card.name + "': unsupported type '" + card.type + "' (expected NMOS/PMOS)");
    }

    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const mos3::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < mos3::MOS3mPTSize; ++i) {
            if (std::strcmp(mos3::MOS3mPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &mos3::MOS3mPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr, "Warning: model '%s': unknown MOS3 parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        mos3::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & mos3::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & mos3::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & mos3::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & mos3::Shim::IF_STRING) {
            std::fprintf(stderr, "Warning: model '%s': string parameter '%s' not supported; using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = mos3::MOS3mParam(entry->id, &v, &ucb);
        if (rc != mos3::Shim::OK) {
            throw ParseError("Model '" + card.name + "': MOS3mParam failed for '" + lkey + "'");
        }
    }

    return out;
}

} // namespace neospice
