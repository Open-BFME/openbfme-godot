"""Lane MODS-3: the launcher's one-click mods (launcher/scripts/core/mod_catalog.gd, mod_install.gd; docs/MODS.md) against a local
stand-in for the All In One BFME Launcher's workshop service (launcher_test_server.ReleaseServer playing both of its hosts: the query and
download API and the content-addressed /<uploader>/<md5> files). The catalogue is launcher/tests/fixtures/mods/catalogue.json: made-up
packages in the service's JSON shape whose file bytes follow a rule, so no real mod data and no request to the real service.

The launcher tests skip (loudly) without Godot 4.7.2.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
LAUNCHER = REPO / "launcher"
FIXTURE = LAUNCHER / "tests" / "fixtures" / "mods" / "catalogue.json"
sys.path.insert(0, str(HERE))

from launcher_test_server import ReleaseServer  # noqa: E402

GODOT = os.environ.get("GODOT", "godot")
DATA = "OpenBFMELauncher"


def _godot_ok() -> bool:
    try:
        return subprocess.run([GODOT, "--version"], capture_output=True, text=True, timeout=60).stdout.startswith("4.7.2.")
    except (OSError, subprocess.TimeoutExpired):
        return False


needs_godot = pytest.mark.skipif(not _godot_ok(), reason="SKIP mods launcher tests: Godot 4.7.2 not found (set GODOT)")


def file_bytes(guid: str, f: dict, unknown: int) -> bytes:
    """the made-up bytes of a fixture file (the rule in catalogue.json's comment)"""
    if f["Md5"] == hashlib.md5(b"").hexdigest():
        return b""
    unit = f"{guid}/{f['Name']}\n".encode()
    n = f["Size"] or unknown
    return (unit * (n // len(unit) + 1))[:n]



def logged(path) -> str:
    """the path as the launcher log writes it: its privacy filter replaces the account name (e.g. in /tmp/pytest-of-<name>) with <user>"""
    user = os.environ.get("USER") or os.environ.get("USERNAME") or os.environ.get("LOGNAME") or ""
    text = str(path)
    return re.sub(r"(?i)(?<![a-z0-9_])" + re.escape(user) + r"(?![a-z0-9_])", "<user>", text) if user else text

class Service:
    """the fixture served the way bfmeladder.com serves it"""

    def __init__(self, server: ReleaseServer) -> None:
        self.server = server
        self.fx = json.loads(FIXTURE.read_text())
        self.entries = json.loads(json.dumps(self.fx["entries"]))
        self.data: dict[tuple[str, str], bytes] = {}
        for guid, e in self.entries.items():
            for f in e["Files"]:
                d = file_bytes(guid, f, self.fx["unknown_size"])
                assert hashlib.md5(d).hexdigest() == f["Md5"], (guid, f["Name"])
                f["Url"] = f["Url"].replace(self.fx["files_origin"], server.origin)
                self.data[(guid, f["Name"])] = d
                server.files[self.file_path(f)] = d
        for t, pages in self.fx["query"].items():
            for n, page in enumerate(pages):
                server.files[f"/api/workshop/query?keyword=&game=2&type={t}&sortMode=0&page={n}"] = json.dumps(page).encode()
        self.publish()

    def publish(self) -> None:
        for guid, e in self.entries.items():
            self.server.files[f"/api/workshop/download?guid={guid}"] = json.dumps(e).encode()

    @staticmethod
    def file_path(f: dict) -> str:
        return "/" + f["Url"].split("/", 3)[3]

    def file(self, guid: str, name: str) -> dict:
        return next(f for f in self.entries[guid]["Files"] if f["Name"] == name)

    def file_requests(self) -> list[tuple[str, dict]]:
        return [(p, h) for p, h in self.server.requests if not p.startswith("/api/")]


@pytest.fixture
def service():
    s = ReleaseServer()
    yield Service(s)
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
    for k in ("ROTWK_INSTALL", "BFME2_INSTALL", "OPENBFME_MOD", "OPENBFME_MOD_INSTALL"):
        env.pop(k, None)
    cmd = [GODOT, "--headless", "--path", str(LAUNCHER), "--", f"--aio-base={service.server.origin}", *args]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=180, stdin=subprocess.DEVNULL)
    return Run(r.returncode, r.stdout + r.stderr)


def mods_dir(tmp: Path) -> Path:
    return tmp / "data" / DATA / "mods"


def snapshot(root: Path) -> dict[str, tuple[int, str]]:
    out = {}
    for p in sorted(root.rglob("*")):
        if p.is_file() or p.is_symlink():
            out[str(p.relative_to(root))] = (p.lstat().st_size, hashlib.md5(p.read_bytes()).hexdigest() if p.is_file() else "link")
    return out


def changed(before: dict, after: dict) -> set[str]:
    return {k for k in before.keys() | after.keys() if before.get(k) != after.get(k)}


# ---- the tests --------------------------------------------------------------------------------------------------------------------------

@needs_godot
def test_mod_catalog_rules():
    """the layering, the classification, the checks, the grouping and the game arguments (tests/mod_catalog_test.gd, no network)"""
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/mod_catalog_test.gd"],
                       capture_output=True, text=True, timeout=180, stdin=subprocess.DEVNULL)
    assert r.returncode == 0 and "MODS OK" in r.stdout, r.stdout[-4000:] + r.stderr[-2000:]


def make_asset_packs(root: Path) -> None:
    """lane SDK-1: two made-up asset packs: "HD Units" (a model, a texture, a readme) and "Sneaky" (a texture and an INI file)"""
    (root / "HD Units" / "textures").mkdir(parents=True)
    (root / "HD Units" / "gumaarms_skn.w3d").write_bytes(b"w" * 600)
    (root / "HD Units" / "textures" / "GUMaArms.dds").write_bytes(b"DDS " + b"t" * 420)
    (root / "HD Units" / "README.md").write_text("about")
    (root / "Sneaky").mkdir()
    (root / "Sneaky" / "tree.dds").write_bytes(b"DDS ")
    (root / "Sneaky" / "weapon.ini").write_text("Weapon X\nEnd\n")


@needs_godot
def test_asset_packs_listed_and_turned_off(tmp_path, service):
    """lane SDK-1: --asset-packs lists the game's asset-packs folder with the game's rules; --asset-pack-off / -on write disabled.txt"""
    root = tmp_path / "game-user" / "asset-packs"
    make_asset_packs(root)
    r = launch(tmp_path, service, "--asset-packs")
    assert r.code == 0 and r.has("LAUNCHER ASSET PACKS 2 in "), r  # the folder itself is scrubbed from the log (launcher_log.gd)
    assert r.has("LAUNCHER ASSET PACK HD Units | on | 1 models, 1 textures | 1 MB"), r
    assert r.has("LAUNCHER ASSET PACK Sneaky problem: weapon.ini is not a model or texture"), r
    r = launch(tmp_path, service, "--asset-pack-off=Sneaky", "--asset-packs")
    assert r.code == 0 and r.has("LAUNCHER ASSET PACK Sneaky | off |"), r
    assert (root / "disabled.txt").read_text().splitlines()[1:] == ["sneaky"]
    r = launch(tmp_path, service, "--asset-pack-on=sneaky", "--asset-pack-off=Nope", "--asset-packs")
    assert r.code == 1 and r.has("LAUNCHER ASSET PACK Sneaky | on |") and r.has("cannot turn it off: there is no asset pack Nope"), r
    assert service.server.requests == [], "asset packs ask nothing of the service"


@needs_godot
def test_asset_packs_card_shows_the_games_findings(tmp_path, service):
    """lane SDK-1b (b): the game's report.json (scripts/release/release.gd) per pack: the models left out, with their reasons; a pack the
    game has not checked says so (lane SDK-1c: no online findings, online is permissive)"""
    root = tmp_path / "game-user" / "asset-packs"
    make_asset_packs(root)
    (root / "Unchecked").mkdir()
    (root / "Unchecked" / "tree.dds").write_bytes(b"DDS ")
    report = {"format": "openbfme.asset-pack-report 1", "checked": "2026-10-27T00:00:00", "packs": [
        {"name": "HD Units", "misfits": [{"file": "art/w3d/db/dbcitadel.w3d", "why": "different bones or models than the game's (x)"}]}]}
    (root / "report.json").write_text(json.dumps(report))
    script = tmp_path / "card.gd"
    script.write_text("""extends SceneTree
const AssetPacks := preload("res://scripts/core/asset_packs.gd")
const Screen := preload("res://scripts/ui/mods_screen.gd")
func shown(c: Node, top: Node) -> bool:
	while c != null and c != top:
		if c is CanvasItem and not c.visible:
			return false
		c = c.get_parent()
	return true
func _initialize() -> void:
	var s = Screen.new()
	root.add_child(s)
	s.set_asset_packs(AssetPacks.list())
	for c in s._packs_box.find_children("*", "", true, false):
		if c is Label or c is Button:
			print("CARD ", c.text, " | ", shown(c, s))
	for b in s._packs_box.find_children("*", "Button", true, false):
		if b.text == "Details":
			b.pressed.emit()
			break
	for c in s._packs_box.find_children("*", "Label", true, false):
		if c.text.begins_with("art/w3d/db/dbcitadel.w3d"):
			print("SHOWN ", shown(c, s))
	quit(0)
""")
    env = dict(os.environ, OPENBFME_GAME_USER_DIR=str(tmp_path / "game-user"), XDG_DATA_HOME=str(tmp_path / "data"))
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", str(script)], env=env, capture_output=True, text=True, timeout=180,
                       stdin=subprocess.DEVNULL)
    out = r.stdout + r.stderr
    assert "CARD 1 models not used: they don't fit the game's (different bones or models), so the game's own are drawn | true" in out, out
    assert "CARD art/w3d/db/dbcitadel.w3d: different bones or models than the game's (x) | false" in out, out  # behind the toggle
    assert "online" not in out.lower(), out  # lane SDK-1c: online is permissive, the card names no online finding
    assert "SHOWN true" in out, out  # the toggle shows the reasons
    assert "CARD Not checked against your game files yet: start the game once. | true" in out, out  # the pack the game has not checked
    assert "SCRIPT ERROR" not in out, out


