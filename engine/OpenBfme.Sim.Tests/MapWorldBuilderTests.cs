using System.Diagnostics;
using OpenBfme.Sim.Map;
using OpenBfme.Sim.Pathing;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

[Collection(TickBudgetCollection.Name)]
public sealed class MapWorldBuilderTests
{
    private readonly ITestOutputHelper _output;

    public MapWorldBuilderTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void FixtureLoadsGridStartsPlotsKnownObjectsAndReportsUnknownTemplates()
    {
        var map = FixtureMap();
        var launch = FixtureLaunch(firstStart: 1, secondStart: 0);
        var world = SimWorld.FromBundle(launch, FixtureBundle(), map);

        Assert.Equal(8, map.HeightGrid.Width);
        Assert.Equal(6, map.HeightGrid.Height);
        Assert.Equal(48, map.HeightGrid.Samples.Count);
        Assert.False(world.PassabilityGrid.IsPassable(3, 1));
        Assert.True(world.PassabilityGrid.IsPassable(3, 3));
        Assert.Equal(10, world.PassabilityGrid.CellSize);
        Assert.Equal(Fixed64.FromInt(65), world.MapLoadReport!.PlayerStartPositions[0].X);
        Assert.Equal(Fixed64.FromInt(15), world.MapLoadReport.PlayerStartPositions[1].X);
        Assert.Equal(2, world.MapLoadReport.PlotsPerPlayer[0]);
        Assert.Equal(2, world.MapLoadReport.PlotsPerPlayer[1]);
        Assert.Equal(4, world.BuildPlots.Count);
        Assert.Equal(3, world.MapLoadReport.SpawnedObjectCount);
        Assert.Equal(1, world.MapLoadReport.UnknownTemplates["MissingRock"]);
        Assert.Equal(2, world.Objects.Values.Count(value => value.TemplateName == "CookBase"));
        Assert.Equal(-1, Assert.Single(world.Objects.Values,
            value => value.TemplateName == "CookMember").Team);
        Assert.Empty(world.MapLoadReport.PlayersWithoutBases);
    }

    [Fact]
    public void MovementOnFixtureMapRoutesAroundWallInRetailWorldUnits()
    {
        var grid = MapWorldBuilder.BuildPassabilityGrid(FixtureMap());
        var world = new SimWorld(
            new SimConfig(new[] { MoverTemplate() }, 17, 1),
            ModuleRegistry.CreateDefault(),
            33,
            grid);
        var unit = world.SpawnObject("Mover", 0, Point(15, 15));
        Assert.True(world.SubmitCommand(Move(1, 0, unit.Id, Point(65, 15))));
        var reachedGapRow = false;

        for (var tick = 0; tick < 600; tick++)
        {
            world.Tick();
            var cell = grid.WorldToCell(unit.Position);
            Assert.True(grid.IsPassable(cell.X, cell.Y),
                $"entered wall cell ({cell.X}, {cell.Y}) at tick {world.TickIndex}");
            reachedGapRow |= cell.Y >= 3;
            if (!unit.FindModule<LocomotorModule>()!.HasOrder) break;
        }

        Assert.True(reachedGapRow, "unit did not route through the wall opening");
        Assert.False(unit.FindModule<LocomotorModule>()!.HasOrder);
        Assert.True(unit.Position.DistanceSquaredTo(Point(65, 15)) <= Fixed64.One);
    }

    [Fact]
    public void TwoMapWorldsWithAttackMovingHordesHashEquallyForSixHundredTicks()
    {
        SimWorld Build()
        {
            var map = FixtureMap();
            var world = SimWorld.FromBundle(FixtureLaunch(0, 1), FixtureBundle(), map);
            var templateIndex = FixtureBundle().Templates.Single(row => row.Name == "CookHorde").Index;
            var left = world.SpawnObject("CookHorde", 0, Point(15, 15));
            var right = world.SpawnObject("CookHorde", 1, Point(65, 45));
            world.AddHorde(new SnapshotHorde(100_000, 0, templateIndex, new[] { left.Id }, 0));
            world.AddHorde(new SnapshotHorde(100_001, 1, templateIndex, new[] { right.Id }, 0));
            Assert.True(world.SubmitCommand(AttackMove(1, 0, 100_000, Point(45, 35))));
            Assert.True(world.SubmitCommand(AttackMove(1, 1, 100_001, Point(45, 35))));
            return world;
        }

        var first = Build();
        var second = Build();
        for (var tick = 0; tick < 600; tick++)
        {
            first.Tick();
            second.Tick();
            Assert.Equal(first.StateHash(), second.StateHash());
        }
    }

