# OpenBFME — Active direction (integration owner)

**Updated:** 2026-07-13  
**Owner:** integration orchestrator (this session)  
**Authority:** overrides exploratory stage work when they conflict

## North star

Private **BFME2 1.06** playable skirmish port in **Godot 4.7**, using retail assets from a user-owned install, converted into private packs under `.private` only.

## This week’s definition of done (M2 visual slice)

A human can launch the retail path and play **Men vs Men on Fords of Isen II** where:

### Graphical / audiovisual fidelity (primary)
- All four Men units use imported retail models/materials/anims (Soldier, Archer, Tower Guard, Knight)
- All five structures use imported retail models (no procedural masonry fallback when pack GLBs exist)
- Fords terrain uses cooked retail terrain textures with Godot blending (not flat gray/decimated-only look)
- High-count map props resolve to converted retail meshes where conversion succeeds; unresolved types are listed, not silently faked as “done”
- Unit portraits, command buttons, and control-bar art come from converted retail UI leaves
- Music + unit VO + SFX for the slice roster route from the private pack

### Playable loop (required, rules may be approximate)
- Select / move / attack / death with imported presentation
- Farm income + train all four units from production buildings
- Place or use the five structure roles; damage/destroy enemy Fortress → win/lose
- Enemy AI completes a basic build/train/attack loop

### Explicit non-goals this week
- New synthetic proof stages 1–10 features
- Campaign / WotR / multiplayer
- Full oracle micro-parity for every weapon timing
- Declaring `vertical_slice_complete: true` without the checklist above
- Writing retail bytes outside `.private`
- Silent generic props presented as retail fidelity

## Architecture (unchanged)
```
F:\BFME2 → importer → .private content pack → Godot runtime (no BIG/W3D/INI/map at play)
```

## Scoreboard (daily)
1. Visual: terrain blend, props resolved %, structures retail-bound, 4 units on field
2. UI/audio: retail command art + strings + events wired
3. Play: four-unit production, fortress victory, AI finishes a match
4. Gates: focused retail tests green; do not weaken assertions

## Worker law
- One integration owner merges and re-issues contracts
- Every Codex/subagent task names allowed paths, forbidden paths, acceptance command
- No open-ended “improve the engine” briefs
- Fail closed in private parity mode
- Path locks: concurrent workers must not edit the same files
