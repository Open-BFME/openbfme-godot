using System.Diagnostics;
using System.Text.Json;
using OpenBfme.Sim;
using OpenBfme.Sim.Pathing;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

public class MovementSystemTests
{
    private readonly ITestOutputHelper _output;

    public MovementSystemTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void SpeedFiftyFiveCrossesTwoHundredUnitsInExpectedTicks()
    {
        var world = CreateWorld(PassabilityGrid.Uniform(256, 16));
        var unit = world.SpawnObject("unit", 0, Point(1, 4));
        SubmitMove(world, unit.Id, Point(201, 4));

        var ticks = AdvanceUntilIdle(world, unit, 200);
        var expected = (int)Math.Ceiling(200.0 / (55.0 * 0.033));

        Assert.True(ticks >= expected - 1 && ticks <= expected + 1,
            $"ticks={ticks} expected={expected} position={unit.Position} speed={unit.FindModule<LocomotorModule>()!.CurrentSpeed}");
        Assert.True(unit.Position.DistanceSquaredTo(Point(201, 4)) <= Fixed64.One);
        Assert.Empty(world.EventsThisTick);
    }

    [Fact]
    public void DefaultPassabilityGridUsesMatchConfigurationDimensions()
    {
        var config = new SimConfig(
            new[] { UnitTemplate() },
            randomSeed: 1,
            teamCount: 1,
            mapWidthCells: 73,
            mapHeightCells: 41);
        var world = new SimWorld(config, ModuleRegistry.CreateDefault());

        Assert.Equal(73, world.PassabilityGrid.Width);
        Assert.Equal(41, world.PassabilityGrid.Height);
        Assert.Equal(73 * 41, world.PassabilityGrid.CellCount);
    }

    [Fact]
    public void WallIsRoutedAroundWithoutEnteringAnImpassableCell()
    {
        const int width = 64;
        const int height = 32;
        var passable = Enumerable.Repeat(true, width * height).ToArray();
        var costs = Enumerable.Repeat(1, width * height).ToArray();
        for (var y = 0; y < 27; y++) passable[y * width + 30] = false;
        var grid = new PassabilityGrid(width, height, passable, costs);
        var world = CreateWorld(grid, speed: 40);
        var unit = world.SpawnObject("unit", 0, Point(5, 5));
        SubmitMove(world, unit.Id, Point(55, 5));

        for (var tick = 0; tick < 500; tick++)
        {
            world.Tick();
            var x = unit.Position.X.ToIntFloor();
            var y = unit.Position.Y.ToIntFloor();
            Assert.True(grid.IsPassable(x, y), $"entered impassable cell ({x}, {y}) at tick {world.TickIndex}");
            if (!unit.FindModule<LocomotorModule>()!.HasOrder) break;
        }

        Assert.False(unit.FindModule<LocomotorModule>()!.HasOrder);
        Assert.True(unit.Position.DistanceSquaredTo(Point(55, 5)) <= Fixed64.One,
            $"final position={unit.Position} distance2={unit.Position.DistanceSquaredTo(Point(55, 5))}");
        Assert.Equal(1, world.Movement.CachedFlowFieldCount);
        world.SetPassabilityGrid(PassabilityGrid.Uniform(width, height));
        Assert.Equal(0, world.Movement.CachedFlowFieldCount);
    }

    [Fact]
    public void HordeWheelsForThirtyDegreesButStopsAndReformsForOneHundredTwenty()
    {
        var wheel = CreateHordeWorld(headingDegrees: 15, out var wheelHorde);
        var wheelStart = AveragePosition(wheel, wheelHorde.Members);
        SubmitMove(wheel, wheelHorde.Id, Point(110, 110));
        wheel.Tick();
        Assert.True(wheel.Movement.TryGetHordeState(wheelHorde.Id, out var wheelState));
        Assert.False(wheelState!.StoppedForReformLastTick);
        Assert.NotEqual(wheelStart, wheelState.LeaderPosition);

        var reform = CreateHordeWorld(headingDegrees: -75, out var reformHorde);
        var reformStart = AveragePosition(reform, reformHorde.Members);
        SubmitMove(reform, reformHorde.Id, Point(110, 110));
        reform.Tick();
        Assert.True(reform.Movement.TryGetHordeState(reformHorde.Id, out var reformState));
        Assert.True(reformState!.StoppedForReformLastTick);
        Assert.True(reformState.IsReforming);
        Assert.Equal(reformStart, reformState.LeaderPosition);

        reform.Advance(250);
        Assert.True(reform.Movement.TryGetHordeState(reformHorde.Id, out reformState));
        Assert.False(reformState!.HasOrder);
        Assert.False(reformState.IsReforming);
        for (var index = 0; index < reformHorde.Members.Count; index++)
        {
            var member = reform.Objects[reformHorde.Members[index]];
            var slot = reform.Movement.GetHordeSlotPosition(reformHorde, index);
            Assert.True(member.Position.DistanceSquaredTo(slot) <= Fixed64.FromFraction(1, 16),
                $"member {member.Id} is not in slot {index}: member={member.Position} slot={slot}");
        }
    }

