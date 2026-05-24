#!/usr/bin/env python3
"""
Test KiCad SPICE model library against neospice.

For each model file, extracts .model and .subckt definitions, wraps each in
a minimal test circuit (.op), and runs through neospice. Reports:
  - PARSE_ERROR: neospice couldn't parse the netlist
  - SIM_ERROR: parsed but simulation failed (convergence, etc.)
  - WARNING: ran but produced warnings
  - OK: ran cleanly

Usage:
    python3 tools/test_kicad_models.py [--neospice PATH] [--category CATEGORY]
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

KICAD_LIB = Path(__file__).parent.parent / "third_party" / "KiCad-Spice-Library" / "Models"

def is_binary_file(filepath):
    """Check if a file is binary by looking for null bytes in the first 8KB."""
    try:
        with open(filepath, 'rb') as f:
            chunk = f.read(8192)
            return b'\x00' in chunk or b'\xff\xfe' in chunk[:2] or b'\xfe\xff' in chunk[:2]
    except Exception:
        return True


def extract_models(filepath):
    """Extract top-level .model definitions from a SPICE file.

    Models defined inside .subckt/.ends blocks are internal to that
    subcircuit and cannot be tested standalone, so we skip them.
    """
    models = []
    if is_binary_file(filepath):
        return models
    try:
        text = filepath.read_text(errors='replace')
    except Exception:
        return models

    # Build a set of line numbers that are inside .subckt/.ends blocks
    depth = 0
    inside_subckt = set()
    for lineno, raw_line in enumerate(text.splitlines(), start=1):
        stripped = raw_line.strip().lower()
        if stripped.startswith('.subckt'):
            depth += 1
        if depth > 0:
            inside_subckt.add(lineno)
        if stripped.startswith('.ends'):
            depth = max(depth - 1, 0)

    for m in re.finditer(
        r'^\s*\.model\s+(\S+)\s+(\S+)\s*\(?(.*?)(?:\)|$)',
        text, re.IGNORECASE | re.MULTILINE | re.DOTALL
    ):
        # Determine the line number of this match
        match_lineno = text[:m.start()].count('\n') + 1
        if match_lineno in inside_subckt:
            continue  # skip models inside subcircuit blocks
        name = m.group(1)
        mtype = m.group(2).upper()
        models.append((name, mtype, str(filepath.resolve())))
    return models


def classify_pin_role(description):
    """Classify a pin description string into a role for test circuit generation.

    Returns one of: 'vcc', 'vee', 'inp', 'inm', 'out', 'gnd', 'generic'.
    """
    d = description.strip().lower()

    # Check for standalone single-character tokens
    if d in ('+',):
        return 'inp'
    if d in ('-',):
        return 'inm'
    if d in ('o',):
        return 'out'

    # VCC patterns (positive supply)
    for kw in ('positive power supply', 'positive supply', 'positive power-supply',
               '+vcc', '+v', '+vsupply', 'vcc', 'vdd', 'v+'):
        if kw in d:
            return 'vcc'

    # VEE patterns (negative supply)
    for kw in ('negative power supply', 'negative supply', 'negative power-supply',
               '-vee', '-v', '-vsupply', 'vee', 'vss', 'v-'):
        if kw in d:
            return 'vee'

    # Non-inverting input (MUST check before inverting)
    for kw in ('non-inverting', 'non inverting', 'noninverting',
               '+in', '+input', 'in+'):
        if kw in d:
            return 'inp'
    # 'inp' as standalone word (avoid matching inside "inverting input")
    if re.search(r'\binp\b', d):
        return 'inp'

    # Inverting input (after non-inverting check)
    for kw in ('inverting input', 'inverting',
               '-in', '-input', 'in-'):
        if kw in d:
            return 'inm'
    # 'inm' as standalone word
    if re.search(r'\binm\b', d):
        return 'inm'

    # Output
    for kw in ('output', 'out', 'vout'):
        if kw in d:
            return 'out'

    # Ground
    for kw in ('ground', 'gnd', 'common'):
        if kw in d:
            return 'gnd'

    return 'generic'


def parse_pin_comments(lines, subckt_line_idx, num_ports):
    """Parse pin-mapping comments above a .SUBCKT definition.

    Tries three patterns (pipe-tree, inline tabular, compact inline).
    Returns a list of role strings (length = num_ports), or None.
    """
    # Determine how far back to look (up to 20 lines)
    start = max(0, subckt_line_idx - 20)
    preceding = lines[start:subckt_line_idx]

    # --- Pattern A: Pipe-tree ---
    # Look for consecutive comment lines containing '|'
    # In the pipe-tree format, each line's pipe count indicates which pin
    # the description belongs to:
    #   * connections:      non-inverting input   <- pin 0 (no pipes)
    #   *                   |   inverting input   <- pin 1 (1 pipe)
    #   *                   |   |   positive ...  <- pin 2 (2 pipes)
    # Lines with pipes but no description (only '|' chars) are just
    # continuation markers and are skipped.
    pipe_lines = []
    header_line = None  # The "connections:" line above the pipes (pin 0)
    for i in range(len(preceding) - 1, -1, -1):
        line = preceding[i]
        stripped = line.strip()
        if stripped.startswith('*') and '|' in stripped:
            pipe_lines.insert(0, (start + i, stripped))
        elif pipe_lines:
            # Check if this is the header line (e.g., "* connections: non-inverting input")
            if stripped.startswith('*'):
                # Look for a description on this header line
                m_hdr = re.match(r'\*\s*(?:[Cc]onnections:\s*)?(.+)', stripped)
                if m_hdr and header_line is None:
                    candidate = m_hdr.group(1).strip()
                    # Only treat as pin-0 header if it looks like a pin description
                    # (not a section separator like "///" or "***")
                    if candidate and not re.match(r'^[/\*=\-]+$', candidate):
                        header_line = candidate
                continue
            break

    if pipe_lines:
        roles = [None] * num_ports

        # If we found a header line, it describes pin 0
        if header_line:
            # Extract description: if it has "connections:" prefix, take what follows
            hdr_match = re.match(r'(?:[Cc]onnections:\s*)(.*)', header_line)
            desc = hdr_match.group(1).strip() if hdr_match else header_line
            if desc:
                roles[0] = classify_pin_role(desc)

        for _, pline in pipe_lines:
            # Remove the leading '*'
            content = pline[1:] if pline.startswith('*') else pline
            # Count pipes and find description
            pipe_count = content.count('|')
            # Extract text after last pipe
            last_pipe_idx = content.rfind('|')
            desc = content[last_pipe_idx + 1:].strip()
            if desc and pipe_count > 0:
                pin_idx = pipe_count  # pipe_count = pin index (0-based)
                if pin_idx < num_ports:
                    roles[pin_idx] = classify_pin_role(desc)

        # Only return if we got at least some roles
        if any(r is not None for r in roles):
            # Fill any None entries with 'generic'
            return [r if r is not None else 'generic' for r in roles]

    # --- Pattern C: Inline tabular header ---
    # Look for a comment line 1-3 lines above .SUBCKT where token count matches num_ports
    for offset in range(1, min(4, len(preceding) + 1)):
        idx = len(preceding) - offset
        if idx < 0:
            break
        line = preceding[idx].strip()
        if not line.startswith('*'):
            continue
        content = line[1:].strip()  # remove '*'
        tokens = content.split()
        if len(tokens) == num_ports and all(t == t.upper() and t.isalpha() or
                                              re.match(r'^[A-Z][A-Z0-9]*[+\-]?$', t) or
                                              t in ('+', '-', 'O')
                                              for t in tokens):
            roles = [classify_pin_role(t) for t in tokens]
            return roles

    # --- Pattern B: Compact inline ---
    # Match "Connections:" in a comment line 1-5 lines above
    for offset in range(1, min(6, len(preceding) + 1)):
        idx = len(preceding) - offset
        if idx < 0:
            break
        line = preceding[idx].strip()
        if not line.startswith('*'):
            continue
        m = re.match(r'\*\s*[Cc]onnections:\s*(.*)', line)
        if m:
            raw = m.group(1).strip()
            if not raw:
                continue
            # Split glued tokens like V+V-O
            # Split on boundaries between known role keywords
            tokens = re.findall(r'V[+\-]|IN[+\-]|OUT|GND|LE|[+\-]|O|[A-Za-z][A-Za-z0-9]*', raw)
            if len(tokens) == num_ports:
                roles = [classify_pin_role(t) for t in tokens]
                return roles

    return None


def detect_file_convention(lines):
    """Detect a file-level pin convention header.

    Scans the first ~100 lines for a CONNECTIONS: line that does NOT
    immediately precede a .SUBCKT (within 5 lines). Returns a list of
    roles, or None.
    """
    scan_end = min(len(lines), 100)
    for i in range(scan_end):
        line = lines[i].strip()
        if not line.startswith('*'):
            continue
        m = re.match(r'\*\s*CONNECTIONS:\s+(.*)', line, re.IGNORECASE)
        if not m:
            continue
        raw = m.group(1).strip()
        if not raw:
            continue

        # Check that this does NOT immediately precede a .SUBCKT (within 5 lines)
        near_subckt = False
        for j in range(i + 1, min(i + 6, len(lines))):
            if lines[j].strip().lower().startswith('.subckt'):
                near_subckt = True
                break
        if near_subckt:
            continue

        # Parse tokens (handle glued like V+V-O)
        tokens = re.findall(r'V[+\-]|IN[+\-]|OUT|GND|LE|CA|CB|[+\-]|O|[A-Za-z][A-Za-z0-9]*', raw)
        if tokens:
            roles = [classify_pin_role(t) for t in tokens]
            return roles

    return None


def extract_subcircuits(filepath):
    """Extract top-level .subckt definitions from a SPICE file.

    Nested subcircuits (defined inside another .subckt/.ends block)
    are internal and cannot be instantiated standalone, so we skip them.
    Returns list of (name, ports, filepath, roles) tuples.
    """
    subcircuits = []
    if is_binary_file(filepath):
        return subcircuits
    try:
        text = filepath.read_text(errors='replace')
    except Exception:
        return subcircuits

    lines = text.splitlines()

    # Detect file-level pin convention (e.g., LinearTech.lib header)
    file_convention = detect_file_convention(lines)

    # Build a set of line numbers that are inside .subckt/.ends blocks.
    # depth tracks nesting; we want only depth==0 definitions.
    depth = 0
    inside_subckt = set()
    for lineno, raw_line in enumerate(lines, start=1):
        stripped = raw_line.strip().lower()
        # Mark lines at depth > 0 BEFORE incrementing on .subckt
        if stripped.startswith('.subckt'):
            if depth > 0:
                inside_subckt.add(lineno)
            depth += 1
        elif stripped.startswith('.ends'):
            depth = max(depth - 1, 0)
        elif depth > 0:
            inside_subckt.add(lineno)

    for m in re.finditer(
        r'^\s*\.subckt\s+(\S+)\s+(.*?)$',
        text, re.IGNORECASE | re.MULTILINE
    ):
        match_lineno = text[:m.start()].count('\n') + 1
        if match_lineno in inside_subckt:
            continue  # skip nested subcircuits
        name = m.group(1)
        ports_line = m.group(2).strip()
        # Ports are space-separated until we hit params: or key=val
        ports = []
        for tok in ports_line.split():
            if '=' in tok or tok.upper() == 'PARAMS:':
                break
            ports.append(tok)

        # Try to determine pin roles from comments
        subckt_line_idx = match_lineno - 1  # 0-based index into lines list
        roles = parse_pin_comments(lines, subckt_line_idx, len(ports))

        # Fall back to file convention if pin comments didn't match
        if roles is None and file_convention is not None and len(ports) == len(file_convention):
            roles = file_convention

        # Fall back to default 5-pin op-amp convention
        if roles is None and len(ports) == 5:
            roles = ['inp', 'inm', 'vcc', 'vee', 'out']

        subcircuits.append((name, ports, str(filepath.resolve()), roles))
    return subcircuits


def make_model_test(model_name, model_type, filepath):
    """Generate a test circuit for a standalone .model card."""
    mtype = model_type.upper()

    if mtype in ('D',):
        return f"""* Test {model_name}
