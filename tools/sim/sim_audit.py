#!/usr/bin/env python3
"""Simulation floating-point audit (lane WEAPON-1 review round 2; PLAN rule 3).

Lockstep multiplayer needs bit-identical simulation results on Windows / MSVC and Linux / GCC. engine/cmake/SimFp.cmake carries the compiler contract
and engine/CMakeLists.txt the manifest of simulation sources (OPENBFME_SIM_SOURCES, written to <build>/sim_manifest.json). This tool audits the
GENERATED build, so it must run after configure (CMAKE_EXPORT_COMPILE_COMMANDS is on):

  1. FLAGS   every compile command of the project (core, Lua, tests; not thirdparty) carries the required flags of the manifest, with no conflicting
             override (fast-math, contraction, x87 evaluation, a non-strict /fp mode, link-time optimisation), the LAST of repeated modes winning.
  2. CLASSIFY every translation unit under engine/src (and the generated / staged sources of the build) is either in OPENBFME_SIM_SOURCES or matches
             an entry of the reviewed exclusion list of tools/sim/sim_policy.json ("excluded": glob + reason); anything else is unclassified: failure. A
             manifest source without a compile command, an exclusion that matches nothing, and a source in both lists are errors too.
  3. AST     a type-aware check (libclang through its Python bindings) over every manifest C++ source AND the project headers it includes: floating
             arithmetic (+ - * / and compound assignments with a floating result, also inside macro expansions: the operator comes from the AST node, not
             from source tokens), <cmath> style calls, floating -> integer conversions (explicit or implicit; UB when out of range: use cvttss2si /
             fistp32 / ftol2) are violations outside the registered facade implementations. Integer arithmetic, comparisons, copies and float -> double
             widening are allowed. EVERY libclang error or fatal diagnostic, in any file, fails the audit (a translation unit the check cannot parse is
             not checked). Existing simulation code is PINNED in tools/sim/sim_baseline.json by stable identity (file, enclosing function, kind, normalised
             expression); a new identity fails, a pin that no longer matches also fails (lower it with --update-baseline, which can only remove pins).
  4. LINK    the effective link options (build.ninja LINK_FLAGS, or the Makefile link.txt files) carry no fast-math / contraction / LTO option: linking with
             -ffast-math changes the start-up MXCSR (crtfastmath.o sets FTZ / DAZ).

Run it where the compiler of the build runs (the include paths are probed from the compiler of the compile command), e.g. on the Steam Deck inside the
flatpak SDK that built the project:
    flatpak run --filesystem=home --env=PYTHONPATH=<dir with the 'clang' package> --command=python3 org.freedesktop.Sdk//25.08 tools/sim/sim_audit.py --build <build dir>
libclang: `pip install libclang==18.1.1` (OPENBFME_LIBCLANG_PYTHONPATH adds a directory holding the 'clang' package, OPENBFME_LIBCLANG_LIBRARY a
libclang shared library). Without it the AST part cannot run: the default is an error (--ast=require); --ast=skip prints a loud notice and exits 3.
"""
import argparse
import fnmatch
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENGINE = os.path.join(REPO, "engine")
POLICY_PATH = os.path.join(REPO, "tools", "sim", "sim_policy.json")
BASELINE_PATH = os.path.join(REPO, "tools", "sim", "sim_baseline.json")

# The acceptance stops of this tool (docs/STOPS.md), printed with every run.
STOPS = {
    "S-230": "the AST check needs the libclang Python bindings (not part of the default toolchain: pip install libclang==18.1.1) and runs where the build's "
             "compiler runs; it was exercised on GCC / Linux compile commands only (MSVC options are translated best effort, unverified); implicit "
             "conversions are recognised through libclang's UNEXPOSED_EXPR nodes, template bodies only when no operand is a concrete integer, and the "
             "staged Lua C sources (lvm.c: the VM arithmetic is the PC24 emulation by design) are not scanned",
    "S-231": "existing simulation code of other lanes (PATH-1, HORDE-1, LOGIC-1, the terrain and the shared WWMath headers) still contains bare floating "
             "arithmetic, libm calls and float -> int casts: pinned by stable identity in tools/sim/sim_baseline.json (only removable); the compiler "
             "contract already forbids contraction in them, but not libm or cast differences between platforms",
    "S-234": "src/GodotDevice/GodotGameWorld.cpp is a simulation-INPUT file that lives in the GDExtension (it builds the game-start inputs from the GameInfo, "
             "creates the logic objects and queues the player's rally-point commands); it is audited (listed in the manifest) and its violations are pinned, "
             "but the construction is not extracted into manifested core code here: lanes START-1 (game start from GameInfo) and HUD-1 (player commands "
             "through GameMessage) are rewriting exactly that code and must move it into core",
}

# ---------------------------------------------------------------------------------------------------------------------------------------------
# 1. compile command flags
# ---------------------------------------------------------------------------------------------------------------------------------------------
GNU_FORBIDDEN = re.compile(
    r"^-(ffast-math|Ofast|funsafe-math-optimizations|fassociative-math|freciprocal-math|ffinite-math-only|fno-signed-zeros|fno-trapping-math|"
    r"fno-rounding-math|mfpmath=387|m80387|mno-sse2|mno-sse|fexcess-precision=fast|ffloat-store|ffp-contract=(fast|on)|fsingle-precision-constant)$")