@needs_godot
def test_mods_window(tmp_path, service):
    """the real window (scripts/main.gd, headless, offline): Load the list, the question before the first mod download, Install with an
    enhancement, Play with, Pause / Resume, Remove (tests/mods_window_test.gd)"""
    (tmp_path / "home").mkdir()
    make_asset_packs(tmp_path / "game-user" / "asset-packs")
    env = dict(os.environ, XDG_DATA_HOME=str(tmp_path / "data"), OPENBFME_GAME_USER_DIR=str(tmp_path / "game-user"), HOME=str(tmp_path / "home"))
    cmd = [GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/mods_window_test.gd", "--", "--offline",
           f"--aio-base={service.server.origin}"]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=300, stdin=subprocess.DEVNULL)
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "MODS WINDOW OK" in out, out[-4000:]
    assert "SCRIPT ERROR" not in out, out[-4000:]
    # the window asks the service nothing before Load the list (the first request is the list's first page)
    assert service.server.requests[0][0] == "/api/workshop/query?keyword=&game=2&type=0&sortMode=0&page=0", service.server.requests[:2]
    record = json.loads((mods_dir(tmp_path) / "test-realms" / "openbfme-aio.json").read_text())
    assert record["enhancements"] == ["test-realms-nointro"], record
    assert not (mods_dir(tmp_path) / "test-siege").exists()


