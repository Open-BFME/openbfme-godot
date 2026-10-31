"""Lane AIO-1: the launcher's opt-in download of the game files from the All In One BFME Launcher's service (docs/AIO.md,
launcher/scripts/core/aio_install.gd), against a local stand-in for that service (launcher_test_server.ReleaseServer playing both of its
hosts: the workshop API's /api/workshop/download?guid=... and the content-addressed /<owner>/<md5> files) with made-up files and made-up
pinned tables (--aio-pins): no retail bytes and no request to the real service.

The launcher tests skip (loudly) without Godot 4.7.2; test_aio_pins_are_current always runs.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
LAUNCHER = REPO / "launcher"
sys.path.insert(0, str(HERE))

import aio_pins  # noqa: E402
from launcher_test_server import ReleaseServer  # noqa: E402

GODOT = os.environ.get("GODOT", "godot")
OWNER = "bdc6bc60-73f9-4833-9069-3076aff26418"


def _godot_ok() -> bool:
    try:
        return subprocess.run([GODOT, "--version"], capture_output=True, text=True, timeout=60).stdout.startswith("4.7.2.")
    except (OSError, subprocess.TimeoutExpired):
        return False


needs_godot = pytest.mark.skipif(not _godot_ok(), reason="SKIP AIO launcher tests: Godot 4.7.2 not found (set GODOT)")


def test_aio_pins_are_current():
    """every downloaded file is pinned (Sol r1): tools/release/aio_package_files.json has the 315 + 297 English files, holds every archive
    of engine/data/retail-archives with the same MD5 and size, and the launcher's aio_pins.gd is generated from it"""
    files = aio_pins.load_files()
    assert {g: len(rows) for g, rows in files.items()} == {"original-RotWK": 315, "original-BFME2": 297, "original-BFME1": 346}
    assert aio_pins.check_tables(files) == []
    for rows in files.values():
        names = [r[0].lower() for r in rows]
        assert len(set(names)) == len(names)
        assert all(len(r) == 3 and len(r[1]) == 32 and isinstance(r[2], int) and r[2] >= 0 for r in rows)
    assert aio_pins.main(["--check"]) == 0
    # a drift between the two tables is an error, not a silent pick
    broken = json.loads(json.dumps(files))
    broken["original-RotWK"] = [r if r[0] != "_patch201.big" else [r[0], "0" * 32, r[2]] for r in broken["original-RotWK"]]
    assert any("_patch201.big" in e for e in aio_pins.check_tables(broken))


def _gd_packages(text: str, const: str) -> dict:
    """the rows of one constant of the generated aio_pins.gd, read from its text (not through aio_pins.render)"""
    block = text.split(f"const {const} := [", 1)[1].split("\n]\n", 1)[0]
    out, guid = {}, None
    for line in block.splitlines():
        if line.startswith("\t{"):
            guid = json.loads("{" + line.strip()[1:].split(', "files": [')[0] + "}")["guid"]
            out[guid] = []
        elif line.startswith("\t\t["):
            out[guid].append(json.loads(line.strip().rstrip(",")))
    return out


def test_aio_bfme1_pins_match_the_package_json_and_stay_optional():
    """community PR #36 (restored on the owner's decision, 2026-10-18): the 346 BFME1 pins in the launcher are exactly the package json's,
    with its metadata (aio_bfme1_package.json), and only in BFME1_PACKAGES: never in the base download PACKAGES"""
    text = (LAUNCHER / "scripts" / "core" / "aio_pins.gd").read_text()
    files = json.loads((HERE / "aio_package_files.json").read_text())
    meta = json.loads((HERE / "aio_bfme1_package.json").read_text())
    assert meta["package"] == {"guid": "original-BFME1", "name": "Vanilla (1.03)"} and meta["language"] == "EN"
    assert meta["source"] == "https://bfmeladder.com/api/workshop/download?guid=original-BFME1"
    base, optional = _gd_packages(text, "PACKAGES"), _gd_packages(text, "BFME1_PACKAGES")
    assert list(base) == ["original-RotWK", "original-BFME2"] and list(optional) == ["original-BFME1"]
    assert optional["original-BFME1"] == files["original-BFME1"]
    assert base == {g: files[g] for g in base}
    row = next(ln for ln in text.splitlines() if '"guid": "original-BFME1"' in ln)
    assert '"key": "bfme1"' in row and '"game": 0' in row and '"folder": "BFME1"' in row and f'"owner": "{OWNER}"' in row
    assert f'"bytes": {sum(r[2] for r in files["original-BFME1"])}' in row
    names = {r[0].lower() for r in files["original-BFME1"]}
    assert {"lotrbfme.exe", "worldbuilder.exe", "dsound.dll"} <= names
    # the checker refuses a BFME1 table that is missing, empty, malformed or has a name twice
    for broken, why in [({k: v for k, v in files.items() if k != "original-BFME1"}, "expected"),
                        ({**files, "original-BFME1": []}, "no pinned files"),
                        ({**files, "original-BFME1": files["original-BFME1"] + [files["original-BFME1"][0]]}, "pinned twice"),
                        ({**files, "original-BFME1": [["x.big", "zz", 1]]}, "not [name, md5, size]"),
                        ({**files, "original-BFME9": [["x.big", "0" * 32, 1]]}, "expected")]:
        assert any(why in e for e in aio_pins.check_tables(broken)), why


# ---- the stand-in service ---------------------------------------------------------------------------------------------------------------

def _file(name: str, data: bytes, language: str = "ALL", pinned: bool = False, size: int | None = None) -> dict:
    return {"name": name, "data": data, "language": language, "pinned": pinned, "size": len(data) if size is None else size}


