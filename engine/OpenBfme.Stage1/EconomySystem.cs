namespace OpenBfme.Stage1;

/// <summary>
/// Deterministic Stage 2 economy state. It is deliberately integer-only and is
/// driven by the same fixed simulation tick as combat and navigation.
/// </summary>
public sealed class EconomySystem
{
    public const int FirstBuildingId = 200;
    public const int FirstJobId = 1;

    private readonly Simulation _simulation;
    private readonly List<EconomyCommand> _commands = [];
    private int _nextCommandIndex;
    private int _nextBuildingId = FirstBuildingId;
    private int _nextJobId = FirstJobId;

    internal EconomySystem(Simulation simulation, EconomyDefinition definition)
    {
        _simulation = simulation;
        EconomyDefinition.Validate(definition);
        Definition = definition;
        foreach (SideEconomyDefinition side in definition.Sides.OrderBy(static item => item.Team))
        {
            int startingPopulation = simulation.Hordes
                .Where(horde => horde.Team == side.Team && horde.AliveCount > 0)
                .Sum(static horde => horde.PopulationCost);
            Economies.Add(new TeamEconomy
            {
                Team = side.Team,
                Resources = side.StartingResources,
                TotalEarned = 0,
                PopulationUsed = startingPopulation,
                PopulationReserved = 0,
                PopulationCap = side.PopulationCap,
            });
        }
    }

    public EconomyDefinition Definition { get; }
    public List<TeamEconomy> Economies { get; } = [];
    public List<EconomyBuilding> Buildings { get; } = [];
    internal IReadOnlyList<EconomyCommand> CommandQueue => _commands;
    internal int NextCommandIndex => _nextCommandIndex;
    internal int NextBuildingId => _nextBuildingId;
    internal int NextJobId => _nextJobId;

    public TeamEconomy? FindEconomy(TeamId team) => Economies.FirstOrDefault(item => item.Team == team);

    public EconomyBuilding? FindBuilding(int entityId) => Buildings.FirstOrDefault(item => item.EntityId == entityId);

    public BuildingDefinition? FindBuildingDefinition(int typeCode) =>
        Definition.Buildings.FirstOrDefault(item => item.TypeCode == typeCode);

    public HordeBlueprint? FindBlueprint(int typeCode) =>
        Definition.HordeBlueprints.FirstOrDefault(item => item.TypeCode == typeCode);

    public void ScheduleCommand(EconomyCommand command)
    {
        if (command.ExecuteTick < _simulation.Tick)
        {
            throw new InvalidOperationException("Economy commands cannot be scheduled in the past.");
        }

        if (command.Team is not (TeamId.None or TeamId.Blue or TeamId.Red))
        {
            throw new ArgumentException("An economy command has an invalid team.", nameof(command));
        }

        _commands.Add(command);
        _commands.Sort(_nextCommandIndex, _commands.Count - _nextCommandIndex, EconomyCommandComparer.Instance);
    }

    internal void AdvanceBeforeCombat()
    {
        ApplyCommandsForCurrentTick();
        UpdateConstruction();
        UpdateIncome();
        UpdateProduction();
    }

    internal void ReleaseDestroyedHordePopulation()
    {
        foreach (Horde horde in _simulation.Hordes.OrderBy(static item => item.EntityId))
        {
            if (horde.AliveCount > 0 || horde.PopulationReleased)
            {
                continue;
            }

            TeamEconomy? economy = FindEconomy(horde.Team);
            if (economy is not null)
            {
                economy.PopulationUsed = Math.Max(0, economy.PopulationUsed - horde.PopulationCost);
            }

            horde.PopulationReleased = true;
        }
    }

