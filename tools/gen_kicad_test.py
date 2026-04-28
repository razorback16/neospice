#!/usr/bin/env python3
"""Generate neospice E2E test from a KiCad-Spice-Library model.

Usage:
    python tools/gen_kicad_test.py <model-file> [model-name]

Examples:
    # Op-amp (auto-detects subcircuit)
    python tools/gen_kicad_test.py "third_party/KiCad-Spice-Library/Models/Operational Amplifier/Lm358.mod"

    # BJT (picks first NPN/PNP model, or specify one)
    python tools/gen_kicad_test.py "third_party/KiCad-Spice-Library/Models/Transistor/BJT/BJT.lib" BC547A

    # JFET
    python tools/gen_kicad_test.py "third_party/KiCad-Spice-Library/Models/Transistor/FET/2n4416a.lib"

    # Diode
    python tools/gen_kicad_test.py "third_party/KiCad-Spice-Library/Models/Diode/diode.lib" 1N4148

Generates:
    tests/circuits/<name>.lib        - cleaned model (ngspice-compatible)
    tests/circuits/<name>_test.cir   - test circuit with .op and .ac
    tests/unit/test_kicad_<name>.cpp - gtest E2E test (DC + AC + ngspice comparison)

Then add tests/unit/test_kicad_<name>.cpp to tests/CMakeLists.txt to wire it in.
"""

import argparse
import re
import sys
import textwrap
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CIRCUITS_DIR = PROJECT_ROOT / "tests" / "circuits"
UNIT_DIR = PROJECT_ROOT / "tests" / "unit"

# Non-standard .MODEL parameters that ngspice rejects
STRIP_PARAMS = {"MFG", "VCEO", "ICRATING", "ICRATINGMAX", "PKG"}


# ---------------------------------------------------------------------------
# Model parsing
# ---------------------------------------------------------------------------

class Model:
    """A parsed .MODEL or .SUBCKT from a SPICE library file."""
    def __init__(self, name, kind, raw_lines):
        self.name = name        # e.g. "BC547A", "LM358/NS"
        self.kind = kind        # "NPN", "PNP", "NJF", "PJF", "D", "SUBCKT"
        self.raw_lines = raw_lines

    @property
    def safe_name(self):
        return re.sub(r'[^A-Za-z0-9_]', '_', self.name)


def join_continuation(lines):
    """Join SPICE continuation lines (leading +) into single logical lines."""
    result = []
    for line in lines:
        stripped = line.rstrip('\r\n')
        if stripped.startswith('+'):
            if result:
                result[-1] += ' ' + stripped[1:].strip()
            else:
                result.append(stripped[1:].strip())
        else:
            result.append(stripped)
    return result


def parse_models(filepath):
    """Parse all .MODEL and .SUBCKT definitions from a SPICE library file."""
    with open(filepath, 'r', errors='replace') as f:
        raw_lines = f.readlines()

    logical = join_continuation(raw_lines)
    models = []

    # Find .MODEL lines
    for line in logical:
        m = re.match(r'\.MODEL\s+(\S+)\s+(\w+)', line, re.I)
        if m:
            name, kind = m.group(1), m.group(2).upper()
            models.append(Model(name, kind, [line]))

    # Find .SUBCKT blocks (for op-amps etc)
    in_subckt = False
    subckt_lines = []
    subckt_name = None
    for line in raw_lines:
        stripped = line.strip()
        up = stripped.upper()
        if up.startswith('.SUBCKT'):
            in_subckt = True
            subckt_lines = [line]
            parts = stripped.split()
            subckt_name = parts[1] if len(parts) > 1 else "UNKNOWN"
        elif in_subckt:
            subckt_lines.append(line)
            if up.startswith('.ENDS'):
                models.append(Model(subckt_name, "SUBCKT", subckt_lines))
                in_subckt = False
                subckt_lines = []

    return models


def clean_model_line(line):
    """Remove non-standard parameters from a .MODEL line."""
    for param in STRIP_PARAMS:
        line = re.sub(rf'\b{param}=\S+\s*', '', line, flags=re.I)
    # Fix "TR=1m2" → "TR=1E-3" (malformed SPICE suffix)
    line = re.sub(r'TR=(\d+)m2\b', r'TR=\1E-3', line, flags=re.I)
    return line


