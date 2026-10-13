#pragma once
#include "core/circuit.hpp"
#include "parser/parse_state.hpp"
#include "parser/subcircuit.hpp"
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace neospice {

/// Parameter-independent front end of a parse: include resolution,
/// tokenization and subcircuit extraction. It is immutable once built and may
/// be shared by concurrent parse_prepared() calls.
struct PreparedNetlist {
    std::string title;
    SpiceDialect dialect = SpiceDialect::AUTO;
    std::vector<TokenizedLine> lines;  // top-level lines after subcircuit extraction
    std::unordered_map<std::string, SubcircuitDef> subcircuit_defs;
};

class NetlistParser {
public:
    Circuit parse(const std::string& netlist);
    Circuit parse_file(const std::string& filepath);

    /// Split parse() into a reusable front end and a per-circuit back end.
    /// parse_prepared(prepare(text)) builds the same circuit as parse(text);
    /// parameter overrides apply only in parse_prepared().
    PreparedNetlist prepare(const std::string& netlist);
    PreparedNetlist prepare_file(const std::string& filepath);
    Circuit parse_prepared(const PreparedNetlist& prepared);

    void set_parameter_overrides(const std::map<std::string, double>& values) {
        parameter_overrides_ = values;
    }
    void set_dialect(SpiceDialect d) { dialect_ = d; }
    SpiceDialect dialect() const { return dialect_; }

    /// Force PSpice/LTspice compatibility mode (options.pspice_compat) instead
    /// of deriving it from the auto-detected dialect. Mirrors ngspice's
    /// `-D ngbehavior=ps/lt/psa` override, which the netlist-keyword heuristic
    /// cannot infer for keyword-free vendor libraries. std::nullopt = auto.
    void set_force_pspice_compat(std::optional<bool> v) { force_pspice_compat_ = v; }

    const std::unordered_map<std::string, SubcircuitDef>& subcircuit_defs() const {
        return subcircuit_defs_;
    }

    /// Recursively resolve .include / .lib directives in a netlist string.
    /// base_dir: directory of the file containing this netlist (for relative paths)
    /// include_stack: set of canonical paths already being processed (for circular detection)
    std::string resolve_includes(const std::string& content,
                                 const std::string& base_dir,
                                 std::set<std::string>& include_stack);

    /// Load subcircuit, model, function, and parameter definitions from a file
    /// without building a full circuit.  Used by Circuit::include().
    DefinitionSet load_definitions(const std::string& filepath);

    /// Expand a subcircuit instance into an existing circuit.
    /// Looks up subckt_name in defs, synthesises an X-line, expands it,
    /// and runs pass1/pass2/pass3 to add the resulting devices to ckt.
    /// Does NOT call finalize — the caller is responsible for that.
    void expand_subcircuit_into(
        Circuit& ckt,
        const std::string& instance_name,
        const std::string& subckt_name,
        const std::vector<std::string>& port_nodes,
        const DefinitionSet& defs,
        const std::unordered_map<std::string, std::string>& instance_params = {});

private:
    std::map<std::string, double> parameter_overrides_;
    SpiceDialect dialect_ = SpiceDialect::AUTO;
    std::optional<bool> force_pspice_compat_;  // nullopt = derive from dialect_
    std::unordered_map<std::string, SubcircuitDef> subcircuit_defs_;

    SpiceDialect detect_dialect(const std::string& content) const;

    // Shared by parse_file() and prepare_file().
    std::string read_and_resolve_includes(const std::string& filepath);

    // Back end shared by parse() and parse_prepared(): passes 0.25 through 3.
    Circuit build_circuit(
        std::vector<TokenizedLine> lines,
        const std::unordered_map<std::string, SubcircuitDef>& subcircuit_defs,
        const std::string& title, SpiceDialect dialect);

    // Parser pass functions (share state via ParseState struct). Pass 0 runs
    // before a ParseState exists, so prepared definitions remain read-only.
    static void pass0_extract_subcircuits(
        std::vector<TokenizedLine>& lines,
        std::unordered_map<std::string, SubcircuitDef>& subcircuit_defs);
    static void pass025_resolve_funcs_params(ParseState& state);
    static void pass04_collect_globals(ParseState& state);
    static void pass05_expand_subcircuits(ParseState& state);
    static void pass1_collect_models_params(ParseState& state);
    static void pass2_parse_elements(ParseState& state);
    static void pass3_resolve_deferred(ParseState& state);

    // Lazy .model support: parse + AKO-resolve + memoize a single model card on
    // first reference. Returns a pointer to the cached ModelCard, or nullptr if
    // no .model with that name exists. `name` may be any case (it is lowercased
    // internally). Transitively materializes the AKO base if present.
    static ModelCard* ensure_model(ParseState& state, const std::string& name);

    // Force every .model card in `model_raw` to be parsed/resolved now. Used by
    // the load_definitions() / Circuit::include() API path, which packages the
    // full model set into a DefinitionSet.
    static void materialize_all_models(ParseState& state);
};

} // namespace neospice
