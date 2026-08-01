# Agent reasoning operating manual

**Owner:** Integration owner / project owner  
**Owns:** How agents and AI workers should *think* through OpenBFME work — orientation order, claim standards, scope bounding, fail-closed defaults, stop rules, and how to report.  
**Does not own:** Product scope (`DIRECTION.md`), volatile evidence (`STATUS.md`), queue/locks/packets (`AGENT_WORKFLOW.md`), hard forbid-list (`AGENTS.md`), or verification gate ordering (`VERIFICATION.md`).  
**Last verified commit:** dirty working tree (authored with systems-first RotWK model, 2026-08-01)  
**Update trigger:** A repeated reasoning failure (wrong authority, invented parity, unbounded scope, weak verification) needs a durable cognitive rule — or a rule here is superseded by a mechanical test/hook.  
**Validation:** Compare decisions against this manual *and* the packet/`AGENTS.md` hard constraints; if they conflict, hard constraints win.

This document is the **reasoning** companion to the **process** docs. Read it when
starting work, when stuck, and before claiming done. It does not replace a task
packet or authorize edits.

---

## 1. Purpose

OpenBFME is a long-running, evidence-driven reimplementation of **RotWK 2.01** in
Godot. Models write a lot of the code; the human owner owns correctness. AI
output is easy to make *plausible* and hard to make *true*.

This manual exists so every worker reasons the same way:

1. Orient to the right authority before changing anything.
2. Bound the job to one observable outcome.
3. Prefer retail/source evidence over inference.
4. Fail closed when evidence is missing.
5. Prove the change with the smallest check that can disprove it.
6. When the implementer is Grok, double-check coding work with **Sol medium**
   before claiming done (see §10a).
7. Stop and escalate instead of inventing, expanding, or “finishing” the product.

If a step below would violate `AGENTS.md` or the task packet, **stop**. This
manual never expands authority.

---

## 2. Role model (who is reasoning)

Before acting, name your role for this turn. Do not mix roles silently.

| Role | May do | Must not do |
|---|---|---|
| **Discovery** | Read, measure, census, write private notes under the job root, propose a packet | Implement product behavior, publish packs, weaken tests |
| **Implementer** | Edit only allowed paths; run the packet’s focused acceptance | Final gates, selection/publish, broad refactors, invent oracles |
| **Reviewer** | Read diffs + evidence; emit findings with severity | Expand scope, “fix while reviewing,” trust implementer narrative over code |
| **Integration owner** | Locks, publish, final gates, promote packets, accept lessons | (Human / designated owner only) |
| **Retrospective** | Propose narrow guidance from evidence | Self-edit skills, queue, or AGENTS without a META packet |

Default for an open chat request with no packet: **Discovery first**. Do not
start large implementation until the outcome, source of truth, paths, non-goals,
and acceptance command are clear — either in a packet or restated and confirmed.

---

## 3. Orientation order (always)

When a request arrives, load truth in this order. Do **not** skip to code search
first on product/scope questions.

```text
1. AGENTS.md                 hard forbid-list and authority boundaries
2. DIRECTION.md              product target, systems ladder, non-goals
3. docs/MILESTONE_CURRENT.md active systems-iteration objective
4. STATUS.md                 what is verified / blocked *right now*
5. Task packet (if any)      objective, paths, oracle, acceptance
6. Lane guides               ARCHITECTURE / CONTENT_PIPELINE / VERIFICATION /
                             BFME2_PARITY / ROTWK_SYSTEMS_PATH as needed
7. Code and tests            only after the claim type is known
```

### Document ownership rule

If two docs disagree, prefer the document that **owns that claim type**
(`docs/README.md` table). Never treat a historical retail investigation count,
old M2 freeze language, or a convincing chat summary as current status.

| Claim type | Authority |
|---|---|
| Product target / ladder / non-goals | `DIRECTION.md` |
| What system is in flight | `docs/MILESTONE_CURRENT.md` |
| Current gates, hashes, blockers | `STATUS.md` only |
| How work is queued and locked | `docs/AGENT_WORKFLOW.md` |
| What counts as proof | `docs/VERIFICATION.md` + parity contracts |
| Hard agent constraints | `AGENTS.md` |

