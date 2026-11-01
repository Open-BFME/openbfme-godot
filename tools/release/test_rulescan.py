"""PRIVSPEED-1: the privacy gate's fast scans (rulescan.py, autorelease.binary_rule_hits / parallel_scans) find exactly what the
pre-PRIVSPEED-1 code found. The oracle is that code itself, kept here verbatim (reference_hits: every rule's rx.finditer over every
form), never the fast path's own output; planted values in every encoding and alignment, in nested and duplicated members, are checked
by name as well."""
from __future__ import annotations

import io
import random
import re
import sys
import tarfile
import gzip
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autorelease  # noqa: E402
import rulescan  # noqa: E402
from test_autorelease import PRIVATE, ip, zip_of, _leaks  # noqa: E402

RULES = autorelease.BUILTIN + PRIVATE


def reference_normal(text: str) -> str:
    """autorelease._normal as it was before PRIVSPEED-1 (1b7934099), verbatim"""
    import unicodedata

    def back(t: str) -> str:
        for k, v in autorelease.DENEUTRAL.items():
            t = t.replace(chr(k), v)
        return t
    return back(unicodedata.normalize("NFKC", back(text)))


def reference_texts(data: bytes):
    """autorelease.binary_texts as it was before PRIVSPEED-1 (1b7934099), verbatim"""
    def decodings():
        yield "latin-1", data.decode("latin-1")
        yield "utf-8", data.decode("utf-8", "replace")
        for enc, step in (("utf-16-le", 2), ("utf-16-be", 2), ("utf-32-le", 4), ("utf-32-be", 4)):
            for off in range(step):
                yield f"{enc} +{off}", data[off:len(data) - (len(data) - off) % step].decode(enc, "replace")
    for enc, text in decodings():
        yield enc, text
        if not text.isascii():
            norm = reference_normal(text)
            if norm != text:
                yield f"{enc} NFKC", norm


def reference_hits(path, text, rules, check_path=True, metadata=False, data=None):
    """autorelease.denylist_hits as it was before PRIVSPEED-1 (1b7934099), verbatim but for the module prefix"""
    A = autorelease
    names = A.name_forms(path) if check_path else []
    named = [rid for rid, rx, allow, _ in rules if not any(g.match(path) for g in allow) and any(rx.search(n) for n in names)]
    if named:
        A.PRIVATE_NAMES.add(path)
    shown = A.sanitize(path, rules)
    if text is not None:
        for own in A.OWN_LINES:
            text = text.replace(own, "")
    texts = None if text is None else [(None, f) for f in A.text_forms(text, unescape=metadata)]
    if data is not None:
        texts = reference_texts(data)
    live = [r for r in rules if not any(g.match(path) for g in r[2])]
    found: dict[str, list[str]] = {r[0]: [] for r in live}
    for rid, rx, _, _ in live:
        if rid in named:
            m = next(m for m in (rx.search(n) for n in names) if m)
            found[rid].append(f"denylisted ({rid}) in a file name: {shown} ({A._mask(m.group(0))})")
    seen: set[tuple[str, str]] = set()
    values: dict[tuple[str, str], set[str]] = {}
    for enc, form in texts or ():
        for rid, rx, _, paths_only in live:
            if paths_only:
                continue
            for m in rx.finditer(form):
                if enc and len(m.group(0)) <= A.short_binary(rid):
                    continue
                if enc == "utf-8" and m.group(0).isascii():
                    continue
                if enc and enc.endswith(" NFKC") and m.group(0) in values.get((rid, enc[:-5]), ()):
                    continue
                if enc:
                    values.setdefault((rid, enc), set()).add(m.group(0))
                where = f"{shown}:{form.count(chr(10), 0, m.start()) + 1}" if enc is None else f"{shown} ({enc} bytes at {m.start()})"
                key = (rid, where if enc is None else enc)
                if key not in seen:
                    seen.add(key)
                    found[rid].append(f"denylisted ({rid}) in {where} ({A._mask(m.group(0))})")
    return [h for rid in dict.fromkeys(r[0] for r in live) for h in found[rid]]


