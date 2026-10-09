# OpenBFME

A faithful recreation of **The Battle for Middle-earth II: Rise of the
Witch-king 2.01** in Godot. It runs on the original game files from your
own install. This repository contains no game assets.

Game logic is C++ in a GDExtension (`engine/`), translated from EA's
Command & Conquer Generals: Zero Hour source and the BFME decompiles, keeping
SAGE class and file names. Godot (`godot/`) is only the device layer. The
previous codebase lives on in this repository's history (tag `legacy-final`).

## Build, run, test

Needs Visual Studio 2022 (C++ x64), CMake, Ninja, Git and Godot 4.7.

    git submodule update --init          (godot-cpp 10.0.0-stable)
    build.bat                            (extension -> godot\bin, tests -> engine\build)
    set ROTWK_INSTALL=<RotWK dir>        (pure 2.01 = RotWK 2.01 + BFME2 1.06 archives)
    set BFME2_INSTALL=<BFME2 dir>
    set GODOT=<path to Godot_v4.7-stable_win64_console.exe>
    run_tests.bat                        (0 pass, 77 smoke skipped, 1 fail)
    %GODOT% --path godot                 (first-unit viewer; drag to orbit, wheel to zoom)
    %GODOT% --path godot res://scenes/w3d_viewer.tscn   (animated retail models; add -- --bench for 1,000 soldiers)

Install paths can also live in `user://install-paths.cfg` as `KEY=VALUE` lines.
Mounting checks every archive's size and md5 against `engine/data/retail-archives/`
and stops on any unknown or mismatched file (no opt-out). Hashes are cached in
`user://retail-md5-cache.tsv` by absolute path + size + last-write time, so only the first
mount (about 12 s for 213 archives) is slow; the load order is documented in
`engine/src/Common/RetailArchivePolicy.h`.

## Behaviour references

- RotWK 2.01 INI files: every number in the game.
- EA's Command & Conquer Generals: Zero Hour source (GPL-3): the SAGE
  engine that BFME was built on.
- [Open-BFME-1](https://github.com/Open-BFME/Open-BFME-1) and
  [Open-BFME-2](https://github.com/Open-BFME/Open-BFME-2): matching
  decompilations of BFME1 and BFME2.

GPL-3.0; see `LICENSE` and `NOTICE` (Zero Hour-derived parts and EA's terms).
