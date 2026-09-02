namespace OpenBfme.Sim;

/// <summary>
/// Deterministic, data-driven skirmish planner. It observes immutable bundle
/// templates plus authoritative world state and emits command-v1 commands via
/// the same seat path used by a human player.
/// </summary>
internal static class AiPlanner
{
    private static readonly Fixed64 ThreatRadius = Fixed64.FromInt(80);

    public static void Plan(SimWorld world, AiPlayerState state, int tick)
    {
        state.LastPlanTick = tick;
        UpdateIncome(world, state, tick);
        var view = Observe(world, state);
        var resources = world.TeamResources(state.Team);
        var budget = state.Handicap == Fixed64.One
            ? resources
            : EconomyTemplate.ScaleInteger(resources, state.Handicap);
        var commands = new List<SimCommand>();

        PlanEconomy(world, state, tick, view, commands, ref budget);
        PlanProduction(world, state, tick, view, commands, ref budget);
        PlanTech(world, state, tick, view, commands, ref budget);
        PlanArmy(world, state, tick, view, commands);

        if (!world.SubmitAiCommands(state, tick, commands))
            throw new InvalidOperationException($"AI player {state.PlayerIndex} could not submit commands for tick {tick}");
        state.PreviousArmyHealth = view.ArmyHealth;
        state.PreviousEnemyHealth = view.EnemyArmyHealth;
    }

    private static void UpdateIncome(SimWorld world, AiPlayerState state, int tick)
    {
        var resources = world.TeamResources(state.Team);
        if (state.IncomeSampleTick != 0)
        {
            var elapsed = tick - state.IncomeSampleTick;
            var gained = Math.Max(0, resources - state.LastResources);
            state.IncomeRate = elapsed > 0 ? gained * 1_000 / checked(elapsed * world.TickMilliseconds) : 0;
        }
        state.LastResources = resources;
        state.IncomeSampleTick = tick;
    }

    private static AiWorldView Observe(SimWorld world, AiPlayerState state)
    {
        var ownedStructures = new List<GameObject>();
        var enemyStructures = new List<GameObject>();
        var enemyBuilders = new List<GameObject>();
        var ownedArmy = new List<GameObject>();
        var enemyArmy = new List<GameObject>();
        var hordeMembers = new HashSet<int>();
        var hordeCarriers = new HashSet<int>();
        foreach (var horde in world.Hordes)
        {
            hordeCarriers.Add(horde.Id);
            foreach (var member in horde.Members) hordeMembers.Add(member);
        }

        foreach (var gameObject in world.Objects.Values)
        {
            if (gameObject.IsDead || gameObject.IsDying) continue;
            var roles = AiTemplateRoles.Classify(gameObject.Template);
            var own = gameObject.Team == state.Team;
            if ((roles & AiUnitRole.Structure) != 0)
            {
                (own ? ownedStructures : enemyStructures).Add(gameObject);
                continue;
            }
            if (!own && IsBuilder(gameObject.Template))
            {
                enemyBuilders.Add(gameObject);
                continue;
            }
            if (gameObject.IsUnderConstruction) continue;
            if (hordeMembers.Contains(gameObject.Id)) continue;
            if (hordeCarriers.Contains(gameObject.Id) || IsArmyObject(gameObject, roles))
                (own ? ownedArmy : enemyArmy).Add(gameObject);
        }

        var rally = Centroid(ownedStructures, state.RallyPoint);
        state.RallyPoint = rally;
        var threat = FindThreat(ownedStructures, enemyArmy);
        return new AiWorldView(
            ownedStructures,
            enemyStructures,
            enemyBuilders,
            ownedArmy,
            enemyArmy,
            CountEconomy(ownedStructures),
            ArmyStrength(world, ownedArmy),
            ArmyStrength(world, enemyArmy),
            ArmyHealth(world, ownedArmy),
            ArmyHealth(world, enemyArmy),
            ArmyMaximumHealth(world, ownedArmy),
            Composition(ownedArmy),
            Composition(enemyArmy),
            rally,
            threat,
            Centroid(enemyArmy, rally));
    }

