#pragma once

#include "core/circuit.hpp"
#include "parser/expression.hpp"
#include "parser/model_cards.hpp"
#include "parser/subcircuit.hpp"
#include "parser/tokenizer.hpp"
#include "devices/asrc/asrc_device.hpp"
#include "devices/asrc/expression_ast.hpp"
#include "devices/device_registry.hpp"
#include "devices/vcvs_nonlinear.hpp"  // for TablePoint
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neospice {

/// A self-contained set of definitions extracted from a SPICE file.
/// Used by Circuit::include() / Circuit::X() to load subcircuits,
/// models, functions, and parameters without building an entire circuit.
struct DefinitionSet {
    std::unordered_map<std::string, SubcircuitDef> subcircuit_defs;
    std::unordered_map<std::string, ModelCard> models;
    std::unordered_map<std::string, FuncDef> func_defs;
    std::unordered_map<std::string, double> params;
};

struct ParseState {
    // Output circuit (reference, not value -- caller owns it)
    Circuit& ckt;

    // Tokenized lines (mutated across passes)
    std::vector<TokenizedLine> lines;

    // Subcircuit definitions (reference to parser's map)
    std::unordered_map<std::string, SubcircuitDef>& subcircuit_defs;

    // Expression/param state
    std::unordered_map<std::string, FuncDef> func_defs;
    std::unordered_map<std::string, double> global_params;
    std::unordered_map<std::string, double> params;
    std::unordered_set<std::string> global_nodes;

    // Model state
    // `models` is a *memoization cache*: a model card is fully parsed (via
    // parse_model_card + AKO resolution) only when it is first referenced by an
    // instance. See `model_raw` below and ensure_model() in netlist_parser.cpp.
    std::unordered_map<std::string, ModelCard> models;
    std::unordered_map<std::string, ResistorModel> res_models;
    std::unordered_map<std::string, CapacitorModel> cap_models;
    std::unordered_map<std::string, InductorModel> ind_models;
    int next_model_order = 0;
    int next_element_order = 0;

    // Lazy .model parsing support.
    //   raw_tokens : the original ".model ..." token vector for the card.
    //   key        : lowercased model name (matches the lowercased keys produced
    //                by subcircuit expansion and used everywhere for lookup).
    // Pass 1 populates this cheaply for every .model card; the expensive
    // parse_model_card work is deferred until ensure_model() is called for a
    // referenced model.
    struct RawModel {
        std::vector<std::string> tokens;
        int source_order = 0;
        bool parsed = false;       // memoized into `models` yet?
        bool resolving = false;    // cycle guard during AKO resolution
        std::string cache_key;     // key under which it lives in `models`
                                   // (original-case card name); valid iff parsed
    };
    std::unordered_map<std::string, RawModel> model_raw;

    // Deferred struct types (were anonymous structs inside parse())

    struct DeferredCCVS {
        std::string name;
        int32_t np, nn;
        std::string vsense_name;
        double rm;
        int line_number;
    };

    struct DeferredCCCS {
        std::string name;
        int32_t np, nn;
        std::string vsense_name;
        double gain;
        double m = 1.0;
        int line_number;
    };

    struct DeferredPolyCCVS {
        std::string name;
        int32_t np, nn;
        std::vector<std::string> vsense_names;
        std::vector<double> coeffs;
        int line_number;
    };

    struct DeferredPolyCCCS {
        std::string name;
        int32_t np, nn;
        std::vector<std::string> vsense_names;
        std::vector<double> coeffs;
        int line_number;
    };

    struct DeferredCoupledInductor {
        std::string name;
        std::string l1_name;
        std::string l2_name;
        double coupling;
        int line_number;
    };

    struct DeferredVSwitch {
        std::string name;
        int32_t np, nn, ncp, ncn;
        std::string model_name;
        int line_number;
    };

    struct DeferredCSwitch {
        std::string name;
        int32_t np, nn;
        std::string vsense_name;
        std::string model_name;
        int line_number;
    };

    struct DeferredLTRA {
        std::string name;
        int32_t p1p, p1n, p2p, p2n;
        std::string model_name;
        int line_number;
        // Initial conditions
        double ic_v1 = 0.0, ic_i1 = 0.0, ic_v2 = 0.0, ic_i2 = 0.0;
        bool ic_v1_given = false, ic_i1_given = false;
        bool ic_v2_given = false, ic_i2_given = false;
    };

    struct DeferredASRC {
        std::string name;
        int32_t np, nn;
        ASRCDevice::Mode mode;
        asrc::CompiledExpression expr;
        // Per-variable resolved data (filled at parse time for V() refs)
        std::vector<int32_t> node_indices;   // -1 = ground, -2 = TIME
        std::vector<int32_t> node_indices2;  // second node for V(n1,n2)
        // Names of vsources for I() refs (resolved later)
        std::vector<std::string> vsrc_names; // empty string if not I() ref
        int line_number;
        // Temperature coefficients
        double tc1 = 0.0;
        double tc2 = 0.0;
        double temp = -1.0;   // Kelvin, -1 = use sim default
        double dtemp = 0.0;   // Kelvin
    };

    struct DeferredTableVCCS {
        std::string name;
        int32_t np, nn;
        asrc::CompiledExpression expr;
        std::vector<int32_t> node_indices;
        std::vector<int32_t> node_indices2;
        std::vector<std::string> vsrc_names;
        std::vector<TablePoint> table_points;
        int line_number;
    };

    // Deferred element vectors
    std::vector<DeferredCCVS> deferred_ccvs;
    std::vector<DeferredCCCS> deferred_cccs;
    std::vector<DeferredPolyCCVS> deferred_poly_ccvs;
    std::vector<DeferredPolyCCCS> deferred_poly_cccs;
    std::vector<DeferredCoupledInductor> deferred_coupled_inductors;
    std::vector<DeferredVSwitch> deferred_vswitches;
    std::vector<DeferredCSwitch> deferred_cswitches;
    std::vector<DeferredLTRA> deferred_ltras;
    std::vector<DeferredASRC> deferred_asrcs;
    std::vector<DeferredTableVCCS> deferred_table_vccs;

    // DeviceRegistry parsed elements
    std::unordered_map<char, std::vector<std::unique_ptr<ParsedElement>>> parsed_elements;

    // Dialect
    SpiceDialect dialect = SpiceDialect::AUTO;

    // node_raw helper
    std::function<int32_t(const std::string&)> node_raw;

    explicit ParseState(Circuit& c, std::unordered_map<std::string, SubcircuitDef>& defs)
        : ckt(c), subcircuit_defs(defs) {
        node_raw = [this](const std::string& s) -> int32_t {
            return static_cast<int32_t>(ckt.node(s));
        };
    }
};

} // namespace neospice