@needs_godot
def test_mods_list_groups_enhancements_and_marks_engine_edits(tmp_path, service):
    r = launch(tmp_path, service, "--mods-list")
    assert r.code == 0 and r.has("LAUNCHER MODS 5 packages"), r
    mod = [ln.removeprefix("LAUNCHER MOD ") for ln in r.lines if ln.startswith("LAUNCHER MOD ")]
    assert mod[:3] == ["test-patch-99 | patch | needs engine features | Made-Up Patch 9.9 1.0.0 | by The Test Team | 1 MB",
                       "  needs an engine feature: Made-Up Patch 9.9: lotrbfme2ep1.exe",
                       "  needs an engine feature: Made-Up Patch 9.9: game.dat"], r
    assert "test-realms | patch | ready | Example Realms 1.2.0 | by Realm Builders | 1 MB" in mod, r
    assert "+ test-realms-nointro | enhancement | ready | Realms No Intro 1.0.0 | by Realm Builders | 1 MB" in mod, r
    assert "+ test-realms-font | enhancement | ready | Realms Big Font 1.0.1 | by Realm Builders | 1 MB" in mod, r
    assert "+ test-siege-timer | enhancement | needs engine features | Siege Timer 1.0.0 | by Siege Folk | 1 MB" in mod, r
    assert "test-tales | mod | ready | Tiny Tales 2.0.1 | by A. Storyteller | 1 MB" in mod, r
    assert any(m.startswith("test-broken | mod | refused: The service's package test-broken lists a file name that is not a plain relative path") for m in mod), r
    assert mod[-1].startswith("test-zoom | enhancement | ready | Zoom For The Original"), r
    assert r.has("enhancements of the original game or of a package that is not listed"), r
    asks = [(p, h) for p, h in service.server.requests if p.startswith("/api/")]
    assert all(h.get("authaccountuuid") == "unauthenticated" and "authaccountpassword" in h for _, h in asks), asks
    assert "/api/workshop/download?guid=test-bfme2-patch" in [p for p, _ in asks], "a dependency outside the list is read too"
    assert "/api/workshop/download?guid=original-RotWK" not in [p for p, _ in asks], "the base game is never asked for"
    assert service.file_requests() == [], "listing downloads no file"
    # kept briefly: a second list asks nothing; refresh asks again
    n = len(service.server.requests)
    r = launch(tmp_path, service, "--mods-list")
    assert r.code == 0 and r.has("(kept from the last half hour)") and len(service.server.requests) == n, r
    r = launch(tmp_path, service, "--mods-list=refresh")
    assert r.code == 0 and not r.has("(kept from") and len(service.server.requests) > n, r