**Stale traps (common):** Men/Fords vertical-slice freeze as strategy; BFME2-only
parity baseline; “M2 acceptance %” as product completion; duplicating volatile
counts into plans.

---

## 4. Classify the request

Every request is one of these. Classify before planning.

| Class | Goal | Default move |
|---|---|---|
| **Question / explain** | Accurate answer from authorities + code | Read, cite paths, no drive-by edits |
| **Discovery** | Turn ambiguity into evidence + a boundable packet | Measure, census, gap list; do not implement |
| **Implementation** | One observable outcome with focused proof | Packet or equivalent contract → smallest diff |
| **Parity / oracle** | Close or classify a difference vs retail | Evidence first; fail closed if oracle missing |
| **Conversion / pack** | Deterministic artifacts + provenance | Fail-closed convert; no silent synthetic fill |
| **Review** | Adversarial findings on a bounded diff | Read-only; severity + reproduction |
| **Gate / verification** | Prove or disprove a claim at the right level | Smallest check first; owner for final gates |
| **META / workflow** | Change how work is done | Separate META packet; owner authority |
| **Ops / play spike** | Ground systems in a short session | Record observations; do not redefine strategy |

If the request spans multiple classes, **split**. Implementation that needs an
unknown oracle becomes Discovery first.

Reject or reframe work that:

- invents retail behavior without source;
- weakens assertions to green a gate;
- writes retail payloads outside `.private`;
- expands synthetic Stage 1–10 product surface without owner authority;
- optimizes only for historical M2 Men/Fords unless the packet names that gate;
- treats “not Men/Fords” or “not BFME2” as out of scope for RotWK systems work.

---

## 5. Source-of-truth hierarchy

When deciding what the game *should* do, rank evidence highest to lowest:

```text
1. Effective RotWK 2.01 source / catalog view for the claim
2. Original-game observation (oracle captures, behavioral traces)
3. Named contracts, schemas, and focused tests in-repo
4. Existing runtime that already encodes a measured decision
5. OpenSage / external references (comparison only; never sole proof)
6. Historical docs and investigation notes (context; counts may be stale)
7. Analogy, “typical RTS,” or model prior knowledge  ← not a parity source
```

**Rules:**

- INI presence ≠ conversion ≠ runtime ≠ audiovisual parity.
- BFME2-only evidence cannot substitute for RotWK-only content (e.g. Angmar).
- Parsed values and asset counts are not completion.
- If the source is ambiguous, **classify the gap** and fail closed; do not pick a
  “reasonable” default in private parity mode.
- Prefer census → closure → convert → gap report over multi-day one-map JSON
  recipes when building systems.

---

## 6. The reasoning loop

Use this loop for every non-trivial task. Skip steps only when the packet already
supplies them and they still match the tree.

### Step A — Restate the outcome

Write one sentence:

> After this work, an external observer can see/verify: **___**.

If you cannot name an observable outcome, you are not ready to implement.

### Step B — Name the scoreboard effect

Tie the outcome to:

- a system on the ladder in `DIRECTION.md`, and/or
- the active objective in `MILESTONE_CURRENT.md`, and/or
- a specific blocker or coverage row in `STATUS.md`.

If it advances none of those and is not containment/P0, say so and ask whether
to proceed.

### Step C — Bound the surface

List:

- **allowed paths** (narrow);
- **forbidden paths** (publication, selection, unrelated hotspots);
- **non-goals** (explicit);
- **dependencies** and **stop_if** conditions.

Prefer one primary system per packet. Prefer proving generality on **≥2 maps or
≥2 factions** when claiming a system is general — but do not expand maps/factions
inside an implementation packet if that was not the objective.

### Step D — Name the disproof

Before coding, name the **smallest focused command** that would fail if the
change is wrong. Workers run only that (plus strictly necessary local follow-ups).
They do **not** run final milestone gates or rewrite selection to make a check
convenient.

### Step E — Discover or implement

- Missing oracle / ambiguous retail behavior → Discovery packet or read-only
  investigation; stop before inventing.
- Clear source + bounded paths → implement the smallest change that satisfies the
  oracle/acceptance.
- Prefer mechanical port of measured behavior over redesign-while-porting.

### Step F — Verify honestly

- Run the named focused check.
- Treat warnings, leaks, orphans, escaped retail paths, and required-path
  fallbacks as failures when the gate forbids them.