def packages() -> list[dict]:
    """two made-up base packages shaped like original-RotWK / original-BFME2 (an English install of each)"""
    r = os.urandom
    return [
        {"key": "rotwk", "guid": "original-RotWK", "name": "Vanilla (2.01)", "game": 2, "folder": "RotWK", "files": [
            _file("_patch201.big", r(150000), pinned=True),
            _file("lang\\English.big", r(40000), "EN", pinned=True),
            _file("lang\\EnglishAudio.big", r(30000), "EN NL NO PL SV TR", pinned=True, size=0),  # the service's Size 0
            _file("lang\\French.big", r(20000), "FR"),
            _file("data\\movies\\EALogo.vp6", r(25000)),
            _file("game.dat", r(12000)),
        ]},
        {"key": "bfme2", "guid": "original-BFME2", "name": "Vanilla (1.06)", "game": 1, "folder": "BFME2", "files": [
            _file("INI.big", r(60000), pinned=True),
            _file("lang\\German.big", r(10000), "DE"),
            _file("lotrbfme2.exe", r(9000)),
        ]},
        # community PR #36: the optional BFME1 package (its pins ride along in every test: the base download must never take it)
        {"key": "bfme1", "guid": "original-BFME1", "name": "Vanilla (1.03)", "game": 0, "folder": "BFME1", "files": [
            _file("ini.big", r(50000)),
            _file("lang\\english.big", r(20000), "EN"),
            _file("lang\\french.big", r(8000), "FR"),
            _file("lotrbfme.exe", r(16000)),
            _file("dsound.dll", r(7000)),
        ]},
    ]


def manifest(pkg: dict, origin: str) -> dict:
    files = []
    for f in pkg["files"]:
        md5 = hashlib.md5(f["data"]).hexdigest()
        files.append({"Guid": md5, "Name": f["name"], "Url": f"{origin}/{OWNER}/{md5}", "Md5": md5, "Language": f["language"],
                      "Size": f["size"]})
    return {"Guid": pkg["guid"], "Name": pkg["name"], "Version": "1.0.0", "Description": "", "Author": "EA", "Owner": OWNER,
            "Game": pkg["game"], "Type": 0, "CreationTime": 1747765137549, "Files": files, "Maps": [], "Dependencies": []}


def pins(pkgs: list[dict]) -> list[dict]:
    return [{"key": p["key"], "guid": p["guid"], "name": p["name"], "game": p["game"], "folder": p["folder"], "label": p["folder"] + " test",
             "owner": OWNER, "bytes": sum(len(f["data"]) for f in p["files"]), "language": "EN", "policySha256": "0" * 64,
             "files": [[f["name"].replace("\\", "/"), hashlib.md5(f["data"]).hexdigest(), len(f["data"])] for f in p["files"]
                       if f["language"] == "ALL" or "EN" in f["language"].split()]}
            for p in pkgs]


def manifest_path(guid: str) -> str:
    return f"/api/workshop/download?guid={guid}"


def file_path(data: bytes) -> str:
    return f"/{OWNER}/{hashlib.md5(data).hexdigest()}"


class Service:
    def __init__(self, server: ReleaseServer, tmp: Path, pkgs: list[dict] | None = None) -> None:
        self.server, self.tmp = server, tmp
        self.pkgs = packages() if pkgs is None else pkgs
        self.manifests = {p["guid"]: manifest(p, server.origin) for p in self.pkgs}
        self.pins_file = tmp / "pins.json"
        self.pins_file.write_text(json.dumps(pins(self.pkgs)))
        self.publish()
        for p in self.pkgs:
            for f in p["files"]:
                server.files[file_path(f["data"])] = f["data"]

    def publish(self) -> None:
        for guid, m in self.manifests.items():
            self.server.files[manifest_path(guid)] = json.dumps(m).encode()

    def entry(self, guid: str, name: str) -> dict:
        return next(f for f in self.manifests[guid]["Files"] if f["Name"] == name)

    def data(self, name: str) -> bytes:
        return next(f["data"] for p in self.pkgs for f in p["files"] if f["name"] == name)

    def file_requests(self) -> list[tuple[str, dict]]:
        return [(p, h) for p, h in self.server.requests if p.startswith(f"/{OWNER}/")]


@pytest.fixture
def service(tmp_path):
    s = ReleaseServer()
    yield Service(s, tmp_path)
    s.stop()


@pytest.fixture(scope="module", autouse=True)
def imported():
    if _godot_ok():
        for _ in range(2):
            subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--import"], capture_output=True, timeout=300)


class Run:
    def __init__(self, code: int, out: str) -> None:
        self.code, self.out = code, out
        self.lines = [ln for ln in out.splitlines() if ln.startswith("LAUNCHER")]

    def has(self, text: str) -> bool:
        return any(text in ln for ln in self.lines)

    def __repr__(self) -> str:
        return f"exit {self.code}\n" + "\n".join(self.lines) + ("\n" + self.out[-2000:] if not self.lines else "")


def launcher_data(tmp: Path) -> Path:
    """the launcher's user:// (custom_user_dir_name OpenBFMELauncher) of a launch(): $XDG_DATA_HOME/OpenBFMELauncher; lane MACGATE-1: Godot on
    macOS ignores XDG_*, there it is $HOME/Library/Application Support/OpenBFMELauncher, so launch() gives the run a HOME of its own"""
    if sys.platform == "darwin":
        return tmp / "home" / "Library" / "Application Support" / "OpenBFMELauncher"
    return tmp / "data" / "OpenBFMELauncher"


def launch(tmp: Path, service: Service, *args: str) -> Run:
    env = dict(os.environ, XDG_DATA_HOME=str(tmp / "data"), OPENBFME_GAME_USER_DIR=str(tmp / "game-user"))
    if sys.platform == "darwin":
        env["HOME"] = str(tmp / "home")
    cmd = [GODOT, "--headless", "--path", str(LAUNCHER), "--", f"--aio-base={service.server.origin}", f"--aio-pins={service.pins_file}", *args]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=180, stdin=subprocess.DEVNULL)
    return Run(r.returncode, r.stdout + r.stderr)


def target(tmp: Path) -> Path:
    return tmp / "games"


def install(tmp: Path, service: Service, *extra: str) -> Run:
    return launch(tmp, service, "--aio=on", f"--aio-install={target(tmp)}", "--aio-consent", *extra)


def on_disk(tmp: Path, folder: str, name: str) -> Path:
    return target(tmp) / folder / name.replace("\\", "/")


# ---- the tests --------------------------------------------------------------------------------------------------------------------------

