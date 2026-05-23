#include "devices/mosfet_common.hpp"
#include "parser/model_cards.hpp"
#include "parser/tokenizer.hpp"
#include "devices/mos1/mos1_device.hpp"
#include "devices/mos1/mos1_model_card.hpp"
#include "devices/mos3/mos3_device.hpp"
#include "devices/mos3/mos3_model_card.hpp"
#include "devices/mos9/mos9_device.hpp"
#include "devices/mos9/mos9_model_card.hpp"
#include "devices/bsim3/bsim3_device.hpp"
#include "devices/bsim3/bsim3_model_card.hpp"
#include "devices/bsim3v32/bsim3v32_device.hpp"
#include "devices/bsim3v32/bsim3v32_model_card.hpp"
#include "devices/bsim4v7/bsim4v7_model_card.hpp"
#include "devices/bsimsoi/bsimsoi_device.hpp"
#include "devices/bsimsoi/bsimsoi_model_card.hpp"
#include "devices/hisim2/hisim2_device.hpp"
#include "devices/hisim2/hisim2_model_card.hpp"
#include "devices/hisimhv/hisimhv_device.hpp"
#include "devices/hisimhv/hisimhv_model_card.hpp"
#include <algorithm>
#include <cstdio>

namespace neospice {

namespace {
std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}
} // anonymous namespace

std::unique_ptr<ParsedElement> parse_mosfet_element(
    const std::vector<std::string>& tokens, ParseContext& ctx)
{
    // M name nd ng ns nb [nsub] modelname [W=.. L=.. NF=.. AD=.. AS=.. PD=..
    //                                      PS=.. NRD=.. NRS=.. SA=.. SB=.. SD=..]
    // 5-terminal form (e.g. HiSIM_HV): M1 d g s b sub modelname ...
    if (tokens.size() < 6) {
        ctx.error("M card requires name, nd, ng, ns, nb, modelname");
        return nullptr;
    }
    auto m = std::make_unique<ParsedMosfet>();
    m->name        = tokens[0];
    m->nd          = ctx.node(tokens[1]);
    m->ng          = ctx.node(tokens[2]);
    m->ns          = ctx.node(tokens[3]);
    m->nb          = ctx.node(tokens[4]);
    m->line_number = ctx.line_number;

    // Detect 5- or 6-terminal M-cards by scanning forward from
    // tokens[5].  The rule: any non-model-name, non-key=value token
    // before the first model-name token is an extra terminal node.
    //
    // 4-terminal (standard MOSFET):  M1 d g s b model [params]
    // 5-terminal (HiSIM_HV):         M1 d g s b sub model [params]
    // 6-terminal (BSIMSOI):           M1 d g s e p b model [params]
    size_t param_start = 6;
    std::string tok5_lower = to_lower(tokens[5]);
    bool tok5_is_model = (ctx.models.find(tokens[5]) != ctx.models.end() ||
                          ctx.models.find(tok5_lower) != ctx.models.end());
    if (!tok5_is_model && tokens.size() >= 7 &&
        tokens[5].find('=') == std::string::npos &&
        tokens[6].find('=') == std::string::npos) {
        // tokens[5] is an extra terminal.  Check whether tokens[6]
        // is the model name (5-terminal) or yet another node
        // (6-terminal).
        std::string tok6_lower = to_lower(tokens[6]);
        bool tok6_is_model = (ctx.models.find(tokens[6]) != ctx.models.end() ||
                              ctx.models.find(tok6_lower) != ctx.models.end());
        if (!tok6_is_model && tokens.size() >= 8 &&
            tokens[6].find('=') == std::string::npos &&
            tokens[7].find('=') == std::string::npos) {
            // 6-terminal form (e.g. BSIMSOI): tokens[5]=p, tokens[6]=b
            m->nsub = ctx.node(tokens[5]);
            m->nsub_given = true;
            m->npnode = ctx.node(tokens[6]);
            m->nbulk_given = true;
            m->model_name = tokens[7];
            param_start = 8;
        } else {
            // 5-terminal form: tokens[5] is substrate node
            m->nsub = ctx.node(tokens[5]);
            m->nsub_given = true;
            m->model_name = tokens[6];
            param_start = 7;
        }
    } else {
        m->model_name = tokens[5];
    }

    for (size_t i = param_start; i < tokens.size(); ++i) {
        auto eq = tokens[i].find('=');
        if (eq == std::string::npos) continue;
        std::string key = to_lower(tokens[i].substr(0, eq));
        if (key == "ic") {
            // ic=VDS,VGS,VBS  or  ic=VDS,VGS  or  ic=VDS
            std::string valstr = tokens[i].substr(eq + 1);
            std::vector<double> icvals;
            size_t start = 0;
            while (start < valstr.size()) {
                size_t comma = valstr.find(',', start);
                if (comma == std::string::npos) comma = valstr.size();
                std::string field = valstr.substr(start, comma - start);
                if (!field.empty())
                    icvals.push_back(parse_spice_number(field));
                start = comma + 1;
            }
            if (icvals.size() >= 1) { m->ic_vds = icvals[0]; m->ic_vds_given = true; }
            if (icvals.size() >= 2) { m->ic_vgs = icvals[1]; m->ic_vgs_given = true; }
            if (icvals.size() >= 3) { m->ic_vbs = icvals[2]; m->ic_vbs_given = true; }
            continue;
        }
        double val = parse_spice_number(tokens[i].substr(eq + 1));
        if      (key == "w")   { m->geom.W = val; m->wGiven = true; }
        else if (key == "l")   { m->geom.L = val; m->lGiven = true; }
        else if (key == "nf")  m->geom.NF  = val;
        else if (key == "ad")  m->geom.AD  = val;
        else if (key == "as")  m->geom.AS  = val;
        else if (key == "pd")  m->geom.PD  = val;
        else if (key == "ps")  m->geom.PS  = val;
        else if (key == "nrd") m->geom.NRD = val;
        else if (key == "nrs") m->geom.NRS = val;
        else if (key == "sa")  m->geom.SA  = val;
        else if (key == "sb")  m->geom.SB  = val;
        else if (key == "sd")  m->geom.SD  = val;
        else if (key == "m")   m->geom.M   = val;
        // Silently ignore other unknown M-card keys
    }
    return m;
}

