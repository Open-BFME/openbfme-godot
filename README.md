# OpenBFME

**Owner:** integration owner
**Owns:** repository entrypoint, bootstrap, launch commands, and canonical-document links
**Does not own:** product scope, current status, architecture, or milestone acceptance
**Last verified commit:** `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
**Update trigger:** a documented command, path, or canonical document changes
**Validation:** `python -m pytest importer/tests/test_m2_gate_script.py -q`

OpenBFME is a modern, moddable Godot RTS engine targeting measured compatibility
with **The Battle for Middle-earth II 1.06**. Retail content is converted locally
from a user-owned installation and remains under the ignored `.private` workspace.

## Current playable target

Men versus Men on Fords of Isen II is the active private milestone. It uses the
strict selected retail pack and must fail closed when required retail content is
missing. See [DIRECTION.md](DIRECTION.md), [STATUS.md](STATUS.md), and
[docs/MILESTONE_CURRENT.md](docs/MILESTONE_CURRENT.md).

```bat
run_retail_slice.bat
run_retail_slice.bat --test
```

The final milestone command is integration-owner-only:

```bat
run_m2_acceptance.bat -IntegrationOwnerPublish
```

## Repository map

| Path | Role |
|---|---|
| `game/` | Godot presentation and current retail-slice runtime |
| `importer/` | BFME2 extraction, census, conversion, provenance, and pack build |
| `engine/` | Pure authoritative simulation work |
| `contracts/` | Tracked policy and public content/mod contracts |
| `.agents/skills/` | Repository-specific reusable agent workflows |
| `.private/retail-work/` | Ignored retail inputs, tools, caches, reports, and oracle evidence |
| `.private/content-packs/` | Ignored immutable converted packs and active selection |
| `.private/orchestration/` | Ignored integration-owner queue, locks, and loop metrics |

## Canonical documents

- [DIRECTION.md](DIRECTION.md) — stable BFME2 1.06 target, scope ladder, parity definition, and non-goals.
- [STATUS.md](STATUS.md) — current identities, capabilities, blockers, and verified results.
- [PLAN.md](PLAN.md) — milestone sequence and permanent engineering decisions.
- [AGENTS.md](AGENTS.md) — enforceable repository-wide agent guardrails.
- [docs/MILESTONE_CURRENT.md](docs/MILESTONE_CURRENT.md) — exact active definition of done.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — system boundaries and interface status.
- [docs/BFME2_PARITY.md](docs/BFME2_PARITY.md) — completeness and evidence policy.
- [docs/CONTENT_PIPELINE.md](docs/CONTENT_PIPELINE.md) — importer and pack contract.
- [docs/SIMULATION_PROTOCOL.md](docs/SIMULATION_PROTOCOL.md) — deterministic simulation and lockstep protocol.
- [docs/MODDING.md](docs/MODDING.md) — simulation and presentation mod policy.
- [docs/AGENT_WORKFLOW.md](docs/AGENT_WORKFLOW.md) — bounded implementation loop.
- [docs/VERIFICATION.md](docs/VERIFICATION.md) — focused and final gate map.
- [docs/RELEASE_POLICY.md](docs/RELEASE_POLICY.md) — private containment and code-only distribution.
- [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) — donor, tool, license, and provenance ledger.
- [docs/RETROSPECTIVE.md](docs/RETROSPECTIVE.md) — confirmed engineering lessons.

## Guardrails

- Never commit, export, log, or distribute retail payloads.
- Do not add new synthetic Stage 1–10 product work.
- Use the smallest focused check first.
- Never weaken an assertion merely to pass.
- Preserve user changes and honor path locks.
