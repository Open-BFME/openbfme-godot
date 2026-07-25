# OpenBFME documentation

The complete map of the project's documentation. Everything is listed here — if
a document is not on this page, it does not exist.

## Start here

1. [Project README](../README.md) — purpose, present state, and roadmap.
2. [Onboarding](ONBOARDING.md) — the guided path (`tools/onboard.py`) plus the
   manual workflow: prerequisites, install validation, content conversion, and
   launching the game.
3. [FAQ](FAQ.md) — legality, assets, AI, scope, platforms, common
   misconceptions.
4. [Contributing](../CONTRIBUTING.md) — contribution boundaries, tests, and the
   retail-content firewall.

## Project truth

Each of these owns a different kind of claim. If two documents disagree, prefer
the one that owns that claim type.

| Document | What it answers |
|---|---|
| [DIRECTION.md](../DIRECTION.md) | What are we building, in what order, what is out of scope? |
| [STATUS.md](../STATUS.md) | What is verified **right now**, and what is blocked? |
| [PLAN.md](../PLAN.md) | What is the engineering sequence? |
| [CAMPAIGN_PLAN.md](CAMPAIGN_PLAN.md) | How does the campaign lane get built? |

Volatile facts — gate results, counts, hashes, blockers — belong in `STATUS.md`
and nowhere else. Every other document should link to it rather than restate it.

## Engineering guides

| Area | Guide |
|---|---|
| System boundaries and state ownership | [ARCHITECTURE.md](ARCHITECTURE.md) |
| What parity means and how it is evidenced | [BFME2_PARITY.md](BFME2_PARITY.md) |
| Import, conversion, packs, containment | [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md) |
| Verification doctrine and gate hierarchy | [VERIFICATION.md](VERIFICATION.md) |
| Deterministic simulation and networking target | [SIMULATION_PROTOCOL.md](SIMULATION_PROTOCOL.md) |
| Scaling doctrine (data-driven factions, batching, budgets) | [ENGINE_DESIGN_NOTES.md](ENGINE_DESIGN_NOTES.md) |
| Mod and content-pack contract | [MODDING.md](MODDING.md) |
| Build, toolchain pins, release gates | [BUILD_AND_RELEASE.md](BUILD_AND_RELEASE.md) |
| Code-only public distribution policy | [RELEASE_POLICY.md](RELEASE_POLICY.md) |
| Third-party tools and provenance | [THIRD_PARTY.md](THIRD_PARTY.md) |
| Asset containment lanes | [legal_assets.md](legal_assets.md) |
| Art direction | [art_bible.md](art_bible.md) |
| AI-assisted development methodology | [AI_DEVELOPMENT.md](AI_DEVELOPMENT.md) |
| How agent work is queued, locked, and reviewed | [AGENT_WORKFLOW.md](AGENT_WORKFLOW.md) |

## Retail reference

These are the reverse-engineering findings: how the **original game** is authored
and how its executable behaves, plus the contracts our converter and runtime
honour as a result. They are expensive to re-derive, so they are kept in full.

| Document | Subject |
|---|---|
| [RETAIL_HUD_APT_ORACLE.md](RETAIL_HUD_APT_ORACLE.md) | The retail APT/Palantir HUD: ActionScript programs, text and audio host calls, external movies, reachability |
| [RETAIL_CONTROLBAR_WND_ORACLE.md](RETAIL_CONTROLBAR_WND_ORACLE.md) | The retail `ControlBar.wnd` companion, its 21 callbacks, and the four unresolved built-ins |
| [RETAIL_HUD_RUNTIME.md](RETAIL_HUD_RUNTIME.md) | What our converter and Godot runtime do with the above |
| [RETAIL_ENVIRONMENT_ORACLE.md](RETAIL_ENVIRONMENT_ORACLE.md) | Sky, fog, lighting, water and reflection — including the world-sky executable trace |
| [RETAIL_STRUCTURE_LIFECYCLE.md](RETAIL_STRUCTURE_LIFECYCLE.md) | Construction, damage states, destruction: predicates, FX, audio, presenter contract |
| [RETAIL_ASSET_CONVERSION.md](RETAIL_ASSET_CONVERSION.md) | Converter classes, dependency closure, the prop eligibility ladder, roads, particles, W3D edge cases |
| [RETAIL_SLICE_RUNTIME.md](RETAIL_SLICE_RUNTIME.md) | Map selection, exact terrain build, navigation gap register, ambient audio, profile composition |
| [BFME2_VISUAL_ORACLE_NOTES.md](BFME2_VISUAL_ORACLE_NOTES.md) | Observed retail composition, for side-by-side parity comparison |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | Standing limitations and compatibility boundaries |
| [N_TEAM_SCOPING.md](N_TEAM_SCOPING.md) | Survey of remaining two-team assumptions |
| [MILESTONE_CURRENT.md](MILESTONE_CURRENT.md) | Historical M2 acceptance contract, retained because `tools/` and `importer/tests/` parse it |

### How to read the counts inside them

Each retail document was written across several dated investigations. The
consolidated files preserve those sections verbatim, which means they contain
**mutually inconsistent** blocker totals, supported/unsupported counts, and
"N of 21 implemented" figures. Those are historical snapshots, deliberately kept
because the surrounding evidence depends on them. None of them is current
status. Current status is `STATUS.md` and the runner output.

## Documentation rules

- Only documents linked from this page may make status or scope claims.
- Volatile numbers live in `STATUS.md`. Do not copy them elsewhere.
- No archive directories. Git history is the archive — when a document's work is
  finished, delete it rather than marking it "Superseded" and leaving it in the
  tree.