@needs_godot
def test_mod_install_needs_consent(tmp_path, service):
    r = launch(tmp_path, service, "--mod-get=test-realms")
    assert r.code == 1 and r.has("has not agreed to download mods"), r
    assert service.server.requests == [] and not (mods_dir(tmp_path) / "test-realms").exists()
    for bad in ["../escape", "original-RotWK", "a/b"]:
        r = launch(tmp_path, service, f"--mod-get={bad}", "--mod-consent")
        assert r.code == 1 and r.has("is not a package OpenBFME can install"), r
    assert service.server.requests == []


@needs_godot
def test_mod_install_lays_out_writes_only_its_folder_and_starts(tmp_path, service):
    launch(tmp_path, service, "--version")
    (tmp_path / "game-user").mkdir(exist_ok=True)
    before = snapshot(tmp_path)
    r = launch(tmp_path, service, "--mod-get=test-realms", "--mod-with=test-realms-nointro,test-realms-font", "--mod-consent")
    assert r.code == 0 and r.has("LAUNCHER MODS done: Example Realms 1.2.0 (ready), 6 files downloaded, 0 already there, 0 removed"), r
    assert r.has("LAUNCHER MODS consent given: Mods are downloaded from bfmeladder.com"), r
    base = mods_dir(tmp_path) / "test-realms"
    rel = f"data/{DATA}/mods/test-realms/"
    outside = {p for p in changed(before, snapshot(tmp_path)) if not p.startswith(rel)}
    assert outside <= {f"data/{DATA}/launcher.log", f"data/{DATA}/state.cfg"}, outside   # the log, and the consent in state.cfg
    want = {"_____short_intro.big": ("test-realms-nointro", "_____short_intro.big"), "__realms_ini.big": ("test-realms-nointro", "__realms_ini.big"),
            "lang/English.big": ("test-realms-font", "lang\\English.big"), "__realms_data.big": ("test-realms", "__realms_data.big"),
            "lang/englishaudio.big": ("test-realms", "lang\\englishaudio.big"), "worldbuilder_noeula.txt": ("test-realms", "worldbuilder_noeula.txt")}
    got = {str(p.relative_to(base / "install")): p.read_bytes() for p in (base / "install").rglob("*") if p.is_file()}
    assert got == {k: service.data[v] for k, v in want.items()}, sorted(got)
    assert not (base / "mod").exists()
    record = json.loads((base / "openbfme-aio.json").read_text())
    assert record["schema"] == "openbfme.aio-package 1" and record["guid"] == "test-realms" and record["parts"] == ["install"], record
    assert record["enhancements"] == ["test-realms-nointro", "test-realms-font"] and record["files"] == 6, record
    assert [l["guid"] for l in record["layers"]] == ["test-realms-nointro", "test-realms-font", "test-realms"], record
    assert record["engine_edits"] == [] and record["left_out"] == ["Example Realms: Realms_Launcher.exe (a tool)", "Example Realms: mod.txt"], record
    assert not any(p.startswith("/" + service.file(g, n)["Url"].split("/", 3)[3]) for p, _ in service.file_requests()
                   for g, n in [("test-realms", "lang\\German.big"), ("test-realms", "Realms_Launcher.exe")]), "files left out are never downloaded"
    # the pick and the game's arguments (Updater.game_arguments, what Play passes)
    r = launch(tmp_path, service, "--mod-args")
    assert r.code == 0 and r.has("LAUNCHER game arguments: ") and not r.has("--mod"), r
    r = launch(tmp_path, service, "--mod-use=test-realms", "--mod-args")
    assert r.code == 0 and r.has(f"LAUNCHER game arguments: -- --mod-install={logged(base / 'install')}"), r
    cfg = (mods_dir(tmp_path) / "active.cfg").read_text()
    assert 'active="test-realms"' in cfg and '"test-realms-nointro", "test-realms-font"' in cfg, cfg
    # again without the enhancements: their files go, the package's own come back; the pick follows only with --mod-use
    n = len(service.file_requests())
    r = launch(tmp_path, service, "--mod-get=test-realms", "--mod-consent", "--mod-args")
    assert r.code == 1 and r.has("2 files downloaded, 3 already there, 1 removed") and r.has("are not the installed ones"), r
    assert len(service.file_requests()) == n + 2
    assert (base / "install" / "__realms_ini.big").read_bytes() == service.data[("test-realms", "__realms_ini.big")]
    assert not (base / "install" / "_____short_intro.big").exists()
    r = launch(tmp_path, service, "--mod-use=test-realms", "--mod-args")
    assert r.code == 0 and r.has(f"--mod-install={logged(base / 'install')}"), r
    # uninstall: its folder only; the original game is picked again
    r = launch(tmp_path, service, "--mod-remove=test-realms", "--mod-args")
    assert r.code == 0 and r.has("LAUNCHER MODS removed Example Realms") and not r.has("--mod-install"), r
    assert not base.exists() and (mods_dir(tmp_path) / "active.cfg").exists()
    r = launch(tmp_path, service, "--mod-remove=test-realms")
    assert r.code == 1 and r.has("test-realms is not installed"), r