@needs_godot
def test_aio_is_on_by_default_and_needs_consent(tmp_path, service):
    r = launch(tmp_path, service, f"--aio-install={target(tmp_path)}")
    assert r.code == 1 and r.has("has not agreed"), r  # on by default (DEFAULT_ENABLED), and nothing without the consent
    r = launch(tmp_path, service, "--aio=off", f"--aio-install={target(tmp_path)}", "--aio-consent")
    assert r.code == 1 and r.has("is off"), r
    r = launch(tmp_path, service, "--aio=on", f"--aio-install={target(tmp_path)}")
    assert r.code == 1 and r.has("has not agreed"), r
    r = launch(tmp_path, service, "--aio=on", "--aio-install=relative/folder", "--aio-consent")
    assert r.code == 1 and r.has("must be an absolute path"), r
    assert service.server.requests == [], "nothing is asked of the service before the setting, the consent and the folder"
    assert not target(tmp_path).exists()


@needs_godot
def test_aio_installs_both_games_and_checks_every_file(tmp_path, service):
    r = install(tmp_path, service)
    assert r.code == 0 and r.has("LAUNCHER AIO done: 7 files downloaded, 0 already there"), r
    # MACFIX-1 (issue #46): what the download takes of each package, then every finished file with the running byte count
    assert r.has("LAUNCHER AIO original-RotWK: the download takes 5 of the package's 6 files (EN; every one is pinned and matches)"), r
    assert r.has("LAUNCHER AIO original-BFME2: the download takes 2 of the package's 3 files (EN; every one is pinned and matches)"), r
    taken = [(p["folder"], f) for p in service.pkgs if p["key"] != "bfme1" for f in sorted(p["files"], key=lambda f: f["name"].replace("\\", "/"))
               if f["language"] == "ALL" or "EN" in f["language"].split()]
    total = sum(len(f["data"]) for _, f in taken)
    def milestones(how: str) -> list[str]:
        out, done = [], 0
        for i, (folder, f) in enumerate(taken, 1):
            done += len(f["data"])
            out.append(f"LAUNCHER AIO File {i} of 7, {folder}/{f['name'].replace(chr(92), '/')} {how} "
                       f"({done / 1e9:.2f} of {total / 1e9:.2f} GB, {int(done * 100.0 / total)}%)")
        return out
    assert [ln for ln in r.lines if " downloaded and checked (" in ln] == milestones("downloaded and checked"), r
    assert r.has("AIO consent given: The game files are downloaded from the servers of the All In One BFME Launcher"), r
    assert not (target(tmp_path) / "BFME1").exists()  # community PR #36: the optional BFME1 package is never part of it
    for p in service.pkgs:
        if p["key"] == "bfme1":
            continue
        for f in p["files"]:
            path = on_disk(tmp_path, p["folder"], f["name"])
            english = f["language"] == "ALL" or "EN" in f["language"].split()
            assert path.exists() == english, path
            if english:
                assert path.read_bytes() == f["data"], path
    assert not list(target(tmp_path).rglob("*.part"))
    record = json.loads((launcher_data(tmp_path) / "aio-install.json").read_text())
    assert record["folders"] == {"rotwk": str(target(tmp_path) / "RotWK"), "bfme2": str(target(tmp_path) / "BFME2")}
    # the first-run handoff (InstallLocator::readDownloaded)
    marker = (tmp_path / "game-user" / "downloaded-installs.cfg").read_text().splitlines()
    assert marker[1:] == ["SOURCE=All In One BFME Launcher service: original-RotWK, original-BFME2",
                          f"ROTWK_INSTALL={target(tmp_path) / 'RotWK'}", f"BFME2_INSTALL={target(tmp_path) / 'BFME2'}"], marker
    assert r.has("the game will offer these folders on its first start"), r
    asks = [h for p, h in service.server.requests if p.startswith("/api/workshop/download")]
    assert len(asks) == 2 and all(h.get("authaccountuuid") == "unauthenticated" and h.get("user-agent", "").endswith(" (+https://github.com/Open-BFME/openbfme-godot)")
                                  for h in asks), asks
    # the setting is remembered; a second run checks what is there and downloads nothing
    before = len(service.file_requests())
    r = launch(tmp_path, service, f"--aio-install={target(tmp_path)}", "--aio-consent")
    assert r.code == 0 and r.has("0 files downloaded, 7 already there"), r
    assert [ln for ln in r.lines if " already there and checked (" in ln] == milestones("already there and checked"), r
    assert len(service.file_requests()) == before


@needs_godot
def test_aio_a_pinned_archive_with_another_md5_downloads_nothing(tmp_path, service):
    e = service.entry("original-RotWK", "lang\\English.big")
    other = os.urandom(40000)
    md5 = hashlib.md5(other).hexdigest()
    e.update(Md5=md5, Guid=md5, Url=f"{service.server.origin}/{OWNER}/{md5}")
    service.server.files[file_path(other)] = other
    service.publish()
    r = install(tmp_path, service)
    assert r.code == 3 and r.has(f"lists lang/English.big with MD5 {md5}") and r.has("Nothing was downloaded"), r
    assert r.has("Try again later. Or install the games with the All In One BFME Launcher yourself (https://bfmeladder.com/download)"), r
    assert service.file_requests() == []


@needs_godot
@pytest.mark.parametrize("change,why", [
    (lambda m, o: m["Files"].append({**m["Files"][0], "Name": "__patch202.big"}), "lists __patch202.big, which OpenBFME's list of RotWK test does not have"),
    (lambda m, o: m["Files"].pop(0), "no longer lists _patch201.big"),
    (lambda m, o: m.update(Owner="someone-else"), "moved its files (owner 'someone-else'"),
    (lambda m, o: m["Files"][4].update(Name="..\\..\\escaped.dat"), "not a plain relative path"),
    (lambda m, o: m["Files"][4].update(Name="C:\\Windows\\x.dat"), "not a plain relative path"),
    (lambda m, o: m["Files"][4].update(Url="https://example.com/" + m["Files"][4]["Md5"]), "not at the pinned http://<address>"),  # the launcher log writes addresses as <address>
    (lambda m, o: m["Files"][1].update(Size=123), "lists lang/English.big with 123 bytes"),
    (lambda m, o: m["Files"][5].update(Md5="0" * 32, Url=f"{o}/{OWNER}/" + "0" * 32), "lists game.dat with MD5 00000000"),
    (lambda m, o: m["Files"].append({**m["Files"][5], "Name": "extra.txt"}), "lists extra.txt, which OpenBFME's list of RotWK test does not have"),
    (lambda m, o: m["Files"][5].update(Size=12000.5), "lists game.dat with the size 12000.5"),
    (lambda m, o: m["Files"][3].update(Size=-1), "with the size -1"),
    (lambda m, o: m.update(Game=2.5), "is no longer the base game package (Game 2.5"),
    (lambda m, o: m.update(Type=0.5), "Type 0.5"),
    (lambda m, o: m.update(Type=1), "is no longer the base game package"),
    (lambda m, o: m.update(Guid="official-2"), "answered with the package 'official-2'"),
])
def test_aio_a_changed_package_is_refused(tmp_path, service, change, why):
    change(service.manifests["original-RotWK"], service.server.origin)
    service.publish()
    r = install(tmp_path, service)
    assert r.code == 3 and r.has(why), r
    assert service.file_requests() == []
    assert not (tmp_path / "escaped.dat").exists() and not (target(tmp_path) / "escaped.dat").exists()


