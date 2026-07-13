using System.Reflection;

namespace OpenBfme.Stage1;

public static class SelfTests
{
    private const double MinimumBenchmarkTicksPerSecond = 30.0;

    public static int Run(TextWriter output)
    {
        (string Name, Func<TestResult> Run)[] tests =
        [
            ("fixed_tick_commands", FixedTickAndCommands),
            ("stable_ids_slots_paths", StableIdsSlotsAndPaths),
            ("exact_replan_and_order_safety", ExactReplanAndOrderSafety),
            ("bounded_contacts_and_mirror", BoundedContactsAndMirror),
            ("combat_20v20", Combat20Vs20),
            ("obstacle_gap_detour", ObstacleGapDetour),
            ("repeat_hashes", RepeatHashes),
            ("fortress_victory", FortressVictory),
            ("runtime_independence", RuntimeIndependence),
            ("benchmark_50_hordes", Benchmark50Hordes),
        ];

        int passed = 0;
        int failed = 0;
        foreach ((string name, Func<TestResult> run) in tests)
        {
            TestResult result;
            try
            {
                result = run();
            }
            catch (Exception exception)
            {
                result = TestResult.Fail($"exception={Sanitize(exception.GetType().Name)}");
            }

            if (result.Passed)
            {
                passed++;
            }
            else
            {
                failed++;
            }

            output.WriteLine($"TEST name={name} status={(result.Passed ? "PASS" : "FAIL")} detail={Sanitize(result.Detail)}");
        }

        output.WriteLine($"SUMMARY command=self-test passed={passed} failed={failed} status={(failed == 0 ? "PASS" : "FAIL")}");
        return failed == 0 ? 0 : 1;
    }

    private static TestResult FixedTickAndCommands()
    {
        Simulation simulation = Scenarios.CreateArena();
        Horde horde = simulation.AddHorde(TeamId.Blue, Scenarios.Center(10, 31), 3);
        WorldPos initial = horde.Anchor;
        simulation.ScheduleCommand(new SimCommand(0, 1, horde.EntityId, OrderKind.Move, Scenarios.Center(20, 31)));
        simulation.ScheduleCommand(new SimCommand(5, 2, horde.EntityId, OrderKind.Stop, default));
        simulation.Advance(5);
        WorldPos beforeStop = horde.Anchor;
        simulation.AdvanceOneTick();
        WorldPos stopped = horde.Anchor;
        simulation.ScheduleCommand(new SimCommand(8, 3, horde.EntityId, OrderKind.Move, Scenarios.Center(24, 31)));
        simulation.Advance(2);
        bool stayedStopped = stopped == horde.Anchor;
        simulation.AdvanceOneTick();
        bool lateCommandMoved = stopped != horde.Anchor;

        bool passed = Simulation.TicksPerSecond == 30 && simulation.Tick == 9 && beforeStop != initial &&
            stayedStopped && lateCommandMoved && horde.Order == OrderKind.Move && horde.Path.Count > 0;
        return passed
            ? TestResult.Pass($"tick={simulation.Tick}_initial_move={beforeStop.X - initial.X}_late_command=1")
            : TestResult.Fail($"tick={simulation.Tick}_order={(int)horde.Order}_path={horde.Path.Count}");
    }

    private static TestResult StableIdsSlotsAndPaths()
    {
        Simulation simulation = Scenarios.CreateArena();
        Horde first = simulation.AddHorde(TeamId.Blue, Scenarios.Center(10, 31), 15);
        Horde second = simulation.AddHorde(TeamId.Blue, Scenarios.Center(12, 36), 15);
        simulation.ScheduleCommand(new SimCommand(0, 1, first.EntityId, OrderKind.Move, Scenarios.Center(50, 31)));
        simulation.AdvanceOneTick();

        int[] ids = simulation.Hordes.SelectMany(static horde => horde.Members).Select(static member => member.EntityId).ToArray();
        bool stableIds = ids.SequenceEqual(ids.OrderBy(static id => id)) && ids.Distinct().Count() == ids.Length;
        bool stableSlots = first.Members.Select(static member => member.FormationSlot).SequenceEqual(Enumerable.Range(0, 15));
        bool oneHordePath = first.Path.Count > 0 && second.Path.Count == 0;
        bool noMemberPathSurface = typeof(Member).GetProperty("Path", BindingFlags.Public | BindingFlags.Instance) is null;
        bool passed = stableIds && stableSlots && oneHordePath && noMemberPathSurface;
        return passed
            ? TestResult.Pass($"first_horde={first.EntityId}_first_member={ids[0]}_path_cells={first.Path.Count}")
            : TestResult.Fail($"ids={stableIds}_slots={stableSlots}_paths={oneHordePath}_member_path={noMemberPathSurface}");
    }

