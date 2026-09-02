namespace OpenBfme.Sim;

/// <summary>Deterministic SAGE-shaped targeting, order pursuit, firing, and damage.</summary>
public sealed class CombatSystem
{
    private const int SpatialCellSize = 8;
    private static readonly Fixed64 AttackMoveAcquireMargin = Fixed64.FromInt(2);
    private static readonly Fixed64 AggressiveAcquireMargin = Fixed64.FromInt(8);

    private readonly SimConfig _config;
    private readonly SortedDictionary<(int X, int Y), List<GameObject>> _spatial = new();
    private readonly SortedDictionary<int, SnapshotHorde> _memberHordes = new();
    private readonly SortedDictionary<int, HordeCombatMotion> _hordeMotions = new();

    public CombatSystem(SimConfig config) =>
        _config = config ?? throw new ArgumentNullException(nameof(config));

    internal void ApplyCommand(SimWorld world, SimCommand command, bool ownershipAlreadyChecked = false)
    {
        var ids = ObjectIds(command);
        if (command.Type == "move")
        {
            foreach (var gameObject in OwnedObjects(world, command, ids, ownershipAlreadyChecked))
            {
                gameObject.Combat?.ClearOrder();
            }
            return;
        }

        if (command.Type == "stance")
        {
            if (!TryParseStance(command.GetString("stance"), out var stance))
            {
                world.RecordCombatDiagnostic(command, 0, "invalid_stance",
                    $"unknown stance '{command.GetString("stance")}'");
                return;
            }
            foreach (var gameObject in OwnedObjects(world, command, ids, ownershipAlreadyChecked))
            {
                if (gameObject.Combat == null) continue;
                gameObject.Combat.Stance = stance;
                gameObject.FindModule<StancesBehaviorModule>()?.ApplyProfile(gameObject, stance);
            }
            return;
        }

        if (command.Type == "stop")
        {
            foreach (var gameObject in OwnedObjects(world, command, ids, ownershipAlreadyChecked))
            {
                gameObject.Combat?.ClearOrder();
            }
            return;
        }

        if (command.Type == "attack_move")
        {
            var goal = new FixedVector2(command.GetFixed("x"), command.GetFixed("y"));
            foreach (var gameObject in OwnedObjects(world, command, ids, ownershipAlreadyChecked))
            {
                if (gameObject.Combat == null) continue;
                gameObject.Combat.OrderKind = CombatOrderKind.AttackMove;
                gameObject.Combat.AttackMoveGoal = goal;
                gameObject.Combat.HasAttackMoveGoal = true;
                gameObject.Combat.OrderedTargetId = 0;
                gameObject.Combat.DropTarget();
            }
            return;
        }

        if (command.Type == "attack")
        {
            var targetId = ResolveTargetId(world, checked((int)command.GetLong("target")));
            foreach (var gameObject in OwnedObjects(world, command, ids, ownershipAlreadyChecked))
            {
                if (gameObject.Combat == null) continue;
                gameObject.Combat.OrderKind = CombatOrderKind.Attack;
                gameObject.Combat.OrderedTargetId = targetId;
                gameObject.Combat.EngagedTargetId = targetId;
                gameObject.Combat.HasAttackMoveGoal = false;
                gameObject.Combat.AcquiredAutomatically = false;
                gameObject.Combat.HasFiredAtTarget = false;
                gameObject.Combat.ResetWeaponCycle();
            }
        }
    }

    public void PrepareMovement(SimWorld world)
    {
        ArgumentNullException.ThrowIfNull(world);
        BuildSpatialIndex(world);
        BuildHordeIndex(world);
        _hordeMotions.Clear();

        foreach (var gameObject in world.Objects.Values)
        {
            var state = gameObject.Combat;
            if (state == null || gameObject.IsDead || gameObject.IsDying || gameObject.IsUnderConstruction)
            {
                continue;
            }
            var weapon = WeaponFor(gameObject);
            if (weapon == null) continue;
            RefreshTarget(world, gameObject, state, weapon);
            if (state.EngagedTargetId != 0
                && world.Objects.TryGetValue(state.EngagedTargetId, out var target)
                && IsHostileTarget(gameObject, target))
            {
                PrepareTargetMovement(world, gameObject, target, state, weapon);
            }
            else if (state.OrderKind == CombatOrderKind.AttackMove && state.HasAttackMoveGoal)
            {
                RequestMovement(world, gameObject, state.AttackMoveGoal, stop: false, resume: true);
            }
        }
        ApplyHordeMovement(world);
    }

