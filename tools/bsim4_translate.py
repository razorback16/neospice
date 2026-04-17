#!/usr/bin/env python3
"""Mechanical translator: UCB BSIM4 C source -> neospice C++ port.

Usage
-----
    bsim4_translate.py INPUT.c OUTPUT.cpp [--define NAME ...]

Rule reference
--------------
Rules and the validation procedure are documented in
docs/superpowers/plans/2026-04-16-milestone4-bsim4-ucb-z-port-phase1b.md
(Task 6 section).

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

Accepted diff classes (when validating against Phase-1a hand-translated
bsim4v7_temp.cpp / bsim4v7_setup.cpp)
----------------------------------------------------------------------
- Header/banner placement: Phase-1a inserts a translation note *inside*
  the original UCB comment block, then a different #include list, with
  no ``using namespace Shim;`` line. The translator emits the banner
  untouched plus the standard neospice block listed in rule I. This
  yields whole-block diffs in the first ~30 lines.
- ``#undef`` trailer macros: Phase-1a appends ``#undef`` blocks after
  the closing namespace brace; the translator does not emit them.
- ``return(OK)``  -> Phase-1a kept ``return Shim::OK;`` or
  ``return(Shim::OK);``. The translator produces ``return 0;`` per rule D.
- ``TSTALLOC`` macro: Phase-1a redefined TSTALLOC to a simpler macro
  with no error-check branch. The translator inserts a TODO comment
  and leaves the macro call in place.
- Hand fixes: Phase-1a tweaked a few ``namarray`` calls to pass elements
  instead of the array (``namarray[0], namarray[1]``), and inserted
  comments/blank lines. These are tolerated as whitespace/comment diffs.
- fprintf / printf: Phase-1a leaves them alone; translator does too.
- ``#ifdef`` / ``#ifndef`` branches other than USE_OMP remain verbatim,
  because the wrapped file ``#define``s PREDICTOR at the top (per the
  ``--define`` flag) so conditional compilation resolves correctly.

Unacceptable diffs (indicates a bug in the translator)
------------------------------------------------------
- Any ``*(here->...Ptr) {+=,-=} <expr>;`` not rewritten to ``mat.add``.
- Missed / over-applied type substitution.
- USE_OMP block not stripped to the #else branch.
- Error-code / IFerror call not rewritten.

Python stdlib only. No third-party regex/libclang.
"""

from __future__ import annotations

import argparse
import re
import sys
from typing import Callable, List, Tuple


# ---------------------------------------------------------------------------
# Rule A: preprocessor handling (USE_OMP stripping)
# ---------------------------------------------------------------------------

def strip_omp_blocks(src: str) -> str:
    """Remove USE_OMP #ifdef branches; keep the #else branch verbatim.

    Uses a stack so nesting of other ``#ifdef`` blocks inside a USE_OMP
    block is tolerated. If a USE_OMP block has no #else, the whole block
    is deleted (the ``#endif`` disappears with it).
    """

    lines = src.split("\n")
    out: List[str] = []

    # Each stack entry is a dict with:
    #   kind: "omp-if" | "omp-else" | "other"
    #   emit: bool  (are we currently emitting lines inside this block?)
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

        # Match #ifdef/#ifndef/#if first
        m_if = re.match(r"#\s*(ifdef|ifndef|if)\b(.*)$", stripped)
        m_else = re.match(r"#\s*else\b", stripped)
        m_elif = re.match(r"#\s*elif\b", stripped)
        m_endif = re.match(r"#\s*endif\b", stripped)

        if m_if:
            kind = m_if.group(1)
            tail = m_if.group(2)
            # USE_OMP form: #ifdef USE_OMP (only the USE_OMP symbol)
            is_omp = (
                kind in ("ifdef", "ifndef")
                and re.match(r"\s*USE_OMP\b", tail) is not None
            )
            if is_omp and kind == "ifdef":
                # Enter OMP #ifdef branch. Do NOT emit the #ifdef line.
                stack.append({"kind": "omp-if", "emit": False})
                i += 1
                continue
            else:
                # Not a USE_OMP ifdef: keep verbatim, push as "other".
                if emitting():
                    out.append(line)
                stack.append({"kind": "other", "emit": True})
                i += 1
                continue

        if m_else:
            if stack and stack[-1]["kind"] in ("omp-if", "omp-else"):
                # Flip: was skipping OMP branch, now emit #else branch verbatim.
                # Swap frame to omp-else with emit=True; do NOT emit the #else
                # line itself (the #endif is also dropped).
                stack[-1] = {"kind": "omp-else", "emit": True}
                i += 1
                continue
            # Regular #else: emit it if we're emitting.
            if emitting():
                out.append(line)
            i += 1
            continue

        if m_elif:
            if stack and stack[-1]["kind"] in ("omp-if", "omp-else"):
                # #elif inside OMP block - treat like #else (keep that branch,
                # drop the directive line).
                stack[-1] = {"kind": "omp-else", "emit": True}
                i += 1
                continue
            if emitting():
                out.append(line)
            i += 1
            continue

        if m_endif:
            if stack and stack[-1]["kind"] in ("omp-if", "omp-else"):
                # Close OMP frame; do NOT emit the #endif line.
                stack.pop()
                i += 1
                continue
            # Regular #endif.
            if stack:
                stack.pop()
            if emitting() or not stack:
                # If stack is empty we're back at top level -> emit.
                out.append(line)
            i += 1
            continue

        # Non-directive line.
        if emitting():
            out.append(line)
        i += 1

    return "\n".join(out)