    private static TestResult ExactReplanAndOrderSafety()
    {
        NavigationGrid grid = new(20, 12);
        Simulation simulation = new(grid);
        Horde horde = simulation.AddHorde(TeamId.Blue, NavigationGrid.CellCenter(new GridCell(2, 5)), 15);
        WorldPos exact = new(horde.Anchor.X + 123, horde.Anchor.Y + 77);
        simulation.ScheduleCommand(new SimCommand(0, 0, horde.EntityId, OrderKind.Move, exact));
        simulation.Advance(4);
        bool exactSameCell = horde.Anchor == exact;

        WorldPos far = new(NavigationGrid.CellCenter(new GridCell(16, 5)).X + 137, NavigationGrid.CellCenter(new GridCell(16, 5)).Y - 91);
        simulation.ScheduleCommand(new SimCommand(simulation.Tick, 1, horde.EntityId, OrderKind.Move, far));
        simulation.Advance(3);
        int originalRevision = horde.PathRevision;
        grid.SetBlocked(new GridCell(8, 5));
        simulation.Advance(160);
        bool replanned = horde.PathRevision > originalRevision && horde.Anchor == far;

        Horde ally = simulation.AddHorde(TeamId.Blue, NavigationGrid.CellCenter(new GridCell(4, 8)), 3);
        bool friendlyRejected = false;
        try
        {
            simulation.ScheduleCommand(new SimCommand(simulation.Tick, 2, horde.EntityId, OrderKind.AttackTarget, default, ally.EntityId));
        }
        catch (ArgumentException)
        {
            friendlyRejected = true;
        }

        bool passed = exactSameCell && replanned && friendlyRejected && simulation.ValidateState(out _);
        return passed
            ? TestResult.Pass($"exact=1_replanned={horde.PathRevision}_friendly_rejected=1")
            : TestResult.Fail($"exact={exactSameCell}_replanned={replanned}_friendly={friendlyRejected}_anchor={horde.Anchor.X}:{horde.Anchor.Y}");
    }

    private static TestResult BoundedContactsAndMirror()
    {
        Simulation contacts = new(new NavigationGrid(16, 12));
        Horde blue = contacts.AddHorde(TeamId.Blue, NavigationGrid.CellCenter(new GridCell(5, 6)), 15, rangedEvery: 5);
        Horde red = contacts.AddHorde(TeamId.Red, NavigationGrid.CellCenter(new GridCell(6, 6)), 1);
        contacts.ScheduleCommand(new SimCommand(0, 0, blue.EntityId, OrderKind.AttackTarget, red.Anchor, red.EntityId));
        contacts.AdvanceOneTick();
        int contactCount = blue.Members.Count(member => !member.IsRanged && member.TargetEntityId == red.Members[0].EntityId);

        Simulation first = CreateMirrorScenario(reverseTeams: false);
        Simulation second = CreateMirrorScenario(reverseTeams: true);
        first.Advance(600);
        second.Advance(600);
        int firstBlue = first.Hordes.Where(static horde => horde.Team == TeamId.Blue).Sum(static horde => horde.AliveCount);
        int firstRed = first.Hordes.Where(static horde => horde.Team == TeamId.Red).Sum(static horde => horde.AliveCount);
        int secondBlue = second.Hordes.Where(static horde => horde.Team == TeamId.Blue).Sum(static horde => horde.AliveCount);
        int secondRed = second.Hordes.Where(static horde => horde.Team == TeamId.Red).Sum(static horde => horde.AliveCount);
        bool mirrored = firstBlue == secondRed && firstRed == secondBlue;
        bool passed = contactCount is > 0 and <= 4 && mirrored;
        return passed
            ? TestResult.Pass($"contacts={contactCount}_first={firstBlue}:{firstRed}_second={secondBlue}:{secondRed}")
            : TestResult.Fail($"contacts={contactCount}_mirrored={mirrored}_first={firstBlue}:{firstRed}_second={secondBlue}:{secondRed}");
    }

