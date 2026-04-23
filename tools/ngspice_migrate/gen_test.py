"""Generate per-device test scaffolding matching the real test framework.

Produces:
  - CMakeLists.txt following the pattern from tests/devices/hisim2/
  - test_<ns>_compare.cpp with DC OP and AC comparison tests
  - SPICE circuit files (.cir) for each test
"""
from __future__ import annotations

from typing import List, Optional


# ---------------------------------------------------------------------------
# Device category helpers
# ---------------------------------------------------------------------------

def _infer_device_category(desc) -> str:
    """Infer device category from model_types and terminal count."""
    if not desc.model_types:
        return "generic"
    first = desc.model_types[0].spice_name.lower()
    if first in ("nmos", "pmos"):
        return "mosfet"
    elif first in ("npn", "pnp"):
        return "bjt"
    elif first in ("njf", "pjf"):
        return "jfet"
    elif first in ("nhfet", "phfet"):
        return "hfet"
    elif first == "d":
        return "diode"
    return "generic"


def _infer_spice_prefix(desc) -> str:
    """Infer SPICE element prefix from descriptor or model category."""
    if getattr(desc, 'spice_prefix', ''):
        return desc.spice_prefix
    cat = _infer_device_category(desc)
    return {
        "mosfet": "M", "bjt": "Q", "jfet": "J",
        "hfet": "Z", "diode": "D",
    }.get(cat, "X")


def _infer_level(desc) -> int:
    """Infer a SPICE LEVEL value from descriptor."""
    levels = getattr(desc, 'levels', [])
    if levels:
        return levels[0]
    # Fallback: return 1 as default
    return 1


def _has_polarity_pair(desc) -> bool:
    """Check if the device has N/P type pairs (NMOS/PMOS, NPN/PNP, etc.)."""
    if len(desc.model_types) >= 2:
        names = [mt.spice_name.lower() for mt in desc.model_types]
        pairs = [("nmos", "pmos"), ("npn", "pnp"), ("njf", "pjf"), ("nhfet", "phfet")]
        for a, b in pairs:
            if a in names and b in names:
                return True
    return False


# ---------------------------------------------------------------------------
# Circuit templates by device category
# ---------------------------------------------------------------------------

def _mosfet_nmos_dc_circuit(desc, level: int) -> str:
    """Generate NMOS DC OP test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    n_terms = len(desc.terminals)
    # For 5-terminal devices (like HiSIMHV), add extra ground
    body = "0 0" if n_terms == 4 else " ".join(["0"] * (n_terms - 2))
    return f"""{name_upper} NMOS DC Operating Point
Vdd vdd 0 1.8
Vgs gate 0 0.9
Rd vdd drain 1k
M1 drain gate {body} NMOD W=10u L=1u
.model NMOD NMOS LEVEL={level}
.op
.end
"""


def _mosfet_pmos_dc_circuit(desc, level: int) -> str:
    """Generate PMOS DC OP test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    n_terms = len(desc.terminals)
    # PMOS: source and bulk at Vdd
    if n_terms == 4:
        body = "vdd vdd"
    else:
        body = " ".join(["vdd"] * (n_terms - 2))
    return f"""{name_upper} PMOS DC Operating Point
Vdd vdd 0 1.8
Vg gate 0 1.0
Rload drain 0 1k
M1 drain gate {body} PMOD W=10u L=1u
.model PMOD PMOS LEVEL={level}
.op
.end
"""


def _mosfet_ac_circuit(desc, level: int) -> str:
    """Generate NMOS AC test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    n_terms = len(desc.terminals)
    body = "0 0" if n_terms == 4 else " ".join(["0"] * (n_terms - 2))
    return f"""{name_upper} NMOS AC Small-Signal
Vdd vdd 0 1.8
Vin gate 0 0.9 AC 1
Rd vdd drain 1k
M1 drain gate {body} NMOD W=10u L=1u
.model NMOD NMOS LEVEL={level}
.ac dec 10 100 10meg
.end
"""


def _bjt_npn_dc_circuit(desc, level: int) -> str:
    """Generate NPN BJT DC OP test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} NPN DC Operating Point
Vcc vcc 0 5
Vbb base_src 0 0.8
Rc vcc col 1k
Rb base_src base 10k
Q1 col base 0 0 QMOD
.model QMOD NPN
.op
.end
"""


def _bjt_pnp_dc_circuit(desc, level: int) -> str:
    """Generate PNP BJT DC OP test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} PNP DC Operating Point
Vcc vcc 0 5
Vbb base_src 0 4.2
Rload col 0 1k
Rb base_src base 10k
Q1 col base vcc vcc QMOD
.model QMOD PNP
.op
.end
"""


def _bjt_ac_circuit(desc, level: int) -> str:
    """Generate NPN BJT AC test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} NPN AC Small-Signal
