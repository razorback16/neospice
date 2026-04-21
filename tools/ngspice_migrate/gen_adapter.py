"""Device adapter generator for migrated ngspice models.

Emits a C++ header (hpp) and implementation (cpp) that bridge the
neospice ``Device`` interface to the translated UCB code.  The
generated adapter follows the same architecture as the hand-written
``src/devices/bsim4v7/bsim4v7_device.hpp/cpp``.

The generator is parameterised by a :class:`ModelDescriptor` (or any
object satisfying the ``_Desc`` protocol) so the same logic can emit
adapters for diodes, BJTs, MOSFETs, and any other migrated model.
"""

from __future__ import annotations

import re
from typing import List, Optional, Protocol


# ---------------------------------------------------------------------------
# Minimal protocol for the descriptor fields we consume
# ---------------------------------------------------------------------------

class _CleanupLinkedList(Protocol):
    field: str
    next_field: str


class _VersionStamp(Protocol):
    field: str
    given_field: str
    value: str


class _GeomParam(Protocol):
    name: str
    field: str
    given: str
    default: str
    always_given: bool


class _Terminal(Protocol):
    name: str
    field: str


class _Desc(Protocol):
    prefix: str
    neospice_name: str
    namespace: str
    cpp_instance: str
    cpp_model: str
    terminals: List[_Terminal]
    state_count: int
    state_base_field: str
    next_instance_field: str
    instances_field: str
    next_model_field: str
    model_ptr_field: str
    name_field: str
    setup_function: str
    temp_function: str
    load_function: str
    geometry: List[_GeomParam]
    has_internal_nodes: bool
    cleanup_linked_lists: List[_CleanupLinkedList]
    version_stamp: Optional[_VersionStamp]


# ---------------------------------------------------------------------------
# TSTALLOC extraction
# ---------------------------------------------------------------------------

def extract_tstalloc_fields(setup_source: str, ptr_suffix: str = "Ptr") -> List[str]:
    """Extract matrix pointer field names from TSTALLOC calls in translated setup source.

    Returns a list of unique field names (e.g. ``["BSIM4v7DPbpPtr", ...]``)
    preserving first-occurrence order.
    """
    pattern = rf'TSTALLOC\(\s*([A-Za-z0-9_]+{re.escape(ptr_suffix)})\s*,'
    seen: set[str] = set()
    result: list[str] = []
    for m in re.finditer(pattern, setup_source):
        name = m.group(1)
        if name not in seen:
            seen.add(name)
            result.append(name)
    return result


# ---------------------------------------------------------------------------
# Header generator
# ---------------------------------------------------------------------------