void resolve_mosfets(
    std::vector<std::unique_ptr<ParsedElement>>& elements,
    const std::unordered_map<std::string, ModelCard>& models,
    Circuit& ckt, ParseContext& ctx)
{
    // Resolve deferred MOSFETs.  Level dispatch:
    //   LEVEL=1           -> MOS1 (Shichman-Hodges)
    //   LEVEL=9           -> MOS9 (Modified Level 3)
    //   LEVEL=8 or 49     -> BSIM3v3
    //   LEVEL=14 (default)-> BSIM4v7
    //   LEVEL=61 or 68    -> HiSIM2
    //   LEVEL=73           -> HiSIM_HV (5-terminal high-voltage MOSFET)
    //   LEVEL=10 or 58     -> BSIMSOI  (6-terminal SOI MOSFET)
    std::unordered_map<std::string, std::unique_ptr<BSIM4v7ModelCard>> bsim4_cards;
    std::unordered_map<std::string, std::unique_ptr<MOS1ModelCard>> mos1_cards;
    std::unordered_map<std::string, std::unique_ptr<MOS3ModelCard>> mos3_cards;
    std::unordered_map<std::string, std::unique_ptr<MOS9ModelCard>> mos9_cards;
    std::unordered_map<std::string, std::unique_ptr<BSIM3ModelCard>> bsim3_cards;
    std::unordered_map<std::string, std::unique_ptr<BSIM3v32ModelCard>> bsim3v32_cards;
    std::unordered_map<std::string, std::unique_ptr<HSM2ModelCard>> hisim2_cards;
    std::unordered_map<std::string, std::unique_ptr<HSMHVModelCard>> hisimhv_cards;
    std::unordered_map<std::string, std::unique_ptr<B4SOIModelCard>> bsimsoi_cards;
    std::unordered_map<std::string, int> mosfet_levels;

    for (const auto& elem : elements) {
        const auto& m = static_cast<const ParsedMosfet&>(*elem);
        auto it = models.find(m.model_name);
        if (it == models.end()) {
            fprintf(stderr, "Warning: Line %d: Unknown model '%s' — skipping MOSFET '%s'\n",
                    m.line_number, m.model_name.c_str(), m.name.c_str());
            continue;
        }

        try {

        // Detect MOSFET level (cached per model name)
        int level;
        auto lev_it = mosfet_levels.find(m.model_name);
        if (lev_it != mosfet_levels.end()) {
            level = lev_it->second;
        } else {
            level = detect_mosfet_level(it->second);
            mosfet_levels[m.model_name] = level;
        }

        if (level == 1) {
            // MOS1 Level 1 Shichman-Hodges
            auto card_it = mos1_cards.find(m.model_name);
            if (card_it == mos1_cards.end()) {
                try {
                    card_it = mos1_cards.emplace(m.model_name,
                                                  to_mos1_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            MOS1Device::Geom mos1_geom;
            mos1_geom.W   = m.geom.W;
            mos1_geom.L   = m.geom.L;
            mos1_geom.AD  = m.geom.AD;
            mos1_geom.AS  = m.geom.AS;
            mos1_geom.PD  = m.geom.PD;
            mos1_geom.PS  = m.geom.PS;
            mos1_geom.NRD = m.geom.NRD;
            mos1_geom.NRS = m.geom.NRS;
            mos1_geom.M   = m.geom.M;
            mos1_geom.wGiven = m.wGiven;
            mos1_geom.lGiven = m.lGiven;
            auto dev = MOS1Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                        mos1_geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 3) {
            // MOS3 Level 3 MOSFET
            auto card_it = mos3_cards.find(m.model_name);
            if (card_it == mos3_cards.end()) {
                try {
                    card_it = mos3_cards.emplace(m.model_name,
                                                  to_mos3_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            MOS3Device::Geom mos3_geom;
            mos3_geom.W   = m.geom.W;
            mos3_geom.L   = m.geom.L;
            mos3_geom.AD  = m.geom.AD;
            mos3_geom.AS  = m.geom.AS;
            mos3_geom.PD  = m.geom.PD;
            mos3_geom.PS  = m.geom.PS;
            mos3_geom.NRD = m.geom.NRD;
            mos3_geom.NRS = m.geom.NRS;
            mos3_geom.M   = m.geom.M;
            mos3_geom.wGiven = m.wGiven;
            mos3_geom.lGiven = m.lGiven;
            auto dev = MOS3Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                        mos3_geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 7 || level == 8 || level == 49) {
            // BSIM3 — dispatch to v3.2x or v3.3 based on VERSION param.
            // VERSION < 3.3 -> BSIM3v32, otherwise -> BSIM3 v3.3.
            auto ver_it = it->second.params.find("version");
            bool use_v32 = false;
            if (ver_it != it->second.params.end() && ver_it->second < 3.3)
                use_v32 = true;

            if (use_v32) {
                auto card_it = bsim3v32_cards.find(m.model_name);
                if (card_it == bsim3v32_cards.end()) {
                    try {
                        card_it = bsim3v32_cards.emplace(m.model_name,
                                                          to_bsim3v32_card(it->second)).first;
                    } catch (const ParseError& e) {
                        throw ParseError("Line " + std::to_string(m.line_number) +
                                         ": " + e.what());
                    }
                }
                BSIM3v32Device::Geom g32;
                g32.W   = m.geom.W;
                g32.L   = m.geom.L;
                g32.AD  = m.geom.AD;
                g32.AS  = m.geom.AS;
                g32.PD  = m.geom.PD;
                g32.PS  = m.geom.PS;
                g32.NRD = m.geom.NRD;
                g32.NRS = m.geom.NRS;
                g32.M   = m.geom.M;
                auto dev = BSIM3v32Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                                 g32, *card_it->second);
                if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                    dev->set_ic(m.ic_vds, m.ic_vds_given,
                                m.ic_vgs, m.ic_vgs_given,
                                m.ic_vbs, m.ic_vbs_given);
                }
                ckt.add_device(std::move(dev));
            } else {
                auto card_it = bsim3_cards.find(m.model_name);
                if (card_it == bsim3_cards.end()) {
                    try {
                        card_it = bsim3_cards.emplace(m.model_name,
                                                      to_bsim3_card(it->second)).first;
                    } catch (const ParseError& e) {
                        throw ParseError("Line " + std::to_string(m.line_number) +
                                         ": " + e.what());
                    }
                }
                BSIM3Device::Geom g3;
                g3.W   = m.geom.W;
                g3.L   = m.geom.L;
                g3.AD  = m.geom.AD;
                g3.AS  = m.geom.AS;
                g3.PD  = m.geom.PD;
                g3.PS  = m.geom.PS;
                g3.NRD = m.geom.NRD;
                g3.NRS = m.geom.NRS;
                g3.M   = m.geom.M;
                auto dev = BSIM3Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                             g3, *card_it->second);
                if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                    dev->set_ic(m.ic_vds, m.ic_vds_given,
                                m.ic_vgs, m.ic_vgs_given,
                                m.ic_vbs, m.ic_vbs_given);
                }
                ckt.add_device(std::move(dev));
            }
        } else if (level == 9) {
            // MOS9 Modified Level 3
            auto card_it = mos9_cards.find(m.model_name);
            if (card_it == mos9_cards.end()) {
                try {
                    card_it = mos9_cards.emplace(m.model_name,
                                                  to_mos9_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            MOS9Device::Geom mos9_geom;
            mos9_geom.W   = m.geom.W;
            mos9_geom.L   = m.geom.L;
            mos9_geom.AD  = m.geom.AD;
            mos9_geom.AS  = m.geom.AS;
            mos9_geom.PD  = m.geom.PD;
            mos9_geom.PS  = m.geom.PS;
            mos9_geom.NRD = m.geom.NRD;
            mos9_geom.NRS = m.geom.NRS;
            mos9_geom.M   = m.geom.M;
            mos9_geom.wGiven = m.wGiven;
            mos9_geom.lGiven = m.lGiven;
            auto dev = MOS9Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                        mos9_geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 14) {
            // BSIM4v7 (LEVEL=14 or default)
            auto card_it = bsim4_cards.find(m.model_name);
            if (card_it == bsim4_cards.end()) {
                try {
                    card_it = bsim4_cards.emplace(m.model_name,
                                                  to_bsim4_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            auto dev = BSIM4v7Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                           m.geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 61 || level == 68) {
            // HiSIM2
            auto card_it = hisim2_cards.find(m.model_name);
            if (card_it == hisim2_cards.end()) {
                try {
                    card_it = hisim2_cards.emplace(m.model_name,
                                                    to_hisim2_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            HSM2Device::Geom hsm2_geom;
            hsm2_geom.W   = m.geom.W;
            hsm2_geom.L   = m.geom.L;
            hsm2_geom.M   = m.geom.M;
            hsm2_geom.NF  = m.geom.NF;
            hsm2_geom.AD  = m.geom.AD;
            hsm2_geom.AS  = m.geom.AS;
            hsm2_geom.PD  = m.geom.PD;
            hsm2_geom.PS  = m.geom.PS;
            hsm2_geom.NRD = m.geom.NRD;
            hsm2_geom.NRS = m.geom.NRS;
            auto dev = HSM2Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                        hsm2_geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 73) {
            // HiSIM_HV (5-terminal high-voltage MOSFET)
            auto card_it = hisimhv_cards.find(m.model_name);
            if (card_it == hisimhv_cards.end()) {
                try {
                    card_it = hisimhv_cards.emplace(m.model_name,
                                                    to_hisimhv_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            HSMHVDevice::Geom hv_geom;
            hv_geom.W   = m.geom.W;
            hv_geom.L   = m.geom.L;
            hv_geom.M   = m.geom.M;
            hv_geom.NF  = m.geom.NF;
            hv_geom.AD  = m.geom.AD;
            hv_geom.AS  = m.geom.AS;
            hv_geom.PD  = m.geom.PD;
            hv_geom.PS  = m.geom.PS;
            hv_geom.NRD = m.geom.NRD;
            hv_geom.NRS = m.geom.NRS;
            // Use 5th terminal (substrate) if given; pass GROUND_INTERNAL-1
            // as sentinel for "no substrate node" (make() maps it to UCB -1).
            int32_t n_sub = m.nsub_given ? m.nsub : (GROUND_INTERNAL - 1);
            auto dev = HSMHVDevice::make(m.name, m.nd, m.ng, m.ns, m.nb, n_sub,
                                         hv_geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 10 || level == 58) {
            // BSIMSOI (6-terminal SOI MOSFET)
            auto card_it = bsimsoi_cards.find(m.model_name);
            if (card_it == bsimsoi_cards.end()) {
                try {
                    card_it = bsimsoi_cards.emplace(m.model_name,
                                                     to_bsimsoi_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            B4SOIDevice::Geom soi_geom;
            soi_geom.W   = m.geom.W;
            soi_geom.L   = m.geom.L;
            soi_geom.M   = m.geom.M;
            soi_geom.NF  = m.geom.NF;
            soi_geom.AD  = m.geom.AD;
            soi_geom.AS  = m.geom.AS;
            soi_geom.PD  = m.geom.PD;
            soi_geom.PS  = m.geom.PS;
            soi_geom.NRD = m.geom.NRD;
            soi_geom.NRS = m.geom.NRS;
            soi_geom.SA  = m.geom.SA;
            soi_geom.SB  = m.geom.SB;
            soi_geom.SD  = m.geom.SD;
            // BSIMSOI terminal mapping from ParsedMosfet:
            //   nd=drain, ng=gate, ns=source, nb=e(substrate/backgate),
            //   nsub=p(body-contact), npnode=b(bulk)
            // For 4-terminal M-cards (no extra terminals given), the extra
            // nodes default to GROUND_INTERNAL which maps to ground.
            int32_t n_e = m.nb;                                       // 4th positional
            int32_t n_p = m.nsub_given ? m.nsub : GROUND_INTERNAL;    // 5th positional or ground
            int32_t n_b = m.nbulk_given ? m.npnode : GROUND_INTERNAL; // 6th positional or ground
            auto dev = B4SOIDevice::make(m.name, m.nd, m.ng, m.ns,
                                          n_e, n_p, n_b,
                                          soi_geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else {
            throw ParseError("Line " + std::to_string(m.line_number) +
                             ": Unsupported MOSFET LEVEL=" +
                             std::to_string(level) + " for model '" +
                             m.model_name + "'");
        }

        } catch (const ParseError& e) {
            fprintf(stderr, "Warning: %s — skipping MOSFET '%s'\n",
                    e.what(), m.name.c_str());
            continue;
        }
    }
    // Transfer card ownership to the Circuit (cards must outlive the devices).
    for (auto& [name, card] : bsim4_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : mos1_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : mos3_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : mos9_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : bsim3_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : bsim3v32_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : hisim2_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : hisimhv_cards) {
        ckt.add_model_card(std::move(card));
    }
    for (auto& [name, card] : bsimsoi_cards) {
        ckt.add_model_card(std::move(card));
    }
}

void register_mosfet_parser(DeviceRegistry& reg) {
    DeviceRegistry::ElementParserEntry entry;
    entry.prefix = 'm';
    entry.parse = parse_mosfet_element;
    entry.resolve = resolve_mosfets;
    reg.add_element_parser(std::move(entry));
}

} // namespace neospice