    public void Resolve(SimWorld world)
    {
        ArgumentNullException.ThrowIfNull(world);
        foreach (var attacker in world.Objects.Values)
        {
            var state = attacker.Combat;
            if (state == null) continue;
            ResolvePendingImpacts(world, attacker, state);
            if (attacker.IsDead || attacker.IsDying || attacker.IsUnderConstruction) continue;
            var weapon = WeaponFor(attacker);
            if (weapon == null || state.EngagedTargetId == 0) continue;
            if (!world.Objects.TryGetValue(state.EngagedTargetId, out var target)
                || !IsHostileTarget(attacker, target)
                || !IsWithinWeaponRange(attacker, target, weapon))
            {
                if (state.CyclePhase == WeaponCyclePhase.PreAttack) state.ResetWeaponCycle();
                continue;
            }
            ResolveWeaponCycle(world, attacker, target, state, weapon);
        }
    }

    private void RefreshTarget(
        SimWorld world,
        GameObject attacker,
        CombatState state,
        WeaponTemplate weapon)
    {
        if (state.EngagedTargetId != 0
            && world.Objects.TryGetValue(state.EngagedTargetId, out var current)
            && IsHostileTarget(attacker, current))
        {
            return;
        }
        state.DropTarget();
        if (state.OrderKind == CombatOrderKind.Attack)
        {
            if (world.Objects.TryGetValue(state.OrderedTargetId, out var ordered)
                && IsHostileTarget(attacker, ordered))
            {
                state.EngagedTargetId = ordered.Id;
            }
            else
            {
                state.ClearOrder();
            }
            return;
        }

        var range = weapon.AttackRange;
        if (state.Stance == UnitStance.Aggressive) range += AggressiveAcquireMargin;
        else if (state.OrderKind == CombatOrderKind.AttackMove) range += AttackMoveAcquireMargin;
        if (state.OrderKind == CombatOrderKind.None && !IdleAcquisitionAllowed(world, attacker)) return;
        var targetId = FindNearestEnemy(attacker, range);
        if (targetId != 0)
        {
            state.EngagedTargetId = targetId;
            state.AcquiredAutomatically = true;
        }
    }

    private void PrepareTargetMovement(
        SimWorld world,
        GameObject attacker,
        GameObject target,
        CombatState state,
        WeaponTemplate weapon)
    {
        if (IsWithinWeaponRange(attacker, target, weapon))
        {
            RequestMovement(world, attacker, target.Position, stop: true, resume: false);
            return;
        }
        var pursue = state.Stance != UnitStance.HoldGround
            && (state.OrderKind is CombatOrderKind.Attack or CombatOrderKind.AttackMove
                || state.Stance == UnitStance.Aggressive);
        RequestMovement(world, attacker, target.Position, stop: !pursue, resume: false);
    }

    private void RequestMovement(
        SimWorld world,
        GameObject gameObject,
        FixedVector2 destination,
        bool stop,
        bool resume)
    {
        if (_memberHordes.TryGetValue(gameObject.Id, out var horde))
        {
            if (!_hordeMotions.TryGetValue(horde.Id, out var motion))
            {
                motion = new HordeCombatMotion(horde);
                _hordeMotions.Add(horde.Id, motion);
            }
            if (stop) motion.Stop = true;
            else if (!motion.HasTarget || !resume)
            {
                motion.Destination = destination;
                motion.HasTarget = !resume;
                motion.HasDestination = true;
            }
            return;
        }
        SetObjectMovement(gameObject, destination, stop);
    }

    private void ApplyHordeMovement(SimWorld world)
    {
        foreach (var motion in _hordeMotions.Values)
        {
            if (motion.Stop)
            {
                world.Movement.StopHorde(motion.Horde.Id);
            }
            else if (motion.HasDestination)
            {
                world.Movement.SetHordeOrder(
                    world,
                    motion.Horde,
                    motion.Destination,
                    MoveOrderKind.AttackMove);
            }
        }
    }

