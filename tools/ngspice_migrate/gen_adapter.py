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
from typing import Dict, List, Optional, Protocol, Tuple


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
# NIintegrate charge offset extraction
# ---------------------------------------------------------------------------

def extract_charge_offsets(load_source: str, prefix: str) -> list[str]:
    """Extract charge state offset field names from NIintegrate calls."""
    pattern = rf'NIintegrate\s*\([^,]+,[^,]+,[^,]+,[^,]+,\s*here->({re.escape(prefix)}\w+)\s*\)'
    return list(dict.fromkeys(re.findall(pattern, load_source)))


# ---------------------------------------------------------------------------
# Output parameter extraction from devsup pTable
# ---------------------------------------------------------------------------

def extract_output_params(devsup_source: str, prefix: str) -> list[dict]:
    """Extract parameter entries from instance pTable in devsup source.

    Returns list of dicts: {'name': str, 'id': str, 'type': str, 'is_output': bool}
    """
    results: list[dict] = []
    pattern = r'(IOP[ARU]*|OP[R]?)\s*\(\s*"(\w+)"\s*,\s*(\w+)\s*,\s*(\w+)'
    for m in re.finditer(pattern, devsup_source):
        macro, name, param_id, ptype = m.groups()
        # OP / OPR are pure output (operating-point) parameters.
        # IOP is input-or-output (geometry); treated as non-output for
        # query_param generation so it gets a direct field reference.
        is_output = macro in ("OP", "OPR")
        results.append({
            'name': name,
            'id': param_id,
            'type': ptype,
            'is_output': is_output,
        })
    return results


# ---------------------------------------------------------------------------
# AC stamp extraction from *acld.c
# ---------------------------------------------------------------------------

def extract_ac_stamps(acld_source: str, prefix: str) -> Dict[str, List[Dict]]:
    """Extract AC matrix stamps from an ngspice *acld.c source file.

    Returns a dict with:
      'real_stamps': [{ptr_field, value_expr}]  — entries for G matrix
      'imag_stamps': [{ptr_field, value_expr}]  — entries for C matrix (divide by omega)

    The ngspice AC load pattern is:
      *(here->XXXPtr)     += real_value     → goes to G
      *(here->XXXPtr + 1) += imag_value     → goes to C (value/omega = cap)
    """
    real_stamps: List[Dict[str, str]] = []
    imag_stamps: List[Dict[str, str]] = []

    # Match *(here->FIELDPtr) += expr  and  *(here->FIELDPtr + 1) += expr
    # Real: *(here->XXXPtr) += value
    real_pat = re.compile(
        r'\*\(\s*here->(' + re.escape(prefix) + r'\w+Ptr)\s*\)\s*\+=\s*(.+?)\s*;'
    )
    # Imag: *(here->XXXPtr + 1) += value
    imag_pat = re.compile(
        r'\*\(\s*here->(' + re.escape(prefix) + r'\w+Ptr)\s*\+\s*1\s*\)\s*\+=\s*(.+?)\s*;'
    )

    for m in imag_pat.finditer(acld_source):
        imag_stamps.append({
            'ptr_field': m.group(1),
            'value_expr': m.group(2).strip(),
        })

    for m in real_pat.finditer(acld_source):
        real_stamps.append({
            'ptr_field': m.group(1),
            'value_expr': m.group(2).strip(),
        })

    return {
        'real_stamps': real_stamps,
        'imag_stamps': imag_stamps,
    }


# ---------------------------------------------------------------------------
# Noise source extraction from *noi.c
# ---------------------------------------------------------------------------

