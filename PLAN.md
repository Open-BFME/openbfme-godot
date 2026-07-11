# Open BFME — Master Execution Plan

**Working title:** Open BFME  
**Engine:** Godot 4.7 (desktop-first: Windows, later Linux/macOS)  
**Genre:** Single-player skirmish RTS inspired by *The Battle for Middle-earth II*  
**Predecessor:** `C:\Users\Jonathan\Desktop\middle-earth-rts` (browser prototype / design bible)  
**Retail reference install:** `F:\BFME2`  
**Owner intent:** Desktop product that *feels* like BFME2 skirmish, not a web experiment.

This document is the execution bible. Stages are ordered so each one is **playable**, **demoable**, and **unblocks the next**. Do not start Stage N+2 systems until Stage N exit criteria pass.

---

## 0. North star & non-goals

### North star (v1.0 “Skirmish Complete”)

A desktop game where you can:

1. Launch → pick **map + two factions + difficulty**
2. Freeform-build a base, train **battalions**, fight with **heroes + spellbook**
3. Use walls/gates/towers, stances, fear, power tree, hero revival
4. Defeat the enemy fortress against a competent skirmish AI
5. Look/sound like a modern low-fantasy RTS *in the BFME2 mold* (not a screenshot clone unless legally clean)

### Explicit non-goals for v1

| Out of scope | Why |
|---|---|
| War of the Ring campaign | Months of content + UI + rules |
| Online multiplayer | Needs lockstep/rollback redesign |
| Siege ladders / climb walls | Pathing complexity vs payoff |
| Full 1:1 ability parity every unit | Content treadmill; do “iconic subset” first |
| Shipping with EA/Tolkien assets | Legal landmine (see §1) |
| Browser build | Desktop is the product |

### Name note