    private static void SetObjectMovement(GameObject gameObject, FixedVector2 destination, bool stop)
    {
        if (gameObject.FindModule<LocomotorModule>() is { } locomotor)
        {
            if (stop) locomotor.ClearOrder();
            else locomotor.SetOrder(destination, MoveOrderKind.Move);
            return;
        }
        if (gameObject.FindModule<LinearMoverModule>() is { } linear)
        {
            if (stop) linear.ClearDestination();
            else linear.SetDestination(destination);
        }
    }

    private void ResolveWeaponCycle(
        SimWorld world,
        GameObject attacker,
        GameObject target,
        CombatState state,
        WeaponTemplate weapon)
    {
        if (state.CyclePhase != WeaponCyclePhase.Ready)
        {
            if (state.CycleTicksRemaining > 1)
            {
                state.CycleTicksRemaining--;
                return;
            }
            if (state.CyclePhase == WeaponCyclePhase.Reload) state.ShotsInClip = 0;
            if (state.CyclePhase == WeaponCyclePhase.PreAttack)
            {
                state.CyclePhase = WeaponCyclePhase.Ready;
                Fire(world, attacker, target, state, weapon);
                return;
            }
            state.CyclePhase = WeaponCyclePhase.Ready;
            state.CycleTicksRemaining = 0;
        }

        var needsPreAttack = weapon.PreAttackDelayTicks > 0
            && (weapon.PreAttackType == PreAttackType.PER_SHOT || state.ShotsInClip == 0);
        if (needsPreAttack)
        {
            state.CyclePhase = WeaponCyclePhase.PreAttack;
            state.CycleTicksRemaining = weapon.PreAttackDelayTicks;
            return;
        }
        Fire(world, attacker, target, state, weapon);
    }

    private void Fire(
        SimWorld world,
        GameObject attacker,
        GameObject target,
        CombatState state,
        WeaponTemplate weapon)
    {
        var eventName = weapon.Projectile == null
            ? weapon.Name
            : $"{weapon.Name}:projectile:{weapon.Projectile.ProjectileTemplateName}";
        world.RaiseEvent(new SimEvent("fire", attacker.Id, target.Id, Name: eventName));
        state.HasFiredAtTarget = true;
        for (var index = 0; index < weapon.DamageNuggets.Count; index++)
        {
            var nugget = weapon.DamageNuggets[index];
            if (nugget.DelayTicks > 0)
            {
                state.PendingImpacts.Add(new PendingDamageImpact(
                    target.Id, target.Position, weapon.Name, index, nugget.DelayTicks));
            }
            else
            {
                ApplyNugget(
                    world, attacker.Id, attacker.Team, target.Id, target.Position, nugget,
                    index == 0 ? weapon.MetaImpact : null);
            }
        }
        state.ShotsInClip++;
        if (weapon.ClipSize > 0 && state.ShotsInClip >= weapon.ClipSize)
        {
            state.CyclePhase = weapon.ClipReloadTimeTicks > 0
                ? WeaponCyclePhase.Reload
                : WeaponCyclePhase.Ready;
            state.CycleTicksRemaining = weapon.ClipReloadTimeTicks;
        }
        else
        {
            var delay = Math.Max(weapon.DelayBetweenShotsTicks, weapon.FiringDurationTicks);
            state.CyclePhase = delay > 0 ? WeaponCyclePhase.BetweenShots : WeaponCyclePhase.Ready;
            state.CycleTicksRemaining = delay;
        }
    }

    private void ResolvePendingImpacts(SimWorld world, GameObject attacker, CombatState state)
    {
        for (var index = 0; index < state.PendingImpacts.Count;)
        {
            var impact = state.PendingImpacts[index];
            if (impact.TicksRemaining > 1)
            {
                state.PendingImpacts[index] = impact with { TicksRemaining = impact.TicksRemaining - 1 };
                index++;
                continue;
            }
            state.PendingImpacts.RemoveAt(index);
            if (!_config.WeaponTemplates.TryGetValue(impact.WeaponName, out var weapon)
                || (uint)impact.NuggetIndex >= (uint)weapon.DamageNuggets.Count)
            {
                continue;
            }
            ApplyNugget(
                world,
                attacker.Id,
                attacker.Team,
                impact.TargetId,
                impact.ImpactPoint,
                weapon.DamageNuggets[impact.NuggetIndex],
                impact.NuggetIndex == 0 ? weapon.MetaImpact : null);
        }
    }