def generate_adapter_hpp(desc: _Desc) -> str:
    """Return the complete text of the device adapter header file."""
    parts: list[str] = []
    ns = desc.namespace
    prefix = desc.prefix
    has_cleanup = bool(desc.cleanup_linked_lists)

    # --- Preamble ----------------------------------------------------------
    parts.append("#pragma once\n")
    parts.append(f'// Adapter bridging the neospice Device interface to the UCB {prefix} code.\n')
    parts.append("\n")
    parts.append('#include "devices/device.hpp"\n')
    parts.append(f'#include "devices/{ns}/{ns}_def.hpp"\n')
    parts.append(f'#include "devices/{ns}/{ns}_shim.hpp"\n')
    parts.append("#include <memory>\n")
    parts.append("#include <optional>\n")
    parts.append("#include <utility>\n")
    parts.append("#include <vector>\n")
    parts.append("\n")

    # --- Namespace ---------------------------------------------------------
    parts.append("namespace neospice {\n")
    parts.append("\n")

    # --- ModelCard struct ---------------------------------------------------
    parts.append(f"struct {prefix}ModelCard {{\n")
    parts.append(f"    {ns}::{desc.cpp_model} ucb{{}};   // aggregate UCB model fields\n")
    parts.append("\n")
    parts.append(f"    {prefix}ModelCard() = default;\n")
    parts.append(f"    ~{prefix}ModelCard();\n")
    if has_cleanup:
        parts.append(f"    {prefix}ModelCard({prefix}ModelCard&&) noexcept = delete;\n")
    parts.append("\n")
    parts.append("    // Non-copyable / non-movable: UCB model is threaded with raw\n")
    parts.append("    // pointers; copying would alias them.\n")
    parts.append(f"    {prefix}ModelCard(const {prefix}ModelCard&)            = delete;\n")
    parts.append(f"    {prefix}ModelCard& operator=(const {prefix}ModelCard&) = delete;\n")
    parts.append("};\n")
    parts.append("\n")

    # --- Device class ------------------------------------------------------
    parts.append(f"class {prefix}Device : public Device {{\n")
    parts.append("public:\n")

    # Geom struct
    parts.append("    struct Geom {\n")
    for gp in desc.geometry:
        default = gp.default if gp.default else "0.0"
        parts.append(f"        double {gp.name} = {default};\n")
    parts.append("    };\n")
    parts.append("\n")

    # Factory
    term_params = ", ".join(
        f"int32_t n_{t.name}" for t in desc.terminals
    )
    parts.append(f"    static std::unique_ptr<{prefix}Device> make(\n")
    parts.append(f"        std::string name, {term_params},\n")
    parts.append(f"        const Geom& geom, {prefix}ModelCard& shared_card);\n")
    parts.append("\n")

    # Device interface overrides
    parts.append("    // Device interface\n")
    parts.append("    void declare_internal_nodes(Circuit& ckt) override;\n")
    parts.append("    void stamp_pattern(SparsityBuilder& builder) const override;\n")
    parts.append("    void assign_offsets(const SparsityPattern& pattern) override;\n")
    parts.append("    void evaluate(const std::vector<double>& voltages,\n")
    parts.append("                  NumericMatrix& mat, std::vector<double>& rhs) override;\n")
    parts.append("    void ac_stamp(const std::vector<double>& voltages,\n")
    parts.append("                  NumericMatrix& G, NumericMatrix& C) override;\n")
    parts.append("\n")
    parts.append(f"    int32_t state_vars() const override {{ return {desc.state_count}; }}\n")
    parts.append("    void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override;\n")
    parts.append("    double compute_trunc(const IntegratorCtx& ctx,\n")
    parts.append("                         const SimOptions& opts) const override;\n")
    parts.append("    bool device_converged() const override;\n")
    parts.append("    std::optional<double> query_param(const std::string& name) const override;\n")
    parts.append("    void reset_temp() override { temp_done_ = false; }\n")
    parts.append("\n")
    parts.append("    std::vector<NoiseSource> noise_sources(\n")
    parts.append("        double freq, const std::vector<double>& dc_solution) const override;\n")
    parts.append("\n")

    # Private section
    parts.append("private:\n")
    parts.append(f"    explicit {prefix}Device(std::string name) : Device(std::move(name)) {{}}\n")
    parts.append("\n")
    parts.append(f"    mutable {ns}::{desc.cpp_instance} inst_{{}};\n")
    parts.append(f"    {ns}::{desc.cpp_model}* model_ = nullptr;\n")
    parts.append("\n")
    parts.append("    std::vector<std::pair<int,int>> journal_;\n")
    parts.append("\n")
    parts.append("    double* state0_ = nullptr;\n")
    parts.append("    double* state1_ = nullptr;\n")
    parts.append("    double* state2_ = nullptr;\n")
    parts.append("    int32_t state_base_ = -1;\n")
    parts.append("\n")
    parts.append("    mutable bool temp_done_ = false;\n")
    parts.append("    mutable int last_noncon_ = 0;\n")
    parts.append("\n")
    parts.append("    int32_t max_neo_node_ = -1;\n")
    parts.append("\n")
    parts.append("    std::vector<double> ghost_voltages_;\n")
    parts.append("    std::vector<double> ghost_rhs_;\n")

    parts.append("};\n")
    parts.append("\n")
    parts.append("} // namespace neospice\n")

    return "".join(parts)


# ---------------------------------------------------------------------------
# Implementation generator
# ---------------------------------------------------------------------------