GitHub already has [`chipgw/openbfme`](https://github.com/chipgw/openbfme) (SAGE-compatible engine research). Prefer:

- **Project folder / Godot project:** `open-bfme`
- **Product display name (private builds):** *Open BFME*
- **Public display name later:** something original (e.g. *Free Peoples Skirmish*, *Anduin Command*) if you ever distribute outside a private machine

---

## 1. Legal asset doctrine (read before any extraction)

You own a legal copy of BFME2 at `F:\BFME2`. That does **not** grant rights to redistribute models, textures, music, VO, maps, or UI.

### Three asset lanes (mandatory)

| Lane | Source | Allowed use | Ship / share? |
|---|---|---|---|
| **A — Reference only** | Extracted from `F:\BFME2` `.big` packs | Local private playtests, side-by-side comparison, measuring scale/timing/palette | **NO** — never commit to public git, never zip to friends as “the game” |
| **B — Original / generated** | Grok Imagine, Meshy/Tripo, hand-made, CC0 packs | Default runtime path | **YES** |
| **C — Licensed third-party** | Explicit commercial/CC licenses you track | Runtime path if license file present | **YES** if license allows |

### Repo hygiene rules

1. Godot project lives in `C:\Users\Jonathan\Desktop\open-bfme\game\` (git-tracked).
2. Extracted BFME2 content lives in `C:\Users\Jonathan\Desktop\open-bfme\_bfme2_extract\` (**gitignored**, outside `res://` if possible).
3. Runtime assets only under `game/assets/` and only lanes B/C.
4. Optional **private** Godot export preset “Dev Reference” can load Lane A via absolute path / user:// symlink — never the default export.
5. `tools/asset_policy.md` + `.gitignore` enforced from day 1.

**Execution stance for this plan:**  
Use Lane A aggressively for **design truth** (INI stats, silhouette, camera height, music *mood*). Ship the game on Lane B/C. Grok Imagine is the primary 2D factory; 3D via Meshy/Tripo/Blockbench/Blender, not W3D re-export into a public build.

---

## 2. What the retail install actually contains

Discovered layout under `F:\BFME2`:

| Archive / path | ~Size | Contents (modding community knowledge) |
|---|---|---|
| `w3d.big` | ~1.0 GB | 3D models (Westwood W3D / SAGE meshes + anims) |
| `textures0–4.big` | ~1.2 GB total | DDS/TGA textures for units, buildings, UI |
| `terrain.big` | ~340 MB | Terrain textures / terrain-related art |
| `music.big` | ~374 MB | Score / adaptive music streams |
| `audio.big` | ~372 MB | SFX + likely VO banks |
| `ambientstreams.big` | ~55 MB | Ambient loops |
| `maps.big` | ~54 MB | Official maps + map assets |
| `ini.big` | ~23 MB | **Balance & design data** (units, weapons, armor, powers) — gold |
| `bases.big` | ~2.5 MB | Prefab bases / living-world-ish data |
| `window.big` / `apt\*.big` | varies | Flash/APT UI packages (command bar, palantir, spellbook, CAH) |
| `data\movies\` | VP6 | Cinematics (skip for v1) |
| `data\cursors\` | ANI/CUR | Cursors (easy to remake) |
| `lang\` | | String tables / localization |
| `worldbuilder.exe` | | Official map editor (still useful as reference for map scale) |

### Extraction toolchain (Stage 0)

| Tool | Purpose |
|---|---|
| **FinalBIG** / FinalBIGv2 | Open `.big`, extract all / by path |
| **INI browser** (any text editor after extract) | Unit costs, HP, weapon sets, command sets |
| **W3D viewers / converters** (community: w3d plugins, OpenSAGE-related tools, bfme2-modding-utils) | Inspect meshes; **not** required for Godot if you regenerate art |
| **ffmpeg** | Convert extracted audio containers → `.ogg` / `.wav` for Godot (Lane A private only) |
| Custom Python scripts in `tools/` | Catalog manifests, hash files, build CSV of unit names → Godot resource stubs |

### Highest-ROI extracts (do these first, not all 4 GB of textures)

1. **Entire `ini.big`** → design database  
2. **String / language files** → official unit/building names list  
3. **UI icons subset** from textures / APT (for side-by-side “does our icon read?”)  
4. **One faction’s W3D + textures** (Gondor infantry + fortress) for scale reference only  
5. **Music track list + 2–3 ambient loops** for tempo/mood reference; replace with royalty-free / generated score for ship  

Do **not** start by converting the whole `w3d.big`. That’s a multi-week tar pit with almost no gameplay return.

---

## 3. Design inheritance from the browser prototype

Treat `middle-earth-rts` as a **solved design draft**, not code to port line-by-line.

### Keep (mechanics that already felt like BFME2)

- **Battalion** as sim atom (pooled HP, member visuals die as HP drops)
- Freeform building + fortress win condition
- Formations: line / column / wedge / loose
- Stances: aggressive / defensive / hold
- Veterancy + replenishment + banner carrier concept
- Terror / fear auras + hero immunity
- Spellbook tiers + power points from kills
- Farm efficiency radius
- Walls / gates / postern / towers / garrison
- Hero ability rank gates + revival cost scaling
- Ring / Gollum as optional mid-late spice
- Custom hero as v1.x garnish
- Faction data-driven plans for AI

### Re-architect (Godot-native)

| Browser | Godot |
|---|---|
| `G` singleton blob | Autoloads split by domain (`GameState`, `SimClock`, `AudioBus`, `Events`) |
| Three.js meshes on entities | Sim entities pure data; `UnitView` / `BuildingView` scenes bind by id |
| DOM HUD | Control-based HUD scenes + themes |
| Steering-only move | NavigationServer3D + avoidance + building collision |
| Procedural meshes | Imported GLB + MultiMesh for far LOD |
| `config.js` | `Resource` classes + `.tres` / CSV import |
| `selftest.js` headless | GUT or custom `--headless` scene tests |

### Balance bootstrap

Port numbers from `middle-earth-rts/src/config.js` first (already tuned for fun), then **diff against extracted INI** for “BFME2 authenticity pass” later (Stage 6+). Fun > authenticity early.

---

## 4. Target architecture (Godot 4.7)

```
open-bfme/
  PLAN.md                          ← this file
  docs/                            ← ADRs, art bible, faction sheets
  tools/                           ← extract, import, imagine batch scripts
  reference/                       ← notes, screenshots, INI summaries (no binaries)
  _bfme2_extract/                  ← GITIGNORED Lane A dumps
  _imagine_raw/                    ← GITIGNORED raw Grok outputs
  game/                            ← Godot project root
    project.godot
    addons/
    assets/
      art/                         ← Lane B/C only
      audio/
      ui/
      maps/
    data/
      factions/
      units/
      buildings/
      powers/
      damage_matrix.tres
    src/
      core/                        ← time, events, rng, save schema
      sim/                         ← pure gameplay (no Node3D required)
        battalion.gd
        building.gd
        combat.gd
        economy.gd
        orders.gd
        path_queries.gd
      view/                        ← 3D presentation
        battalion_view.tscn
        building_view.tscn
        vfx/
      ai/
      ui/
      maps/
      net/                         ← stub only; unused in v1
    scenes/
      boot.tscn
      main_menu.tscn
      match.tscn
      map_editor_lite.tscn         ← optional Stage 8
    tests/
```

### Core runtime loop

```
SimClock (fixed 10–20 Hz logical tick)
  → AI
  → Order resolution
  → Movement (nav agent desired velocity)
  → Combat / projectiles
  → Economy / construction / train queues
  → Powers / auras / status
  → Victory check
View (frame rate)
  → Interpolate transforms
  → AnimationTree / MultiMesh LOD
  → HUD bind to GameState snapshots
```

**Rule:** gameplay correctness never depends on FPS. Browser prototype mixed them; do not repeat that.

### Entity model

```gdscript
# Conceptual
class_name Battalion
var id: int
var type_id: StringName
var side: int                 # 0 player, 1 enemy, 2 wild
var faction_id: StringName
var hp: float
var hp_max: float
var pos: Vector2              # XZ plane; Y from heightmap
var facing: float
var stance: Stance
var order: Order              # move / attack / attack_move / garrison / stop
var members_alive: int        # visual budget from hp ratio
var rank: int
var xp: float
var statuses: Array           # fear, knockdown, poison, buffs
```

Members are **not** full agents. One nav agent per battalion (BFME2-like horde control). Optional later: soft member offsets via formation slots.

### Combat matrix (Stage 4+)

Damage types: `slash, pierce, specialist, crush, siege, magic, hero, structural`  
Armor types: `infantry_light, infantry_heavy, pike, cavalry, monster, hero, building, wall, siege`

Start with a small CSV → `DamageMatrix` resource. Browser had a few multipliers; Open BFME should do a **real matrix** once two factions fight.

---

## 5. Asset strategy (BFME2 + Grok Imagine + 3D)

### 5.1 BFME2 pipeline (Lane A)

**Stage 0 deliverables:**

```
_bfme2_extract/
  ini/                 # full dump from ini.big
  catalogs/
    units.csv          # name, kind, side, key fields parsed from INI
    buildings.csv
    weapons.csv
    powers.csv
    music_list.txt
  samples/
    gondor_soldier/    # optional few w3d+dds for scale
    fortress_gondor/
  audio_samples/       # few wav for timing (attack swing length)
```

**Scripts to write:**

| Script | Job |
|---|---|
| `tools/extract_big.ps1` | Wrap FinalBIG CLI/GUI steps; document manual clicks if no CLI |
| `tools/parse_ini_to_csv.py` | Crawl extracted INI → CSV + JSON for Godot import |
| `tools/catalog_audio.py` | List music/sfx names from extract |
| `tools/scale_reference.md` | Human notes: fortress footprint, soldier height vs camera |

**Godot import of Lane A:** only via optional `DevAssetMirror` autoload that is compiled out / feature-tagged `reference_assets`.

### 5.2 Grok Imagine pipeline (Lane B — primary 2D)

Use Imagine for everything orthographic UI and concept:

| Asset class | Aspect | Style prompt anchors |
|---|---|---|
| Unit command icons | 1:1 | “BFME2-like fantasy RTS icon, painted, readable at 64px, dark wood frame, no text” |
| Building icons | 1:1 | same + structure silhouette |
| Spell / power icons | 1:1 | glow, elemental, readable |
| Portraits (heroes) | 1:1 or 3:4 | painterly bust, faction color grade |
| Faction emblems | 1:1 | simple heraldry |
| Main menu key art | 16:9 | cinematic, not logo-heavy |
| Loading splash | 16:9 | map painting feel |
| Cursor sets | 1:1 | select, attack, move, garrison, invalid |

**Folder convention:**

```
_imagine_raw/
  2026-07-11_icons_gondor/
game/assets/ui/icons/
  soldier.png          # curated, cropped, size-normalized
```

**Batch workflow (repeatable):**

1. Write `docs/art_bible.md` (palette, outline rules, banned modern/sci-fi).
2. Generate 4–8 variants per icon via Imagine.
3. Human pick → crop to 128 / 256 → optional Godot import presets (mipmaps, filter).
4. Keep prompt + seed notes in `docs/imagine_log.md` for consistency.

**Consistency tricks:**

- Always paste 1–2 **reference icons you already approved** into Imagine edits.
- Fix a **faction color strip**: Gondor steel/blue-white, Mordor iron/red-black, Elves gold/green, Goblins bone/teal.
- Generate **icon sheets** (grid) then slice — more consistent than one-offs.

### 5.3 3D art pipeline (Lane B)

Do **not** depend on W3D→GLTF conversion for v1.

| Tier | Use | How |
|---|---|---|
| **Placeholder** | Stages 1–3 | Godot CSG / simple capsule+banner MultiMesh |
| **Hero / fortress** | Stage 4–5 | Meshy/Tripo or Blender hero kit; 1 fortress per faction |
| **Battalion members** | Stage 5–6 | Low-poly (~1–3k tris) shared skeleton; 3–5 body variants per unit type |
| **Environment** | Stage 3+ | Modular rocks/trees; terrain textures from CC0 or painted |

**LOD policy (mandatory before large armies):**

- LOD0: skinned mesh near camera  
- LOD1: static posed mesh  
- LOD2: MultiMesh impostor / cross billboard beyond N meters  

Browser GLB path taught: skinned + mixer per member dies at scale. Godot must bake that lesson in early.

### 5.4 Audio pipeline

| Layer | v0.5 | v1.0 |
|---|---|---|
| UI clicks | procedural or CC0 | custom light foley |
| Combat | synthesized + free packs | layered swings/impacts per armor class |
| VO | silent or generic grunts | faction bark sets (ElevenLabs / local TTS / hire) — **not** BFME VO in public builds |
| Music | 2–3 royalty-free loops + adaptive crossfade | original score or licensed; BFME `music.big` only for private A/B mood tests |
| Ambient | wind/crows loops | biome sets |

Mirror what worked in the prototype: **throttle + distance gate** combat SFX.

### 5.5 “Download” free asset sources (Lane C)

Track licenses in `docs/THIRD_PARTY.md`:

- Kenney / CC0 UI & nature packs (prototyping)
- OpenGameArt / itch.io CC0 terrain
- Godot Terrain3D demo materials (if used)
- Freesound (verify license per clip)

Never mix Lane A files into these folders.

---

## 6. Stage plan (execute in order)

Each stage has: **goal**, **player-visible demo**, **systems**, **assets**, **tests**, **exit criteria**, **est. effort** (solo, part-time-ish).

---

### Stage 0 — Project spine & asset vault  
**Effort:** 2–4 days  
**Goal:** Empty Godot game that boots, plus legal-safe asset pipelines.

**Work**

1. Create Godot 4.7 project `game/` (Forward Plus, desktop).
2. Setup git: `.gitignore` for `_bfme2_extract/`, `_imagine_raw/`, `.godot/`, exports.
3. Autoloads: `Events`, `SimClock`, `Audio`, `Debug`.
4. Install FinalBIG; extract **`ini.big` only** first; dump to `_bfme2_extract/ini`.
5. Write `tools/parse_ini_to_csv.py` → unit/building name lists.
6. Write `docs/art_bible.md` + `docs/legal_assets.md`.
7. Generate **placeholder** icon set (8 icons) via Grok Imagine → `game/assets/ui/icons/`.
8. Main menu scene: New Skirmish / Options / Quit (Options = volume only).

**Demo:** Boot to menu with custom icons.  
**Exit criteria:**

- [ ] Project runs from editor and exported `.exe` smoke test  
- [ ] INI catalog CSV exists with ≥50 unit/building rows  
- [ ] Lane A path gitignored and verified `git status` clean of binaries  
- [ ] Art bible + imagine log started  

---

### Stage 1 — Vertical slice: one map, one vs one, battalions move/fight  
**Effort:** 1.5–2.5 weeks  
**Goal:** The fun core without economy.

**Systems**

- Map scene: heightmap or flat plane + NavigationRegion3D
- Camera: RTS rig (edge pan, WASD, zoom, rotate, focus fortress key)
- Selection: click + box select (battalion level)
- Orders: move, stop, attack-move, attack target
- `Battalion` sim + `BattalionView`
- Melee + simple projectile
- Two hard-scripted armies spawn (Gondor soldiers vs Orc warriors)
- Win: destroy enemy fortress dummy (static building with HP)

**Assets**

- Capsule/blockout units + color by side  
- 2 Imagine portraits  
- Placeholder SFX  

**Tests**

- Headless: 20 soldiers vs 20 orcs resolves without NaNs  
- Selection count / order issuance unit tests  

**Demo:** “Battle sandbox” from menu.  
**Exit criteria:**

- [ ] 50 battalions total still ≥30 FPS on your machine (even as blocks)  
- [ ] Nav avoids a rock/building obstacle  
- [ ] Fixed timestep combat deterministic with seeded RNG  

---

### Stage 2 — Economy, build menu, train queues  
**Effort:** 1.5–2 weeks  
**Goal:** Real skirmish opening.

**Systems**

- Resources (single resource first, like prototype — *not* classic peon gather)
- Building placement ghost + valid/invalid  
- Construction timer + HP ramp  
- Fortress, farm/mill, barracks, archery  
- Train queue UI + rally point  
- Population / battalion cap  
- Farm efficiency radius (from prototype — excellent anti-spam)

**Data**

- Import unit/building costs from prototype `config.js` into `.tres`
- Cross-check names against INI catalog (do not yet force INI numbers)

**Assets**

- Imagine: full Gondor + Mordor **build/train icon rows**  
- Blockout buildings with distinct silhouettes  
- Optional: 1 fortress GLB art pass  

**Demo:** Build a base, train 5 battalions, kill dummy AI base.  
**Exit criteria:**

- [ ] Cannot place buildings overlapping / off-nav  
- [ ] Saving build menu layout data-driven (no hardcoded button lists in UI code)  
- [ ] Resource income readable in HUD  

---

### Stage 3 — Walls, gates, towers, fog of war lite  
**Effort:** 1.5–2 weeks  
**Goal:** BFME base identity.

**Systems**

- Wall segment placement with snap + rotation  
- Gate (friendly pass, enemy block) via navigation links / obstacles rebuild  
- Standalone tower auto-fire  
- Wall tower upgrade  
- Building collision baked into navmesh (rebake incremental or tile regions)
- **Fog of war v1:** exploration grid + vision from units/buildings (CPU grid is fine)

**Assets**

- Wall/gate kits (modular meshes)  
- Imagine: wall/tower/postern icons  
- Cursor pack (select/attack/move/build)

**Demo:** Wall off a chokepoint; tower shoots; fog reveals with army.  
**Exit criteria:**

- [ ] Enemy AI path cannot phase through closed gate  
- [ ] Friendly units path through open gate  
- [ ] Nav rebuild < 100ms for local wall edits or staged full rebake acceptable  

---

### Stage 4 — Heroes, abilities, veterancy, stances  
**Effort:** 2 weeks  
**Goal:** “This is BFME” combat feel.

**Systems**

- Hero unit type (size 1, big HP, ability bar)  
- Ability framework: instant, target unit, target ground, toggle  
- Cooldowns + rank gates  
- Veterancy XP on damage/kills; rank VFX  
- Replenishment out of combat  
- Stances (aggressive / defensive / hold)  
- Fear/terror aura + cower/flee  
- Knockback on monsters  
- Hero revival at fortress  

**Content (minimum)**

| Faction | Heroes (v1) | Signature abilities |
|---|---|---|
| Gondor | Aragorn, Gandalf | AoE slash, heal; word of power, blast |
| Mordor | Nazgûl, Mouth | terror, dread; debuff, summon trash |

Port ability numbers from prototype first.

**Assets**

- Imagine: hero portraits + ability icons  
- Distinct hero meshes (can still be stylized low-poly)  
- Palantir-style portrait frame UI (original art)

**Demo:** Level Aragorn, cast abilities, fear breaks low-rank orcs.  
**Exit criteria:**

- [ ] Abilities are data (`AbilityDef` resource), not `match` spaghetti per hero  
- [ ] Fear respects rank immunity / hero immunity  
- [ ] Revival cost scales with death count  

---

### Stage 5 — Spellbook, powers, weather moments  
**Effort:** 1–1.5 weeks  
**Goal:** Power points fantasy.

**Systems**

- PP from kills/buildings  
- Tier tree UI (spend locks path — simplified 3×3 is enough)  
- Powers: Heal, Reveal, Reinforce, Sunflare/Darkness, Earthquake, faction summon  
- Global lighting tween for Darkness / Cloud Break  

**Assets**

- Imagine: full spellbook icons + tree frame  
- VFX: ground rings, columns, quake shake  

**Demo:** Mid-game spell turns a fight.  
**Exit criteria:**

- [ ] Powers target modes share one input state machine with buildings placement  
- [ ] PP economy cannot soft-lock tree  

---

### Stage 6 — Second faction pair + real art pass + damage matrix  
**Effort:** 2–3 weeks  
**Goal:** Roster breadth without campaign scope.

**Content**

- Finish **Gondor vs Mordor** full skirmish rosters (from prototype list)
- Add **Elves vs Goblins** as second matchup (prototype already defined)
- Damage matrix pass using INI inspiration  
- Upgrades: forged blades, fire arrows, heavy armor, banners  

**Art**

- Replace blockouts for **all trained units** of Gondor/Mordor at LOD0/LOD2  
- Building GLBs for economy + production  
- Faction menu emblems (Imagine)  
- Music: 3-state adaptive (explore / battle / victory) royalty-free or original  

**BFME2 INI authenticity pass (optional side quest)**

- Diff prototype HP/DPS vs INI  
- Adjust only where fights feel wrong  

**Exit criteria:**

- [ ] All four factions selectable as player/enemy  
- [ ] No unit uses “missing mesh” placeholder in default skin pack  
- [ ] 80 battalions + full bases ≥ 40 FPS on target PC with LOD  

---

### Stage 7 — Skirmish AI that doesn’t suck  
**Effort:** 2 weeks  
**Goal:** Default opponent is fun on Normal/Hard.

**Port AI ideas from `ai.js`**

- Budget split: construction / troops / tech  
- Build order plan per faction (`aiPlan` resource)  
- Creep clear → pressure waves → raids  
- Retreat below HP threshold  
- Difficulty multipliers  

**New Godot AI requirements**

- Scouting under fog  
- Wall response (build towers if player walls)  
- Hero ability usage heuristics  
- Power spending rules  

**Tests**

- Headless AI vs AI 10-minute smoke  
- Scripted “player turtling” → AI expands and attacks  

**Exit criteria:**

- [ ] Easy/Normal/Hard distinguishable  
- [ ] AI never soft-locks (0 income infinite wait)  
- [ ] Human player can still cheese less than prototype  

---

### Stage 8 — Maps, save/load, UX polish, options  
**Effort:** 2 weeks  
**Goal:** Feels like a game you launch every evening.

**Systems**

- 4 hand-authored skirmish maps (use Worldbuilder only as **scale reference**, rebuild in Godot)  
  - Anduin Vale, Misty Foothills, Fangorn Edge, Brown Lands (names from prototype OK if original geometry)  
- Map metadata: start spots, lairs, resources bias, nav, lighting  
- Save/load match (`GameState` serializable)  
- Pause menu, game speed, key rebind  
- Control groups 1–9  
- Formations UI  
- Score screen stats (from prototype)  
- Help overlay  

**Assets**

- Imagine: map pick thumbnails  
- Minimap framework  
- Main menu key art final  

**Exit criteria:**

- [ ] Cold boot → win a Normal skirmish without debug keys  
- [ ] Save/load mid-combat restores orders & HP  
- [ ] Options persist  

---

### Stage 9 — Juice, audio, custom hero, Ring (optional pack)  
**Effort:** 2–3 weeks  
**Goal:** Prototype’s “soul systems” reborn.

**Priority order inside stage**

1. Audio mix + music states + unit barks  
2. Combat juice: hit flashes, ragdoll-lite knockdown, blood-free fantasy impacts  
3. Garrison polish + mount/dismount if art allows  
4. The One Ring + Gollum (prototype design)  
5. Create-A-Hero lite (class, colors, 2 abilities, stat budget)  

**Assets**

- Imagine: CAH UI panels, ring UI  
- Extra hero ability icons  

**Exit criteria:**

- [ ] New player understands UI without external readme  
- [ ] Optional systems can be disabled for “classic skirmish”  

---

### Stage 10 — Hardening, performance, release engineering  
**Effort:** 1–2 weeks  
**Goal:** v1.0 private “gold” build.

**Work**

- Profiler pass: multimesh, occlusion, shadow distance, fog GPU  
- Crash hygiene, assert strip, logging  
- Installer (optional Inno Setup) / portable zip  
- `README` for you: how to rebuild assets  
- Verify **zero Lane A files** in export with automated path scan  
- Balance tournament: AI mirror matches  

**Exit criteria:**

- [ ] Export scanner reports clean  
- [ ] 30-minute play session no softlock  
- [ ] Known issues list written  

---

## 7. Stage dependency graph

```
0 Spine/Assets
  └─1 Fight sandbox
      └─2 Economy/build
          └─3 Walls/FoW
              └─4 Heroes/stances
                  └─5 Spellbook
                      ├─6 Roster+art+matrix
                      │   └─7 AI
                      │       └─8 Maps/save/UX
                      │           └─9 Juice/optional systems
                      │               └─10 Gold
```

Parallel tracks after Stage 2 (if you use AI agents / split days):

- **Track Code:** systems stages  
- **Track Art:** Imagine icons → 3D hero/building → LOD  
- **Track Design:** INI studies → matrix → map layouts  

Never let Track Art block Stage 1–3 code.

---

## 8. Grok Imagine production schedule (concrete)

| When | Deliverable | Count (approx) |
|---|---|---|
| Stage 0 | Menu + cursor + 8 stub icons | ~15 |
| Stage 2 | Gondor/Mordor build+train icons | ~40 |
| Stage 4 | Hero portraits + abilities | ~30 |
| Stage 5 | Spellbook set | ~20 |
| Stage 6 | Elves/Goblins icons + emblems | ~50 |
| Stage 8 | Map thumbs + score/menu art | ~15 |
| Stage 9 | CAH + ring | ~20 |

**Running total ~190 2D assets** — manageable if batched and icon-sheeted.

Prompt template (store in art bible):

```text
Fantasy RTS command icon, top-down three-quarter object, painted
style similar to early-2000s PC strategy UI, thick readable silhouette,
muted fantasy palette, subtle rim light, square composition, no text,
no modern elements, no copyrighted logos, dark wooden frame border
```

For Middle-earth *feel* without trademarked names in **public** UI strings, use internal IDs `gondor` but display names you are comfortable with if you ever share builds. Private-only builds can use familiar names.

---

## 9. BFME2 extract schedule (concrete)

| Stage | Extract | Purpose |
|---|---|---|
| 0 | `ini.big` full | Design catalog |
| 0 | string/lang bits | Naming list |
| 1 | sample weapon ranges from INI | Tune attack ranges |
| 3 | map list from `maps.big` metadata if readable | Map size norms |
| 4 | hero power INIs | Ability inspiration |
| 5 | spellbook related INI | Power tree inspiration |
| 6 | armor/weapon set INIs | Damage matrix |
| any (private) | 1–2 music tracks | Mood reference only |
| skip | full `w3d.big` conversion | Unless research hobby |

---

## 10. Quality bar (from prototype AGENTS.md, adapted)

1. Data-driven factions: new faction = data + art, not core combat edits  
2. One mechanism reused (status system, modifiers, floating text)  
3. No gameplay `Time.get_ticks_msec()` — use `SimClock.time`  
4. Balance numbers only in resources  
5. Views never decide damage  
6. Every stage leaves a **playable** build  
7. Automated tests for combat math + save schema  
8. Performance budget documented on **your** PC  

---

## 11. Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| W3D conversion rabbit hole | Weeks lost | Ban full conversion from critical path |
| Navmesh wall editing pain | Base building feels broken | Tile-based local rebake; Stage 3 spike early |
| Army scale FPS death | Can’t do late game | MultiMesh LOD from Stage 1 budget test |
| Scope creep to campaign/MP | Never ship | Stage gates; written non-goals |
| Copyright contamination | Can’t share / legal risk | Export scanner; dual lanes |
| AI too dumb | Empty game | Stage 7 before art perfection |
| Art inconsistency | Looks amateur | Art bible + reference sheet in every Imagine batch |
| Name collision OpenBFME | SEO/confusion | Distinct public name if releasing |

---

## 12. First 14 days (tactical checklist)

**Day 1–2**

- [ ] Godot 4.7 project created under `game/`  
- [ ] Git + gitignore lanes  
- [ ] Autoloads + boot → menu  
- [ ] FinalBIG: extract `ini.big`  
- [ ] `parse_ini_to_csv.py` v0  

**Day 3–4**

- [ ] Art bible  
- [ ] Imagine: menu BG + 8 icons  
- [ ] RTS camera prototype scene  
- [ ] Click ground → move placeholder unit  

**Day 5–7**

- [ ] Battalion select + box select  
- [ ] Attack + HP bars  
- [ ] Two teams + fortress dummies  
- [ ] Nav obstacle demo  

**Day 8–10**

- [ ] Fixed sim tick  
- [ ] Projectile  
- [ ] Headless combat test  
- [ ] Basic HUD (resources stub, minimap blank)  

**Day 11–14**

- [ ] Start Stage 2: place farm + barracks  
- [ ] Train soldier battalion  
- [ ] Income tick  
- [ ] Tag build `v0.2-economy-wip`  

If day 14 isn’t fun to click around, fix feel before adding factions.

---

## 13. Success metrics

| Milestone | Definition of done |
|---|---|
| **Alpha** | Stages 0–5: you can win a hero+power skirmish on blockout art |
| **Beta** | Stages 0–8: four factions, maps, AI Normal, save/load, clean art for Gondor/Mordor |
| **v1.0** | Stage 10 + Stage 9 must-haves (audio + juice); optional CAH/Ring if time |
| **Personal fun bar** | You choose Open BFME over BFME2 skirmish for a week of evening games |

---

## 14. How this uses the browser project

| From `middle-earth-rts` | Action |
|---|---|
| `config.js` numbers | Port to Godot resources Stage 2 |
| `FEATURES-BFME2.md` | Feature backlog filter (ignore web-only notes) |
| `ai.js` budgets | Stage 7 AI plans |
| `behaviors.js` terror/stealth/efficiency | Stage 4–6 systems |
| Icons under `assets/icons/` | Temporary stand-ins; replace with Imagine set for consistency |
| GLBs under `assets/models/` | Candidate Lane B imports (already original/AI-gen — better than BFME W3D legally) |
| Selftest ideas | Recreate as Godot headless tests |
| Three.js code | Do not port |

**Immediate free win:** copy `assets/icons/*.png` and unit/building GLBs from the browser project into Open BFME as **bootstrap Lane B** while Imagine catches up. Those are already non-EA.

---

## 15. Recommended Godot settings (start)

- Renderer: **Forward Plus**  
- Physics ticks: 30 or 60; **sim tick separate** at 10–20 Hz  
- 3D physics for projectiles optional — prefer kinematic sim  
- Use **NavigationServer3D** + `NavigationAgent3D` per battalion  
- UI scale-aware theme (1080p baseline, 1440p/4K tested)  
- Input map: mirror prototype (WASD, Q/E, A attack-move, S stop, control groups)  

Plugins to consider later (not day 1):

- Terrain3D (large maps)  
- GUT (tests)  
- GodotSteam only if you ever distribute on Steam (far future)

---

## 16. Definition of “good clone” for this project

Open BFME succeeds if:

1. **Battalion micro + freeform base + heroes + powers** feel right  
2. Desktop performance allows real late-game  
3. Walls and fog make map geometry matter  
4. Art is cohesive original fantasy RTS (Imagine + GLB), not a legal liability  
5. Development stages always leave something playable  

It does **not** need:

- Pixel-identical UI  
- Every BFME2 unit  
- Campaign  
- Multiplayer  
- Retail music/VO  

---

## 17. Next action (when you say go)

1. Scaffold `game/` Godot project + gitignore  
2. Extract `F:\BFME2\ini.big` → `_bfme2_extract/ini`  
3. Port a minimal `UnitDef` / `BuildingDef` from prototype config  
4. Implement Stage 1 sandbox  

---

*Document version: 1.0 — 2026-07-11*  
*BFME2 path verified: `F:\BFME2`*  
*Prototype path: `C:\Users\Jonathan\Desktop\middle-earth-rts`*