@needs_godot
def test_aio_a_corrupt_download_is_deleted(tmp_path, service):
    good = service.data("game.dat")
    service.server.files[file_path(good)] = bytes(len(good))  # same size, other bytes
    r = install(tmp_path, service)
    assert r.code == 3 and r.has("game.dat from the service has MD5") and r.has("it was deleted"), r
    dest = on_disk(tmp_path, "RotWK", "game.dat")
    assert not dest.exists() and not Path(str(dest) + ".part").exists()


@needs_godot
def test_aio_a_cut_download_resumes(tmp_path, service):
    data = service.data("_patch201.big")
    service.server.truncate[file_path(data)] = 50000
    r = install(tmp_path, service)
    assert r.code == 2 and r.has("resumes from here next time"), r
    part = Path(str(on_disk(tmp_path, "RotWK", "_patch201.big")) + ".part")
    assert part.stat().st_size == 50000
    r = install(tmp_path, service)
    assert r.code == 0 and r.has("resumed _patch201.big at 50000 bytes"), r
    ranges = [h.get("range") for p, h in service.server.requests if p == file_path(data)]
    assert ranges[-1] == "bytes=50000-", ranges
    assert on_disk(tmp_path, "RotWK", "_patch201.big").read_bytes() == data


@needs_godot
def test_aio_never_overwrites_a_file_it_did_not_write(tmp_path, service):
    mine = on_disk(tmp_path, "RotWK", "game.dat")
    mine.parent.mkdir(parents=True)
    mine.write_bytes(b"the player's own file")
    r = install(tmp_path, service)
    assert r.code == 3 and r.has("already exists and is not original-RotWK's file"), r
    assert mine.read_bytes() == b"the player's own file"


@needs_godot
def test_aio_redirects_only_to_allowed_hosts(tmp_path, service):
    data = service.data("game.dat")
    service.server.redirect[file_path(data)] = "https://example.com/elsewhere"
    r = install(tmp_path, service)
    assert r.code == 2 and r.has("refused URL 'https://example.com/elsewhere'") and r.has("All In One BFME Launcher host"), r


@needs_godot
@pytest.mark.parametrize("body,code,exit_code,why", [
    (b"<html>maintenance</html>", 200, 3, "is not the file list OpenBFME knows"),
    (b"{}", 404, 3, "answered HTTP 404 for the file list of original-RotWK"),
    (b"{}", 503, 2, "answered HTTP 503 for the file list of original-RotWK"),
])
def test_aio_an_answer_it_does_not_recognise_stops(tmp_path, service, body, code, exit_code, why):
    service.server.files[manifest_path("original-RotWK")] = body
    if code != 200:
        del service.server.files[manifest_path("original-RotWK")]
        service.server.status[manifest_path("original-RotWK")] = code
    r = install(tmp_path, service)
    assert r.code == exit_code and r.has(why) and r.has("Try again later"), r
    assert service.file_requests() == []


@needs_godot
def test_aio_a_file_that_disappeared_is_named(tmp_path, service):
    data = service.data("lotrbfme2.exe")
    del service.server.files[file_path(data)]
    r = install(tmp_path, service)
    assert r.code == 3 and r.has("The service no longer has BFME2/lotrbfme2.exe (") and r.has("Try again later"), r
    assert not (tmp_path / "game-user" / "downloaded-installs.cfg").exists()


@needs_godot
def test_aio_a_size_0_entry_gets_the_pinned_size(tmp_path, service):
    """lang\\EnglishAudio.big is listed with Size 0 (as the real service does): the pinned size is used, a longer file is refused"""
    data = service.data("lang\\EnglishAudio.big")
    service.server.files[file_path(data)] = data + b"x"
    r = install(tmp_path, service)
    assert r.code == 3 and r.has("RotWK/lang/EnglishAudio.big: the service answered something OpenBFME does not recognise"), r
    service.server.files[file_path(data)] = data
    r = install(tmp_path, service)
    assert r.code == 0, r
    assert on_disk(tmp_path, "RotWK", "lang\\EnglishAudio.big").read_bytes() == data


@needs_godot
def test_aio_an_existing_file_needs_the_pinned_size_too(tmp_path, service):
    """Sol r1: a file with the right MD5 but not the pinned size was accepted (here the pin says one byte more; the service says 0)"""
    pins = json.loads(service.pins_file.read_text())
    for row in pins[0]["files"]:
        if row[0] == "game.dat":
            row[2] += 1
    service.pins_file.write_text(json.dumps(pins))
    service.entry("original-RotWK", "game.dat")["Size"] = 0
    service.publish()
    mine = on_disk(tmp_path, "RotWK", "game.dat")
    mine.parent.mkdir(parents=True)
    mine.write_bytes(service.data("game.dat"))
    r = install(tmp_path, service)
    assert r.code == 3 and r.has("already exists and is not original-RotWK's file (12000 bytes, expected 12001)"), r


@needs_godot
def test_aio_a_cancel_removes_only_its_own_partial_files(tmp_path, service):
    owned = Path(str(on_disk(tmp_path, "BFME2", "INI.big")) + ".part")
    owned.parent.mkdir(parents=True)
    owned.write_bytes(service.data("INI.big")[:100])
    other = on_disk(tmp_path, "RotWK", "mine.part")
    other.parent.mkdir(parents=True)
    other.write_bytes(b"not the launcher's")
    r = install(tmp_path, service, "--aio-stop-after=RotWK/_patch201.big:cancel")
    assert r.code == 2 and r.has("cancelled") and r.has("removed 1 partial files of this download"), r
    assert not owned.exists() and other.read_bytes() == b"not the launcher's"
    assert on_disk(tmp_path, "RotWK", "_patch201.big").read_bytes() == service.data("_patch201.big"), "a checked file stays"
    assert not (tmp_path / "game-user" / "downloaded-installs.cfg").exists()


