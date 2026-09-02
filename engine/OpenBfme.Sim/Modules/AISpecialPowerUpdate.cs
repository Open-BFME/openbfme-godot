namespace OpenBfme.Sim;

/// <summary>AI planner-side command trigger using the authored button and target policy.</summary>
[SageModule("AISpecialPowerUpdate", ModuleTier.Structural)]
public sealed class AISpecialPowerUpdateModule : ModuleBase
{
    public const string TypeName = "AISpecialPowerUpdate";
    private readonly string _buttonName;
    private readonly string _aiType;
    private readonly Fixed64 _range;
    private readonly Fixed64 _radius;
    private int _lastIssuedTick;

    public AISpecialPowerUpdateModule(ModuleSpec spec) : base(spec)
    {
        _buttonName = spec.GetString("CommandButtonName", "");
        _aiType = spec.GetString("SpecialPowerAIType", "").ToUpperInvariant();
        _range = ModuleRuntime.ReadFixed(spec, "SpecialPowerRange", Fixed64.Zero);
        _radius = ModuleRuntime.ReadFixed(spec, "SpecialPowerRadius", Fixed64.Zero);
    }

    internal bool TryPlan(
        SimWorld world,
        GameObject self,
        AiPlayerState state,
        int tick,
        ICollection<SimCommand> commands)
    {
        if (_lastIssuedTick == tick || _buttonName.Length == 0
            || !TryResolveButton(world.AiConfig.Tech, out var button)) return false;

        if (button.SpecialPower.Length == 0)
            return TryPlanButtonCommand(world, self, state, tick, commands, button);
        if (!world.AiConfig.Tech.SpecialPowers.TryGetValue(button.SpecialPower, out var power)
            || power.RequiredSciences.Any(science => !world.TeamHasScience(state.Team, science))
            || world.PowerReadyTick(state.Team, button.SpecialPower) > tick
            || !world.CommandAllows(self, "power", button.SpecialPower)) return false;

        var selfCast = IsSelfCast();
        var target = selfCast ? null : SelectTarget(world, self);
        if (target == null && !selfCast) return false;
        var args = new List<KeyValuePair<string, CommandValue>>
        {
            new("objects", CommandValue.OfLongList(new long[] { self.Id })),
            new("name", CommandValue.OfString(button.SpecialPower)),
        };
        if (target != null)
            args.Add(new KeyValuePair<string, CommandValue>("target", CommandValue.OfLong(target.Id)));
        commands.Add(new SimCommand(tick, state.Team, state.CommandSequence++, "power", args, state.Seat));
        _lastIssuedTick = tick;
        return true;
    }

    private bool TryPlanButtonCommand(
        SimWorld world,
        GameObject self,
        AiPlayerState state,
        int tick,
        ICollection<SimCommand> commands,
        CommandButtonTemplate button)
    {
        if (!world.CommandButtonAllows(self, button.Name)) return false;
        if (button.Command.Equals("SET_STANCE", StringComparison.Ordinal))
            return TryPlanStance(self, state, tick, commands, button);
        if (button.Command.Equals("TOGGLE_WEAPONSET", StringComparison.Ordinal))
            return TryPlanWeaponToggle(world, self, state, tick, commands, button);
        if (button.Command.Equals("FIRE_WEAPON", StringComparison.Ordinal))
            return TryPlanWeaponFire(world, self, state, tick, commands, button);
        return false;
    }

    private bool TryPlanStance(
        GameObject self,
        AiPlayerState state,
        int tick,
        ICollection<SimCommand> commands,
        CommandButtonTemplate button)
    {
        if (self.Combat == null || !TryStance(button.Stances, out var stance, out var commandValue)
            || DesiredStance(self, state) != stance || self.Combat.Stance == stance) return false;
        commands.Add(new SimCommand(tick, state.Team, state.CommandSequence++, "stance", new[]
        {
            new KeyValuePair<string, CommandValue>("objects", CommandValue.OfLongList(new long[] { self.Id })),
            new KeyValuePair<string, CommandValue>("stance", CommandValue.OfString(commandValue)),
        }, state.Seat));
        _lastIssuedTick = tick;
        return true;
    }

    private bool TryPlanWeaponToggle(
        SimWorld world,
        GameObject self,
        AiPlayerState state,
        int tick,
        ICollection<SimCommand> commands,
        CommandButtonTemplate button)
    {
        var target = SelectTarget(world, self);
        if (target == null) return false;
        if (_aiType.Contains("TOGGLE_SIEGE", StringComparison.Ordinal)
            && !target.Template.KindOf.Contains("STRUCTURE", StringComparer.Ordinal)) return false;
        var flags = ModuleRuntime.Tokens(button.FlagsUsedForToggle);
        if (flags.Length == 0 || flags.All(self.ConditionTokens.Contains)) return false;
        AddAbilityCommand(state, tick, commands, self, button, target);
        return true;
    }

