namespace OpenBfme.Stage1;

/// <summary>
/// Fixed-tick authoritative Stage 1 simulation. The simulation contains no rendering,
/// wall-clock, floating-point, engine, retail-data, or donor-code dependency.
/// </summary>
public sealed class Simulation
{
    public const int TicksPerSecond = 30;
    public const int HordeMovePerTick = 360;
    public const int MemberMovePerTick = 430;
    public const int ProjectileMovePerTick = 900;

    public const int EngagementDistance = 7_000;
    public const int AnchorHoldDistance = 1_500;
    public const int MeleeRange = 950;
    public const int RangedRange = 6_000;
    public const int FortressRange = 1_500;
    public const int MeleeDamage = 22;
    public const int ProjectileDamage = 18;
    public const int MeleeCooldown = 8;
    public const int RangedCooldown = 12;

    private readonly List<SimCommand> _commands = [];
    private int _nextCommandIndex;
    private int _nextHordeId = 100;
    private int _nextMemberId = 1_000;
    private int _nextProjectileId = 100_000;

    public Simulation(NavigationGrid navigation)
    {
        Navigation = navigation;
    }

    public int Tick { get; private set; }
    public NavigationGrid Navigation { get; }
    public List<Horde> Hordes { get; } = [];
    public List<Fortress> Fortresses { get; } = [];
    public List<Projectile> Projectiles { get; } = [];
    public EconomySystem? Economy { get; private set; }
    public TeamId Winner { get; private set; } = TeamId.None;
    internal IReadOnlyList<SimCommand> CommandQueue => _commands;
    internal int NextCommandIndex => _nextCommandIndex;
    internal int NextHordeId => _nextHordeId;
    internal int NextMemberId => _nextMemberId;
    internal int NextProjectileId => _nextProjectileId;

    public void EnableEconomy(EconomyDefinition definition)
    {
        ArgumentNullException.ThrowIfNull(definition);
        if (Economy is not null)
        {
            throw new InvalidOperationException("The economy is already enabled.");
        }

        Economy = new EconomySystem(this, definition);
    }

    public void ScheduleEconomyCommand(EconomyCommand command)
    {
        RequireEconomy().ScheduleCommand(command);
    }

    public bool TryPlaceBuilding(TeamId team, int typeCode, WorldPos position, out EconomyBuilding? building) =>
        RequireEconomy().TryPlaceBuilding(team, typeCode, position, out building);

    public bool TryTrain(int buildingId, int blueprintTypeCode, out int jobId) =>
        RequireEconomy().TryTrain(buildingId, blueprintTypeCode, out jobId);

    public bool TrySetRally(int buildingId, WorldPos position) =>
        RequireEconomy().TrySetRally(buildingId, position);

    public bool DamageBuilding(int buildingId, int damage) =>
        RequireEconomy().DamageBuilding(buildingId, damage);

    public Fortress AddFortress(TeamId team, WorldPos position, int health = 5_000)
    {
        if (team is TeamId.None || !Navigation.IsWalkable(position))
        {
            throw new ArgumentException("A fortress requires a valid team and walkable position.", nameof(team));
        }

        int entityId = team == TeamId.Blue ? 1 : 2;
        if (Fortresses.Any(fortress => fortress.EntityId == entityId))
        {
            throw new InvalidOperationException($"Fortress {entityId} already exists.");
        }

        Fortress result = new()
        {
            EntityId = entityId,
            Team = team,
            Position = position,
            MaximumHealth = health,
            Health = health,
        };
        Fortresses.Add(result);
        Fortresses.Sort(static (left, right) => left.EntityId.CompareTo(right.EntityId));
        return result;
    }

    public Horde AddHorde(TeamId team, WorldPos anchor, int memberCount, int rangedEvery = 0)
    {
        if (team is TeamId.None || memberCount <= 0 || !Navigation.IsWalkable(anchor))
        {
            throw new ArgumentException("A horde requires a valid team, member count, and walkable anchor.");
        }

        Horde horde = new()
        {
            EntityId = _nextHordeId++,
            Team = team,
            Anchor = anchor,
            Destination = anchor,
        };

        for (int slot = 0; slot < memberCount; slot++)
        {
            WorldPos desired = Add(anchor, FormationOffset(slot, memberCount));
            WorldPos position = Navigation.IsWalkable(desired) ? desired : anchor;
            horde.Members.Add(new Member
            {
                EntityId = _nextMemberId++,
                HordeId = horde.EntityId,
                Team = team,
                FormationSlot = slot,
                IsRanged = rangedEvery > 0 && ((slot + 1) % rangedEvery == 0),
                Position = position,
            });
        }

        Hordes.Add(horde);
        return horde;
    }