    [Fact]
    public void RetailFordsBootsFightsAndTwinRunsForEighteenHundredTicks()
    {
        var bundlePath = RepoPath("workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");
        var mapPath = RepoPath("workspace", "logs", "lane-map-core", "fords-map-v1.json");
        if (!File.Exists(bundlePath) || !File.Exists(mapPath))
        {
            _output.WriteLine($"SKIP: retail proof requires bundle={bundlePath} and map={mapPath}");
            return;
        }
        var bundle = BundleDocument.Load(bundlePath);
        var map = MapDocument.Load(mapPath);
        var launch = MatchLaunch.Load(RepoPath("contracts", "fixtures", "match-launch-v1.json"));

        SimWorld Build()
        {
            var world = SimWorld.FromBundle(launch, bundle, map);
            var loaded = bundle.Templates
                .Where(row => world.BundleLoadReport!.TemplatesFailed.All(failure => failure.Template != row.Name))
                .Select(row => row.Name)
                .ToHashSet(StringComparer.OrdinalIgnoreCase);
            var left = SelectHorde(bundle, loaded, "GondorFighterHorde");
            var right = SelectHorde(bundle, loaded, "MordorFighterHorde");
            var start0 = map.StartPositions[0];
            var start1 = map.StartPositions[1];
            SpawnHorde(world, bundle, left, 0, 100_000, start0);
            SpawnHorde(world, bundle, right, 1, 100_001, start1);
            var meeting = NearestPassableCenter(world.PassabilityGrid,
                new FixedVector2((start0.X + start1.X) / Fixed64.FromInt(2),
                    (start0.Y + start1.Y) / Fixed64.FromInt(2)));
            Assert.True(world.SubmitCommand(AttackMove(1, 0, 100_000, meeting)));
            Assert.True(world.SubmitCommand(AttackMove(1, 1, 100_001, meeting)));
            return world;
        }

        var first = Build();
        var second = Build();
        var damageEvents = 0;
        var stopwatch = Stopwatch.StartNew();
        for (var tick = 0; tick < 1_800; tick++)
        {
            first.Tick();
            second.Tick();
            damageEvents += first.EventsThisTick.Count(value => value.Kind == "damage");
            Assert.Equal(first.StateHash(), second.StateHash());
        }
        stopwatch.Stop();
        var report = first.MapLoadReport!;
        var millisecondsPerTick = (decimal)stopwatch.ElapsedMilliseconds / (1_800 * 2);
        _output.WriteLine($"retail map grid={first.PassabilityGrid.Width}x{first.PassabilityGrid.Height} cell={first.PassabilityGrid.CellSize} " +
            $"map_objects={report.MapObjectCount} spawned={report.SpawnedObjectCount} unknown={report.UnknownTemplates.Values.Sum()} " +
            $"starting_base_markers={report.StartingBaseSpawnedCount} " +
            $"starts={string.Join(',', report.PlayerStartPositions.Select(pair => $"p{pair.Key}:{pair.Value.X}/{pair.Value.Y}"))} " +
            $"plots={string.Join(',', report.PlotsPerPlayer.Select(pair => $"p{pair.Key}:{pair.Value}"))} " +
            $"damage_events={damageEvents} ms_per_tick={millisecondsPerTick:F3}");
        _output.WriteLine("retail unknown templates: " + string.Join(", ",
            report.UnknownTemplates.Select(pair => $"{pair.Key}:{pair.Value}")));
        Assert.Equal(2, report.StartingBaseSpawnedCount);
        Assert.True(damageEvents > 0, "retail Fords hordes produced no damage");
    }

    private static BundleHordeRow SelectHorde(
        BundleDocument document,
        IReadOnlySet<string> loaded,
        string preferred)
    {
        var candidates = document.Hordes!
            .Where(horde => loaded.Contains(horde.Name)
                && horde.RankInfo.Count > 0
                && horde.RankInfo.All(rank => loaded.Contains(rank.UnitType)))
            .OrderBy(horde => horde.Name, StringComparer.Ordinal)
            .ToArray();
        return candidates.FirstOrDefault(horde => horde.Name.Equals(preferred, StringComparison.OrdinalIgnoreCase))
            ?? candidates.First();
    }

