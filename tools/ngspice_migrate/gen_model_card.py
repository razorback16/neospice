"""Generate model card conversion functions from descriptor.

Produces ``<ns>_model_card.hpp`` and ``<ns>_model_card.cpp`` that convert a
parsed :class:`ModelCard` (from the netlist parser) into the device-specific
``<Prefix>ModelCard`` struct by dispatching on SPICE model type and
calling the device's ``mParam()`` for each key/value pair.
"""
from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .descriptor import ModelDescriptor


def generate_model_card_hpp(desc) -> str:
    """Generate ``<ns>_model_card.hpp`` with ``to_<ns>_card`` declaration."""
    ns = desc.neospice_name
    prefix = desc.prefix

    lines = [
        '#pragma once',
        f'#include "devices/{ns}/{ns}_device.hpp"',
        '#include "parser/model_cards.hpp"',
        '#include <memory>',
        '',
        'namespace neospice {',
        '',
        f'std::unique_ptr<{prefix}ModelCard> to_{ns}_card(const ModelCard& card);',
        '',
        '} // namespace neospice',
    ]
    return '\n'.join(lines) + '\n'


def generate_model_card_cpp(desc) -> str:
    """Generate ``<ns>_model_card.cpp`` with ``to_<ns>_card`` implementation."""
    ns = desc.neospice_name
    prefix = desc.prefix

    lines = [
        f'#include "devices/{ns}/{ns}_model_card.hpp"',
        f'#include "devices/{ns}/{ns}_def.hpp"',
        f'#include "devices/{ns}/{ns}_shim.hpp"',
        '#include <algorithm>',
        '#include <cstdio>',
        '#include <cstring>',
        '#include <cctype>',
        '',
        'namespace neospice {',
        '',
        'static std::string to_lower_mc(const std::string& s) {',
        '    std::string r = s;',
        '    std::transform(r.begin(), r.end(), r.begin(), ::tolower);',
        '    return r;',
        '}',
        '',
        f'std::unique_ptr<{prefix}ModelCard> to_{ns}_card(const ModelCard& card) {{',
        f'    auto out = std::make_unique<{prefix}ModelCard>();',
        f'    auto& ucb = out->ucb;',
        '',
    ]

    # Type dispatch
    if hasattr(desc, 'model_types') and desc.model_types:
        first = True
        for mt in desc.model_types:
            keyword = 'if' if first else '} else if'
            first = False
            lines.append(f'    {keyword} (card.type == "{mt.spice_name}") {{')
            if mt.flag_field:
                lines.append(f'        ucb.{mt.flag_field} = {mt.flag_value};')
                lines.append(f'        ucb.{mt.flag_field}Given = 1;')
        lines.append('    } else {')
        types_str = "/".join(mt.spice_name.upper() for mt in desc.model_types)
        lines.append(
            f'        throw ParseError("Model \'" + card.name + '
            f'"\': unsupported type \'" + card.type + '
            f'"\' (expected {types_str})");'
        )
        lines.append('    }')
    lines.append('')

    # Parameter dispatch loop
    lines.append('    for (const auto& [lkey, val] : card.params) {')
    lines.append('        if (lkey == "level") continue;')
    lines.append('')
    lines.append(f'        const {ns}::Shim::IfParm* entry = nullptr;')
    lines.append(f'        for (int i = 0; i < {ns}::{prefix}mPTSize; ++i) {{')
    lines.append(f'            if (std::strcmp({ns}::{prefix}mPTable[i].keyword, lkey.c_str()) == 0) {{')
    lines.append(f'                entry = &{ns}::{prefix}mPTable[i];')
    lines.append('                break;')
    lines.append('            }')
    lines.append('        }')
    lines.append('        if (entry == nullptr) {')
    lines.append(f'            std::fprintf(stderr, "Warning: model \'%s\': unknown {prefix} parameter \'%s\' (ignored)\\n",')
    lines.append('                card.name.c_str(), lkey.c_str());')
    lines.append('            continue;')
    lines.append('        }')
    lines.append('')
    lines.append(f'        {ns}::Shim::IfValue v{{}};')
    lines.append('        int dtype = entry->dataType & 0x1F;')
    lines.append(f'        if (dtype & {ns}::Shim::IF_REAL) {{')
    lines.append('            v.rValue = val;')
    lines.append(f'        }} else if (dtype & {ns}::Shim::IF_INTEGER) {{')
    lines.append('            v.iValue = static_cast<int>(val);')
    lines.append(f'        }} else if (dtype & {ns}::Shim::IF_FLAG) {{')
    lines.append('            v.iValue = (val != 0.0) ? 1 : 0;')
    lines.append(f'        }} else if (dtype & {ns}::Shim::IF_STRING) {{')
    lines.append(f'            std::fprintf(stderr, "Warning: model \'%s\': string parameter \'%s\' not supported; using default\\n",')
    lines.append('                card.name.c_str(), lkey.c_str());')
    lines.append('            continue;')
    lines.append('        } else {')
    lines.append('            continue;')
    lines.append('        }')
    lines.append('')
    lines.append(f'        int rc = {ns}::{prefix}mParam(entry->id, &v, &ucb);')
    lines.append(f'        if (rc != {ns}::Shim::OK) {{')
    lines.append(f'            throw ParseError("Model \'" + card.name + "\': {prefix}mParam failed for \'" + lkey + "\'");')
    lines.append('        }')
    lines.append('    }')
    lines.append('')
    lines.append('    return out;')
    lines.append('}')
    lines.append('')
    lines.append('} // namespace neospice')

    return '\n'.join(lines) + '\n'