- Do not delete or weaken assertions to pass.
- Do not claim identity-bound parity from mixed digests or stale runs.
- **If this session’s implementer is Grok and code was written or modified:**
  run the **Sol medium** double-check (§10a) before treating the work as ready
  to report. Grok does not self-certify coding diffs.

### Step G — Report for integration

Return:

1. what changed (paths + intent);
2. exact command(s) run and result (focused check **and** Sol medium review);
3. Sol medium findings (or “no defects”) and what was fixed;
4. what is still unproven;
5. risks and recommended next packet (if any).

Do not claim milestone completion, pack publication, or “parity achieved” from a
worker-focused check alone.

---

## 7. Fail-closed defaults

When uncertain, choose the option that **surfaces the gap** rather than hides it.

| Situation | Fail-closed choice |
|---|---|
| Missing retail asset / binding | Unresolved / gap ledger entry; no silent generic mesh in parity mode |
| Unknown module / opcode | Opaque-deferred or explicit unsupported; no fake success |
| Ambiguous INI / script semantics | Record ambiguity; do not guess “what Sage probably means” |
| Pack requirement absent | Fail load or mark publication not ready; do not mix synthetic fill |
| Test fails after “real” fix attempt | Escalate with evidence; do not hollow the assertion |
| Overlap with user/other agent paths | Stop; do not merge by force or destructive git |
| Broad gate needed to diagnose | Escalate; workers do not own final/shared mutation gates |

**Silent fallback is a product bug**, not a helpful feature, in private parity
mode.

---

## 8. Systems-first reasoning (not slice-freeze)

The active model is **systems-first iterative** (`DIRECTION.md`):

1. Pick one primary system (or two tightly coupled).
2. Implement/harden with fail-closed conversion/runtime.
3. Attach a focused automated check.
4. Prefer multi-map / multi-faction proof when claiming generality.
5. Record evidence in `STATUS.md`; move on.

Historical Men/Fords M2 tooling may still exist for regression. Reason about it
as **legacy gate surface**, not as the product strategy. Do not block RotWK
systems work because it is “outside the Fords slice.”

Progress metrics that matter:

- reusable factories (cook, closure, bind, pack, sim, script/AI);
- coverage / gap burn-down;
- “RotWK pack still boots” smoke;

not “Fords M2 freeze percentage.”

---

## 9. Scope discipline while coding

### Prefer

- Smallest diff that moves the named outcome.
- Existing factories, ledgers, and pack paths over new parallel pipelines.
- Explicit gap accounting over partial green dashboards.
- One authority for a behavior; delete or clearly demote the loser after proof.
- Private job root: `.private/scratch/jobs/<task-id>/` for scratch and reports.

### Avoid

- Open-ended “improve,” “polish,” “finish,” or multi-subsystem rewrites in one go.
- Drive-by refactors and formatting outside allowed paths.
- New docs that restate STATUS or invent parallel plans (Git history is the archive).
- Unmeasured performance work (“should be faster”).
- Stubs labeled as temporary parity.
- Combining importer + runtime + simulation + docs + perf unless the packet truly
  requires that boundary **and** has checks for it.

### Path and concurrency reasoning

- Main worktree = integration owner.
- Path-prefix overlap with locks or live user edits → **stop**.
- Large importer / generated-runtime files are single-writer.
- Never commit retail payloads; never export `.private` content.

---

## 10a. Grok implementers: Sol medium double-check (mandatory)

**Owner rule:** Grok Build / Grok agents are allowed to implement, but they are
**not** the final judge of their own coding quality on this repo. After any
non-trivial code change and before “done,” they must call **Sol medium** via the
Codex CLI for an independent double-check.

### When it applies

| Applies | Does not apply |
|---|---|
| Grok wrote or modified product/test/tooling code | Pure Q&A with no edits |
| Grok changed importer, runtime, sim, scripts, gates, or contracts | Docs-only wording with no behavior change (optional but recommended if claims change) |
| Grok fixed a bug or green-gated a runner | Owner explicitly waived Sol for a tiny mechanical edit |

“Non-trivial” means anything beyond a typo or a single-line comment. When unsure,
run Sol.

### What “Sol medium” means

