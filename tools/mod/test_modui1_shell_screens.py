"""Lane MODUI-1: every shell screen of a retail mod works as on 2.01 (owner 2026-10-30: Edain's Skirmish setup opened empty: its MpGameSetup.apt was
refused by the port's Apt loader, so the lobby showed the frame only). For RotWK 2.01 and for every mod of OPENBFME_MOD_CORPUS that ships Apt movies
of its own, the game runs the MENU-1 walk headless (game.gd --menu1-walk, scripts/menu1_walk.gd: Solo Play -> Skirmish with its profile box and lobby,
the lobby's map list and slots, three players set up, Options, LAN, Online, Replays, Load, Credits, Create-a-Hero; MENU1_SHELL_ONLY: the game
itself is not started, a mod's game is the simulation's tests' business): a mod must fail no step that 2.01 passes (a shell
error such as a movie that did not load, a layering problem, an empty lobby) and leave no call unhandled that 2.01 handles. 2.01's own failed steps
are printed (MODUI1 2.01 ...): they are gaps of the port's 2.01 screens, not of mod support.
The C++ side (engine/tests/test_mods1_retail.cpp "MODUI-1 mod corpus") loads every Apt movie of every corpus mod.
Needs Godot with the openbfme extension (GODOT, else `godot`), ROTWK_INSTALL / BFME2_INSTALL and OPENBFME_MOD_CORPUS: SKIP LOUDLY otherwise.
OPENBFME_MODUI1_JOBS sets how many games run at once (default 4)."""
from __future__ import annotations

import concurrent.futures
import json
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")
CORPUS = os.environ.get("OPENBFME_MOD_CORPUS", "")


def _skip_reason() -> str | None:
    if not shutil.which(GODOT):
        return "Godot is not installed here (GODOT)"
    if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
        return "ROTWK_INSTALL / BFME2_INSTALL are not set"
    if not CORPUS:
        return "OPENBFME_MOD_CORPUS is not set"
    if not list((REPO / "godot" / "bin").glob("openbfme.*")):
        return "the openbfme GDExtension is not built (godot/bin)"
    return None


def big_names(path: Path) -> list[str]:
    """The file names of a BIGF / BIG4 archive (its directory only)."""
    with open(path, "rb") as f:
        head = f.read(16)
        if len(head) < 16 or head[:4] not in (b"BIGF", b"BIG4"):
            return []
        count, header_size = struct.unpack(">II", head[8:16])
        table = f.read(max(0, header_size - 16))
    names, at = [], 0
    for _ in range(count):
        end = table.index(b"\0", at + 8)
        names.append(table[at + 8:end].decode("latin-1"))
        at = end + 1
    return names


def _ships_apt(folder: Path, movie: str = "") -> bool:
    """Whether the folder (or archive) holds an Apt movie: any, or the one named (lower-case "<name>.apt")."""
    def hit(name: str) -> bool:
        n = name.lower().replace("/", "\\").rsplit("\\", 1)[-1]
        return n == movie if movie else n.endswith(".apt")
    files = [folder] if folder.is_file() else [p for p in folder.rglob("*") if p.is_file()]
    for p in files:
        if hit(p.name):
            return True
        if p.suffix.lower() == ".big" and any(hit(n) for n in big_names(p)):
            return True
    return False


def corpus_mods(corpus: Path) -> list[tuple[str, list[str], list[Path]]]:
    """(name, game arguments, the folders / archives it mounts) per corpus entry, laid out as engine/tests/test_mods1_retail.cpp mounts it: an All In
    One package's stack install/ folders (in the record's order) are --mod-install folders, its first mod/ folder a --mod operand; any other folder
    or .big is a --mod operand."""
    out = []
    for p in sorted(corpus.iterdir()):
        if not (p.is_dir() or p.suffix.lower() == ".big"):
            continue
        record = p / "openbfme-aio.json"
        if not record.exists():
            out.append((p.name, [f"--mod={p}"], [p]))
            continue
        manifest = json.loads(record.read_text(encoding="utf-8"))
        stack = manifest.get("stack")
        if not isinstance(stack, list):
            continue  # a dependency laid out for another package
        args, parts, mod = [], [], None
        for member in stack:
            folder = p.parent / member["folder"]
            if (folder / "install").is_dir():
                args.append(f"--mod-install={folder / 'install'}")
                parts.append(folder / "install")
            if mod is None and (folder / "mod").is_dir():
                mod = folder / "mod"
        if mod is not None:
            args.append(f"--mod={mod}")
            parts.append(mod)
        # the stack's modified game programs: the launcher passes each one's MD5 (lane MODS-3, Common/ModEngineFeatures.h)
        args += [f"--mod-engine={e.rsplit(' md5 ', 1)[1]}" for e in manifest.get("engine_edits", []) if " md5 " in e]
        out.append((p.name, args, parts))
    return out