    internal Horde AddHordeFromBlueprint(TeamId team, WorldPos anchor, int memberCount, int rangedCount)
    {
        if (rangedCount < 0 || rangedCount > memberCount)
        {
            throw new ArgumentOutOfRangeException(nameof(rangedCount));
        }

        Horde horde = AddHorde(team, anchor, memberCount);
        for (int slot = 0; slot < memberCount; slot++)
        {
            // Difference of cumulative integer ratios distributes exactly N ranged
            // members over stable slots, while retaining Stage 1's 3/5/15 patterns.
            horde.Members[slot].IsRanged = ((slot + 1) * rangedCount / memberCount) !=
                (slot * rangedCount / memberCount);
        }

        return horde;
    }

    public void ScheduleCommand(SimCommand command)
    {
        if (command.ExecuteTick < Tick)
        {
            throw new InvalidOperationException("Commands cannot be scheduled in the past.");
        }

        Horde? commandHorde = Hordes.FirstOrDefault(horde => horde.EntityId == command.HordeId);
        if (commandHorde is null)
        {
            throw new ArgumentException($"Unknown horde {command.HordeId}.", nameof(command));
        }

        if (command.Kind == OrderKind.AttackTarget && TryGetEntityTeam(command.TargetEntityId, out TeamId targetTeam) && targetTeam == commandHorde.Team)
        {
            throw new ArgumentException("Attack-target commands must name an enemy entity.", nameof(command));
        }

        _commands.Add(command);
        _commands.Sort(_nextCommandIndex, _commands.Count - _nextCommandIndex, CommandComparer.Instance);
    }

    public void AdvanceOneTick()
    {
        Economy?.AdvanceBeforeCombat();
        ApplyCommandsForCurrentTick();
        UpdateEngagements();
        MoveHordeAnchors();
        MoveMembersToFormation();
        ResolveMemberCombat();
        MoveProjectiles();
        CheckVictory();
        Economy?.ReleaseDestroyedHordePopulation();
        Tick++;
    }

    public void Advance(int tickCount)
    {
        if (tickCount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(tickCount));
        }