def generate_adapter_cpp(desc: _Desc, setup_source: str = "", def_content: str = "") -> str:
    """Return the complete text of the device adapter implementation file.

    Parameters
    ----------
    desc:
        Model descriptor.
    setup_source:
        Translated setup .cpp source text.  When provided, TSTALLOC field
        names are extracted and used to emit RESOLVE() calls in
        assign_offsets.  When empty, a TODO comment is emitted instead.
    def_content:
        Generated *_def.hpp header text.  When provided, MatrixOffset fields
        are extracted directly from the header (most reliable).  Falls back
        to TSTALLOC extraction from *setup_source* when empty.
    """
    parts: list[str] = []
    ns = desc.namespace
    name = desc.neospice_name
    prefix = desc.prefix
    has_cleanup = bool(desc.cleanup_linked_lists)

    # --- Includes ----------------------------------------------------------
    parts.append(f'#include "devices/{ns}/{ns}_device.hpp"\n')
    parts.append("\n")
    parts.append('#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx\n')
    parts.append('#include "core/types.hpp"          // SimOptions defaults\n')
    parts.append("\n")
    parts.append("#include <cmath>\n")
    if has_cleanup:
        parts.append("#include <cstdlib>\n")
    parts.append("#include <optional>\n")
    parts.append("#include <stdexcept>\n")
    parts.append("#include <string>\n")
    parts.append("\n")

    # --- Forward declarations for UCB entry-point functions -----------------
    parts.append(f"// Forward declarations for translated UCB functions.\n")
    parts.append(f"namespace neospice::{ns} {{\n")
    parts.append(f"    int {desc.setup_function}(Shim::Matrix*, {desc.cpp_model}*, Shim::Ckt*, int*);\n")
    parts.append(f"    int {desc.temp_function}({desc.cpp_model}*, Shim::Ckt*);\n")
    parts.append(f"    int {desc.load_function}({desc.cpp_model}*, Shim::Ckt*);\n")
    parts.append("}\n")
    parts.append("\n")

    # --- Namespace ---------------------------------------------------------
    parts.append("namespace neospice {\n")
    parts.append("\n")
    parts.append(f"using namespace neospice::{ns};\n")
    parts.append("\n")

    # --- ModelCard destructor ----------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"// {prefix}ModelCard destructor\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    if has_cleanup:
        parts.append(f"{prefix}ModelCard::~{prefix}ModelCard() {{\n")
        for cl in desc.cleanup_linked_lists:
            parts.append(f"    auto* p = ucb.{cl.field};\n")
            parts.append("    while (p) {\n")
            parts.append(f"        auto* next = p->{cl.next_field};\n")
            parts.append("        std::free(p);\n")
            parts.append("        p = next;\n")
            parts.append("    }\n")
            parts.append(f"    ucb.{cl.field} = nullptr;\n")
        parts.append("}\n")
    else:
        parts.append(f"{prefix}ModelCard::~{prefix}ModelCard() = default;\n")
    parts.append("\n")

    # --- neo_to_ucb helper -------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// Neospice -> UCB node translation.\n")
    parts.append("// Neospice uses GROUND_INTERNAL = -1 for ground and consecutive non-negative\n")
    parts.append("// indices for real nodes.  UCB uses 0 for ground and >=1 for real nodes.\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("static inline int neo_to_ucb(int32_t neo) {\n")
    parts.append("    return (neo < 0) ? 0 : (neo + 1);\n")
    parts.append("}\n")
    parts.append("\n")

    # --- make() factory ----------------------------------------------------
    term_params = ", ".join(
        f"int32_t n_{t.name}" for t in desc.terminals
    )
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// make\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"std::unique_ptr<{prefix}Device>\n")
    parts.append(f"{prefix}Device::make(std::string name,\n")
    parts.append(f"        {term_params},\n")
    parts.append(f"        const Geom& geom, {prefix}ModelCard& shared_card) {{\n")
    parts.append(f"    std::unique_ptr<{prefix}Device> dev(new {prefix}Device(std::move(name)));\n")
    parts.append("    dev->model_ = &shared_card.ucb;\n")
    parts.append("\n")

    # Version stamp (before anything else touches the model)
    vs = desc.version_stamp
    if vs:
        parts.append(f"    if (!shared_card.ucb.{vs.given_field}) {{\n")
        parts.append(f'        shared_card.ucb.{vs.field} = "{vs.value}";\n')
        parts.append(f"        shared_card.ucb.{vs.given_field} = 1;\n")
        parts.append("    }\n")
        parts.append("\n")

    parts.append("    auto& inst = dev->inst_;\n")
    parts.append(f"    inst.{desc.name_field} = dev->name().c_str();\n")
    parts.append(f"    inst.{desc.model_ptr_field} = dev->model_;\n")
    parts.append("\n")

    # Node wiring
    parts.append("    // Node wiring (UCB convention).\n")
    for t in desc.terminals:
        parts.append(f"    inst.{t.field} = neo_to_ucb(n_{t.name});\n")
    parts.append("\n")

    # Geometry wiring
    if desc.geometry:
        parts.append("    // Geometry.\n")
        for gp in desc.geometry:
            parts.append(f"    inst.{gp.field} = geom.{gp.name};\n")
            if gp.always_given:
                parts.append(f"    inst.{gp.given} = 1;\n")
            else:
                cmp_val = gp.default if gp.default else "0.0"
                parts.append(f"    inst.{gp.given} = (geom.{gp.name} != {cmp_val}) ? 1 : 0;\n")
        parts.append("\n")

    # Thread onto instance list
    parts.append("    // Thread onto the shared model's instance list.\n")
    parts.append(f"    inst.{desc.next_instance_field} = shared_card.ucb.{desc.instances_field};\n")
    parts.append(f"    shared_card.ucb.{desc.instances_field} = &inst;\n")
    parts.append("\n")

    # Compute max_neo_node_
    node_vars = ", ".join(f"n_{t.name}" for t in desc.terminals)
    parts.append("    // Remember the widest real node index for ghost array sizing.\n")
    parts.append("    int32_t widest = -1;\n")
    parts.append(f"    for (int32_t n : {{{node_vars}}}) if (n > widest) widest = n;\n")
    parts.append("    dev->max_neo_node_ = widest;\n")
    parts.append("\n")
    parts.append("    return dev;\n")
    parts.append("}\n")
    parts.append("\n")

    # --- declare_internal_nodes --------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// declare_internal_nodes\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"void {prefix}Device::declare_internal_nodes(Circuit& ckt) {{\n")
    parts.append("    SparsityBuilder scratch(1);\n")
    parts.append("    Shim::Matrix shim_matrix(scratch);\n")
    parts.append("\n")
    parts.append("    Shim::Ckt setup_ckt;\n")
    parts.append("    setup_ckt.CKTtemp    = T_NOMINAL;\n")
    parts.append("    setup_ckt.CKTnomTemp = T_NOMINAL;\n")
    parts.append("    setup_ckt.CKTinternalNodeCounter = 1000;\n")
    parts.append("\n")

    if desc.has_internal_nodes:
        parts.append("    setup_ckt.node_alloc = [&ckt, this](const char* name) -> int {\n")
        parts.append('        std::string full = "__" + name_ + "_" + name;\n')
        parts.append("        int32_t neo = ckt.node(full);\n")
        parts.append("        return neo + 1;  // UCB convention: ground=0, real>=1\n")
        parts.append("    };\n")
        parts.append("\n")

    parts.append("    int states = 0;\n")
    parts.append(f"    int rc = {desc.setup_function}(&shim_matrix, model_, &setup_ckt, &states);\n")
    parts.append("    if (rc != Shim::OK) {\n")
    parts.append(f'        throw std::runtime_error("{desc.setup_function} failed with rc=" + std::to_string(rc));\n')
    parts.append("    }\n")
    parts.append("\n")
    parts.append("    const auto& journal = shim_matrix.reservation_journal();\n")
    parts.append("    journal_.assign(journal.begin(), journal.end());\n")
    parts.append("\n")
    parts.append("    // Recompute max_neo_node_ to cover internal nodes.\n")
    parts.append("    for (auto [r, c] : journal_) {\n")
    parts.append("        int mx = std::max(r, c);\n")
    parts.append("        if (mx > 0) {\n")
    parts.append("            int32_t neo = mx - 1;\n")
    parts.append("            if (neo > max_neo_node_) max_neo_node_ = neo;\n")
    parts.append("        }\n")
    parts.append("    }\n")
    parts.append("}\n")
    parts.append("\n")

    # --- stamp_pattern -----------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// stamp_pattern\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"void {prefix}Device::stamp_pattern(SparsityBuilder& builder) const {{\n")
    parts.append("    for (auto [r, c] : journal_) {\n")
    parts.append("        if (r <= 0 || c <= 0) continue;\n")
    parts.append("        builder.add(r - 1, c - 1);\n")
    parts.append("    }\n")
    parts.append("}\n")
    parts.append("\n")

    # --- assign_offsets ----------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// assign_offsets\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"void {prefix}Device::assign_offsets(const SparsityPattern& pattern) {{\n")
    parts.append("    std::vector<MatrixOffset> offsets(journal_.size(), -1);\n")
    parts.append("    for (std::size_t i = 0; i < journal_.size(); ++i) {\n")
    parts.append("        auto [r, c] = journal_[i];\n")
    parts.append("        if (r <= 0 || c <= 0) continue;\n")
    parts.append("        offsets[i] = pattern.offset(r - 1, c - 1);\n")
    parts.append("    }\n")
    parts.append("\n")
    parts.append("#define RESOLVE(f)                                                       \\\n")
    parts.append("    do {                                                                 \\\n")
    parts.append("        if (inst_.f >= 0)                                                \\\n")
    parts.append("            inst_.f = offsets[inst_.f];                                  \\\n")
    parts.append("    } while (0)\n")
    parts.append("\n")

    # Primary: extract fields from the generated def header (100% reliable).
    # Fallback: extract from TSTALLOC calls in the translated setup source.
    if def_content:
        from .gen_def import extract_matrix_offset_fields
        resolve_fields = extract_matrix_offset_fields(def_content)
    elif setup_source:
        resolve_fields = extract_tstalloc_fields(setup_source, desc.matrix_ptr_suffix)
    else:
        resolve_fields = []

    if resolve_fields:
        for fld in resolve_fields:
            parts.append(f"    RESOLVE({fld});\n")
    else:
        parts.append(f"    // TODO: add RESOLVE() calls for each {prefix}*{desc.matrix_ptr_suffix} field\n")
        parts.append("    // (model-specific; must match TSTALLOC sites in the setup file).\n")
    parts.append("\n")
    parts.append("#undef RESOLVE\n")
    parts.append("}\n")
    parts.append("\n")

    # --- set_state_ptrs ----------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// set_state_ptrs\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"void {prefix}Device::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {{\n")
    parts.append("    state0_ = s0;\n")
    parts.append("    state1_ = s1;\n")
    parts.append("    state2_ = s2;\n")
    parts.append("    state_base_ = base;\n")
    parts.append(f"    inst_.{desc.state_base_field} = base;\n")
    parts.append("}\n")
    parts.append("\n")

    # --- evaluate ----------------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// evaluate\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"void {prefix}Device::evaluate(const std::vector<double>& voltages,\n")
    parts.append(f"        {' ' * len(prefix)}                NumericMatrix& mat,\n")
    parts.append(f"        {' ' * len(prefix)}                std::vector<double>& rhs) {{\n")
    parts.append("    const int n_real = static_cast<int>(rhs.size());\n")
    parts.append("    const int n_ghost = (max_neo_node_ >= 0 ? max_neo_node_ + 1 : 0) + 1;\n")
    parts.append("\n")
    parts.append("    ghost_voltages_.assign(n_ghost, 0.0);\n")
    parts.append("    ghost_rhs_.assign(n_ghost, 0.0);\n")
    parts.append("    for (int32_t k = 0; k <= max_neo_node_ && k < n_real; ++k) {\n")
    parts.append("        ghost_voltages_[k + 1] = voltages[k];\n")
    parts.append("    }\n")
    parts.append("\n")
    parts.append("    Shim::Ckt ckt;\n")
    parts.append("\n")
    parts.append("    // Integrator context.\n")
    parts.append("    const IntegratorCtx* ic = tls_integrator_ctx;\n")
    parts.append("    if (ic) {\n")
    parts.append("        ckt.CKTmode  = ic->mode;\n")
    parts.append("        ckt.CKTorder = ic->order;\n")
    parts.append("        ckt.CKTdelta = ic->delta;\n")
    parts.append("        for (int i = 0; i < 8; ++i) ckt.CKTag[i]       = ic->ag[i];\n")
    parts.append("        for (int i = 0; i < 8; ++i) ckt.CKTdeltaOld[i] = ic->delta_old[i];\n")
    parts.append("    } else {\n")
    parts.append("        ckt.CKTmode  = 0x10 | 0x200;  // MODEDCOP | MODEINITJCT\n")
    parts.append("        ckt.CKTorder = 1;\n")
    parts.append("    }\n")
    parts.append("\n")
    parts.append("    // SimOptions.\n")
    parts.append("    const SimOptions* sim_opts = (ic ? ic->options : nullptr);\n")
    parts.append("    SimOptions fallback;\n")
    parts.append("    if (!sim_opts) sim_opts = &fallback;\n")
    parts.append("    ckt.CKTtemp    = sim_opts->temp;\n")
    parts.append("    ckt.CKTnomTemp = sim_opts->temp;\n")
    parts.append("    ckt.CKTgmin    = sim_opts->gmin;\n")
    parts.append("    ckt.CKTreltol  = sim_opts->reltol;\n")
    parts.append("    ckt.CKTabstol  = sim_opts->abstol;\n")
    parts.append("    ckt.CKTvoltTol = sim_opts->vntol;\n")
    parts.append("    ckt.CKTbypass  = 0;\n")
    parts.append("    ckt.CKTnoncon  = 0;\n")
    parts.append("\n")
    parts.append("    // State ring.\n")
    parts.append("    ckt.CKTstate0 = state0_;\n")
    parts.append("    ckt.CKTstate1 = state1_;\n")
    parts.append("    ckt.CKTstate2 = state2_;\n")
    parts.append("\n")
    parts.append("    // Ghost rhs / old-iterate pointers.\n")
    parts.append("    ckt.CKTrhs    = ghost_rhs_.data();\n")
    parts.append("    ckt.CKTrhsOld = ghost_voltages_.data();\n")
    parts.append("    ckt.mat       = &mat;\n")
    parts.append("\n")

    # First-call temp
    parts.append(f"    // First-call {desc.temp_function}.\n")
    parts.append("    if (!temp_done_) {\n")
    parts.append(f"        int rc = {desc.temp_function}(model_, &ckt);\n")
    parts.append("        if (rc != Shim::OK) {\n")
    parts.append(f'            throw std::runtime_error("{desc.temp_function} failed with rc=" + std::to_string(rc));\n')
    parts.append("        }\n")
    parts.append("        temp_done_ = true;\n")
    parts.append("    }\n")
    parts.append("\n")

    # Instance chain splicing + load call
    parts.append("    // Splice this instance as sole member, call load, then restore.\n")
    parts.append(f"    {desc.cpp_instance}* saved_head      = model_->{desc.instances_field};\n")
    parts.append(f"    {desc.cpp_instance}* saved_next_inst = inst_.{desc.next_instance_field};\n")
    parts.append(f"    {desc.cpp_model}*    saved_next_mod  = model_->{desc.next_model_field};\n")
    parts.append(f"    model_->{desc.instances_field}  = &inst_;\n")
    parts.append(f"    inst_.{desc.next_instance_field} = nullptr;\n")
    parts.append(f"    model_->{desc.next_model_field}  = nullptr;\n")
    parts.append(f"    int rc = {desc.load_function}(model_, &ckt);\n")
    parts.append(f"    model_->{desc.instances_field}  = saved_head;\n")
    parts.append(f"    inst_.{desc.next_instance_field} = saved_next_inst;\n")
    parts.append(f"    model_->{desc.next_model_field}  = saved_next_mod;\n")
    parts.append("    if (rc != Shim::OK) {\n")
    parts.append(f'        throw std::runtime_error("{desc.load_function} failed with rc=" + std::to_string(rc));\n')
    parts.append("    }\n")
    parts.append("\n")

    # Capture convergence flag
    parts.append("    last_noncon_ = ckt.CKTnoncon;\n")
    parts.append("\n")

    # Fold ghost rhs back
    parts.append("    // Fold ghost rhs contributions back into the real rhs.\n")
    parts.append("    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {\n")
    parts.append("        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];\n")
    parts.append("    }\n")
    parts.append("}\n")
    parts.append("\n")

    # --- ac_stamp (stub) ---------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// ac_stamp — TODO: implement G/C matrix split from ngspice AC load file\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"void {prefix}Device::ac_stamp(const std::vector<double>& /*voltages*/,\n")
    parts.append(f"        {' ' * len(prefix)}                NumericMatrix& /*G*/,\n")
    parts.append(f"        {' ' * len(prefix)}                NumericMatrix& /*C*/) {{\n")
    parts.append(f'    // TODO: Port the AC stamp from the ngspice *acld.c file.\n')
    parts.append(f'    // Conductances (gm, gds, ...) go into G; capacitances (Cgs, Cgd, ...) into C.\n')
    parts.append(f'    // See existing implementations: bsim4v7_device.cpp, bjt_device.cpp.\n')
    parts.append("}\n")
    parts.append("\n")

    # --- compute_trunc (stub) ----------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// compute_trunc — TODO: implement LTE calculation from charge state variables\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"double {prefix}Device::compute_trunc(const IntegratorCtx& /*ctx*/,\n")
    parts.append(f"        {' ' * len(prefix)}                  const SimOptions& /*opts*/) const {{\n")
    parts.append("    // TODO: Identify charge state variables (from NIintegrate calls in load)\n")
    parts.append("    // and compute LTE-based timestep. See bjt_device.cpp for the pattern.\n")
    parts.append("    return 1e30;\n")
    parts.append("}\n")
    parts.append("\n")

    # --- device_converged --------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// device_converged\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"bool {prefix}Device::device_converged() const {{\n")
    parts.append("    return last_noncon_ == 0;\n")
    parts.append("}\n")
    parts.append("\n")

    # --- query_param (stub) ------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// query_param — TODO: add operating-point parameter mappings\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"std::optional<double> {prefix}Device::query_param(const std::string& /*name*/) const {{\n")
    parts.append("    // TODO: Map parameter names to instance fields. See bjt_device.cpp.\n")
    parts.append("    return std::nullopt;\n")
    parts.append("}\n")
    parts.append("\n")

    # --- noise_sources (stub) ----------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// noise_sources — TODO: implement device noise model\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"std::vector<Device::NoiseSource> {prefix}Device::noise_sources(\n")
    parts.append(f"        double /*freq*/, const std::vector<double>& /*dc_solution*/) const {{\n")
    parts.append("    // TODO: Port noise sources from the ngspice *noise.c file.\n")
    parts.append("    // Common noise types:\n")
    parts.append("    //   Thermal: 4*k*T*G  (conductance noise)\n")
    parts.append("    //   Shot:    2*q*|I|  (junction current noise)\n")
    parts.append("    //   Flicker: KF*|I|^AF / f^EF  (1/f noise)\n")
    parts.append("    // Use sim_temp() for temperature (inherited from Device base class).\n")
    parts.append("    // See bjt_device.cpp and bsim4v7_device.cpp for examples.\n")
    parts.append("    return {};\n")
    parts.append("}\n")
    parts.append("\n")

    # --- Close namespace ---------------------------------------------------
    parts.append("} // namespace neospice\n")

    return "".join(parts)
