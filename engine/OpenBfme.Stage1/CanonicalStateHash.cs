namespace OpenBfme.Stage1;

/// <summary>FNV-1a 32 over a canonical, ID-ordered stream of authoritative integers.</summary>
public static class CanonicalStateHash
{
    public static uint Compute(Simulation simulation)
    {
        Fnv1A32 hash = new();
        hash.Add(simulation.Tick);
        hash.Add((int)simulation.Winner);
        hash.Add(Simulation.TicksPerSecond);
        hash.Add(simulation.Navigation.Width);
        hash.Add(simulation.Navigation.Height);
        hash.Add(simulation.NextHordeId);
        hash.Add(simulation.NextMemberId);
        hash.Add(simulation.NextProjectileId);

        hash.Add(-5);
        hash.Add(simulation.CommandQueue.Count - simulation.NextCommandIndex);
        for (int index = simulation.NextCommandIndex; index < simulation.CommandQueue.Count; index++)
        {
            SimCommand command = simulation.CommandQueue[index];
            hash.Add(command.ExecuteTick);
            hash.Add(command.Sequence);
            hash.Add(command.HordeId);
            hash.Add((int)command.Kind);
            hash.Add(command.Destination.X);
            hash.Add(command.Destination.Y);
            hash.Add(command.TargetEntityId);
        }

        foreach (GridCell cell in simulation.Navigation.BlockedCells())
        {
            hash.Add(cell.X);
            hash.Add(cell.Y);
        }

        hash.Add(-10);
        foreach (Fortress fortress in simulation.Fortresses.OrderBy(static item => item.EntityId))
        {
            hash.Add(fortress.EntityId);
            hash.Add((int)fortress.Team);
            hash.Add(fortress.Position.X);
            hash.Add(fortress.Position.Y);
            hash.Add(fortress.Health);
            hash.Add(fortress.MaximumHealth);
        }

        hash.Add(-20);
        foreach (Horde horde in simulation.Hordes.OrderBy(static item => item.EntityId))
        {
            hash.Add(horde.EntityId);
            hash.Add((int)horde.Team);
            hash.Add(horde.Anchor.X);
            hash.Add(horde.Anchor.Y);
            hash.Add(horde.Destination.X);
            hash.Add(horde.Destination.Y);
            hash.Add((int)horde.Order);
            hash.Add(horde.TargetEntityId);
            hash.Add(horde.EngagedHordeId);
            hash.Add(horde.PathIndex);
            hash.Add(horde.PathRevision);
            hash.Add(horde.Path.Count);
            foreach (GridCell cell in horde.Path)
            {
                hash.Add(cell.X);
                hash.Add(cell.Y);
            }

            foreach (Member member in horde.Members.OrderBy(static item => item.EntityId))
            {
                hash.Add(member.EntityId);
                hash.Add(member.HordeId);
                hash.Add((int)member.Team);
                hash.Add(member.FormationSlot);
                hash.Add(member.IsRanged ? 1 : 0);
                hash.Add(member.Position.X);
                hash.Add(member.Position.Y);
                hash.Add(member.Health);
                hash.Add(member.MaximumHealth);
                hash.Add(member.AttackCooldownTicks);
                hash.Add(member.TargetEntityId);
            }

            hash.Add(-30);
        }

        hash.Add(-40);
        foreach (Projectile projectile in simulation.Projectiles.OrderBy(static item => item.EntityId))
        {
            hash.Add(projectile.EntityId);
            hash.Add((int)projectile.Team);
            hash.Add(projectile.SourceEntityId);
            hash.Add(projectile.TargetEntityId);
            hash.Add(projectile.Position.X);
            hash.Add(projectile.Position.Y);
            hash.Add(projectile.LastKnownTarget.X);
            hash.Add(projectile.LastKnownTarget.Y);
            hash.Add(projectile.Damage);
            hash.Add(projectile.IsActive ? 1 : 0);
        }

        EconomySystem? economy = simulation.Economy;
        if (economy is null)
        {
            return hash.Value;
        }

        hash.Add(-50);
        hash.Add(1);
        if (economy is not null)
        {
            EconomyDefinition definition = economy.Definition;
            EconomyRules rules = definition.Rules;
            FarmEfficiencyRules farms = definition.FarmEfficiency;
            hash.Add(definition.RulesVersion);
            hash.Add(economy.NextBuildingId);
            hash.Add(economy.NextJobId);
            hash.Add(rules.MaximumTrainQueue);
            hash.Add(rules.ConstructionHealthRamp);
            hash.Add(rules.BuildingBlocksNavigationAt);
            hash.Add(rules.QueuedBattalionsCountTowardPopulation);
            hash.Add(rules.SpawnSearchMaximumRadiusCells);
            hash.Add(rules.SpawnSearchOrder.Count);
            foreach (SpawnDirection direction in rules.SpawnSearchOrder)
            {
                hash.Add((int)direction);
            }

            hash.Add(farms.RadiusSubcells);
            hash.Add(farms.BasePermille);
            hash.Add(farms.PenaltyPerNeighborPermille);
            hash.Add(farms.MinimumPermille);
            hash.Add(economy.Economies.Count);
            foreach (TeamEconomy team in economy.Economies.OrderBy(static item => item.Team))
            {
                hash.Add((int)team.Team);
                hash.Add(team.Resources);
                hash.Add(team.PopulationUsed);
                hash.Add(team.PopulationReserved);
                hash.Add(team.PopulationCap);
                hash.Add(team.TotalEarned);
            }
        }

        hash.Add(-51);
        int remainingEconomyCommands = economy is null ? 0 : economy.CommandQueue.Count - economy.NextCommandIndex;
        hash.Add(remainingEconomyCommands);
        if (economy is not null)
        {
            for (int index = economy.NextCommandIndex; index < economy.CommandQueue.Count; index++)
            {
                EconomyCommand command = economy.CommandQueue[index];
                hash.Add(command.ExecuteTick);
                hash.Add(command.Sequence);
                hash.Add((int)command.Kind);
                hash.Add((int)command.Team);
                hash.Add(command.BuildingId);
                hash.Add(command.TypeCode);
                hash.Add(command.Position.X);
                hash.Add(command.Position.Y);
            }
        }

        hash.Add(-60);
        hash.Add(economy?.Definition.Buildings.Count ?? 0);
        if (economy is not null)
        {
            foreach (BuildingDefinition building in economy.Definition.Buildings.OrderBy(static item => item.TypeCode))
            {
                hash.Add(building.TypeCode);
                hash.Add((int)building.Role);
                hash.Add(building.Cost);
                hash.Add(building.ConstructionTicks);
                hash.Add(building.MaximumHealth);
                hash.Add(building.FootprintWidthCells);
                hash.Add(building.FootprintHeightCells);
                hash.Add(building.BuildMenuSlot);
                hash.Add(building.IncomeAmount);
                hash.Add(building.IncomeIntervalTicks);
                hash.Add(building.Trains.Count);
                foreach (int blueprintTypeCode in building.Trains.Order())
                {
                    hash.Add(blueprintTypeCode);
                }
            }
        }

        hash.Add(-61);
        hash.Add(economy?.Definition.HordeBlueprints.Count ?? 0);
        if (economy is not null)
        {
            foreach (HordeBlueprint blueprint in economy.Definition.HordeBlueprints.OrderBy(static item => item.TypeCode))
            {
                hash.Add(blueprint.TypeCode);
                hash.Add(blueprint.MemberCount);
                hash.Add(blueprint.RangedCount);
                hash.Add(blueprint.Cost);
                hash.Add(blueprint.ProductionTicks);
                hash.Add(blueprint.Population);
                hash.Add(blueprint.TrainMenuSlot);
            }
        }

        hash.Add(-70);
        hash.Add(economy?.Buildings.Count ?? 0);
        if (economy is not null)
        {
            foreach (EconomyBuilding building in economy.Buildings.OrderBy(static item => item.EntityId))
            {
                hash.Add(building.EntityId);
                hash.Add((int)building.Team);
                hash.Add(building.TypeCode);
                hash.Add(building.Position.X);
                hash.Add(building.Position.Y);
                hash.Add(building.Health);
                hash.Add(building.ConstructionHealthCap);
                hash.Add(building.ConstructionProgressTicks);
                hash.Add(building.IsCompleted ? 1 : 0);
                hash.Add(building.IsDestroyed ? 1 : 0);
                hash.Add(building.HasRallyPoint ? 1 : 0);
                hash.Add(building.RallyPoint.X);
                hash.Add(building.RallyPoint.Y);
                hash.Add(building.NextIncomeTick);
                hash.Add(economy.FarmEfficiencyPermille(building));
                hash.Add(building.Jobs.Count);
                foreach (ProductionJob job in building.Jobs)
                {
                    hash.Add(job.JobId);
                    hash.Add(job.BlueprintTypeCode);
                    hash.Add(job.RemainingTicks);
                    hash.Add(job.EnqueuedTick);
                    hash.Add(job.ReservedPopulation);
                }
            }
        }

        return hash.Value;
    }

    private struct Fnv1A32
    {
        private const uint OffsetBasis = 2_166_136_261;
        private const uint Prime = 16_777_619;
        private uint _value;
        private bool _initialized;

        public readonly uint Value => _initialized ? _value : OffsetBasis;

        public void Add(int value)
        {
            if (!_initialized)
            {
                _value = OffsetBasis;
                _initialized = true;
            }

            unchecked
            {
                AddByte((byte)value);
                AddByte((byte)(value >> 8));
                AddByte((byte)(value >> 16));
                AddByte((byte)(value >> 24));
            }
        }

        private void AddByte(byte value)
        {
            _value ^= value;
            _value *= Prime;
        }
    }
}