    public bool TryPlaceBuilding(TeamId team, int typeCode, WorldPos position, out EconomyBuilding? building)
    {
        building = null;
        TeamEconomy? economy = FindEconomy(team);
        BuildingDefinition? definition = FindBuildingDefinition(typeCode);
        if (economy is null || definition is null || definition.Role == BuildingRole.Fortress || economy.Resources < definition.Cost)
        {
            return false;
        }

        if ((definition.FootprintWidthCells & 1) == 0 || (definition.FootprintHeightCells & 1) == 0)
        {
            return false;
        }

        GridCell center = _simulation.Navigation.ToCell(position);
        if (NavigationGrid.CellCenter(center) != position)
        {
            return false;
        }

        IReadOnlyList<GridCell> footprint = FootprintCells(center, definition);
        if (footprint.Count == 0 || footprint.Any(_simulation.Navigation.IsBlocked) || OccupiesLivingEntity(footprint))
        {
            return false;
        }

        int entityId = _nextBuildingId;
        if (!_simulation.Navigation.TrySetDynamicBlocker(entityId, footprint))
        {
            return false;
        }

        economy.Resources -= definition.Cost;
        _nextBuildingId++;
        bool completed = definition.ConstructionTicks == 0;
        building = new EconomyBuilding
        {
            EntityId = entityId,
            Team = team,
            TypeCode = typeCode,
            Position = position,
            Health = completed ? definition.MaximumHealth : 1,
            ConstructionHealthCap = completed ? definition.MaximumHealth : 1,
            ConstructionProgressTicks = completed ? definition.ConstructionTicks : 0,
            IsCompleted = completed,
            HasRallyPoint = false,
            RallyPoint = position,
            NextIncomeTick = completed && definition.IncomeIntervalTicks > 0
                ? _simulation.Tick + definition.IncomeIntervalTicks
                : -1,
        };
        Buildings.Add(building);
        Buildings.Sort(static (left, right) => left.EntityId.CompareTo(right.EntityId));
        return true;
    }

    public bool TrySetRally(int buildingId, WorldPos requestedPosition)
    {
        EconomyBuilding? building = FindBuilding(buildingId);
        BuildingDefinition? definition = building is null ? null : FindBuildingDefinition(building.TypeCode);
        if (building is null || definition is null || building.IsDestroyed || definition.Role != BuildingRole.Production || !InWorld(requestedPosition))
        {
            return false;
        }

        building.HasRallyPoint = true;
        building.RallyPoint = requestedPosition;
        return true;
    }

    public bool TryTrain(int buildingId, int blueprintTypeCode, out int jobId)
    {
        jobId = 0;
        EconomyBuilding? building = FindBuilding(buildingId);
        BuildingDefinition? buildingDefinition = building is null ? null : FindBuildingDefinition(building.TypeCode);
        HordeBlueprint? blueprint = FindBlueprint(blueprintTypeCode);
        TeamEconomy? economy = building is null ? null : FindEconomy(building.Team);
        if (building is null || buildingDefinition is null || blueprint is null || economy is null ||
            building.IsDestroyed || !building.IsCompleted || buildingDefinition.Role != BuildingRole.Production ||
            !buildingDefinition.Trains.Contains(blueprintTypeCode) ||
            building.Jobs.Count >= Definition.Rules.MaximumTrainQueue || economy.Resources < blueprint.Cost)
        {
            return false;
        }

        int populationCommitted = economy.PopulationUsed;
        if (Definition.Rules.QueuedBattalionsCountTowardPopulation != 0)
        {
            populationCommitted += economy.PopulationReserved;
        }

        if (populationCommitted + blueprint.Population > economy.PopulationCap)
        {
            return false;
        }

        economy.Resources -= blueprint.Cost;
        economy.PopulationReserved += blueprint.Population;
        jobId = _nextJobId++;
        building.Jobs.Add(new ProductionJob
        {
            JobId = jobId,
            BlueprintTypeCode = blueprint.TypeCode,
            RemainingTicks = blueprint.ProductionTicks,
            EnqueuedTick = _simulation.Tick,
            ReservedPopulation = blueprint.Population,
        });
        return true;
    }

    public bool DamageBuilding(int buildingId, int damage)
    {
        if (damage <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(damage));
        }

        EconomyBuilding? building = FindBuilding(buildingId);
        if (building is null || building.IsDestroyed)
        {
            return false;
        }

        building.Health = Math.Max(0, building.Health - damage);
        if (building.IsDestroyed)
        {
            DestroyBuilding(building);
        }

