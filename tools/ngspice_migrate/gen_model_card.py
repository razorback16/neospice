"""Generate model card conversion functions from descriptor.

Produces ``<ns>_model_card.hpp`` and ``<ns>_model_card.cpp`` that convert a
parsed :class:`ModelCard` (from the netlist parser) into the device-specific
``<Prefix>ModelCard`` struct by dispatching on SPICE model type and
calling the device's ``mParam()`` for each key/value pair.

The generated .cpp uses the shared ``convert_model_card_params`` template
from ``devices/model_card_utils.hpp`` to avoid duplicating the parameter
conversion loop across devices.
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
        '#include "devices/model_card_utils.hpp"',
        '',
        'namespace neospice {',
        '',
        f'std::unique_ptr<{prefix}ModelCard> to_{ns}_card(const ModelCard& card) {{',
        f'    auto out = std::make_unique<{prefix}ModelCard>();',
        f'    auto& ucb = out->ucb;',
        '',
    ]

    # Type dispatch using validate_model_type
    if hasattr(desc, 'model_types') and desc.model_types:
        # Build the constexpr type entries array
        entries = ', '.join(
            f'{{"{mt.spice_name}", {mt.flag_value}}}'
            for mt in desc.model_types
        )
        lines.append(f'    constexpr ModelCardTypeEntry types[] = {{{entries}}};')

        # All model types share the same flag_field for a given device
        flag_field = desc.model_types[0].flag_field
        lines.append(f'    ucb.{flag_field} = validate_model_type(card, types);')

        # Set the typeGiven field if applicable
        mt0 = desc.model_types[0]
        if mt0.has_type_given and mt0.flag_field:
            given_field = mt0.given_field
            lines.append(f'    ucb.{given_field} = 1;')

    lines.append('')

    # Parameter conversion using shared template
    lines.append(f'    namespace S = {ns}::Shim;')
    lines.append(f'    convert_model_card_params<S::IfParm, S::IfValue>(')
    lines.append(f'        card, ucb, {ns}::{prefix}mPTable, {ns}::{prefix}mPTSize,')
    lines.append(f'        {ns}::{prefix}mParam, "{prefix}");')

    lines.append('')
    lines.append('    return out;')
    lines.append('}')
    lines.append('')
    lines.append('} // namespace neospice')

    return '\n'.join(lines) + '\n'
