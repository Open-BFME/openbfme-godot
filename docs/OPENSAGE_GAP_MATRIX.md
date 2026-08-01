# OpenSAGE → OpenBFME gap matrix

**Owner:** integration owner  
**Owns:** cross-project parity checklist (oracle only)  
**Does not own:** product scope (`DIRECTION.md`) or volatile gate numbers (`STATUS.md`)  
**Update trigger:** new OpenSAGE demo surfaces, or OpenBFME systems-ladder moves  
**Validation:** map each row to `docs/MILESTONE_CURRENT.md` systems and a focused proof

## Why this exists

[OpenSAGE](https://github.com/OpenSAGE/OpenSAGE) is a **C# reimplementation of the SAGE
runtime** (Generals first; BFME-family formats expanding). Demos often **load and
present retail installs** earlier than a Godot product pack path can.

OpenBFME is a different finish line:

| Axis | OpenSAGE | OpenBFME |
|------|----------|----------|
| Goal | Engine that runs SAGE content from install trees | **RotWK 2.01 product** in Godot via fail-closed convert → packs |
| “Working” | Parse/render/simulate from INI/W3D/MAP/APT | Compiled contracts + runtime consumers + pack audit |
| Legal/process | Legal install; no EA binaries redistributed | No invent greening; no silent retail payloads in public tree |

Use OpenSAGE demos as a **wiring backlog oracle**, not a license to skip convert
discipline or load `.big` as the product runtime.

Public channel/video references used as orientation (not exhaustive):

- [OpenSAGE YouTube](https://www.youtube.com/@opensage1853)
- Examples: [lQsYAMiQ0s8](https://www.youtube.com/watch?v=lQsYAMiQ0s8),
  [nDXarrjqxrg](https://www.youtube.com/watch?v=nDXarrjqxrg),
  [JyhOUE8jSz4](https://www.youtube.com/watch?v=JyhOUE8jSz4)

## Matrix (surface → ladder → our state → next proof)

Statuses: **ahead** / **partial** / **behind** / **different** (not comparable).

| OpenSAGE-visible surface | Ladder system | OpenBFME state (2026-07) | Gap / next proof |
|--------------------------|---------------|--------------------------|------------------|
| Install discovery + BIG archives | Pipeline | **partial** — catalog + doctor + layered RotWK/BFME2 | Keep fail-closed provenance; no silent archive mix |
| Map load / multiplayer corpus | Maps | **partial** — 72 MP maps cook corpus; 69 connected / 3 disconnected starts | Connectivity + nav mesh; terrain materials via layered assets |
| Terrain textures / cliff blend | Maps / assets | **partial** — RotWK `terrain.big` thin; layered install supplies BFME2 base | Full-profile multi-map cook with layered install |
| Map props / object placement bind | Object binding | **partial** — binding factory ~**79.7%** bound over 72 maps | Burn unbound types (CaptureFlag, Outpost, Inn, lairs, trees…) |
| W3D mesh + hierarchy draw | Assets | **partial** — GLB convert path; not full SAGE draw pipeline | W3D action/skin depth; presentation parity checks |
| Unit animations / clips | Assets / sim | **partial** — clip streams exist; not all retail actions wired | Animation consumer coverage per battalion |
| INI object modules as engine | Assets / sim | **different** — module census + **contracts** + compilers | Keep contract bar; grow runtime consumers |
| Weapons / projectiles | Sim | **partial** — compilers + slice consumers | Multi-faction weapon runtime proof |
| Construction / build plots | Sim | **partial** — structure runtime + build plots runners | Multi-map buildability generation |
| Selection / command UI | Shell | **partial** — HUD APT convert + retail HUD oracles | RotWK shell chrome completeness |
| APT menus / strategic chrome | Shell / WOTR | **partial** — strategic APT convert; WOTR shell in flight | APT action coverage vs OpenSAGE demos |
| Particles / FX lists | Assets | **partial** — particle profile path; not full engine FX | FX list runtime parity checklist |
| Scripts / AI | AI / scripts | **partial** — script pack + WP handlers growing | Script world surface + AI library composition |
| Skirmish multi-map shell | Shell | **partial** — registry catalog 72 maps; pack cook optional | Multimap pack + entry map launch without invent terrain |
| Faction convert completeness | Pipeline | **ahead (discipline)** — 7 factions converter-gap **0**; batch now writes durable coverage + object artifacts | Keep convert bar; do not invent-green residual census leaves |
| Pack / runtime receipt | Pipeline | **partial → improving** — convert filters visual inventory to catalog winners + asserts recipe patterns archive-resolve; **angmar** pack cook/audit **PUBLICATION_READY** (`rotwk_faction_pack_proof.py`) | Re-prove remaining 6 factions; standalone `compile_unit_recipe` still lacks catalog filter |
| Pack publish / selection | Pipeline | **partial** — publish-faction-to-slice; selection owner-gated | No silent `selection.json` rewrite |
| Campaigns | Campaigns | **behind** | After skirmish shell systems |
| War of the Ring living map | WOTR | **partial** — regions, handoff, autoresolve, UI in progress | Strategic gap register burn-down |
| Multiplayer netcode | MP | **behind / different** | After single-player systems floor |
| Create-a-Hero | Assets / shell | **partial** — CaH special powers census merge | Full CaH runtime + UI |

## What we intentionally do *not* copy

1. **Black-box SAGE VM in C# as the product** — OpenBFME ships Godot + packs.
2. **Pretty wrong content** — missing ModifierLists / UI textures stay gaps or
   policy exclusions, not invent-greened units.
3. **One-map vertical-slice freeze as strategy** — systems-first RotWK ladder.

## Operator hooks (OpenBFME proofs)

```text
# Map cook corpus (72)
python tools/rotwk_map_cook_corpus.py --install <ROTWK>

# Object binding burn-down
python tools/rotwk_binding_factory.py --install <ROTWK>

# Multi-map catalog proof (no selection rewrite)
python tools/rotwk_multimap_skirmish.py --install <ROTWK>

# 7-faction convert + durable coverage/artifacts
python tools/rotwk_faction_convert_batch.py --install <ROTWK>

# Pack/runtime receipt (publication stage)
python tools/rotwk_faction_pack_proof.py --install <ROTWK> --faction angmar

# Offline gate
powershell -ExecutionPolicy Bypass -File tools/gate-rotwk-systems.ps1 -SkipLiveRetail
```

## Suggested priority from demo oracles

1. **Map presentability** — terrain materials + connected starts + prop bind %  
2. **W3D / anim depth** on already-converted playable units  
3. **Shell APT** surfaces OpenSAGE shows working  
4. **Particles / FX** after unit/structure visual closure  
5. Keep **convert + pack receipt** floor while wiring presentation

## Bottom line

OpenSAGE can look **ahead on engine load/render of retail SAGE**. OpenBFME can be
**ahead or stricter on fail-closed import, multi-faction convert, and product pack
gates**. The matrix above is the translation layer between those two scoreboards.