    private static void PlanEconomy(
        SimWorld world,
        AiPlayerState state,
        int tick,
        AiWorldView view,
        ICollection<SimCommand> commands,
        ref long budget)
    {
        var missing = Math.Max(0, state.EconomyTarget - view.EconomyStructures);
        var score = missing == 0 ? 0 : 1_000L + missing * 100L;
        world.RecordAiDecision(state, tick, "economy", score,
            $"owned={view.EconomyStructures} target={state.EconomyTarget} income={state.IncomeRate}");
        if (missing == 0) return;
        if (!TrySelectBuild(world, state, view, AiBuildCategory.Economy, budget, out var build)) return;
        commands.Add(BuildCommand(state, tick, build));
        budget -= build.Template.Economy.BuildCost;
        state.Phase = AiPhase.Economy;
        world.RecordAiDecision(state, tick, "build_resource", score + 250,
            $"base={build.Base.Id} plot={build.Plot.Index} template={build.Template.Name}");
    }

    private static void PlanProduction(
        SimWorld world,
        AiPlayerState state,
        int tick,
        AiWorldView view,
        ICollection<SimCommand> commands,
        ref long budget)
    {
        var producers = world.Objects.Values
            .Where(value => value.Team == state.Team && !value.IsDead && !value.IsDying
                && value.FindModule<ProductionModule>() != null)
            .ToArray();
        var used = world.CommandPointsUsed(state.Team);
        var maximum = world.CommandPointsMaximum(state.Team);
        world.RecordAiDecision(state, tick, "production",
            producers.Length == 0 || (maximum > 0 && used >= maximum) ? 0 : 700,
            $"producers={producers.Length} command_points={used}:{maximum}");
        var hasTrainCapableProducer = false;
        var plannedCommandPoints = 0L;
        foreach (var producer in producers)
        {
            var production = producer.FindModule<ProductionModule>()!;
            var candidates = AuthorizedObjects(world, state, producer, "UNIT_BUILD", AiBuildCategory.Unit)
                .Where(template => (AiTemplateRoles.Classify(template) & AiUnitRole.Structure) == 0)
                .Where(world.CanSpawnProductionTemplate)
                .ToArray();
            if (candidates.Length == 0) continue;
            hasTrainCapableProducer = true;
            if (producer.IsUnderConstruction || production.QueueLength != 0) continue;
            var selected = SelectUnit(world, state, view, candidates, budget);
            if (selected == null) continue;
            var cost = selected.Economy.BuildCost > 0
                ? selected.Economy.BuildCost
                : production.CostOf(selected.Name);
            if (cost > budget
                || !world.CanReserveCommandPoints(
                    state.Team,
                    checked(plannedCommandPoints + selected.Economy.CommandPoints))) continue;
            commands.Add(Command(state, tick, "train",
                ("objects", CommandValue.OfLongList(new long[] { producer.Id })),
                ("template", CommandValue.OfString(selected.Name)),
                ("count", CommandValue.OfLong(1))));
            budget -= cost;
            plannedCommandPoints = checked(plannedCommandPoints + selected.Economy.CommandPoints);
            world.RecordAiDecision(state, tick, "train", UnitUtility(view, selected),
                $"producer={producer.Id} template={selected.Name}");
        }
        if (!hasTrainCapableProducer && view.EconomyStructures >= state.EconomyTarget
            && TrySelectBuild(world, state, view, AiBuildCategory.Producer, budget, out var build))
        {
            commands.Add(BuildCommand(state, tick, build));
            budget -= build.Template.Economy.BuildCost;
            world.RecordAiDecision(state, tick, "build_producer", 900,
                $"base={build.Base.Id} plot={build.Plot.Index} template={build.Template.Name}");
        }
    }

