# FAQ

## What is OpenBFME?

A Godot engine project plus a Python importer. It converts *Rise of the
Witch-king* / BFME2 content **on your PC** from a game install you own.
Retail-derived files live under `workspace/`; use them freely. Git ignores
`workspace/` and the publication-boundary CI scans tracked files for retail
bytes and machine-absolute paths — that is the whole policy.

## Do I need the game?

Yes for real play. Target is **RotWK 2.01** (with BFME2 base). We do not ship
pre-converted packs.

## What does "fail closed" mean?

If something required is missing or wrong, tools **error out** instead of
quietly inventing content. That keeps bugs visible.

## Is it playable?

Developer alpha. Skirmish shell and convert tools exist; coverage is uneven.
See [orchestration/queue.md](../orchestration/queue.md) and [docs/state/](state/).

## RotWK or BFME2?

**RotWK** is the main target. BFME2 alone is optional comparison
(`--game bfme2`). Campaigns and War of the Ring are planned later
([DIRECTION.md](../DIRECTION.md)), not finished features.

## Can I use Codex to learn the code?

Yes. Clone the repo and point [Codex](https://openai.com/codex/) (or a similar
agent) at it. Ask for entry points, how convert -> pack -> launch works, and
fix ideas with a small check command. Confirm with real gates.

## Where does OpenSAGE fit?

We take inspiration and research cues from
[OpenSAGE](https://github.com/OpenSAGE/OpenSAGE) and use the
[OpenSAGE BlenderPlugin](https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin) as
an **external** convert helper. We do not ship OpenSAGE as our engine. See
[OPENSAGE_GAP_MATRIX.md](OPENSAGE_GAP_MATRIX.md) and [THIRD_PARTY.md](THIRD_PARTY.md).

## How do I mod?

Start with the real example pack `game/mods/example_hard_orcs/` and
[MODDING.md](MODDING.md).

## Multiplayer?

Lockstep + lobby foundations exist. Not a polished online service yet.

## How do I help?

[CONTRIBUTING.md](../CONTRIBUTING.md). Small, tested changes.

## License

Project source is [Unlicense](../LICENSE) (public domain). Game assets and
trademarks stay with their owners.
