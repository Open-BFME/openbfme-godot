# Known failure signatures

Use this file to recognize patterns, not to prove repetition. A retrospective must cite the underlying task packets, diffs, logs, or review findings before promoting a lesson. This initial index contains diagnostic signatures only; owner-approved incident references may be added by a separate bounded `META` task.

| Signature | Diagnostic evidence | Preferred correction | Do not do |
|---|---|---|---|
| Scope expands beyond BFME2 1.06 | Task or diff introduces RotWK or unrelated product work without an owner decision | Restore the BFME2-only boundary in the active packet | Treat an aspirational roadmap as current authorization |
| Asset presence is reported as parity | Completion claim has converted files but no retail/original-game behavior evidence | Require the applicable oracle and evidence lanes | Infer parity from parsed INI values or file counts |
| Translation and redesign happen together | Port diff also changes cadence, ownership, data model, or gameplay behavior | Mechanically port first; modernize in a separate reviewed packet | Accept compiler success as behavioral proof |
| Worker mutates canonical publication | Worker changes selected packs, shared profiles, queue state, or final-gate publication | Return publication to the integration owner | Broaden a bounded worker lock after starting |
| Private retail payload escapes | Retail bytes, paths containing payload copies, or verbose logs appear outside approved `.private` roots | Stop, contain, and perform a P0 audit | Continue other work before containment is proven |
| Assertion is weakened to obtain green output | Expected behavior or threshold changes without new oracle evidence | Repair implementation or obtain owner-approved requirement evidence | Reclassify a failure as expected solely to pass |
| Slow broad gate replaces focused diagnosis | Worker runs an integration/final gate before the smallest relevant check | Add or use one focused acceptance command | Measure diligence by gate duration or assertion count |
| Guidance grows after one ordinary incident | One-off mistake produces a global rule, new skill, or automation | Constrain the next packet only | Persist speculative guidance without recurrence evidence |
| Presentation state enters authoritative simulation | Selection, camera, UI, audio, or render-only data affects hashes or commands | Move client-local state outside deterministic truth | Patch divergence with transform snapshots |
| Simulation and presentation mods share one compatibility rule | Cosmetic overrides require identical gameplay hashes, or presentation changes alter deterministic data | Validate and hash the two pack categories separately | Allow presentation packs to write simulation fields |