GNU_LTO = re.compile(r"^-(flto(=.*)?|fuse-linker-plugin|fwhole-program)$")
MSVC_FORBIDDEN = re.compile(r"^[-/](fp:fast|fp:precise|fp:contract|arch:IA32|arch:SSE|Qvec-report:0)$", re.I)
MSVC_LTO = re.compile(r"^[-/](GL|LTCG(:.*)?)$", re.I)


def command_args(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"], posix=(os.name != "nt"))


def check_flags(args, manifest):
    """Problems of one compile command's arguments against the manifest's contract (a list of strings)."""
    problems = []
    family = manifest["compiler_family"]
    if family == "msvc":
        norm = [a for a in args]
        for flag in manifest["required_flags"]:
            if not any(a.lower() == flag.lower() or a.lower() == "-" + flag[1:].lower() for a in norm):
                problems.append("missing required flag " + flag)
        modes = [a.lower().lstrip("-/") for a in norm if re.match(r"^[-/]fp:(strict|precise|fast)$", a, re.I)]
        if modes and modes[-1] != "fp:strict":
            problems.append("effective floating point mode is /" + modes[-1] + " (the last /fp: wins), not /fp:strict")
        for a in norm:
            if MSVC_FORBIDDEN.match(a):
                problems.append("conflicting option " + a)
            if MSVC_LTO.match(a):
                problems.append("link-time optimisation option " + a)
        return problems
    for flag in manifest["required_flags"]:
        if flag not in args:
            problems.append("missing required flag " + flag)
    contracts = [a for a in args if a.startswith("-ffp-contract=")]
    if contracts and contracts[-1] != "-ffp-contract=off":
        problems.append("effective contraction mode is " + contracts[-1] + " (the last wins), not -ffp-contract=off")
    for a in args:
        if GNU_FORBIDDEN.match(a):
            problems.append("conflicting option " + a)
        if GNU_LTO.match(a):
            problems.append("link-time optimisation option " + a)
    # a fast-math style option AFTER -fno-fast-math would win: report the order too
    if "-fno-fast-math" in args:
        after = args[len(args) - 1 - args[::-1].index("-fno-fast-math") + 1:]
        for a in after:
            if GNU_FORBIDDEN.match(a):
                problems.append("option " + a + " comes after -fno-fast-math")
    return problems


THIRD_PARTY = ("thirdparty",)


def project_entries(entries, build_dir):
    """The compile commands the contract applies to: everything of this project (src, tests, the staged Lua, project generated sources), not the third-party
    trees: engine/thirdparty and the sources generated for them under <build>/thirdparty (godot-cpp's gen/ bindings)."""
    out = []
    third_src = os.path.join(ENGINE, "thirdparty") + os.sep
    third_gen = os.path.join(os.path.abspath(build_dir), "thirdparty") + os.sep
    for e in entries:
        path = os.path.normpath(os.path.join(e.get("directory", "."), e["file"]))
        if path.startswith(third_src) or path.startswith(third_gen):
            continue
        out.append((path, e))
    return out


def relative_to_engine(path):
    rel = os.path.relpath(path, ENGINE)
    return None if rel.startswith("..") else rel.replace(os.sep, "/")


def audit_flags(entries, manifest, build_dir):
    problems = []
    for path, e in project_entries(entries, build_dir):
        for p in check_flags(command_args(e), manifest):
            problems.append("%s: %s" % (relative_to_engine(path) or path, p))
    return problems


def check_link_flags(args, manifest):
    """Problems of one link command's options: the floating-point options that change the start-up environment (crtfastmath.o sets FTZ / DAZ) and LTO."""
    problems = []
    forbidden, lto = (MSVC_FORBIDDEN, MSVC_LTO) if manifest["compiler_family"] == "msvc" else (GNU_FORBIDDEN, GNU_LTO)
    for a in args:
        if forbidden.match(a):
            problems.append("conflicting link option " + a)
        if lto.match(a):
            problems.append("link-time optimisation link option " + a)
    return problems


def link_tokens(text, build_dir, problems, stack=()):
    """The arguments of a link command fragment, with every response file (@file) expanded, nested ones to any depth. A response file that cannot be read, or
    that includes itself, is a problem: an unexpanded file could hide a forbidden option."""
    out = []
    for tok in shlex.split(text, posix=(os.name != "nt")):
        if tok.startswith("@") and len(tok) > 1:
            path = os.path.normpath(os.path.join(build_dir, tok[1:].strip('"')))
            if path in stack:
                problems.append("response file includes itself: " + path)
                continue
            try:
                with open(path, encoding="utf-8", errors="replace") as f:
                    content = f.read()
            except OSError as e:
                problems.append("unreadable response file %s (%s)" % (path, e.strerror or e))
                continue
            out += link_tokens(content, build_dir, problems, stack + (path,))
            continue
        out.append(tok)
    return out


NINJA_LINK_VARIABLES = ("LINK_FLAGS", "LINK_LIBRARIES", "LINK_PATH")


def audit_link(build_dir, manifest):
    """The complete effective link command of the generated build: LINK_FLAGS, LINK_LIBRARIES and LINK_PATH of every Ninja link statement (a `-ffast-math` given
    through target_link_libraries lands in LINK_LIBRARIES), or the whole link.txt of a Makefile generator, response files expanded."""
    problems, checked = [], 0
    ninja = os.path.join(build_dir, "build.ninja")
    third = os.sep + "thirdparty" + os.sep
    if os.path.exists(ninja):
        target = None
        with open(ninja, encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("build "):
                    target = line[len("build "):].split(":")[0].strip()
                m = re.match(r"^\s+(LINK_FLAGS|LINK_LIBRARIES|LINK_PATH)\s*=\s*(.*)$", line)
                if m:
                    if m.group(1) == "LINK_FLAGS":
                        checked += 1
                    found = []
                    found += check_link_flags(link_tokens(m.group(2), build_dir, found), manifest)
                    for p in found:
                        problems.append("%s: %s (in %s)" % (target, p, m.group(1)))
    else:
        for root, _dirs, files in os.walk(build_dir):
            if "link.txt" in files and third not in root + os.sep:
                checked += 1
                with open(os.path.join(root, "link.txt"), encoding="utf-8", errors="replace") as f:
                    found = []
                    found += check_link_flags(link_tokens(f.read(), build_dir, found), manifest)
                    for p in found:
                        problems.append("%s: %s" % (os.path.relpath(root, build_dir), p))
    return problems, checked


# ---------------------------------------------------------------------------------------------------------------------------------------------
# 2. the classification of every translation unit
# ---------------------------------------------------------------------------------------------------------------------------------------------
def classification_name(path, build_dir):
    """The name a translation unit is classified under: src/... (relative to engine/) for the project's sources, <build>/... for generated and staged
    ones. Tests (engine/tests) and anything else are not part of the product: None."""
    rel = relative_to_engine(path)
    if rel is not None:
        return rel if rel.startswith("src/") else None
    b = os.path.relpath(path, build_dir)
    if not b.startswith(".."):
        return "<build>/" + b.replace(os.sep, "/")
    return None


def audit_manifest(entries, manifest, policy, build_dir):
    problems = []
    built = {}
    for path, e in project_entries(entries, build_dir):
        name = classification_name(path, build_dir)
        if name:
            built[name] = e
    listed = set(manifest["sources"])
    excluded = policy["excluded"]
    for item in excluded:
        if not item.get("reason", "").strip():
            problems.append("exclusion %s has no reason" % item.get("glob"))
    used = set()
    for rel in sorted(listed):
        if rel not in built:
            problems.append("manifest source %s has no compile command (not built, or a stale manifest)" % rel)
    for name in sorted(built):
        hits = [i for i, item in enumerate(excluded) if fnmatch.fnmatch(name, item["glob"])]
        used.update(hits)
        if name in listed:
            if hits:
                problems.append("%s is in OPENBFME_SIM_SOURCES and also matched by the exclusion %s: it is one or the other" % (name, excluded[hits[0]]["glob"]))
        elif not hits:
            problems.append("unclassified source %s: it is neither in OPENBFME_SIM_SOURCES (engine/CMakeLists.txt) nor matched by a reviewed exclusion "
                            "with a reason (tools/sim/sim_policy.json: excluded)" % name)
    on_disk = None
    for i, item in enumerate(excluded):
        if i in used:
            continue
        if on_disk is None:
            on_disk = [os.path.relpath(os.path.join(r, n), ENGINE).replace(os.sep, "/") for r, _d, fs in os.walk(os.path.join(ENGINE, "src")) for n in fs]
        if not any(fnmatch.fnmatch(n, item["glob"]) for n in on_disk) and not item["glob"].startswith("<build>/"):
            problems.append("the exclusion %s matches no source file: remove it" % item["glob"])
        # (a glob that matches files this build does not compile, e.g. the Godot device sources of a core-only build, is fine)
    return problems, built


# ---------------------------------------------------------------------------------------------------------------------------------------------
# 3. the AST check
# ---------------------------------------------------------------------------------------------------------------------------------------------
MATH_NAMES = set("""sqrt sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh exp exp2 expm1 log log2 log10 log1p pow hypot cbrt fmod remainder remquo
floor ceil round trunc rint nearbyint lround llround lrint llrint fabs fmin fmax fdim fma ldexp scalbn frexp modf copysign nextafter erf erfc tgamma lgamma
sqrtf sinf cosf tanf asinf acosf atanf atan2f expf logf powf floorf ceilf roundf truncf fabsf fmodf ldexpf frexpf""".split())
ARITH_OPS = {"+", "-", "*", "/"}
ARITH_ASSIGN_OPS = {"+=", "-=", "*=", "/="}


def import_clang():
    extra = os.environ.get("OPENBFME_LIBCLANG_PYTHONPATH")
    if extra and extra not in sys.path:
        sys.path.insert(0, extra)
    try:
        import clang.cindex as cindex
    except ImportError:
        return None
    lib = os.environ.get("OPENBFME_LIBCLANG_LIBRARY")
    if lib:
        cindex.Config.set_library_file(lib)
    try:
        cindex.Index.create()
    except Exception:  # libclang shared library missing
        return None
    return cindex


def probe_includes(compiler):
    """The compiler's system include directories (so libclang finds the same standard library the build uses)."""
    if os.path.basename(compiler).lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe"):
        dirs = [d for d in os.environ.get("INCLUDE", "").split(os.pathsep) if d]  # vcvars64.bat sets it: run the audit from the same developer prompt
        if not dirs:
            raise RuntimeError("the MSVC include path is unknown: run the audit from a developer prompt (INCLUDE is empty)")
        return dirs
    try:
        out = subprocess.run([compiler, "-E", "-x", "c++", "-v", "-"], input="", capture_output=True, text=True, timeout=60).stderr
    except OSError as ex:
        raise RuntimeError("cannot run the build's compiler %s to probe its include paths: %s" % (compiler, ex))
    dirs, grab = [], False
    for line in out.splitlines():
        if line.startswith("#include <...> search starts here"):
            grab = True
        elif line.startswith("End of search list"):
            break
        elif grab:
            dirs.append(line.strip())
    if not dirs:
        raise RuntimeError("the compiler %s printed no include search list" % compiler)
    return dirs


def msvc_to_clang(args):
    """The cl.exe options that matter for parsing, as clang options (UNVERIFIED on a Windows host: stop S-230)."""
    out = []
    for a in args:
        m = re.match(r"^[-/](I|D)(.+)$", a)
        if m:
            out.append("-" + m.group(1) + m.group(2))
        elif re.match(r"^[-/]std:", a):
            out.append("-std=" + a.split(":", 1)[1])
    return out + ["-fms-compatibility", "-fms-extensions", "-Wno-everything"]


def clang_args(entry, system_dirs):
    args = command_args(entry)
    if os.path.basename(args[0]).lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe"):
        return ["-x", "c++"] + msvc_to_clang(args[1:]) + [x for d in system_dirs for x in ("-isystem", d)]
    out, skip = [], False
    src = os.path.normpath(os.path.join(entry.get("directory", "."), entry["file"]))
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-MD", "-MMD", "-MP") or os.path.normpath(os.path.join(entry.get("directory", "."), a)) == src:
            continue
        out.append(a)
    for d in system_dirs:
        out += ["-isystem", d]
    return ["-x", "c++"] + out


def _scan_worker(job):
    """Parse one translation unit and return (path, violations, errors). Run in a worker process."""
    path, args, policy = job
    cindex = import_clang()
    if cindex is None:
        return path, [], ["libclang unavailable in the worker"], set()
    visited = set()
    violations, errors = scan_tu(cindex, path, args, policy, visited=visited)
    return path, violations, errors, visited


# CXBinaryOperatorKind / CXUnaryOperatorKind of libclang >= 17 (clang-c/Index.h)
BINARY_ARITH = {3: "*", 4: "/", 6: "+", 7: "-"}
BINARY_ARITH_ASSIGN = {23: "*=", 24: "/=", 26: "+=", 27: "-="}
UNARY_INCDEC = {1: "++", 2: "--", 3: "++", 4: "--"}


def operator_kinds(cindex):
    """The operator of a BINARY_OPERATOR / COMPOUND_ASSIGNMENT_OPERATOR / UNARY_OPERATOR node straight from the AST (clang_getCursorBinaryOperatorKind),
    so arithmetic inside macro expansions is identified like any other (the spelling tokens of an expression written by a macro are not its operator)."""
    import ctypes
    lib = cindex.conf.lib
    binary = lib.clang_getCursorBinaryOperatorKind
    binary.argtypes = [cindex.Cursor]
    binary.restype = ctypes.c_int
    unary = lib.clang_getCursorUnaryOperatorKind
    unary.argtypes = [cindex.Cursor]
    unary.restype = ctypes.c_int
    return binary, unary


BINARY_NAMES = {3: "*", 4: "/", 5: "%", 6: "+", 7: "-", 8: "<<", 9: ">>", 10: "<=>", 11: "<", 12: ">", 13: "<=", 14: ">=", 15: "==", 16: "!=", 17: "&",
                18: "^", 19: "|", 20: "&&", 21: "||", 22: "=", 23: "*=", 24: "/=", 25: "%=", 26: "+=", 27: "-=", 28: "<<=", 29: ">>=", 30: "&=", 31: "^=",
                32: "|=", 33: ","}
UNARY_NAMES = {1: "x++", 2: "x--", 3: "++x", 4: "--x", 5: "&", 6: "*", 7: "+", 8: "-", 9: "~", 10: "!", 11: "real", 12: "imag", 13: "ext", 14: "co_await"}


def token_text(c):
    return " ".join(t.spelling for t in c.get_tokens())


def expression_text(cindex, c, binary_kind, unary_kind):
    """The COMPLETE expression of an AST node, built from the AST (operators and operands, macro arguments already substituted), not from source tokens:
    the identity of a violation. Never truncated (the audit prints a shortened form only for display)."""
    CK = cindex.CursorKind

    def ex(n):
        k = n.kind
        ch = list(n.get_children())
        if k in (CK.BINARY_OPERATOR, CK.COMPOUND_ASSIGNMENT_OPERATOR) and len(ch) == 2:
            return "(%s %s %s)" % (ex(ch[0]), BINARY_NAMES.get(binary_kind(n), "op%d" % binary_kind(n)), ex(ch[1]))
        if k == CK.UNARY_OPERATOR and len(ch) == 1:
            return "(%s %s)" % (UNARY_NAMES.get(unary_kind(n), "uop%d" % unary_kind(n)), ex(ch[0]))
        if k in (CK.PAREN_EXPR, CK.UNEXPOSED_EXPR) and len(ch) == 1:
            return ex(ch[0])
        if k == CK.CALL_EXPR:
            args = [ex(a) for a in n.get_arguments()]
            ref = n.referenced
            name = ref.spelling if ref is not None and ref.spelling else (n.spelling or "call")
            return "%s(%s)" % (name, ", ".join(args))
        if k == CK.DECL_REF_EXPR:
            return n.spelling or token_text(n)
        if k == CK.MEMBER_REF_EXPR:
            return "%s.%s" % (ex(ch[0]) if ch else "?", n.spelling)
        if k in (CK.CSTYLE_CAST_EXPR, CK.CXX_STATIC_CAST_EXPR, CK.CXX_FUNCTIONAL_CAST_EXPR, CK.CXX_REINTERPRET_CAST_EXPR, CK.CXX_CONST_CAST_EXPR) and ch:
            return "cast<%s>(%s)" % (n.type.spelling, ex(ch[-1]))
        if k == CK.ARRAY_SUBSCRIPT_EXPR and len(ch) == 2:
            return "%s[%s]" % (ex(ch[0]), ex(ch[1]))
        if k == CK.CONDITIONAL_OPERATOR and len(ch) == 3:
            return "(%s ? %s : %s)" % (ex(ch[0]), ex(ch[1]), ex(ch[2]))
        if k in (CK.INTEGER_LITERAL, CK.FLOATING_LITERAL, CK.CXX_BOOL_LITERAL_EXPR, CK.STRING_LITERAL, CK.CHARACTER_LITERAL):
            return token_text(n) or n.spelling
        if ch:
            return "%s<%s>" % (k.name, ", ".join(ex(x) for x in ch))
        return "%s<%s>" % (k.name, token_text(n))

    return ex(c)


def shortened(text, limit=160):
    """Display form only: identities are never shortened."""
    return text if len(text) <= limit else text[:limit - 3] + "..."


def scan_tu(cindex, path, args, policy, root=None, base=None, visited=None):
    """(violations, errors) of one translation unit. A violation is (file relative to `base`, line, column, kind, enclosing function, expression);
    errors are EVERY error / fatal diagnostic of the parse, wherever it is located (a unit libclang cannot parse is not checked).
    root defaults to engine/src (the project's code, whose nodes are inspected), base to engine/."""
    root = os.path.normpath(root or os.path.join(ENGINE, "src"))
    base = os.path.normpath(base or ENGINE)
    index = cindex.Index.create()
    tu = index.parse(path, args=args, options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
    errors = []
    src_root = root + os.sep
    for d in tu.diagnostics:
        if d.severity >= cindex.Diagnostic.Error:
            f = d.location.file
            where = "<unknown>"
            if f is not None:
                n = os.path.normpath(f.name)
                where = os.path.relpath(n, base).replace(os.sep, "/") if n.startswith(base + os.sep) else n
            errors.append("%s:%d: %s" % (where, d.location.line, d.spelling))
    if visited is not None:
        # coverage is the parsed unit and its project inclusions, not the files that happen to contain AST nodes (an emptied file must make its pins stale)
        files = [os.path.normpath(tu.spelling)] + [os.path.normpath(i.include.name) for i in tu.get_includes() if i.include is not None]
        for n in files:
            if n.startswith(src_root):
                visited.add(os.path.relpath(n, base).replace(os.sep, "/"))
    seen = {}
    CK, TK = cindex.CursorKind, cindex.TypeKind
    binary_kind, unary_kind = operator_kinds(cindex)
    float_kinds = {TK.FLOAT, TK.DOUBLE, TK.LONGDOUBLE, TK.FLOAT128, TK.HALF}
    int_kinds = {TK.CHAR_U, TK.UCHAR, TK.CHAR16, TK.CHAR32, TK.USHORT, TK.UINT, TK.ULONG, TK.ULONGLONG, TK.UINT128, TK.CHAR_S, TK.SCHAR,
                 TK.WCHAR, TK.SHORT, TK.INT, TK.LONG, TK.LONGLONG, TK.INT128}
    unresolved_kinds = {TK.INVALID, TK.DEPENDENT, TK.UNEXPOSED}
    func_kinds = {CK.FUNCTION_DECL, CK.CXX_METHOD, CK.CONSTRUCTOR, CK.DESTRUCTOR, CK.CONVERSION_FUNCTION, CK.FUNCTION_TEMPLATE}
    scope_kinds = {CK.NAMESPACE, CK.CLASS_DECL, CK.STRUCT_DECL, CK.CLASS_TEMPLATE}
    exempt_files = set(policy["facade_files"])
    exempt_functions = set(policy["facade_functions"])
    state = {"exempt": False, "func": ""}

    def kind_of(c):
        return c.type.get_canonical().kind

    def qualified(c):
        parts = [c.spelling]
        p = c.semantic_parent
        while p is not None and p.kind in scope_kinds:
            if p.spelling:
                parts.append(p.spelling)
            p = p.semantic_parent
        return "::".join(reversed(parts))

    def in_repo(name):
        return os.path.normpath(name).startswith(src_root)

    def report(c, kind):
        if state["exempt"]:
            return
        loc = c.location
        if loc.file is None:
            return
        # every node is kept: nested macro nodes share one expansion location (file, line, column) and are told apart by their complete expression
        key = (os.path.normpath(loc.file.name), loc.line, loc.column, kind, state["func"], expression_text(cindex, c, binary_kind, unary_kind))
        seen[key] = seen.get(key, 0) + 1

    def inspect_node(c):
        k = c.kind
        if k == CK.BINARY_OPERATOR or k == CK.COMPOUND_ASSIGNMENT_OPERATOR:
            op = binary_kind(c)
            children = list(c.get_children())
            kinds = [kind_of(ch) for ch in children]
            if op in BINARY_ARITH or op in BINARY_ARITH_ASSIGN:
                if kind_of(c) in float_kinds or (op in BINARY_ARITH_ASSIGN and any(x in float_kinds for x in kinds)):
                    # (a compound assignment into an integer from a float operand is float arithmetic followed by a conversion)
                    report(c, "float-arithmetic")
                elif kind_of(c) in unresolved_kinds and all(x in unresolved_kinds | float_kinds for x in kinds):
                    # a template body: the operand types are unknown until instantiation; with no concrete integer operand it may be floating
                    report(c, "dependent-arithmetic")
        elif k == CK.UNARY_OPERATOR and kind_of(c) in float_kinds and unary_kind(c) in UNARY_INCDEC:
            report(c, "float-arithmetic")
        elif k == CK.CALL_EXPR:
            ref = c.referenced
            if ref is not None and ref.spelling in MATH_NAMES and ref.kind in (CK.FUNCTION_DECL, CK.FUNCTION_TEMPLATE):
                rl = ref.location
                if rl.file is None or not in_repo(rl.file.name):
                    report(c, "math-call")
            elif ref is not None and ref.spelling in ("abs", "labs") and kind_of(c) in float_kinds:
                report(c, "math-call")
        elif k == CK.UNEXPOSED_EXPR:
            # an implicit conversion floating -> integer (bool is excluded: a comparison with zero is exact)
            if kind_of(c) in int_kinds:
                children = list(c.get_children())
                if len(children) == 1 and kind_of(children[0]) in float_kinds:
                    report(c, "float-to-int")
        elif k in (CK.CSTYLE_CAST_EXPR, CK.CXX_STATIC_CAST_EXPR, CK.CXX_FUNCTIONAL_CAST_EXPR):
            if kind_of(c) in int_kinds:
                children = list(c.get_children())
                if children and kind_of(children[-1]) in float_kinds:
                    report(c, "float-to-int")

    def walk(c, func):
        loc = c.location
        if loc.file is not None and not in_repo(loc.file.name):
            return
        if visited is not None and loc.file is not None:
            visited.add(os.path.relpath(os.path.normpath(loc.file.name), base).replace(os.sep, "/"))
        if c.kind in func_kinds:
            func = qualified(c)
        rel = os.path.relpath(os.path.normpath(loc.file.name), base).replace(os.sep, "/") if loc.file is not None else ""
        state["exempt"] = rel in exempt_files or (func is not None and func in exempt_functions)
        state["func"] = func or ""
        inspect_node(c)
        for ch in c.get_children():
            walk(ch, func)

    for top in tu.cursor.get_children():
        walk(top, None)
    result = []
    for key in sorted(seen):
        file, line, col, kind, func, expr = key
        for n in range(seen[key]):
            result.append((os.path.relpath(file, base).replace(os.sep, "/"), line, col, kind, func, expr))
    return result, errors


def source_line(rel, line):
    try:
        with open(os.path.join(ENGINE, rel), encoding="utf-8", errors="replace") as f:
            for n, text in enumerate(f, 1):
                if n == line:
                    return text.strip()
    except OSError:
        pass
    return ""


def run_ast(manifest, built, policy, jobs):
    cindex = import_clang()
    if cindex is None:
        return None, None
    compilers = {}
    work = []
    for rel in manifest["sources"]:
        if not rel.endswith((".cpp", ".cc", ".cxx")):
            continue
        entry = built.get(rel)
        if entry is None:
            continue
        compiler = command_args(entry)[0]
        if compiler not in compilers:
            compilers[compiler] = probe_includes(compiler)
        work.append((os.path.normpath(os.path.join(entry.get("directory", "."), entry["file"])), clang_args(entry, compilers[compiler]), policy))
    merged, errors, scanned = {}, [], set()
    with ProcessPoolExecutor(max_workers=jobs) as pool:
        for path, violations, errs, visited in pool.map(_scan_worker, work):
            scanned |= visited
            per_tu = {}
            for v in violations:
                per_tu[v] = per_tu.get(v, 0) + 1
            for v, n in per_tu.items():  # the same header node seen from several units counts once: the largest count of any unit
                merged[v] = max(merged.get(v, 0), n)
            errors += ["%s: %s" % (os.path.relpath(path, ENGINE), e) for e in errs]
    out = []
    for v in sorted(merged):
        out += [v] * merged[v]
    run_ast.scanned = scanned  # the files whose nodes this run inspected (a pin of a file that no unit of THIS build includes cannot be judged stale)
    return out, errors


# ---------------------------------------------------------------------------------------------------------------------------------------------
# the baseline: stable identities
# ---------------------------------------------------------------------------------------------------------------------------------------------
def identities(violations):
    """{file: {identity: count}} with identity = kind | enclosing function | normalised expression (no line numbers: moving code does not change it,
    replacing one violation by another does)."""
    out = {}
    for (file, _line, _col, kind, func, expr) in violations:
        ident = "%s | %s | %s" % (kind, func, expr)
        out.setdefault(file, {}).setdefault(ident, 0)
        out[file][ident] += 1
    return out


def load_baseline(path=None):
    path = path or BASELINE_PATH
    data = json.load(open(path, encoding="utf-8"))
    validate_baseline(data)
    return data


def validate_baseline(data):
    if not isinstance(data, dict) or data.get("version") != 2 or not isinstance(data.get("files"), dict):
        raise ValueError("tools/sim/sim_baseline.json is not a version 2 baseline (use --migrate-baseline once on an old one)")
    for file, idents in data["files"].items():
        if not isinstance(idents, dict) or not idents:
            raise ValueError("baseline entry %s is empty or malformed" % file)
        for ident, n in idents.items():
            if not isinstance(n, int) or n < 1 or ident.count(" | ") < 2:
                raise ValueError("baseline entry %s: bad identity or count %r" % (file, ident))


def compare_baseline(current, baseline_files, scanned=None):
    """(problems, stale): a identity above its pin (or absent from it) is a problem; a pin above the current count is stale and must be lowered."""
    problems, stale = [], []
    for file, idents in sorted(current.items()):
        pinned = baseline_files.get(file, {})
        for ident, n in sorted(idents.items()):
            if n > pinned.get(ident, 0):
                problems.append("%s: new violation (%d found, %d pinned): %s" % (file, n, pinned.get(ident, 0), ident))
    for file, idents in sorted(baseline_files.items()):
        if scanned is not None and file not in scanned:
            continue  # not part of this build (e.g. a source of the Godot extension in a core-only build)
        for ident, n in sorted(idents.items()):
            now = current.get(file, {}).get(ident, 0)
            if now < n:
                stale.append("%s: pin no longer matches (%d found, %d pinned): %s" % (file, now, n, ident))
    return problems, stale


def lowered_baseline(current, baseline_files, scanned=None):
    """The baseline after --update-baseline: every pin lowered to the current count, none raised, none added. Raises when the update would not be a pure
    removal (a new identity or a higher count)."""
    problems, _stale = compare_baseline(current, baseline_files)
    if problems:
        raise ValueError("refusing to rewrite the baseline: it would raise an allowance or add an exemption:\n  " + "\n  ".join(problems[:20]))
    out = {}
    for file, idents in baseline_files.items():
        if scanned is not None and file not in scanned:
            out[file] = dict(idents)  # not part of this build: its pins stay as they are
            continue
        kept = {i: min(n, current.get(file, {}).get(i, 0)) for i, n in idents.items()}
        kept = {i: n for i, n in kept.items() if n > 0}
        if kept:
            out[file] = kept
    return out


def migrate_baseline(current, old_counts, allow_increase=False):
    """One-time conversion of the version 1 baseline ({file: {kind: count}}) to identities: only when no kind count of any file went UP."""
    problems = []
    kinds = {}
    for file, idents in current.items():
        for ident, n in idents.items():
            kinds.setdefault(file, {}).setdefault(ident.split(" | ")[0], 0)
            kinds[file][ident.split(" | ")[0]] += n
    for file, ks in kinds.items():
        for kind, n in ks.items():
            if n > old_counts.get(file, {}).get(kind, 0):
                problems.append("%s: %d %s now, %d pinned" % (file, n, kind, old_counts.get(file, {}).get(kind, 0)))
    if problems and not allow_increase:
        raise ValueError("refusing to migrate the baseline: counts went up (--allow-increase accepts them once, e.g. when the detector got stricter):\n  "
                         + "\n  ".join(problems))
    return current


def repin_baseline(current, old_files, allow_increase=False, adopt=()):
    """Re-express the pins with the current identities (the identity format changed, e.g. complete AST expressions instead of source tokens). Allowed only
    when no (file, kind) count rises above what was pinned; files in `adopt` (newly classified as simulation, no pins yet) may be added."""
    def kinds(files):
        out = {}
        for file, idents in files.items():
            for ident, n in idents.items():
                k = ident.split(" | ")[0]
                out.setdefault(file, {}).setdefault(k, 0)
                out[file][k] += n
        return out
    now, before = kinds(current), kinds(old_files)
    problems = []
    for file, ks in sorted(now.items()):
        for kind, n in sorted(ks.items()):
            allowed = before.get(file, {}).get(kind, 0)
            if n > allowed and file not in adopt:
                problems.append("%s: %d %s now, %d pinned" % (file, n, kind, allowed))
    if problems and not allow_increase:
        raise ValueError("refusing to re-pin: counts rose (--allow-increase accepts them once; the baseline diff is the review):\n  " + "\n  ".join(problems))
    return current


def write_baseline(files, path=None):
    with open(path or BASELINE_PATH, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"version": 2, "files": {k: dict(sorted(files[k].items())) for k in sorted(files)}}, f, indent=1)
        f.write("\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", required=True, help="the configured build directory (compile_commands.json, sim_manifest.json)")
    ap.add_argument("--ast", choices=("require", "skip"), default="require")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--update-baseline", action="store_true",
                    help="lower the pins of tools/sim/sim_baseline.json to the current findings (refuses to raise an allowance or add an exemption)")
    ap.add_argument("--migrate-baseline", action="store_true", help="convert a version 1 (counts) baseline to identities, once; refuses when a count went up")
    ap.add_argument("--allow-increase", action="store_true", help="with --migrate-baseline / --repin: accept the increases (the detector got stricter; the diff is the review)")
    ap.add_argument("--repin", action="store_true", help="re-express the pins with the current identity format; refuses when a (file, kind) count rises "
                    "(files named by --adopt may be new)")
    ap.add_argument("--adopt", nargs="+", metavar="FILE", help="pin the current findings of these files (relative to engine/) that have no pins yet: for sources "
                    "newly added to the manifest; the diff of the baseline is the review")
    ap.add_argument("--json", help="write every violation to this file")
    ap.add_argument("--show", action="store_true", help="print every violation (default: per file counts and the new ones)")
    a = ap.parse_args(argv)

    build = os.path.abspath(a.build)
    for name in ("compile_commands.json", "sim_manifest.json"):
        if not os.path.exists(os.path.join(build, name)):
            print("ERROR: %s not found in %s: configure the build first (and use a Ninja or Makefile generator)" % (name, build))
            return 2
    entries = json.load(open(os.path.join(build, "compile_commands.json"), encoding="utf-8"))
    manifest = json.load(open(os.path.join(build, "sim_manifest.json"), encoding="utf-8"))
    policy = json.load(open(POLICY_PATH, encoding="utf-8"))

    failures = []
    flag_problems = audit_flags(entries, manifest, build)
    link_problems, links = audit_link(build, manifest)
    manifest_problems, built = audit_manifest(entries, manifest, policy, build)
    failures += ["FLAGS: " + p for p in flag_problems] + ["LINK: " + p for p in link_problems] + ["CLASSIFY: " + p for p in manifest_problems]
    print("flags: %d compile commands checked, %d problem(s)" % (len(project_entries(entries, build)), len(flag_problems)))
    print("link: %d link command(s) checked, %d problem(s)" % (links, len(link_problems)))
    print("classification: %d simulation sources, %d excluded, %d problem(s)" % (
        len(manifest["sources"]), sum(1 for n in built if n not in manifest["sources"]), len(manifest_problems)))

    violations, notes = None, []
    if a.ast == "skip":
        print("AST CHECK SKIPPED (--ast=skip): the floating-point arithmetic of the simulation sources was NOT checked")
    else:
        violations, errors = run_ast(manifest, built, policy, a.jobs)
        if violations is None:
            print("ERROR: libclang is not available (pip install libclang==18.1.1; OPENBFME_LIBCLANG_PYTHONPATH / OPENBFME_LIBCLANG_LIBRARY). "
                  "The AST check cannot run; this is registered as stop S-230.")
            return 2
        failures += ["AST PARSE: " + e for e in errors]
        if a.json:
            with open(a.json, "w", encoding="utf-8", newline="\n") as f:
                json.dump([{"file": v[0], "line": v[1], "column": v[2], "kind": v[3], "function": v[4], "expression": v[5]} for v in violations], f, indent=1)
        current = identities(violations)
        baseline_exists = os.path.exists(BASELINE_PATH)
        try:
            if a.migrate_baseline:
                old = json.load(open(BASELINE_PATH, encoding="utf-8"))
                write_baseline(migrate_baseline(current, old, a.allow_increase))
                print("baseline migrated to identities")
            elif a.repin:
                write_baseline(repin_baseline(current, load_baseline()["files"], a.allow_increase, tuple(a.adopt or ())))
                print("baseline re-pinned")
            elif a.update_baseline:
                write_baseline(lowered_baseline(current, load_baseline()["files"], getattr(run_ast, "scanned", None)))
                print("baseline lowered")
            elif a.adopt:
                base = load_baseline()["files"]
                for f in a.adopt:
                    if f in base:
                        raise ValueError("%s already has pins: --adopt is for files with none (lower them with --update-baseline)" % f)
                    if f in current:
                        base[f] = current[f]
                write_baseline(base)
                print("adopted: " + ", ".join(a.adopt))
        except ValueError as ex:
            print("ERROR: " + str(ex))
            return 2
        try:
            baseline = load_baseline()["files"] if baseline_exists or a.migrate_baseline else {}
        except ValueError as ex:
            print("ERROR: " + str(ex))
            return 2
        problems, stale = compare_baseline(current, baseline, getattr(run_ast, "scanned", None))
        failures += ["AST: " + p for p in problems] + ["BASELINE: " + p for p in stale]
        print("ast: %d violation(s) in %d file(s) (%d pinned files)" % (len(violations), len(current), len(baseline)))
        per_kind = {}
        for (file, _l, _c, kind, _f, _e) in violations:
            per_kind.setdefault(file, {}).setdefault(kind, 0)
            per_kind[file][kind] += 1
        for file, kinds in sorted(per_kind.items()):
            print("  %-70s %s" % (file, ", ".join("%s %d" % (k, n) for k, n in sorted(kinds.items()))))
        if a.show:
            for (file, line, col, kind, func, expr) in violations:
                print("    %s:%d:%d: %s: %s  [%s]" % (file, line, col, kind, source_line(file, line), shortened(expr)))

    for sid, text in sorted(STOPS.items()):
        print("stop %s: %s" % (sid, text))
    if failures:
        print("\nsim audit FAILED:")
        for f in failures[:200]:
            print("  " + f)
        if len(failures) > 200:
            print("  ... %d more" % (len(failures) - 200))
        return 1
    print("sim audit ok" + ("" if violations is not None else " (flags, link and classification only; AST skipped)"))
    return 3 if a.ast == "skip" else 0


if __name__ == "__main__":
    sys.exit(main())
