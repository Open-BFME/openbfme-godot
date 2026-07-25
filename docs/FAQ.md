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
factions (Men, Elves, Dwarves, Isengard, Mordor, Goblins/Wild, Angmar), eight
cooked skirmish maps, five AI difficulty tiers, per-team spellbooks, hero
abilities, optional creep lairs, and early lockstep multiplayer with an in-game
lobby. Cross-faction matches run. Coverage is uneven across factions - Mordor
is materially behind the others - and presentation and reliability work remains
open. Use [STATUS.md](../STATUS.md) for the exact evidence boundary.

## Is the whole game implemented?

No. Full skirmish parity, hardened production multiplayer, replays, and several
shell and presentation systems remain unfinished. All seven faction surfaces
exist and are exercised by per-faction gates, but coverage depth still varies
by faction. The campaigns are in scope and in progress. War of the Ring is not
a later milestone: it is explicitly outside project scope. Create-a-Hero may be
considered after the core skirmish and multiplayer work.

## Why did the project start with one faction on one map?

Because a credible small slice is more valuable than broad shallow coverage.
Proving Men against Men on Fords of Isen II end to end forced the importer,
simulation, rendering, UI, audio, gameplay, original-game comparison, and
reliability processes to work together before any of them was scaled out. That
slice has since been generalised to seven factions and eight maps.

## BFME2 or Rise of the Witch-king?

Rise of the Witch-king 2.01 is the product baseline. BFME2 1.06 is the base game
underneath it and remains the source of evidence for everything 2.01 does not
change. Angmar is a playable faction, not an optional extension.

The project started against 1.06 alone because it was the smaller, better
measured baseline; that ordering is history, not the current target.

## Are campaigns or War of the Ring planned?

The Good and Evil campaigns are in scope and being built - maps, scripting,
objectives, and cinematics. The lane has its own plan in
[CAMPAIGN_PLAN.md](CAMPAIGN_PLAN.md).

War of the Ring is not planned. The strategic layer lives in `livingworld*.ini`
data that nothing in the pipeline imports, and its maps carry almost no
scripting. That is a scope decision rather than a sequencing one.

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