# ---------------------------------------------------------------------------
# Rule I (part 1): split banner from body
# ---------------------------------------------------------------------------

def split_banner(src: str) -> Tuple[str, str]:
    """Return (banner, rest). Banner is every line up to (but not including)
    the first ``#include`` directive. If no ``#include`` is found, return
    the whole source as banner and empty body.
    """
    lines = src.split("\n")
    for idx, ln in enumerate(lines):
        if re.match(r"\s*#\s*include\b", ln):
            return "\n".join(lines[:idx]), "\n".join(lines[idx:])
    return src, ""


# ---------------------------------------------------------------------------
# Literal / comment protection
# ---------------------------------------------------------------------------

_SENTINEL_FMT = "\x01SENT_{kind}_{idx}\x01"


def protect_literals(src: str) -> Tuple[str, Callable[[str], str]]:
    """Replace every string literal and every line/block comment with an
    opaque sentinel. Returns the protected text and a restore function.
    """
    store: List[Tuple[str, str]] = []  # (kind, text)

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
        # Line comment (legal in C99 and the .c files do use it)
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

        # Loop until no sentinels remain (substitutions shouldn't produce new ones
        # but the loop is cheap and defensive).
        while True:
            new = sentinel_re.sub(sub, text)
            if new == text:
                return new
            text = new

    return protected, restore


# ---------------------------------------------------------------------------
# Rules B, C, D, E, F: token substitutions
# ---------------------------------------------------------------------------

# Order matters: apply type renames before casts-drop, and before error codes.
# Each entry is (pattern, replacement). Patterns use \b word boundaries.
TOKEN_SUBS: List[Tuple[re.Pattern, str]] = [
    # Rule B: struct-tag forms first (to avoid renaming the tag *and* the alias
    # in one pass). Apply the "struct sBSIM4xxx" form first, since after
    # BSIM4instance/BSIM4model renaming the old struct-tag would still remain.
    (re.compile(r"\bstruct\s+sBSIM4instance\b"), "struct BSIM4v7Instance"),
    (re.compile(r"\bstruct\s+sBSIM4model\b"), "struct BSIM4v7Model"),
    # Rule B: type aliases.
    (re.compile(r"\bBSIM4instance\b"), "BSIM4v7Instance"),
    (re.compile(r"\bBSIM4model\b"), "BSIM4v7Model"),
    (re.compile(r"\bGENmodel\b"), "BSIM4v7Model"),
    (re.compile(r"\bGENinstance\b"), "BSIM4v7Instance"),
    (re.compile(r"\bCKTcircuit\b"), "Shim::Ckt"),
    (re.compile(r"\bIFvalue\b"), "Shim::IfValue"),
    (re.compile(r"\bIFuid\b"), "const char *"),
    (re.compile(r"\bSMPmatrix\b"), "Shim::Matrix"),

    # Rule C: drop redundant casts (runs AFTER rule B so the renamed type
    # appears in the pattern).
    (re.compile(r"\(\s*BSIM4v7Model\s*\*\s*\)\s*inModel\b"), "inModel"),
    (re.compile(r"\(\s*BSIM4v7Instance\s*\*\s*\)\s*inInst\b"), "inInst"),

    # Rule D: error-code returns.
    (re.compile(r"\breturn\s*\(\s*OK\s*\)\s*;"), "return 0;"),
    (re.compile(r"\breturn\s+OK\s*;"), "return 0;"),
    (re.compile(r"\breturn\s*\(\s*E_BADPARM\s*\)\s*;"), "return Shim::E_BADPARM;"),
    (re.compile(r"\breturn\s*\(\s*E_PARMRANGE\s*\)\s*;"), "return Shim::E_PARMRANGE;"),
    (re.compile(r"\breturn\s*\(\s*E_NOMEM\s*\)\s*;"), "return Shim::E_NOMEM;"),

    # Rule F: tmalloc / FREE.
    # tmalloc(sizeof(double) * n)  ->  Shim::tmalloc<double>(n)
    (re.compile(
        r"\btmalloc\s*\(\s*sizeof\s*\(\s*double\s*\)\s*\*\s*([^)]+?)\s*\)"
    ), r"Shim::tmalloc<double>(\1)"),
    (re.compile(r"\bFREE\s*\("), "Shim::FREE("),
]