    private static Simulation CreateMirrorScenario(bool reverseTeams)
    {
        Simulation simulation = new(new NavigationGrid(24, 16));
        TeamId leftTeam = reverseTeams ? TeamId.Red : TeamId.Blue;
        TeamId rightTeam = reverseTeams ? TeamId.Blue : TeamId.Red;
        Horde left = simulation.AddHorde(leftTeam, NavigationGrid.CellCenter(new GridCell(6, 8)), 20, rangedEvery: 5);
        Horde right = simulation.AddHorde(rightTeam, NavigationGrid.CellCenter(new GridCell(18, 8)), 20, rangedEvery: 5);
        simulation.ScheduleCommand(new SimCommand(0, 0, left.EntityId, OrderKind.AttackMove, right.Anchor));
        simulation.ScheduleCommand(new SimCommand(0, 1, right.EntityId, OrderKind.AttackMove, left.Anchor));
        return simulation;
    }

    private static TestResult Combat20Vs20()
    {
        Simulation simulation = Scenarios.CreateCombat20Vs20();
        bool projectileObserved = false;
        int resolvedTick = -1;
        for (int tick = 0; tick < 1_200; tick++)
        {
            simulation.AdvanceOneTick();
            projectileObserved |= simulation.Projectiles.Count > 0;
            if (!simulation.ValidateState(out string failure))
            {
                return TestResult.Fail($"invalid_tick={tick}_{failure}");
            }

            int blueAlive = simulation.Hordes.Where(static horde => horde.Team == TeamId.Blue).Sum(static horde => horde.AliveCount);
            int redAlive = simulation.Hordes.Where(static horde => horde.Team == TeamId.Red).Sum(static horde => horde.AliveCount);
            if (blueAlive == 0 || redAlive == 0)
            {
                resolvedTick = simulation.Tick;
                break;
            }
        }

        int blueFinal = simulation.Hordes.Where(static horde => horde.Team == TeamId.Blue).Sum(static horde => horde.AliveCount);
        int redFinal = simulation.Hordes.Where(static horde => horde.Team == TeamId.Red).Sum(static horde => horde.AliveCount);
        bool healthChanged = simulation.Hordes.SelectMany(static horde => horde.Members).Any(static member => member.Health < member.MaximumHealth);
        bool passed = resolvedTick > 0 && projectileObserved && healthChanged;
        return passed
            ? TestResult.Pass($"resolved_tick={resolvedTick}_blue_alive={blueFinal}_red_alive={redFinal}_projectile=1_hash={simulation.StateHash():X8}")
            : TestResult.Fail($"resolved_tick={resolvedTick}_blue_alive={blueFinal}_red_alive={redFinal}_projectile={(projectileObserved ? 1 : 0)}");
    }

    private static TestResult ObstacleGapDetour()
    {
        Simulation simulation = Scenarios.CreateArena();
        Horde horde = simulation.AddHorde(TeamId.Blue, Scenarios.Center(10, 10), 15);
        simulation.ScheduleCommand(new SimCommand(0, 1, horde.EntityId, OrderKind.Move, Scenarios.Center(54, 10)));
        simulation.AdvanceOneTick();

        GridCell? crossing = horde.Path.Cast<GridCell?>().FirstOrDefault(cell => cell?.X == Scenarios.WallX);
        if (crossing is null || crossing.Value.Y < Scenarios.GapMinimumY || crossing.Value.Y > Scenarios.GapMaximumY)
        {
            return TestResult.Fail("path_did_not_use_gap");
        }

        bool stayedWalkable = true;
        int maximumAnchorY = horde.Anchor.Y;
        for (int tick = 0; tick < 600 && horde.PathIndex < horde.Path.Count; tick++)
        {
            simulation.AdvanceOneTick();
            maximumAnchorY = Math.Max(maximumAnchorY, horde.Anchor.Y);
            stayedWalkable &= simulation.Navigation.IsWalkable(horde.Anchor);
            stayedWalkable &= horde.Members.Where(static member => member.IsAlive).All(member => simulation.Navigation.IsWalkable(member.Position));
        }

        bool crossed = simulation.Navigation.ToCell(horde.Anchor).X > Scenarios.WallX;
        // Let the real members finish catching up after the anchor consumes its path.
        simulation.Advance(60);
        bool membersCrossed = horde.Members
            .Where(static member => member.IsAlive)
            .All(member => simulation.Navigation.ToCell(member.Position).X > Scenarios.WallX);
        bool detoured = maximumAnchorY / NavigationGrid.CellSize >= Scenarios.GapMinimumY;
        bool passed = stayedWalkable && crossed && membersCrossed && detoured;
        return passed
            ? TestResult.Pass($"crossing_y={crossing.Value.Y}_max_y={maximumAnchorY}_path_cells={horde.Path.Count}_members_crossed=1")
            : TestResult.Fail($"walkable={stayedWalkable}_crossed={crossed}_members_crossed={membersCrossed}_detoured={detoured}_path_index={horde.PathIndex}");
    }

