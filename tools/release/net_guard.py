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
`OS.create_process("perf", ...)` / `OS.execute("kill", ...)`, PERF-3's --perf-stat, and WINCRASH-1's log folder
`OS.shell_show_in_file_manager` / `shell_open` of exactly `ProjectSettings.globalize_path(<"user://..." literal or const, no "..">)` or of
a variable its file validates (FILE_MANAGER_CALL): a URL, a UNC path, a conditional, a concatenation or any other argument is a finding,
Godot's globalize_path passes those through: Sol r1 / r2), or a string literal naming a networking class
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


def lex_gd_stream(text: str) -> list[tuple[str, str, int, int]]:
    """The GDScript source as one ordered stream of (kind, value, line, column): kind "ident", "punct" or "string" (the literal's content);
    comments dropped. The column of a token is where it starts on its line (a function at column 0 is a top-level function)."""
    out: list[tuple[str, str, int, int]] = []
    i, n, line, line_start = 0, len(text), 1, 0
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            line_start = i
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
            out.append(("string", content, line, i - line_start))
            if "\n" in content:
                line += content.count("\n")
                line_start = text.rfind("\n", 0, j) + 1
            i = j + len(quote)
            continue
        m = _IDENT.match(text, i)
        if m:
            out.append(("ident", m.group(0), line, i - line_start))
            i = m.end()
            continue
        if not c.isspace():
            out.append(("punct", c, line, i - line_start))
        i += 1
    return out


def lex_gd(text: str) -> tuple[list[tuple[str, int]], list[tuple[str, int]]]:
    tokens, strings = [], []
    for kind, value, line, _column in lex_gd_stream(text):
        (strings if kind == "string" else tokens).append((value, line))
    return tokens, strings


# the developer profiler (lane PERF-3, --perf-stat): Linux `perf stat` on the game's own process and `kill -INT` of that perf: allowed only as
# the token sequence OS . create_process|execute ( "perf"|"kill" , (by tokens: no comment or whitespace changes what is matched)
PROFILER_COMMANDS = {"create_process": {"perf"}, "execute": {"kill"}}
# lane WINCRASH-1 r4: the file manager is opened by exactly one function of the game, release.gd's _show_in_file_manager(path), which
# accepts only a path inside OS.get_user_data_dir() after normalising; its callers open_user_folder(kind) (a fixed set of folder kinds,
# their user:// paths built inside it) and open_log_file(name) (a validated session log file name) build the path. Any other reference to
# these OS methods, anywhere (another file, another function, a string naming one for call() / Callable()), is a finding.
FILE_MANAGER_METHODS = {"shell_open", "shell_show_in_file_manager"}
# lane UI-3: the launcher's "Show folder" / "Open log folder" likewise go through one function, paths.gd show_in_file_manager(path), which
# opens only an existing local folder that is the launcher's data folder (or inside it) or one of the game folders it found
FILE_MANAGER_OWNERS = {("godot/scripts/release/release.gd", "_show_in_file_manager"), ("launcher/scripts/ui/paths.gd", "show_in_file_manager")}


def gdscript_findings(text: str, path: str | None = None) -> list[str]:
    stream = lex_gd_stream(text)
    found = []
    function = None  # the top-level function the token is in
    for k, (kind, value, line, column) in enumerate(stream):
        if kind == "ident" and column == 0:
            j = k + 1 if value == "static" and k + 1 < len(stream) and stream[k + 1][1] == "func" else k  # "static func name" too
            function = stream[j + 1][1] if stream[j][1] == "func" and j + 1 < len(stream) and stream[j + 1][0] == "ident" else None
        if kind == "string":
            if GODOT_NET.match(value):
                found.append(f"line {line}: string \"{value}\"")
            if value in FILE_MANAGER_METHODS or value in GODOT_OS_CALLS:
                found.append(f"line {line}: string \"{value}\" names an OS process / file-manager method")
            continue
        if kind != "ident":
            continue
        if GODOT_NET.match(value) and not (k and stream[k - 1][1] == "."):
            found.append(f"line {line}: {value}")
        if value in GODOT_OS_CALLS and k >= 2 and stream[k - 1][1] == "." and stream[k - 2][1] == "OS":
            if value in FILE_MANAGER_METHODS and (path, function) in FILE_MANAGER_OWNERS:
                continue
            nxt = stream[k + 1:k + 4]
            if (value in PROFILER_COMMANDS and len(nxt) == 3 and nxt[0][1] == "(" and nxt[1][0] == "string" and nxt[1][1] in PROFILER_COMMANDS[value]
                    and nxt[2][1] == ","):
                continue
            found.append(f"line {line}: OS.{value}")
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
        rel = str(gd.relative_to(REPO))
        f = gdscript_findings(gd.read_text(encoding="utf-8", errors="replace"), rel)
        if f:
            out[rel] = f
    return out


def scan_launcher() -> dict[str, list[str]]:
    """{file: findings} for the launcher's shipped GDScript (launcher/ without launcher/tests)."""
    out = {}
    for gd in sorted((REPO / "launcher").rglob("*.gd")):
        rel = gd.relative_to(REPO / "launcher").parts
        if rel[0] in ("tests", ".godot"):
            continue
        f = gdscript_findings(gd.read_text(encoding="utf-8", errors="replace"), str(gd.relative_to(REPO)).replace("\\", "/"))
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