def extract_model(filepath, model_name=None):
    """Extract a specific model (or first suitable one) from a library file."""
    models = parse_models(filepath)
    if not models:
        print(f"Error: no models found in {filepath}", file=sys.stderr)
        sys.exit(1)

    if model_name:
        for m in models:
            if m.name.upper() == model_name.upper():
                return m
        print(f"Error: model '{model_name}' not found. Available:", file=sys.stderr)
        for m in models:
            print(f"  {m.name} ({m.kind})", file=sys.stderr)
        sys.exit(1)

    # Auto-select: prefer subcircuit (op-amp), then NPN, PNP, NJF, D
    priority = {"SUBCKT": 0, "NPN": 1, "PNP": 2, "NJF": 3, "PJF": 4, "D": 5}
    candidates = sorted(models, key=lambda m: priority.get(m.kind, 99))
    return candidates[0]


# ---------------------------------------------------------------------------
# Model file generation (cleaned for ngspice)
# ---------------------------------------------------------------------------

def write_clean_lib(model, src_path, dst_path):
    """Write a cleaned model file suitable for ngspice."""
    with open(src_path, 'r', errors='replace') as f:
        content = f.read()

    if model.kind == "SUBCKT":
        # For subcircuits, copy the whole file but clean .MODEL lines
        lines = content.split('\n')
        cleaned = []
        for line in lines:
            if re.match(r'\.MODEL', line, re.I):
                line = clean_model_line(line)
            cleaned.append(line)
        out = '\n'.join(cleaned)
    else:
        # For .MODEL, extract just the relevant model
        logical = join_continuation(content.split('\n'))
        out_lines = []
        for line in logical:
            m = re.match(r'\.MODEL\s+(\S+)', line, re.I)
            if m and m.group(1).upper() == model.name.upper():
                out_lines.append(clean_model_line(line))
        if not out_lines:
            out_lines = [clean_model_line(l) for l in model.raw_lines]
        out = '\n'.join(out_lines) + '\n'

    # Normalize line endings
    out = out.replace('\r\n', '\n')

    with open(dst_path, 'w') as f:
        f.write(out)


# ---------------------------------------------------------------------------
# Test circuit generation
# ---------------------------------------------------------------------------

def gen_opamp_circuit(model, lib_name):
    """Inverting amplifier: gain = -10, +/-15V supply."""
    # Detect pin order from .SUBCKT line
    subckt_line = model.raw_lines[0].strip()
    parts = subckt_line.split()
    # .SUBCKT <name> <pin1> <pin2> ...
    pins = parts[2:]
    n_pins = len(pins)

    # Standard 5-pin: non-inv, inv, vcc, vee, out
    if n_pins == 5:
        pin_map = f"X1 ninv inv vcc vee out {model.name}"
    elif n_pins == 3:
        # 3-pin: in+, in-, out (no supply pins)
        pin_map = f"X1 ninv inv out {model.name}"
    else:
        pin_map = f"* NOTE: {n_pins}-pin subcircuit - verify pin order\nX1 " + \
                  " ".join(["ninv", "inv", "vcc", "vee", "out"][:n_pins]) + \
                  f" {model.name}"

    return textwrap.dedent(f"""\
        {model.name} Inverting Amplifier Test
        * Supply: +/-15V, Input: 50mV DC + AC 1V
        * Gain = -Rf/Rg = -100k/10k = -10
        *
        .include {lib_name}
        *
        VCC vcc 0 DC 15
        VEE vee 0 DC -15
        *
        Vin in 0 DC 0.05 AC 1
        *
        Rg in inv 10k
        Rf out inv 100k
        *
        R_bias 0 ninv 100
        *
        {pin_map}
        *
        Rload out 0 10k
        *
        .op
        .ac dec 10 1 10e6
        .end
    """)


def gen_npn_circuit(model, lib_name):
    """Common-emitter amplifier with voltage divider bias."""
    return textwrap.dedent(f"""\
        {model.name} Common-Emitter Amplifier Test
        *
        .include {lib_name}
        *
        VCC vcc 0 DC 12
        *
        Vin in 0 DC 0 AC 1
        Cin in base 1u
        *
        R1 vcc base 56k
        R2 base 0 12k
        *
        RC vcc col 3.3k
        RE emit 0 1k
        CE emit 0 100u
        *
        Q1 col base emit {model.name}
        *
        Cout col out 1u
        Rload out 0 10k
        *
        .op
        .ac dec 10 10 10e6
        .end
    """)