def extract_noise_sources(noi_source: str, prefix: str) -> List[Dict]:
    """Extract noise source definitions from an ngspice noise file.

    Returns a list of dicts with:
      'name':      str  — noise source name (e.g. "rd", "id", "1ovf")
      'type':      str  — "thermal", "shot", "flicker", or "custom"
      'node1':     str  — first node field (e.g. "HSM2dNodePrime")
      'node2':     str  — second node field (e.g. "HSM2dNode")
      'psd_expr':  str  — PSD expression or description
    """
    sources: List[Dict] = []

    # Extract noise source names from the static array
    names_pat = re.compile(r'"\.(\w+)"')
    noise_names = names_pat.findall(noi_source)

    # Match THERMNOISE macro calls
    therm_pat = re.compile(
        r'NevalSrc\s*\([^,]+,\s*[^,]+,\s*\w+,\s*THERMNOISE\s*,'
        r'\s*here->(\w+)\s*,\s*here->(\w+)\s*,'
        r'\s*(.+?)\s*\)'
    )
    for m in therm_pat.finditer(noi_source):
        sources.append({
            'name': f'thermal_{m.group(1)}',
            'type': 'thermal',
            'node1': m.group(1),
            'node2': m.group(2),
            'psd_expr': f'4*k*T*({m.group(3).strip()})',
        })

    # Match SHOTNOISE macro calls
    shot_pat = re.compile(
        r'NevalSrc\s*\([^,]+,\s*[^,]+,\s*\w+,\s*SHOTNOISE\s*,'
        r'\s*here->(\w+)\s*,\s*here->(\w+)\s*,'
        r'\s*here->(\w+)\s*\)'
    )
    for m in shot_pat.finditer(noi_source):
        sources.append({
            'name': f'shot_{m.group(3)}',
            'type': 'shot',
            'node1': m.group(1),
            'node2': m.group(2),
            'psd_expr': f'2*q*|here->{m.group(3)}|',
        })

    # Match N_GAIN pattern followed by *= 4*CONSTboltz*T*G (thermal with gain)
    # Common HiSIM pattern: NevalSrc(..., N_GAIN, node1, node2, 0.0);
    # noizDens[X] *= 4 * CONSTboltz * TTEMP * G;
    gain_pat = re.compile(
        r'NevalSrc\s*\([^,]+,\s*[^,]+,\s*\w+,\s*N_GAIN\s*,'
        r'\s*here->(\w+)\s*,\s*here->(\w+)\s*,'
        r'\s*\(double\)\s*0\.0\s*\)\s*;'
        r'[^;]*?noizDens\[\w+\]\s*\*=\s*(.+?)\s*;',
        re.DOTALL
    )
    for m in gain_pat.finditer(noi_source):
        expr = m.group(3).strip()
        node1 = m.group(1)
        node2 = m.group(2)
        # Classify based on expression
        if 'CONSTboltz' in expr and 'freq' not in expr.lower():
            ntype = 'thermal'
        elif 'freq' in expr.lower() or 'pow' in expr.lower():
            ntype = 'flicker'
        else:
            ntype = 'custom'
        sources.append({
            'name': f'{ntype}_{node1}_{node2}',
            'type': ntype,
            'node1': node1,
            'node2': node2,
            'psd_expr': expr,
        })

    # Extract flicker noise patterns
    flicker_pat = re.compile(
        r'NevalSrc\s*\([^,]+,\s*[^,]+,\s*\w+,\s*N_GAIN\s*,'
        r'\s*here->(\w+)\s*,\s*here->(\w+)\s*,'
        r'\s*\(double\)\s*0\.0\s*\)\s*;'
        r'[^;]*?noizDens\[\w+\]\s*\*=\s*([^;]*(?:freq|pow)[^;]*)\s*;',
        re.DOTALL
    )
    for m in flicker_pat.finditer(noi_source):
        # Avoid duplicates from the gain_pat above
        found = False
        for s in sources:
            if s['node1'] == m.group(1) and s['node2'] == m.group(2) and s['type'] == 'flicker':
                found = True
                break
        if not found:
            sources.append({
                'name': f'flicker_{m.group(1)}_{m.group(2)}',
                'type': 'flicker',
                'node1': m.group(1),
                'node2': m.group(2),
                'psd_expr': m.group(3).strip(),
            })

    return sources