@needs_godot
@pytest.mark.parametrize("how", ["cancel", "pause"])
def test_aio_a_stop_after_the_last_check_hands_nothing_off(tmp_path, service, how):
    """Sol r1: a cancel during the final verification still published the handoff"""
    r = install(tmp_path, service, f"--aio-stop-after=BFME2/lotrbfme2.exe:{how}")
    assert r.code == 2 and r.has("cancelled" if how == "cancel" else "paused"), r
    assert not (tmp_path / "game-user" / "downloaded-installs.cfg").exists()
    assert not (launcher_data(tmp_path) / "aio-install.json").exists()
    r = install(tmp_path, service)
    assert r.code == 0 and r.has("0 files downloaded, 7 already there"), r
    assert (tmp_path / "game-user" / "downloaded-installs.cfg").exists()


# ---- community PR #36: the optional BFME1 package -------------------------------------------------------------------------------------

def bfme1_install(tmp: Path, service: Service, *extra: str) -> Run:
    return launch(tmp, service, "--game=bfme1", "--aio=on", "--aio-package=bfme1", f"--aio-install={target(tmp)}", "--aio-consent", *extra)


def guids_asked(service: Service) -> list[str]:
    return [p.split("guid=", 1)[1] for p, _ in service.server.requests if p.startswith("/api/workshop/download")]


BFME1_EN = ["ini.big", "lang\\english.big", "lotrbfme.exe", "dsound.dll"]


@needs_godot
def test_aio_bfme1_is_refused_unless_bfme1_is_the_chosen_game(tmp_path, service):
    """opt-in only: the BFME1 package needs the BFME1 game chosen, the setting and the consent; nothing is asked of the service otherwise"""
    r = launch(tmp_path, service, "--game=bfme2", "--aio=on", "--aio-package=bfme1", f"--aio-install={target(tmp_path)}", "--aio-consent")
    assert r.code == 1 and r.has("offered only when Battle for Middle-earth I is the chosen game"), r
    r = launch(tmp_path, service, "--aio=on", "--aio-package=bfme1", f"--aio-install={target(tmp_path)}", "--aio-consent")
    assert r.code == 1 and r.has("offered only when Battle for Middle-earth I is the chosen game"), r  # the remembered game is bfme2
    r = launch(tmp_path, service, "--game=bfme1", "--aio=on", "--aio-package=bfme1", f"--aio-install={target(tmp_path)}")
    assert r.code == 1 and r.has("has not agreed"), r
    r = launch(tmp_path, service, "--game=bfme1", "--aio=off", "--aio-package=bfme1", f"--aio-install={target(tmp_path)}", "--aio-consent")
    assert r.code == 1 and r.has("is off"), r
    r = launch(tmp_path, service, "--game=bfme1", "--aio=on", "--aio-package=rotwk-mod", f"--aio-install={target(tmp_path)}", "--aio-consent")
    assert r.code == 1 and r.has("no download 'rotwk-mod'"), r
    assert service.server.requests == [] and not target(tmp_path).exists()


@needs_godot
def test_aio_base_download_never_takes_bfme1(tmp_path, service):
    """the base download is unchanged with BFME1 chosen too: RotWK + BFME2 only, the BFME1 package is never asked for"""
    r = launch(tmp_path, service, "--game=bfme1", "--aio=on", f"--aio-install={target(tmp_path)}", "--aio-consent")
    assert r.code == 0 and r.has("LAUNCHER AIO done: 7 files downloaded, 0 already there"), r
    assert guids_asked(service) == ["original-RotWK", "original-BFME2"]
    assert not (target(tmp_path) / "BFME1").exists() and not r.has("BFME Arena")
    assert not (tmp_path / "game-user" / "downloaded-bfme1.cfg").exists()


@needs_godot
def test_aio_bfme1_installs_only_bfme1_with_its_own_handoff(tmp_path, service):
    # a base download first: its handoff must survive the BFME1 one untouched (the game's readDownloaded refuses a BFME1_INSTALL key)
    assert install(tmp_path, service).code == 0
    marker = tmp_path / "game-user" / "downloaded-installs.cfg"
    before = marker.read_text()
    service.server.requests.clear()
    r = bfme1_install(tmp_path, service)
    assert r.code == 0 and r.has("LAUNCHER AIO done: 4 files downloaded, 0 already there"), r
    assert r.has("AIO consent given: Optional: Battle for Middle-earth I is downloaded from BFME Arena (bfmeladder.com), a third-party "
                 "service. OpenBFME does not host or distribute it."), r
    assert guids_asked(service) == ["original-BFME1"]
    pkg = next(p for p in service.pkgs if p["key"] == "bfme1")
    for f in pkg["files"]:
        path = on_disk(tmp_path, "BFME1", f["name"])
        assert path.exists() == (f["name"] in BFME1_EN), path
        if path.exists():
            assert path.read_bytes() == f["data"]
    assert marker.read_text() == before and "BFME1" not in before
    handoff = (tmp_path / "game-user" / "downloaded-bfme1.cfg").read_text().splitlines()
    assert handoff[1:] == ["SOURCE=All In One BFME Launcher service: original-BFME1", f"BFME1_INSTALL={target(tmp_path) / 'BFME1'}"], handoff
    data = launcher_data(tmp_path)
    assert json.loads((data / "aio-install-bfme1.json").read_text())["folders"] == {"bfme1": str(target(tmp_path) / "BFME1")}
    assert json.loads((data / "aio-install.json").read_text())["folders"].keys() == {"rotwk", "bfme2"}
    # a second run checks what is there and downloads nothing
    r = bfme1_install(tmp_path, service)
    assert r.code == 0 and r.has("0 files downloaded, 4 already there"), r