def _rewrite_iferror(src: str) -> str:
    """Rule E: ``(*(SPfrontEnd->IFerror))(LEVEL, fmt, ...)`` ->
    ``Shim::report_error(Shim::LEVEL, fmt, ...)``.

    Also handles the form ``SPfrontEnd->IFerror(LEVEL, fmt, ...)`` in case
    any call site omits the deref parens.
    """
    # Form A: (*(SPfrontEnd->IFerror))(LEVEL, ...
    patt_a = re.compile(
        r"\(\s*\*\s*\(\s*SPfrontEnd\s*->\s*IFerror\s*\)\s*\)\s*"
    )
    src = patt_a.sub("Shim::report_error", src)
    # Form B: SPfrontEnd->IFerror(
    patt_b = re.compile(r"\bSPfrontEnd\s*->\s*IFerror\b")
    src = patt_b.sub("Shim::report_error", src)
    # Normalise the level arguments that sit inside the just-rewritten call.
    # ERR_FATAL / ERR_WARNING at function-arg position -> Shim::ERR_FATAL etc.
    # We do this broadly via whole-word match; the Shim namespace provides the
    # constants, and the file has ``using namespace Shim;`` at the top per
    # rule I, but be explicit for safety.
    src = re.sub(r"\bERR_WARNING\b", "Shim::ERR_WARNING", src)
    src = re.sub(r"\bERR_FATAL\b", "Shim::ERR_FATAL", src)
    return src


def apply_token_subs(src: str) -> str:
    for patt, repl in TOKEN_SUBS:
        src = patt.sub(repl, src)
    src = _rewrite_iferror(src)
    return src


# ---------------------------------------------------------------------------
# Rule G: matrix stamping rewrites
# ---------------------------------------------------------------------------

# Matches the start of a stamp line:
#   (*(here->BSIM4xxxPtr) += <expr>
# up to the semicolon (expression may span multiple lines). There is an outer
# pair of parens wrapping the whole statement in the UCB source ("(*(p) += e)")
# or none. We handle both.
_STAMP_START = re.compile(
    r"""
    (?P<indent>[ \t]*)             # leading whitespace
    (?P<outer_open>\(?)            # optional outer (
    \*\s*\(\s*here\s*->\s*
    (?P<field>BSIM4[A-Za-z0-9_]+Ptr)
    \s*\)\s*
    (?P<op>\+=|-=|=)               # compound assign or plain =
    \s*
    """,
    re.VERBOSE,
)


def rewrite_matrix_stamps(src: str) -> str:
    """Rule G: rewrite every ``*(here->BSIM4xxxPtr) += expr;`` to
    ``mat.add(here->BSIM4xxxPtr, expr);`` (negating ``expr`` for ``-=``).

    Expressions may span multiple lines; we walk character-by-character
    tracking parenthesis depth until we hit the terminating ``;``.
    """

    out_parts: List[str] = []
    pos = 0
    n = len(src)

    while pos < n:
        m = _STAMP_START.search(src, pos)
        if m is None:
            out_parts.append(src[pos:])
            break

        # Emit everything up to the match.
        out_parts.append(src[pos:m.start()])

        indent = m.group("indent")
        outer_open = m.group("outer_open")
        field = m.group("field")
        op = m.group("op")

        # Parse expression: from m.end() to the matching ';'. Track paren depth.
        expr_start = m.end()
        depth = 0
        j = expr_start
        while j < n:
            c = src[j]
            if c == "(":
                depth += 1
            elif c == ")":
                if depth == 0:
                    # Unbalanced closing paren at depth 0: this is the outer
                    # ``)`` that was opened by outer_open. Stop here.
                    break
                depth -= 1
            elif c == ";" and depth == 0:
                break
            j += 1

        # Bounds check.
        if j >= n:
            # malformed; fall back to no rewrite
            out_parts.append(src[m.start():])
            break

        expr = src[expr_start:j]

        # If there was an outer '(' and we stopped on ')', consume it + then the ';'.
        trailing = ""
        if src[j] == ")":
            # consume closing paren + search forward for ';'
            k = j + 1
            # Allow some whitespace between ) and ;
            while k < n and src[k] in " \t":
                k += 1
            if k < n and src[k] == ";":
                j = k  # j points to ;
            else:
                # Malformed; bail out.
                out_parts.append(src[m.start():])
                break

        # Clean up the expression: strip trailing whitespace (keep inner newlines/
        # indentation to preserve the multi-line flavour). We do not collapse
        # internal whitespace.
        expr_clean = expr.rstrip()

        # Build the replacement.
        if op == "=":
            # Assignment form — not expected in b4ld.c; emit a TODO comment
            # and keep the original line untouched.
            out_parts.append(
                indent + "/* TODO(bsim4_translate): assign-form stamp, left as-is */\n"
            )
            # emit original span
            out_parts.append(src[m.start():j + 1])
        else:
            if op == "+=":
                rewritten_expr = expr_clean
            else:  # op == "-="
                rewritten_expr = "-(" + expr_clean + ")"
            out_parts.append(
                f"{indent}mat.add(here->{field}, {rewritten_expr});"
            )

        pos = j + 1  # advance past ';'

    return "".join(out_parts)


