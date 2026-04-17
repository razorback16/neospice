"""Roundtrip validation for BSIM4v7 migration.

Generates BSIM4v7 C++ from ngspice source using the migration tool and
validates the output against the existing hand-ported files.
"""

from __future__ import annotations

from pathlib import Path
import re

import pytest

from ngspice_migrate.descriptor import load_descriptor
from ngspice_migrate.transformer import Transformer


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).parent.parent.parent  # /home/subhagato/Codes/spice-cpp
DESCRIPTOR = REPO_ROOT / "tools" / "descriptors" / "bsim4v7.yaml"
NGSPICE_DIR = Path("/home/subhagato/Codes/ngspice/src/spicelib/devices/bsim4v7")
EXISTING_DIR = REPO_ROOT / "src" / "devices" / "bsim4v7"


# ---------------------------------------------------------------------------
# TestBSIM4v7Roundtrip
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not NGSPICE_DIR.exists(), reason="ngspice source not available")
class TestBSIM4v7Roundtrip:
    """Roundtrip tests: translate ngspice C -> C++ and compare with hand-port."""

    @pytest.fixture(autouse=True)
    def _setup(self):
        """Load the descriptor and build a Transformer once per test."""
        self.desc = load_descriptor(DESCRIPTOR)
        self.cfg = self.desc.to_transformer_config()
        self.tx = Transformer(self.cfg)

    # -- helpers -----------------------------------------------------------

    def _translate_file(self, filename: str) -> str:
        """Read an ngspice source file and run the full translation pipeline."""
        src = (NGSPICE_DIR / filename).read_text()
        return self.tx.translate(src)

    # -- tests -------------------------------------------------------------

    def test_load_stamp_count_matches(self):
        """mat.add(here-> count in generated output matches hand-ported file."""
        generated = self._translate_file("b4v7ld.c")
        gen_count = len(re.findall(r"mat\.add\(here->", generated))

        existing_text = (EXISTING_DIR / "bsim4v7_load.cpp").read_text()
        existing_count = len(re.findall(r"mat\.add\(here->", existing_text))

        # Both should be in the ~103-104 range.
        assert gen_count == existing_count, (
            f"Stamp count mismatch: generated={gen_count}, "
            f"existing={existing_count}"
        )

    def test_type_substitutions_complete(self):
        """Raw ngspice types CKTcircuit / SMPmatrix must not survive translation.

        GENmodel occurrences inside comments are tolerated.
        """
        raw_types = ["CKTcircuit", "SMPmatrix"]

        for _role, filename in self.desc.source_files.items():
            generated = self._translate_file(filename)

            # Strip comments so we don't false-positive on comment text.
            no_block_comments = re.sub(r"/\*.*?\*/", "", generated, flags=re.DOTALL)
            no_comments = re.sub(r"//[^\n]*", "", no_block_comments)

            for raw_type in raw_types:
                hits = re.findall(rf"\b{raw_type}\b", no_comments)
                assert len(hits) == 0, (
                    f"{raw_type} found {len(hits)} time(s) in translated "
                    f"{filename} (outside comments)"
                )

    def test_no_unrewritten_stamps(self):
        """All ``*(here->BSIM4v7*Ptr) += / -=`` patterns must be rewritten."""
        generated = self._translate_file("b4v7ld.c")

        # Match the raw ngspice stamp pattern that should have been rewritten.
        unrewritten = re.findall(
            r"\*\s*\(\s*here\s*->\s*BSIM4v7[A-Za-z0-9_]*Ptr\s*\)\s*[+\-]=",
            generated,
        )
        assert len(unrewritten) == 0, (
            f"Found {len(unrewritten)} unrewritten stamp(s) in translated "
            f"b4v7ld.c: {unrewritten[:5]}"
        )