On this machine, Sol is the Codex CLI default model (`gpt-5.6-sol` in
`~/.codex/config.toml`) with **`model_reasoning_effort = "medium"`**. Pin both
explicitly so a config drift does not silently weaken the review:

```bat
codex.cmd review --uncommitted -c model="gpt-5.6-sol" -c model_reasoning_effort="medium"
```

For a deeper correctness pass (logic, packet paths, fail-closed gaps), use a
read-only exec review after `review`:

```bat
codex.cmd exec -s read-only -c model="gpt-5.6-sol" -c model_reasoning_effort="medium" -C "%CD%" "Double-check the uncommitted OpenBFME coding changes. Read AGENTS.md, the task objective, and the focused acceptance result. Report correctness bugs, invented parity, path/scope violations, weak or hollow tests, silent fallbacks, and missing fail-closed handling. Severity P0/P1/P2. Do not edit files."
```

On PowerShell, prefer `codex.cmd` over `codex` when script execution policy
blocks `codex.ps1`.

Do **not** edit shared Codex config, memories, skills, or hooks from an
implementation task. Call Sol; do not reconfigure Sol.

### How to use the result

1. Run the packet’s focused acceptance first (or the smallest disproof command).
2. Run Sol medium on the uncommitted (or packet-bounded) diff.
3. Treat Sol **P0/P1** findings as blocking: fix and re-run focused check + Sol,
   or escalate with the template in §11 if the fix needs new scope/authority.
4. Sol **P2** may be listed as follow-up unless it is a real correctness risk.
5. Sol **P3**/style noise: discard unless it violates an explicit repo rule.
6. Report Sol’s summary in the handoff. “I think it’s fine” is not a substitute.

### Limits

- Sol medium is a **second model opinion**, not identity-bound parity proof and
  not a substitute for focused runners or owner final gates.
- Sol must stay **read-only** unless the owner authorized a separate Sol
  implementation packet.
- If Codex/Sol is unavailable, **stop claiming done**: say blocked on Sol
  double-check, leave the focused check result, and ask the owner how to
  proceed. Do not skip silently.

### Why this rule exists

Grok is strong at orientation, docs, and fast iteration on this tree, but coding
diffs need a stronger independent check before they are treated as ready. Sol
medium is that check. Self-review by the same Grok session does not satisfy this
rule.

---

## 10. Verification reasoning

From `docs/VERIFICATION.md`, internalize the hierarchy:

```text
focused implementation check
  → affected-lane integration checks
  → selected-pack and containment checks
  → oracle and reliability evidence
  → milestone / systems final gate (owner)
```

Workers live almost entirely on the **first** rung. Climbing rungs is an owner
decision.

A claim is only as strong as the **identity** it was measured under:

```text
git revision + dirty-state digest + profile digest + bundle digest
```

Evidence from another identity is stale unless proven independent of the changed
inputs. When the tree is dirty and multi-agent active, say so; do not launder
diagnostic runs into approval language.

---

## 11. Stop conditions (immediate)

Stop and escalate — do not “push through” — when any of these appear:

- Possible retail escape outside `.private` or into git history
- Unauthorized canonical publication or `selection.json` mutation
- Lock / path overlap or changed base commit identity
- Weakened, deleted, or vacuous tests to obtain green
- Stub, silent fallback, or invented parity behavior
- Cross-subsystem scope growth beyond the packet
- Missing or contradictory oracle evidence
- Repeated failure after one honest correction
- Two failed P0/P1 fix rounds
- Diagnosis that only a broad/shared-mutating gate can perform
- Apparent need for destructive git (`reset --hard`, force-push, stash games)

### Escalation template

```text
Blocker:
Evidence:
Attempted:
Options:
Recommendation:
Authority or lock requested:
```

---

## 12. Anti-patterns (how agents usually go wrong here)

Memorize these failure modes; they are project-specific, not generic LLM advice.

1. **Plausible RTS inventing** — implementing “how BFME feels” from training data
   instead of RotWK source/oracle.
2. **Count theater** — treating converted file counts or INI field presence as
   skirmish parity.
3. **Slice gravity** — rejecting multi-map/Angmar/RotWK systems work because an
   old M2 document mentioned Fords.
4. **Green by subtraction** — deleting assertions, skipping maps, or catching
   exceptions to silence failures.
5. **Authority blur** — writing volatile numbers into plans; treating chat memory
   as STATUS; treating OpenSage as the oracle of record.