# the values every rule must catch (built-in and the test's private list), spelled as written, in another case, full-width, with the
# look-alikes neutral() writes and with the characters IGNORECASE matches to ASCII letters (Kelvin sign, long s, dotted / dotless I);
# spelled in pieces here, so that this file passes the tree's own privacy audit (only test_autorelease.py is exempt)
HOST, VM, USER, IDS = "build" + "box-7", "lab-" + "vm-q", "ops" + "user", "998877" + "665544"
C = chr   # the look-alikes by code point (U+2011 non-breaking hyphen, U+FF20 full-width @, U+0130 / U+0131 dotted / dotless I, U+212A
#           Kelvin sign, U+017F long s)
WIDE = "".join(C(ord(c) + 0xFEE0) if c.isalnum() else c for c in HOST)
PLANTED = [ip(192, 168, 7, 20), ip(10, 20, 30, 40), ip(172, 20, 1, 9), ip(100, 101, 102, 103), "box.tail1234." + "ts.net",
           "~/." + "ssh/id_x", "/mnt/c/" + "Users/someone/x", "arn:" + "aws:iam::1:role/x", "aws account: 1234-" + "5678-9012",
           "account_id = " + "ab" * 16, "owner.private" + "@mailhost.net", HOST, "Build" + "Box-7", VM, VM.upper(), f"~{USER}/x",
           USER + "@host", IDS, WIDE, HOST.replace("-", C(0x2011)), "".join(C(ord(c) + 0xFEE0) if c.isalnum() else C(0x2011) for c in VM),
           USER + C(0xFF20) + HOST, HOST.upper().replace("I", C(0x130)), HOST.replace("i", C(0x131)), "aws_secret_access_" + C(0x212A) + "ey",
           USER.replace("s", C(0x17F), 1) + "@x"]
ENCODINGS = ["latin-1", "utf-8", "utf-16-le", "utf-16-be", "utf-32-le", "utf-32-be"]
NOISE = "aZ09._-@~/\\:% \n\x00" + "".join(map(C, (0xFFFD, 0x212A, 0x17F, 0x131, 0x130, 0x2011, 0xFF20, 0xFF41, 0x4E00, 0xE9, 0x1F600)))


def _random_text(rng: random.Random, n: int, plants: list[str]) -> str:
    out = []
    while len(out) < n:
        if rng.random() < 0.08:
            p = rng.choice(plants)
            out += list(p[:rng.randint(1, len(p))] if rng.random() < 0.5 else p)
        else:
            out.append(rng.choice(NOISE))
    return "".join(out)


PATTERNS = [rx for _, rx, _, _ in RULES] + [r[1] for r in autorelease.compile_rules([
    {"id": "w", "kind": "word", "pattern": ["abc", "abd", "ab", "a.b", "k1", "si"]},
    {"id": "u", "kind": "user", "pattern": ["ops", "opsuser", "kid"]},
    {"id": "r1", "kind": "regex", "pattern": r"\bab\w*@[a-z]+\.net\b"},
    {"id": "r2", "kind": "regex", "pattern": r"a*"},                 # empty matches: whole text
    {"id": "r3", "kind": "regex", "pattern": r"(a)\1b"},             # a back reference: whole text
    {"id": "r4", "kind": "regex", "pattern": r"(?-i:Ab)c"},          # inline flags: whole text
    {"id": "r5", "kind": "regex", "pattern": r"xy$|ab\.?c"},         # an end anchor: no break characters
    {"id": "r6", "kind": "regex", "pattern": r"^ab|(?<=@)net"},
    {"id": "r7", "kind": "regex", "pattern": r"(?:ab){2,}|[0-9]+\.~"},
    {"id": "r8", "kind": "regex", "pattern": r"[^/]+@a|.{0,3}zz"},
    {"id": "r9", "kind": "regex", "pattern": r"(?=ab)a|é\w+|\Bsi\B"},
    {"id": "r10", "kind": "regex", "pattern": r"k(?![a-z])|[a-z]{2}\.[a-z]{2}(?=/)"},
], "test")]


