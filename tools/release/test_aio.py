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
    assert {g: len(rows) for g, rows in files.items()} == {"original-RotWK": 315, "original-BFME2": 297}
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
    def __init__(self, server: ReleaseServer, tmp: Path) -> None:
        self.server, self.tmp = server, tmp
        self.pkgs = packages()
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


def launch(tmp: Path, service: Service, *args: str) -> Run:
    env = dict(os.environ, XDG_DATA_HOME=str(tmp / "data"), OPENBFME_GAME_USER_DIR=str(tmp / "game-user"))
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
    assert r.has("AIO consent given: The game files are downloaded from the servers of the All In One BFME Launcher"), r
    for p in service.pkgs:
        for f in p["files"]:
            path = on_disk(tmp_path, p["folder"], f["name"])
            english = f["language"] == "ALL" or "EN" in f["language"].split()
            assert path.exists() == english, path
            if english:
                assert path.read_bytes() == f["data"], path
    assert not list(target(tmp_path).rglob("*.part"))
    record = json.loads((tmp_path / "data" / "OpenBFMELauncher" / "aio-install.json").read_text())
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
    (lambda m, o: m["Files"][4].update(Url="https://example.com/" + m["Files"][4]["Md5"]), "not at the pinned http://127.0.0.1"),
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
    assert not (tmp_path / "data" / "OpenBFMELauncher" / "aio-install.json").exists()
    r = install(tmp_path, service)
    assert r.code == 0 and r.has("0 files downloaded, 7 already there"), r
    assert (tmp_path / "game-user" / "downloaded-installs.cfg").exists()
