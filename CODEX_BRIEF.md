# Combat Effects, Damage Pipeline, and FX Timing Lane — Codex Brief

## Task Summary
Map unmapped retail combat fields into the Godot 4.7 runtime and importer, close a known armor fallback defect, and promote high-usage rows to runtime coverage. Priority: RotWK 2.01.

## Key Unmapped Gaps (from .private\retail-work\reports\retail-ini-coverage\)

### 1. **field:object.DamageCreationList** (CRITICAL)
- **Status**: Fully UNMAPPED (665 RotWK sites, 605 BFME2)
- **Semantics**: Debris and damage-object spawning on destruction
- **Scope**: Map end-to-end:
  - Importer contract entry in `importer/module_contracts.py` EXECUTABLE_TYPED_MODULE_EVIDENCE registry
  - Descriptor field in importer
  - Godot consumer in game/src
  - Focused runner in game/tests that executes the exact semantics (spawn debris on object death)
- **Acceptance**: Registry entry names both consumer file and runner; runner passes green

### 2. **FX Timing Fields** (HIGH PRIORITY)
- **BurstDelay** (751 RotWK sites)
- **InitialDelay** (404 RotWK sites)
- **FramesPerRow / TotalFrames** (atlas/atlas sprite fields)
- **Semantics**: Authored FX timing and atlas layout; effect cadence must match authored values
- **Scope**: Map at least BurstDelay and InitialDelay into FX runtime consumer
- **Acceptance**: Consumer reads these fields and applies them; focused runner exercises timing (e.g., delayed particle burst)

### 3. **Structure Armor Fallback Defect** (KNOWN DEFECT)
- **Current State**: game/src/retail_slice/retail_slice_sim.gd contains provisional 0.25 armor scalar fallback for structure kinds without compiled armor tables
  - Search phrase: "provisional" or "no compiled armor"
- **Fix**: Compile real armor.ini tables for those kinds (importer-side)
- **Verify**: Add a focused runner that fails if any selected structure kind hits the provisional path; make it pass
- **Acceptance**: No structure kind falls back; runner green

### 4. **High-Usage Importer Rows to Runtime Coverage** (PROMOTE)
Promote these to runtime testing ONLY if a real consumer exists or you add one:
- **weapon.DamageType** / **DeathType** / **DamageFXType**
- **object.DamageFX**
- **object.MaxHealthDamaged** / **MaxHealthReallyDamaged** (damaged-state thresholds)
- **object.UseWeaponTiming** (timing inheritance)
- **weapon nested DamageNugget variants** (not yet covered)
- **modifierlist.Modifier** / **Duration** / **Category** (attribute modifier buffs/debuffs)

**Promotion Rule (STRICT)**:
- A row only becomes runtime coverage through the EXECUTABLE_TYPED_MODULE_EVIDENCE registry in importer/module_contracts.py
- Each entry MUST name a concrete Godot consumer file AND a focused runner that executes exact semantics
- Importer parsing alone is NOT coverage
- Registry entries are APPEND-ONLY; never delete or reorder

## Environment Setup

```
BFME2_INSTALL=C:\Users\Jonathan\Desktop\open-bfme\.private\retail-work\editions\rotwk\layered-install\layer-1-bfme2
PYTHONPATH=importer;.private\retail-work\tools\python-3.12-env\Lib\site-packages
Python: .private\retail-work\tools\cpython-3.12.13\python.exe
Godot: resolve via tools\resolve-godot.bat, then run: <godot> --headless --path game --script res://tests/<runner>.gd
Importer tests: run_importer_tests.bat
```

## Implementation Strategy

### Step 1: Catalog Existing Coverage
- Read importer/module_contracts.py and identify all existing EXECUTABLE_TYPED_MODULE_EVIDENCE entries for combat/damage/FX
- Read game/tests/ and list all existing combat/damage/FX runners
- Identify which gaps already have partial consumers or runners

### Step 2: Map DamageCreationList (End-to-End)
- **Importer side**:
  - Add contract entry in module_contracts.py (name the consumer and runner files)
  - Parse DamageCreationList from object.ini retail rows
  - Store in descriptor structure
- **Godot consumer**:
  - Add code to read DamageCreationList from descriptor
  - Spawn debris/damage objects on parent object destruction
  - Name the file clearly (e.g., game/src/retail_slice/damage_creation.gd)
- **Test runner**:
  - Create game/tests/test_damage_creation_list.gd
  - Execute: spawn an object, destroy it, verify debris/damage objects appear
  - Must pass green

### Step 3: Map FX Timing (BurstDelay, InitialDelay)
- **Importer side**:
  - Add contract entries for fxparticlesystem.BurstDelay, fxparticlesystem.InitialDelay
  - Parse from .ini files
  - Store in FX descriptor
- **Godot consumer**:
  - Modify or create FX runtime consumer to read and apply BurstDelay/InitialDelay
  - Delay particle burst emission by authored delay
  - Name clearly (e.g., game/src/retail_slice/fx_timing.gd or extend existing FX module)