def test_every_strategy_is_exercised():
    """the equivalence tests below cover each strategy (a run where everything fell back to the whole text would prove nothing)"""
    modes = {rulescan.scan_of(rx).mode for rx in PATTERNS}
    assert modes == {"cand", "seg", "presence", "full"}, modes
    assert all(rulescan.scan_of(rx).mode != "full" for _, rx, _, po in autorelease.BUILTIN + PRIVATE if not po)


@pytest.mark.parametrize("seed", range(6))
def test_finditer_is_rx_finditer(seed):
    """the same match objects (span and text) in the same order as rx.finditer, for every pattern, on texts made of literals, their
    pieces, case and look-alike variants (Kelvin sign, long s, dotless / dotted I), line breaks and non-ASCII noise"""
    rng = random.Random(seed)
    plants = PLANTED + ["abc", "abd", "a.b", "k1", "si", "ops", USER, "kid", "ab" + "@x.net", "xy", "net", "abab", "12.~", "zz", "é1",
                        "ab.cd/", "Ab", "aab"]
    for n in (0, 1, 7, 300, 3000):
        text = _random_text(rng, n, plants)
        folded = rulescan.fold(text)
        for rx in PATTERNS:
            want = [(m.span(), m.group(0)) for m in rx.finditer(text)]
            assert [(m.span(), m.group(0)) for m in rulescan.finditer(rx, text)] == want, (rx.pattern, n)
            assert [(m.span(), m.group(0)) for m in rulescan.finditer(rx, text, folded)] == want, (rx.pattern, n)


@pytest.mark.parametrize("chunk", [1 << 20, 64, 7])
def test_the_groups_are_the_decodings_in_order(monkeypatch, chunk):
    """a split scan reads every decoding once, in the order of the whole one (its hits joined in that order are the whole scan's); a
    long decoding's normal form made from the bytes piece by piece (small pieces here) is the old one"""
    monkeypatch.setattr(autorelease, "NORMAL_CHUNK", chunk)
    assert sum(autorelease.BINARY_GROUPS, ()) == autorelease.BINARY_DECODINGS
    rng = random.Random(4)
    data = rng.randbytes(999) + "x \uff4c\uff41\uff42".encode("utf-16-be")
    for _ in range(12):
        text = "".join(rng.choice(NORMAL_NOISE) for _ in range(rng.randint(0, 300)))
        data += rng.randbytes(rng.randrange(64)) + text.encode(rng.choice(ENCODINGS[1:]), "replace")
    assert [f for g in autorelease.BINARY_GROUPS for f in autorelease.binary_texts(data, g)] == list(reference_texts(data))


# the characters whose normalisation depends on their neighbours: combining marks (composing with what comes before, reordered among
# themselves), Hangul jamo (composed into syllables), compatibility forms, full-width letters, ligatures, the dotted I
NORMAL_NOISE = [C(x) for x in (0x301, 0x307, 0x323, 0x308, 0x344, 0x1100, 0x1161, 0x11A8, 0xAC00, 0xFF41, 0xFB01, 0x2460, 0x2474, 0x130,
                               0x2011, 0xFF20, 0x2217, 0x223C, 0x3392, 0x1D400, 0xFFFD, 0x0B47, 0x0B3E, 0x0CC6, 0x0CD5, 0x1E00)] + list("aeIoAz0(.@ ")


@pytest.mark.parametrize("chunk", [1, 2, 5, 64])
def test_normal_in_pieces_is_normal_of_the_whole(monkeypatch, chunk):
    """_normal cut before ASCII characters gives the whole text's normalisation (random texts dense in neighbour-dependent characters,
    and a real binary's decodings)"""
    monkeypatch.setattr(autorelease, "NORMAL_CHUNK", chunk)
    rng = random.Random(chunk)
    for _ in range(300):
        text = "".join(rng.choice(NORMAL_NOISE) for _ in range(rng.randint(0, 120)))
        assert autorelease._normal(text) == reference_normal(text), [hex(ord(c)) for c in text]
    exe = Path(sys.executable).resolve().read_bytes()[:1 << 16]
    for enc in autorelease.BINARY_DECODINGS:
        text = autorelease._decoding(exe, enc)
        assert autorelease._normal(text) == reference_normal(text), enc