    private bool TryPlanWeaponFire(
        SimWorld world,
        GameObject self,
        AiPlayerState state,
        int tick,
        ICollection<SimCommand> commands,
        CommandButtonTemplate button)
    {
        var target = SelectTarget(world, self);
        if (target == null || button.WeaponSlot.Length == 0
            || !world.WeaponSlotAllowsTarget(self, button.WeaponSlot, target)) return false;
        AddAbilityCommand(state, tick, commands, self, button, target);
        return true;
    }

    private void AddAbilityCommand(
        AiPlayerState state,
        int tick,
        ICollection<SimCommand> commands,
        GameObject self,
        CommandButtonTemplate button,
        GameObject target)
    {
        commands.Add(new SimCommand(tick, state.Team, state.CommandSequence++, "ability", new[]
        {
            new KeyValuePair<string, CommandValue>("objects", CommandValue.OfLongList(new long[] { self.Id })),
            new KeyValuePair<string, CommandValue>("name", CommandValue.OfString(button.Name)),
            new KeyValuePair<string, CommandValue>("target", CommandValue.OfLong(target.Id)),
        }, state.Seat));
        _lastIssuedTick = tick;
    }

    private bool TryResolveButton(TechCatalog tech, out CommandButtonTemplate button)
    {
        if (tech.CommandButtons.TryGetValue(_buttonName, out button!)) return true;
        const string retailTypo = "Command_GoblinKingCallOfTheDeep";
        return _buttonName.Equals(retailTypo, StringComparison.Ordinal)
            && tech.CommandButtons.TryGetValue("Command_GoblinKingCallFromTheDeep", out button!);
    }

    private static UnitStance DesiredStance(GameObject self, AiPlayerState state) =>
        (state.Phase == AiPhase.Defend
            || self.FindModule<AIUpdateInterfaceModule>() is { AutoAcquireEnabled: false })
            && self.Combat!.OrderKind == CombatOrderKind.None
            && self.Combat.EngagedTargetId == 0
            ? UnitStance.HoldGround
        : self.Combat!.OrderKind == CombatOrderKind.Attack || self.Combat.EngagedTargetId != 0
            ? UnitStance.Aggressive
            : UnitStance.Battle;

    private static bool TryStance(string value, out UnitStance stance, out string commandValue)
    {
        commandValue = ModuleRuntime.Tokens(value).FirstOrDefault() switch
        {
            "Aggressive" => "aggressive",
            "Battle" => "battle",
            "HoldGround" => "hold_ground",
            _ => "",
        };
        stance = commandValue switch
        {
            "aggressive" => UnitStance.Aggressive,
            "battle" => UnitStance.Battle,
            "hold_ground" => UnitStance.HoldGround,
            _ => 0,
        };
        return stance != 0;
    }

    private GameObject? SelectTarget(SimWorld world, GameObject self)
    {
        var allyTarget = TargetsAllies();
        var structureTarget = TargetsStructure();
        var candidates = world.Objects.Values.Where(value => value.Id != self.Id
            && !value.IsDead && !value.IsDying
            && (allyTarget ? value.Team == self.Team : value.Team != self.Team)
            && IsEligibleTarget(value, structureTarget));
        if (_range > Fixed64.Zero)
        {
            var limit = _range * _range;
            candidates = candidates.Where(value => self.Position.DistanceSquaredTo(value.Position) <= limit);
        }
        return candidates.Select(value => new
            {
                Object = value,
                Score = ClusterScore(world, self, value.Position, allyTarget),
            })
            .OrderByDescending(value => value.Score)
            .ThenBy(value => self.Position.DistanceSquaredTo(value.Object.Position))
            .ThenBy(value => value.Object.Id)
            .Select(value => value.Object)
            .FirstOrDefault();
    }

