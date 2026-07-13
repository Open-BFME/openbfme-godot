using System.Text.Json;

namespace OpenBfme.Stage1;

public static class Program
{
    public static int Main(string[] args)
    {
        string command = args.Length == 0 ? "self-test" : args[0].ToLowerInvariant();
        return command switch
        {
            "self-test" or "test" => SelfTests.Run(Console.Out),
            "stage2-self-test" => Stage2SelfTests.Run(Console.Out),
            "benchmark" => RunBenchmark(args),
            "bundle-test" => RunBundleTest(args),
            "stage2-bundle-test" => RunStage2BundleTest(args),
            "scenario" => RunScenario(args),
            "help" or "--help" or "-h" => ShowHelp(),
            _ => UnknownCommand(command),
        };
    }

    private static int RunBenchmark(string[] args)
    {
        int ticks = 300;
        if (args.Length > 1 && (!int.TryParse(args[1], out ticks) || ticks <= 0))
        {
            Console.Error.WriteLine("ERROR code=invalid_ticks");
            return 2;
        }

        BenchmarkResult result = Scenarios.RunBenchmark(ticks);
        bool passed = result.StateValid && result.Hordes == 50 && result.Members == 750 && result.TicksPerSecond >= 30.0;
        Console.WriteLine(FormattableString.Invariant(
            $"BENCHMARK scenario=50-hordes hordes={result.Hordes} members={result.Members} ticks={result.Ticks} elapsed_ms={result.ElapsedMilliseconds:F3} ticks_per_second={result.TicksPerSecond:F2} minimum=30 state_valid={(result.StateValid ? 1 : 0)} hash={result.Hash:X8} status={(passed ? "PASS" : "FAIL")}"));
        return passed ? 0 : 1;
    }

    private static int RunScenario(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("ERROR code=missing_scenario choices=combat20,obstacle,victory,replay");
            return 2;
        }

        string name = args[1].ToLowerInvariant();
        Simulation? simulation = name switch
        {
            "combat20" => Scenarios.CreateCombat20Vs20(),
            "victory" => Scenarios.CreateFortressVictoryScenario(),
            "replay" => Scenarios.CreateReplayScenario(),
            "obstacle" => CreateObstacleScenario(),
            _ => null,
        };
        if (simulation is null)
        {
            Console.Error.WriteLine($"ERROR code=unknown_scenario name={name}");
            return 2;
        }

        int ticks = 360;
        if (args.Length > 2 && (!int.TryParse(args[2], out ticks) || ticks <= 0))
        {
            Console.Error.WriteLine("ERROR code=invalid_ticks");
            return 2;
        }

        simulation.Advance(ticks);
        bool valid = simulation.ValidateState(out string failure);
        int blueAlive = simulation.Hordes.Where(static horde => horde.Team == TeamId.Blue).Sum(static horde => horde.AliveCount);
        int redAlive = simulation.Hordes.Where(static horde => horde.Team == TeamId.Red).Sum(static horde => horde.AliveCount);
        Console.WriteLine(
            $"SCENARIO name={name} ticks={simulation.Tick} blue_alive={blueAlive} red_alive={redAlive} winner={(int)simulation.Winner} state_valid={(valid ? 1 : 0)} failure={failure} hash={simulation.StateHash():X8} status={(valid ? "PASS" : "FAIL")}");
        return valid ? 0 : 1;
    }

    private static int RunBundleTest(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("ERROR code=missing_bundle_path");
            return 2;
        }

        try
        {
            BundleResult result = BundleScenario.Run(args[1]);
            bool passed = result.StateValid && result.Hordes == 2 && result.Members == 30 && result.Winner == TeamId.Blue && result.RedFortressHealth == 0;
            Console.WriteLine($"BUNDLE_CSHARP ticks={result.Ticks} hordes={result.Hordes} members={result.Members} living={result.LivingMembers} winner={(int)result.Winner} blue_fortress_hp={result.BlueFortressHealth} red_fortress_hp={result.RedFortressHealth} state_valid={(result.StateValid ? 1 : 0)} failure={result.Failure} hash={result.Hash:X8} status={(passed ? "PASS" : "FAIL")}");
            return passed ? 0 : 1;
        }
        catch (Exception exception) when (exception is IOException or JsonException or InvalidDataException or InvalidOperationException)
        {
            Console.Error.WriteLine($"ERROR code=bundle_contract detail={exception.Message}");
            return 1;
        }
    }

    private static int RunStage2BundleTest(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("ERROR code=missing_bundle_path");
            return 2;
        }

        try
        {
            Stage2BundleResult result = Stage2BundleScenario.Run(args[1]);
            bool passed = result.StateValid && result.Buildings == 3 && result.CompletedBuildings == 3 &&
                result.Hordes == 7 && result.BlueTotalEarned == 1_600 && result.RedTotalEarned == 0 &&
                result.BluePopulationReserved == 0 && result.Winner == TeamId.Blue &&
                result.RedFortressHealth == 0;
            Console.WriteLine($"BUNDLE_STAGE2_CSHARP ticks={result.Ticks} buildings={result.Buildings} completed_buildings={result.CompletedBuildings} hordes={result.Hordes} living={result.LivingMembers} blue_resources={result.BlueResources} red_resources={result.RedResources} blue_total_earned={result.BlueTotalEarned} red_total_earned={result.RedTotalEarned} blue_population_used={result.BluePopulationUsed} blue_population_reserved={result.BluePopulationReserved} winner={(int)result.Winner} red_fortress_hp={result.RedFortressHealth} state_valid={(result.StateValid ? 1 : 0)} failure={result.Failure} hash={result.Hash:X8} status={(passed ? "PASS" : "FAIL")}");
            return passed ? 0 : 1;
        }
        catch (Exception exception) when (exception is IOException or JsonException or InvalidDataException or InvalidOperationException or ArgumentException)
        {
            Console.Error.WriteLine($"ERROR code=stage2_bundle_contract detail={exception.Message}");
            return 1;
        }
    }

    private static Simulation CreateObstacleScenario()
    {
        Simulation simulation = Scenarios.CreateArena();
        Horde horde = simulation.AddHorde(TeamId.Blue, Scenarios.Center(10, 10), 15);
        simulation.ScheduleCommand(new SimCommand(0, 1, horde.EntityId, OrderKind.Move, Scenarios.Center(54, 10)));
        return simulation;
    }

    private static int ShowHelp()
    {
        Console.WriteLine("USAGE command=self-test|stage2-self-test|benchmark [ticks]|bundle-test <bundle-path>|stage2-bundle-test <bundle-path>|scenario <combat20|obstacle|victory|replay> [ticks]");
        return 0;
    }

    private static int UnknownCommand(string command)
    {
        Console.Error.WriteLine($"ERROR code=unknown_command command={command}");
        return 2;
    }
}