6. **Role collapse** — implementer publishing packs, reviewer fixing code,
   retrospective editing AGENTS mid-task.
7. **Gate inflation** — running the world to prove a one-file fix; or never
   running the one command that can disprove it.
8. **Parallel universe pipelines** — second converter, second binder, second pack
   layout “just for this task.”
9. **Redesign during port** — changing cadence, ownership, or semantics while
   claiming a mechanical translation.
10. **Optimistic fallback** — placeholder art/audio/UI in private parity so the
    demo “looks complete.”
11. **Grok self-certify** — shipping or declaring coding work done without a Sol
    medium double-check (or skipping Sol because the focused test went green).

When you notice yourself doing one of these, reverse the step and re-enter the
loop at Step A or escalate.

---

## 13. How to talk to the owner (output shape)

Jonathan owns product judgment. Prefer:

- **Short orientation**: role, objective, authority used.
- **Concrete evidence**: paths, commands, exit codes, ledger %, oracle IDs.
- **Honest remainder**: what still fails closed / gap list.
- **One recommended next step**, not a five-sprint plan unless asked.

Avoid:

- Milestone completion claims without identity-bound owner gates.
- “Should be fine” without a disproof command.
- Restating half of DIRECTION as filler.
- Asking permission for every local, reversible edit **inside** an already
  authorized packet — but **do** ask before publish, selection, destructive git,
  broad gates, or scope expansion.

---

## 14. Production loop cheat-sheet

From `AGENT_WORKFLOW.md` — pick the loop that matches the class:

| Loop | Output |
|---|---|
| Discovery | Evidence + bounded ready packet |
| Census | Classification of source/root/reference |
| Conversion | Deterministic artifacts + provenance + gap ledger |
| Parity | Closed or classified oracle difference |
| Implementation | Focused passing diff on allowed paths |
| Review | Findings or explicit no-defect |
| Fix | Closed accepted P0/P1 only |
| Integration | Union of affected checks (owner) |
| Performance | Measured improvement only |
| Retrospective | Proposals; no self-modification |

---

## 15. Quick self-check before “done”

Answer yes to all, or do not claim done:

- [ ] Outcome is observable and matches the packet/request class  
- [ ] Source of truth is named; nothing critical was invented  
- [ ] Diff stays inside allowed paths; forbidden paths untouched  
- [ ] Non-goals respected; no drive-by scope  
- [ ] Focused acceptance command run; result reported honestly  
- [ ] If implementer is Grok and code changed: Sol medium double-check run (§10a); P0/P1 closed or escalated  
- [ ] Failures fail closed (gaps visible, no silent parity fake)  
- [ ] No retail payload committed or published without authority  
- [ ] Unresolved risks listed for the owner  

---

## 16. Related documents

| Document | Use when |
|---|---|
| [AGENTS.md](../AGENTS.md) | Hard constraints and forbid-list |
| [AGENT_WORKFLOW.md](AGENT_WORKFLOW.md) | Packets, locks, review, metrics, retrospectives |
| [DIRECTION.md](../DIRECTION.md) | Target, ladder, systems-first model |
| [MILESTONE_CURRENT.md](MILESTONE_CURRENT.md) | Active systems objective |
| [STATUS.md](../STATUS.md) | Current verified state and blockers |
| [VERIFICATION.md](VERIFICATION.md) | Evidence doctrine and gate hierarchy |
| [AI_DEVELOPMENT.md](AI_DEVELOPMENT.md) | Why evidence is strict under AI-assisted work |
| [ROTWK_SYSTEMS_PATH.md](ROTWK_SYSTEMS_PATH.md) | Operator commands for RotWK systems factory |
| [BFME2_PARITY.md](BFME2_PARITY.md) | What parity claims require (RotWK-primary) |

---

## 17. One-page mental model

```text
Orient (authorities) → Classify request → Bound outcome
        ↓
  Oracle/source clear?  --no--> Discover / escalate (fail closed)
        yes
        ↓
  Smallest change on allowed paths
        ↓
  Focused check that can disprove it
        ↓
  Report evidence + remainder; owner integrates
```

**Default bias:** truth over demo, gaps over guesses, systems over snowflake
profiles, RotWK evidence over BFME2 nostalgia, stop over invent.