        return true;
    }

    public int FarmEfficiencyPermille(EconomyBuilding building)
    {
        BuildingDefinition? definition = FindBuildingDefinition(building.TypeCode);
        if (definition is null || definition.Role != BuildingRole.Resource || building.IsDestroyed || !building.IsCompleted)
        {
            return 0;
        }

        int neighbors = 0;
        long radiusSquared = (long)Definition.FarmEfficiency.RadiusSubcells * Definition.FarmEfficiency.RadiusSubcells;
        foreach (EconomyBuilding candidate in Buildings)
        {
            if (candidate.EntityId == building.EntityId || candidate.Team != building.Team || candidate.IsDestroyed || !candidate.IsCompleted)
            {
                continue;
            }

            BuildingDefinition? candidateDefinition = FindBuildingDefinition(candidate.TypeCode);
            if (candidateDefinition?.Role == BuildingRole.Resource &&
                WorldPos.DistanceSquared(building.Position, candidate.Position) <= radiusSquared)
            {
                neighbors++;
            }
        }

        int efficiency = Definition.FarmEfficiency.BasePermille - (neighbors * Definition.FarmEfficiency.PenaltyPerNeighborPermille);
        return Math.Max(Definition.FarmEfficiency.MinimumPermille, efficiency);
    }

    public bool ValidateState(out string failure)
    {
        foreach (TeamEconomy economy in Economies)
        {
            if (economy.Team is TeamId.None || economy.Resources < 0 || economy.TotalEarned < 0 || economy.PopulationUsed < 0 ||
                economy.PopulationReserved < 0 || economy.PopulationUsed + economy.PopulationReserved > economy.PopulationCap)
            {
                failure = $"invalid_economy={(int)economy.Team}";
                return false;
            }
        }

        HashSet<int> ids = [];
        HashSet<int> jobs = [];
        foreach (EconomyBuilding building in Buildings.OrderBy(static item => item.EntityId))
        {
            BuildingDefinition? definition = FindBuildingDefinition(building.TypeCode);
            if (!ids.Add(building.EntityId) || building.EntityId < FirstBuildingId || definition is null ||
                building.Team is TeamId.None || building.Health < 0 || building.Health > building.ConstructionHealthCap ||
                building.ConstructionHealthCap < 1 || building.ConstructionHealthCap > definition.MaximumHealth ||
                building.ConstructionProgressTicks < 0 || building.ConstructionProgressTicks > definition.ConstructionTicks ||
                building.IsCompleted != (building.ConstructionProgressTicks >= definition.ConstructionTicks) ||
                (!building.IsDestroyed && FootprintCells(_simulation.Navigation.ToCell(building.Position), definition)
                    .Any(cell => _simulation.Navigation.DynamicBlockOwner(cell) != building.EntityId)))
            {
                failure = $"invalid_building={building.EntityId}";
                return false;
            }

            foreach (ProductionJob job in building.Jobs)
            {
                if (!jobs.Add(job.JobId) || job.JobId <= 0 || FindBlueprint(job.BlueprintTypeCode) is null ||
                    job.RemainingTicks < 0 || job.ReservedPopulation <= 0)
                {
                    failure = $"invalid_job={job.JobId}";
                    return false;
                }
            }
        }

        failure = "none";
        return true;
    }

    private void ApplyCommandsForCurrentTick()
    {
        while (_nextCommandIndex < _commands.Count && _commands[_nextCommandIndex].ExecuteTick <= _simulation.Tick)
        {
            EconomyCommand command = _commands[_nextCommandIndex++];
            switch (command.Kind)
            {
                case EconomyCommandKind.Place:
                    TryPlaceBuilding(command.Team, command.TypeCode, command.Position, out _);
                    break;
                case EconomyCommandKind.Train:
                    TryTrain(command.BuildingId, command.TypeCode, out _);
                    break;
                case EconomyCommandKind.Rally:
                    TrySetRally(command.BuildingId, command.Position);
                    break;
                default:
                    throw new InvalidOperationException($"Unknown economy command kind {(int)command.Kind}.");
            }
        }
    }

    private void UpdateConstruction()
    {
        foreach (EconomyBuilding building in Buildings.OrderBy(static item => item.EntityId))
        {
            if (building.IsDestroyed || building.IsCompleted)
            {
                continue;
            }

            BuildingDefinition definition = FindBuildingDefinition(building.TypeCode)!;
            int nextProgress = Math.Min(definition.ConstructionTicks, building.ConstructionProgressTicks + 1);
            int nextHealthCap = nextProgress >= definition.ConstructionTicks
                ? definition.MaximumHealth
                : Math.Max(1, (int)((long)definition.MaximumHealth * nextProgress / definition.ConstructionTicks));
            int constructionGain = nextHealthCap - building.ConstructionHealthCap;
            building.Health = Math.Min(nextHealthCap, building.Health + constructionGain);
            building.ConstructionHealthCap = nextHealthCap;
            building.ConstructionProgressTicks = nextProgress;

            if (nextProgress >= definition.ConstructionTicks)
            {
                building.IsCompleted = true;
                building.NextIncomeTick = definition.IncomeIntervalTicks > 0
                    ? _simulation.Tick + definition.IncomeIntervalTicks
                    : -1;
            }
        }
    }

    private void UpdateIncome()
    {
        foreach (EconomyBuilding building in Buildings.OrderBy(static item => item.EntityId))
        {
            BuildingDefinition definition = FindBuildingDefinition(building.TypeCode)!;
            if (building.IsDestroyed || !building.IsCompleted || definition.Role != BuildingRole.Resource ||
                building.NextIncomeTick < 0 || _simulation.Tick < building.NextIncomeTick)
            {
                continue;
            }

            TeamEconomy economy = FindEconomy(building.Team)!;
            int efficiency = FarmEfficiencyPermille(building);
            int payout = (int)((long)definition.IncomeAmount * efficiency / 1_000);
            economy.Resources += payout;
            economy.TotalEarned += payout;
            building.NextIncomeTick += definition.IncomeIntervalTicks;
        }
    }

    private void UpdateProduction()
    {
        foreach (EconomyBuilding building in Buildings.OrderBy(static item => item.EntityId))
        {
            if (building.IsDestroyed || !building.IsCompleted || building.Jobs.Count == 0)
            {
                continue;
            }

            ProductionJob job = building.Jobs[0];
            if (job.RemainingTicks > 0)
            {
                job.RemainingTicks--;
            }

            if (job.RemainingTicks > 0 || !TryFindSpawnPosition(building, out WorldPos spawnPosition))
            {
                continue;
            }

            HordeBlueprint blueprint = FindBlueprint(job.BlueprintTypeCode)!;
            Horde horde = _simulation.AddHordeFromBlueprint(building.Team, spawnPosition, blueprint.MemberCount, blueprint.RangedCount);
            horde.PopulationCost = blueprint.Population;
            horde.PopulationReleased = false;

            TeamEconomy economy = FindEconomy(building.Team)!;
            economy.PopulationReserved = Math.Max(0, economy.PopulationReserved - job.ReservedPopulation);
            economy.PopulationUsed += blueprint.Population;
            building.Jobs.RemoveAt(0);

            if (building.HasRallyPoint && ResolveWalkableNear(building.RallyPoint, includeCenter: true, out WorldPos destination) && destination != spawnPosition)
            {
                _simulation.ScheduleCommand(new SimCommand(
                    _simulation.Tick,
                    1_000_000 + job.JobId,
                    horde.EntityId,
                    OrderKind.Move,
                    destination));
            }
        }
    }

    private void DestroyBuilding(EconomyBuilding building)
    {
        TeamEconomy? economy = FindEconomy(building.Team);
        if (economy is not null)
        {
            economy.PopulationReserved = Math.Max(0, economy.PopulationReserved - building.Jobs.Sum(static job => job.ReservedPopulation));
        }

        building.Jobs.Clear();
        building.NextIncomeTick = -1;
        _simulation.Navigation.ClearDynamicBlocker(building.EntityId);
    }

    private bool TryFindSpawnPosition(EconomyBuilding building, out WorldPos position) =>
        ResolveWalkableNear(building.Position, includeCenter: false, out position);

    private bool ResolveWalkableNear(WorldPos origin, bool includeCenter, out WorldPos position)
    {
        GridCell center = _simulation.Navigation.ToCell(origin);
        Queue<(GridCell Cell, int Distance)> frontier = new();
        HashSet<GridCell> visited = [center];
        frontier.Enqueue((center, 0));
        while (frontier.Count > 0)
        {
            (GridCell cell, int distance) = frontier.Dequeue();
            if ((includeCenter || distance > 0) && !_simulation.Navigation.IsBlocked(cell))
            {
                position = NavigationGrid.CellCenter(cell);
                return true;
            }

            if (distance >= Definition.Rules.SpawnSearchMaximumRadiusCells)
            {
                continue;
            }

            foreach (SpawnDirection direction in Definition.Rules.SpawnSearchOrder)
            {
                GridCell next = Offset(cell, direction);
                if (_simulation.Navigation.Contains(next) && visited.Add(next))
                {
                    frontier.Enqueue((next, distance + 1));
                }
            }
        }

        position = default;
        return false;
    }

    private bool OccupiesLivingEntity(IReadOnlyList<GridCell> footprint)
    {
        HashSet<GridCell> cells = footprint.ToHashSet();
        BuildingDefinition[] fortressDefinitions = Definition.Buildings
            .Where(static definition => definition.Role == BuildingRole.Fortress)
            .ToArray();
        foreach (Fortress fortress in _simulation.Fortresses.Where(static fortress => fortress.IsAlive))
        {
            GridCell fortressCenter = _simulation.Navigation.ToCell(fortress.Position);
            if (fortressDefinitions.Length == 0)
            {
                if (cells.Contains(fortressCenter))
                {
                    return true;
                }

                continue;
            }

            foreach (BuildingDefinition fortressDefinition in fortressDefinitions)
            {
                if (FootprintsIntersect(footprint, fortressCenter, fortressDefinition))
                {
                    return true;
                }
            }
        }

        foreach (Horde horde in _simulation.Hordes)
        {
            if (horde.AliveCount > 0 && cells.Contains(_simulation.Navigation.ToCell(horde.Anchor)))
            {
                return true;
            }

            if (horde.Members.Any(member => member.IsAlive && cells.Contains(_simulation.Navigation.ToCell(member.Position))))
            {
                return true;
            }
        }

        return false;
    }

    private static bool FootprintsIntersect(
        IReadOnlyList<GridCell> footprint,
        GridCell otherCenter,
        BuildingDefinition otherDefinition)
    {
        int minimumX = footprint.Min(static cell => cell.X);
        int maximumX = footprint.Max(static cell => cell.X);
        int minimumY = footprint.Min(static cell => cell.Y);
        int maximumY = footprint.Max(static cell => cell.Y);
        int otherHalfWidth = otherDefinition.FootprintWidthCells / 2;
        int otherHalfHeight = otherDefinition.FootprintHeightCells / 2;
        return minimumX <= otherCenter.X + otherHalfWidth &&
            maximumX >= otherCenter.X - otherHalfWidth &&
            minimumY <= otherCenter.Y + otherHalfHeight &&
            maximumY >= otherCenter.Y - otherHalfHeight;
    }

    private IReadOnlyList<GridCell> FootprintCells(GridCell center, BuildingDefinition definition)
    {
        int halfWidth = definition.FootprintWidthCells / 2;
        int halfHeight = definition.FootprintHeightCells / 2;
        List<GridCell> cells = [];
        for (int y = center.Y - halfHeight; y <= center.Y + halfHeight; y++)
        {
            for (int x = center.X - halfWidth; x <= center.X + halfWidth; x++)
            {
                GridCell cell = new(x, y);
                if (!_simulation.Navigation.Contains(cell))
                {
                    return Array.Empty<GridCell>();
                }

                cells.Add(cell);
            }
        }

        return cells;
    }

    private bool InWorld(WorldPos position) =>
        position.X >= 0 && position.X < _simulation.Navigation.Width * NavigationGrid.CellSize &&
        position.Y >= 0 && position.Y < _simulation.Navigation.Height * NavigationGrid.CellSize;

    private static GridCell Offset(GridCell cell, SpawnDirection direction) => direction switch
    {
        SpawnDirection.North => new GridCell(cell.X, cell.Y - 1),
        SpawnDirection.East => new GridCell(cell.X + 1, cell.Y),
        SpawnDirection.South => new GridCell(cell.X, cell.Y + 1),
        SpawnDirection.West => new GridCell(cell.X - 1, cell.Y),
        _ => cell,
    };

    private sealed class EconomyCommandComparer : IComparer<EconomyCommand>
    {
        public static EconomyCommandComparer Instance { get; } = new();

        public int Compare(EconomyCommand left, EconomyCommand right)
        {
            int value = left.ExecuteTick.CompareTo(right.ExecuteTick);
            if (value != 0) return value;
            value = left.Sequence.CompareTo(right.Sequence);
            if (value != 0) return value;
            value = left.Kind.CompareTo(right.Kind);
            if (value != 0) return value;
            value = left.Team.CompareTo(right.Team);
            if (value != 0) return value;
            value = left.BuildingId.CompareTo(right.BuildingId);
            if (value != 0) return value;
            return left.TypeCode.CompareTo(right.TypeCode);
        }
    }
}