@needs_godot
@pytest.mark.parametrize("change,why", [
    (lambda s: s.entry("original-BFME1", "lotrbfme.exe").update(Md5="0" * 32, Url=f"{s.server.origin}/{OWNER}/" + "0" * 32),
     "lists lotrbfme.exe with MD5 00000000000000000000000000000000; BFME1 test has"),
    (lambda s: s.entry("original-BFME1", "dsound.dll").update(Size=7001), "lists dsound.dll with 7001 bytes; BFME1 test has 7000"),
    (lambda s: s.manifests["original-BFME1"]["Files"].append({**s.entry("original-BFME1", "dsound.dll"), "Name": "dsoal-aldrv.dll"}),
     "lists dsoal-aldrv.dll, which OpenBFME's list of BFME1 test does not have"),
    (lambda s: s.manifests["original-BFME1"].update(Game=1), "is no longer the base game package (Game 1"),
])
def test_aio_bfme1_a_changed_package_downloads_nothing(tmp_path, service, change, why):
    change(service)
    service.publish()
    r = bfme1_install(tmp_path, service)
    assert r.code == 3 and r.has(why) and r.has("Nothing was downloaded" if "Game" not in why else "Try again later"), r
    assert service.file_requests() == []
    assert not (tmp_path / "game-user" / "downloaded-bfme1.cfg").exists()


@needs_godot
def test_aio_bfme1_a_file_with_other_bytes_or_size_is_deleted(tmp_path, service):
    """the service's bytes are checked against the pins, not the manifest: other bytes of the same size are deleted, a longer file refused"""
    exe = service.data("lotrbfme.exe")
    service.server.files[file_path(exe)] = bytes(len(exe))
    r = bfme1_install(tmp_path, service)
    assert r.code == 3 and r.has("BFME1/lotrbfme.exe from the service has MD5") and r.has("it was deleted"), r
    dest = on_disk(tmp_path, "BFME1", "lotrbfme.exe")
    assert not dest.exists() and not Path(str(dest) + ".part").exists()
    service.server.files[file_path(exe)] = exe + b"x"
    r = bfme1_install(tmp_path, service)
    assert r.code == 3 and r.has("BFME1/lotrbfme.exe: the service answered something OpenBFME does not recognise"), r
    assert not dest.exists() and not (tmp_path / "game-user" / "downloaded-bfme1.cfg").exists()
    # a file already in the folder that is not the pinned one is never overwritten
    service.server.files[file_path(exe)] = exe
    dll = on_disk(tmp_path, "BFME1", "dsound.dll")
    dll.write_bytes(b"the player's own DSOAL")
    r = bfme1_install(tmp_path, service)
    assert r.code == 3 and r.has("already exists and is not original-BFME1's file"), r
    assert dll.read_bytes() == b"the player's own DSOAL"


@needs_godot
def test_aio_bfme1_window_offers_it_only_when_chosen(tmp_path, service):
    """the real window (scripts/main.gd, headless, offline): the Game files screen offers the base download until the Play card's game
    picker is set to Battle for Middle-earth I; then it offers the optional BFME1 package (labelled BFME Arena, third party) and its
    Download press installs only that; back on BFME II / RotWK, the base offer and the base download again (tests/bfme1_offer_test.gd)"""
    env = dict(os.environ, XDG_DATA_HOME=str(tmp_path / "data"), OPENBFME_GAME_USER_DIR=str(tmp_path / "game-user"), HOME=str(tmp_path / "home"),
               BFME1_INSTALL="")
    (tmp_path / "home").mkdir()
    cmd = [GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/bfme1_offer_test.gd", "--", "--offline",
           f"--aio-base={service.server.origin}", f"--aio-pins={service.pins_file}", f"--bfme1-offer-target={target(tmp_path)}"]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=300, stdin=subprocess.DEVNULL)
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "BFME1 OFFER OK" in out, out[-4000:]
    # BFME1 (paused), the refused resume (nothing asked), BFME1 again (completed), then the base download
    assert guids_asked(service) == ["original-BFME1", "original-BFME1", "original-RotWK", "original-BFME2"], guids_asked(service)
    for name in BFME1_EN:
        assert on_disk(tmp_path, "BFME1", name).read_bytes() == service.data(name)
    assert on_disk(tmp_path, "RotWK", "_patch201.big").read_bytes() == service.data("_patch201.big")
    assert "BFME1" not in (tmp_path / "game-user" / "downloaded-installs.cfg").read_text()


@needs_godot
def test_bfme1_assets_script(tmp_path):
    """launcher/tests/bfme1_assets_test.gd: the real pins accept the package's own manifest and refuse a changed one; the downloaded
    BFME1 folder is found and passed to the game"""
    env = dict(os.environ, BFME1_INSTALL="", XDG_DATA_HOME=str(tmp_path / "data"), HOME=str(tmp_path))
    env.pop("BFME1_TEST_MANIFEST", None)
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/bfme1_assets_test.gd"], capture_output=True,
                       text=True, timeout=300, stdin=subprocess.DEVNULL, env=env)
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "BFME1 ASSETS OK" in out, out[-4000:]


# ---- MACFIX-1 (issue #46): the progress display of a large download in the real window ---------------------------------------------

BIG = 64 * 1024 * 1024
BIG_FILES = ["RotWK/big1.big", "RotWK/big2.big", "BFME2/zbig3.big"]


def big_packages() -> list[dict]:
    """the two base packages with three 64 MiB files among small ones (the last file of the download is big too: its check keeps the
    window drawing until the bar has reached the total)"""
    r = os.urandom
    pkgs = packages()
    pkgs[0]["files"] = [_file("_patch201.big", r(150000)), _file("big1.big", r(BIG)), _file("big2.big", r(BIG)),
                        _file("lang\\English.big", r(40000), "EN"), _file("lang\\French.big", r(20000), "FR")]
    pkgs[1]["files"] = [_file("INI.big", r(60000)), _file("zbig3.big", r(BIG))]
    return pkgs