# ---------------------------------------------------------------------------
# Rule H: K&R signature rewrite
# ---------------------------------------------------------------------------

# Pattern: identifier( arg1, arg2, ... )\n<typedecls>\n{
# The function name is on its own or the previous token. After the ')' there
# is a newline, then a series of parameter type declarations (one per param,
# each ending with ';'), then '{'.

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


def rewrite_knr_signatures(src: str) -> str:
    """Rule H: ``Foo(a, b)\\n int a; int b;\\n {`` -> ``Foo(int a, int b) {``.

    We parse the declaration block to build a {name -> typed-param} map,
    then re-emit the signature with typed parameters inline.
    """

    def rewrite(m: re.Match) -> str:
        sig = m.group("sig")
        decls = m.group("decls")

        # Parse the arg list.
        name_m = re.match(r"([A-Za-z_][A-Za-z_0-9]*)\s*\((.*)\)", sig, re.DOTALL)
        if not name_m:
            return m.group(0)
        fn_name = name_m.group(1)
        args_str = name_m.group(2).strip()
        if not args_str or args_str == "void":
            # Nothing to rewrite; but still strip the decl block (there isn't one).
            return m.group(0)
        # Split by commas at depth 0 (K&R args are just identifiers, no commas
        # inside, but be safe).
        args = [a.strip() for a in args_str.split(",") if a.strip()]

        # Parse each declaration line.
        # Each decl is roughly:  [register] <type...> <name1>[, <name2> ...] ;
        name_to_type: dict = {}
        for line in decls.split(";"):
            line = line.strip()
            if not line:
                continue
            # Strip trailing semicolon remnants.
            toks = line.split()
            if len(toks) < 2:
                continue
            # Drop leading "register".
            if toks[0] == "register":
                toks = toks[1:]
            if not toks:
                continue
            # Names are the last comma-separated tokens. The type is everything
            # before. But names may be prefixed with '*' (pointer) attached to
            # the last type token. Rejoin and split on commas to get name list,
            # with the type = line minus the name list.
            # Re-parse: find last "," at depth 0 in the declarators, or use the
            # simple approach: split on whitespace, then peel off names from end.
            # Actually safer: split declarators on commas.
            # Example: "double Nvtm, Ijth, Isb, XExpBV" -> type="double", names=["Nvtm", ...]
            # Example: "double *Vjm" -> type="double", names=["*Vjm"]
            # Example: "register SMPmatrix *matrix" (after token sub: "Shim::Matrix *matrix")
            #   -> type = "Shim::Matrix", names = ["*matrix"]
            full = " ".join(toks)
            # Split declarators on commas.
            decl_list = [d.strip() for d in full.split(",")]
            # The first declarator has the type; subsequent only names.
            first = decl_list[0]
            # The first declarator: type ... name. Peel off last identifier
            # (possibly preceded by '*').
            fm = re.match(r"^(.*?)([ \t*]+)([A-Za-z_][A-Za-z_0-9]*)$", first)
            if not fm:
                continue
            type_core = fm.group(1).strip()
            stars_in_between = fm.group(2)
            first_name = fm.group(3)
            stars = "*" * stars_in_between.count("*")
            # Build typed params.
            def typed(n: str) -> str:
                # n may be '*name' (extra indirection) or 'name'.
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
                # Fallback: keep as-is, probably already typed (ISO C form).
                typed_args.append(a)

        return f"{m.group('pre')}{fn_name}({', '.join(typed_args)}) {{"

    return _KNR_CAND.sub(rewrite, src)