    [Fact]
    public void CommandBundleMovesOwnedObjectsAndRejectsAnotherTeam()
    {
        var launch = MatchLaunch.Load(MatchLaunchTests.RepoPath("contracts", "fixtures", "match-launch-v1.json"));
        var grid = PassabilityGrid.Uniform(64, 64);
        var world = new SimWorld(launch, new[] { UnitTemplate() }, passabilityGrid: grid);
        var owned = world.SpawnObject("unit", 0, Point(4, 4));
        var enemy = world.SpawnObject("unit", 1, Point(8, 4));
        var bundleJson = """
            {"schema":"openbfme.command.v1","tick":1,"seat":0,"seq":0,"commands":[{"type":"move","args":{"objects":[OWNED,ENEMY],"x":30,"y":20}}]}
            """
            .Replace("OWNED", owned.Id.ToString(System.Globalization.CultureInfo.InvariantCulture), StringComparison.Ordinal)
            .Replace("ENEMY", enemy.Id.ToString(System.Globalization.CultureInfo.InvariantCulture), StringComparison.Ordinal);
        var bundle = SimCommandBundle.Parse(bundleJson);

        Assert.True(world.SubmitCommandBundle(bundle));
        world.Tick();

        Assert.True(owned.FindModule<LocomotorModule>()!.HasOrder);
        Assert.False(enemy.FindModule<LocomotorModule>()!.HasOrder);
        var diagnostic = Assert.Single(world.Diagnostics);
        Assert.Equal("wrong_team", diagnostic.Code);
        Assert.Equal(enemy.Id, diagnostic.Object);
    }

    [Fact]
    public void TwinWorldsMatchEveryHashForSixHundredTicks()
    {
        var first = CreateWorld(PassabilityGrid.Uniform(128, 128));
        var second = CreateWorld(PassabilityGrid.Uniform(128, 128));
        for (var index = 0; index < 20; index++)
        {
            var start = Point(4 + index, 4 + index % 5);
            var a = first.SpawnObject("unit", index % 2, start);
            var b = second.SpawnObject("unit", index % 2, start);
            Assert.Equal(a.Id, b.Id);
            var destination = Point(90 + index % 10, 80 + index % 7);
            var commandA = MoveCommand(1 + index % 3, index % 2, index, a.Id, destination);
            var commandB = MoveCommand(1 + index % 3, index % 2, index, b.Id, destination);
            Assert.True(first.SubmitCommand(commandA));
            Assert.True(second.SubmitCommand(commandB));
        }

        for (var tick = 0; tick < 600; tick++)
        {
            first.Tick();
            second.Tick();
            Assert.Equal(first.StateHash(), second.StateHash());
        }
    }

    [Fact]
    public void CanonicalSnapshotRestoresHordeMovementMidReform()
    {
        var world = CreateHordeWorld(headingDegrees: -75, out var horde);
        SubmitMove(world, horde.Id, Point(110, 110));
        world.Advance(5);

        var grid = world.PassabilityGrid;
        var restored = SimWorld.Restore(
            world.Snapshot(),
            new SimConfig(new[] { UnitTemplate() }, randomSeed: 20260902, teamCount: 2),
            ModuleRegistry.CreateDefault(),
            grid);

        Assert.Equal(world.StateHash(), restored.StateHash());
        for (var tick = 0; tick < 100; tick++)
        {
            world.Tick();
            restored.Tick();
            Assert.Equal(world.StateHash(), restored.StateHash());
        }
    }

    [Fact]
    public void SnapshotShowsMovedGroundCoordinatesAndYaw()
    {
        var world = CreateWorld(PassabilityGrid.Uniform(64, 64));
        var initialHeading = Fixed64.FromRaw(13_493_037_705L);
        var unit = world.SpawnObject("unit", 0, Point(4, 5), headingRadians: initialHeading);
        SubmitMove(world, unit.Id, Point(30, 20));
        world.Advance(5);

        using var document = JsonDocument.Parse(SnapshotWriter.Write(world));
        var objects = document.RootElement.GetProperty("objects");
        Assert.NotEqual(4m, objects.GetProperty("x")[0].GetDecimal());
        Assert.NotEqual(5m, objects.GetProperty("z")[0].GetDecimal());
        Assert.NotEqual((decimal)initialHeading.Raw / Fixed64.OneRaw,
            objects.GetProperty("yaw")[0].GetDecimal());
        Assert.Equal(0m, objects.GetProperty("y")[0].GetDecimal());
    }