@pytest.mark.parametrize("chunk", [3, 1 << 20])
def test_sparse_normalisation_is_normal_of_the_whole(monkeypatch, chunk):
    """mostly inert texts (U+FFFD, ASCII, inert characters beyond U+FFFF) with rare neighbour-dependent ones take _nfkc's sparse
    path, and give the whole text's normalisation"""
    monkeypatch.setattr(autorelease, "NORMAL_CHUNK", chunk)
    monkeypatch.setattr(autorelease, "SPARSE_MIN", 0)
    rng = random.Random(9)
    rare = NORMAL_NOISE + [C(0x1D400), C(0x1F100), C(0x2F800), C(0x1D165)]   # math bold A, enclosed 0, CJK compatibility, a combining stem
    inert = [C(0xFFFD), "a", "(", C(0x20000), C(0x10000), C(0x4E00)]
    taken = 0
    for _ in range(200):
        text = "".join(rng.choice(rare) if rng.random() < 0.01 else rng.choice(inert) for _ in range(rng.randint(100, 5000)))
        rx, astral = autorelease.non_inert()
        runs = [m.group(0) for m in rx.finditer(text) if not all(c >= C(0x10000) and c not in astral for c in m.group(0))]
        taken += len(runs) <= len(text) >> 6
        assert autorelease._normal(text) == reference_normal(text), [hex(ord(c)) for c in text if c not in inert]
    assert taken > 100, "the sparse path was taken"


def test_a_normal_form_equal_to_its_decoding_is_none(monkeypatch):
    """_normal_form compares the whole when the first changed piece cannot tell: pieces that change but join to the decoding itself
    give None (no NFKC form), as _normal(text) == text did"""
    monkeypatch.setattr(autorelease, "NORMAL_CHUNK", 1)
    fake = {"a": "a", "b": "", "c": "bc", "d": "d"}
    monkeypatch.setattr(autorelease, "_normal_piece", lambda t: fake.get(t, t))
    assert [b - a for a, b in autorelease._pieces(b"abcd", "latin-1")] == [1, 1, 1, 1]
    assert autorelease._normal_form(b"abcd", "latin-1") is None
    fake["d"] = "e"
    assert autorelease._normal_form(b"abcd", "latin-1") == "abce"


def test_fast_utf32_is_the_codec():
    """the UTF-32 decodings: every unit value class (valid, surrogate, above U+10FFFF) in both byte orders is the codec's 'replace'"""
    import struct
    rng = random.Random(8)
    for enc, f in (("utf-32-le", "<"), ("utf-32-be", ">")):
        for high in (0, 1, 0x10, 0x80, 0xFF):
            units = [high << 24 | b2 << 16 | b1 << 8 | rng.randrange(256) for b2 in range(256) for b1 in range(256)]
            data = struct.pack(f"{f}{len(units)}I", *units)
            assert autorelease._utf32_replace(data, enc) == data.decode(enc, "replace"), (enc, high)
        for n in (0, 1, 2, 3, 5, 7):
            data = rng.randbytes(n)
            assert autorelease._utf32_replace(data, enc) == data.decode(enc, "replace")
        data = struct.pack(f"{f}3I", 0x41, 0x10FFFF, 0x1F600)
        assert autorelease._utf32_replace(data, enc) == data.decode(enc)


@pytest.mark.parametrize("chunk", [rulescan.INDEX_CHUNK, 1, 7])
def test_the_literal_index_is_every_position_of_every_literal(monkeypatch, chunk):
    """one trie pass finds every (overlapping) start of every literal in the folded text, words that are prefixes of others included,
    literals across the cut between two folded pieces once"""
    monkeypatch.setattr(rulescan, "INDEX_CHUNK", chunk)
    rng = random.Random(2)
    for _ in range(40):
        words = sorted({"".join(rng.choice("ab.-@s") for _ in range(rng.randint(1, 4))) for _ in range(rng.randint(1, 9))})
        pats = [re.compile(re.escape(w), re.I) for w in words]
        text = "".join(rng.choice("ab.-@xAS" + C(0x17F)) for _ in range(rng.randint(0, 400)))
        index = rulescan.literal_index(text, pats)
        for w in words:
            assert index[w] == [i for i in range(len(text)) if re.match(re.escape(w), text[i:], re.I)], (w, words)


