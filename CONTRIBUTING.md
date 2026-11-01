# Contributing to OpenBFME

Thanks for wanting to help. Bug fixes, ports of missing game behaviour, tests, tools and documentation are all welcome. This page says
how a pull request gets from your fork into a release.

## The one rule: no game files

OpenBFME runs on the player's own copy of The Rise of the Witch-king 2.01 and The Battle for Middle-earth II 1.06. This repository
never contains anything from those games, and a pull request that adds any of it cannot be merged:

- no files from the game's install or its archives (`.big`, `.w3d`, `.dds`, `.tga`, `.wav`, `.mp3`, `.map`, `.str`, `.csf`, `.apt`,
  `game.dat` ...), whatever their name;
- nothing extracted, converted or re-encoded from them (textures saved as PNG, sounds as OGG, maps as JSON, string tables as text);
- no screenshots or recordings of the original game in the repository.

Code reads the game's files at runtime from the folders in `ROTWK_INSTALL` and `BFME2_INSTALL`. A test that needs them uses the shared
retail mount (`engine/tests/RetailTestMount.cpp`) and skips, saying so, when the folders are not set. Test data you need is generated
by the test or committed as plain numbers you measured, never as game bytes. The commit gate, `tools/precommit.py`, refuses game file
types by name and by content; install it as your pre-commit hook:

```sh
ln -s ../../tools/precommit.py .git/hooks/pre-commit      # Windows: copy it there instead
```

Please also keep personal data out: no paths of your own machine, no e-mail addresses in files, no logs of your install.

The gate also refuses conflict-marker lines in the **staged blob**, not the working tree, reporting the file and 1-based line number.
It checks every changed text file (including pure renames): lines starting with seven `<` or seven `>`, and lines consisting of exactly
seven `=` (LF or CRLF). NUL-containing binary blobs are skipped by this check only; the byte-format checks still apply. Markdown and
AsciiDoc underlines of other lengths, such as `=====`, are allowed. The current tree has no matching documentation underlines or
literal test fixtures, so there is **no allowlist and no path exemption**; tests should generate marker lines at runtime. Reads use one
`git cat-file --batch` process, one blob at a time, to keep large commits fast without buffering all their contents.

## Opening a pull request

1. Fork the repository and branch from `main`.
2. Build and run the unit tests as [Build from source](README.md#build-from-source) describes. Without the game folders the tests that
   need the game skip; that is fine for most changes. If you have the games, run them with the folders set too.
3. Keep the pull request to one change. Follow the code around it: keep the original SAGE (Zero Hour) class and file names, and for game
   behaviour name the source it follows in a comment (the retail game, a BFME decompile or the Zero Hour source). We port the original
   behaviour; we do not invent new rules. Simulation code must stay deterministic (it runs in lockstep multiplayer across Windows and
   Linux): its maths goes through `engine/src/GameLogic/SimMath.h`.
4. Open the pull request against `main` and fill in the template.

## Before you open a PR

- Stage your changes with `git add`, then run `python3 tools/precommit.py` from the repository root. The commit gate checks staged
  files, not unstaged edits.
- Assert exact expected values in tests: for an expected count of 3, use `CHECK(count == 3);`, not `CHECK(count > 0);` or
  `doctest::Approx`. If a bound really is needed, explain why with `// tolerance: <reason>` (GDScript `# tolerance: <reason>`) on the
  same line or the line above.
- If you add or close rows in `docs/STOPS.md`, run `python3 tools/stops_count.py --write` and include the updated `README.md` count.
- Retail-data tests need your own game files through `ROTWK_INSTALL` and `BFME2_INSTALL`; they skip without them, as on public CI.
  The Godot smoke test also needs the installed games. Never add game files to your PR.
- Install the Python test dependencies with `python -m pip install -r tools/requirements-test.txt` (a virtual environment is handy).
- Run the Python tests with `python -m pytest -n 6 -q tools`. Some tests also need game files, built helpers or extra dependencies;
  public CI runs the subset that needs none of those.

## What we check

Every pull request runs `.github/workflows/pr.yml` on GitHub's own runners, with no secrets and read-only access. The check named
**CI** sums it up: it passes only when every job below passed.

- the commit gate (`tools/precommit.py`) on the files your pull request changes;
- the public tree audit (`tools/release/pr_audit.py`): no game files, no private paths, no binaries or archives that are not listed,
  nothing in a folder that must stay out, and the rules for this `.github` folder (workflows that parse, with read-only permissions,
  no secrets, actions pinned by commit, GitHub's runners only);
- actionlint and zizmor on the workflows;
- a Linux build of the engine and its unit tests without game files;
- a Windows build (the release's llvm-mingw cross build), compile only;
- a macOS arm64 build of the game library and unit test executable, followed by the console filter regression tests;
- the Python tests that need no game files.

A maintainer then reviews the change. We may ask for changes, or for a test that shows the behaviour against the original game.

## How a pull request lands

This repository is published from the project's development line: each release brings `main` up to date with one "Sync the rebuild"
pull request. So a reviewed pull request lands like this:

1. **Reviewed.** A maintainer reviews your pull request at its current head commit.
2. **Ported.** Your commits are brought into the development line with you as their author, and go through the project's full test
   run with the game files.
3. **Released.** With the next release, your pull request is merged here first (with a merge commit, so it shows as merged and your
   commits are in `main`'s history), and the release's sync pull request credits it: "Includes #123 by @you".

Between review and release your pull request stays open; that can take a few days. The release only merges a pull request whose head
is still the reviewed commit and whose changes are exactly in the release, so please do not push to it after the review unless we ask
you to. If the port needed changes, we ask you to add them to your pull request (or push them to your branch, if you allow edits by
maintainers), so that what is merged here is exactly what was released.

## Licence

OpenBFME is licensed under the GNU General Public License v3.0 (see `LICENSE`). By opening a pull request you agree that your
contribution is published under it. Say in the pull request where any code you did not write yourself comes from, and under which
licence.