@needs_godot
def test_mod_with_its_own_folder_gets_the_mod_argument(tmp_path, service):
    r = launch(tmp_path, service, "--mod-get=test-tales", "--mod-consent", "--mod-use=test-tales", "--mod-args")
    base = mods_dir(tmp_path) / "test-tales"
    assert r.code == 0 and r.has("2 files downloaded"), r
    assert r.has(f"LAUNCHER game arguments: -- --mod={logged(base / 'mod')}") and not r.has("--mod-install"), r
    assert sorted(str(p.relative_to(base)) for p in base.rglob("*") if p.is_file()) == ["mod/!!tales.big", "mod/data/ini/object/tale.ini", "openbfme-aio.json"]
    record = json.loads((base / "openbfme-aio.json").read_text())
    assert record["left_out"] == ["Tale Maps: tale\\tale.ini (a map: maps from the service are not supported yet)"], record


@needs_godot
def test_mod_that_needs_engine_features_installs_without_them(tmp_path, service):
    r = launch(tmp_path, service, "--mod-get=test-siege", "--mod-consent")
    assert r.code == 0 and r.has("LAUNCHER MODS done: Sample Siege 0.3 (needs engine features), 4 files downloaded"), r
    base = mods_dir(tmp_path) / "test-siege" / "install"
    assert sorted(str(p.relative_to(base)) for p in base.rglob("*") if p.is_file()) == ["__mup_patch.big", "__siege.big", "data/ini/weapon.ini", "lang/englishpatch99.big"]
    assert (base / "lang" / "englishpatch99.big").read_bytes() == service.data[("test-siege", "lang\\englishpatch99.big")], "L2 wins over L3"
    record = json.loads((mods_dir(tmp_path) / "test-siege" / "openbfme-aio.json").read_text())
    assert record["engine_edits"] == ["Sample Siege: d3d9.dll", "Made-Up Patch 9.9: lotrbfme2ep1.exe", "Made-Up Patch 9.9: game.dat"], record
    assert "Made-Up BFME2 Patch: INI.big (for another game: not mounted from a mod yet)" in record["left_out"], record
    for name in ("d3d9.dll", "lotrbfme2ep1.exe", "game.dat", "INI.big", "Patcher.exe"):
        assert not list((mods_dir(tmp_path) / "test-siege").rglob(name)), name


