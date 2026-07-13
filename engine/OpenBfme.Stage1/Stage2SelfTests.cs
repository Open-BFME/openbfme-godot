namespace OpenBfme.Stage1;

public static class Stage2SelfTests
{
    public static int Run(TextWriter output)
    {
        (string Name, Func<TestResult> Run)[] tests =
        [
            ("transactional_placement_blocking", TransactionalPlacementBlocking),
            ("buildable_zero_construction_rejected", BuildableZeroConstructionRejected),
            ("construction_health_damage", ConstructionHealthDamage),
            ("farm_efficiency_exact_income", FarmEfficiencyExactIncome),
            ("fifo_debit_population", FifoDebitPopulation),
            ("destroyed_producer_releases_queue", DestroyedProducerReleasesQueue),
            ("deterministic_spawn_rally_replay", DeterministicSpawnRallyReplay),
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
                result = TestResult.Fail($"exception={exception.GetType().Name}:{exception.Message}");
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

        output.WriteLine($"SUMMARY command=stage2-self-test passed={passed} failed={failed} status={(failed == 0 ? "PASS" : "FAIL")}");
        return failed == 0 ? 0 : 1;
    }

    private static TestResult TransactionalPlacementBlocking()
    {
        Simulation simulation = CreateSimulation();
        EconomySystem economy = simulation.Economy!;
        TeamEconomy blue = economy.FindEconomy(TeamId.Blue)!;
        simulation.AddFortress(TeamId.Blue, Center(4, 4));
        uint before = simulation.StateHash();
        bool offCenter = simulation.TryPlaceBuilding(TeamId.Blue, 2, new WorldPos(10_000, 10_000), out _);
        bool unchanged = before == simulation.StateHash() && blue.Resources == 1_600 && economy.NextBuildingId == 200;

        simulation.Navigation.SetBlocked(new GridCell(10, 10));
        int resourcesBeforeBlocked = blue.Resources;
        bool staticBlocked = simulation.TryPlaceBuilding(TeamId.Blue, 2, Center(10, 10), out _);
        bool blockedUnchanged = !staticBlocked && blue.Resources == resourcesBeforeBlocked && economy.NextBuildingId == 200;
        simulation.Navigation.SetBlocked(new GridCell(10, 10), false);

        uint beforeFortressOverlap = simulation.StateHash();
        bool fortressEdgeOverlap = simulation.TryPlaceBuilding(TeamId.Blue, 2, Center(6, 4), out _);
        bool fortressOverlapTransactional = !fortressEdgeOverlap && simulation.StateHash() == beforeFortressOverlap &&
            blue.Resources == 1_600 && economy.NextBuildingId == 200 && economy.Buildings.Count == 0 &&
            Enumerable.Range(3, 3).SelectMany(y => Enumerable.Range(5, 3).Select(x => new GridCell(x, y)))
                .All(cell => simulation.Navigation.DynamicBlockOwner(cell) == 0);

        bool placed = simulation.TryPlaceBuilding(TeamId.Blue, 2, Center(10, 10), out EconomyBuilding? building);
        bool immediateBlocker = placed && building is not null && building.EntityId == 200 && building.Health == 1 &&
            simulation.Navigation.DynamicBlockOwner(new GridCell(9, 9)) == 200 &&
            simulation.Navigation.DynamicBlockOwner(new GridCell(11, 11)) == 200;
        uint beforeBuildingOverlap = simulation.StateHash();
        bool overlapRejected = !simulation.TryPlaceBuilding(TeamId.Blue, 2, Center(12, 10), out _) &&
            blue.Resources == 1_300 && economy.NextBuildingId == 201 && simulation.StateHash() == beforeBuildingOverlap;
        bool passed = !offCenter && unchanged && blockedUnchanged && fortressOverlapTransactional && immediateBlocker &&
            overlapRejected && simulation.ValidateState(out _);
        return passed
            ? TestResult.Pass($"building={building!.EntityId}_resources={blue.Resources}_footprint=9_fortress_edge_transactional=1")
            : TestResult.Fail($"off_center={offCenter}_unchanged={unchanged}_blocked={blockedUnchanged}_fortress_edge={fortressOverlapTransactional}_placed={placed}_immediate={immediateBlocker}_overlap={overlapRejected}");
    }

    private static TestResult BuildableZeroConstructionRejected()
    {
        EconomyDefinition valid = CreateDefinition();
        EconomyDefinition invalid = valid with
        {
            Buildings = valid.Buildings
                .Select(static building => building.TypeCode == 2 ? building with { ConstructionTicks = 0 } : building)
                .ToArray(),
        };
        bool rejected = false;
        try
        {
            Simulation invalidSimulation = new(new NavigationGrid(32, 32));
            invalidSimulation.EnableEconomy(invalid);
        }
        catch (InvalidDataException exception)
        {
            rejected = exception.Message == "bundle_contract=buildable_construction_ticks";
        }

        Simulation validSimulation = new(new NavigationGrid(32, 32));
        validSimulation.EnableEconomy(valid);
        bool fortressZeroAccepted = validSimulation.Economy!.FindBuildingDefinition(1)?.ConstructionTicks == 0;
        return rejected && fortressZeroAccepted
            ? TestResult.Pass("buildable_zero=rejected_fortress_zero=accepted")
            : TestResult.Fail($"rejected={rejected}_fortress_zero={fortressZeroAccepted}");
    }

    private static TestResult ConstructionHealthDamage()
    {
        Simulation simulation = CreateSimulation();
        simulation.TryPlaceBuilding(TeamId.Blue, 2, Center(10, 10), out EconomyBuilding? building);
        if (building is null || building.Health != 1 || building.ConstructionHealthCap != 1)
        {
            return TestResult.Fail("initial_ramp_not_one");
        }

        simulation.Advance(10);
        bool firstRamp = building.ConstructionProgressTicks == 10 && building.ConstructionHealthCap == 200 && building.Health == 200;
        simulation.DamageBuilding(building.EntityId, 50);
        simulation.Advance(10);
        bool damagePreserved = building.ConstructionHealthCap == 400 && building.Health == 350;
        simulation.Advance(40);
        bool completed = building.IsCompleted && building.ConstructionProgressTicks == 60 && building.ConstructionHealthCap == 1_200 && building.Health == 1_150;
        int resourcesBeforeDestroy = simulation.Economy!.FindEconomy(TeamId.Blue)!.Resources;
        simulation.DamageBuilding(building.EntityId, 2_000);
        bool destroyed = building.IsDestroyed && simulation.Navigation.DynamicBlockOwner(new GridCell(10, 10)) == 0 &&
            simulation.Economy.FindEconomy(TeamId.Blue)!.Resources == resourcesBeforeDestroy;
        bool passed = firstRamp && damagePreserved && completed && destroyed && simulation.ValidateState(out _);
        return passed
            ? TestResult.Pass("ramp=200:400:1200_damage_deficit=50_destroy_unblocked=1")
            : TestResult.Fail($"ramp={firstRamp}_damage={damagePreserved}_complete={completed}_destroy={destroyed}");
    }

    private static TestResult FarmEfficiencyExactIncome()
    {
        Simulation simulation = CreateSimulation();
        TeamEconomy blue = simulation.Economy!.FindEconomy(TeamId.Blue)!;
        simulation.TryPlaceBuilding(TeamId.Blue, 2, Center(10, 10), out EconomyBuilding? first);
        simulation.TryPlaceBuilding(TeamId.Blue, 2, Center(18, 10), out EconomyBuilding? second);
        if (first is null || second is null)
        {
            return TestResult.Fail("farm_placement");
        }

        simulation.Advance(60);
        bool completed = first.IsCompleted && second.IsCompleted && first.NextIncomeTick == 149 && second.NextIncomeTick == 149;
        bool clustered = simulation.Economy.FarmEfficiencyPermille(first) == 800 && simulation.Economy.FarmEfficiencyPermille(second) == 800;
        int afterCosts = blue.Resources;
        simulation.Advance(89);
        bool noEarlyIncome = blue.Resources == afterCosts && simulation.Tick == 149;
        simulation.AdvanceOneTick();
        bool exactPayout = blue.Resources == afterCosts + 160 && blue.TotalEarned == 160;
        simulation.DamageBuilding(second.EntityId, 2_000);
        simulation.Advance(89);
        bool noEarlySolo = blue.Resources == afterCosts + 160;
        simulation.AdvanceOneTick();
        bool soloPayout = blue.Resources == afterCosts + 260 && blue.TotalEarned == 260 &&
            simulation.Economy.FarmEfficiencyPermille(first) == 1_000;
        bool passed = completed && clustered && noEarlyIncome && exactPayout && noEarlySolo && soloPayout && simulation.ValidateState(out _);
        return passed
            ? TestResult.Pass($"first_due=149_cluster_payout=160_solo_payout=100_resources={blue.Resources}_total_earned={blue.TotalEarned}")
            : TestResult.Fail($"complete={completed}_cluster={clustered}_no_early={noEarlyIncome}_payout={exactPayout}_solo_early={noEarlySolo}_solo={soloPayout}_resources={blue.Resources}_total_earned={blue.TotalEarned}");
    }

    private static TestResult FifoDebitPopulation()
    {
        Simulation simulation = CreateSimulation();
        TeamEconomy blue = simulation.Economy!.FindEconomy(TeamId.Blue)!;
        simulation.TryPlaceBuilding(TeamId.Blue, 3, Center(10, 10), out EconomyBuilding? producer);
        simulation.Advance(90);
        if (producer is null || !producer.IsCompleted)
        {
            return TestResult.Fail("producer_not_completed");
        }

        int[] jobs = new int[5];
        bool queued = true;
        for (int index = 0; index < jobs.Length; index++)
        {
            queued &= simulation.TryTrain(producer.EntityId, 100, out jobs[index]);
        }

        bool immediateDebitReserve = queued && jobs.SequenceEqual([1, 2, 3, 4, 5]) &&
            blue.Resources == 200 && blue.PopulationReserved == 5 && blue.PopulationUsed == 0;
        bool sixthRejected = !simulation.TryTrain(producer.EntityId, 100, out _) && blue.Resources == 200 && blue.PopulationReserved == 5;
        simulation.Advance(90);
        bool fifoSpawn = simulation.Hordes.Count == 1 && simulation.Hordes[0].EntityId == 100 &&
            producer.Jobs.Select(static job => job.JobId).SequenceEqual([2, 3, 4, 5]) &&
            blue.PopulationUsed == 1 && blue.PopulationReserved == 4;
        bool passed = immediateDebitReserve && sixthRejected && fifoSpawn && simulation.ValidateState(out _);
        return passed
            ? TestResult.Pass($"jobs=1:2:3:4:5_spawn={simulation.Hordes[0].EntityId}_used={blue.PopulationUsed}_reserved={blue.PopulationReserved}")
            : TestResult.Fail($"queued={immediateDebitReserve}_sixth={sixthRejected}_fifo={fifoSpawn}_resources={blue.Resources}_used={blue.PopulationUsed}_reserved={blue.PopulationReserved}");
    }

    private static TestResult DestroyedProducerReleasesQueue()
    {
        Simulation simulation = CreateSimulation();
        TeamEconomy blue = simulation.Economy!.FindEconomy(TeamId.Blue)!;
        simulation.TryPlaceBuilding(TeamId.Blue, 3, Center(10, 10), out EconomyBuilding? producer);
        simulation.Advance(90);
        simulation.TryTrain(producer!.EntityId, 100, out _);
        simulation.TryTrain(producer.EntityId, 100, out _);
        int debitedResources = blue.Resources;
        simulation.DamageBuilding(producer.EntityId, 4_000);
        bool passed = producer.IsDestroyed && producer.Jobs.Count == 0 && blue.PopulationReserved == 0 &&
            blue.Resources == debitedResources && simulation.Navigation.DynamicBlockOwner(new GridCell(10, 10)) == 0 &&
            simulation.ValidateState(out _);
        return passed
            ? TestResult.Pass($"resources={blue.Resources}_reserved={blue.PopulationReserved}_block_owner=0")
            : TestResult.Fail($"destroyed={producer.IsDestroyed}_jobs={producer.Jobs.Count}_resources={blue.Resources}_reserved={blue.PopulationReserved}");
    }

    private static TestResult DeterministicSpawnRallyReplay()
    {
        (Simulation First, Horde Horde) first = RunRallyScenario();
        (Simulation First, Horde Horde) second = RunRallyScenario();
        WorldPos expectedSpawn = Center(10, 8);
        WorldPos expectedRallyFallback = Center(20, 19);
        bool hashesEqual = first.First.StateHash() == second.First.StateHash();
        bool deterministicSpawn = first.Horde.EntityId == 100 && second.Horde.EntityId == 100 &&
            first.Horde.Anchor != expectedSpawn && first.Horde.Destination == expectedRallyFallback &&
            first.Horde.Order == OrderKind.Move;
        bool passed = hashesEqual && deterministicSpawn && first.First.ValidateState(out _);
        return passed
            ? TestResult.Pass($"spawn_origin={expectedSpawn.X}:{expectedSpawn.Y}_rally={first.Horde.Destination.X}:{first.Horde.Destination.Y}_hash={first.First.StateHash():X8}")
            : TestResult.Fail($"hashes={hashesEqual}_spawn={first.Horde.Anchor.X}:{first.Horde.Anchor.Y}_destination={first.Horde.Destination.X}:{first.Horde.Destination.Y}_order={(int)first.Horde.Order}");
    }

    private static (Simulation First, Horde Horde) RunRallyScenario()
    {
        Simulation simulation = CreateSimulation();
        simulation.Navigation.SetBlocked(new GridCell(20, 20));
        simulation.TryPlaceBuilding(TeamId.Blue, 3, Center(10, 10), out EconomyBuilding? producer);
        simulation.Advance(90);
        simulation.TrySetRally(producer!.EntityId, Center(20, 20));
        simulation.TryTrain(producer.EntityId, 100, out _);
        simulation.Advance(90);
        return (simulation, simulation.Hordes.Single());
    }

    private static Simulation CreateSimulation()
    {
        Simulation simulation = new(new NavigationGrid(32, 32));
        simulation.EnableEconomy(CreateDefinition());
        return simulation;
    }

    internal static EconomyDefinition CreateDefinition() => new()
    {
        RulesVersion = 2,
        Rules = new EconomyRules
        {
            MaximumTrainQueue = 5,
            ConstructionHealthRamp = 1,
            BuildingBlocksNavigationAt = 0,
            QueuedBattalionsCountTowardPopulation = 1,
            SpawnSearchMaximumRadiusCells = 12,
            SpawnSearchOrder = [SpawnDirection.North, SpawnDirection.East, SpawnDirection.South, SpawnDirection.West],
        },
        FarmEfficiency = new FarmEfficiencyRules
        {
            RadiusSubcells = 12_000,
            BasePermille = 1_000,
            PenaltyPerNeighborPermille = 200,
            MinimumPermille = 400,
        },
        Sides =
        [
            new SideEconomyDefinition { Team = TeamId.Blue, StartingResources = 1_600, PopulationCap = 6 },
            new SideEconomyDefinition { Team = TeamId.Red, StartingResources = 0, PopulationCap = 6 },
        ],
        Buildings =
        [
            new BuildingDefinition { TypeCode = 1, ObjectId = "test.object.fortress", Role = BuildingRole.Fortress, Cost = 0, ConstructionTicks = 0, MaximumHealth = 5_000, FootprintWidthCells = 3, FootprintHeightCells = 3, BuildMenuSlot = -1, IncomeAmount = 0, IncomeIntervalTicks = 0, Trains = [] },
            new BuildingDefinition { TypeCode = 2, ObjectId = "test.object.farm", Role = BuildingRole.Resource, Cost = 300, ConstructionTicks = 60, MaximumHealth = 1_200, FootprintWidthCells = 3, FootprintHeightCells = 3, BuildMenuSlot = 0, IncomeAmount = 100, IncomeIntervalTicks = 90, Trains = [] },
            new BuildingDefinition { TypeCode = 3, ObjectId = "test.object.barracks", Role = BuildingRole.Production, Cost = 400, ConstructionTicks = 90, MaximumHealth = 2_000, FootprintWidthCells = 3, FootprintHeightCells = 3, BuildMenuSlot = 1, IncomeAmount = 0, IncomeIntervalTicks = 0, Trains = [100] },
            new BuildingDefinition { TypeCode = 4, ObjectId = "test.object.archery", Role = BuildingRole.Production, Cost = 400, ConstructionTicks = 90, MaximumHealth = 1_800, FootprintWidthCells = 3, FootprintHeightCells = 3, BuildMenuSlot = 2, IncomeAmount = 0, IncomeIntervalTicks = 0, Trains = [101] },
        ],
        HordeBlueprints =
        [
            new HordeBlueprint { TypeCode = 100, Id = "test.horde.practice-blocks", DisplayName = "Practice Blocks", MemberCount = 15, RangedCount = 0, Cost = 200, ProductionTicks = 90, Population = 1, TrainMenuSlot = 0 },
            new HordeBlueprint { TypeCode = 101, Id = "test.horde.practice-pins", DisplayName = "Practice Pins", MemberCount = 15, RangedCount = 15, Cost = 250, ProductionTicks = 105, Population = 1, TrainMenuSlot = 0 },
        ],
    };

    private static WorldPos Center(int x, int y) => NavigationGrid.CellCenter(new GridCell(x, y));

    private static string Sanitize(string value) => value.Replace(' ', '_').Replace('\r', '_').Replace('\n', '_');

    private readonly record struct TestResult(bool Passed, string Detail)
    {
        public static TestResult Pass(string detail) => new(true, detail);
        public static TestResult Fail(string detail) => new(false, detail);
    }
}
