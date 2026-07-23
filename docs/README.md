# OpenBFME documentation

This is the public map of the project. OpenBFME has extensive specialist notes,
but most visitors should begin with the short path below rather than reading the
entire archive.

## Start here

1. [Project README](../README.md) — purpose, present state, and roadmap.
2. [Onboarding](ONBOARDING.md) — the ten-minute guided path: run
   `tools/onboard.py` to check prerequisites, validate your install, convert
   or verify content, and run the verification gates.
3. [Getting started](GETTING_STARTED.md) — the manual workflow: prepare a
   BFME2 1.06 installation, run the doctor, import content locally, and launch
   the development slice.
4. [FAQ](FAQ.md) — legality, assets, AI, supported scope, platforms, and common
   misconceptions.
5. [Contributing](../CONTRIBUTING.md) — contribution boundaries, tests, and the
   retail-content firewall.

## Project truth

These documents are authoritative for the claims they own:

| Document | What it answers |
|---|---|
| [DIRECTION.md](../DIRECTION.md) | What are we building, in what order, and what is out of scope? |
| [STATUS.md](../STATUS.md) | What is verified right now, and what remains blocked? |
| [PLAN.md](../PLAN.md) | What is the engineering sequence after the current milestone? |
| [MILESTONE_CURRENT.md](MILESTONE_CURRENT.md) | What exactly must pass before the current slice is complete? |

Numbers, hashes, current gate results, and blockers belong in `STATUS.md`. Other
documents should link to it rather than copying volatile claims.

## Core engineering guides

| Area | Guide |
|---|---|
| System boundaries | [ARCHITECTURE.md](ARCHITECTURE.md) |
| BFME2 evidence and compatibility | [BFME2_PARITY.md](BFME2_PARITY.md) |
| Import, conversion, packs, and containment | [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md) |
| Verification and completion gates | [VERIFICATION.md](VERIFICATION.md) |
| Deterministic simulation and networking target | [SIMULATION_PROTOCOL.md](SIMULATION_PROTOCOL.md) |
| Mod and content-pack direction | [MODDING.md](MODDING.md) |
| Code-only public distribution | [RELEASE_POLICY.md](RELEASE_POLICY.md) |
| Third-party tools and provenance | [THIRD_PARTY.md](THIRD_PARTY.md) |
| AI-assisted development | [AI_DEVELOPMENT.md](AI_DEVELOPMENT.md) |

## Specialist reference notes

Files beginning with `RETAIL_` and the conversion/oracle notes document narrow,
source-backed implementation findings. They are useful when working on that exact
subsystem, but they are not a recommended newcomer reading order and do not own
current project status.

Several older stage-era plans remain marked **Superseded** while their unique
technical conclusions are migrated into canonical contracts and tests. A
superseded document is historical context, not authorization for new work and not
evidence that its old status statements are current.

## Where different information belongs

- Stable product scope: `DIRECTION.md`
- Current evidence and blockers: `STATUS.md`
- Architecture: `docs/ARCHITECTURE.md`
- Exact active acceptance: `docs/MILESTONE_CURRENT.md`
- Practical onboarding: `docs/GETTING_STARTED.md`
- Public contribution rules: `CONTRIBUTING.md`
- Narrow converter/runtime findings: the applicable specialist reference

If two documents appear to disagree, use the owner map above and prefer the
document that owns that type of claim.