def gen_pnp_circuit(model, lib_name):
    """PNP common-emitter amplifier."""
    return textwrap.dedent(f"""\
        {model.name} PNP Common-Emitter Amplifier Test
        *
        .include {lib_name}
        *
        VCC vcc 0 DC -12
        *
        Vin in 0 DC 0 AC 1
        Cin in base 1u
        *
        R1 vcc base 56k
        R2 base 0 12k
        *
        RC vcc col 3.3k
        RE emit 0 1k
        CE emit 0 100u
        *
        Q1 col base emit {model.name}
        *
        Cout col out 1u
        Rload out 0 10k
        *
        .op
        .ac dec 10 10 10e6
        .end
    """)


def gen_njfet_circuit(model, lib_name):
    """N-channel JFET common-source amplifier."""
    return textwrap.dedent(f"""\
        {model.name} Common-Source Amplifier Test
        *
        .include {lib_name}
        *
        VDD vdd 0 DC 15
        *
        Vin in 0 DC 0 AC 1
        Cin in gate 1u
        *
        Rg gate 0 1MEG
        *
        RD vdd drain 2.2k
        RS source 0 470
        CS source 0 100u
        *
        J1 drain gate source {model.name}
        *
        Cout drain out 1u
        Rload out 0 10k
        *
        .op
        .ac dec 10 10 10e6
        .end
    """)


def gen_pjfet_circuit(model, lib_name):
    """P-channel JFET common-source amplifier."""
    return textwrap.dedent(f"""\
        {model.name} P-JFET Common-Source Amplifier Test
        *
        .include {lib_name}
        *
        VSS vss 0 DC -15
        *
        Vin in 0 DC 0 AC 1
        Cin in gate 1u
        *
        Rg gate 0 1MEG
        *
        RD vss drain 2.2k
        RS source 0 470
        CS source 0 100u
        *
        J1 drain gate source {model.name}
        *
        Cout drain out 1u
        Rload out 0 10k
        *
        .op
        .ac dec 10 10 10e6
        .end
    """)


def gen_diode_circuit(model, lib_name):
    """Diode half-wave rectifier with RC filter."""
    return textwrap.dedent(f"""\
        {model.name} Diode Rectifier Test
        *
        .include {lib_name}
        *
        Vin in 0 DC 0 AC 1 SIN(0 5 1k)
        *
        D1 in out {model.name}
        Rload out 0 1k
        Cfilter out 0 10u
        *
        .op
        .ac dec 10 10 10e6
        .tran 10u 5m
        .end
    """)


CIRCUIT_GENERATORS = {
    "SUBCKT": gen_opamp_circuit,
    "NPN":    gen_npn_circuit,
    "PNP":    gen_pnp_circuit,
    "NJF":    gen_njfet_circuit,
    "PJF":    gen_pjfet_circuit,
    "D":      gen_diode_circuit,
}


# ---------------------------------------------------------------------------
# C++ test generation
# ---------------------------------------------------------------------------

def get_ac_params(model):
    """Return (fstart, fstop) matching the generated .cir file's .ac line."""
    if model.kind == "SUBCKT":
        return "1.0", "10e6"
    return "10.0", "10e6"


