"""Generic 8-pass C-to-C++ transformer for ngspice device models.

This module extracts the pipeline from ``bsim4_translate.py`` into a
configurable class so any ngspice model (diode, BJT, MOS1, BSIM3, etc.)
can be migrated with the same passes -- only the prefix and struct names
differ.

Pipeline ordering (deliberately paranoid)
-----------------------------------------
1. strip_omp_blocks()        - preprocessor state machine, line oriented
2. split_banner()            - capture all lines before first #include
3. protect_literals()        - replace "..." and comments with sentinels
4. apply_token_subs()        - rules B, C, D, E, F (whole word only)
5. rewrite_matrix_stamps()   - rule G, line/multi-line
6. rewrite_knr_signatures()  - rule H
7. unprotect()               - restore string + comment sentinels
8. wrap()                    - rule I, assemble final output
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Callable, List, Tuple


# ---------------------------------------------------------------------------
# Configuration dataclass
# ---------------------------------------------------------------------------

@dataclass
class TransformerConfig:
    """All model-specific identifiers needed by the 8-pass pipeline.

    Example for the diode model::

        TransformerConfig(
            instance_struct="DIOinstance",
            model_struct="DIOmodel",
            instance_tag="sDIOinstance",
            model_tag="sDIOmodel",
            cpp_instance="DIOInstance",
            cpp_model="DIOModel",
            gen_instance="GENinstance",
            gen_model="GENmodel",
            prefix="DIO",
            namespace="dio",
            defines=["PREDICTOR"],
        )
    """

    # Original C struct typedef names (e.g. "DIOinstance", "DIOmodel")
    instance_struct: str
    model_struct: str

    # Original C struct tags (e.g. "sDIOinstance", "sDIOmodel")
    instance_tag: str
    model_tag: str

    # C++ replacement names (e.g. "DIOInstance", "DIOModel")
    cpp_instance: str
    cpp_model: str

    # Generic base struct names (e.g. "GENinstance", "GENmodel")
    gen_instance: str = "GENinstance"
    gen_model: str = "GENmodel"

    # Prefix used in matrix-stamp field names (e.g. "DIO" matches DIOposPosPtr)
    prefix: str = ""

    # C++ namespace component (e.g. "dio" -> neospice::dio)
    namespace: str = ""

    # Preprocessor symbols to ``#define`` at file top
    defines: List[str] = field(default_factory=list)

    # Regex fragment for stamp fields. Auto-derived from *prefix* if empty.
    stamp_field_pattern: str = ""

    def __post_init__(self) -> None:
        if not self.stamp_field_pattern and self.prefix:
            self.stamp_field_pattern = rf"{re.escape(self.prefix)}[A-Za-z0-9_]+Ptr"


# ---------------------------------------------------------------------------
# Sentinel format for literal/comment protection
# ---------------------------------------------------------------------------

_SENTINEL_FMT = "\x01SENT_{kind}_{idx}\x01"


# ---------------------------------------------------------------------------
# Transformer
# ---------------------------------------------------------------------------

class Transformer:
    """Configurable 8-pass C-to-C++ transformer for ngspice device models."""

    def __init__(self, cfg: TransformerConfig) -> None:
        self.cfg = cfg
        self._token_subs = self._build_token_subs()
        self._stamp_start_re = self._build_stamp_regex()

    # -- helpers for building config-dependent regex -------------------------

    def _build_token_subs(self) -> List[Tuple[re.Pattern, str]]:
        """Build the ordered list of (pattern, replacement) pairs from config.

        Order mirrors bsim4_translate.py: struct-tag forms first, then type
        aliases, then cast drops, error-code returns, tmalloc/FREE.
        """
        cfg = self.cfg
        subs: List[Tuple[re.Pattern, str]] = []

        # Rule B: struct-tag forms first.
        subs.append((
            re.compile(rf"\bstruct\s+{re.escape(cfg.instance_tag)}\b"),
            f"struct {cfg.cpp_instance}",
        ))
        subs.append((
            re.compile(rf"\bstruct\s+{re.escape(cfg.model_tag)}\b"),
            f"struct {cfg.cpp_model}",
        ))

        # Rule B: type aliases.
        subs.append((re.compile(rf"\b{re.escape(cfg.instance_struct)}\b"), cfg.cpp_instance))
        subs.append((re.compile(rf"\b{re.escape(cfg.model_struct)}\b"), cfg.cpp_model))
        subs.append((re.compile(rf"\b{re.escape(cfg.gen_model)}\b"), cfg.cpp_model))
        subs.append((re.compile(rf"\b{re.escape(cfg.gen_instance)}\b"), cfg.cpp_instance))
        subs.append((re.compile(r"\bCKTcircuit\b"), "Shim::Ckt"))
        subs.append((re.compile(r"\bIFvalue\b"), "Shim::IfValue"))
        subs.append((re.compile(r"\bIFparm\b"), "Shim::IfParm"))
        subs.append((re.compile(r"\bIFuid\b"), "const char *"))
        subs.append((re.compile(r"\bSMPmatrix\b"), "Shim::Matrix"))

        # Rule C: drop redundant casts (after rule B so renamed type appears).
        subs.append((
            re.compile(rf"\(\s*{re.escape(cfg.cpp_model)}\s*\*\s*\)\s*inModel\b"),
            "inModel",
        ))
        subs.append((
            re.compile(rf"\(\s*{re.escape(cfg.cpp_instance)}\s*\*\s*\)\s*inInst\b"),
            "inInst",
        ))

        # Rule D: error-code returns.
        subs.append((re.compile(r"\breturn\s*\(\s*OK\s*\)\s*;"), "return 0;"))
        subs.append((re.compile(r"\breturn\s+OK\s*;"), "return 0;"))
        subs.append((re.compile(r"\breturn\s*\(\s*E_BADPARM\s*\)\s*;"), "return Shim::E_BADPARM;"))
        subs.append((re.compile(r"\breturn\s*\(\s*E_PARMRANGE\s*\)\s*;"), "return Shim::E_PARMRANGE;"))
        subs.append((re.compile(r"\breturn\s*\(\s*E_NOMEM\s*\)\s*;"), "return Shim::E_NOMEM;"))

        # Rule F: tmalloc / FREE.
        subs.append((
            re.compile(r"\btmalloc\s*\(\s*sizeof\s*\(\s*double\s*\)\s*\*\s*([^)]+?)\s*\)"),
            r"Shim::tmalloc<double>(\1)",
        ))
        subs.append((re.compile(r"\bFREE\s*\("), "Shim::FREE("))

        # Rule G2: SMPmakeElt(matrix, ...) -> matrix->make_elt(...)
        subs.append((
            re.compile(r"\bSMPmakeElt\s*\(\s*matrix\s*,"),
            "matrix->make_elt(",
        ))

        # Rule G3: ngspice infrastructure functions -> Shim:: prefixed
        subs.append((re.compile(r"\bCKTdltNNum\b"), "Shim::CKTdltNNum"))
        subs.append((re.compile(r"\bCKTmkVolt\b"), "Shim::CKTmkVolt"))
        subs.append((re.compile(r"\bCKTinst2Node\b"), "Shim::CKTinst2Node"))
        subs.append((re.compile(r"\bCKTnode\b"), "Shim::CKTnode"))

        # Rule G4: ngspice copy() string duplication -> strdup
        subs.append((re.compile(r"\bcopy\s*\("), "strdup("))

        return subs

    def _build_stamp_regex(self) -> re.Pattern:
        """Build the regex that matches the start of a matrix-stamp line."""
        fp = self.cfg.stamp_field_pattern
        if not fp:
            # Fallback: match any field ending in Ptr.
            fp = r"[A-Za-z0-9_]+Ptr"
        return re.compile(
            rf"""
            (?P<indent>[ \t]*)             # leading whitespace
            (?P<outer_open>\(?)            # optional outer (
            \*\s*\(\s*here\s*->\s*
            (?P<field>{fp})
            \s*\)\s*
            (?P<op>\+=|-=|=)               # compound assign or plain =
            \s*
            """,
            re.VERBOSE,
        )

    # -----------------------------------------------------------------------
    # Pass 1: strip USE_OMP blocks
    # -----------------------------------------------------------------------

    @staticmethod
    def strip_omp_blocks(src: str) -> str:
        """Remove USE_OMP ``#ifdef`` branches; keep the ``#else`` branch verbatim.

        Uses a stack so nesting of other ``#ifdef`` blocks inside a USE_OMP
        block is tolerated.  If a USE_OMP block has no ``#else``, the whole
        block is deleted (the ``#endif`` disappears with it).
        """
        lines = src.split("\n")
        out: List[str] = []

        stack: List[dict] = []

        def emitting() -> bool:
            for frame in stack:
                if not frame["emit"]:
                    return False
            return True

        i = 0
        while i < len(lines):
            line = lines[i]
            stripped = line.lstrip()

            m_if = re.match(r"#\s*(ifdef|ifndef|if)\b(.*)$", stripped)
            m_else = re.match(r"#\s*else\b", stripped)
            m_elif = re.match(r"#\s*elif\b", stripped)
            m_endif = re.match(r"#\s*endif\b", stripped)

            if m_if:
                kind = m_if.group(1)
                tail = m_if.group(2)
                is_omp = (
                    kind in ("ifdef", "ifndef")
                    and re.match(r"\s*USE_OMP\b", tail) is not None
                )
                if is_omp and kind == "ifdef":
                    stack.append({"kind": "omp-if", "emit": False})
                    i += 1
                    continue
                else:
                    if emitting():
                        out.append(line)
                    stack.append({"kind": "other", "emit": True})
                    i += 1
                    continue

            if m_else:
                if stack and stack[-1]["kind"] in ("omp-if", "omp-else"):
                    stack[-1] = {"kind": "omp-else", "emit": True}
                    i += 1
                    continue
                if emitting():
                    out.append(line)
                i += 1
                continue

            if m_elif:
                if stack and stack[-1]["kind"] in ("omp-if", "omp-else"):
                    stack[-1] = {"kind": "omp-else", "emit": True}
                    i += 1
                    continue
                if emitting():
                    out.append(line)
                i += 1
                continue

            if m_endif:
                if stack and stack[-1]["kind"] in ("omp-if", "omp-else"):
                    stack.pop()
                    i += 1
                    continue
                if stack:
                    stack.pop()
                if emitting() or not stack:
                    out.append(line)
                i += 1
                continue

            if emitting():
                out.append(line)
            i += 1

        return "\n".join(out)

    # -----------------------------------------------------------------------
    # Pass 2: split banner from body
    # -----------------------------------------------------------------------

    @staticmethod
    def split_banner(src: str) -> Tuple[str, str]:
        """Return ``(banner, rest)``.

        Banner is every line up to (but not including) the first ``#include``
        directive.  If no ``#include`` is found, return the whole source as
        banner and an empty body.
        """
        lines = src.split("\n")
        for idx, ln in enumerate(lines):
            if re.match(r"\s*#\s*include\b", ln):
                return "\n".join(lines[:idx]), "\n".join(lines[idx:])
        return src, ""

    # -----------------------------------------------------------------------
    # Pass 3: protect literals / comments
    # -----------------------------------------------------------------------

    @staticmethod
    def protect_literals(src: str) -> Tuple[str, Callable[[str], str]]:
        """Replace every string literal and every line/block comment with an
        opaque sentinel.  Returns the protected text and a restore function.
        """
        store: List[Tuple[str, str]] = []

        def store_match(kind: str, text: str) -> str:
            idx = len(store)
            store.append((kind, text))
            return _SENTINEL_FMT.format(kind=kind, idx=idx)

        i = 0
        n = len(src)
        out_parts: List[str] = []
        while i < n:
            ch = src[i]
            # Block comment
            if ch == "/" and i + 1 < n and src[i + 1] == "*":
                j = src.find("*/", i + 2)
                if j == -1:
                    j = n
                else:
                    j += 2
                out_parts.append(store_match("CMT", src[i:j]))
                i = j
                continue
            # Line comment
            if ch == "/" and i + 1 < n and src[i + 1] == "/":
                j = src.find("\n", i)
                if j == -1:
                    j = n
                out_parts.append(store_match("CMT", src[i:j]))
                i = j
                continue
            # String literal
            if ch == '"':
                j = i + 1
                while j < n:
                    if src[j] == "\\":
                        j += 2
                        continue
                    if src[j] == '"':
                        j += 1
                        break
                    j += 1
                out_parts.append(store_match("STR", src[i:j]))
                i = j
                continue
            # Char literal
            if ch == "'":
                j = i + 1
                while j < n:
                    if src[j] == "\\":
                        j += 2
                        continue
                    if src[j] == "'":
                        j += 1
                        break
                    j += 1
                out_parts.append(store_match("CHR", src[i:j]))
                i = j
                continue
            out_parts.append(ch)
            i += 1

        protected = "".join(out_parts)

        sentinel_re = re.compile(r"\x01SENT_(CMT|STR|CHR)_(\d+)\x01")

        def restore(text: str) -> str:
            def sub(m):
                return store[int(m.group(2))][1]
            while True:
                new = sentinel_re.sub(sub, text)
                if new == text:
                    return new
                text = new

        return protected, restore

    # -----------------------------------------------------------------------
    # Pass 4: token substitutions (rules B, C, D, E, F)
    # -----------------------------------------------------------------------

    def _rewrite_iferror(self, src: str) -> str:
        """Rule E: ``(*(SPfrontEnd->IFerror))(LEVEL, fmt, ...)`` ->
        ``Shim::report_error(Shim::LEVEL, fmt, ...)``.
        """
        patt_a = re.compile(
            r"\(\s*\*\s*\(\s*SPfrontEnd\s*->\s*IFerror\s*\)\s*\)\s*"
        )
        src = patt_a.sub("Shim::report_error", src)
        patt_b = re.compile(r"\bSPfrontEnd\s*->\s*IFerror\b")
        src = patt_b.sub("Shim::report_error", src)
        patt_c = re.compile(r"\bSPfrontEnd\s*->\s*IFerrorf\s*")
        src = patt_c.sub("Shim::report_error", src)
        src = re.sub(r"\bERR_WARNING\b", "Shim::ERR_WARNING", src)
        src = re.sub(r"\bERR_FATAL\b", "Shim::ERR_FATAL", src)
        return src

    @staticmethod
    def rewrite_tstalloc_macro(src: str) -> str:
        """Rewrite the TSTALLOC macro definition to use matrix->make_elt.

        All ngspice models define the same TSTALLOC macro pattern::

            #define TSTALLOC(ptr,first,second) \\
            do { if((here->ptr = SMPmakeElt(matrix,...)) == (double *)NULL) ...

        We replace it with the simplified C++ version::

            #define TSTALLOC(ptr,first,second) \\
            { here->ptr = matrix->make_elt(here->first, here->second); }
        """
        patt = re.compile(
            r'#\s*define\s+TSTALLOC\s*\([^)]*\)\s*\\\n'
            r'(?:.*\\\n)*'
            r'.*',
            re.MULTILINE,
        )
        replacement = (
            "#define TSTALLOC(ptr,first,second) \\\n"
            "{ here->ptr = matrix->make_elt(here->first, here->second); }"
        )
        return patt.sub(replacement, src)

    @staticmethod
    def strip_ngspice_frontend(src: str) -> str:
        """Strip ngspice front-end constructs that have no neospice equivalent.

        Removes:
        - JOB/TSKtask variable declarations
        - Noise-analysis job-list lookup loops (ft_curckt->ci_curTask)
        - int error; / CKTnode *tmp; declarations when unused

        This runs on the RAW source (before protect_literals) so comments and
        string literals are still visible.
        """
        src = re.sub(
            r'^[ \t]*JOB\s+\*\w+\s*;\s*\n', '', src, flags=re.MULTILINE,
        )
        src = re.sub(
            r'/\*[^*]*noise analysis[^*]*\*/\s*\n'
            r'[ \t]*for\s*\([^;]*ft_curckt[^{]*\{[^}]*\{[^}]*\}[^}]*\}\s*\n',
            '',
            src,
            flags=re.DOTALL | re.IGNORECASE,
        )
        return src

    def apply_token_subs(self, src: str) -> str:
        for patt, repl in self._token_subs:
            src = patt.sub(repl, src)
        src = self._rewrite_iferror(src)
        src = self.rewrite_tstalloc_macro(src)
        return src

    # -----------------------------------------------------------------------
    # Pass 5: matrix stamp rewriting (rule G)
    # -----------------------------------------------------------------------

    def rewrite_matrix_stamps(self, src: str) -> str:
        """Rule G: rewrite every ``*(here->PREFIXxxxPtr) += expr;`` to
        ``mat.add(here->PREFIXxxxPtr, expr);`` (negating *expr* for ``-=``).

        Expressions may span multiple lines; we walk character-by-character
        tracking parenthesis depth until we hit the terminating ``;``.
        """
        stamp_re = self._stamp_start_re
        out_parts: List[str] = []
        pos = 0
        n = len(src)

        while pos < n:
            m = stamp_re.search(src, pos)
            if m is None:
                out_parts.append(src[pos:])
                break

            out_parts.append(src[pos:m.start()])

            indent = m.group("indent")
            field = m.group("field")
            op = m.group("op")

            expr_start = m.end()
            depth = 0
            j = expr_start
            while j < n:
                c = src[j]
                if c == "(":
                    depth += 1
                elif c == ")":
                    if depth == 0:
                        break
                    depth -= 1
                elif c == ";" and depth == 0:
                    break
                j += 1

            if j >= n:
                out_parts.append(src[m.start():])
                break

            expr = src[expr_start:j]

            if src[j] == ")":
                k = j + 1
                while k < n and src[k] in " \t":
                    k += 1
                if k < n and src[k] == ";":
                    j = k
                else:
                    out_parts.append(src[m.start():])
                    break

            expr_clean = expr.rstrip()

            if op == "=":
                out_parts.append(
                    indent + "/* TODO(translator): assign-form stamp, left as-is */\n"
                )
                out_parts.append(src[m.start():j + 1])
            else:
                if op == "+=":
                    rewritten_expr = expr_clean
                else:  # op == "-="
                    rewritten_expr = "-(" + expr_clean + ")"
                out_parts.append(
                    f"{indent}mat.add(here->{field}, {rewritten_expr});"
                )

            pos = j + 1

        return "".join(out_parts)

    # -----------------------------------------------------------------------
    # Pass 6: K&R signature rewrite (rule H)
    # -----------------------------------------------------------------------

    _KNR_CAND = re.compile(
        r"""
        (?P<pre>^|\n)
        (?P<sig>[A-Za-z_][A-Za-z_0-9]*\s*\([^();{}]*\))   # name(arg-list)
        \s*\n
        (?P<decls>
            (?:\s*[A-Za-z_][A-Za-z_0-9 \t*,]*[^;{}\n]*;\s*\n)+
        )
        (?P<brace>\s*\{)
        """,
        re.VERBOSE | re.MULTILINE,
    )

    @staticmethod
    def rewrite_knr_signatures(src: str) -> str:
        """Rule H: ``Foo(a, b)\\nint a; int b;\\n{`` -> ``Foo(int a, int b) {``."""

        def rewrite(m: re.Match) -> str:
            sig = m.group("sig")
            decls = m.group("decls")

            name_m = re.match(r"([A-Za-z_][A-Za-z_0-9]*)\s*\((.*)\)", sig, re.DOTALL)
            if not name_m:
                return m.group(0)
            fn_name = name_m.group(1)
            args_str = name_m.group(2).strip()
            if not args_str or args_str == "void":
                return m.group(0)
            args = [a.strip() for a in args_str.split(",") if a.strip()]

            name_to_type: dict = {}
            for line in decls.split(";"):
                line = line.strip()
                if not line:
                    continue
                toks = line.split()
                if len(toks) < 2:
                    continue
                if toks[0] == "register":
                    toks = toks[1:]
                if not toks:
                    continue
                full = " ".join(toks)
                decl_list = [d.strip() for d in full.split(",")]
                first = decl_list[0]
                fm = re.match(r"^(.*?)([ \t*]+)([A-Za-z_][A-Za-z_0-9]*)$", first)
                if not fm:
                    continue
                type_core = fm.group(1).strip()
                stars_in_between = fm.group(2)
                first_name = fm.group(3)
                stars = "*" * stars_in_between.count("*")

                def typed(n: str) -> str:
                    n = n.strip()
                    extra_stars = ""
                    while n.startswith("*"):
                        extra_stars += "*"
                        n = n[1:].strip()
                    composed_type = (type_core + " " + stars + extra_stars).strip()
                    return f"{composed_type} {n}"

                name_to_type[first_name] = typed(first_name)
                for dd in decl_list[1:]:
                    name_to_type[dd.strip().lstrip("*")] = typed(dd)

            typed_args: List[str] = []
            for a in args:
                if a in name_to_type:
                    typed_args.append(name_to_type[a])
                else:
                    typed_args.append(a)

            return f"{m.group('pre')}{fn_name}({', '.join(typed_args)}) {{"

        return Transformer._KNR_CAND.sub(rewrite, src)

    # -----------------------------------------------------------------------
    # Pass 8: final wrap (rule I)
    # -----------------------------------------------------------------------

    def wrap(self, src: str, banner: str) -> str:
        """Assemble the final output with namespace, includes, and defines."""
        cfg = self.cfg
        header_prefix = ""
        for d in cfg.defines:
            header_prefix += f"#define {d}\n"

        compat_defines = (
            "\n"
            "#ifndef CONSTvt0\n"
            "#define CONSTvt0 0.025852037\n"
            "#endif\n"
            "#ifndef CONSTroot2\n"
            "#define CONSTroot2 1.4142135623730950488\n"
            "#endif\n"
            "#ifndef CONSTCtoK\n"
            "#define CONSTCtoK 273.15\n"
            "#endif\n"
            "#ifndef CHARGE\n"
            "#define CHARGE 1.6021918e-19\n"
            "#endif\n"
            "#ifndef FABS\n"
            "#define FABS(x) std::fabs(x)\n"
            "#endif\n"
            "#ifndef ABS\n"
            "#define ABS(x) std::fabs(x)\n"
            "#endif\n"
            "#ifndef MAX\n"
            "#define MAX(a, b) (((a) > (b)) ? (a) : (b))\n"
            "#endif\n"
            "#ifndef MIN\n"
            "#define MIN(a, b) (((a) < (b)) ? (a) : (b))\n"
            "#endif\n"
            "#ifndef TMALLOC\n"
            "#define TMALLOC(type, num) (new type[num]())\n"
            "#endif\n"
            "#ifndef NG_IGNORE\n"
            "#define NG_IGNORE(x) (void)(x)\n"
            "#endif\n"
            "#ifndef cp_getvar\n"
            "#define cp_getvar(name, type, ptr) 0\n"
            "#endif\n"
            "#ifndef CP_REAL\n"
            "#define CP_REAL 0\n"
            "#endif\n"
            "#ifndef NUMELEMS\n"
            "#define NUMELEMS(ARRAY) (sizeof(ARRAY)/sizeof(*(ARRAY)))\n"
            "#endif\n"
            "#ifndef IOP\n"
            "#define IOP(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}\n"
            "#endif\n"
            "#ifndef IOPU\n"
            "#define IOPU(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}\n"
            "#endif\n"
            "#ifndef IP\n"
            "#define IP(a,b,c,d) {a, b, (Shim::IF_SET|c), d}\n"
            "#endif\n"
            "#ifndef OP\n"
            "#define OP(a,b,c,d) {a, b, (Shim::IF_ASK|c), d}\n"
            "#endif\n"
            "#ifndef OPU\n"
            "#define OPU(a,b,c,d) {a, b, (Shim::IF_ASK|c), d}\n"
            "#endif\n"
            "#ifndef IOPA\n"
            "#define IOPA(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}\n"
            "#endif\n"
            "#ifndef IOPR\n"
            "#define IOPR(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|Shim::IF_REDUNDANT|c), d}\n"
            "#endif\n"
            "#ifndef IOPAU\n"
            "#define IOPAU(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_ASK|c), d}\n"
            "#endif\n"
            "#ifndef IPR\n"
            "#define IPR(a,b,c,d) {a, b, (Shim::IF_SET|Shim::IF_REDUNDANT|c), d}\n"
            "#endif\n"
            "#ifndef OPR\n"
            "#define OPR(a,b,c,d) {a, b, (Shim::IF_ASK|Shim::IF_REDUNDANT|c), d}\n"
            "#endif\n"
        )

        translated_header = (
            "\n"
            "// Translated to C++ for neospice by tools/ngspice_migrate.\n"
            "\n"
            f"#include \"devices/{cfg.namespace}/{cfg.namespace}_def.hpp\"\n"
            f"#include \"devices/{cfg.namespace}/{cfg.namespace}_shim.hpp\"\n"
            "#include <cmath>\n"
            "#include <cstdio>\n"
            "#include <cstring>\n"
            + compat_defines
            + "\n"
            f"namespace neospice::{cfg.namespace} {{\n"
            "\n"
            "using namespace Shim;\n"
        )
        footer = f"\n}} // namespace neospice::{cfg.namespace}\n"

        banner_out = banner.rstrip() + "\n"
        body = src
        if not body.startswith("\n"):
            body = "\n" + body
        if not body.endswith("\n"):
            body = body + "\n"

        return header_prefix + banner_out + translated_header + body + footer

    # -----------------------------------------------------------------------
    # Helpers applied after unprotect
    # -----------------------------------------------------------------------

    @staticmethod
    def _insert_mat_ref(src: str) -> str:
        """Insert ``auto &mat = *ckt->mat;`` in functions that use ``mat.add(``."""
        if "mat.add(" not in src:
            return src
        return re.sub(
            r'(Shim::Ckt\s*\*\s*ckt\s*\)\s*\{)\n',
            r'\1\nauto &mat = *ckt->mat;\n',
            src,
        )

    @staticmethod
    def _annotate_tstalloc(src: str) -> str:
        """Add a TODO comment above the first TSTALLOC call."""
        m = re.search(r"(^|\n)([ \t]*)TSTALLOC\s*\(", src)
        if not m:
            return src
        indent = m.group(2)
        todo = (
            f"{m.group(1)}{indent}"
            "/* TODO(translator): TSTALLOC macro kept as-is; "
            "needs manual rewrite. */\n"
        )
        return src[:m.start()] + todo + src[m.start():]

    @staticmethod
    def _strip_ngspice_includes(src: str) -> str:
        """Remove ``#include "ngspice/..."`` and other common UCB headers."""
        # ngspice/ prefixed includes
        src = re.sub(
            r'^[ \t]*#\s*include\s+"ngspice/[^"]+"\s*\n',
            "",
            src,
            flags=re.MULTILINE,
        )
        # Common UCB headers without prefix
        _ucb_headers = [
            "spice.h",
            "cktdefs.h",
            "smpdefs.h",
            "trandefs.h",
            "devdefs.h",
            "sperror.h",
            "util.h",
            "const.h",
            "suffix.h",
            "jobdefs.h",
            "ftedefs.h",
        ]
        for h in _ucb_headers:
            src = re.sub(
                r'^[ \t]*#\s*include\s+"' + re.escape(h) + r'"\s*\n',
                "",
                src,
                flags=re.MULTILINE,
            )
        # C standard headers replaced by C++ equivalents
        src = re.sub(
            r'^[ \t]*#\s*include\s+<stdio\.h>\s*\n', "", src, flags=re.MULTILINE
        )
        src = re.sub(
            r'^[ \t]*#\s*include\s+<math\.h>\s*\n', "", src, flags=re.MULTILINE
        )
        return src

    # -----------------------------------------------------------------------
    # Full pipeline
    # -----------------------------------------------------------------------

    def translate(self, source_text: str) -> str:
        """Run the complete 8-pass pipeline and return the translated C++."""
        # 1. Strip USE_OMP blocks.
        src = self.strip_omp_blocks(source_text)

        # 1b. Strip ngspice front-end constructs (JOB, ft_curckt, etc.)
        src = self.strip_ngspice_frontend(src)

        # 2. Split off banner.
        banner, rest = self.split_banner(src)

        # 3. Protect literals + comments.
        protected, restore = self.protect_literals(rest)

        # 4. Token substitutions (rules B/C/D/E/F).
        protected = self.apply_token_subs(protected)

        # 5. Matrix stamps (rule G).
        protected = self.rewrite_matrix_stamps(protected)

        # 6. K&R signatures (rule H).
        protected = self.rewrite_knr_signatures(protected)

        # 7. Restore literals + comments.
        body = restore(protected)

        # 8a. Annotate TSTALLOC + strip ngspice includes.
        body = self._annotate_tstalloc(body)
        body = self._strip_ngspice_includes(body)

        # Also strip model-specific def header if present
        if self.cfg.instance_struct:
            # e.g. "bsim4def.h" or "<model>def.h" or "<model>defs.h"
            def_base = self.cfg.instance_struct.lower().replace("instance", "") + "def"
            # Match both "xyzdef.h" and "xyzdefs.h" patterns
            body = re.sub(
                r'^[ \t]*#\s*include\s+"' + re.escape(def_base) + r's?\.h"\s*\n',
                "",
                body,
                flags=re.MULTILINE,
            )

        # 8b. Insert mat reference for functions with matrix stamps.
        body = self._insert_mat_ref(body)

        # 8c. Wrap.
        return self.wrap(body, banner)