def walk(args: list[str]) -> tuple[int, list[str]]:
    home = tempfile.mkdtemp(prefix="openbfme-modui1-home-")
    try:
        env = {**os.environ, "MENU1_SHELL_ONLY": "1", "HOME": home, "XDG_DATA_HOME": os.path.join(home, ".local/share"), "XDG_CONFIG_HOME": os.path.join(home, ".config")}
        r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--", "--menu1-walk", "--movies=off", *args], env=env,
                           capture_output=True, text=True, timeout=1500)
        # the walk's lines, and why a run stopped before the walk (the game's GAME FAIL / RELEASE lines)
        return r.returncode, [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(("MENU1 ", "GAME FAIL", "RELEASE ERROR", "ERROR:"))]
    finally:
        shutil.rmtree(home, ignore_errors=True)


def _list_line(lines: list[str], prefix: str) -> set[str]:
    for l in lines:
        if l.startswith(prefix):
            return set(json.loads(l[len(prefix):]))
    return set()


def failed_steps(lines: list[str]) -> set[str]:
    """ "<step>: <why>" of every failed MENU-1 step."""
    return {l[len("MENU1 "):].replace(": FAIL ", ": ", 1) for l in lines if l.startswith("MENU1 ") and ": FAIL " in l}


def unhandled(lines: list[str]) -> set[str]:
    """The engine callbacks (fscommands) a movie called that the port does not handle. A script's call of a function it never defined is skipped
    silently by retail (stop S-380), so those ("MENU1 calls without function") are printed, not compared."""
    return _list_line(lines, "MENU1 unhandled commands: ")


def test_ships_apt_reads_big_directories(tmp_path):
    names = [("MpGameSetup.apt", b"x"), ("data\\ini\\a.ini", b"y")]
    table = b"".join(struct.pack(">II", 0, len(d)) + n.encode() + b"\0" for n, d in names)
    head = b"BIGF" + struct.pack("<I", 0) + struct.pack(">II", len(names), 16 + len(table))
    (tmp_path / "m").mkdir()
    (tmp_path / "m" / "a.big").write_bytes(head + table)
    assert big_names(tmp_path / "m" / "a.big") == ["MpGameSetup.apt", "data\\ini\\a.ini"]
    assert _ships_apt(tmp_path / "m")
    assert _ships_apt(tmp_path / "m", "mpgamesetup.apt") and not _ships_apt(tmp_path / "m", "mainmenu.apt")
    (tmp_path / "n").mkdir()
    (tmp_path / "n" / "b.big").write_bytes(b"BIGF" + struct.pack("<I", 0) + struct.pack(">II", 0, 16))
    assert not _ships_apt(tmp_path / "n")


def test_every_shell_screen_of_every_corpus_mod_works_as_on_201():
    why = _skip_reason()
    if why:
        pytest.skip("SKIP LOUDLY: " + why + ", so the mods' shell screens were not checked")
    entries = [(name, args, parts) for name, args, parts in corpus_mods(Path(CORPUS)) if any(_ships_apt(part) for part in parts)]
    mods = [(name, args) for name, args, parts in entries]
    own_menu = {name for name, args, parts in entries if any(_ships_apt(part, "mainmenu.apt") for part in parts)}
    assert mods, f"no mod of {CORPUS} ships an Apt movie"
    jobs = int(os.environ.get("OPENBFME_MODUI1_JOBS", "4"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        base = pool.submit(walk, [])
        runs = {name: pool.submit(walk, args) for name, args in mods}
        code, base_lines = base.result()
        assert any(l.startswith("MENU1 RESULT: ") for l in base_lines), f"2.01: exit {code}\n" + "\n".join(base_lines)
        retail_fails = failed_steps(base_lines)
        for f in sorted(retail_fails):
            print(f"MODUI1 2.01 itself: FAIL {f}")
        retail = unhandled(base_lines)
        failures = []
        for name, run in runs.items():
            code, lines = run.result()
            result = next((l for l in lines if l.startswith("MENU1 RESULT: ")), "")
            fails = sorted(failed_steps(lines) - retail_fails)
            if name in own_menu:
                # the mod's own main menu leaves an entry out: no screen to check there (printed, not failed)
                left_out = [f for f in fails if ": the menu has no button " in f]
                for f in left_out:
                    print(f"MODUI1 {name}:   its own MainMenu.apt leaves this out: {f}")
                fails = [f for f in fails if f not in left_out]
            for n in sorted(_list_line(lines, "MENU1 mod data notes: ")):
                print(f"MODUI1 {name}:   {n}")
            extra = sorted(unhandled(lines) - retail)
            print(f"MODUI1 {name}: {result or 'no MENU1 RESULT line'}, {len(fails)} step(s) and {len(extra)} unhandled command(s) beyond 2.01's")
            calls = sorted(_list_line(lines, "MENU1 calls without function: ") - _list_line(base_lines, "MENU1 calls without function: "))
            for c in calls:
                print(f"MODUI1 {name}:   the mod's script calls what it never defined (retail skips it, S-380): {c}")
            if not result or fails or extra:
                why = [l for l in lines if l.startswith(("GAME FAIL", "RELEASE ERROR", "ERROR:"))][:5] if not result else []
                failures.append(f"{name}: exit {code}, {result or 'no MENU1 RESULT line'}\n  " + "\n  ".join(why + [f"FAIL {f}" for f in fails] + [f"unhandled: {e}" for e in extra]))
        assert not failures, "\n".join(failures)
