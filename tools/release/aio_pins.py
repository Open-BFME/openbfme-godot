"""Lane AIO-1: the launcher's pins for the download from the All In One BFME Launcher's service (docs/AIO.md,
launcher/scripts/core/aio_install.gd).

    python3 tools/release/aio_pins.py           (rewrites launcher/scripts/core/aio_pins.gd from tools/release/aio_package_files.json)
    python3 tools/release/aio_pins.py --check   (exit 1 when the committed file is not what the tables give; tools/release/test_aio.py)
    python3 tools/release/aio_pins.py --capture <install dir> <manifest dir>
                                                (rewrites tools/release/aio_package_files.json from a verified download: <install dir>/RotWK,
                                                 <install dir>/BFME2, <install dir>/BFME1 and the service's manifests <manifest dir>/original-RotWK.raw, ...)

Every file the launcher installs is pinned (Sol r1: the 399 files outside the archive tables trusted the manifest). The source is
tools/release/aio_package_files.json: per package, the relative name, MD5 and size of every English file, captured on 2026-10-10 from
the download that lane AIO-1 r2 verified against the service's manifests (612 of 612 MD5s) and the archive tables (213 of 213). Only
names, MD5s and sizes: no retail bytes. Every archive of engine/data/retail-archives must be in it with the same MD5 and size (checked
here, so the two tables cannot drift).

The launcher is its own Godot project and cannot read engine/data or tools/, so the pins are a generated GDScript constant (a script is
exported with the project; a JSON file would need the export presets changed). The AIO numbering of the games (BFME2 1, RotWK 2) and its
folder names (BFME2, RotWK) are the AIO launcher's (BfmeKit BfmeRegistryManager.GameNameToInt, AllInOneLauncher
Core/BfmeSyncManager.InstallGame).
"""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TABLES = REPO / "engine" / "data" / "retail-archives"
FILES = Path(__file__).resolve().parent / "aio_package_files.json"
OUT = REPO / "launcher" / "scripts" / "core" / "aio_pins.gd"

# table -> (key the launcher reports, AIO game number, AIO folder name, label for the player, the service's file owner (pinned: the
# files' URLs are <files host>/<owner>/<md5>; the owner's decision 2026-10-10))
OWNER = "bdc6bc60-73f9-4833-9069-3076aff26418"
GAMES = [("rotwk-201-english-archives.json", "rotwk", 2, "RotWK", "The Rise of the Witch-king 2.01", OWNER),
         ("bfme2-106-english-archives.json", "bfme2", 1, "BFME2", "The Battle for Middle-earth II 1.06", OWNER)]
# community PR #36 (Hailey Tribble), restored on the owner's decision of 2026-10-18 ("add it as OPTIONAL for the BFME1 menu; we don't
# distribute it, it's downloaded from BFME Arena"): the original BFME1 1.03 package, 346 English files. It is NOT in PACKAGES (the base
# download stays RotWK + BFME2) but in BFME1_PACKAGES, which the launcher offers only when Battle for Middle-earth I is chosen. Its
# metadata (guid, name, language, where it was captured) is tools/release/aio_bfme1_package.json: the engine has no BFME1 archive table,
# so there is nothing to cross-check its archives against. AIO numbers BFME1 as game 0 (BfmeRegistryManager.GameNameToInt).
BFME1_META = Path(__file__).resolve().parent / "aio_bfme1_package.json"
# issue #49: the base packages' other languages (tools/release/aio_languages.py), rendered into each base package as "languages"
LANGUAGES = Path(__file__).resolve().parent / "aio_language_files.json"
OPTIONAL = [(BFME1_META.name, "bfme1", 0, "BFME1", "Battle for Middle-earth I 1.03", OWNER)]


def package_table(table: str) -> dict:
    """an engine archive table (engine/data/retail-archives) or, for an optional package, its metadata next to this script"""
    return json.loads((BFME1_META if table == BFME1_META.name else TABLES / table).read_text())


def load_files() -> dict:
    return json.loads(FILES.read_text())


def check_tables(files: dict) -> list[str]:
    """every archive of the engine's tables is pinned with the same MD5 and size"""
    errors = []
    expected = {package_table(table)["package"]["guid"] for table, *_ in GAMES + OPTIONAL}
    if set(files) != expected:
        errors.append(f"{FILES.name} has the packages {sorted(files)}, expected {sorted(expected)}")
    for table, *_ in OPTIONAL:
        guid = package_table(table)["package"]["guid"]
        names = [str(f[0]).lower() for f in files.get(guid, [])]
        if not names:
            errors.append(f"{guid}: no pinned files in {FILES.name}")
        if len(set(names)) != len(names):
            errors.append(f"{guid}: a file is pinned twice in {FILES.name}")
        for f in files.get(guid, []):
            if not (len(f) == 3 and isinstance(f[0], str) and isinstance(f[1], str) and len(f[1]) == 32
                    and all(c in "0123456789abcdef" for c in f[1]) and isinstance(f[2], int) and f[2] >= 0):
                errors.append(f"{guid}: the pin {f} is not [name, md5, size]")
    for table, *_ in GAMES:
        t = json.loads((TABLES / table).read_text())
        pins = {f[0].lower(): f for f in files.get(t["package"]["guid"], [])}
        for a in t["archives"]:
            f = pins.get(a["path"].lower())
            if f is None or f[1] != a["md5"] or f[2] != a["size"]:
                errors.append(f"{t['package']['guid']}: {a['path']} is {f} in {FILES.name}, {a['md5']} {a['size']} in {table}")
    return errors


