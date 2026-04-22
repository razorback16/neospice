#include "devices/hisimhv/hisimhv_model_card.hpp"
#include "devices/hisimhv/hisimhv_def.hpp"
#include "devices/hisimhv/hisimhv_shim.hpp"
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

std::unique_ptr<HSMHVModelCard> to_hisimhv_card(const ModelCard& card) {
    auto out = std::make_unique<HSMHVModelCard>();
    auto& ucb = out->ucb;

    if (card.type == "nmos") {
        ucb.HSMHV_type = 1;
        ucb.HSMHV_type_Given = 1;
    } else if (card.type == "pmos") {
        ucb.HSMHV_type = -1;
        ucb.HSMHV_type_Given = 1;
    } else {
        throw ParseError("Model '" + card.name + "': unsupported type '" + card.type + "' (expected NMOS/PMOS)");
    }

    for (const auto& [lkey, val] : card.params) {
        if (lkey == "level") continue;

        const hisimhv::Shim::IfParm* entry = nullptr;
        for (int i = 0; i < hisimhv::HSMHVmPTSize; ++i) {
            if (std::strcmp(hisimhv::HSMHVmPTable[i].keyword, lkey.c_str()) == 0) {
                entry = &hisimhv::HSMHVmPTable[i];
                break;
            }
        }
        if (entry == nullptr) {
            std::fprintf(stderr, "Warning: model '%s': unknown HSMHV parameter '%s' (ignored)\n",
                card.name.c_str(), lkey.c_str());
            continue;
        }

        hisimhv::Shim::IfValue v{};
        int dtype = entry->dataType & 0x1F;
        if (dtype & hisimhv::Shim::IF_REAL) {
            v.rValue = val;
        } else if (dtype & hisimhv::Shim::IF_INTEGER) {
            v.iValue = static_cast<int>(val);
        } else if (dtype & hisimhv::Shim::IF_FLAG) {
            v.iValue = (val != 0.0) ? 1 : 0;
        } else if (dtype & hisimhv::Shim::IF_STRING) {
            std::fprintf(stderr, "Warning: model '%s': string parameter '%s' not supported; using default\n",
                card.name.c_str(), lkey.c_str());
            continue;
        } else {
            continue;
        }

        int rc = hisimhv::HSMHVmParam(entry->id, &v, &ucb);
        if (rc != hisimhv::Shim::OK) {
            throw ParseError("Model '" + card.name + "': HSMHVmParam failed for '" + lkey + "'");
        }
    }

    return out;
}

} // namespace neospice