def _gen_noise_sources(desc, noise_sources: List[Dict]) -> str:
    """Generate noise_sources() body from extracted noise source definitions."""
    if not noise_sources:
        return (
            '    // TODO: Port noise sources from the ngspice *noise.c file.\n'
            '    // Common noise types:\n'
            '    //   Thermal: 4*k*T*G  (conductance noise)\n'
            '    //   Shot:    2*q*|I|  (junction current noise)\n'
            '    //   Flicker: KF*|I|^AF / f^EF  (1/f noise)\n'
            '    // Use sim_temp() for temperature (inherited from Device base class).\n'
            '    // See bjt_device.cpp and bsim4v7_device.cpp for examples.\n'
            '    return {};\n'
        )

    lines = []
    lines.append('    constexpr double kB = 1.3806226e-23;  // Boltzmann constant')
    lines.append('    constexpr double q  = 1.602176634e-19; // electron charge')
    lines.append('    const double T = sim_temp();')
    lines.append(f'    const double m = inst_.{desc.prefix}m;')
    lines.append('    std::vector<NoiseSource> ns;')
    lines.append(f'    ns.reserve({len(noise_sources)});')
    lines.append('')

    for src in noise_sources:
        lines.append(f'    // {src["name"]} ({src["type"]}): {src["psd_expr"]}')
        if src['type'] == 'thermal':
            lines.append(f'    // TODO: map inst fields for thermal noise PSD = 4*kB*T*G')
            lines.append(f'    // ns.push_back({{ucb_to_neo(inst_.{src["node1"]}),')
            lines.append(f'    //               ucb_to_neo(inst_.{src["node2"]}),')
            lines.append(f'    //               m * 4.0 * kB * T * G}});  // replace G with actual conductance')
        elif src['type'] == 'shot':
            lines.append(f'    // TODO: map inst fields for shot noise PSD = 2*q*|I|')
            lines.append(f'    // ns.push_back({{ucb_to_neo(inst_.{src["node1"]}),')
            lines.append(f'    //               ucb_to_neo(inst_.{src["node2"]}),')
            lines.append(f'    //               m * 2.0 * q * std::abs(I)}});  // replace I with actual current')
        elif src['type'] == 'flicker':
            lines.append(f'    // TODO: map inst fields for flicker noise PSD = KF*|I|^AF / f^EF')
            lines.append(f'    // ns.push_back({{ucb_to_neo(inst_.{src["node1"]}),')
            lines.append(f'    //               ucb_to_neo(inst_.{src["node2"]}),')
            lines.append(f'    //               m * psd_1f}});  // replace psd_1f with actual 1/f formula')
        else:
            lines.append(f'    // TODO: implement custom noise for {src["name"]}')
        lines.append('')

    lines.append('    (void)freq; (void)dc_solution; (void)kB; (void)q; (void)T; (void)m;')
    lines.append('    return ns;')
    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# AC stamp body generator (shim-based approach)
# ---------------------------------------------------------------------------