def test_fold_keeps_positions_and_every_case_variant():
    u = rulescan._universe()
    f = rulescan.fold(u)
    assert len(f) == len(u)
    for c in "abcdefghijklmnopqrstuvwxyz0123456789@.-_~/":
        for m in re.finditer(re.escape(c), u, re.I):
            assert f[m.start()] == c, (hex(ord(m.group(0))), c)


def _planted_blob(rng: random.Random, value: str, enc: str, pad: int) -> bytes:
    """random bytes, then the value between spaces in `enc`, `pad` bytes off the random part's alignment"""
    return rng.randbytes(rng.randrange(40)) + bytes(8 + pad) + f" {value} ".encode(enc) + bytes(8) + rng.randbytes(rng.randrange(40))


def test_binary_hits_are_the_reference_in_every_encoding_and_alignment():
    """a planted value in every encoding at every alignment: the fast path's lines are the reference's, line for line (and the rule
    is found: by its id, in the encoding it was written in)"""
    rng = random.Random(7)
    for value in PLANTED:
        for enc in ENCODINGS:
            try:
                raw = value.encode(enc)
            except UnicodeEncodeError:
                continue
            for pad in range(4):
                data = _planted_blob(rng, value, enc, pad)
                want = reference_hits("x/data.bin", None, RULES, check_path=False, data=data)
                got = autorelease.denylist_hits("x/data.bin", None, RULES, check_path=False, data=data)
                assert got == want, (value, enc, pad)
                if len(raw) > 8:
                    assert got, (value, enc, pad)


def test_binary_hits_are_the_reference_on_a_real_binary():
    """this Python's own executable (machine code, string tables, UTF-16 resources on Windows) with the planted values spread inside"""
    import sysconfig  # noqa: PLC0415
    ldlibrary = sysconfig.get_config_var("LDLIBRARY") or ""
    # lane MACGATE-1: a macOS framework build's executable is a 34 KB stub; its machine code is the framework's library
    # (PYTHONFRAMEWORKPREFIX/LDLIBRARY, e.g. Python.framework/Versions/3.12/Python)
    candidates = [Path(sysconfig.get_config_var(var) or "", ldlibrary) for var in ("LIBDIR", "PYTHONFRAMEWORKPREFIX") if sysconfig.get_config_var(var)]
    lib = next((c for c in candidates if c.is_file() and c.stat().st_size > 1 << 19), Path(sys.executable).resolve())
    exe = lib.read_bytes()[:1 << 19]
    assert len(exe) == 1 << 19
    rng = random.Random(3)
    for k in range(3):
        cut = bytearray(exe[k << 17:(k + 2) << 17])
        for value in rng.sample(PLANTED, 8):
            at = rng.randrange(len(cut))
            cut[at:at] = value.encode(rng.choice(ENCODINGS[1:]))
        data = bytes(cut)
        assert autorelease.denylist_hits("lib/x.so", None, RULES, data=data) == reference_hits("lib/x.so", None, RULES, data=data)


@pytest.mark.parametrize("metadata", [False, True])
def test_text_hits_are_the_reference(metadata):
    rng = random.Random(11 + metadata)
    for n in (5, 200, 4000):
        text = _random_text(rng, n, PLANTED).replace("\x00", " ")
        for path in ("docs/a.md", "docs/\uff4c\uff41\uff42-vm-q.txt", "engine/tests/x.cpp"):
            assert autorelease.denylist_hits(path, text, RULES, metadata=metadata) == \
                reference_hits(path, text, RULES, metadata=metadata), (n, path)


