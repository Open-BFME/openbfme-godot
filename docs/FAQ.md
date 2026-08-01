# Frequently asked questions

## Is this a remake, port, engine, or mod?

The most accurate short description is an open-source compatibility engine. It
reimplements gameplay and presentation in a modern runtime and converts required
content locally from a **Rise of the Witch-king 2.01** installation (with BFME2
base as the importer overlay resolves it) supplied by the user.

## Does the repository contain BFME2 assets?

It must not. Retail archives, extracted files, decoded media, converted models,
textures, audio, maps, screenshots used as private oracle evidence, and runtime
retail packs remain outside Git under `.private`.

## Will I need to own RotWK / BFME2?

Yes for the retail-compatibility path: **RotWK 2.01** is the product baseline
(BFME2 base is required as the layered install resolves it). OpenBFME does not
provide a copy of the game or a preconverted asset pack.

## Is this legal?

This repository follows a code-only engineering boundary: users provide their
own lawfully acquired installation and conversion happens locally. That is an
engineering policy, not legal advice or a court ruling. Public distribution also
requires license, provenance, trademark, and clean-export review.

## Is it playable right now?

As a private developer-playable alpha, yes; as a finished release, no. The
current tree offers a skirmish shell with N-player setup, seven selectable
faction surfaces (the six BFME2 factions plus an Angmar import from RotWK
2.01), five source maps, five AI difficulty tiers, per-team spellbooks, hero
abilities, optional creep lairs, and early lockstep multiplayer with an
in-game lobby. Men versus Men on Fords of Isen II remains the most deeply
verified slice; coverage across the other factions and maps is substantial but
uneven, and presentation and reliability work remains open. Use
[STATUS.md](../STATUS.md) for the exact evidence boundary.

## Is the whole game implemented?

No. Full skirmish parity, hardened production multiplayer, replays, and several
shell and presentation systems remain unfinished. All seven faction surfaces
exist and are exercised by per-faction gates, but coverage depth still varies
by faction. Campaigns and War of the Ring are in
scope as ladder steps 6 and 7 - late, because they depend on the faction, hero
and scripting work beneath them. Create-a-Hero is ladder step 5.

## Why did early work emphasize Men versus Men on one map?

Historically, a small deep slice forced importer, simulation, rendering, UI,
audio, and reliability to work together. That **Men/Fords vertical-slice freeze
is no longer the product strategy**. Development is systems-first against
RotWK 2.01 data (see `DIRECTION.md` and `docs/MILESTONE_CURRENT.md`). Men on
Fords remains the most deeply gate-verified developer slice for legacy
acceptance tooling.

## Why RotWK rather than BFME2 alone?

**Jonathan (project owner)** set RotWK 2.01 as the parity baseline so the
project does not run a second full parity program after BFME2. RotWK is a
content superset (including Angmar). BFME2 1.06 remains available as an
optional comparison install (`--game bfme2`) for diagnostics.

## Are campaigns or War of the Ring planned?

Yes, as **later ladder steps**, not as the current active objective. Product
scope includes skirmish first, then shell/Create-a-Hero, campaigns (as shipped
by RotWK), and War of the Ring. See `DIRECTION.md`. They are not started as
shipped playable features yet.

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

The foundations exist today: deterministic lockstep over ENet with an in-game
lobby (player names, chat, settings, per-peer faction choice), verified by
headless determinism and network gates. The target remains self-hosted,
server-refereed play for up to eight players without mandatory Steam or
central accounts; hardening toward that production quality is ongoing work,
not a finished feature.

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
