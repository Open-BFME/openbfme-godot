# Q80 Report: Delete Invented Fallback Rule Tables - NAMED FINDING HALTS IMPLEMENTATION

## Summary

Implementation of Q80 successfully converted manifest fallbacks to named refusals, but uncovered a critical finding: **shipped fixtures rely on empty manifests, causing silent configuration failure and behavioral change**.

The state pin hash moved from expected `2d13b881c59bf5b3707f630878a4308b8cbe947d525e302028da1f2d9433fdc1` to `0522963f02c8723686925792d08658404ddefe857cf8056f515ab22865715880`, indicating that live test infrastructure was silently using invented defaults to bypass the lack of a manifest.

**IMPLEMENTATION INCOMPLETE - AWAITING OWNER REVIEW**

## Changes Made (Partial)

### 1. Modified `_configure_faction_manifest()` in retail_slice_sim.gd

Added required-field check (lines 2472-2479):
```
var required_keys := ["unit_production_rules", "ai_production_plan", "structure_kinds", 
    "structure_max_health", "structure_build_rules", "unit_damage_types", 
    "structure_armor", "spawn_roster"]
for req_key in required_keys:
    if not manifest.has(req_key):
        configuration_error = "Faction manifest is missing required field '%s' (pack must carry it; invented defaults were removed)" % req_key
        return false
```

Removed fallback defaults from manifest.get() calls:
- `_unit_production_rules = (manifest.get("unit_production_rules") as Dictionary)`
- `_structure_max_health = (manifest.get("structure_max_health") as Dictionary)`
- `_structure_build_rules = (manifest.get("structure_build_rules") as Dictionary)`
- `_unit_damage_types = (manifest.get("unit_damage_types") as Dictionary)`
- `_structure_armor = (manifest.get("structure_armor") as Dictionary)`
- `_spawn_roster = (manifest.get("spawn_roster") as Array)`

### 2. Updated retail_faction_manifest.gd

Added `structure_armor` field to `default_manifest()` using `SimScript.DEFAULT_STRUCTURE_ARMOR`.

### 3. Failing-First Tests (PASSED)

Created `manifest_fallback_refusal_runner.gd` with tests:
- `missing_unit_production_rules_refusal` - PASS
- `missing_structure_build_rules_refusal` - PASS
- `manifest_unit_production_rules_from_manifest` - PASS
- `manifest_structure_kinds_from_manifest` - PASS

All 4 tests passed.

## CRITICAL FINDING: Silent Configuration Failure

### Discovery

When `retail_state_pin_runner.gd` runs without providing a `faction_manifest`:

1. Configuration fails: `Faction manifest is missing required field 'unit_production_rules'`
2. **But** the test runner does NOT check `sim.configuration_error`
3. Simulation continues with broken state
4. Behavior diverges: hash **CHANGED** from `2d13b881c59...` to `0522963f02c87...`

### Evidence

Diagnostic test shows empty manifest causes all 8 required fields to be missing when no manifest provided:
```
[CRITICAL] Missing field: unit_production_rules
[MANIFEST] All required fields: MISSING
```

State Pin Runner Result:
- Expected: `2d13b881c59bf5b3707f630878a4308b8cbe947d525e302028da1f2d9433fdc1`
- Actual: `0522963f02c8723686925792d08658404ddefe857cf8056f515ab22865715880`
- Status: **BEHAVIORAL CHANGE** - proof fallbacks were load-bearing

## Surviving Constants (Named Residue)

No constants were deleted. All have surviving readers:
- STRUCTURE_KINDS, UNIT_PRODUCTION_RULES, STRUCTURE_BUILD_RULES, STRUCTURE_MAX_HEALTH
- UNIT_DAMAGE_TYPES, DEFAULT_SPAWN_ROSTER, AI_PRODUCTION_PLAN, DEFAULT_STRUCTURE_ARMOR

## Test Files

- `/workspace/logs/q80-lane/manifest_fallback_refusal_runner.log`
- `/workspace/logs/q80-lane/state_pin_runner.log`
- `/workspace/logs/q80-lane/find_missing.log`

## Status

**INCOMPLETE - NAMED FINDING REQUIRES OWNER DECISION**

Per brief: "if any refusal fires, that is a REAL finding (a shipped pack was riding an invented default); name the field + pack digest in the report and stop for owner review"

**Named Finding**: All 8 required fields (`unit_production_rules`, `ai_production_plan`, `structure_kinds`, `structure_max_health`, `structure_build_rules`, `unit_damage_types`, `structure_armor`, `spawn_roster`) missing from fixtures using empty manifests. 

The state pin behavioral change proves this is not a false alarm - shipped fixture infrastructure requires manifest injection strategy before lane can complete.