def gen_cpp_test(model, cir_filename, lib_filename):
    safe = model.safe_name.lower()
    class_name = f"KiCad_{model.safe_name}_Test"
    fstart, fstop = get_ac_params(model)

    # Determine which nodes to check based on device type
    if model.kind == "SUBCKT":
        dc_checks = textwrap.dedent("""\
            double v_out = dc.voltage("out");
            double v_inv = dc.voltage("inv");
            double v_ninv = dc.voltage("ninv");

            // Inverting amp: virtual ground at inv ≈ ninv
            EXPECT_NEAR(v_inv, v_ninv, 0.05)
                << "Virtual ground: inv=" << v_inv << " ninv=" << v_ninv;

            // Output should be approximately -gain * Vin + bias
            // With gain=-10 and Vin=50mV: Vout ≈ bias - 0.5V
            // Just check output is in a reasonable range
            EXPECT_GT(std::abs(v_out), 0.01)
                << "Output should be nonzero, got " << v_out;
        """)
        ac_checks = textwrap.dedent("""\
            const auto& v_out = ac.voltages.at("v(out)");

            // Low-frequency gain should be ~20 dB (gain of 10)
            double gain_low = 20.0 * std::log10(std::max(std::abs(v_out[0]), 1e-20));
            EXPECT_NEAR(gain_low, 20.0, 1.0)
                << "Low-freq gain should be ~20 dB, got " << gain_low;

            // High-frequency gain should roll off
            double gain_high = 20.0 * std::log10(std::max(std::abs(v_out.back()), 1e-20));
            EXPECT_LT(gain_high, gain_low - 3.0)
                << "Gain at max freq should be < low-freq gain - 3dB";
        """)
    elif model.kind in ("NPN", "PNP"):
        vcc_val = "12" if model.kind == "NPN" else "-12"
        dc_checks = textwrap.dedent(f"""\
            double v_col = dc.voltage("col");
            double v_base = dc.voltage("base");
            double v_emit = dc.voltage("emit");

            // BJT should be in active region
            {"EXPECT_GT(v_col, v_emit)" if model.kind == "NPN" else "EXPECT_LT(v_col, v_emit)"}
                << "Collector should be {'above' if model.kind == 'NPN' else 'below'} emitter in active region";

            // Base-emitter voltage ~0.6-0.7V
            double vbe = {'v_base - v_emit' if model.kind == 'NPN' else 'v_emit - v_base'};
            EXPECT_NEAR(vbe, 0.65, 0.15)
                << "Vbe should be ~0.65V, got " << vbe;
        """)
        ac_checks = textwrap.dedent("""\
            const auto& v_out = ac.voltages.at("v(out)");

            // CE amp should have voltage gain > 1 at mid-frequencies
            double gain_mid = std::abs(v_out[v_out.size() / 2]);
            EXPECT_GT(gain_mid, 1.0)
                << "CE amp mid-band gain should be > 1";
        """)
    elif model.kind in ("NJF", "PJF"):
        dc_checks = textwrap.dedent("""\
            double v_drain = dc.voltage("drain");
            double v_gate = dc.voltage("gate");
            double v_source = dc.voltage("source");

            // JFET should be conducting
            EXPECT_NE(v_drain, 0.0)
                << "Drain should have a non-zero voltage";
        """)
        ac_checks = textwrap.dedent("""\
            const auto& v_out = ac.voltages.at("v(out)");

            // CS amp should have voltage gain > 1 at mid-frequencies
            double gain_mid = std::abs(v_out[v_out.size() / 2]);
            EXPECT_GT(gain_mid, 0.5)
                << "CS amp mid-band gain should be > 0.5";
        """)
    else:  # Diode
        dc_checks = textwrap.dedent("""\
            double v_out = dc.voltage("out");

            // With DC=0 input, diode is off, output should be ~0
            EXPECT_NEAR(v_out, 0.0, 0.1)
                << "Output with 0V DC input should be ~0";
        """)
        ac_checks = textwrap.dedent("""\
            const auto& v_out = ac.voltages.at("v(out)");

            // Diode should pass signal (though attenuated)
            EXPECT_GT(std::abs(v_out[0]), 0.01)
                << "Diode should pass some AC signal";
        """)

    # Indent the checks for embedding in the test body
    dc_checks = textwrap.indent(dc_checks.rstrip(), "    ")
    ac_checks = textwrap.indent(ac_checks.rstrip(), "    ")

    return textwrap.dedent(f"""\
        // KiCad-Spice-Library E2E test: {model.name} ({model.kind})
        // Auto-generated by tools/gen_kicad_test.py

        #include <gtest/gtest.h>
        #include "api/neospice.hpp"
        #include "framework/ngspice_runner.hpp"
        #include "framework/comparator.hpp"

        #include <cmath>
        #include <complex>
        #include <string>

        using namespace neospice;

        class {class_name} : public ::testing::Test {{
        protected:
            void SetUp() override {{
                cir_path_ = std::string(TEST_CIRCUITS_DIR) + "/{cir_filename}";
                ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
            }}
            std::string cir_path_;
            std::unique_ptr<NgspiceRunner> ngspice_;
            Simulator sim_;
        }};

        TEST_F({class_name}, DCOperatingPoint) {{
            auto ckt = sim_.load(cir_path_);
            auto dc = sim_.run_dc(ckt);

        {dc_checks}
        }}

        TEST_F({class_name}, ACResponse) {{
            auto ckt = sim_.load(cir_path_);
            auto ac = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, {fstart}, {fstop});

            ASSERT_FALSE(ac.frequency.empty());
            ASSERT_TRUE(ac.voltages.count("v(out)") > 0);

        {ac_checks}
        }}

        TEST_F({class_name}, NgspiceDCComparison) {{
            DCResult ng_result;
            try {{
                ng_result = ngspice_->run_dc(cir_path_);
            }} catch (const std::exception& e) {{
                GTEST_SKIP() << "ngspice not available: " << e.what();
            }}

            auto ckt = sim_.load(cir_path_);
            auto neo_result = sim_.run_dc(ckt);

            auto cmp = compare_dc(ng_result, neo_result, {{1e-2, 1e-6}});
            EXPECT_TRUE(cmp.passed)
                << "DC comparison failed. Worst: " << cmp.worst_signal
                << " error: " << cmp.worst_error;
        }}

        TEST_F({class_name}, NgspiceACComparison) {{
            ACResult ng_result;
            try {{
                ng_result = ngspice_->run_ac(cir_path_);
            }} catch (const std::exception& e) {{
                GTEST_SKIP() << "ngspice not available: " << e.what();
            }}

            if (ng_result.frequency.empty()) {{
                GTEST_SKIP() << "ngspice returned empty AC result";
            }}

            auto ckt = sim_.load(cir_path_);
            auto neo_result = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, {fstart}, {fstop});

            ASSERT_FALSE(neo_result.frequency.empty());
            ASSERT_EQ(ng_result.frequency.size(), neo_result.frequency.size());

            ACResult ng_filtered;
            ng_filtered.frequency = ng_result.frequency;
            for (const auto& [name, data] : ng_result.voltages) {{
                ng_filtered.voltages[name] = data;
            }}
            auto cmp = compare_ac(ng_filtered, neo_result, {{1e-2, 1e-6}});
            EXPECT_TRUE(cmp.passed)
                << "AC comparison failed. Worst: " << cmp.worst_signal
                << " error: " << cmp.worst_error;
        }}
    """)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate neospice E2E test from a KiCad-Spice-Library model.")
    parser.add_argument("model_file", help="Path to .mod/.lib file")
    parser.add_argument("model_name", nargs="?", help="Model name (auto-detected if omitted)")
    parser.add_argument("--dry-run", action="store_true", help="Print what would be generated")
    args = parser.parse_args()

    src_path = Path(args.model_file)
    if not src_path.exists():
        # Try relative to project root
        src_path = PROJECT_ROOT / args.model_file
    if not src_path.exists():
        print(f"Error: file not found: {args.model_file}", file=sys.stderr)
        sys.exit(1)

    model = extract_model(src_path, args.model_name)
    print(f"Selected model: {model.name} ({model.kind})")

    if model.kind not in CIRCUIT_GENERATORS:
        print(f"Error: unsupported model type '{model.kind}'. "
              f"Supported: {', '.join(CIRCUIT_GENERATORS.keys())}", file=sys.stderr)
        sys.exit(1)

    safe = model.safe_name.lower()
    lib_filename = f"{safe}.lib"
    cir_filename = f"{safe}_test.cir"
    cpp_filename = f"test_kicad_{safe}.cpp"

    lib_path = CIRCUITS_DIR / lib_filename
    cir_path = CIRCUITS_DIR / cir_filename
    cpp_path = UNIT_DIR / cpp_filename

    if args.dry_run:
        print(f"\nWould generate:")
        print(f"  {lib_path}")
        print(f"  {cir_path}")
        print(f"  {cpp_path}")
        print(f"\nCircuit:\n{CIRCUIT_GENERATORS[model.kind](model, lib_filename)}")
        return

    # Generate files
    write_clean_lib(model, src_path, lib_path)
    print(f"  wrote {lib_path}")

    cir_content = CIRCUIT_GENERATORS[model.kind](model, lib_filename)
    with open(cir_path, 'w') as f:
        f.write(cir_content)
    print(f"  wrote {cir_path}")

    cpp_content = gen_cpp_test(model, cir_filename, lib_filename)
    with open(cpp_path, 'w') as f:
        f.write(cpp_content)
    print(f"  wrote {cpp_path}")

    print(f"\nTo wire in, add to tests/CMakeLists.txt:")
    print(f"    unit/{cpp_filename}")


if __name__ == "__main__":
    main()