    [Fact]
    public void ThousandHordesOfTenMoveToTwentyGoalsForThreeHundredTicks()
    {
        var grid = PassabilityGrid.Uniform(160, 160);
        var world = CreateWorld(grid, speed: 70);
        const int hordeCount = 1_000;
        const int membersPerHorde = 10;
        for (var hordeIndex = 0; hordeIndex < hordeCount; hordeIndex++)
        {
            var centerX = 8 + hordeIndex % 40 * 3;
            var centerY = 8 + hordeIndex / 40 * 3;
            var members = new int[membersPerHorde];
            for (var memberIndex = 0; memberIndex < membersPerHorde; memberIndex++)
            {
                var offset = FormationOffset(membersPerHorde, memberIndex);
                members[memberIndex] = world.SpawnObject(
                    "unit",
                    0,
                    new FixedVector2(Fixed64.FromInt(centerX) + offset.X, Fixed64.FromInt(centerY) + offset.Y)).Id;
            }
            var hordeId = 100_000 + hordeIndex;
            world.AddHorde(new SnapshotHorde(hordeId, 0, 0, members, 0));
            var goalIndex = hordeIndex % 20;
            var goal = Point(125 + goalIndex % 5 * 5, 110 + goalIndex / 5 * 8);
            Assert.True(world.SubmitCommand(MoveCommand(1, 0, hordeIndex, hordeId, goal)));
        }

        var stopwatch = Stopwatch.StartNew();
        world.Advance(300);
        stopwatch.Stop();

        _output.WriteLine($"horde movement scale: 1,000 hordes x 10 members x 300 ticks = {stopwatch.ElapsedMilliseconds} ms");
        Assert.Equal(20, world.Movement.CachedFlowFieldCount);
        Assert.True(stopwatch.ElapsedMilliseconds >= 0);
    }

    private static SimWorld CreateWorld(PassabilityGrid grid, int speed = 55) =>
        new(
            new SimConfig(new[] { UnitTemplate(speed) }, randomSeed: 20260902, teamCount: 2),
            ModuleRegistry.CreateDefault(),
            tickMilliseconds: 33,
            grid);

    private static ObjectTemplate UnitTemplate(int speed = 55) => new(
        "unit",
        new[]
        {
            new ModuleSpec(LocomotorModule.TypeName, new Dictionary<string, long>
            {
                ["Speed"] = speed,
                ["SpeedDamaged"] = speed,
                ["TurnRate"] = 360,
                ["Acceleration"] = 5000,
                ["Braking"] = 5000,
                ["MinTurnSpeed"] = 0,
                ["MaxTurnWithoutReform"] = 45,
            }),
        });

    private static SimWorld CreateHordeWorld(int headingDegrees, out SnapshotHorde horde)
    {
        var world = CreateWorld(PassabilityGrid.Uniform(128, 128));
        var heading = Fixed64.FromInt(headingDegrees) * Fixed64.FromRaw(13_493_037_705L) / Fixed64.FromInt(180);
        var members = new int[10];
        for (var index = 0; index < members.Length; index++)
        {
            var offset = FormationOffset(members.Length, index);
            members[index] = world.SpawnObject(
                "unit",
                0,
                Point(20, 20) + offset,
                headingRadians: heading).Id;
        }
        horde = new SnapshotHorde(100, 0, 0, members, 0);
        world.AddHorde(horde);
        return world;
    }

    private static FixedVector2 FormationOffset(int memberCount, int memberIndex)
    {
        var columns = 1;
        while (columns * columns < memberCount) columns++;
        var rows = (memberCount + columns - 1) / columns;
        var column = memberIndex % columns;
        var row = memberIndex / columns;
        return new FixedVector2(
            Fixed64.FromInt(2) * Fixed64.FromFraction(2L * column - (columns - 1), 2),
            Fixed64.FromInt(2) * Fixed64.FromFraction(2L * row - (rows - 1), 2));
    }

    private static FixedVector2 AveragePosition(SimWorld world, IReadOnlyList<int> ids)
    {
        long x = 0;
        long y = 0;
        foreach (var id in ids)
        {
            x += world.Objects[id].Position.X.Raw;
            y += world.Objects[id].Position.Y.Raw;
        }
        return new FixedVector2(Fixed64.FromRaw(x / ids.Count), Fixed64.FromRaw(y / ids.Count));
    }

    private static int AdvanceUntilIdle(SimWorld world, GameObject unit, int maximum)
    {
        for (var ticks = 1; ticks <= maximum; ticks++)
        {
            world.Tick();
            if (!unit.FindModule<LocomotorModule>()!.HasOrder) return ticks;
        }
        return maximum + 1;
    }

    private static void SubmitMove(SimWorld world, int id, FixedVector2 destination) =>
        Assert.True(world.SubmitCommand(MoveCommand(1, 0, 0, id, destination)));

    private static SimCommand MoveCommand(
        int tick,
        int team,
        int seq,
        int id,
        FixedVector2 destination) => TestWorlds.Command(
            tick,
            team,
            seq,
            "move",
            ("id", CommandValue.OfLong(id)),
            ("x", CommandValue.OfFixed(destination.X)),
            ("y", CommandValue.OfFixed(destination.Y)));

    private static FixedVector2 Point(int x, int y) =>
        new(Fixed64.FromInt(x), Fixed64.FromInt(y));
}