def test_name_hits_are_the_reference():
    """file and member names (every rule, path-only ones included, through the name prefilter): the reference's lines; the names of
    test_autorelease's deny cases and random ones with the planted values inside"""
    from test_autorelease import DENY_CASES
    rng = random.Random(12)
    paths = [p for p, _, _ in DENY_CASES] + ["docs/\uff4c\uff41\uff42-vm-q.txt", "a.zip:x/" + HOST + "/y.bin", "x.tar.gz[gzip]:" + USER]
    for _ in range(300):
        paths.append("/".join(rng.choice(["docs", "tools", "services/online", "a.zip:x", "b[gzip]", rng.choice(PLANTED),
                                           "runbook-" + rng.choice("ab"), "x.log", "AGENTS.md"]) for _ in range(rng.randint(1, 4))))
    for path in paths:
        assert autorelease.denylist_hits(path, None, RULES) == reference_hits(path, None, RULES), path


def test_a_sabotaged_fast_path_is_caught(monkeypatch):
    """the oracle is independent of the fast path: a fold that forgot the long s, or occurrences that lose the last one, change the
    lines, and the equivalence check fails"""
    data = b"\x00" + (USER.replace("s", C(0x17F), 1) + "@x aws_secret_access_" + C(0x212A) + "ey " + HOST).encode("utf-16-le") + b"\x00"
    want = reference_hits("x.bin", None, RULES, data=data)
    assert {"accounts", "aws-account", "hosts"} <= {h.split("(")[1].split(")")[0] for h in want}, want
    assert autorelease.denylist_hits("x.bin", None, RULES, data=data) == want
    with monkeypatch.context() as m:
        m.setattr(rulescan, "_SPECIALS", [])
        assert autorelease.denylist_hits("x.bin", None, RULES, data=data) != want
    real = rulescan.Scan.occurrences
    with monkeypatch.context() as m:
        m.setattr(rulescan.Scan, "occurrences", lambda self, text, folded, index=None: real(self, text, folded, index)[:-1])
        assert autorelease.denylist_hits("x.bin", None, RULES, data=data) != want
    assert autorelease.denylist_hits("x.bin", None, RULES, data=data) == want


def _tar_gz(members: dict[str, bytes]) -> bytes:
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as t:
        for name, data in members.items():
            info = tarfile.TarInfo(name)
            info.size, info.mode = len(data), 0o644
            t.addfile(info, io.BytesIO(data))
    return gzip.compress(buf.getvalue(), mtime=0)


def _assets(tmp_path: Path) -> list[Path]:
    """two release archives: every planted value in every encoding and alignment in a member of a tar.gz inside the zip (depth 3), the
    same 1.5 MB library in both archives and twice in one (one scan of it), plus a clean member and a member name a rule matches"""
    rng = random.Random(5)
    big = bytearray(rng.randbytes(3 << 19))
    leaks = {}
    for i, value in enumerate(PLANTED):
        for enc in ENCODINGS:
            try:
                raw = value.encode(enc)
            except UnicodeEncodeError:
                continue
            for pad in range(4):
                leaks[f"p/{i}-{enc}-{pad}.bin"] = b"\xfe" + bytes(8 + pad) + f" {value} ".encode(enc) + bytes(8) + b"\xfe"
    at = len(big) // 2
    big[at:at] = f"x {USER}@relay x".encode("utf-16-be")
    inner = _tar_gz({**leaks, "lib/libbig.so": bytes(big), "lib/clean.bin": rng.randbytes(5000)})
    a = tmp_path / "openbfme-v0.4.9-linux-x64.zip"
    a.write_bytes(zip_of({"openbfme/data.tar.gz": inner, "openbfme/libbig.so": bytes(big), f"openbfme/{HOST}.txt": b"x\n"}))
    b = tmp_path / "openbfme-v0.4.9-windows-x64.zip"
    b.write_bytes(zip_of({"openbfme/libbig.so": bytes(big), "openbfme/other.bin": rng.randbytes(1 << 20 | 7)}))
    return [a, b]


@pytest.fixture(scope="module")
def assets(tmp_path_factory):
    return _assets(tmp_path_factory.mktemp("rulescan-assets"))