.include "{filepath}"
V1 anode 0 0.7
D1 anode 0 {model_name}
.op
.end
"""
    elif mtype in ('NPN',):
        return f"""* Test {model_name}
.include "{filepath}"
VCC vcc 0 5
VBB bb 0 0.7
RC vcc col 1k
RB bb base 10k
Q1 col base 0 {model_name}
.op
.end
"""
    elif mtype in ('PNP',):
        return f"""* Test {model_name}
.include "{filepath}"
VCC vcc 0 -5
VBB bb 0 -0.7
RC vcc col 1k
RB bb base 10k
Q1 col base 0 {model_name}
.op
.end
"""
    elif mtype in ('NJF', 'PJF'):
        sign = 1 if mtype == 'NJF' else -1
        return f"""* Test {model_name}
.include "{filepath}"
VDD vdd 0 {sign * 5}
VGG gg 0 0
RD vdd drain 1k
J1 drain gg 0 {model_name}
.op
.end
"""
    elif mtype in ('NMOS', 'PMOS'):
        sign = 1 if mtype == 'NMOS' else -1
        return f"""* Test {model_name}
.include "{filepath}"
VDD vdd 0 {sign * 3}
VGG gg 0 {sign * 1.5}
RD vdd drain 1k
M1 drain gg 0 0 {model_name} W=10u L=1u
.op
.end
"""
    elif mtype in ('NMF', 'PMF'):
        return f"""* Test {model_name}
