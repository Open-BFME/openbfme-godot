# Frequently asked questions

## Is this a remake, port, engine, or mod?

The most accurate short description is an open-source compatibility engine. It
reimplements gameplay and presentation in a modern runtime and converts required
content locally from a BFME2 installation supplied by the user.

## Does the repository contain BFME2 assets?

It must not. Retail archives, extracted files, decoded media, converted models,
textures, audio, maps, screenshots used as private oracle evidence, and runtime
retail packs remain outside Git under `.private`.

## Will I need to own BFME2?

Yes for the retail-compatibility path. OpenBFME does not provide a copy of the
game or a preconverted asset pack.

## Is this legal?

This repository follows a code-only engineering boundary: users provide their
own lawfully acquired installation and conversion happens locally. That is an
engineering policy, not legal advice or a court ruling. Public distribution also
requires license, provenance, trademark, and clean-export review.

## Is it playable right now?

The most recent stable-enough diagnostic showed a private developer-playable
alpha rather than a finished release. Men versus Men on Fords of Isen II was the
best-covered slice, alongside a skirmish shell, six faction surfaces, a broad Men
roster, spellbook work, AI, and five source maps. Kimi's active UI rewrite is now
changing that surface, so there is no frozen current runtime identity. The
non-Men faction suites still had failing assertions, four maps lacked bound
props, and visual/reliability gates remained open. Use [STATUS.md](../STATUS.md)
for the exact evidence boundary.

## Is the whole game implemented?

No. Full skirmish parity, production multiplayer, replays, and several shell and
presentation systems remain unfinished. All six faction surfaces exist, but
their full runtime suites are not green. Campaigns and War of the Ring are not
later milestones: they are explicitly outside project scope. Create-a-Hero may
be considered after the core skirmish and multiplayer work.

## Why start with Men versus Men on one map?

Because a credible small slice is more valuable than broad shallow coverage. The
first milestone forces the importer, simulation, rendering, UI, audio, gameplay,
original-game comparison, and reliability processes to work together.

## Why BFME2 rather than Rise of the Witch-king?

BFME2 1.06 provides a smaller initial target. Rise of the Witch-king is outside
the current scope and cannot silently change BFME2 compatibility evidence.

## Are campaigns or War of the Ring planned?

No. OpenBFME is scoped to skirmish, multiplayer, and the engine/modding platform.
The Good and Evil campaigns, campaign maps and scripting, and War of the Ring
are not part of the product roadmap.

## Why Godot?

Godot is open source, has no mandatory royalty model, is approachable for modders,
and gives the project direct control over rendering, UI, input, audio, tooling,
and desktop integration.

## Why not Unreal Engine?

The project owner already knew Godot and wanted a genuinely open engine with a
straightforward license. Learning a different large engine would not have helped
the initial experiment reach a playable proof quickly.

## Is this connected to BFME Reforged?

No. OpenBFME is an independent project with a different technical strategy. It
focuses on a code-only engine and local conversion from a user-owned BFME2
installation. It is not an official continuation, fork, or endorsed competitor.

## Was this really built with AI?

Yes. AI models have written and reviewed a substantial amount of the code under
human direction. That makes evidence and review more important, not less. Read
[AI_DEVELOPMENT.md](AI_DEVELOPMENT.md) for the methodology and limitations.

## Will it support multiplayer?

The intended design is self-hosted, server-refereed deterministic lockstep for up
to eight players, without mandatory Steam or central accounts. That architecture
is decided but not yet a completed feature.

## Can presentation mods differ between players?

That is the target. Simulation packs and presentation packs are designed to have
separate identities so art and audio replacements need not change authoritative
gameplay. The complete production mod contract is not implemented yet.

## Which platforms are supported?

Development is currently Windows-first. Windows client and Windows/Linux server
support are goals, but public platform support must wait for clean-machine and
cross-platform verification.

## How can I help?

Start with [CONTRIBUTING.md](../CONTRIBUTING.md). Useful work is narrow,
source-backed, testable, and careful about the retail-content boundary.