    private static TestResult RepeatHashes()
    {
        uint[] first = RunReplayHashes(360);
        uint[] second = RunReplayHashes(360);
        bool equal = first.AsSpan().SequenceEqual(second);
        int mismatch = -1;
        if (!equal)
        {
            for (int index = 0; index < first.Length; index++)
            {
                if (first[index] != second[index])
                {
                    mismatch = index;
                    break;
                }
            }
        }

        return equal
            ? TestResult.Pass($"ticks={first.Length}_final_hash={first[^1]:X8}")
            : TestResult.Fail($"mismatch={mismatch}");
    }

    private static uint[] RunReplayHashes(int ticks)
    {
        Simulation simulation = Scenarios.CreateReplayScenario();
        uint[] hashes = new uint[ticks];
        for (int index = 0; index < ticks; index++)
        {
            simulation.AdvanceOneTick();
            hashes[index] = simulation.StateHash();
        }

        return hashes;
    }

    private static TestResult FortressVictory()
    {
        Simulation simulation = Scenarios.CreateFortressVictoryScenario();
        int victoryTick = -1;
        for (int tick = 0; tick < 500; tick++)
        {
            simulation.AdvanceOneTick();
            if (simulation.Winner == TeamId.Blue)
            {
                victoryTick = simulation.Tick;
                break;
            }
        }

        Fortress red = simulation.Fortresses.Single(static fortress => fortress.Team == TeamId.Red);
        bool passed = victoryTick > 0 && red.Health == 0;
        return passed
            ? TestResult.Pass($"winner={(int)simulation.Winner}_tick={victoryTick}_red_hp={red.Health}")
            : TestResult.Fail($"winner={(int)simulation.Winner}_tick={simulation.Tick}_red_hp={red.Health}");
    }

    private static TestResult RuntimeIndependence()
    {
        string[] bannedTokens = ["godot", "bfme2", "opensage", "w3d", "big"];
        string[] references = typeof(Simulation).Assembly.GetReferencedAssemblies().Select(static name => name.Name ?? string.Empty).ToArray();
        string? forbiddenReference = references.FirstOrDefault(reference =>
            bannedTokens.Any(token => reference.Contains(token, StringComparison.OrdinalIgnoreCase)));
        bool noPackages = references.All(reference => reference.StartsWith("System", StringComparison.Ordinal) || reference == "mscorlib");
        Simulation simulation = Scenarios.CreateArena();
        simulation.AdvanceOneTick();
        bool passed = forbiddenReference is null && noPackages && simulation.ValidateState(out _);
        return passed
            ? TestResult.Pass($"assembly_refs={references.Length}_external_refs=0_hash={simulation.StateHash():X8}")
            : TestResult.Fail($"forbidden={forbiddenReference ?? "non_system_reference"}");
    }

    private static TestResult Benchmark50Hordes()
    {
        BenchmarkResult result = Scenarios.RunBenchmark();
        bool passed = result.StateValid && result.Hordes == 50 && result.Members == 750 &&
            result.TicksPerSecond >= MinimumBenchmarkTicksPerSecond;
        string detail = FormattableString.Invariant(
            $"hordes={result.Hordes}_members={result.Members}_ticks={result.Ticks}_elapsed_ms={result.ElapsedMilliseconds:F3}_ticks_per_second={result.TicksPerSecond:F2}_minimum=30_hash={result.Hash:X8}");
        return passed ? TestResult.Pass(detail) : TestResult.Fail(detail);
    }

    private static string Sanitize(string value) => value.Replace(' ', '_').Replace('\r', '_').Replace('\n', '_');

    private readonly record struct TestResult(bool Passed, string Detail)
    {
        public static TestResult Pass(string detail) => new(true, detail);
        public static TestResult Fail(string detail) => new(false, detail);
    }
}