@needs_godot
def test_mod_a_bad_package_or_file_is_refused(tmp_path, service):
    r = launch(tmp_path, service, "--mod-get=test-broken", "--mod-consent")
    assert r.code == 3 and r.has("not a plain relative path"), r
    assert service.file_requests() == [] and not (mods_dir(tmp_path) / "test-broken").exists()
    r = launch(tmp_path, service, "--mod-get=test-realms", "--mod-with=test-zoom", "--mod-consent")
    assert r.code == 1 and r.has("Zoom For The Original does not extend Example Realms"), r
    assert service.file_requests() == []
    # a file whose bytes are not the listed MD5 is deleted, nothing is recorded
    f = service.file("test-realms", "__realms_data.big")
    service.server.files[service.file_path(f)] = b"x" * f["Size"]
    r = launch(tmp_path, service, "--mod-get=test-realms", "--mod-consent")
    assert r.code == 3 and r.has("__realms_data.big of Example Realms from the service has MD5"), r
    base = mods_dir(tmp_path) / "test-realms"
    assert not (base / "install" / "__realms_data.big").exists() and not list(base.rglob("*.part")) and not (base / "openbfme-aio.json").exists()


@needs_godot
def test_mod_a_cut_download_resumes(tmp_path, service):
    f = service.file("test-realms", "lang\\English.big")   # listed with Size 0: the server's length is used
    path = service.file_path(f)
    service.server.truncate[path] = 1000
    r = launch(tmp_path, service, "--mod-get=test-realms", "--mod-consent")
    assert r.code == 2 and r.has("lang/English.big of Example Realms"), r
    base = mods_dir(tmp_path) / "test-realms"
    assert (base / "install" / "lang" / "English.big.part").stat().st_size == 1000
    assert not (base / "openbfme-aio.json").exists()
    r = launch(tmp_path, service, "--mod-args")
    assert r.code == 0, r   # nothing picked: a half-installed mod is not started
    r = launch(tmp_path, service, "--mod-get=test-realms", "--mod-consent")
    assert r.code == 0 and r.has("resumed lang/English.big at 1000 bytes"), r
    assert [h.get("range") for p, h in service.file_requests() if p == path][-1] == "bytes=1000-"
    assert (base / "install" / "lang" / "English.big").read_bytes() == service.data[("test-realms", "lang\\English.big")]
