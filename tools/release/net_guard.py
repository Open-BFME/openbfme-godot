"""Lane RELEASE-1: the privacy rule "no network calls besides LAN play", checked on the sources.

    python3 tools/release/net_guard.py        (prints every finding of the engine and the shipped GDScript; exit 1 when one is outside the allow list)

C / C++ (engine/src): a string- and comment-aware lexer (string, character and raw string literals, line and block comments, digit
separators) yields tokens; a finding is
  * any REFERENCE to a global socket / resolver / HTTP API, not only a call: `socket(`, `(::socket)(`, `&socket`, `auto f = socket;`,
    a call after a "http://..." string on the same line (review r2: these passed), unless it is a member (`a.connect`, `p->connect`) or
    qualified by a class / namespace (`Signal::connect`, `std::bind`); the global qualifier `::socket` is a reference;
  * a string literal that names such an API or a network library (`GetProcAddress(h, "WSASocketW")`, `dlopen("libcurl.so")`);
  * an #include of a socket / network header.
GDScript (godot/, not godot/tests): the same lexing for "..." / '...' / triple-quoted strings and # comments; a finding is any
networking class, `OS.execute` / `create_process` / `shell_open` / `request_permission` (except the developer profiler's literal
`OS.create_process("perf", ...)` / `OS.execute("kill", ...)`, PERF-3's --perf-stat), or a string literal naming a networking class
(`ClassDB.instantiate("HTTPRequest")`).
Only engine/src/GameNetwork/Transport.cpp (the LAN transport, UDP) may have C / C++ findings.
The launcher (launcher/, not launcher/tests; lane LAUNCH-1) is checked the same way with its own allow list, finding by finding:
HTTPClient only in launcher/scripts/net/http_fetch.gd (whose every request passes NetPolicy.check: GitHub's release hosts over HTTPS),
OS.create_process only where the launcher starts the game (updater.gd) and its own updated executable (self_update.gd), OS.execute
only for the self-update's atomic replace on Windows (self_update.gd: cmd /c move /y).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# every C / POSIX / Winsock / WinINet / WinHTTP / curl entry point that opens, connects, resolves or sends on a socket
NATIVE_API = re.compile(r"(socket|socketpair|connect|connectx|sendto|sendmsg|sendmmsg|recvfrom|recvmsg|recvmmsg|getaddrinfo|getnameinfo|"
                        r"GetAddrInfo\w*|GetNameInfo\w*|gethostbyname\w*|gethostbyaddr|WSAStartup|WSASocket\w*|WSAConnect\w*|WSAAccept|"
                        r"WSAIoctl|WSASend\w*|WSARecv\w*|InternetOpen\w*|InternetConnect\w*|WinHttp\w*|URLDownloadToFile\w*|URLOpen\w*|"
                        r"HttpOpenRequest\w*|HttpSendRequest\w*|curl_\w+)$")
NATIVE_LIBRARY = re.compile(r"(?i)(ws2_32|wsock32|wininet|winhttp|urlmon|libcurl|libsocket)(\.dll|\.so[.\d]*|\.lib)?$")
NATIVE_HEADER = re.compile(r"(?i)^(sys/socket\.h|netinet/.*|arpa/inet\.h|netdb\.h|sys/un\.h|winsock2?\.h|ws2tcpip\.h|ws2def\.h|mswsock\.h|"
                           r"wininet\.h|winhttp\.h|urlmon\.h|curl/.*)$")
ALLOWED_NATIVE = {"engine/src/GameNetwork/Transport.cpp"}
# launcher file -> the findings it may have (the text after "line <n>: ")
ALLOWED_LAUNCHER = {"launcher/scripts/net/http_fetch.gd": {"HTTPClient"},
                    "launcher/scripts/core/updater.gd": {"OS.create_process"},
                    # OS.execute: `cmd /c move /y` on Windows, the one atomic replace of the launcher's executable (MoveFileEx)
                    "launcher/scripts/core/self_update.gd": {"OS.create_process", "OS.execute"}}

GODOT_NET = re.compile(r"(HTTPRequest|HTTPClient|WebSocketPeer|WebSocketMultiplayerPeer|StreamPeerTCP|StreamPeerTLS|TCPServer|PacketPeerUDP|"
                       r"PacketPeerDTLS|DTLSServer|UDPServer|ENetConnection|ENetMultiplayerPeer|ENetPacketPeer|WebRTC\w*|JavaScriptBridge|"
                       r"MultiplayerAPI\w*|SceneMultiplayer|OfflineMultiplayerPeer|MultiplayerPeer\w*|IP|IPUnix)$")
GODOT_OS_CALLS = {"execute", "execute_with_pipe", "create_process", "create_instance", "shell_open", "shell_show_in_file_manager",
                  "request_permission", "request_permissions"}


_INCLUDE = re.compile(r"#\s*include\s*[<\"]([^>\"\n]+)[>\"]")
_RAW = re.compile(r"(?:u8|u|U|L)?R\"([^ ()\\\t\n]{0,16})\(")
_QUOTE = re.compile(r"(?:u8|u|U|L)?([\"'])")
_IDENT = re.compile(r"[A-Za-z_]\w*")
_NUMBER = re.compile(r"\d[\w.']*")
_GD_QUOTE = re.compile(r"(?:&|\^|r)?(\"\"\"|'''|\"|')")


def lex_c(text: str) -> tuple[list[tuple[str, int]], list[tuple[str, int]], list[tuple[str, int]]]:
    """(tokens, string literal contents, #include targets), each with its line number. Comments and literals never produce tokens."""
    tokens, strings, includes = [], [], []
    i, n, line = 0, len(text), 1
    at_line_start = True
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            at_line_start = True
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        if c == "\\" and i + 1 < n and text[i + 1] == "\n":
            i += 2
            line += 1
            continue
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n and text[i + 1] == "\n":  # a continued line comment
                    line += 1
                    i += 1
                i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            line += text.count("\n", i, end)
            i = end
            continue
        if c == "#" and at_line_start:
            m = _INCLUDE.match(text, i)
            if m:
                includes.append((m.group(1).strip(), line))
                i = m.end()
                continue
            tokens.append(("#", line))
            i += 1
            at_line_start = False
            continue
        at_line_start = False
        m = _RAW.match(text, i)
        if m:
            close = ")" + m.group(1) + "\""
            end = text.find(close, m.end())
            end = n if end < 0 else end
            strings.append((text[m.end():end], line))
            line += text.count("\n", i, end)
            i = end + len(close)
            continue
        m = _QUOTE.match(text, i)
        if m and (m.group(0) != "'" or not (tokens and re.match(r"\d", tokens[-1][0] or "x") and text[i - 1].isalnum())):
            quote = m.group(1)
            j = m.end()
            buf = []
            while j < n and text[j] != quote and text[j] != "\n":
                if text[j] == "\\" and j + 1 < n:
                    buf.append(text[j:j + 2])
                    j += 2
                    continue
                buf.append(text[j])
                j += 1
            if quote == '"':
                strings.append(("".join(buf), line))
            i = j + 1
            continue
        m = _IDENT.match(text, i)
        if m:
            tokens.append((m.group(0), line))
            i = m.end()
            continue
        m = _NUMBER.match(text, i)  # numbers, digit separators included
        if m:
            tokens.append((m.group(0), line))
            i = m.end()
            continue
        for op in ("::", "->", "."):
            if text.startswith(op, i):
                tokens.append((op, line))
                i += len(op)
                break
        else:
            tokens.append((c, line))
            i += 1
    return tokens, strings, includes


EXPRESSION_KEYWORDS = {"return", "case", "else", "do", "throw", "co_return", "co_await", "co_yield", "sizeof", "decltype", "typeid",
                       "alignof", "noexcept", "new", "delete", "not", "and", "or", "if", "while", "for", "switch"}


def _is_declarator(tokens: list[tuple[str, int]], k: int) -> bool:
    """tokens[k] is the name in a declaration: after a type (`UDP socket`, `UDP &socket`, `unique_ptr<UDP> socket`, `int *connect`)."""
    j = k - 1
    while j >= 0 and tokens[j][0] in ("&", "*", "&&", "const"):
        j -= 1
    if j < 0:
        return False
    t = tokens[j][0]
    return t == ">" or (re.match(r"[A-Za-z_]\w*$", t) is not None and t not in EXPRESSION_KEYWORDS)


def _scopes(tokens: list[tuple[str, int]]) -> tuple[list[int], dict[int, int], list[int]]:
    """(innermost enclosing '{' of each token or -1, matching '}' of each '{', innermost enclosing '(' of each token or -1)."""
    brace_of, paren_of = [], []
    match: dict[int, int] = {}
    braces, parens = [], []
    for k, (t, _) in enumerate(tokens):
        brace_of.append(braces[-1] if braces else -1)
        paren_of.append(parens[-1] if parens else -1)
        if t == "{":
            braces.append(k)
        elif t == "}" and braces:
            match[braces.pop()] = k
        elif t == "(":
            parens.append(k)
        elif t == ")" and parens:
            match[parens.pop()] = k
    return brace_of, match, paren_of


def _declaration_scope(tokens, k: int, brace_of, match, paren_of) -> tuple[int, int]:
    """Where a name declared at tokens[k] is visible (review r3: the shadowing exemption was file-wide). A parameter (or a for / if
    condition variable) inside parentheses: from k through the block that follows the closing ')' (the function body, its member-init list
    included), or to the ';' when no block follows (a declaration only). Anything else: from k to the end of its enclosing block."""
    end_all = len(tokens) - 1
    p = paren_of[k]
    if p >= 0:
        close = match.get(p, end_all)
        j = close + 1
        while j < len(tokens) and tokens[j][0] not in ("{", ";"):
            j += 1
        if j < len(tokens) and tokens[j][0] == "{":
            return k, match.get(j, end_all)
        return k, j
    b = brace_of[k]
    return k, (match.get(b, end_all) if b >= 0 else end_all)


def native_findings(text: str) -> list[str]:
    """Every reference to a global network API in C / C++ text (see the module doc). A local, parameter or member called like an API
    (`UDP &socket`) shadows it for bare uses INSIDE its scope only; a call, the global qualifier `::socket` (also after `return` or any other
    keyword, review r3) and every use outside such a scope are findings."""
    tokens, strings, includes = lex_c(text)
    brace_of, match, paren_of = _scopes(tokens)
    scopes: dict[str, list[tuple[int, int]]] = {}
    for k, (tok, _) in enumerate(tokens):
        if NATIVE_API.match(tok) and _is_declarator(tokens, k) and not (k + 1 < len(tokens) and tokens[k + 1][0] == "("
                                                                      and paren_of[k] < 0 and brace_of[k] < 0):
            scopes.setdefault(tok, []).append(_declaration_scope(tokens, k, brace_of, match, paren_of))
    found = []
    for k, (tok, line) in enumerate(tokens):
        if not NATIVE_API.match(tok):
            continue
        prev = tokens[k - 1][0] if k else ""
        nxt = tokens[k + 1][0] if k + 1 < len(tokens) else ""
        if prev in (".", "->"):
            continue  # a member: obj.connect, rs->connect
        if prev == "::":
            before = tokens[k - 2][0] if k >= 2 else ""
            if re.match(r"[A-Za-z_]\w*$", before) and before not in EXPRESSION_KEYWORDS:
                continue  # qualified by a class or namespace: Signal::connect, std::bind
            found.append(f"line {line}: ::{tok}")  # the global qualifier (`return ::socket`, `(::socket)`, `x > ::socket`) names the C API
            continue
        if _is_declarator(tokens, k) and nxt != "(":
            continue  # the declaration of a local / parameter / member of that name
        in_scope = any(a <= k <= b for a, b in scopes.get(tok, []))
        if nxt == "(" or not in_scope:
            found.append(f"line {line}: {tok}")
    for s, line in strings:
        if NATIVE_API.match(s) or NATIVE_LIBRARY.match(s):
            found.append(f"line {line}: string \"{s}\"")
    for h, line in includes:
        if NATIVE_HEADER.match(h):
            found.append(f"line {line}: #include {h}")
    return found


def lex_gd(text: str) -> tuple[list[tuple[str, int]], list[tuple[str, int]]]:
    tokens, strings = [], []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c == "#":
            while i < n and text[i] != "\n":
                i += 1
            continue
        m = _GD_QUOTE.match(text, i)
        if m:
            quote = m.group(1)
            j = m.end()
            buf = []
            while j < n and not text.startswith(quote, j) and (len(quote) == 3 or text[j] != "\n"):
                if text[j] == "\\" and j + 1 < n:
                    buf.append(text[j:j + 2])
                    j += 2
                    continue
                buf.append(text[j])
                j += 1
            content = "".join(buf)
            strings.append((content, line))
            line += content.count("\n")
            i = j + len(quote)
            continue
        m = _IDENT.match(text, i)
        if m:
            tokens.append((m.group(0), line))
            i = m.end()
            continue
        tokens.append((c, line))
        i += 1
    return tokens, strings


# the developer profiler (lane PERF-3, --perf-stat): Linux `perf stat` on the game's own process and `kill -INT` of that perf. Allowed only as a
# literal first argument, and only when every process call on the line is one of these (a call naming anything else stays a finding)
PROFILER_CALL = re.compile(r'OS\.(create_process|execute)\(\s*"(perf|kill)"\s*,')
PROCESS_CALL = re.compile(r'OS\.(\w+)\s*\(')


def gdscript_findings(text: str) -> list[str]:
    tokens, strings = lex_gd(text)
    lines = text.splitlines()
    found = []
    for k, (tok, line) in enumerate(tokens):
        if GODOT_NET.match(tok) and not (k and tokens[k - 1][0] == "."):
            found.append(f"line {line}: {tok}")
        if tok in GODOT_OS_CALLS and k >= 2 and tokens[k - 1][0] == "." and tokens[k - 2][0] == "OS":
            src = lines[line - 1] if 0 < line <= len(lines) else ""
            calls = [m for m in PROCESS_CALL.finditer(src) if m.group(1) in GODOT_OS_CALLS]
            if calls and len(calls) == len(PROFILER_CALL.findall(src)):
                continue
            found.append(f"line {line}: OS.{tok}")
    for s, line in strings:
        if GODOT_NET.match(s):
            found.append(f"line {line}: string \"{s}\"")
    return found


SOURCE_SUFFIXES = (".cpp", ".h", ".c", ".hpp", ".inc", ".cc", ".cxx", ".hh", ".ipp", ".inl", ".tpp")


def scan() -> dict[str, list[str]]:
    """{file: findings} for engine/src and the shipped GDScript (godot/ without godot/tests)."""
    out = {}
    for src in sorted((REPO / "engine/src").rglob("*")):
        if src.is_file() and src.suffix.lower() in SOURCE_SUFFIXES:
            f = native_findings(src.read_text(encoding="utf-8", errors="replace"))
            if f:
                out[str(src.relative_to(REPO))] = f
    for gd in sorted((REPO / "godot").rglob("*.gd")):
        if "tests" in gd.relative_to(REPO / "godot").parts:
            continue
        f = gdscript_findings(gd.read_text(encoding="utf-8", errors="replace"))
        if f:
            out[str(gd.relative_to(REPO))] = f
    return out


def scan_launcher() -> dict[str, list[str]]:
    """{file: findings} for the launcher's shipped GDScript (launcher/ without launcher/tests)."""
    out = {}
    for gd in sorted((REPO / "launcher").rglob("*.gd")):
        rel = gd.relative_to(REPO / "launcher").parts
        if rel[0] in ("tests", ".godot"):
            continue
        f = gdscript_findings(gd.read_text(encoding="utf-8", errors="replace"))
        if f:
            out[str(gd.relative_to(REPO))] = f
    return out


def launcher_finding_allowed(path: str, finding: str) -> bool:
    return finding.split(": ", 1)[-1] in ALLOWED_LAUNCHER.get(path, set())


def main() -> int:
    bad = 0
    for path, findings in scan().items():
        allowed = path in ALLOWED_NATIVE
        bad += 0 if allowed else 1
        for f in findings:
            print(f"{'allowed' if allowed else 'FINDING'} {path}: {f}")
    for path, findings in scan_launcher().items():
        for f in findings:
            allowed = launcher_finding_allowed(path, f)
            bad += 0 if allowed else 1
            print(f"{'allowed' if allowed else 'FINDING'} {path}: {f}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