@needs_godot
def test_aio_progress_keeps_moving_in_the_window_during_a_large_download(tmp_path):
    """issue #46 (the Mac launcher stuck on "Checking File 2 of 612" for the whole 8.6 GB download while the files arrived): the real
    window downloads 192 MiB from the stand-in service, its 16 KiB chunks throttled to the order of 100 MB/s (the issue's line: 30-60
    MB/s); every frame the bottom bar and the Game files bar are sampled (tests/aio_progress_test.gd): the window keeps drawing (no
    frame gap over 1 s), both bars only move forward and reach the total, each big file is shown mid-download, and every file lands
    with its bytes"""
    server = ReleaseServer()
    try:
        service = Service(server, tmp_path, big_packages())
        for p in service.pkgs:
            for f in p["files"]:
                if len(f["data"]) == BIG:
                    server.throttle[file_path(f["data"])] = 0.0001
        env = dict(os.environ, XDG_DATA_HOME=str(tmp_path / "data"), OPENBFME_GAME_USER_DIR=str(tmp_path / "game-user"),
                   HOME=str(tmp_path / "home"))
        (tmp_path / "home").mkdir()
        cmd = [GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/aio_progress_test.gd", "--", "--offline",
               f"--aio-base={server.origin}", f"--aio-pins={service.pins_file}", f"--progress-target={target(tmp_path)}",
               "--progress-big=" + ",".join(BIG_FILES)]
        started = time.monotonic()
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=400, stdin=subprocess.DEVNULL)
        took = time.monotonic() - started
        out = r.stdout + r.stderr
        print(next((ln for ln in out.splitlines() if ln.startswith("AIO PROGRESS frames")), ""), f"({3 * BIG / 1e6 / took:.0f} MB/s overall)")
        assert r.returncode == 0 and "AIO PROGRESS OK" in out, out[-4000:]
        assert "Message queue out of memory" not in out, out[-4000:]
        for p in service.pkgs:
            if p["key"] == "bfme1":
                continue
            for f in p["files"]:
                path = on_disk(tmp_path, p["folder"], f["name"])
                english = f["language"] == "ALL" or "EN" in f["language"].split()
                assert path.exists() == english, path
                if english:
                    assert path.read_bytes() == f["data"], path
        assert not list(target(tmp_path).rglob("*.part"))
    finally:
        server.stop()


# ---- issue #49: the download's text and voice languages ---------------------------------------------------------------------------------

LANG_NAMES = {"FR": "French", "DE": "German", "IT": "Italian"}


def language_packages() -> list[dict]:
    """the two base packages with an English, a French and a German version (French and German with their own speech archives; the
    English one is shared like the Dutch version shares it, which the "drop" lists model)"""
    r = os.urandom
    pkgs = packages()
    pkgs[0]["files"] = [
        _file("_patch201.big", r(150000), pinned=True),
        _file("lang\\English.big", r(40000), "EN", pinned=True),
        _file("lang\\EnglishAudio.big", r(30000), "EN NL", pinned=True),
        _file("lang\\French.big", r(20000), "FR"),
        _file("lang\\FrenchAudio.big", r(21000), "FR"),
        _file("lang\\German.big", r(22000), "DE"),
        _file("lang\\GermanAudio.big", r(23000), "DE"),
        _file("lang\\Italian.big", r(9000), "IT"),  # pinned, but BFME2 has no Italian version here: never offered
    ]
    pkgs[1]["files"] = [
        _file("INI.big", r(60000), pinned=True),
        _file("lang\\english.big", r(10000), "EN"),
        _file("lang\\French.big", r(11000), "FR"),
        _file("lang\\German.big", r(12000), "DE"),
    ]
    return pkgs


def language_pins(pkgs: list[dict]) -> list[dict]:
    """pins() plus each base package's "languages" (aio_pins.gd's form, as tools/release/aio_languages.py captures it)"""
    out = pins(pkgs)
    for p, pin in zip(pkgs, out):
        if p["key"] == "bfme1":
            continue
        langs = {}
        for code in ("FR", "DE", "IT"):
            add = [[f["name"].replace("\\", "/"), hashlib.md5(f["data"]).hexdigest(), len(f["data"])] for f in p["files"] if f["language"] == code]
            if not add:
                continue
            drop = [f["name"].replace("\\", "/") for f in p["files"]
                    if f["language"] != "ALL" and "EN" in f["language"].split() and code not in f["language"].split()]
            langs[code] = {"name": LANG_NAMES[code], "drop": drop, "add": add}
        pin["languages"] = langs
    return out


@pytest.fixture
def lang_service(tmp_path):
    s = ReleaseServer()
    svc = Service(s, tmp_path, language_packages())
    svc.pins_file.write_text(json.dumps(language_pins(svc.pkgs)))
    yield svc
    s.stop()


def _have(tmp: Path, folder: str) -> set[str]:
    base = target(tmp) / folder
    return {str(p.relative_to(base)).replace("\\", "/") for p in base.rglob("*") if p.is_file()}


@needs_godot
def test_aio_language_takes_that_versions_files(tmp_path, lang_service):
    r = install(tmp_path, lang_service, "--aio-language=FR")
    assert r.code == 0 and r.has("LAUNCHER AIO language French"), r
    assert r.has("original-RotWK: the download takes 3 of the package's 8 files (French; every one is pinned and matches)"), r
    assert _have(tmp_path, "RotWK") == {"_patch201.big", "lang/French.big", "lang/FrenchAudio.big"}
    assert _have(tmp_path, "BFME2") == {"INI.big", "lang/French.big"}
    assert (target(tmp_path) / "RotWK" / "lang" / "French.big").read_bytes() == lang_service.data("lang\\French.big")
    # the choice is remembered: the next start (no option) takes the same version and finds it all there
    r = launch(tmp_path, lang_service, f"--aio-install={target(tmp_path)}", "--aio-consent")
    assert r.code == 0 and r.has("0 files downloaded, 5 already there"), r


@needs_godot
def test_aio_voice_language_takes_another_versions_speech(tmp_path, lang_service):
    r = install(tmp_path, lang_service, "--aio-language=FR", "--aio-voice=DE")
    assert r.code == 0 and r.has("LAUNCHER AIO language French text, German voices"), r
    assert _have(tmp_path, "RotWK") == {"_patch201.big", "lang/French.big", "lang/GermanAudio.big"}
    other = tmp_path / "en"
    r = install(other, lang_service, "--aio-language=DE", "--aio-voice=EN")
    assert r.code == 0, r
    assert _have(other, "RotWK") == {"_patch201.big", "lang/German.big", "lang/EnglishAudio.big"}