    private static void PlanTech(
        SimWorld world,
        AiPlayerState state,
        int tick,
        AiWorldView view,
        ICollection<SimCommand> commands,
        ref long budget)
    {
        foreach (var producer in view.Structures)
        {
            if (producer.FindModule<ProductionModule>() is not { QueueLength: 0 }) continue;
            var set = CommandSet(world, producer);
            if (set == null) continue;
            foreach (var entry in set.Entries)
            {
                var button = entry.Button;
                if (button.Command is "PLAYER_UPGRADE" or "OBJECT_UPGRADE"
                    && world.AiConfig.Tech.Upgrades.TryGetValue(button.Upgrade, out var upgrade)
                    && !world.ObjectHasUpgrade(producer, upgrade.Name)
                    && !world.IsUpgradeInProgress(producer, upgrade)
                    && upgrade.Prerequisites.All(name => !world.HasUpgradeTemplate(name)
                        || world.ObjectHasUpgrade(producer, name))
                    && upgrade.BuildCost <= budget
                    && (state.IncomeRate > 0 || budget >= upgrade.BuildCost * 2))
                {
                    commands.Add(Command(state, tick, "upgrade",
                        ("objects", CommandValue.OfLongList(new long[] { producer.Id })),
                        ("name", CommandValue.OfString(upgrade.Name))));
                    budget -= upgrade.BuildCost;
                    world.RecordAiDecision(state, tick, "upgrade", 500 + state.IncomeRate,
                        $"producer={producer.Id} name={upgrade.Name}");
                    return;
                }
                if (button.Command == "PURCHASE_SCIENCE"
                    && world.AiConfig.Tech.Sciences.TryGetValue(button.Science, out var science)
                    && science.IsGrantable
                    && !world.TeamHasScience(state.Team, science.Name)
                    && science.PrerequisiteSciences.All(name => world.TeamHasScience(state.Team, name))
                    && world.TeamPowerPoints(state.Team) >= science.PurchasePointCost)
                {
                    commands.Add(Command(state, tick, "power",
                        ("objects", CommandValue.OfLongList(new long[] { producer.Id })),
                        ("name", CommandValue.OfString(science.Name))));
                    world.RecordAiDecision(state, tick, "science", 450 + science.PurchasePointCost,
                        $"issuer={producer.Id} name={science.Name}");
                    return;
                }
            }
        }
        world.RecordAiDecision(state, tick, "tech", 0, "no affordable authorized upgrade or science");
    }

    private static void PlanArmy(
        SimWorld world,
        AiPlayerState state,
        int tick,
        AiWorldView view,
        ICollection<SimCommand> commands)
    {
        var ids = view.Army.Select(value => (long)value.Id).ToArray();
        var healthPercent = view.ArmyMaximumHealth == 0
            ? 100L
            : view.ArmyHealth * 100 / view.ArmyMaximumHealth;
        var ownLoss = Math.Max(0, state.PreviousArmyHealth - view.ArmyHealth);
        var enemyLoss = Math.Max(0, state.PreviousEnemyHealth - view.EnemyArmyHealth);
        var retreat = state.PreviousArmyHealth > 0
            && healthPercent < state.RetreatHealthPercent
            && ownLoss > enemyLoss;
        world.RecordAiDecision(state, tick, "retreat", retreat ? 1_200 + ownLoss - enemyLoss : 0,
            $"health_percent={healthPercent} exchange={enemyLoss - ownLoss}");
        if (retreat && ids.Length > 0)
        {
            commands.Add(MoveCommand(state, tick, "move", ids, view.RallyPoint));
            state.Phase = AiPhase.Retreat;
            state.Retreating = true;
            return;
        }

        if (view.Threat != null && ids.Length > 0)
        {
            var score = 1_000L + Math.Max(0, view.EnemyStrength - view.ArmyStrength);
            commands.Add(MoveCommand(state, tick, "attack_move", ids, view.Threat.Position));
            state.Phase = AiPhase.Defend;
            state.Retreating = false;
            state.TargetObjectId = view.Threat.Id;
            world.RecordAiDecision(state, tick, "defend", score, $"target={view.Threat.Id}");
            return;
        }
        world.RecordAiDecision(state, tick, "defend", 0, "no enemy inside structure threat radius");

        var used = world.CommandPointsUsed(state.Team);
        var maximum = world.CommandPointsMaximum(state.Team);
        var aboveMargin = view.ArmyStrength > 0
            && view.ArmyStrength * 100 >= Math.Max(1, view.EnemyStrength) * state.AttackMarginPercent;
        var aboveThreshold = maximum == 0 || used * 100 >= maximum * state.AttackCommandPointPercent;
        var strategicTargets = view.EnemyBuilders.Count > 0
            ? view.EnemyBuilders
            : view.EnemyStructures;
        var attackTarget = strategicTargets.FirstOrDefault();
        var attackScore = aboveMargin && aboveThreshold && attackTarget != null
            ? 800L + view.ArmyStrength - view.EnemyStrength
            : 0;
        world.RecordAiDecision(state, tick, "attack", attackScore,
            $"strength={view.ArmyStrength}:{view.EnemyStrength} cp={used}:{maximum} margin={state.AttackMarginPercent}");
        if (attackScore > 0 && ids.Length > 0)
        {
            attackTarget = Nearest(view.RallyPoint, strategicTargets);
            commands.Add(MoveCommand(state, tick, "attack_move", ids, attackTarget!.Position));
            state.Phase = AiPhase.Attack;
            state.Retreating = false;
            state.TargetObjectId = attackTarget.Id;
            return;
        }
        if (ids.Length > 0)
        {
            commands.Add(MoveCommand(state, tick, "move", ids, view.RallyPoint));
            state.Phase = AiPhase.Regroup;
            state.Retreating = false;
            world.RecordAiDecision(state, tick, "rally", 100, $"x={view.RallyPoint.X.Raw} y={view.RallyPoint.Y.Raw}");
        }
    }