def _gen_ac_stamp_from_extraction(desc, acld_source: str) -> str:
    """Generate ac_stamp() body with scaffolding extracted from the AC load file.

    Extracts real (G matrix) and imaginary (C matrix) stamp entries from the
    ngspice *acld.c file and generates a scaffolded implementation.  The generated
    code identifies all matrix entries with their pointer field names and value
    expressions, ready for manual completion.
    """
    prefix = desc.prefix
    stamps = extract_ac_stamps(acld_source, prefix)
    real_stamps = stamps['real_stamps']
    imag_stamps = stamps['imag_stamps']

    lines = []
    lines.append(f'    auto& here = inst_;')
    lines.append(f'    auto* model = model_;')
    lines.append(f'    const double m = here.{prefix}m;')
    lines.append('')

    if not real_stamps and not imag_stamps:
        lines.append('    // No AC stamps could be extracted from the AC load file.')
        lines.append('    // TODO: implement AC stamp manually.')
        lines.append('    (void)here; (void)model; (void)m;')
        return '\n'.join(lines)

    lines.append('    // TODO: Declare local variables for conductances and capacitances.')
    lines.append('    // These should be read from instance fields populated by the DC load.')
    lines.append('    // Example: const double gm = here.PREFIX_gm;')
    lines.append('')
    lines.append(f'    // --- G matrix (conductance) entries ---')
    lines.append(f'    // Extracted {len(real_stamps)} real-part stamps from ngspice AC load.')

    # Group real stamps by pointer field
    for s in real_stamps:
        ptr = s['ptr_field']
        expr = s['value_expr']
        lines.append(f'    // G: inst_.{ptr} += {expr};')

    lines.append('')
    lines.append(f'    // --- C matrix (capacitance) entries ---')
    lines.append(f'    // Extracted {len(imag_stamps)} imaginary-part stamps from ngspice AC load.')
    lines.append(f'    // NOTE: ngspice stamps *(ptr+1) += value where value = cap * omega.')
    lines.append(f'    // neospice C matrix is multiplied by omega by the AC solver,')
    lines.append(f'    // so stamp the capacitance value directly (without omega).')

    for s in imag_stamps:
        ptr = s['ptr_field']
        expr = s['value_expr']
        lines.append(f'    // C: inst_.{ptr} += {expr};  // divide by omega for C matrix')

    lines.append('')
    lines.append('    // TODO: Implement the stamp logic by reading instance fields,')
    lines.append('    // computing y-parameters, and stamping into G and C matrices.')
    lines.append('    // See hisim2_device.cpp or bsim4v7_device.cpp for reference.')
    lines.append('    (void)here; (void)model; (void)m;')
    return '\n'.join(lines)


def _gen_ac_stamp_stub(prefix: str) -> str:
    """Generate a TODO stub for ac_stamp()."""
    return (
        f'    // TODO: Port the AC stamp from the ngspice *acld.c file.\n'
        f'    // Conductances (gm, gds, ...) go into G; capacitances (Cgs, Cgd, ...) into C.\n'
        f'    // See existing implementations: bsim4v7_device.cpp, hisim2_device.cpp.\n'
    )


# ---------------------------------------------------------------------------
# query_param body generator
# ---------------------------------------------------------------------------

def _gen_query_param(desc, output_params):
    """Generate query_param() body from extracted parameter table."""
    if not output_params:
        return '    // TODO: no output parameters found — implement manually\n    return std::nullopt;\n'

    lines = []
    lines.append('    const std::string key = str_tolower(name);')
    lines.append(f'    const double m = inst_.{desc.prefix}m;')
    lines.append('')
    lines.append('    if (state0_ && state_base_ >= 0) {')
    for p in output_params:
        if p['is_output']:
            lines.append(f'        if (key == "{p["name"]}") return 0.0; // TODO: map {p["id"]} to state/inst field, apply m scaling')
    lines.append('    }')
    lines.append('')
    lines.append('    // Geometry (not scaled by m)')
    for p in output_params:
        if not p['is_output']:
            lines.append(f'    if (key == "{p["name"]}") return inst_.{desc.prefix}{p["name"]};')
    lines.append('')
    lines.append('    return std::nullopt;')
    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# compute_trunc body generator
# ---------------------------------------------------------------------------

