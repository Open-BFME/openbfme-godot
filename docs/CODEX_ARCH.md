# Codex architecture opinion (gpt-5.5, xhigh)

> Note: `gpt-5.6-sol` required a newer Codex CLI than installed (0.139.0), so this run used `gpt-5.5` at `model_reasoning_effort=xhigh`.

## Summary (aligned with what we implemented)

### A) Architecture
- Fixed sim tick independent of FPS (`SimClock` @ 10 Hz)
- Battalion is the sim atom; members are visual/casualty only
- Autoloads: ModLoader → ContentDB → GameState; Events bus
- Nodes display sim state; they do not own HP/orders truth

### B) Data packs
- Prefer **JSON content packs** for max moddability (what we ship)
- Optional later: compiled `.tres` cache for editor comfort
- Later packs override by `id` + `priority`

### C) BFME2 assets
- Never hardcode `F:\BFME2` into gameplay
- AssetResolver / pack `assets/` + env `OPENBFME_CONTENT`
- Extracted content is a **content pack**, not engine code

### D) Nav / walls
- Path **battalion center**, not every soldier
- Own blocker map for buildings/walls/gates
- Gates as portals; coarse steer first (stage 3), chunked navmesh later
- Avoid full global rebake every placement

### E) Pitfalls
1. Full agent per soldier → dies at scale  
2. Scene nodes as gameplay truth  
3. Hardcoded unit behavior in scripts  
4. Walls as meshes only (need topology)  
5. Public shipping of licensed retail assets  

### F) Module list
Codex suggested more split systems (Command/Movement/Combat…). Current code is a **cohesive SimWorld** for stages 1–4 speed; split toward Codex’s system list when SimWorld exceeds ~1.5k lines.

## Applied in repo
| Codex advice | Status |
|---|---|
| Fixed sim tick | ✅ `SimClock` |
| JSON packs + override | ✅ `data/base`, `mods/` |
| Sim vs view split | ✅ `sim/` + `view/` |
| Battalion atom | ✅ |
| Blocker obstacles | ✅ stage 3 lite |
| NavServer full bake | ⏳ later |
| System split | ⏳ when needed |