@needs_godot
def test_aio_refuses_a_folder_that_holds_another_language(tmp_path, lang_service):
    """PR #77 review: a second language in one folder would make the game pick English and refuse the other as an unknown archive, so the
    download stops before it requests anything; the same language again is the normal resume"""
    r = install(tmp_path, lang_service, "--aio-language=FR")
    assert r.code == 0, r
    before = len(lang_service.file_requests())
    for args in (("--aio-language=DE",), ("--aio-language=FR", "--aio-voice=DE"), ("--aio-language=FR", "--aio-voice=EN")):
        r = install(tmp_path, lang_service, *args)
        assert r.code == 1 and r.has("already holds") and r.has("another language"), (args, r)
    assert r.has("lang/FrenchAudio.big") or r.has("lang/French.big"), r
    assert len(lang_service.file_requests()) == before
    assert _have(tmp_path, "RotWK") == {"_patch201.big", "lang/French.big", "lang/FrenchAudio.big"}
    # (the last refused choice is the saved one, so the voice is named again)
    r = install(tmp_path, lang_service, "--aio-language=FR", "--aio-voice=FR")
    assert r.code == 0 and r.has("0 files downloaded, 5 already there"), r


@needs_godot
def test_aio_language_refuses_what_it_does_not_offer(tmp_path, lang_service):
    for arg in ("--aio-language=IT", "--aio-language=XX", "--aio-voice=IT"):
        r = install(tmp_path, lang_service, arg)
        assert r.code == 1 and r.has("--aio-language / --aio-voice: the download offers no"), (arg, r)
    assert lang_service.file_requests() == []


def _add_unpinned(svc: Service, name: str, language: str) -> None:
    data = os.urandom(5000)
    md5 = hashlib.md5(data).hexdigest()
    svc.manifests["original-RotWK"]["Files"].append({"Guid": md5, "Name": name, "Url": f"{svc.server.origin}/{OWNER}/{md5}",
                                                     "Md5": md5, "Language": language, "Size": 5000})
    svc.publish()


@needs_godot
def test_aio_a_language_file_it_has_no_pin_for_is_a_changed_package(tmp_path, lang_service):
    """a file tagged with a language the pinned table knows, but not pinned: the package changed"""
    _add_unpinned(lang_service, "lang\\Frenchextra.big", "FR")
    r = install(tmp_path, lang_service, "--aio-language=FR")
    assert r.code == 3 and r.has("lists lang/Frenchextra.big, which OpenBFME's list of"), r
    assert lang_service.file_requests() == []


@needs_godot
def test_aio_a_file_of_a_language_the_table_does_not_know_is_skipped(tmp_path, lang_service):
    """PR #77 review: a new language on the service must not stop every download (English included), as before the PR"""
    _add_unpinned(lang_service, "lang\\Klingon.big", "KL")
    for sub, args in (("fr", ("--aio-language=FR",)), ("en", ())):
        home = tmp_path / sub
        r = install(home, lang_service, *args)
        assert r.code == 0, (sub, r)
        assert not on_disk(home, "RotWK", "lang/Klingon.big").exists()


def test_aio_language_tables_agree_with_the_engine():
    """issue #49: the launcher's language files (tools/release/aio_language_files.json, rendered into aio_pins.gd) and the engine's
    language-archives.json are one table; every English file a language drops is an English pin, none it adds is"""
    import aio_languages
    files, engine = aio_languages.load()
    assert aio_languages.check(files, engine) == []
    english = aio_pins.load_files()
    for guid, per in files.items():
        names = {str(f[0]).lower() for f in english[guid]}
        for code, d in per.items():
            for n in d["drop"]:
                assert n.lower() in names, (guid, code, n)
            assert not any(r[0].lower() in names for r in d["add"]), (guid, code)
    # the German archives are the ones a retail German install has (sizes and MD5s measured on that install)
    de = {(l["game"], a["path"].lower()): (a["md5"], a["size"]) for l in engine["languages"] if l["code"] == "DE" for a in l["archives"]}
    assert de[("rotwk", "lang/german.big")] == ("dc848c78316743147da8c4faefa13813", 4851405)
    assert de[("bfme2", "lang/germanpatch105.big")] == ("9d952b46a9ea61fd013b95ae1c49923c", 1434693)


def test_aio_language_table_check_refuses_empty_duplicate_and_extra_entries(tmp_path):
    """PR #77 security review: check() must not pass an empty launcher table, an empty engine table, two engine entries of one language (they
    collapsed into one before), an engine language the launcher table lacks, or a JSON file with a key given twice"""
    import copy
    import aio_languages
    files, engine = aio_languages.load()
    assert aio_languages.check(files, engine) == []
    assert aio_languages.check({}, engine) != []
    assert aio_languages.check(files, {}) != []
    assert aio_languages.check(files, {"languages": []}) != []
    one_package = {k: v for k, v in files.items() if k != "original-BFME2"}
    assert any("original-BFME2" in e for e in aio_languages.check(one_package, engine))
    # a second entry of a language: the same archives (it collapsed silently) and other archives (it shadowed the first)
    for archives in ("same", "other"):
        twice = copy.deepcopy(engine)
        extra = copy.deepcopy(next(l for l in twice["languages"] if l["game"] == "rotwk" and l["code"] == "DE"))
        if archives == "other":
            extra["archives"][0]["md5"] = "0" * 32
        twice["languages"].append(extra)
        assert any("listed twice" in e for e in aio_languages.check(files, twice)), archives
    # the same language name under another code (the engine keys its table by name)
    renamed = copy.deepcopy(engine)
    entry = next(l for l in renamed["languages"] if l["game"] == "rotwk" and l["code"] == "FR")
    entry["language"] = "German"
    assert aio_languages.check(files, renamed) != []
    # an engine language the launcher table does not have
    extra_lang = copy.deepcopy(engine)
    extra_lang["languages"].append({"game": "rotwk", "patch": "2.01", "code": "XX", "language": "Klingon", "archives": []})
    assert aio_languages.check(files, extra_lang) != []
    # a duplicate archive path inside one entry
    dup_path = copy.deepcopy(engine)
    de = next(l for l in dup_path["languages"] if l["game"] == "rotwk" and l["code"] == "DE")
    de["archives"].append(dict(de["archives"][0]))
    assert any("listed twice" in e for e in aio_languages.check(files, dup_path))
    # a key given twice in a JSON file is refused when loading (json.loads alone keeps the last one)
    with pytest.raises(ValueError):
        import json
        json.loads('{"a": 1, "a": 2}', object_pairs_hook=aio_languages._no_duplicate_keys)