    private void ApplyNugget(
        SimWorld world,
        int attackerId,
        int attackerTeam,
        int targetId,
        FixedVector2 impactPoint,
        DamageNugget nugget,
        MetaImpactNugget? metaImpact)
    {
        if (nugget.Radius <= Fixed64.Zero)
        {
            if (world.Objects.TryGetValue(targetId, out var target)
                && (nugget.FriendlyFire || target.Team != attackerTeam))
            {
                ApplyDamage(world, attackerId, target, impactPoint, nugget, metaImpact);
            }
            return;
        }
        var radiusSquared = nugget.Radius * nugget.Radius;
        foreach (var target in world.Objects.Values)
        {
            if (target.IsDead || target.IsDying || (!nugget.FriendlyFire && target.Team == attackerTeam)) continue;
            if (target.Position.DistanceSquaredTo(impactPoint) <= radiusSquared)
            {
                ApplyDamage(world, attackerId, target, impactPoint, nugget, metaImpact);
            }
        }
    }

    private void ApplyDamage(
        SimWorld world,
        int attackerId,
        GameObject target,
        FixedVector2 impactPoint,
        DamageNugget nugget,
        MetaImpactNugget? metaImpact)
    {
        var amount = nugget.Damage * ArmorMultiplier(target, nugget.DamageType);
        var applied = world.ApplyCombatDamage(target, amount, nugget.DamageType);
        if (applied > Fixed64.Zero)
        {
            world.RaiseEvent(new SimEvent("damage", attackerId, target.Id, applied));
            var impulseOrigin = nugget.Radius > Fixed64.Zero
                ? impactPoint
                : world.Objects.TryGetValue(attackerId, out var attacker)
                    ? attacker.Position
                    : impactPoint;
            ApplyMetaImpact(target, impulseOrigin, metaImpact);
        }
    }

    internal bool FireScriptedWeapon(SimWorld world, GameObject attacker, string weaponName)
    {
        if (!_config.WeaponTemplates.TryGetValue(weaponName, out var weapon)) return false;
        var targetId = FindNearestEnemy(attacker, weapon.AttackRange);
        var impactPoint = targetId != 0 && world.Objects.TryGetValue(targetId, out var target)
            ? target.Position : attacker.Position;
        world.RaiseEvent(new SimEvent("fire", attacker.Id, targetId == 0 ? null : targetId, Name: weaponName));
        for (var index = 0; index < weapon.DamageNuggets.Count; index++)
            ApplyNugget(world, attacker.Id, attacker.Team, targetId, impactPoint,
                weapon.DamageNuggets[index], index == 0 ? weapon.MetaImpact : null);
        return true;
    }

    private static void ApplyMetaImpact(
        GameObject target,
        FixedVector2 impactPoint,
        MetaImpactNugget? metaImpact)
    {
        if (metaImpact == null || metaImpact.Amount <= Fixed64.Zero
            || target.FindModule<PhysicsBehaviorModule>() is not { } physics) return;
        var force = metaImpact.Amount;
        if (metaImpact.Radius > Fixed64.Zero)
        {
            var distance = Fixed64.Sqrt(target.Position.DistanceSquaredTo(impactPoint));
            if (distance > metaImpact.Radius) return;
            var falloff = Fixed64.Clamp(
                Fixed64.One - distance / metaImpact.Radius * metaImpact.TaperOff,
                Fixed64.Zero,
                Fixed64.One);
            force *= falloff;
        }
        physics.ApplyKnockback(impactPoint, target, force);
    }

    private bool IdleAcquisitionAllowed(SimWorld world, GameObject attacker)
    {
        if (attacker.FindModule<AIUpdateInterfaceModule>() is { } ai)
            return ai.ShouldAutoAcquire(world, attacker);
        if (_memberHordes.TryGetValue(attacker.Id, out var horde)
            && world.Objects.TryGetValue(horde.Id, out var carrier)
            && carrier.FindModule<HordeAIUpdateModule>() is { } hordeAi)
            return hordeAi.ShouldAutoAcquire(world);
        return true;
    }