    private bool IsEligibleTarget(GameObject candidate, bool structureTarget)
    {
        var roles = AiTemplateRoles.Classify(candidate.Template);
        if (structureTarget != ((roles & AiUnitRole.Structure) != 0)) return false;
        if (_aiType == "AI_SPECIAL_POWER_CAPTURE_BUILDING"
            && (candidate.Team != -1
                || !candidate.Template.KindOf.Contains("CAPTURABLE", StringComparer.Ordinal))) return false;
        if (_aiType is "AI_SPECIAL_POWER_HEAL_AOE" or "AI_SPELLBOOK_REBUILD"
            && candidate.Health >= candidate.MaxHealth) return false;
        if (_aiType == "AI_SPELLBOOK_BUFFECONOMYBUILDING"
            && (roles & AiUnitRole.Economy) == 0) return false;
        if (_aiType == "AI_SPELLBOOK_DEBUFFECONOMYBUILDING"
            && (roles & AiUnitRole.Economy) == 0) return false;
        if (_aiType == "AI_SPELLBOOK_DEBUFFPRODUCTIONBUILDING"
            && candidate.FindModule<ProductionModule>() == null) return false;
        if (_aiType == "AI_SPELLBOOK_CITADEL"
            && !candidate.Template.KindOf.Contains("FORTRESS", StringComparer.Ordinal)
            && !candidate.Template.KindOf.Contains("CITADEL", StringComparer.Ordinal)) return false;
        if (_aiType == "AI_SPECIAL_POWER_DOMINATE_TROLL"
            && !candidate.Template.KindOf.Contains("TROLL", StringComparer.Ordinal)) return false;
        if (_aiType == "AI_SPELLBOOK_CAPTURE_CREEP"
            && !candidate.Template.KindOf.Contains("CREEP", StringComparer.Ordinal)) return false;
        if (_aiType == "AI_SPELLBOOK_TREE_KILLER"
            && !candidate.Template.KindOf.Contains("TREE", StringComparer.Ordinal)
            && !candidate.Template.KindOf.Contains("SHRUBBERY", StringComparer.Ordinal)) return false;
        return true;
    }

    private int ClusterScore(SimWorld world, GameObject self, FixedVector2 point, bool allies)
    {
        if (_radius <= Fixed64.Zero) return 1;
        var limit = _radius * _radius;
        var score = 0;
        foreach (var candidate in world.Objects.Values)
        {
            if (candidate.IsDead || candidate.IsDying
                || (allies ? candidate.Team != self.Team : candidate.Team == self.Team)
                || candidate.Position.DistanceSquaredTo(point) > limit) continue;
            score++;
        }
        if (_aiType == "AI_SPELLBOOK_STRUCTURE_BASEKILL"
            && world.Objects.Values.Any(value => value.Position == point
                && (value.Template.KindOf.Contains("FORTRESS", StringComparer.Ordinal)
                    || value.Template.KindOf.Contains("CITADEL", StringComparer.Ordinal)))) score += 10_000;
        if (_aiType == "AI_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS"
            && world.Objects.Values.Any(value => value.Position == point
                && value.Template.KindOf.Contains("WALL", StringComparer.Ordinal))) score += 10_000;
        return score;
    }

    private bool IsSelfCast() => _aiType is
        "AI_SPECIAL_POWER_BASIC_SELF_BUFF"
        or "AI_SPECIAL_POWER_CHARGE"
        or "AI_SPECIAL_POWER_ELENDIL"
        or "AI_SPECIAL_POWER_GOBLINKING_MOUNTED"
        or "AI_SPECIAL_POWER_SELFAOEHEALHEROS"
        or "AI_SPECIAL_POWER_TOGGLE_MOUNTED"
        or "AI_SPECIAL_POWER_TOGGLE_SIEGE"
        or "AI_SPELLBOOK_ALWAYS_FIRE"
        or "AI_SPELLBOOK_CALLTHEHORDE"
        or "AI_SPELLBOOK_SHROUD_REVEAL";

    private bool TargetsAllies() => _aiType is
        "AI_SPECIAL_POWER_GIVEXP_AOE"
        or "AI_SPECIAL_POWER_HEAL_AOE"
        or "AI_SPECIAL_POWER_LEGOLAS_TRAINARCHERS"
        or "AI_SPELLBOOK_ASSIST_BATTLE_BUFF"
        or "AI_SPELLBOOK_BUFFECONOMYBUILDING"
        or "AI_SPELLBOOK_BUFFTERRAIN"
        or "AI_SPELLBOOK_CITADEL"
        or "AI_SPELLBOOK_ENSHROUDINGMIST"
        or "AI_SPELLBOOK_HEAL"
        or "AI_SPELLBOOK_REBUILD";

    private bool TargetsStructure() => _aiType is
        "AI_SPECIAL_POWER_CAPTURE_BUILDING"
        or "AI_SPECIAL_POWER_ENEMY_TYPE_KILLER_STRUCTURES"
        or "AI_SPELLBOOK_BUFFECONOMYBUILDING"
        or "AI_SPELLBOOK_CITADEL"
        or "AI_SPELLBOOK_DEBUFFECONOMYBUILDING"
        or "AI_SPELLBOOK_DEBUFFPRODUCTIONBUILDING"
        or "AI_SPELLBOOK_REBUILD"
        or "AI_SPELLBOOK_STRUCTURE_BASEKILL"
        or "AI_SPELLBOOK_STRUCTURE_BREAKER"
        or "AI_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS";

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_lastIssuedTick);
    public override void ReadState(CanonicalReader reader) => _lastIssuedTick = reader.ReadInt();
}