def _gen_compute_trunc(desc, charge_offsets, charge_states_override):
    """Generate compute_trunc() body with LTE calculation."""
    if charge_states_override:
        offset_exprs = [f"state_base_ + {off}" for off in charge_states_override]
    elif charge_offsets:
        offset_exprs = [f"inst_.{name}" for name in charge_offsets]
    else:
        return '    // TODO: no charge state offsets found — implement manually\n    return 1e30;\n'

    lines = []
    lines.append('    if (ctx.order < 2 || ctx.delta <= 0.0) return 1e30;')
    lines.append('    if (!state0_ || !state1_ || !state2_) return 1e30;')
    lines.append('')
    lines.append('    const double h0 = ctx.delta;')
    lines.append('    const double h1 = ctx.delta_old[1];')
    lines.append('    if (h1 <= 0.0) return 1e30;')
    lines.append('')
    lines.append('    double dt_min = 1e30;')
    lines.append('    const double lte_coeff = ctx.lte_coefficient();')
    lines.append('')

    for expr in offset_exprs:
        lines.append(f'    {{ // charge offset: {expr}')
        lines.append(f'        const int qcap = {expr};')
        lines.append( '        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];')
        lines.append( '        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);')
        lines.append( '        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));')
        lines.append( '        if (tol > 0.0 && std::abs(dd2) > 1e-30) {')
        lines.append( '            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));')
        lines.append( '        }')
        lines.append( '    }')

    lines.append('    return dt_min;')
    return '\n'.join(lines)


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