    private static bool TrySelectBuild(
        SimWorld world,
        AiPlayerState state,
        AiWorldView view,
        AiBuildCategory category,
        long budget,
        out BuildChoice choice)
    {
        var options = new List<BuildChoice>();
        foreach (var plot in world.BuildPlots)
        {
            if (plot.OccupantObjectId != 0 || !world.Objects.TryGetValue(plot.BaseObjectId, out var baseObject)
                || baseObject.Team != state.Team || baseObject.IsDead || baseObject.IsDying) continue;
            var candidates = AuthorizedBuildObjects(world, state, baseObject, category);
            foreach (var template in candidates)
            {
                var roles = AiTemplateRoles.Classify(template);
                var wanted = category == AiBuildCategory.Economy
                    ? (roles & AiUnitRole.Economy) != 0
                    : (roles & AiUnitRole.Structure) != 0
                        && (roles & AiUnitRole.Economy) == 0
                        && template.Modules.Any(module => module.TypeName == ProductionModule.TypeName);
                if (!wanted || template.Economy.BuildCost > budget
                    || !template.Modules.Any(module => module.TypeName == GettingBuiltModule.TypeName)) continue;
                var kind = template.Economy.BuildKind.Length == 0 ? template.Name : template.Economy.BuildKind;
                if (plot.AllowedKinds.Count > 0
                    && !plot.AllowedKinds.Contains(kind, StringComparer.Ordinal)
                    && !plot.AllowedKinds.Contains(template.Name, StringComparer.Ordinal)) continue;
                options.Add(new BuildChoice(baseObject, plot, template));
            }
        }
        if (options.Count == 0)
        {
            choice = null!;
            return false;
        }
        var cheapest = options.Min(value => value.Template.Economy.BuildCost);
        var tied = options.Where(value => value.Template.Economy.BuildCost == cheapest)
            .OrderBy(value => value.Template.Name, StringComparer.Ordinal)
            .ThenBy(value => value.Base.Id)
            .ThenBy(value => value.Plot.Index)
            .ToArray();
        choice = tied.Length == 1 ? tied[0] : tied[state.NextTieIndex(world, (uint)tied.Length)];
        return true;
    }

    private static IEnumerable<ObjectTemplate> AuthorizedBuildObjects(
        SimWorld world,
        AiPlayerState state,
        GameObject issuer,
        AiBuildCategory category)
    {
        var authorized = AuthorizedObjects(world, state, issuer, "DOZER_CONSTRUCT", category).ToArray();
        return authorized;
    }

    private static IEnumerable<ObjectTemplate> AuthorizedObjects(
        SimWorld world,
        AiPlayerState state,
        GameObject issuer,
        string command,
        AiBuildCategory category)
    {
        var names = new List<string>();
        var set = CommandSet(world, issuer);
        var commandSetAuthorized = set != null;
        if (set != null)
        {
            foreach (var entry in set.Entries)
                if (entry.Button.Command == command && entry.Button.Object.Length > 0)
                    names.Add(entry.Button.Object);
        }
        else
        {
            names.AddRange(issuer.Template.Economy.CommandSet);
        }
        names = names.Distinct(StringComparer.Ordinal).OrderBy(value => value, StringComparer.Ordinal).ToList();
        if (world.AiConfig.AiBuildLists != null)
            names = world.AiConfig.AiBuildLists.OrderCandidates(state.Faction, category, names).ToList();
        foreach (var name in names)
            if (world.AiConfig.Templates.TryGetValue(name, out var template)
                && (commandSetAuthorized || AiTemplateRoles.IsSide(template, state.Faction)))
                yield return template;
    }

