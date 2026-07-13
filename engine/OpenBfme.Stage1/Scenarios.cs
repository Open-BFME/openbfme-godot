using System.Diagnostics;

namespace OpenBfme.Stage1;

public static class Scenarios
{
    public const int WallX = 32;
    public const int GapMinimumY = 27;
    public const int GapMaximumY = 35;

    public static Simulation CreateArena(int fortressHealth = 5_000)
    {
        NavigationGrid grid = CreateObstacleGrid();
        Simulation simulation = new(grid);
        simulation.AddFortress(TeamId.Blue, Center(4, 31), fortressHealth);
        simulation.AddFortress(TeamId.Red, Center(59, 31), fortressHealth);
        return simulation;
    }

    public static NavigationGrid CreateObstacleGrid()
    {
        NavigationGrid grid = new(64, 64);
        for (int y = 0; y < grid.Height; y++)
        {
            if (y < GapMinimumY || y > GapMaximumY)
            {
                grid.SetBlocked(new GridCell(WallX, y));
            }
        }

        return grid;
    }

    public static Simulation CreateCombat20Vs20()
    {
        Simulation simulation = CreateArena();
        Horde blue = simulation.AddHorde(TeamId.Blue, Center(14, 31), 20, rangedEvery: 5);
        Horde red = simulation.AddHorde(TeamId.Red, Center(50, 31), 20, rangedEvery: 5);
        simulation.ScheduleCommand(new SimCommand(0, 1, blue.EntityId, OrderKind.AttackMove, red.Anchor));
        simulation.ScheduleCommand(new SimCommand(0, 2, red.EntityId, OrderKind.AttackMove, blue.Anchor));
        return simulation;
    }

    public static Simulation CreateReplayScenario()
    {
        Simulation simulation = CreateArena(fortressHealth: 800);
        Horde blue = simulation.AddHorde(TeamId.Blue, Center(12, 30), 15, rangedEvery: 3);
        Horde red = simulation.AddHorde(TeamId.Red, Center(49, 32), 15, rangedEvery: 4);
        simulation.ScheduleCommand(new SimCommand(0, 10, blue.EntityId, OrderKind.Move, Center(20, 24)));
        simulation.ScheduleCommand(new SimCommand(0, 11, red.EntityId, OrderKind.Move, Center(44, 39)));
        simulation.ScheduleCommand(new SimCommand(25, 12, blue.EntityId, OrderKind.Stop, default));
        simulation.ScheduleCommand(new SimCommand(30, 13, blue.EntityId, OrderKind.AttackMove, Center(50, 31)));
        simulation.ScheduleCommand(new SimCommand(30, 14, red.EntityId, OrderKind.AttackMove, Center(14, 31)));
        simulation.ScheduleCommand(new SimCommand(180, 15, blue.EntityId, OrderKind.AttackTarget, default, 2));
        return simulation;
    }

    public static Simulation CreateFortressVictoryScenario()
    {
        Simulation simulation = CreateArena(fortressHealth: 420);
        Horde blue = simulation.AddHorde(TeamId.Blue, Center(53, 31), 15, rangedEvery: 3);
        simulation.ScheduleCommand(new SimCommand(0, 1, blue.EntityId, OrderKind.AttackTarget, default, 2));
        return simulation;
    }

    public static Simulation CreateFiftyHordeBenchmark()
    {
        NavigationGrid grid = new(128, 96);
        for (int y = 0; y < grid.Height; y++)
        {
            if (y < 8 || y > 87)
            {
                grid.SetBlocked(new GridCell(64, y));
            }
        }

        Simulation simulation = new(grid);
        simulation.AddFortress(TeamId.Blue, Center(4, 48), 20_000);
        simulation.AddFortress(TeamId.Red, Center(123, 48), 20_000);

        int sequence = 1;
        for (int index = 0; index < 25; index++)
        {
            int row = index / 5;
            int column = index % 5;
            WorldPos blueStart = Center(10 + (column * 3), 14 + (row * 16));
            WorldPos redStart = Center(117 - (column * 3), 14 + (row * 16));
            Horde blue = simulation.AddHorde(TeamId.Blue, blueStart, 15, rangedEvery: 5);
            Horde red = simulation.AddHorde(TeamId.Red, redStart, 15, rangedEvery: 5);
            simulation.ScheduleCommand(new SimCommand(0, sequence++, blue.EntityId, OrderKind.AttackMove, redStart));
            simulation.ScheduleCommand(new SimCommand(0, sequence++, red.EntityId, OrderKind.AttackMove, blueStart));
        }

        return simulation;
    }

    public static BenchmarkResult RunBenchmark(int ticks = 300)
    {
        // Warm the JIT outside the measurement.
        Simulation warmup = CreateFiftyHordeBenchmark();
        warmup.Advance(3);

        Simulation simulation = CreateFiftyHordeBenchmark();
        Stopwatch stopwatch = Stopwatch.StartNew();
        simulation.Advance(ticks);
        stopwatch.Stop();
        double ticksPerSecond = ticks / stopwatch.Elapsed.TotalSeconds;
        int members = simulation.Hordes.Sum(static horde => horde.Members.Count);
        return new BenchmarkResult(
            ticks,
            simulation.Hordes.Count,
            members,
            stopwatch.Elapsed.TotalMilliseconds,
            ticksPerSecond,
            simulation.StateHash(),
            simulation.ValidateState(out _));
    }

    public static WorldPos Center(int x, int y) => NavigationGrid.CellCenter(new GridCell(x, y));
}

public readonly record struct BenchmarkResult(
    int Ticks,
    int Hordes,
    int Members,
    double ElapsedMilliseconds,
    double TicksPerSecond,
    uint Hash,
    bool StateValid);