Vcc vcc 0 5
Vin base_src 0 0.8 AC 1
Rc vcc col 1k
Rb base_src base 10k
Q1 col base 0 0 QMOD
.model QMOD NPN
.ac dec 10 100 10meg
.end
"""


def _jfet_n_dc_circuit(desc, level: int) -> str:
    """Generate N-channel JFET DC OP test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} N-JFET DC Operating Point
Vdd vdd 0 10
Vgs gate 0 0
Rd vdd drain 2k
J1 drain gate 0 JMOD
.model JMOD NJF
.op
.end
"""


def _jfet_p_dc_circuit(desc, level: int) -> str:
    """Generate P-channel JFET DC OP test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} P-JFET DC Operating Point
Vdd vdd 0 10
Vgs gate 0 0
Rload drain 0 2k
J1 drain gate vdd JMOD
.model JMOD PJF
.op
.end
"""


def _jfet_ac_circuit(desc, level: int) -> str:
    """Generate N-channel JFET AC test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} N-JFET AC Small-Signal
Vdd vdd 0 10
Vin gate 0 0 AC 1
Rd vdd drain 2k
J1 drain gate 0 JMOD
.model JMOD NJF
.ac dec 10 100 10meg
.end
"""


def _diode_dc_circuit(desc, level: int) -> str:
    """Generate diode DC OP test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} Diode DC Operating Point
Vs supply 0 5
Rs supply anode 1k
D1 anode 0 DMOD
.model DMOD D
.op
.end
"""


def _diode_ac_circuit(desc, level: int) -> str:
    """Generate diode AC test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} Diode AC Small-Signal
Vs supply 0 0.7 AC 1
Rs supply anode 100
D1 anode 0 DMOD
.model DMOD D
.ac dec 10 100 10meg
.end
"""


def _hfet_n_dc_circuit(desc, level: int) -> str:
    """Generate N-HFET DC OP test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} N-HFET DC Operating Point
Vdd vdd 0 3.3
Vgs gate 0 0.5
Rd vdd drain 1k
Z1 drain gate 0 ZMOD
.model ZMOD NHFET
.op
.end
"""


def _hfet_ac_circuit(desc, level: int) -> str:
    """Generate N-HFET AC test circuit."""
    ns = desc.neospice_name
    name_upper = ns.upper().replace("_", " ")
    return f"""{name_upper} N-HFET AC Small-Signal
Vdd vdd 0 3.3
Vin gate 0 0.5 AC 1
Rd vdd drain 1k
Z1 drain gate 0 ZMOD
.model ZMOD NHFET
.ac dec 10 100 10meg
.end
"""


# ---------------------------------------------------------------------------
# Generate test circuits
# ---------------------------------------------------------------------------

def _generate_circuits(desc) -> dict[str, str]:
    """Return a dict of {filename: circuit_content} for the device."""
    ns = desc.neospice_name
    cat = _infer_device_category(desc)
    level = _infer_level(desc)
    circuits: dict[str, str] = {}

    if cat == "mosfet":
        circuits[f"{ns}_nmos_dc_op.cir"] = _mosfet_nmos_dc_circuit(desc, level)
        if _has_polarity_pair(desc):
            circuits[f"{ns}_pmos_dc_op.cir"] = _mosfet_pmos_dc_circuit(desc, level)
        circuits[f"{ns}_nmos_ac.cir"] = _mosfet_ac_circuit(desc, level)
    elif cat == "bjt":
        circuits[f"{ns}_npn_dc_op.cir"] = _bjt_npn_dc_circuit(desc, level)
        if _has_polarity_pair(desc):
            circuits[f"{ns}_pnp_dc_op.cir"] = _bjt_pnp_dc_circuit(desc, level)
        circuits[f"{ns}_npn_ac.cir"] = _bjt_ac_circuit(desc, level)
    elif cat == "jfet":
        circuits[f"{ns}_njf_dc_op.cir"] = _jfet_n_dc_circuit(desc, level)
        if _has_polarity_pair(desc):
            circuits[f"{ns}_pjf_dc_op.cir"] = _jfet_p_dc_circuit(desc, level)
        circuits[f"{ns}_njf_ac.cir"] = _jfet_ac_circuit(desc, level)
    elif cat == "diode":
        circuits[f"{ns}_dc_op.cir"] = _diode_dc_circuit(desc, level)
        circuits[f"{ns}_ac.cir"] = _diode_ac_circuit(desc, level)
    elif cat == "hfet":
        circuits[f"{ns}_nhfet_dc_op.cir"] = _hfet_n_dc_circuit(desc, level)
        circuits[f"{ns}_nhfet_ac.cir"] = _hfet_ac_circuit(desc, level)
    else:
        # Generic fallback — emit a placeholder
        circuits[f"{ns}_dc_op.cir"] = f"""{ns.upper()} DC Operating Point
