# OpenBFME verification contract

> **Owner:** Integration owner
> **Owns:** Gate ordering, focused versus final checks, oracle approval, deterministic evidence, performance qualification, and completion declarations.
> **Does not own:** Product scope, implementation design, source conversion rules, release licensing decisions, or worker task assignment.
> **Last verified commit:** `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
> **Update trigger:** A milestone definition, gate, oracle recipe, evidence schema, benchmark, or release-blocking invariant changes.
> **Validation:** The current milestone's final integration-owner command and its identity-bound generated evidence.

## Evidence doctrine

Compilation, asset presence, parsed INI values and passing helper assertions are not
parity by themselves. A claim is supported by the smallest relevant combination of:

- original-game observation;
- retail source/effective-view evidence;
- language-independent command/state trace;
- focused reproduced-defect test;
- deterministic pack/replay identity;
- runtime behavior or capture;
- containment/provenance audit; and
- measured performance or reliability evidence.

Every result belongs to one identity tuple:

```text
Git revision + dirty-state digest + profile digest + bundle digest
```

Evidence from another tuple is stale unless the validating schema explicitly proves
that none of its inputs changed. Current counts, hashes and benchmark values live in
`STATUS.md` or generated reports, not in this document.

## Gate hierarchy

```text
focused implementation check
        -> affected-lane integration checks
        -> selected-pack and containment checks
        -> oracle and reliability evidence
        -> milestone final gate
```

Focused checks diagnose one bounded outcome and are the only checks workers should
run. They do not declare a milestone complete. The integration owner runs the union of
affected checks, controls pack publication, freezes the evidence identity and runs the
final gate.

Assertions are never weakened merely to pass. A passing assertion accompanied by an
error, warning, leak, orphan, remaining resource or RID-allocation diagnostic is a
failure when the gate declares those diagnostics forbidden.

## M2 Men/Fords final gate

The only final M2 declaration command is:

```bat
run_m2_acceptance.bat -IntegrationOwnerPublish
```

It is integration-owner-only. All importer, pack, focused Godot, retail-slice, legacy,
security, containment, oracle and soak commands are subordinate evidence. None is an
alternate final gate, and results cannot be combined from different identities.

The M2 wrapper must verify at least:

- exact selected profile and immutable bundle identity;
- repeat-build and provenance validity;
- strict selected-pack runtime origin with no required fallback;
- the full focused runtime contract set;
- approved retail-versus-Godot capture pairs for the same identity;
- the required live soak, restarts and completed matches;
- frozen performance thresholds and recorded renderer/environment evidence;
- export/private containment; and
- an unchanged identity throughout the final run.

`docs/MILESTONE_CURRENT.md` owns the exact M2 behavior and capture denominator. This
document owns how that evidence is ordered and accepted.

## Focused checks

Use the smallest check that can falsify the change. Existing focused surfaces include:

- importer unit and malicious-fixture suites;
- pack load, schema, provenance and containment runners;
- targeted Godot runtime runners;
- retail-slice integration in test mode;
- oracle workspace/capture/review tools; and
- bounded reliability/performance runners.

Workers must not run broad/final gates for bounded work. A focused pass means only that
the packet's acceptance condition is ready for review. The integration owner decides
which affected-lane checks are required before integration.

## Oracle process

Retail compatibility requires a declared scenario, reproducible camera/input state and
evidence from the original game plus OpenBFME. For M2:

1. Freeze the identity and create a private oracle workspace.
2. Freeze performance thresholds before the final soak.
3. Capture the exact named retail or Godot window and verify dimensions/digests.
4. Review each pair and record differences by severity.
5. Reject any required row with unresolved severity-0 or severity-1 differences.
6. Finalize only after every required row and reliability condition passes for the same
   identity.

Oracle artifacts remain below `workspace/retail-work/oracle`. Tools may capture and
validate evidence, but they do not operate the original game, invent a camera state or
auto-approve a visual judgment.

## Importer and content checks

Pipeline verification covers:

- malicious archive, containment, precedence and cache cases;
- tool/profile/source/recipe attestation;
- cold, warm and resumed deterministic builds;
- transactional publication and selection rollback;
- runtime loading without source/tool access;
- missing-capability and unsafe-path failures;
- no silent strict-retail fallback; and
- code-only export boundary scanning.

Importer or pack success alone does not prove runtime behavior or audiovisual parity.

## Simulation and networking checks

The production simulator requires:

- GDScript-versus-C# traces during the bounded mechanical port;
- exact state/event equivalence before authority cutover;
- the separately reviewed 10 Hz to 30 Hz migration;
- cross-render-rate replay agreement;
- supported-platform digest agreement;
- canonical serialization golden cases and malformed-input rejection;
- two-through-eight-player scenarios;
- maximum legal eight-player load plus headroom;
- latency, jitter, loss and packet reordering;
- late-command rejection without global stalls;
- checkpoint restore, reconnect and observer join; and
- injected desync recovery and repeated-resync disconnect policy.

The current GDScript `state_signature()` is not accepted as a network digest.

## Performance and reliability

Performance work begins with a fixed scenario, environment and metric. Retain an
optimization only when the same benchmark improves without parity or determinism
regression. Track frame-time tails, simulation tick debt, allocations, memory growth,
conversion cold/warm/resume time and network recovery-not only averages.

M2 uses its identity-bound live-soak and restart requirements from the current
milestone contract. Later scale gates qualify the maximum legal eight-player BFME2
command-point/member/projectile load plus the plan's headroom. Numeric results and
thresholds live in generated reports or `STATUS.md`.

## Test admission

Add persistent tests only for:

- oracle-backed compatibility;
- determinism or serialization;
- public schema/protocol/mod contracts;
- private containment;
- network validation or recovery;
- a reproduced defect; or
- a measured performance budget.

Avoid tests that pin prose, incidental implementation structure, trivial getters,
runner totals or assertion counts. Completion is observable behavior plus evidence,
not test volume.

## Failure and handoff

A gate failure records command, identity, first actionable failure and artifact path.
Do not continue to slower gates when a focused prerequisite fails. Do not publish a
new selected pack to diagnose an unreviewed change. If evidence is missing or
contradictory, create a read-only discovery packet rather than inventing the oracle.