@pytest.fixture(scope="module")
def reference_assets(assets):
    with pytest.MonkeyPatch.context() as m:
        m.setattr(autorelease, "denylist_hits", reference_hits)
        return autorelease.asset_problems(assets, autorelease.BUILTIN + PRIVATE, {})


@pytest.mark.parametrize("split", [False, True])
def test_parallel_gate_is_the_sequential_reference(assets, reference_assets, monkeypatch, split):
    """asset_problems in 3 workers (deduplicated, large members off-process, split: the large library's decodings in BINARY_GROUPS on
    several workers) gives the reference's lines in the reference's order: the reference is the pre-PRIVSPEED-1 denylist_hits patched
    into the same walk"""
    if split:
        monkeypatch.setattr(autorelease._Scans, "SPLIT", 1 << 20)
    rules = autorelease.BUILTIN + PRIVATE
    want = reference_assets
    assert want and not _leaks("\n".join(autorelease.sanitize(p, rules) for p in want))
    assert autorelease.asset_problems(assets, rules, {}) == want
    calls = []
    real = autorelease.binary_rule_hits
    monkeypatch.setattr(autorelease, "binary_rule_hits", lambda data, r, *g: calls.append(len(data)) or real(data, r, *g))
    with autorelease.parallel_scans(3) as state:
        got = autorelease.asset_problems(assets, rules, {})
        assert any(isinstance(p, autorelease._Pending) for p in got) and state.pool is not None
        parts = sorted(len(j.parts) for j in state.done.values() if isinstance(j, autorelease._Joined))
        assert parts[-1] == (len(autorelease.BINARY_GROUPS) if split else 1), parts
        got = autorelease.resolve(got)
    assert got == want
    big = [n for n in calls if n >= 3 << 19]
    assert big == [], "the large members are scanned in the workers"
    # every planted member is reported, by name, with the rule that must catch it
    text = "\n".join(want)
    for value_i, value in enumerate(PLANTED):
        for enc in ENCODINGS:
            if len(value.encode(enc, "ignore")) > 8 and value.encode(enc, "ignore").decode(enc) == value:
                for pad in range(4):
                    assert f"p/{value_i}-{enc}-{pad}.bin" in text, (value, enc, pad)
    for copy in ("linux-x64.zip:openbfme/data.tar.gz[gzip]:lib/libbig.so", "linux-x64.zip:openbfme/libbig.so",
                 "windows-x64.zip:openbfme/libbig.so"):
        assert f"denylisted (accounts) in openbfme-v0.4.9-{copy} (utf-16-be" in text, copy   # each copy reported, by its own name


def test_duplicates_are_scanned_once(assets, monkeypatch):
    """the library in three places is scanned once inside parallel_scans (and three times outside it, as before)"""
    size = (3 << 19) + len(f"x {USER}@relay x".encode("utf-16-be"))
    calls = []
    real = autorelease.binary_rule_hits
    monkeypatch.setattr(autorelease, "binary_rule_hits", lambda data, r, *g: calls.append(len(data)) or real(data, r, *g))
    with autorelease.parallel_scans(1):
        inside = autorelease.resolve(autorelease.asset_problems(assets, RULES, {}))
    assert calls.count(size) == 1, calls
    calls.clear()
    assert autorelease.asset_problems(assets, RULES, {}) == inside and calls.count(size) == 3


def test_the_memory_budget_holds_tasks_back_and_changes_nothing(assets, monkeypatch):
    """a budget below one task runs the tasks one at a time (one always runs: no deadlock), the lines are the same; within a budget
    several run together; never more than the job count"""
    want = autorelease.asset_problems(assets, RULES, {})
    monkeypatch.setattr(autorelease._Scans, "SPLIT", 1 << 20)
    seen = []
    real = autorelease._Scans._pump

    def pump(self):
        real(self)
        seen.append((len(self.running), self.used, self.budget))
    monkeypatch.setattr(autorelease._Scans, "_pump", pump)
    for budget, most in ((1, 1), (1 << 40, 3)):
        seen.clear()
        state = autorelease._Scans(3, budget)
        monkeypatch.setattr(autorelease, "_SCAN_STATE", state)
        try:
            got = autorelease.resolve(autorelease.asset_problems(assets, RULES, {}))
        finally:
            state.pool.shutdown(wait=True)
            monkeypatch.setattr(autorelease, "_SCAN_STATE", None)
        assert got == want
        assert max(n for n, _, _ in seen) == most, (budget, seen)
        assert all(n <= 1 or used <= b for n, used, b in seen)