def render() -> str:
    files = load_files()
    errors = check_tables(files)
    if errors:
        raise SystemExit("AIO PINS DISAGREE WITH THE ARCHIVE TABLES:\n" + "\n".join(errors))
    out = ["## GENERATED by tools/release/aio_pins.py from tools/release/aio_package_files.json: do not edit (lane AIO-1).",
           "## Every file the launcher's All In One BFME Launcher download installs, with its MD5 and size (scripts/core/aio_install.gd).",
           "extends RefCounted", ""]
    for const, games, note in (("PACKAGES", GAMES, "## The base download (the Game files screen's Download)."),
                               ("BFME1_PACKAGES", OPTIONAL, "## Community PR #36: the optional BFME1 download, offered only when Battle for Middle-earth I is chosen.")):
        out += [note, f"const {const} := ["]
        for table, key, game, folder, label, owner in games:
            t = package_table(table)
            pkg = t["package"]
            rows = files[pkg["guid"]]
            out.append(f'\t{{"key": "{key}", "guid": {json.dumps(pkg["guid"])}, "name": {json.dumps(pkg["name"])}, "game": {game}, '
                       f'"folder": "{folder}", "label": "{label}", "owner": "{owner}", "bytes": {sum(r[2] for r in rows)}, '
                       f'"language": {json.dumps(t["language"])}, "policySha256": {json.dumps(t["policySha256"])}, "files": [')
            for name, md5, size in rows:
                out.append(f'\t\t[{json.dumps(name)}, "{md5}", {int(size)}],')
            if const == "PACKAGES":
                # issue #49: per language code, the English files it drops and its own files (name, MD5, size; size 0: the service does not know it)
                langs = json.loads(LANGUAGES.read_text()).get(pkg["guid"], {})
                out.append("\t], \"languages\": {")
                for code, d in langs.items():
                    out.append(f'\t\t{json.dumps(code)}: {{"name": {json.dumps(d["name"])}, "drop": {json.dumps(d["drop"])}, "add": {json.dumps(d["add"])}}},')
                out.append("\t}},")
            else:
                out.append("\t]},")
        out.append("]")
    return "\n".join(out) + "\n"


def capture(install: Path, manifests: Path) -> dict:
    """the pins of a verified download: every English file of each package's manifest, hashed from the disk; the manifest's MD5 must agree
    (its Size is not used: the service lists some files with 0)"""
    out = {}
    for table, key, game, folder, label, owner in GAMES + OPTIONAL:
        t = package_table(table)
        guid = t["package"]["guid"]
        m = json.loads((manifests / f"{guid}.raw").read_text())
        rows = []
        for f in m["Files"]:
            langs = f["Language"].split()
            if not (f["Language"] in ("", "ALL") or t["language"] in langs):
                continue
            rel = f["Name"].replace("\\", "/")
            data = install / folder / rel
            h = hashlib.md5()
            with open(data, "rb") as fh:
                for b in iter(lambda: fh.read(1 << 22), b""):
                    h.update(b)
            if h.hexdigest() != f["Md5"]:
                raise SystemExit(f"{data}: MD5 {h.hexdigest()}, the manifest says {f['Md5']}")
            rows.append([rel, f["Md5"], data.stat().st_size])
        rows.sort(key=lambda r: r[0])
        out[guid] = rows
    return out


def main(argv: list[str]) -> int:
    if argv[:1] == ["--capture"]:
        files = capture(Path(argv[1]), Path(argv[2]))
        FILES.write_text("{\n" + ",\n".join(f'  {json.dumps(g)}: [\n' + ",\n".join("    " + json.dumps(r) for r in rows) + "\n  ]"
                                           for g, rows in files.items()) + "\n}\n")
        print(f"wrote {FILES.relative_to(REPO)}: " + ", ".join(f"{g} {len(r)} files" for g, r in files.items()))
        return 0
    text = render()
    if "--check" in argv:
        if not OUT.exists() or OUT.read_text() != text:
            print(f"AIO PINS STALE: {OUT.relative_to(REPO)} differs from {FILES.name}; run python3 tools/release/aio_pins.py")
            return 1
        print("AIO PINS OK")
        return 0
    OUT.write_text(text)
    print(f"wrote {OUT.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
