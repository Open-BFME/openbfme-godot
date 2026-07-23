using OpenBfme.Sim;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

/// <summary>
/// Tier-1 dual-run oracle: replays the GDScript retail sim's recorded scenario
/// (game/tests/retail_dualrun_trace_runner.gd) inside the C# SimWorld and
/// compares SEMANTIC metrics per sampled tick.
///
/// HONEST SCOPE — the engine implements a small module subset, so only the
/// genuine overlap gates:
///   * team resources per sampled tick: EXACT (gating),
///   * mobile entity counts per sampled tick: EXACT (gating; the recorded
///     1-tick training-completion phase offset — GDScript spawns during
///     command_tick + build_ticks, the C# ProductionModule's
///     decrement-then-spawn during command_tick + build_ticks - 1 — never
///     straddles the 10-tick sampling grid in this scenario),
///   * positions: tolerance-compared and REPORTED but non-gating, because the
///     integrators genuinely differ today — the retail sim accelerates
///     (current_speed += accel*dt, brakes on the final leg) while
///     LinearMoverModule moves at constant speed; exact parity is a P2
///     locomotor work item, not a scenario-construction choice.
///
/// Scenario-construction mirrors (documented, deliberately NOT engine edits):
///   * production cost: the engine's ProductionModule has no resource-cost
///     semantics yet (FINDING), so the replay debits the recorded cost
///     out-of-band at the recorded command tick,
///   * the recorded out-of-band resource grant is applied at the same
///     after-tick/after-sample boundary the GDScript harness used,
///   * the produced battalion is excluded from position comparison: the
///     retail sim walks it door -> create point -> rally
///     (QueueProductionExitUpdate), which has no engine counterpart yet.
/// </summary>
public class DualRunOracleTests
{
    private const string TraceRelativePath = ".private/retail-work/reports/dualrun/gdscript_trace.json";
    private const string FarmTemplate = "farm";
    private const string BarracksTemplate = "barracks";
    private const string BattalionTemplate = "soldier";
    private const double PositionToleranceUnits = 1.5;

    private readonly ITestOutputHelper _output;

    public DualRunOracleTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void ReplayMatchesGdScriptTraceOnTheSupportedOverlap()
    {
        var tracePath = LocateTrace();
        if (tracePath == null)
        {
            _output.WriteLine($"SKIPPED: no trace at {TraceRelativePath}; run game/tests/retail_dualrun_trace_runner.gd first.");
            return;
        }
        var trace = DualRunTrace.Load(tracePath);

        var world = BuildWorld(trace, out var idMap, out var producedTraceIds);
        SubmitReplayCommands(trace, world, idMap);

        // Out-of-band ledger adjustments applied after the given tick's sample:
        // the production cost mirror (engine gap) and the recorded grant.
        var adjustmentsAfterTick = new Dictionary<int, long>();
        foreach (var command in trace.Commands)
        {
            switch (command.Type)
            {
                case "queue_unit":
                    adjustmentsAfterTick[command.Tick] =
                        adjustmentsAfterTick.GetValueOrDefault(command.Tick) - trace.UnitCost;
                    break;
                case "grant_resources":
                    adjustmentsAfterTick[command.Tick] =
                        adjustmentsAfterTick.GetValueOrDefault(command.Tick) + command.Amount;
                    break;
            }
        }

        var samplesByTick = trace.Samples.ToDictionary(sample => sample.Tick);
        var divergences = new DivergenceReport();
        CompareSample(trace, samplesByTick[0], world, idMap, producedTraceIds, divergences);
        for (var tick = 1; tick <= trace.TotalTicks; tick++)
        {
            world.Tick();
            if (samplesByTick.TryGetValue(tick, out var sample))
            {
                CompareSample(trace, sample, world, idMap, producedTraceIds, divergences);
            }
            if (adjustmentsAfterTick.TryGetValue(tick, out var delta))
            {
                world.AddTeamResources(0, delta);
            }
        }

        _output.WriteLine($"trace: {tracePath}");
        _output.WriteLine($"sampled ticks compared: {divergences.SamplesCompared} (interval {trace.SampleInterval}, total {trace.TotalTicks})");
        _output.WriteLine($"resources: first divergence tick = {Describe(divergences.FirstResourceDivergenceTick)}, divergent samples = {divergences.ResourceDivergences}");
        _output.WriteLine($"entity counts: first divergence tick = {Describe(divergences.FirstCountDivergenceTick)}, divergent samples = {divergences.CountDivergences}");
        _output.WriteLine($"positions (non-gating, tolerance {PositionToleranceUnits}): first out-of-tolerance tick = {Describe(divergences.FirstPositionDivergenceTick)}, out-of-tolerance samples = {divergences.PositionDivergences}, max gap = {divergences.MaxPositionGap:0.###} units");
        _output.WriteLine($"health (non-gating): first divergence tick = {Describe(divergences.FirstHealthDivergenceTick)}, divergent samples = {divergences.HealthDivergences}");
        foreach (var line in divergences.Details.Take(20))
        {
            _output.WriteLine(line);
        }

        Assert.True(divergences.ResourceDivergences == 0,
            $"team resources diverged first at sampled tick {divergences.FirstResourceDivergenceTick}");
        Assert.True(divergences.CountDivergences == 0,
            $"entity counts diverged first at sampled tick {divergences.FirstCountDivergenceTick}");
    }