    private static CommandSetTemplate? CommandSet(SimWorld world, GameObject issuer) =>
        issuer.CurrentCommandSet.Length > 0
        && world.AiConfig.Tech.CommandSets.TryGetValue(issuer.CurrentCommandSet, out var set)
            ? set
            : null;

    private static ObjectTemplate? SelectUnit(
        SimWorld world,
        AiPlayerState state,
        AiWorldView view,
        IReadOnlyList<ObjectTemplate> candidates,
        long budget)
    {
        var affordable = candidates.Where(template => template.Economy.BuildCost <= budget).ToArray();
        if (affordable.Length == 0) return null;
        var best = affordable.Max(template => UnitUtility(view, template));
        var tied = affordable.Where(template => UnitUtility(view, template) == best)
            .OrderBy(template => template.Name, StringComparer.Ordinal).ToArray();
        return tied.Length == 1 ? tied[0] : tied[state.NextTieIndex(world, (uint)tied.Length)];
    }

    private static long UnitUtility(AiWorldView view, ObjectTemplate template)
    {
        var role = AiTemplateRoles.Classify(template);
        var score = 100L;
        if (view.EnemyComposition.Cavalry > 0 && (role & AiUnitRole.Pike) != 0) score += 500;
        else if (view.EnemyComposition.Cavalry > 0 && (role & AiUnitRole.Infantry) != 0) score += 300;
        if (view.EnemyComposition.Archer > 0 && (role & AiUnitRole.Cavalry) != 0) score += 500;
        if (view.EnemyComposition.Infantry > 0 && (role & AiUnitRole.Archer) != 0) score += 500;
        score += BalanceBonus(view.Composition, role);
        score += Math.Max(0, 200 - template.Economy.BuildCost / 10);
        return score;
    }

    private static long BalanceBonus(AiComposition composition, AiUnitRole role)
    {
        var minimum = Math.Min(composition.Infantry, Math.Min(composition.Archer, composition.Cavalry));
        if ((role & AiUnitRole.Pike) != 0 && composition.Pike <= minimum) return 90;
        if ((role & AiUnitRole.Infantry) != 0 && composition.Infantry <= minimum) return 80;
        if ((role & AiUnitRole.Archer) != 0 && composition.Archer <= minimum) return 80;
        if ((role & AiUnitRole.Cavalry) != 0 && composition.Cavalry <= minimum) return 80;
        return 0;
    }

    private static SimCommand BuildCommand(AiPlayerState state, int tick, BuildChoice choice) =>
        Command(state, tick, "build",
            ("objects", CommandValue.OfLongList(new long[] { choice.Base.Id })),
            ("template", CommandValue.OfString(choice.Template.Name)),
            ("index", CommandValue.OfLong(choice.Plot.Index)));

    private static SimCommand MoveCommand(
        AiPlayerState state,
        int tick,
        string type,
        IReadOnlyList<long> ids,
        FixedVector2 destination) =>
        Command(state, tick, type,
            ("objects", CommandValue.OfLongList(ids)),
            ("x", CommandValue.OfFixed(destination.X)),
            ("y", CommandValue.OfFixed(destination.Y)));

    private static SimCommand Command(
        AiPlayerState state,
        int tick,
        string type,
        params (string Name, CommandValue Value)[] args) => new(
            tick,
            state.Team,
            state.CommandSequence++,
            type,
            args.Select(value => new KeyValuePair<string, CommandValue>(value.Name, value.Value)),
            state.Seat);

    private static bool IsArmyObject(GameObject gameObject, AiUnitRole roles) =>
        !AiTemplateRoles.IsNonCombat(gameObject.Template)
        && ((roles & (AiUnitRole.Infantry | AiUnitRole.Pike | AiUnitRole.Archer | AiUnitRole.Cavalry
            | AiUnitRole.Hero | AiUnitRole.Siege)) != 0
        || gameObject.FindModule<LocomotorModule>() != null
        || gameObject.FindModule<LinearMoverModule>() != null);