    private Fixed64 ArmorMultiplier(GameObject target, DamageType damageType)
    {
        var set = SelectArmorSet(target.Template.ArmorSets, target.Combat?.Conditions);
        if (set == null || !_config.ArmorTemplates.TryGetValue(set.ArmorName, out var armor)) return Fixed64.One;
        return armor.MultiplierFor(damageType);
    }

    private WeaponTemplate? WeaponFor(GameObject gameObject)
    {
        var set = SelectWeaponSet(gameObject.Template.WeaponSets, gameObject.Combat?.Conditions);
        var name = set?.PrimaryWeaponName;
        return name != null && _config.WeaponTemplates.TryGetValue(name, out var weapon) ? weapon : null;
    }

    private static WeaponSet? SelectWeaponSet(
        IReadOnlyList<WeaponSet> sets,
        IReadOnlySet<string>? conditions)
    {
        conditions ??= EmptyConditions.Instance;
        foreach (var set in sets)
        {
            if (set.Matches(conditions)) return set;
        }
        return sets.FirstOrDefault(set => set.Conditions.Count == 0);
    }

    private static ArmorSet? SelectArmorSet(
        IReadOnlyList<ArmorSet> sets,
        IReadOnlySet<string>? conditions)
    {
        conditions ??= EmptyConditions.Instance;
        foreach (var set in sets)
        {
            if (set.Matches(conditions)) return set;
        }
        return sets.FirstOrDefault(set => set.Conditions.Count == 0);
    }

    private int FindNearestEnemy(GameObject attacker, Fixed64 range)
    {
        var centerX = attacker.Position.X.ToIntFloor();
        var centerY = attacker.Position.Y.ToIntFloor();
        var radiusCells = checked((int)((range.Raw + Fixed64.OneRaw - 1) >> Fixed64.FractionBits));
        var minCellX = FloorDivide(centerX - radiusCells, SpatialCellSize);
        var maxCellX = FloorDivide(centerX + radiusCells, SpatialCellSize);
        var minCellY = FloorDivide(centerY - radiusCells, SpatialCellSize);
        var maxCellY = FloorDivide(centerY + radiusCells, SpatialCellSize);
        var rangeSquared = range * range;
        var bestDistance = rangeSquared;
        var bestId = 0;
        for (var cellX = minCellX; cellX <= maxCellX; cellX++)
        {
            for (var cellY = minCellY; cellY <= maxCellY; cellY++)
            {
                if (!_spatial.TryGetValue((cellX, cellY), out var candidates)) continue;
                foreach (var candidate in candidates)
                {
                    if (!IsHostileTarget(attacker, candidate)) continue;
                    var distance = attacker.Position.DistanceSquaredTo(candidate.Position);
                    if (distance > rangeSquared) continue;
                    if (bestId == 0 || distance < bestDistance || (distance == bestDistance && candidate.Id < bestId))
                    {
                        bestDistance = distance;
                        bestId = candidate.Id;
                    }
                }
            }
        }
        return bestId;
    }

    private void BuildSpatialIndex(SimWorld world)
    {
        _spatial.Clear();
        foreach (var gameObject in world.Objects.Values)
        {
            if (!world.IsAttackable(gameObject)) continue;
            var key = (
                FloorDivide(gameObject.Position.X.ToIntFloor(), SpatialCellSize),
                FloorDivide(gameObject.Position.Y.ToIntFloor(), SpatialCellSize));
            if (!_spatial.TryGetValue(key, out var cell))
            {
                cell = new List<GameObject>();
                _spatial.Add(key, cell);
            }
            cell.Add(gameObject);
        }
    }

    private void BuildHordeIndex(SimWorld world)
    {
        _memberHordes.Clear();
        foreach (var horde in world.Hordes)
        {
            foreach (var memberId in horde.Members)
            {
                _memberHordes.TryAdd(memberId, horde);
            }
        }
    }