    private static SimWorld BuildWorld(DualRunTrace trace, out Dictionary<long, int> idMap, out HashSet<long> producedTraceIds)
    {
        var farm = trace.Structures.Single(s => s.IncomePerPayout > 0);
        var barracks = trace.Structures.Single(s => s.IncomePerPayout == 0);
        var templates = new[]
        {
            new ObjectTemplate(FarmTemplate, new[]
            {
                new ModuleSpec(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = farm.MaximumHealth }),
                new ModuleSpec(ResourceGeneratorModule.TypeName, new Dictionary<string, long>
                {
                    ["IntervalTicks"] = trace.FarmPayoutIntervalTicks,
                    ["Amount"] = trace.FarmIncomePerPayout,
                }),
            }),
            new ObjectTemplate(BarracksTemplate, new[]
            {
                new ModuleSpec(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = barracks.MaximumHealth }),
                new ModuleSpec(ProductionModule.TypeName, new Dictionary<string, long>
                {
                    ["Build:" + BattalionTemplate] = trace.UnitBuildTicks,
                }),
            }),
            new ObjectTemplate(BattalionTemplate, new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long>
                {
                    ["MaxHealth"] = checked(trace.UnitMemberHealth * trace.UnitMemberCount),
                }),
                new ModuleSpec(LinearMoverModule.TypeName, new Dictionary<string, long>
                {
                    ["SpeedPerTickRaw"] = trace.SpeedPerTick().Raw,
                }),
            }),
        };
        var world = new SimWorld(new SimConfig(templates, randomSeed: 1, teamCount: 2), ModuleRegistry.CreateDefault());
        world.AddTeamResources(0, trace.StartingResources);
        world.AddTeamResources(1, trace.StartingResources);

        // Spawn the initial battalions in trace-id order, then the structures;
        // the map from GDScript ids to engine ids is positional.
        idMap = new Dictionary<long, int>();
        foreach (var entity in trace.InitialEntities.OrderBy(e => e.Id))
        {
            var spawned = world.SpawnObject(BattalionTemplate, entity.Team, new FixedVector2(entity.X, entity.Y));
            idMap.Add(entity.Id, spawned.Id);
        }
        foreach (var structure in trace.Structures.OrderBy(s => s.Id))
        {
            var template = structure.IncomePerPayout > 0 ? FarmTemplate : BarracksTemplate;
            var spawned = world.SpawnObject(template, structure.Team, new FixedVector2(structure.X, structure.Y));
            idMap.Add(structure.Id, spawned.Id);
        }
        producedTraceIds = new HashSet<long>();
        return world;
    }

    private static void SubmitReplayCommands(DualRunTrace trace, SimWorld world, Dictionary<long, int> idMap)
    {
        foreach (var command in trace.Commands)
        {
            switch (command.Type)
            {
                case "issue_move":
                    foreach (var targetId in command.TargetIds!)
                    {
                        Assert.True(world.SubmitCommand(TestWorlds.Command(command.Tick, command.Team, command.Seq, "move",
                            ("id", CommandValue.OfLong(idMap[targetId])),
                            ("x", CommandValue.OfFixed(command.DestinationX)),
                            ("y", CommandValue.OfFixed(command.DestinationY)))));
                    }
                    break;
                case "queue_unit":
                    Assert.Equal(trace.UnitType, command.UnitType);
                    Assert.True(world.SubmitCommand(TestWorlds.Command(command.Tick, command.Team, command.Seq, "queue_production",
                        ("id", CommandValue.OfLong(idMap[command.ProducerId])),
                        ("template", CommandValue.OfString(BattalionTemplate)))));
                    break;
                case "grant_resources":
                    break; // handled as an after-tick ledger adjustment
                default:
                    Assert.Fail($"trace command type '{command.Type}' has no replay mapping");
                    break;
            }
        }
    }

    private void CompareSample(
        DualRunTrace trace,
        TraceSample sample,
        SimWorld world,
        Dictionary<long, int> idMap,
        HashSet<long> producedTraceIds,
        DivergenceReport divergences)
    {
        divergences.SamplesCompared++;

        foreach (var (team, recorded) in sample.TeamResources)
        {
            var replayed = world.TeamResources(team);
            if (replayed != recorded)
            {
                divergences.RecordResources(sample.Tick,
                    $"  tick {sample.Tick}: team {team} resources gd={recorded} cs={replayed}");
            }
        }

        var mobileObjects = world.Objects.Values
            .Where(o => o.TemplateName == BattalionTemplate)
            .OrderBy(o => o.Id)
            .ToList();
        if (mobileObjects.Count != sample.EntityCount)
        {
            divergences.RecordCount(sample.Tick,
                $"  tick {sample.Tick}: entity count gd={sample.EntityCount} cs={mobileObjects.Count}");
        }

        // Battalions the retail sim produced mid-run have no recorded initial
        // mapping; pair them with unmapped engine objects in id order so
        // counts stay comparable, but exclude them from position parity (the
        // engine has no QueueProductionExitUpdate/rally walk yet).
        var mappedEngineIds = idMap.Values.ToHashSet();
        var unmappedEngineObjects = mobileObjects.Where(o => !mappedEngineIds.Contains(o.Id)).ToList();
        var unmappedTraceEntities = sample.Entities.Where(e => !idMap.ContainsKey(e.Id)).OrderBy(e => e.Id).ToList();
        for (var i = 0; i < Math.Min(unmappedEngineObjects.Count, unmappedTraceEntities.Count); i++)
        {
            idMap.Add(unmappedTraceEntities[i].Id, unmappedEngineObjects[i].Id);
            producedTraceIds.Add(unmappedTraceEntities[i].Id);
        }

        foreach (var entity in sample.Entities)
        {
            if (!idMap.TryGetValue(entity.Id, out var engineId) || !world.Objects.TryGetValue(engineId, out var engineObject))
            {
                continue;
            }
            var health = engineObject.FindModule<ActiveBodyModule>()?.Health ?? 0;
            if (health != entity.Health)
            {
                divergences.RecordHealth(sample.Tick,
                    $"  tick {sample.Tick}: entity gd#{entity.Id} health gd={entity.Health} cs={health}");
            }
            if (producedTraceIds.Contains(entity.Id))
            {
                continue;
            }
            var dx = ToDouble(engineObject.Position.X) - entity.X;
            var dy = ToDouble(engineObject.Position.Y) - entity.Y;
            var gap = Math.Sqrt(dx * dx + dy * dy);
            divergences.MaxPositionGap = Math.Max(divergences.MaxPositionGap, gap);
            if (gap > PositionToleranceUnits)
            {
                divergences.RecordPosition(sample.Tick,
                    $"  tick {sample.Tick}: entity gd#{entity.Id} position gap {gap:0.###} (gd=({entity.X}, {entity.Y}) cs={engineObject.Position})");
            }
        }
    }

    private static double ToDouble(Fixed64 value) => (double)value.Raw / Fixed64.OneRaw;

    private static string Describe(int tick) => tick < 0 ? "none" : tick.ToString();

    private static string? LocateTrace()
    {
        var overridePath = Environment.GetEnvironmentVariable("OPENBFME_DUALRUN_TRACE");
        if (!string.IsNullOrEmpty(overridePath))
        {
            return File.Exists(overridePath) ? overridePath : null;
        }
        var directory = AppContext.BaseDirectory;
        while (directory != null)
        {
            var candidate = Path.Combine(directory, TraceRelativePath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(candidate))
            {
                return candidate;
            }
            directory = Path.GetDirectoryName(directory);
        }
        return null;
    }

    private sealed class DivergenceReport
    {
        public int SamplesCompared;
        public int ResourceDivergences;
        public int CountDivergences;
        public int PositionDivergences;
        public int HealthDivergences;
        public int FirstResourceDivergenceTick = -1;
        public int FirstCountDivergenceTick = -1;
        public int FirstPositionDivergenceTick = -1;
        public int FirstHealthDivergenceTick = -1;
        public double MaxPositionGap;
        public List<string> Details { get; } = new();

        public void RecordResources(int tick, string detail)
        {
            ResourceDivergences++;
            if (FirstResourceDivergenceTick < 0)
            {
                FirstResourceDivergenceTick = tick;
            }
            Details.Add(detail);
        }

        public void RecordCount(int tick, string detail)
        {
            CountDivergences++;
            if (FirstCountDivergenceTick < 0)
            {
                FirstCountDivergenceTick = tick;
            }
            Details.Add(detail);
        }

        public void RecordPosition(int tick, string detail)
        {
            PositionDivergences++;
            if (FirstPositionDivergenceTick < 0)
            {
                FirstPositionDivergenceTick = tick;
            }
            Details.Add(detail);
        }

        public void RecordHealth(int tick, string detail)
        {
            HealthDivergences++;
            if (FirstHealthDivergenceTick < 0)
            {
                FirstHealthDivergenceTick = tick;
            }
            Details.Add(detail);
        }
    }
}