def test_the_memory_budget_setting(monkeypatch):
    monkeypatch.setenv("OPENBFME_PRIVACY_MEMORY_MB", "3000")
    assert autorelease.scan_memory() == 3000 << 20
    for bad in ("x", "255", "-1"):
        monkeypatch.setenv("OPENBFME_PRIVACY_MEMORY_MB", bad)
        with pytest.raises(SystemExit):
            autorelease.scan_memory()
    monkeypatch.delenv("OPENBFME_PRIVACY_MEMORY_MB")
    assert 512 << 20 <= autorelease.scan_memory() <= 16 << 30
    for bad in ("0", "x", "-2"):
        monkeypatch.setenv("OPENBFME_PRIVACY_JOBS", bad)
        with pytest.raises(SystemExit):
            autorelease.scan_jobs()


def test_a_failed_worker_fails_the_gate_and_quotes_nothing(assets, monkeypatch, capsys):
    """a worker that raises (here: a rule it cannot compile, whose pattern holds a private value) fails the gate with the error's type
    only; an unresolved line is never printable"""
    real = autorelease._rule_spec
    monkeypatch.setattr(autorelease, "_rule_spec", lambda rules: real(rules) + (("x", f"({USER}@{HOST}", 0, False),))
    with pytest.raises(SystemExit) as e:
        with autorelease.parallel_scans(2):
            autorelease.resolve(autorelease.asset_problems(assets, RULES, {}))
    assert f"worker failed ({re.error.__name__})" in str(e.value) and not _leaks(str(e.value)), e.value
    out = capsys.readouterr()
    assert not _leaks(out.out + out.err)
    p = autorelease._Pending(lambda: ["x"])
    with pytest.raises(RuntimeError):
        str(p)
    with pytest.raises(RuntimeError):
        f"{p}"


def test_the_gate_command_is_the_same_in_every_job_count(tmp_path, assets):
    """privacy-gate --jobs 1 / 4 / OPENBFME_PRIVACY_JOBS: the same lines and exit code; a bad job count is refused"""
    import json
    import subprocess
    from test_autorelease import TEST_DENYLIST
    deny = tmp_path / "deny.json"
    deny.write_text(json.dumps(TEST_DENYLIST))
    notes = tmp_path / "notes.md"
    notes.write_text("OpenBFME v0.4.9\n")
    base = [sys.executable, str(HERE / "autorelease.py"), "privacy-gate", "--no-tree", "--text", str(notes), "--assets", *map(str, assets),
            "--denylist", str(deny), "--templates", str(tmp_path / "none")]
    outs = []
    for extra, env in (([], {"OPENBFME_PRIVACY_JOBS": "1"}), (["--jobs", "4"], {}), ([], {"OPENBFME_PRIVACY_JOBS": "3"})):
        r = subprocess.run(base + extra, capture_output=True, text=True, env={**__import__("os").environ, **env})
        assert r.returncode == 1 and "METADATA AUDIT FAILED" in r.stdout and not _leaks(r.stdout + r.stderr), r.stdout + r.stderr
        outs.append(r.stdout)
    assert outs[0] == outs[1] == outs[2]
    assert "denylisted (accounts)" in outs[0] and "denylisted (hosts)" in outs[0]
    r = subprocess.run(base + ["--jobs", "0"], capture_output=True, text=True)
    assert r.returncode == 2 and "--jobs is at least 1" in r.stderr
    r = subprocess.run(base, capture_output=True, text=True, env={**__import__("os").environ, "OPENBFME_PRIVACY_JOBS": "x"})
    assert r.returncode == 1 and "OPENBFME_PRIVACY_JOBS" in r.stdout + r.stderr
