# FAQ

## What is OpenBFME?

A code-only Godot engine + Python importer that converts RotWK/BFME2 content
**locally** from an install you own. Not a remake download, not EA assets in Git.

## Do I need to own the game?

Yes. Product baseline is **RotWK 2.01** (BFME2 base as the layered catalog
resolves it). No preconverted packs are distributed.

## Is this legal?

Engineering policy: code-only repo; conversion on your machine. That is not legal
advice. Do not redistribute retail or converted retail content.

## Is it playable?

Developer alpha: skirmish shell and convert tooling exist. Coverage is uneven.
Men/Fords is the deepest legacy gate surface, not the long-term strategy.
See [STATUS.md](../STATUS.md).

## RotWK or BFME2?

**RotWK 2.01** is the parity baseline (importer default). BFME2-only remains
`--game bfme2` for comparison. Campaigns and War of the Ring are later ladder
steps in [DIRECTION.md](../DIRECTION.md), not finished features.

## Why Godot?

Open license, no mandatory store/royalty stack, good fit for a self-hosted RTS
client. The project is not affiliated with BFME Reforged or EA.

## Multiplayer?

Lockstep + ENet foundations and an in-game lobby exist and have headless gates.
Not a hardened production service yet.

## How do I help?

[CONTRIBUTING.md](../CONTRIBUTING.md). Small, tested changes. Never commit
`.private/`, retail files, or secrets.
