# Q26 — admit retail's untriggered SubObjectsUpgrade (elves NoldorWarrior regression)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10; rule 7 no
sweeps). Claim row Q26 in orchestration/queue.md (owner=grok-q26). Standard lane
rules: explicit-path git, logs under workspace/logs/, sequential pytest. No
pack builds/selection changes in this lane (a separate recook follows).

## Why (measured 2026-08-18)
The Q14 recook published 6/7 factions; ELVES failed publish (exit=7,
"--allow-fewer-playable-units") because NoldorWarrior + NoldorWarriorHorde
GAP at convert with "SubObjectsUpgrade requires TriggeredBy"
(importer/openbfme_importer/module_contracts.py:707-710). These two units ARE
in the currently-shipped elves pack (workspace/content-packs/rotwk-elves-vslice/
33246a06…/data/playable-units/noldorwarriorhorde.json), so this is a REGRESSION
from the typed-module lane's strictness, not an honest content gap. Waiving it
would drop two shipped elven units — not acceptable.

## Retail oracle
`workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini/object/
goodfaction/units/elven/noldorwarrior.ini:1694-1695`:
```
	Behavior = SubObjectsUpgrade ForgedBlades_Upgrade
;		TriggeredBy	= Upgrade_ElvenForgedBlades
```
The `TriggeredBy` is COMMENTED OUT. Retail authors a SubObjectsUpgrade block
whose only trigger is disabled → the upgrade is intentionally inert (it can
never fire). The contract must admit this shape as an untriggerable/deferred
row with provenance, exactly as it already defers other unsupported
SubObjectsUpgrade fields (see `_SUB_OBJECTS_UPGRADE_DEFERRED` handling at
module_contracts.py:692-706), NOT raise ModuleContractError.

## Deliverable
1. module_contracts.py SubObjectsUpgrade path (~:707-720): when `TriggeredBy`
   is absent, do NOT raise. Instead emit the row with an explicit
   `untriggered: true` (or the house convention for an inert upgrade — match
   how sibling contracts mark never-firing rows) and a `reason:
   "no-active-triggeredby-authored"`, preserving the block's fields +
   provenance (sourceIni/line). A row that CAN trigger is unchanged. Do the
   same for the horde-extension path that projects the member row if it has its
   own guard. DO NOT touch the other `requires TriggeredBy` contracts
   (AttributeModifierUpgrade :498, GeometryUpgrade :593, ModelConditionUpgrade
   :994, BuildableHeroListUpgrade :1617, AllowBannerSpawnUpgrade :1641,
   ObjectCreationUpgrade :6334) unless a test proves retail authors THOSE
   untriggered too — if you find that, note it as a follow-up row, do not
   expand scope here.
2. Re-seal the module-contracts policy digest if this file carries one (check
   tools/check-product-contracts.py and the CACHE_VERSION in
   faction_object_cache.py — if the contract SHAPE changed, bump what the code
   says to bump, in the same commit).

## Tests — failing-first
- importer/tests/ (find the module_contracts test file): a fixture
  SubObjectsUpgrade block with a commented/absent TriggeredBy compiles to an
  `untriggered` row WITHOUT raising; a block WITH TriggeredBy is unchanged
  (byte-identical to before). Assert NoldorWarrior specifically converts:
  drive convert on the elven noldorwarrior object (or the smallest importer
  entrypoint that exercises the contract) and assert no ModuleContractError.
- Guard against scope creep: a block missing a DIFFERENT required field still
  raises (prove the change is TriggeredBy-specific).

## Definition of Done (verbatim in report)
1. New tests green; FULL importer suite (sequential, pinned interpreter,
   BFME2_INSTALL set) → exactly the 6 Q6 names, 0 errors, log
   workspace/logs/q26-importer-full.txt. (The elves gap is importer-convert,
   caught by the faction convert, not the pytest suite — but the suite must not
   regress.)
2. Proof: `openbfme-import import-faction --game rotwk --faction elves --convert`
   (pinned interpreter, state-root workspace\retail-work) → coverage shows
   converterGapCount 0 and NoldorWarrior/NoldorWarriorHorde converted (was
   gaps=2). Log workspace/logs/q26-elves-convert.txt. Confirm the OTHER 6
   factions still convert clean (spot-check men + one more; W3D models
   cache-hit so this is fast).
3. check_pack_addresses PASS; gate-hygiene PASS; git status clean; commits
   `fix(importer):` / `test(importer):`, explicit paths.
4. Report orchestration/reports/q26-subobjectsupgrade-untriggered.md. Add queue
   row Q27: "typed-module over-strictness cluster — contracts that raise on
   retail-authored-but-inert shapes (SubObjectsUpgrade done; audit the other 6
   requires-TriggeredBy contracts + men GondorKnightsofDol AutoHealBehavior
   healonlyifnotunderattack) — one lane to admit them as deferred rows."