* TODO: add device-specific test circuit
.op
.end
"""

    return circuits


# ---------------------------------------------------------------------------
# Test comparison source (.cpp) generation
# ---------------------------------------------------------------------------

def _gen_test_class_name(desc) -> str:
    """Generate a PascalCase test class name from the descriptor."""
    ns = desc.neospice_name
    # Convert snake_case / lowercase to PascalCase
    parts = ns.replace("-", "_").split("_")
    return "".join(p.capitalize() for p in parts) + "Validation"


def generate_test_compare(desc) -> str:
    """Generate the main comparison test source file."""
    ns = desc.neospice_name
    cat = _infer_device_category(desc)
    cls = _gen_test_class_name(desc)
    name_upper = ns.upper().replace("_", " ")
    circuits = _generate_circuits(desc)

    lines = []
    lines.append(f'// {name_upper} ngspice comparison suite.')
    lines.append(f'// Tests: DC operating point, AC small-signal.')

    # Includes
    lines.append('')
    lines.append('#include <gtest/gtest.h>')
    lines.append('#include "api/neospice.hpp"')
    lines.append('#include "framework/ngspice_runner.hpp"')
    lines.append('#include "framework/comparator.hpp"')
    lines.append('')
    lines.append('#include <cmath>')
    lines.append('#include <complex>')
    lines.append('#include <string>')
    lines.append('#include <algorithm>')
    lines.append('')
    lines.append('using namespace neospice;')
    lines.append('')

    # Test fixture
    lines.append('// ============================================================================')
    lines.append(f'// Test fixture')
    lines.append('// ============================================================================')
    lines.append('')
    lines.append(f'class {cls} : public ::testing::Test {{')
    lines.append('protected:')
    lines.append('    void SetUp() override {')
    lines.append('        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);')
    lines.append('    }')
    lines.append('    std::unique_ptr<NgspiceRunner> ngspice_;')
    lines.append('    Simulator sim_;')
    lines.append('};')
    lines.append('')

    # Generate DC OP tests
    dc_circuits = [(name, cir) for name, cir in circuits.items() if '_dc_op' in name]
    for cir_name, cir_content in dc_circuits:
        test_label = cir_name.replace(f"{ns}_", "").replace("_dc_op.cir", "").title()
        test_label = test_label.replace("_", "")
        test_name = f'{test_label}OperatingPoint'

        lines.append('// ============================================================================')
        lines.append(f'// DC Operating Point — {test_label}')
        lines.append('// ============================================================================')
        lines.append('')
        lines.append(f'TEST_F({cls}, {test_name}) {{')
        lines.append(f'    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/{cir_name}";')
        lines.append('')
        lines.append('    // Run ngspice')
        lines.append('    DCResult ng_result;')
        lines.append('    try {')
        lines.append('        ng_result = ngspice_->run_dc(cir_path);')
        lines.append('    } catch (const std::exception& e) {')
        lines.append('        GTEST_SKIP() << "ngspice not available or failed: " << e.what();')
        lines.append('    }')
        lines.append('')
        lines.append('    if (ng_result.node_voltages.empty()) {')
        lines.append(f'        GTEST_SKIP() << "ngspice returned empty DC result ({name_upper} may not be compiled in)";')
        lines.append('    }')
        lines.append('')
        lines.append('    // Run neospice')
        lines.append('    auto ckt = sim_.load(cir_path);')
        lines.append('    DCResult cs_result = sim_.run_dc(ckt);')
        lines.append('')
        lines.append('    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});')
        lines.append('    EXPECT_TRUE(cmp.passed)')
        lines.append('        << "DC OP comparison failed. Worst: " << cmp.worst_signal')
        lines.append('        << " error: " << cmp.worst_error;')

        # Add basic physics checks based on device type
        if cat == "mosfet":
            if "nmos" in cir_name:
                lines.append('')
                lines.append('    // Verify basic MOSFET physics')
                lines.append('    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);')
                lines.append('    double v_drain = cs_result.node_voltages["v(drain)"];')
                lines.append('    EXPECT_GT(v_drain, 0.1) << "V(drain) should be above ground";')
                lines.append('    EXPECT_LT(v_drain, 1.8) << "V(drain) should be below Vdd";')
            elif "pmos" in cir_name:
                lines.append('')
                lines.append('    // Verify PMOS physics')
                lines.append('    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);')
                lines.append('    double v_drain = cs_result.node_voltages["v(drain)"];')
                lines.append('    EXPECT_GT(v_drain, 0.01) << "PMOS drain should be pulled up";')
                lines.append('    EXPECT_LT(v_drain, 1.8) << "PMOS drain should be below Vdd";')

        elif cat == "bjt":
            if "npn" in cir_name:
                lines.append('')
                lines.append('    // Verify basic BJT physics')
                lines.append('    ASSERT_TRUE(cs_result.node_voltages.count("v(col)") > 0);')
                lines.append('    double v_col = cs_result.node_voltages["v(col)"];')
                lines.append('    EXPECT_GT(v_col, 0.1) << "V(col) should be above ground";')
                lines.append('    EXPECT_LT(v_col, 5.0) << "V(col) should be below Vcc";')

        lines.append('}')
        lines.append('')

    # Generate AC test
    ac_circuits = [(name, cir) for name, cir in circuits.items() if '_ac' in name]
    for cir_name, cir_content in ac_circuits:
        test_label = cir_name.replace(f"{ns}_", "").replace("_ac.cir", "").replace("ac.cir", "").title()
        test_label = test_label.replace("_", "")
        test_name = f'{test_label}AcResponse' if test_label else 'AcResponse'

        lines.append('// ============================================================================')
        lines.append(f'// AC Small-Signal — {test_label or name_upper}')
        lines.append('// ============================================================================')
        lines.append('')
        lines.append(f'TEST_F({cls}, {test_name}) {{')
        lines.append(f'    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/{cir_name}";')
        lines.append('')
        lines.append('    // Run ngspice')
        lines.append('    ACResult ng_result;')
        lines.append('    try {')
        lines.append('        ng_result = ngspice_->run_ac(cir_path);')
        lines.append('    } catch (const std::exception& e) {')
        lines.append('        GTEST_SKIP() << "ngspice not available or failed: " << e.what();')
        lines.append('    }')
        lines.append('')
        lines.append('    if (ng_result.frequency.empty()) {')
        lines.append('        GTEST_SKIP() << "ngspice returned empty AC result";')
        lines.append('    }')
        lines.append('')
        lines.append('    // Run neospice')
        lines.append('    auto ckt = sim_.load(cir_path);')
        lines.append('    ACResult cs_result = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, 100.0, 10e6);')
        lines.append('')
        lines.append('    ASSERT_FALSE(cs_result.frequency.empty());')
        lines.append('    ASSERT_EQ(ng_result.frequency.size(), cs_result.frequency.size())')
        lines.append('        << "Frequency point count mismatch";')
        lines.append('')
        lines.append('    // Filter out internal nodes from ngspice result')
        lines.append('    for (auto it = ng_result.voltages.begin(); it != ng_result.voltages.end(); ) {')
        lines.append("        if (it->first.find('#') != std::string::npos)")
        lines.append('            it = ng_result.voltages.erase(it);')
        lines.append('        else')
        lines.append('            ++it;')
        lines.append('    }')
        lines.append('')
        lines.append('    auto cmp = compare_ac(ng_result, cs_result, {5e-2, 1e-9});')
        lines.append('    EXPECT_TRUE(cmp.passed)')
        lines.append('        << "AC comparison failed. Worst: " << cmp.worst_signal')
        lines.append('        << " error: " << cmp.worst_error;')
        lines.append('}')
        lines.append('')

    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# CMakeLists.txt
# ---------------------------------------------------------------------------

def generate_test_cmake(desc) -> str:
    """Generate the test CMakeLists.txt matching the real framework pattern."""
    ns = desc.neospice_name
    return f"""add_executable(test_{ns}_compare
    test_{ns}_compare.cpp
    ${{CMAKE_SOURCE_DIR}}/tests/framework/ngspice_runner.cpp
    ${{CMAKE_SOURCE_DIR}}/tests/framework/comparator.cpp
)

target_link_libraries(test_{ns}_compare PRIVATE gtest_main neospice_lib)
target_include_directories(test_{ns}_compare PRIVATE ${{CMAKE_SOURCE_DIR}}/tests)
target_compile_definitions(test_{ns}_compare PRIVATE
    NGSPICE_BINARY="/usr/bin/ngspice"
    TEST_CIRCUITS_DIR="${{CMAKE_SOURCE_DIR}}/tests/circuits"
)

gtest_discover_tests(test_{ns}_compare)
"""


# ---------------------------------------------------------------------------
# Circuit file generation
# ---------------------------------------------------------------------------

def generate_circuits(desc) -> dict[str, str]:
    """Return dict of {filename: content} for all test circuits."""
    return _generate_circuits(desc)


# ---------------------------------------------------------------------------
# Legacy API compatibility (kept for backward compat)
# ---------------------------------------------------------------------------

def generate_test_dc(desc) -> str:
    """Legacy: generate DC test stub. Use generate_test_compare() instead."""
    return generate_test_compare(desc)


def generate_test_transient(desc) -> str:
    """Legacy: generate transient test stub. No longer emits separate file."""
    return f"// Transient tests are included in test_{desc.neospice_name}_compare.cpp\n"
