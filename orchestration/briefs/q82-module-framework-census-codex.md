# Q82 — behavior-module framework + traffic-ranked burn-down census + one pilot module

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10 binding;
rule 7: NO sweeps). Claim row Q82 in orchestration/queue.md (owner=codex-q82).
Exclusive tree access; explicit-path git only; logs → workspace/logs/. Pinned
interpreter workspace\retail-work\tools\python-3.12-env\Scripts\python.exe;
sequential pytest. Do NOT publish/select packs (the pilot may BUILD a scratch
pack under workspace/, never select it).

## Why (owner-ratified 2026-08-25)
99 of 178 simulation-class retail module types have no runtime implementation
(736 authored module rows on live objects do nothing —
docs/state/reachable-runtime-summary.md). Today each new behavior becomes more
mass on the 33,716-line retail_slice_sim.gd. Before burning down the list we
need (A) a ranking so lanes work in player-visible order, and (B) a doorway so
every ported module is a small self-contained file. NEW-MODULE RULE (owner):
packs carry RAW AUTHORED FIELDS; ALL interpretation lives in runtime — a
behavior change must never require a recook.

## Deliverable
### A. Traffic-ranked census (data, not opinion)
Script under tools/ that joins the reachable-runtime ledger's not-executable
module rows against the SELECTED packs' object documents and the 22 retail
skirmish maps' placed objects, producing
`docs/state/module-burndown-ranking.json` + a human table in your report:
per module kind → (a) distinct object types carrying it, (b) instances placed
across the 22 retail skirmish maps, (c) rows on faction-buildable units
(things every match trains). Rank by (c) then (b). Mark each kind's oracle:
`zh-gpl-source` (exists under workspace/reference/open-bfme-1/reference/
CnC_Generals_Zero_Hour/GeneralsMD — check per kind), `decomp-symbols`
(reverse/symbols.csv annotations), or `ini-clean-sheet` (the 9 proven
BFME2-only: StancesBehavior, WallHubBehavior, RadiateFearUpdate,
PillageModule, NotifyTargetsOfImminentProbableCrushingUpdate,
MonitorConditionUpdate, DoCommandUpgrade, FloodUpdate, StrafeAreaUpdate).

### B. Module runtime framework (the doorway)
- A base class (e.g. game/src/retail_slice/modules/retail_behavior_module.gd):
  a module instance binds (object handle, its authored data dict verbatim from
  the pack, sim services interface) and exposes tick + lifecycle hooks. Match
  house naming/style; keep the services surface MINIMAL (query state, submit
  effects through existing sim entry points — no direct state mutation outside
  the sim's existing mutators, or lockstep/determinism dies).
- Sim-side registry: at unit/structure spawn, instantiate registered module
  classes for authored module rows of registered kinds; unregistered kinds
  keep today's named-deferred accounting (do not change it).
- Determinism: module tick order must be stable (authored row order, then
  object id). State the ordering rule in code comment + report.

### C. One pilot module end-to-end (proves the frame)
Pick ONE small ranked module with a ZH oracle — RefundDie or SquishCollide
recommended (both have real ZH source; both small). Pipeline it the new way:
importer emits the module's raw authored fields on the object document
(verbatim INI fields + provenance, NO baked decisions — new compiler code
follows the raw-fields rule), runtime module class implements the behavior
reading those fields, ZH source cited as the behavior reference in the
module file header. If the field is already cooked somewhere, still emit the
raw row (lazy-migration precedent). Scratch-cook ONE faction to a workspace
pack to prove the fields flow; do not select it.

## Tests — failing-first
- Framework: a fixture object with a registered fake module ticks in stable
  order across two sims with identical seeds (hash-equal after N ticks);
  unregistered kinds still land in the deferred ledger unchanged.
- Pilot behavior: failing-first runner asserting the retail outcome (e.g.
  RefundDie: killing the object refunds the authored fraction to its owner —
  exact value from INI, cite line; SquishCollide: crushable infantry dies to a
  crusher per authored rules). Oracle values from INI/ZH, never from our code.
- Census: script is deterministic (two runs byte-identical output) and its
  totals reconcile with reachable-runtime-summary.md counts (name any delta).

## Definition of Done (verbatim outputs in report)
1. Census artifact committed + top-20 table in report with oracle column.
2. Framework + pilot runners green (failing-first shown); state pin: expect
   CONSCIOUS RE-MINT if the pilot behavior changes hashed sim state — measure
   twice, ledger the old→new hash in the runner per house convention (see Q76
   precedent). Lockstep runner green.
3. FULL importer suite → exactly the 6 Q6 names; check_pack_addresses PASS;
   gate-hygiene PASS; git clean; commits feat(sim)/feat(importer)/test(...).
4. Report orchestration/reports/q82-framework-census.md incl. the exact recipe
   the NEXT module lane follows (files to touch, test shape) — that recipe is
   the real deliverable. Add queue rows for the top 3 ranked modules with
   their oracle class.
