#include "devices/bsim3v32/bsim3v32_model_card.hpp"
#include "devices/bsim3v32/bsim3v32_def.hpp"
#include "devices/bsim3v32/bsim3v32_shim.hpp"
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

std::unique_ptr<BSIM3v32ModelCard> to_bsim3v32_card(const ModelCard& card) {
    auto out = std::make_unique<BSIM3v32ModelCard>();
    auto& ucb = out->ucb;

    if (card.type == "nmos") {
        ucb.BSIM3v32type = 1;
        ucb.BSIM3v32typeGiven = 1;
    } else if (card.type == "pmos") {
        ucb.BSIM3v32type = -1;
        ucb.BSIM3v32typeGiven = 1;
    } else {
        throw ParseError("Model '" + card.name + "': unsupported type '" + card.type + "' (expected NMOS/PMOS)");
    }

    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const bsim3v32::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < bsim3v32::BSIM3v32mPTSize; ++i) {
            if (std::strcmp(bsim3v32::BSIM3v32mPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &bsim3v32::BSIM3v32mPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr, "Warning: model '%s': unknown BSIM3v32 parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        bsim3v32::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & bsim3v32::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & bsim3v32::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & bsim3v32::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & bsim3v32::Shim::IF_STRING) {
            std::fprintf(stderr, "Warning: model '%s': string parameter '%s' not supported; using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = bsim3v32::BSIM3v32mParam(entry->id, &v, &ucb);
        if (rc != bsim3v32::Shim::OK) {
            throw ParseError("Model '" + card.name + "': BSIM3v32mParam failed for '" + lkey + "'");
        }
    }

    return out;
}

} // namespace neospice