    private IEnumerable<GameObject> OwnedObjects(
        SimWorld world,
        SimCommand command,
        IReadOnlyList<long> ids,
        bool ownershipAlreadyChecked)
    {
        var emitted = new SortedSet<int>();
        foreach (var longId in ids)
        {
            if (longId is < 1 or > int.MaxValue) continue;
            var id = (int)longId;
            var horde = world.FindHordeForCombat(id);
            if (horde != null)
            {
                if (horde.Owner != command.Team)
                {
                    if (!ownershipAlreadyChecked) world.RecordCombatOwnershipDiagnostic(command, id, horde.Owner);
                    continue;
                }
                foreach (var memberId in horde.Members)
                {
                    if (emitted.Add(memberId) && world.Objects.TryGetValue(memberId, out var member)) yield return member;
                }
                if (emitted.Add(id) && world.Objects.TryGetValue(id, out var carrier)) yield return carrier;
                continue;
            }
            if (!world.Objects.TryGetValue(id, out var gameObject))
            {
                if (!ownershipAlreadyChecked) world.RecordCombatDiagnostic(command, id, "unknown_object", $"object {id} does not exist");
                continue;
            }
            if (gameObject.Team != command.Team)
            {
                if (!ownershipAlreadyChecked) world.RecordCombatOwnershipDiagnostic(command, id, gameObject.Team);
                continue;
            }
            if (emitted.Add(id)) yield return gameObject;
        }
    }

    private static IReadOnlyList<long> ObjectIds(SimCommand command)
    {
        if (command.Args.TryGetValue("objects", out var objects) && objects.Kind == CommandValueKind.LongList)
            return objects.LongListValue!;
        if (command.Args.TryGetValue("object", out var single) && single.Kind == CommandValueKind.Long)
            return new[] { single.LongValue };
        if (command.Args.TryGetValue("id", out var legacy) && legacy.Kind == CommandValueKind.Long)
            return new[] { legacy.LongValue };
        throw new KeyNotFoundException($"Command '{command.Type}' has no object target");
    }

    private static int ResolveTargetId(SimWorld world, int id)
    {
        var horde = world.FindHordeForCombat(id);
        if (horde == null) return id;
        foreach (var memberId in horde.Members)
        {
            if (world.Objects.TryGetValue(memberId, out var member) && world.IsAttackable(member)) return memberId;
        }
        return id;
    }

    private static bool IsHostileTarget(GameObject attacker, GameObject target) =>
        attacker.Team != target.Team && !target.IsDead && !target.IsDying
        && target.FindModule<InvisibilityUpdateModule>()?.IsInvisible != true;

    private static bool IsWithinWeaponRange(GameObject attacker, GameObject target, WeaponTemplate weapon)
    {
        var distance = attacker.Position.DistanceSquaredTo(target.Position);
        return distance <= weapon.AttackRange * weapon.AttackRange
            && distance >= weapon.MinimumAttackRange * weapon.MinimumAttackRange;
    }

    private static int FloorDivide(int value, int divisor) =>
        value >= 0 ? value / divisor : -checked((-value + divisor - 1) / divisor);

    private static bool TryParseStance(string value, out UnitStance stance)
    {
        stance = value.Trim().ToLowerInvariant() switch
        {
            "aggressive" => UnitStance.Aggressive,
            "battle" => UnitStance.Battle,
            "hold_ground" => UnitStance.HoldGround,
            _ => 0,
        };
        return stance != 0;
    }

    private sealed class HordeCombatMotion
    {
        public HordeCombatMotion(SnapshotHorde horde) => Horde = horde;
        public SnapshotHorde Horde { get; }
        public FixedVector2 Destination { get; set; }
        public bool Stop { get; set; }
        public bool HasTarget { get; set; }
        public bool HasDestination { get; set; }
    }

    private sealed class EmptyConditions : IReadOnlySet<string>
    {
        public static readonly EmptyConditions Instance = new();
        public int Count => 0;
        public bool Contains(string item) => false;
        public IEnumerator<string> GetEnumerator() => Enumerable.Empty<string>().GetEnumerator();
        System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => GetEnumerator();
        public bool IsProperSubsetOf(IEnumerable<string> other) => false;
        public bool IsProperSupersetOf(IEnumerable<string> other) => false;
        public bool IsSubsetOf(IEnumerable<string> other) => true;
        public bool IsSupersetOf(IEnumerable<string> other) => !other.Any();
        public bool Overlaps(IEnumerable<string> other) => false;
        public bool SetEquals(IEnumerable<string> other) => !other.Any();
    }
}