# ---------------------------------------------------------------------------
# Rule I: final wrap
# ---------------------------------------------------------------------------

def wrap(src: str, banner: str, defines: List[str]) -> str:
    header_prefix = ""
    for d in defines:
        header_prefix += f"#define {d}\n"

    translated_header = (
        "\n"
        "// Translated to C++ for neospice on 2026-04-16 by tools/bsim4_translate.py.\n"
        "// See third_party/bsim4_4.7.0/B4TERMS_OF_USE.\n"
        "\n"
        "#include \"devices/bsim4v7/bsim4v7_def.hpp\"\n"
        "#include \"devices/bsim4v7/bsim4v7_shim.hpp\"\n"
        "#include <cmath>\n"
        "#include <cstdio>\n"
        "\n"
        "namespace neospice::bsim4v7 {\n"
        "\n"
        "using namespace Shim;\n"
    )
    footer = "\n} // namespace neospice::bsim4v7\n"

    # Banner: keep trailing newline for separation.
    banner_out = banner.rstrip() + "\n"
    body = src
    if not body.startswith("\n"):
        body = "\n" + body
    if not body.endswith("\n"):
        body = body + "\n"

    return header_prefix + banner_out + translated_header + body + footer


# ---------------------------------------------------------------------------
# Post-protection cleanup: annotate TSTALLOC calls
# ---------------------------------------------------------------------------

def annotate_tstalloc(src: str) -> str:
    """Add a TODO comment above the first TSTALLOC call, matching the
    expectation in the task plan (TSTALLOC hand-rewrite is an accepted diff).
    """
    m = re.search(r"(^|\n)([ \t]*)TSTALLOC\s*\(", src)
    if not m:
        return src
    indent = m.group(2)
    todo = f"{m.group(1)}{indent}/* TODO(bsim4_translate): TSTALLOC macro kept as-is; the Phase-1a setup.cpp redefines it. */\n"
    return src[:m.start()] + todo + src[m.start():]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def translate(source_text: str, defines: List[str]) -> str:
    # 1. Strip USE_OMP blocks before anything else.
    src = strip_omp_blocks(source_text)

    # 2. Split off banner.
    banner, rest = split_banner(src)

    # 3. Protect literals + comments.
    protected, restore = protect_literals(rest)

    # 4. Token substitutions (rules B/C/D/E/F).
    protected = apply_token_subs(protected)

    # 5. Matrix stamps (rule G).
    protected = rewrite_matrix_stamps(protected)

    # 6. K&R signatures (rule H).
    protected = rewrite_knr_signatures(protected)

    # 7. Restore literals + comments.
    body = restore(protected)

    # 8. Annotate TSTALLOC (if present) and drop the `#include` lines that
    #    rule A1/A2 demand we remove.
    body = annotate_tstalloc(body)
    # Rule A1+A2: delete ngspice and bsim4def includes from body.
    body = re.sub(
        r'^[ \t]*#\s*include\s+"ngspice/[^"]+"\s*\n',
        "",
        body,
        flags=re.MULTILINE,
    )
    body = re.sub(
        r'^[ \t]*#\s*include\s+"bsim4def\.h"\s*\n',
        "",
        body,
        flags=re.MULTILINE,
    )
    # Also delete the common UCB includes that have no ngspice/ prefix.
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
        "ngspice/ngspice.h",
        "ngspice/cktdefs.h",
        "ngspice/devdefs.h",
        "ngspice/smpdefs.h",
        "ngspice/sperror.h",
        "ngspice/suffix.h",
        "ngspice/trandefs.h",
    ]
    for h in _ucb_headers:
        body = re.sub(
            r'^[ \t]*#\s*include\s+"' + re.escape(h) + r'"\s*\n',
            "",
            body,
            flags=re.MULTILINE,
        )
    # Also strip the <stdio.h> / <math.h> pairs - replaced by cmath/cstdio.
    body = re.sub(r'^[ \t]*#\s*include\s+<stdio\.h>\s*\n', "", body, flags=re.MULTILINE)
    body = re.sub(r'^[ \t]*#\s*include\s+<math\.h>\s*\n', "", body, flags=re.MULTILINE)

    # 9. Wrap.
    return wrap(body, banner, defines)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--define", action="append", default=[],
                   help="Preprocessor symbol to #define at file top.")
    args = p.parse_args()

    with open(args.input, "r", encoding="utf-8") as f:
        raw = f.read()

    out = translate(raw, args.define)

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(out)

    return 0


if __name__ == "__main__":
    sys.exit(main())