.include "{filepath}"
VDD vdd 0 3
VGG gg 0 0
RD vdd drain 1k
Z1 drain gg 0 {model_name}
.op
.end
"""
    elif mtype in ('SW', 'CSW', 'R', 'C', 'L'):
        return None  # skip passive/switch models
    else:
        return None


def make_subcircuit_test(name, ports, filepath, roles=None):
    """Generate a test circuit for a subcircuit based on port count."""
    n = len(ports)
    if n < 2:
        return None

    # Heuristic: figure out likely pin functions from port names
    port_names_upper = [p.upper() for p in ports]

    lines = [f"* Test subcircuit {name}"]
    lines.append(f'.include "{filepath}"')

    # Supply voltages — check both named ports and roles
    has_vcc = any(p in ('VCC', 'VDD', 'V+', 'VP', 'VS+', 'AVDD', 'VCC+',
                        'V_SUPPLY', 'VCC_IN', 'PWR') for p in port_names_upper)
    has_vee = any(p in ('VEE', 'VSS', 'V-', 'VN', 'VS-', 'AVSS', 'VCC-',
                        'GND', 'DGND', 'AGND') for p in port_names_upper)

    if not has_vcc and roles:
        has_vcc = 'vcc' in roles
    if not has_vee and roles:
        has_vee = 'vee' in roles

    if has_vcc:
        lines.append("VCC vcc_net 0 5")
    if has_vee:
        lines.append("VEE vee_net 0 -5")

    # Build instance call: connect each port
    # When roles are available from comment parsing, they take priority over
    # the named-port heuristic (port names like 'VP' are ambiguous — could
    # mean V+ supply or non-inverting input depending on the model).
    inst_ports = []
    for i, p in enumerate(ports):
        pu = p.upper()
        role = roles[i] if roles and i < len(roles) else None

        if role and role != 'generic':
            if role == 'vcc':
                inst_ports.append("vcc_net")
            elif role == 'vee':
                inst_ports.append("vee_net")
            elif role == 'inp':
                inst_ports.append("in_p")
            elif role == 'inm':
                inst_ports.append("in_m")
            elif role == 'out':
                inst_ports.append("out_net")
            elif role == 'gnd':
                inst_ports.append("0")
            else:
                net = f"net_{p.lower()}"
                inst_ports.append(net)
        elif pu in ('VCC', 'VDD', 'V+', 'VP', 'VS+', 'AVDD', 'VCC+',
                   'V_SUPPLY', 'VCC_IN', 'PWR'):
            inst_ports.append("vcc_net")
        elif pu in ('VEE', 'VSS', 'V-', 'VN', 'VS-', 'AVSS', 'VCC-'):
            inst_ports.append("vee_net")
        elif pu in ('GND', 'DGND', 'AGND', 'GROUND', '0'):
            inst_ports.append("0")
        elif pu in ('OUT', 'OUTPUT', 'VOUT', 'Y', 'Q', 'QB', 'OUT+', 'OUT-',
                     'OUTA', 'OUTB'):
            inst_ports.append("out_net")
        elif pu in ('IN', 'INPUT', 'VIN', 'IN+', 'INP', 'INA',
                     'NON-INVERTING', 'A'):
            inst_ports.append("in_p")
        elif pu in ('IN-', 'INM', 'INB', 'INVERTING', 'B'):
            inst_ports.append("in_m")
        else:
            net = f"net_{p.lower()}"
            inst_ports.append(net)

    # Create the instance line
    port_str = " ".join(inst_ports)
    lines.append(f"X1 {port_str} {name}")

    # Input sources
    if 'in_p' in inst_ports:
        lines.append("VIN in_p 0 DC 0.5")
    if 'in_m' in inst_ports:
        lines.append("RFB out_net in_m 10k")
        lines.append("RIN in_m 0 10k")

    # Load resistor on output
    if 'out_net' in inst_ports:
        lines.append("RLOAD out_net 0 10k")

    # Tie any undriven generic nets to ground through resistors
    for p in ports:
        net = f"net_{p.lower()}"
        if net in inst_ports and net not in ('in_p', 'in_m', 'out_net',
                                              'vcc_net', 'vee_net', '0'):
            lines.append(f"R_{p.lower()} {net} 0 100k")

    lines.append(".op")
    lines.append(".end")
    return "\n".join(lines) + "\n"


def run_neospice(netlist_text, neospice_bin, timeout=10):
    """Run neospice on a netlist string. Returns (status, output)."""
    with tempfile.NamedTemporaryFile(mode='w', suffix='.spice', delete=False) as f:
        f.write(netlist_text)
        f.flush()
        tmp_path = f.name

    try:
        result = subprocess.run(
            [neospice_bin, tmp_path],
            capture_output=True, text=True, timeout=timeout
        )
        combined = result.stdout + result.stderr
        if result.returncode != 0:
            # Check if it's a parse error or sim error
            if 'Parse error' in combined or 'ParseError' in combined:
                return 'PARSE_ERROR', combined.strip()
            elif 'convergence' in combined.lower() or 'failed' in combined.lower():
                return 'SIM_ERROR', combined.strip()
            else:
                return 'ERROR', combined.strip()
        elif 'Warning' in combined or 'warning' in combined:
            return 'WARNING', combined.strip()
        else:
            return 'OK', combined.strip()
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', 'Timed out after 10s'
    except Exception as e:
        return 'ERROR', str(e)
    finally:
        os.unlink(tmp_path)


def main():
    parser = argparse.ArgumentParser(description='Test KiCad SPICE models against neospice')
    parser.add_argument('--neospice', default=str(Path(__file__).parent.parent / 'build' / 'neospice'),
                        help='Path to neospice binary')
    parser.add_argument('--category', default=None,
                        help='Only test a specific category (Diode, Transistor, etc.)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show details for each test')
    parser.add_argument('--errors-only', action='store_true',
                        help='Only show failures')
    parser.add_argument('--max', type=int, default=0,
                        help='Max models to test (0=all)')
    parser.add_argument('--save', default=None,
                        help='Save full results to JSON file')
    parser.add_argument('--error-type', default=None,
                        help='Only show specific error type (PARSE_ERROR, SIM_ERROR, ERROR, TIMEOUT)')
    parser.add_argument('--file', default=None,
                        help='Only test models from files matching this substring')
    parser.add_argument('--show-stderr', action='store_true',
                        help='Show full stderr output for failures (not just first line)')
    parser.add_argument('--dump-netlist', action='store_true',
                        help='Print the generated test netlist for failing tests')
    args = parser.parse_args()

    if not os.path.exists(args.neospice):
        print(f"Error: neospice binary not found at {args.neospice}")
        sys.exit(1)

    # Collect all model files
    model_files = []
    for ext in ('*.lib', '*.mod', '*.sub', '*.spice', '*.cir'):
        if args.category:
            pattern = f"**/{args.category}/**/{ext}"
        else:
            pattern = f"**/{ext}"
        model_files.extend(KICAD_LIB.glob(pattern))

    model_files = sorted(set(model_files))
    print(f"Found {len(model_files)} model files")

    # Extract all models and subcircuits
    all_tests = []
    for mf in model_files:
        for name, mtype, fpath in extract_models(mf):
            netlist = make_model_test(name, mtype, fpath)
            if netlist:
                rel_path = mf.relative_to(KICAD_LIB)
                all_tests.append(('model', name, mtype, netlist, str(rel_path)))

        for name, ports, fpath, roles in extract_subcircuits(mf):
            netlist = make_subcircuit_test(name, ports, fpath, roles)
            if netlist:
                rel_path = mf.relative_to(KICAD_LIB)
                all_tests.append(('subckt', name, f'{len(ports)}-port', netlist, str(rel_path)))

    if args.file:
        all_tests = [t for t in all_tests if args.file.lower() in t[4].lower()]

    if args.max > 0:
        all_tests = all_tests[:args.max]

    print(f"Generated {len(all_tests)} test circuits")
    print()

    # Run tests
    stats = defaultdict(int)
    errors_by_type = defaultdict(list)
    total = len(all_tests)
    all_results = []

    for i, (kind, name, info, netlist, rel_path) in enumerate(all_tests):
        status, output = run_neospice(netlist, args.neospice)
        stats[status] += 1

        all_results.append({
            'name': name,
            'kind': kind,
            'info': info,
            'status': status,
            'output': output,
            'file': rel_path,
            'netlist': netlist,
        })

        if status not in ('OK', 'WARNING') or args.verbose:
            show = True
            if args.errors_only and status in ('OK', 'WARNING'):
                show = False
            if args.error_type and status != args.error_type:
                show = False
            if show:
                # Extract first meaningful error line
                err_line = ''
                for line in output.split('\n'):
                    if 'error' in line.lower() or 'Error' in line or 'failed' in line.lower():
                        err_line = line.strip()[:120]
                        break
                if not err_line and output:
                    err_line = output.split('\n')[0][:120]

                print(f"[{status:12s}] {kind:7s} {name:30s} ({rel_path})")
                if args.show_stderr and output:
                    for line_text in output.split('\n')[:20]:
                        print(f"              {line_text.strip()[:140]}")
                elif err_line and args.verbose:
                    print(f"              {err_line}")
                if args.dump_netlist:
                    print("--- NETLIST ---")
                    print(netlist)
                    print("--- END ---")

        if status not in ('OK', 'WARNING'):
            # Categorize error
            first_err = output.split('\n')[0] if output else ''
            # Extract error pattern (normalize model/instance names away)
            pattern = re.sub(r"'[^']*'", "'...'", first_err)
            pattern = re.sub(r"Line \d+:", "Line N:", pattern)
            pattern = re.sub(r"=\S+", "=...", pattern)
            errors_by_type[pattern].append((name, rel_path))

        # Progress indicator
        if (i + 1) % 50 == 0:
            print(f"  ... {i+1}/{total} tested", file=sys.stderr)

    # Summary
    print()
    print("=" * 70)
    print("SUMMARY")
    print("=" * 70)
    for status in ('OK', 'WARNING', 'PARSE_ERROR', 'SIM_ERROR', 'ERROR', 'TIMEOUT'):
        if stats[status]:
            pct = 100.0 * stats[status] / total
            bar = '#' * int(pct / 2)
            print(f"  {status:12s}: {stats[status]:4d} / {total}  ({pct:5.1f}%)  {bar}")

    if errors_by_type:
        print()
        print("ERROR PATTERNS (most common first):")
        print("-" * 70)
        sorted_patterns = sorted(errors_by_type.items(), key=lambda x: -len(x[1]))
        for pattern, instances in sorted_patterns[:30]:
            print(f"  [{len(instances):3d}x] {pattern[:100]}")
            if args.verbose:
                for name, path in instances[:3]:
                    print(f"         - {name} ({path})")

        # Per-file summary
        file_errors = defaultdict(int)
        for pattern, instances in errors_by_type.items():
            for name, path in instances:
                file_errors[path] += 1
        if file_errors:
            print()
            print("FAILURES BY FILE (top 20):")
            print("-" * 70)
            for path, count in sorted(file_errors.items(), key=lambda x: -x[1])[:20]:
                print(f"  [{count:3d}x] {path}")

    print()
    pass_rate = 100.0 * (stats['OK'] + stats['WARNING']) / total if total else 0
    print(f"Pass rate: {pass_rate:.1f}% ({stats['OK'] + stats['WARNING']}/{total})")

    if args.save:
        import json
        with open(args.save, 'w') as f:
            json.dump({
                'total': total,
                'stats': dict(stats),
                'results': [r for r in all_results if r['status'] not in ('OK', 'WARNING')],
            }, f, indent=2)
        print(f"Results saved to {args.save}")


if __name__ == '__main__':
    main()
