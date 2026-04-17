"""CLI orchestrator for ngspice model migration.

Usage::

    python -m ngspice_migrate DESCRIPTOR NGSPICE_DIR OUTPUT_DIR [--dry-run]

Reads a YAML descriptor, translates the C source files through the 8-pass
pipeline, generates supporting headers/shims/adapters, and writes everything
to the output directory.
"""

from __future__ import annotations

import argparse
import glob
import sys
from pathlib import Path

from .descriptor import load_descriptor
from .transformer import Transformer
from .gen_def import generate_def_hpp
from .gen_shim import generate_shim_hpp, generate_shim_cpp
from .gen_adapter import generate_adapter_hpp, generate_adapter_cpp
from .gen_cmake import generate_cmake
from .validation import validate_migration


def _write_file(path: Path, content: str, *, dry_run: bool) -> None:
    """Write *content* to *path*, or print what would be written in dry-run mode."""
    if dry_run:
        print(f"  [dry-run] would write {path}  ({len(content)} bytes)")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        print(f"  wrote {path}  ({len(content)} bytes)")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="ngspice_migrate",
        description="Migrate an ngspice device model to neospice C++.",
    )
    parser.add_argument("descriptor", type=Path, help="Path to the YAML descriptor file")
    parser.add_argument("ngspice_dir", type=Path, help="Path to the ngspice device source directory")
    parser.add_argument("output_dir", type=Path, help="Path to the output directory")
    parser.add_argument("--dry-run", action="store_true", help="Print what would be generated without writing files")
    args = parser.parse_args(argv)

    descriptor_path: Path = args.descriptor
    ngspice_dir: Path = args.ngspice_dir
    output_dir: Path = args.output_dir
    dry_run: bool = args.dry_run

    # ------------------------------------------------------------------
    # 1. Load descriptor
    # ------------------------------------------------------------------
    print(f"Loading descriptor: {descriptor_path}")
    desc = load_descriptor(descriptor_path)
    ns = desc.namespace

    # ------------------------------------------------------------------
    # 2. Create transformer
    # ------------------------------------------------------------------
    cfg = desc.to_transformer_config()
    transformer = Transformer(cfg)

    # ------------------------------------------------------------------
    # 3. Translate source files
    # ------------------------------------------------------------------
    generated_sources: list[str] = []
    setup_content: str = ""
    load_content: str = ""

    print(f"\nTranslating source files (namespace={ns}):")
    for role, filename in desc.source_files.items():
        src_path = ngspice_dir / filename
        if not src_path.exists():
            print(f"  WARNING: {src_path} not found, skipping", file=sys.stderr)
            continue

        raw = src_path.read_text(encoding="utf-8", errors="replace")
        translated = transformer.translate(raw)

        out_name = f"{ns}_{role}.cpp"
        out_path = output_dir / out_name
        _write_file(out_path, translated, dry_run=dry_run)
        generated_sources.append(out_name)

        # Keep setup/load content for validation later
        if role == "setup":
            setup_content = translated
        elif role == "load":
            load_content = translated

    # ------------------------------------------------------------------
    # 4. Generate _def.hpp from *def*.h
    # ------------------------------------------------------------------
    print("\nGenerating def header:")
    def_headers = sorted(glob.glob(str(ngspice_dir / "*def*.h")))
    if def_headers:
        def_src_path = Path(def_headers[0])
        def_raw = def_src_path.read_text(encoding="utf-8", errors="replace")
        def_hpp = generate_def_hpp(def_raw, desc)
        def_out = output_dir / f"{ns}_def.hpp"
        _write_file(def_out, def_hpp, dry_run=dry_run)
    else:
        print("  WARNING: no *def*.h found in ngspice_dir", file=sys.stderr)

    # ------------------------------------------------------------------
    # 5. Generate shim hpp/cpp
    # ------------------------------------------------------------------
    print("\nGenerating shim files:")
    shim_hpp = generate_shim_hpp(desc)
    shim_cpp = generate_shim_cpp(desc)
    _write_file(output_dir / f"{ns}_shim.hpp", shim_hpp, dry_run=dry_run)
    _write_file(output_dir / f"{ns}_shim.cpp", shim_cpp, dry_run=dry_run)
    generated_sources.append(f"{ns}_shim.cpp")

    # ------------------------------------------------------------------
    # 6. Generate adapter hpp/cpp
    # ------------------------------------------------------------------
    print("\nGenerating adapter files:")
    adapter_hpp = generate_adapter_hpp(desc)
    adapter_cpp = generate_adapter_cpp(desc, setup_source=setup_content)
    _write_file(output_dir / f"{ns}_device.hpp", adapter_hpp, dry_run=dry_run)
    _write_file(output_dir / f"{ns}_device.cpp", adapter_cpp, dry_run=dry_run)
    generated_sources.append(f"{ns}_device.cpp")

    # ------------------------------------------------------------------
    # 7. Generate CMakeLists.txt
    # ------------------------------------------------------------------
    print("\nGenerating CMakeLists.txt:")
    cmake_txt = generate_cmake(desc, generated_sources)
    _write_file(output_dir / "CMakeLists.txt", cmake_txt, dry_run=dry_run)

    # ------------------------------------------------------------------
    # 8. Validate migration
    # ------------------------------------------------------------------
    print("\nValidation:")
    if setup_content and load_content:
        issues = validate_migration(setup_content, load_content, desc.prefix)
        for issue in issues:
            print(f"  {issue}")
    else:
        print("  SKIP: setup or load file not available for validation")

    print("\nDone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