- **Test runner**:
  - Create game/tests/test_fx_timing_delays.gd
  - Execute: spawn effect with BurstDelay, measure actual emission time, verify match
  - Must pass green

### Step 4: Close Armor Fallback Defect
- **Importer side**:
  - Scan object.ini for structure kinds
  - Compile armor.ini mappings for all structures (ensure no gaps)
  - Verify every selected kind has armor table
- **Godot consumer**:
  - Locate provisional fallback in game/src/retail_slice/retail_slice_sim.gd
  - Remove or conditional-check it
- **Test runner**:
  - Create game/tests/test_structure_armor_tables.gd
  - Iterate all selected structure kinds
  - Assert each has real armor table (not provisional)
  - Fail if any hit fallback path; must pass green

### Step 5: Promote High-Usage Rows
- For each high-usage row in the list above:
  - Check if a consumer already exists
  - If not, add a minimal consumer (reuse existing combat module if possible)
  - Add or enhance registry entry in module_contracts.py
  - Create or enhance focused runner
  - Run runner and confirm green

## Constraints & Rules (MANDATORY)

1. **No branches, no worktrees** — work on current worktree; commit to current branch only
2. **No pack modifications** — never touch .private/selection.json or run apply-selection-transaction
3. **Packs are immutable and sealed** — do not re-cook or modify pack contents
4. **Touch only combat/damage/FX files**:
   - game/src/retail_slice/ (damage, FX, armor)
   - game/tests/ (new runners)
   - importer/ (contracts and descriptors)
5. **Minimal, surgical changes to shared files**:
   - importer/module_contracts.py: APPEND-ONLY registry additions, no reformatting
   - game/src/retail_slice/retail_slice_sim.gd: only remove/fix provisional armor fallback
   - game/src/content_db.gd: minimal or none
6. **No silent fallbacks** — if a path can't find data, it must fail loudly or error
7. **Failing test first, then fix** — write runner that fails, then implement to green
8. **Environment variables must be set**:
   - BFME2_INSTALL=.private\retail-work\editions\rotwk\layered-install\layer-1-bfme2
   - PYTHONPATH=importer;.private\retail-work\tools\python-3.12-env\Lib\site-packages
9. **Redirect Godot runner output to %TEMP%** — uniquely-named files; read and report results
10. **All runners must be rerunnable commands** — every claim needs evidence
11. **Run `python tools\check_pack_addresses.py` before declaring done** — must still be green

## Verification Commands

```powershell
# Run importer tests (ensure no regression)
.\run_importer_tests.bat

# Run individual Godot combat runners
$godot = (.\tools\resolve-godot.bat)
& $godot --headless --path game --script res://tests/test_damage_creation_list.gd 2>&1 | Tee-Object -FilePath "$env:TEMP\test_damage_creation_list.txt"
& $godot --headless --path game --script res://tests/test_fx_timing_delays.gd 2>&1 | Tee-Object -FilePath "$env:TEMP\test_fx_timing_delays.txt"
& $godot --headless --path game --script res://tests/test_structure_armor_tables.gd 2>&1 | Tee-Object -FilePath "$env:TEMP\test_structure_armor_tables.txt"

# Pack address check (must remain green)
python tools\check_pack_addresses.py

# Importer contract validation (if available)
python tools\check-product-contracts.py
```

## Definition of Done

1. DamageCreationList: registry entry + consumer + runner, runner green
2. FX timing (BurstDelay, InitialDelay): registry entries + consumer + runner, runner green
3. Structure armor fallback: defect closed, runner green, no kind hits provisional path
4. High-usage rows promoted: registry entries + consumers + runners for each, all green
5. All importer tests passing
6. `python tools\check_pack_addresses.py` still green
7. `git status` shows only focused combat/damage/FX file changes (no reformatting, no shared-file rewrites)
8. Commit messages end: `Co-Authored-By: Codex Sol <noreply@openai.com>`
9. Report: exact signatures moved with runner outputs cited; anything provisional/deferred named with receipt

## Codex House Rules (Append to Every Brief, Verbatim)

- Commit on the current worktree branch; NEVER push; NEVER merge to main; NEVER `git stash` (shared stash stack)
- Godot runs: env OPENBFME_CONTENT=C:\Users\Jonathan\Desktop\open-bfme\.private\content-packs; exe C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe; redirect runner output to uniquely-named %TEMP% files and read the file
- Python: C:\Users\Jonathan\Desktop\open-bfme\.private\retail-work\tools\python-3.12.13\python.exe with PYTHONPATH pointing at the worktree's importer/
- Retail INI oracle: .private\retail-work\editions\rotwk\cache\effective-assets — NEVER layered-effective-assets (contaminated)
- No pack builds/publishes/selection changes unless the brief explicitly authorizes them
- Commit messages end: Co-Authored-By: Codex Sol <noreply@openai.com>
- An Opus adversarial review gates the merge — write reports for a hostile reviewer; every claim needs a rerunnable command
