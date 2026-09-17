#!/usr/bin/env python3
"""Reproduce fixed paired benchmark fixtures; --check never modifies files.

The static DC families preserve bench_solver_throughput.cpp's full population
and electrical topology. Dynamic grids are synthetic coverage, not research use.
"""
import argparse
import hashlib
import json
from pathlib import Path

MESH_SIDES = (10, 32, 71, 141)  # lround(sqrt(100, 1000, 5000, 20000))
LADDER_STAGES = (100, 1000, 5000, 20000)
DYNAMIC_SIDES = (8, 24, 48)


def resistor_mesh(side):
    lines = [f'* 2D resistor mesh {side}x{side}', 'Vin n0_0 0 DC 1']
    def node(x, y):
        return '0' if x == side-1 and y == side-1 else f'n{x}_{y}'
    count = 0
    for y in range(side):
        for x in range(side):
            for dx, dy in ((1, 0), (0, 1)):
                if x+dx < side and y+dy < side:
                    lines.append(f'R{count} {node(x,y)} {node(x+dx,y+dy)} 1k')
                    count += 1
    return '\n'.join(lines+['.op', '.end', ''])


def diode_ladder(stages):
    lines = [f'* diode ladder, {stages} stages', '.model DMOD D(IS=1e-14 N=1.0)',
             'Vin in 0 DC 5', 'Rin in n0 1k']
    for i in range(stages):
        lines += [f'D{i} n{i} n{i+1} DMOD', f'Rs{i} n{i+1} 0 10k']
    return '\n'.join(lines+['.op', '.end', ''])


def dynamic_grid(side, nonlinear):
    lines = [f'* synthetic {"diode_rc" if nonlinear else "rc"} grid {side}x{side}',
             'V1 in 0 DC 0.8 AC 1 PULSE(0.2 0.8 0 10u 10u 50u 100u)',
             'Rfeed in n0_0 1k']
    if nonlinear:
        lines.append('.model DGRID D(IS=1e-12 N=1.2 RS=2 CJO=1p M=0.4 TT=1n)')
    def node(x, y):
        return 'out' if x == side//2 and y == side//2 else f'n{x}_{y}'
    for y in range(side):
        for x in range(side):
            n = node(x, y)
            if x+1 < side: lines.append(f'Rh{x}_{y} {n} {node(x+1,y)} 1k')
            if y+1 < side: lines.append(f'Rv{x}_{y} {n} {node(x,y+1)} 1k')
            lines.append(f'C{x}_{y} {n} 0 1n')
            if nonlinear: lines.append(f'D{x}_{y} {n} 0 DGRID')
    lines += [f'Rreturn {node(side-1,side-1)} 0 1k', '.op', '.end', '']
    return '\n'.join(lines)


def fixtures():
    result = {}
    for side in MESH_SIDES:
        result[f'mesh_{side}.cir'] = resistor_mesh(side)
    for stages in LADDER_STAGES:
        result[f'diode_ladder_{stages}.cir'] = diode_ladder(stages)
    for nonlinear in (False, True):
        family = 'diode_rc_grid' if nonlinear else 'rc_grid'
        for side in DYNAMIC_SIDES:
            result[f'{family}_{side}.cir'] = dynamic_grid(side, nonlinear)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]/'tests/circuits/paired')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    contents = fixtures()
    manifest = {'schema': 1, 'generator': 'tools/generate_paired_circuits.py',
                'families': {'mesh_sides': MESH_SIDES, 'diode_ladder_stages': LADDER_STAGES,
                             'dynamic_grid_sides': DYNAMIC_SIDES},
                'fixtures': {name: {'bytes': len(value.encode()), 'sha256': hashlib.sha256(value.encode()).hexdigest()}
                             for name, value in contents.items()}}
    contents['manifest.json'] = json.dumps(manifest, indent=2)+'\n'
    if not args.check:
        args.output.mkdir(parents=True, exist_ok=True)
    for name, value in contents.items():
        path = args.output/name
        if args.check:
            if not path.is_file() or path.read_bytes() != value.encode():
                raise SystemExit(f'Generated fixture differs: {path}')
        else:
            path.write_bytes(value.encode())
    print(f'{"Verified" if args.check else "Generated"} {len(contents)-1} fixed circuit fixtures')


if __name__ == '__main__':
    main()
