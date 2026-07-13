# OpenBFME architecture summary

The executable plan and acceptance gates live in [`../PLAN.md`](../PLAN.md). This
summary records the expected architecture without pretending that Phase 0 decisions
have already been proved.

## Fixed decisions

- Godot 4.7 owns rendering, UI, input, audio, and desktop integration.
- Authoritative gameplay is isolated from Godot presentation and render cadence.
- Godot interpolates simulation snapshots; 60/120/144/240 Hz never changes gameplay.
- The importer is a process coordinator that may use .NET, Blender Python, and pinned
  format tools, but it emits only a versioned neutral OpenBFME bundle.
- Runtime code never references OpenSAGE, BIG, W3D, Blender, donor types, or the retail
  installation.
- v0 has one validated bundle format and one active `bfme2_106_slice` target.
- Hordes own global paths and orders. Members own real health, attacks, death, status,
  and formation slots; they do not each own a global navigation agent.
- Authoritative pathfinding has specified deterministic ordering and does not depend on
  Godot NavigationServer or physics-query ordering.
- Native C++/GDExtension work is deferred until profiling proves a hotspot.

## Phase 0 decisions still open

- Pure C# versus typed GDScript for authoritative simulation.
- Simulation and subsystem cadences, measured from the BFME2 1.06 oracle rather than
  assuming a global 30 Hz tick.
- Exact fixed-point representation and path-grid/cook format.
- Whether Fords of Isen II can be the first map without a large script dependency.
- Final animation capability schema after a real Gondor Soldier conversion.
- Performance budgets and production schedule.

## Existing prototype disposition

Keep as donors:

- Camera and input behavior.
- Menu/HUD concepts.
- Content/mod discovery concepts.
- Scenario ideas and useful assertions.
- Presentation asset-loading experiments.

Replace or prove again:

- Pooled battalion HP as canonical simulation.
- The 10 Hz tick-bound view synchronization.
- Loose dictionary data without schema validation.
- Monolithic `SimWorld` and `MatchController` ownership.
- Tests that return success while Godot reports leaked resources.

## Quality invariants

- BFME2 compatibility claims require an original-game oracle fixture and automated
  comparison; INI presence is not proof.
- One stable command runs build, schema, replay, Godot, leak, screenshot, performance,
  donor-dependency, and proprietary-asset gates.
- Passing assertions with `ERROR`, leaked RID/ObjectDB instances, or orphan resources
  is a failed gate.
- Every autonomous task declares allowed paths, dependencies, prohibited data, and
  acceptance commands.
- One integration owner controls cross-lane contracts and architecture decisions.
- No donor code is copied before repository licensing and provenance policy are set.
