#include "devices/hisim2/hisim2_model_card.hpp"
#include "devices/hisim2/hisim2_def.hpp"
#include "devices/hisim2/hisim2_shim.hpp"
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

std::unique_ptr<HSM2ModelCard> to_hisim2_card(const ModelCard& card) {
    auto out = std::make_unique<HSM2ModelCard>();
    auto& ucb = out->ucb;

    if (card.type == "nmos") {
        ucb.HSM2_type = 1;
        ucb.HSM2_type_Given = 1;
    } else if (card.type == "pmos") {
        ucb.HSM2_type = -1;
        ucb.HSM2_type_Given = 1;
    } else {
        throw ParseError("Model '" + card.name + "': unsupported type '" + card.type + "' (expected NMOS/PMOS)");
    }

    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const hisim2::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < hisim2::HSM2mPTSize; ++i) {
            if (std::strcmp(hisim2::HSM2mPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &hisim2::HSM2mPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr, "Warning: model '%s': unknown HSM2 parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        hisim2::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & hisim2::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & hisim2::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & hisim2::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & hisim2::Shim::IF_STRING) {
            std::fprintf(stderr, "Warning: model '%s': string parameter '%s' not supported; using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = hisim2::HSM2mParam(entry->id, &v, &ucb);
        if (rc != hisim2::Shim::OK) {
            throw ParseError("Model '" + card.name + "': HSM2mParam failed for '" + lkey + "'");
        }
    }

    return out;
}

} // namespace neospice
