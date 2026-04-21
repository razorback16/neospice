"""Generate parser integration helpers from descriptor."""
from __future__ import annotations


def generate_parser_hpp(desc) -> str:
    """Generate <ns>_parser.hpp with device creation and model card helpers."""
    ns = desc.neospice_name
    prefix = desc.prefix
    terminals = desc.terminals
    geom_params = desc.geometry if hasattr(desc, 'geometry') else []

    lines = [
        '#pragma once',
        f'#include "devices/{ns}/{ns}_device.hpp"',
        f'#include "devices/{ns}/{ns}_model_card.hpp"',
        '#include "parser/model_cards.hpp"',
        '#include <memory>',
        '#include <string>',
        '#include <unordered_map>',
        '',
        'namespace neospice {',
        '',
        '// --- Model card cache and creation ---',
        f'inline std::unique_ptr<{prefix}ModelCard> create_{ns}_model_card(',
        '        const ModelCard& card) {',
        f'    return to_{ns}_card(card);',
        '}',
        '',
    ]

    # Geometry struct fill helper comment
    lines.append(f'// --- Geometry fill helper ---')
    lines.append(f'// Populate a {prefix}Device::Geom from parsed element geometry.')
    lines.append('')

    # Device creation helper comment showing the make() call pattern
    lines.append(f'// --- Device creation ---')
    term_args = ", ".join(f"n_{t.name}" for t in terminals)
    lines.append(f'// Use {prefix}Device::make(name, {term_args}, geom, model_card)')
    lines.append(f'// Terminals: {", ".join(t.name for t in terminals)}')
    if geom_params:
        lines.append(f'// Geometry: {", ".join(g.name for g in geom_params)}')
    lines.append('')

    lines.append('} // namespace neospice')
    return '\n'.join(lines) + '\n'