    private static void SpawnHorde(
        SimWorld world,
        BundleDocument document,
        BundleHordeRow horde,
        int team,
        int hordeId,
        MapStartPosition start)
    {
        var members = new List<int>();
        foreach (var rank in horde.RankInfo.OrderBy(value => value.Rank))
        foreach (var position in rank.Positions)
        {
            var sign = team == 0 ? Fixed64.One : -Fixed64.One;
            members.Add(world.SpawnObject(rank.UnitType, team, new FixedVector2(
                start.X + sign * Fixed64.FromRaw(position.X.Raw / 10),
                start.Y + Fixed64.FromRaw(position.Y.Raw / 10))).Id);
        }
        var templateIndex = document.Templates.Single(row =>
            row.Name.Equals(horde.Name, StringComparison.OrdinalIgnoreCase)).Index;
        world.AddHorde(new SnapshotHorde(hordeId, team, templateIndex, members, 0));
    }

    private static FixedVector2 NearestPassableCenter(PassabilityGrid grid, FixedVector2 point)
    {
        var origin = grid.WorldToCell(point);
        for (var radius = 0; radius < Math.Max(grid.Width, grid.Height); radius++)
        {
            for (var y = origin.Y - radius; y <= origin.Y + radius; y++)
            for (var x = origin.X - radius; x <= origin.X + radius; x++)
            {
                if (Math.Abs(x - origin.X) != radius && Math.Abs(y - origin.Y) != radius) continue;
                if (!grid.IsPassable(x, y)) continue;
                return new FixedVector2(
                    Fixed64.FromInt(checked(x * grid.CellSize + grid.CellSize / 2)),
                    Fixed64.FromInt(checked(y * grid.CellSize + grid.CellSize / 2)));
            }
        }
        throw new InvalidOperationException("map has no passable cell");
    }

    private static ObjectTemplate MoverTemplate() => new(
        "Mover",
        new[]
        {
            new ModuleSpec(LocomotorModule.TypeName, new Dictionary<string, long>
            {
                ["Speed"] = 60,
                ["SpeedDamaged"] = 60,
                ["TurnRate"] = 360,
                ["Acceleration"] = 5_000,
                ["Braking"] = 5_000,
                ["MinTurnSpeed"] = 0,
                ["MaxTurnWithoutReform"] = 45,
            }),
        });

    private static SimCommand Move(int tick, int team, int id, FixedVector2 target) =>
        TestWorlds.Command(tick, team, 0, "move",
            ("id", CommandValue.OfLong(id)),
            ("x", CommandValue.OfFixed(target.X)),
            ("y", CommandValue.OfFixed(target.Y)));

    private static SimCommand AttackMove(int tick, int team, int id, FixedVector2 target) =>
        TestWorlds.Command(tick, team, 0, "attack_move",
            ("objects", CommandValue.OfLongList(new long[] { id })),
            ("x", CommandValue.OfFixed(target.X)),
            ("y", CommandValue.OfFixed(target.Y)));

    private static MapDocument FixtureMap() => MapDocument.Load(
        RepoPath("contracts", "fixtures", "map-v1.json"));

    private static BundleDocument FixtureBundle() => BundleDocument.Load(
        RepoPath("contracts", "fixtures", "bundle-v1.json"));

    private static MatchLaunch FixtureLaunch(int firstStart, int secondStart) => MatchLaunch.Parse($$"""
        {
          "schema":"openbfme.match-launch.v1","seed":17,
          "pack":{"id":"fixture","sha256":"{{new string('0', 64)}}"},
          "map":{"path":"maps/test/wall.map","sha256":"{{new string('1', 64)}}"},
          "rules":{"tick_ms":33,"starting_resources":1000,"command_point_multiplier":1,"fog_of_war":false,"game_speed":1,"victory":"annihilation"},
          "players":[
            {"seat":0,"team":0,"faction":"FactionMen","controller":"human","start_position":{{firstStart}}},
            {"seat":1,"team":1,"faction":"FactionMordor","controller":"ai","ai_difficulty":"hard","start_position":{{secondStart}}}
          ]
        }
        """);

    private static FixedVector2 Point(int x, int y) =>
        new(Fixed64.FromInt(x), Fixed64.FromInt(y));

    private static string RepoPath(params string[] parts) =>
        Path.Combine(new[] { AppContext.BaseDirectory, "..", "..", "..", "..", ".." }
            .Concat(parts).ToArray());
}