        for (int index = 0; index < tickCount; index++)
        {
            AdvanceOneTick();
        }
    }

    public uint StateHash() => CanonicalStateHash.Compute(this);

    public bool ValidateState(out string failure)
    {
        HashSet<int> ids = [];
        foreach (Fortress fortress in Fortresses)
        {
            if (!ids.Add(fortress.EntityId) || fortress.Team is TeamId.None ||
                fortress.Health < 0 || fortress.Health > fortress.MaximumHealth || !Navigation.IsWalkable(fortress.Position))
            {
                failure = $"invalid_fortress={fortress.EntityId}";
                return false;
            }
        }

        foreach (Horde horde in Hordes)
        {
            if (!ids.Add(horde.EntityId) || horde.Team is TeamId.None || !Navigation.IsWalkable(horde.Anchor) ||
                !InWorld(horde.Destination) || horde.PathIndex < 0 || horde.PathIndex > horde.Path.Count)
            {
                failure = $"invalid_horde={horde.EntityId}";
                return false;
            }

            int previousMemberId = -1;
            HashSet<int> formationSlots = [];
            foreach (Member member in horde.Members)
            {
                if (!ids.Add(member.EntityId) || member.EntityId <= previousMemberId || member.HordeId != horde.EntityId ||
                    member.Team != horde.Team || !formationSlots.Add(member.FormationSlot) || member.FormationSlot < 0 ||
                    member.Health < 0 || member.Health > member.MaximumHealth || member.AttackCooldownTicks < 0 ||
                    !Navigation.IsWalkable(member.Position))
                {
                    failure = $"invalid_member={member.EntityId}";
                    return false;
                }

                previousMemberId = member.EntityId;
            }
        }

        foreach (Projectile projectile in Projectiles)
        {
            if (!ids.Add(projectile.EntityId) || !InWorld(projectile.Position))
            {
                failure = $"invalid_projectile={projectile.EntityId}";
                return false;
            }
        }

        if (Economy is not null && !Economy.ValidateState(out failure))
        {
            return false;
        }

        failure = "none";
        return true;
    }

    public Member? FindMember(int entityId)
    {
        foreach (Horde horde in Hordes)
        {
            foreach (Member member in horde.Members)
            {
                if (member.EntityId == entityId)
                {
                    return member;
                }
            }
        }

        return null;
    }

    private void ApplyCommandsForCurrentTick()
    {
        while (_nextCommandIndex < _commands.Count && _commands[_nextCommandIndex].ExecuteTick <= Tick)
        {
            SimCommand command = _commands[_nextCommandIndex++];
            Horde? horde = Hordes.FirstOrDefault(candidate => candidate.EntityId == command.HordeId);
            if (horde is null || horde.AliveCount == 0)
            {
                continue;
            }

            horde.Order = command.Kind;
            horde.TargetEntityId = command.Kind == OrderKind.AttackTarget ? command.TargetEntityId : 0;
            horde.EngagedHordeId = 0;

            if (command.Kind == OrderKind.Stop)
            {
                horde.Destination = horde.Anchor;
                horde.Path.Clear();
                horde.PathIndex = 0;
                continue;
            }

            WorldPos destination = command.Destination;
            if (command.Kind == OrderKind.AttackTarget && TryGetEntityPosition(command.TargetEntityId, out WorldPos targetPosition))
            {
                destination = targetPosition;
            }

            SetHordePath(horde, destination);
        }
    }

    private void SetHordePath(Horde horde, WorldPos destination)
    {
        destination = ClampToWorld(destination);
        horde.Destination = destination;
        IReadOnlyList<GridCell> path = Navigation.FindPath(horde.Anchor, destination);
        horde.Path.Clear();
        horde.Path.AddRange(path);
        horde.PathIndex = path.Count > 1 ? 1 : 0;
        horde.PathRevision++;
    }

    private void UpdateEngagements()
    {
        foreach (Horde horde in Hordes)
        {
            horde.EngagedHordeId = 0;
            if (horde.AliveCount == 0)
            {
                continue;
            }

            if (horde.Order == OrderKind.AttackTarget)
            {
                Member? memberTarget = FindMember(horde.TargetEntityId);
                if (memberTarget is not null && memberTarget.IsAlive)
                {
                    horde.EngagedHordeId = memberTarget.HordeId;
                    continue;
                }

                Horde? hordeTarget = Hordes.FirstOrDefault(candidate => candidate.EntityId == horde.TargetEntityId && candidate.AliveCount > 0);
                if (hordeTarget is not null)
                {
                    horde.EngagedHordeId = hordeTarget.EntityId;
                }

                continue;
            }

            if (horde.Order != OrderKind.AttackMove)
            {
                continue;
            }

            long bestDistance = (long)EngagementDistance * EngagementDistance;
            int bestId = 0;
            foreach (Horde candidate in Hordes)
            {
                if (candidate.Team == horde.Team || candidate.AliveCount == 0)
                {
                    continue;
                }

                long distance = WorldPos.DistanceSquared(horde.Anchor, candidate.Anchor);
                if (distance < bestDistance || (distance == bestDistance && (bestId == 0 || candidate.EntityId < bestId)))
                {
                    bestDistance = distance;
                    bestId = candidate.EntityId;
                }
            }

            horde.EngagedHordeId = bestId;
        }
    }

    private void MoveHordeAnchors()
    {
        foreach (Horde horde in Hordes)
        {
            if (horde.AliveCount == 0 || horde.Order == OrderKind.Stop)
            {
                continue;
            }

            if (ShouldHoldAnchor(horde))
            {
                continue;
            }

            if (horde.PathIndex < horde.Path.Count && Navigation.IsBlocked(horde.Path[horde.PathIndex]))
            {
                SetHordePath(horde, horde.Destination);
            }

            bool followingPath = horde.PathIndex < horde.Path.Count;
            WorldPos target;
            if (followingPath)
            {
                target = NavigationGrid.CellCenter(horde.Path[horde.PathIndex]);
            }
            else if (horde.Anchor != horde.Destination)
            {
                target = horde.Destination;
            }
            else
            {
                continue;
            }

            WorldPos candidate = IntegerMath.MoveTowards(horde.Anchor, target, HordeMovePerTick);
            if (Navigation.IsWalkable(candidate))
            {
                horde.Anchor = candidate;
            }

            if (followingPath && horde.Anchor == target)
            {
                horde.PathIndex++;
            }
        }
    }

    private bool ShouldHoldAnchor(Horde horde)
    {
        if (horde.EngagedHordeId != 0)
        {
            Horde? target = Hordes.FirstOrDefault(candidate => candidate.EntityId == horde.EngagedHordeId);
            if (target is not null && WorldPos.DistanceSquared(horde.Anchor, target.Anchor) <= (long)AnchorHoldDistance * AnchorHoldDistance)
            {
                return true;
            }
        }

        if (horde.Order == OrderKind.AttackTarget)
        {
            Fortress? fortress = Fortresses.FirstOrDefault(candidate => candidate.EntityId == horde.TargetEntityId && candidate.IsAlive);
            if (fortress is not null && WorldPos.DistanceSquared(horde.Anchor, fortress.Position) <= (long)FortressRange * FortressRange)
            {
                return true;
            }
        }

        return false;
    }

    private void MoveMembersToFormation()
    {
        foreach (Horde horde in Hordes)
        {
            int memberCount = horde.Members.Count;
            foreach (Member member in horde.Members)
            {
                if (!member.IsAlive)
                {
                    continue;
                }

                WorldPos slotPosition = ClampToWorld(Add(horde.Anchor, OrientedFormationOffset(horde, member.FormationSlot, memberCount)));
                WorldPos movementTarget = SharedCorridorTarget(horde, member, slotPosition);
                TryMoveMember(member, movementTarget, MemberMovePerTick);
            }

            ApplyStableSeparation(horde);
        }
    }

    private WorldPos SharedCorridorTarget(Horde horde, Member member, WorldPos formationSlot)
    {
        const int catchUpRadius = 2_200;
        if (horde.Path.Count == 0 ||
            WorldPos.DistanceSquared(member.Position, horde.Anchor) <= (long)catchUpRadius * catchUpRadius)
        {
            return formationSlot;
        }

        // A straggler follows a short look-ahead on the horde's one shared path. The
        // member stores no path or waypoint state; nearest-cell and ties are recomputed
        // canonically from the immutable horde path and member position.
        int nearestIndex = 0;
        long nearestDistance = long.MaxValue;
        for (int index = 0; index < horde.Path.Count; index++)
        {
            long distance = WorldPos.DistanceSquared(member.Position, NavigationGrid.CellCenter(horde.Path[index]));
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearestIndex = index;
            }
        }

        int lookAheadIndex = Math.Min(nearestIndex + 2, horde.Path.Count - 1);
        return NavigationGrid.CellCenter(horde.Path[lookAheadIndex]);
    }

    private void ApplyStableSeparation(Horde horde)
    {
        const int minimumSeparation = 300;
        const int correction = 24;
        for (int leftIndex = 0; leftIndex < horde.Members.Count; leftIndex++)
        {
            Member left = horde.Members[leftIndex];
            if (!left.IsAlive)
            {
                continue;
            }

            for (int rightIndex = leftIndex + 1; rightIndex < horde.Members.Count; rightIndex++)
            {
                Member right = horde.Members[rightIndex];
                if (!right.IsAlive || WorldPos.DistanceSquared(left.Position, right.Position) >= (long)minimumSeparation * minimumSeparation)
                {
                    continue;
                }

                int dx = right.Position.X - left.Position.X;
                int dy = right.Position.Y - left.Position.Y;
                if (Math.Abs(dx) >= Math.Abs(dy))
                {
                    int direction = dx == 0 ? (left.EntityId < right.EntityId ? 1 : -1) : Math.Sign(dx);
                    TryNudge(left, new WorldPos(left.Position.X - (direction * correction), left.Position.Y));
                    TryNudge(right, new WorldPos(right.Position.X + (direction * correction), right.Position.Y));
                }
                else
                {
                    int direction = Math.Sign(dy);
                    TryNudge(left, new WorldPos(left.Position.X, left.Position.Y - (direction * correction)));
                    TryNudge(right, new WorldPos(right.Position.X, right.Position.Y + (direction * correction)));
                }
            }
        }
    }

    private void ResolveMemberCombat()
    {
        List<DamageEvent> damageEvents = [];
        Dictionary<int, int> meleeContacts = [];
        foreach (Horde horde in Hordes)
        {
            Horde? targetHorde = horde.EngagedHordeId == 0
                ? null
                : Hordes.FirstOrDefault(candidate => candidate.EntityId == horde.EngagedHordeId && candidate.AliveCount > 0);
            Fortress? targetFortress = FindFortressTarget(horde);

            foreach (Member member in horde.Members)
            {
                if (!member.IsAlive)
                {
                    continue;
                }

                if (member.AttackCooldownTicks > 0)
                {
                    member.AttackCooldownTicks--;
                }

                if (targetFortress is not null)
                {
                    member.TargetEntityId = targetFortress.EntityId;
                    AttackOrApproach(member, targetFortress.Position, targetFortress.EntityId, true, damageEvents);
                    continue;
                }

                Member? targetMember = targetHorde is null
                    ? null
                    : FindNearestEligibleMember(member, targetHorde, meleeContacts);
                if (targetMember is not null)
                {
                    member.TargetEntityId = targetMember.EntityId;
                    if (!member.IsRanged)
                    {
                        meleeContacts[targetMember.EntityId] = meleeContacts.GetValueOrDefault(targetMember.EntityId) + 1;
                    }
                    AttackOrApproach(member, targetMember.Position, targetMember.EntityId, false, damageEvents);
                }
                else
                {
                    member.TargetEntityId = 0;
                }
            }
        }

        ApplyDamage(damageEvents);
    }

    private Fortress? FindFortressTarget(Horde horde)
    {
        if (horde.Order == OrderKind.AttackTarget)
        {
            return Fortresses.FirstOrDefault(fortress =>
                fortress.EntityId == horde.TargetEntityId && fortress.Team != horde.Team && fortress.IsAlive);
        }

        if (horde.Order != OrderKind.AttackMove || horde.EngagedHordeId != 0)
        {
            return null;
        }

        Fortress? best = null;
        long bestDistance = (long)EngagementDistance * EngagementDistance;
        foreach (Fortress fortress in Fortresses)
        {
            if (fortress.Team == horde.Team || !fortress.IsAlive)
            {
                continue;
            }

            long distance = WorldPos.DistanceSquared(horde.Anchor, fortress.Position);
            if (distance < bestDistance || (distance == bestDistance && (best is null || fortress.EntityId < best.EntityId)))
            {
                best = fortress;
                bestDistance = distance;
            }
        }

        return best;
    }

    private void AttackOrApproach(
        Member attacker,
        WorldPos targetPosition,
        int targetEntityId,
        bool targetIsFortress,
        List<DamageEvent> damageEvents)
    {
        int range = attacker.IsRanged ? RangedRange : targetIsFortress ? FortressRange : MeleeRange;
        if (WorldPos.DistanceSquared(attacker.Position, targetPosition) > (long)range * range)
        {
            return;
        }

        if (attacker.AttackCooldownTicks != 0)
        {
            return;
        }

        if (attacker.IsRanged)
        {
            Projectiles.Add(new Projectile
            {
                EntityId = _nextProjectileId++,
                Team = attacker.Team,
                SourceEntityId = attacker.EntityId,
                TargetEntityId = targetEntityId,
                Position = attacker.Position,
                LastKnownTarget = targetPosition,
                Damage = ProjectileDamage,
            });
            attacker.AttackCooldownTicks = RangedCooldown;
        }
        else
        {
            damageEvents.Add(new DamageEvent(attacker.EntityId, targetEntityId, MeleeDamage));
            attacker.AttackCooldownTicks = MeleeCooldown;
        }
    }

    private void MoveProjectiles()
    {
        List<DamageEvent> damageEvents = [];
        foreach (Projectile projectile in Projectiles)
        {
            if (!projectile.IsActive)
            {
                continue;
            }

            bool targetAlive = TryGetLivingEntity(projectile.TargetEntityId, out TeamId targetTeam, out WorldPos targetPosition);
            if (targetAlive)
            {
                projectile.LastKnownTarget = targetPosition;
            }

            WorldPos next = IntegerMath.MoveTowards(projectile.Position, projectile.LastKnownTarget, ProjectileMovePerTick);
            projectile.Position = ClampToWorld(next);
            if (projectile.Position != projectile.LastKnownTarget)
            {
                continue;
            }

            projectile.IsActive = false;
            if (targetAlive && targetTeam != projectile.Team)
            {
                damageEvents.Add(new DamageEvent(projectile.SourceEntityId, projectile.TargetEntityId, projectile.Damage));
            }
        }

        ApplyDamage(damageEvents);
        Projectiles.RemoveAll(static projectile => !projectile.IsActive);
    }

    private void ApplyDamage(List<DamageEvent> events)
    {
        foreach (DamageEvent damage in events)
        {
            Member? member = FindMember(damage.TargetEntityId);
            if (member is not null && member.IsAlive)
            {
                member.Health = Math.Max(0, member.Health - damage.Amount);
                continue;
            }

            Fortress? fortress = Fortresses.FirstOrDefault(candidate => candidate.EntityId == damage.TargetEntityId && candidate.IsAlive);
            if (fortress is not null)
            {
                fortress.Health = Math.Max(0, fortress.Health - damage.Amount);
            }
        }
    }

    private void CheckVictory()
    {
        if (Fortresses.Count < 2)
        {
            return;
        }

        bool blueAlive = Fortresses.Any(fortress => fortress.Team == TeamId.Blue && fortress.IsAlive);
        bool redAlive = Fortresses.Any(fortress => fortress.Team == TeamId.Red && fortress.IsAlive);
        Winner = (blueAlive, redAlive) switch
        {
            (true, false) => TeamId.Blue,
            (false, true) => TeamId.Red,
            _ => TeamId.None,
        };
    }

    private Member? FindNearestEligibleMember(Member attacker, Horde targetHorde, IReadOnlyDictionary<int, int> meleeContacts)
    {
        Member? best = null;
        long bestDistance = long.MaxValue;
        foreach (Member candidate in targetHorde.Members)
        {
            if (!candidate.IsAlive)
            {
                continue;
            }

            if (!attacker.IsRanged && (meleeContacts.GetValueOrDefault(candidate.EntityId) >= 4 ||
                WorldPos.DistanceSquared(attacker.Position, candidate.Position) > (long)MeleeRange * MeleeRange))
            {
                continue;
            }

            long distance = WorldPos.DistanceSquared(attacker.Position, candidate.Position);
            if (distance < bestDistance || (distance == bestDistance && (best is null || candidate.EntityId < best.EntityId)))
            {
                bestDistance = distance;
                best = candidate;
            }
        }

        return best;
    }

    private bool TryGetEntityTeam(int entityId, out TeamId team)
    {
        Member? member = FindMember(entityId);
        if (member is not null)
        {
            team = member.Team;
            return true;
        }

        Horde? horde = Hordes.FirstOrDefault(candidate => candidate.EntityId == entityId);
        if (horde is not null)
        {
            team = horde.Team;
            return true;
        }

        Fortress? fortress = Fortresses.FirstOrDefault(candidate => candidate.EntityId == entityId);
        if (fortress is not null)
        {
            team = fortress.Team;
            return true;
        }

        team = TeamId.None;
        return false;
    }

    private bool TryGetLivingEntity(int entityId, out TeamId team, out WorldPos position)
    {
        Member? member = FindMember(entityId);
        if (member is not null && member.IsAlive)
        {
            team = member.Team;
            position = member.Position;
            return true;
        }

        Fortress? fortress = Fortresses.FirstOrDefault(candidate => candidate.EntityId == entityId && candidate.IsAlive);
        if (fortress is not null)
        {
            team = fortress.Team;
            position = fortress.Position;
            return true;
        }

        team = TeamId.None;
        position = default;
        return false;
    }

    private bool TryGetEntityPosition(int entityId, out WorldPos position)
    {
        if (TryGetLivingEntity(entityId, out _, out position))
        {
            return true;
        }

        Horde? horde = Hordes.FirstOrDefault(candidate => candidate.EntityId == entityId && candidate.AliveCount > 0);
        if (horde is not null)
        {
            position = horde.Anchor;
            return true;
        }

        return false;
    }

    private void TryMoveMember(Member member, WorldPos target, int maximumStep)
    {
        WorldPos candidate = ClampToWorld(IntegerMath.MoveTowards(member.Position, target, maximumStep));
        if (Navigation.IsWalkable(candidate))
        {
            member.Position = candidate;
            return;
        }

        WorldPos horizontal = new(candidate.X, member.Position.Y);
        if (Navigation.IsWalkable(horizontal))
        {
            member.Position = horizontal;
            return;
        }

        WorldPos vertical = new(member.Position.X, candidate.Y);
        if (Navigation.IsWalkable(vertical))
        {
            member.Position = vertical;
        }
    }

    private void TryNudge(Member member, WorldPos position)
    {
        position = ClampToWorld(position);
        if (Navigation.IsWalkable(position))
        {
            member.Position = position;
        }
    }

    private bool InWorld(WorldPos position) =>
        position.X >= 0 && position.X < Navigation.Width * NavigationGrid.CellSize &&
        position.Y >= 0 && position.Y < Navigation.Height * NavigationGrid.CellSize;

    private WorldPos ClampToWorld(WorldPos position) => new(
        Math.Clamp(position.X, 0, (Navigation.Width * NavigationGrid.CellSize) - 1),
        Math.Clamp(position.Y, 0, (Navigation.Height * NavigationGrid.CellSize) - 1));

    private EconomySystem RequireEconomy() => Economy ??
        throw new InvalidOperationException("The Stage 2 economy is not enabled.");

    private static WorldPos FormationOffset(int slot, int memberCount)
    {
        int columns = Math.Min(5, memberCount);
        int rows = (memberCount + columns - 1) / columns;
        int column = slot % columns;
        int row = slot / columns;
        return new WorldPos(
            ((column * 2) - (columns - 1)) * 325,
            ((row * 2) - (rows - 1)) * 325);
    }

    private static WorldPos OrientedFormationOffset(Horde horde, int slot, int memberCount)
    {
        WorldPos local = FormationOffset(slot, memberCount);
        WorldPos target = horde.PathIndex < horde.Path.Count
            ? NavigationGrid.CellCenter(horde.Path[horde.PathIndex])
            : horde.Destination;
        int dx = target.X - horde.Anchor.X;
        int dy = target.Y - horde.Anchor.Y;
        int forwardX;
        int forwardY;
        if (Math.Abs(dx) >= Math.Abs(dy) && dx != 0)
        {
            forwardX = Math.Sign(dx);
            forwardY = 0;
        }
        else if (dy != 0)
        {
            forwardX = 0;
            forwardY = Math.Sign(dy);
        }
        else
        {
            forwardX = horde.Team == TeamId.Blue ? 1 : -1;
            forwardY = 0;
        }

        int rightX = -forwardY;
        int rightY = forwardX;
        return new WorldPos(
            (local.X * rightX) + (local.Y * forwardX),
            (local.X * rightY) + (local.Y * forwardY));
    }

    private static WorldPos Add(WorldPos left, WorldPos right) => new(left.X + right.X, left.Y + right.Y);

    private readonly record struct DamageEvent(int SourceEntityId, int TargetEntityId, int Amount);

    private sealed class CommandComparer : IComparer<SimCommand>
    {
        public static CommandComparer Instance { get; } = new();

        public int Compare(SimCommand left, SimCommand right)
        {
            int value = left.ExecuteTick.CompareTo(right.ExecuteTick);
            if (value != 0) return value;
            value = left.Sequence.CompareTo(right.Sequence);
            if (value != 0) return value;
            value = left.HordeId.CompareTo(right.HordeId);
            if (value != 0) return value;
            return left.Kind.CompareTo(right.Kind);
        }
    }
}
