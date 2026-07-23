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
by faction. Campaigns and War of the Ring are not
later milestones: they are explicitly outside project scope. Create-a-Hero may
be considered after the core skirmish and multiplayer work.

## Why start with Men versus Men on one map?

Because a credible small slice is more valuable than broad shallow coverage. The
first milestone forces the importer, simulation, rendering, UI, audio, gameplay,
original-game comparison, and reliability processes to work together.

## Why BFME2 rather than Rise of the Witch-king?

BFME2 1.06 remains the primary compatibility target because it is the smaller,
better-measured baseline. Rise of the Witch-king 2.01 is now a supported
*optional* import source: users who also own RotWK can import Angmar as a
seventh faction through the same fail-closed pipeline. RotWK content is
isolated from BFME2 state and cannot silently change BFME2 compatibility
evidence, and RotWK campaign material stays out of scope.

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