def generate_adapter_cpp(desc: _Desc, setup_source: str = "", def_content: str = "",
                         load_source: str = "", devsup_source: str = "",
                         acld_source: str = "", noi_source: str = "") -> str:
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
    acld_source:
        Raw AC load source text (from ngspice *acld.c).  When provided,
        noise and AC stamp info may be extracted.
    noi_source:
        Raw noise source text (from ngspice *noi.c).  When provided,
        noise source scaffolding is generated instead of a TODO stub.
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
    parts.append('#include "devices/ucb_device_init.hpp"\n')
    parts.append('#include "devices/ucb_utils.hpp"\n')
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
    # Derive the UCB macro prefix from the instances_field.  For example,
    # instances_field="BJTinstances"  →  ucb_prefix="BJT".
    ucb_prefix = desc.instances_field.removesuffix("instances") if desc.instances_field else ""

    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// declare_internal_nodes\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"void {prefix}Device::declare_internal_nodes(Circuit& ckt) {{\n")
    parts.append("    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(\n")
    parts.append("        ckt, name_,\n")
    parts.append("        [this](Shim::Matrix& m, Shim::Ckt& c) {\n")
    if ucb_prefix:
        parts.append(f"            UCB_SPLICE_INSTANCE({ucb_prefix});\n")
    parts.append("            int states = 0;\n")
    parts.append(f"            int rc = {desc.setup_function}(&m, model_, &c, &states);\n")
    if ucb_prefix:
        parts.append(f"            UCB_UNSPLICE_INSTANCE({ucb_prefix});\n")
    parts.append("            return rc;\n")
    parts.append("        },\n")
    parts.append(f'        "{desc.setup_function}", journal_, max_neo_node_);\n')
    parts.append("}\n")
    parts.append("\n")

    # --- stamp_pattern -----------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// stamp_pattern\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"void {prefix}Device::stamp_pattern(SparsityBuilder& builder) const {{\n")
    parts.append("    ucb_stamp_pattern(journal_, builder);\n")
    parts.append("}\n")
    parts.append("\n")

    # --- assign_offsets ----------------------------------------------------
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// assign_offsets\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"void {prefix}Device::assign_offsets(const SparsityPattern& pattern) {{\n")
    parts.append("    const auto offsets = ucb_compute_offsets(journal_, pattern);\n")
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

    # --- ac_stamp -----------------------------------------------------------
    has_ac = bool(acld_source)

    if has_ac:
        ac_stamps = extract_ac_stamps(acld_source, desc.prefix)
        n_real = len(ac_stamps['real_stamps'])
        n_imag = len(ac_stamps['imag_stamps'])
        parts.append("// ---------------------------------------------------------------------------\n")
        parts.append(f"// ac_stamp — {n_real} G entries + {n_imag} C entries extracted from ngspice AC load\n")
        parts.append("// ---------------------------------------------------------------------------\n")
        parts.append(f"void {prefix}Device::ac_stamp(const std::vector<double>& /*voltages*/,\n")
        parts.append(f"        {' ' * len(prefix)}                NumericMatrix& G,\n")
        parts.append(f"        {' ' * len(prefix)}                NumericMatrix& C) {{\n")
        parts.append(_gen_ac_stamp_from_extraction(desc, acld_source) + "\n")
        parts.append("}\n")
        parts.append("\n")
    else:
        parts.append("// ---------------------------------------------------------------------------\n")
        parts.append("// ac_stamp — TODO: implement G/C matrix split from ngspice AC load file\n")
        parts.append("// ---------------------------------------------------------------------------\n")
        parts.append(f"void {prefix}Device::ac_stamp(const std::vector<double>& /*voltages*/,\n")
        parts.append(f"        {' ' * len(prefix)}                NumericMatrix& /*G*/,\n")
        parts.append(f"        {' ' * len(prefix)}                NumericMatrix& /*C*/) {{\n")
        parts.append(_gen_ac_stamp_stub(prefix) + "\n")
        parts.append("}\n")
        parts.append("\n")

    # --- compute_trunc -----------------------------------------------------
    charge_offsets = extract_charge_offsets(load_source, desc.prefix) if load_source else []
    charge_states = desc.charge_states if hasattr(desc, 'charge_states') else []
    trunc_body = _gen_compute_trunc(desc, charge_offsets, charge_states)

    has_real_trunc = "TODO" not in trunc_body
    ctx_param = "ctx" if has_real_trunc else "/*ctx*/"
    opts_param = "opts" if has_real_trunc else "/*opts*/"

    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// compute_trunc\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"double {prefix}Device::compute_trunc(const IntegratorCtx& {ctx_param},\n")
    parts.append(f"        {' ' * len(prefix)}                  const SimOptions& {opts_param}) const {{\n")
    parts.append(trunc_body + "\n")
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

    # --- query_param -------------------------------------------------------
    output_params = extract_output_params(devsup_source, desc.prefix) if devsup_source else []
    query_body = _gen_query_param(desc, output_params)

    has_real_query = "TODO: no output" not in query_body
    name_param = "name" if has_real_query else "/*name*/"

    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append("// query_param\n")
    parts.append("// ---------------------------------------------------------------------------\n")
    parts.append(f"std::optional<double> {prefix}Device::query_param(const std::string& {name_param}) const {{\n")
    parts.append(query_body + "\n")
    parts.append("}\n")
    parts.append("\n")

    # --- noise_sources ------------------------------------------------------
    noise_srcs = extract_noise_sources(noi_source, desc.prefix) if noi_source else []
    noise_body = _gen_noise_sources(desc, noise_srcs)
    has_real_noise = 'TODO: Port noise' not in noise_body

    freq_param = "freq" if has_real_noise else "/*freq*/"
    dc_sol_param = "dc_solution" if has_real_noise else "/*dc_solution*/"

    if noise_srcs:
        parts.append("// ---------------------------------------------------------------------------\n")
        parts.append(f"// noise_sources — {len(noise_srcs)} sources extracted from ngspice noise file\n")
        parts.append("// ---------------------------------------------------------------------------\n")
    else:
        parts.append("// ---------------------------------------------------------------------------\n")
        parts.append("// noise_sources — TODO: implement device noise model\n")
        parts.append("// ---------------------------------------------------------------------------\n")

    parts.append(f"std::vector<Device::NoiseSource> {prefix}Device::noise_sources(\n")
    parts.append(f"        double {freq_param}, const std::vector<double>& {dc_sol_param}) const {{\n")
    parts.append(noise_body + "\n")
    parts.append("}\n")
    parts.append("\n")

    # --- Close namespace ---------------------------------------------------
    parts.append("} // namespace neospice\n")

    return "".join(parts)
