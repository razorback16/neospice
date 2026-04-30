"""PyTorch-inspired circuit construction classes."""
from __future__ import annotations

import re
from typing import Any

_ENG_SUFFIXES = {
    'f': 1e-15, 'p': 1e-12, 'n': 1e-9, 'u': 1e-6,
    'm': 1e-3, 'k': 1e3, 'meg': 1e6, 'g': 1e9, 't': 1e12,
}
_ENG_RE = re.compile(r'^([+-]?\d+\.?\d*(?:[eE][+-]?\d+)?)\s*(f|p|n|u|m|k|meg|g|t)?$', re.I)


def parse_value(v):
    """Parse SPICE engineering notation: '1k' -> 1000.0"""
    if isinstance(v, (int, float)):
        return float(v)
    m = _ENG_RE.match(str(v).strip())
    if not m:
        raise ValueError(f"Cannot parse SPICE value: {v!r}")
    num = float(m.group(1))
    suffix = m.group(2)
    if suffix:
        return num * _ENG_SUFFIXES[suffix.lower()]
    return num