    private static bool IsBuilder(ObjectTemplate template) => template.KindOf.Any(value =>
        value.Equals("DOZER", StringComparison.OrdinalIgnoreCase)
        || value.Equals("PORTER", StringComparison.OrdinalIgnoreCase));

    private static int CountEconomy(IEnumerable<GameObject> structures) =>
        structures.Count(value => (AiTemplateRoles.Classify(value.Template) & AiUnitRole.Economy) != 0);

    private static long ArmyStrength(SimWorld world, IEnumerable<GameObject> army) =>
        army.Sum(value => Math.Max(1, value.Template.Economy.BuildCost) + Health(world, value));

    private static long ArmyHealth(SimWorld world, IEnumerable<GameObject> army) =>
        army.Sum(value => Health(world, value));

    private static long ArmyMaximumHealth(SimWorld world, IEnumerable<GameObject> army) =>
        army.Sum(value => Math.Max(0, world.AiHealth(value).Maximum.ToIntFloor()));

    private static long Health(SimWorld world, GameObject value) =>
        Math.Max(0, world.AiHealth(value).Health.ToIntFloor());

    private static AiComposition Composition(IEnumerable<GameObject> army)
    {
        var infantry = 0;
        var pike = 0;
        var archer = 0;
        var cavalry = 0;
        foreach (var value in army)
        {
            var role = AiTemplateRoles.Classify(value.Template);
            if ((role & AiUnitRole.Infantry) != 0) infantry++;
            if ((role & AiUnitRole.Pike) != 0) pike++;
            if ((role & AiUnitRole.Archer) != 0) archer++;
            if ((role & AiUnitRole.Cavalry) != 0) cavalry++;
        }
        return new AiComposition(infantry, pike, archer, cavalry);
    }

    private static GameObject? FindThreat(
        IReadOnlyList<GameObject> structures,
        IReadOnlyList<GameObject> enemies)
    {
        var radiusSquared = ThreatRadius * ThreatRadius;
        GameObject? best = null;
        var bestDistance = Fixed64.MaxValue;
        foreach (var enemy in enemies)
        foreach (var structure in structures)
        {
            var distance = enemy.Position.DistanceSquaredTo(structure.Position);
            if (distance > radiusSquared) continue;
            if (best == null || distance < bestDistance || (distance == bestDistance && enemy.Id < best.Id))
            {
                best = enemy;
                bestDistance = distance;
            }
        }
        return best;
    }

    private static GameObject? Nearest(FixedVector2 point, IReadOnlyList<GameObject> objects)
    {
        GameObject? best = null;
        var bestDistance = Fixed64.MaxValue;
        foreach (var value in objects)
        {
            var distance = value.Position.DistanceSquaredTo(point);
            if (best == null || distance < bestDistance || (distance == bestDistance && value.Id < best.Id))
            {
                best = value;
                bestDistance = distance;
            }
        }
        return best;
    }

    private static FixedVector2 Centroid(IReadOnlyList<GameObject> objects, FixedVector2 fallback)
    {
        if (objects.Count == 0) return fallback;
        var x = 0L;
        var y = 0L;
        foreach (var value in objects)
        {
            x = checked(x + value.Position.X.Raw);
            y = checked(y + value.Position.Y.Raw);
        }
        return new FixedVector2(Fixed64.FromRaw(x / objects.Count), Fixed64.FromRaw(y / objects.Count));
    }

    private sealed record BuildChoice(GameObject Base, BuildPlot Plot, ObjectTemplate Template);
    private sealed record AiComposition(int Infantry, int Pike, int Archer, int Cavalry);
    private sealed record AiWorldView(
        IReadOnlyList<GameObject> Structures,
        IReadOnlyList<GameObject> EnemyStructures,
        IReadOnlyList<GameObject> EnemyBuilders,
        IReadOnlyList<GameObject> Army,
        IReadOnlyList<GameObject> EnemyArmy,
        int EconomyStructures,
        long ArmyStrength,
        long EnemyStrength,
        long ArmyHealth,
        long EnemyArmyHealth,
        long ArmyMaximumHealth,
        AiComposition Composition,
        AiComposition EnemyComposition,
        FixedVector2 RallyPoint,
        GameObject? Threat,
        FixedVector2 EnemyCentroid);
}
