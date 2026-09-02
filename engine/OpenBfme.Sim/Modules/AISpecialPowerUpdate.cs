namespace OpenBfme.Sim;

/// <summary>AI planner-side command trigger using the authored button and target policy.</summary>
[SageModule("AISpecialPowerUpdate", ModuleTier.Structural)]
public sealed class AISpecialPowerUpdateModule : ModuleBase
{
    public const string TypeName = "AISpecialPowerUpdate";
    private readonly string _buttonName;
    private readonly string _aiType;
    private readonly Fixed64 _range;
    private int _lastIssuedTick;

    public AISpecialPowerUpdateModule(ModuleSpec spec) : base(spec)
    {
        _buttonName = spec.GetString("CommandButtonName", "");
        _aiType = spec.GetString("SpecialPowerAIType", "").ToUpperInvariant();
        _range = ModuleRuntime.ReadFixed(spec, "SpecialPowerRange",
            ModuleRuntime.ReadFixed(spec, "SpecialPowerRadius", Fixed64.Zero));
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
        if (!world.AiConfig.Tech.SpecialPowers.ContainsKey(button.SpecialPower)
            || world.PowerReadyTick(state.Team, button.SpecialPower) > tick
            || !world.CommandAllows(self, "power", button.SpecialPower)) return false;

        var target = SelectTarget(world, self);
        var selfCast = IsSelfCast();
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
            || DesiredStance(self.Combat.OrderKind) != stance || self.Combat.Stance == stance) return false;
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
        if (target == null || button.WeaponSlot.Length == 0) return false;
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

    private static UnitStance DesiredStance(CombatOrderKind order) => order switch
    {
        CombatOrderKind.Attack => UnitStance.Aggressive,
        CombatOrderKind.AttackMove => UnitStance.Battle,
        _ => UnitStance.HoldGround,
    };

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
        var allyTarget = _aiType.Contains("HEAL", StringComparison.Ordinal)
            || _aiType.Contains("BUFF", StringComparison.Ordinal);
        var candidates = world.Objects.Values.Where(value => value.Id != self.Id
            && !value.IsDead && !value.IsDying
            && (allyTarget ? value.Team == self.Team && value.Health < value.MaxHealth : value.Team != self.Team));
        if (_range > Fixed64.Zero)
        {
            var limit = _range * _range;
            candidates = candidates.Where(value => self.Position.DistanceSquaredTo(value.Position) <= limit);
        }
        return candidates.OrderBy(value => self.Position.DistanceSquaredTo(value.Position))
            .ThenBy(value => value.Id).FirstOrDefault();
    }

    private bool IsSelfCast() => _aiType.Contains("SELF", StringComparison.Ordinal)
        || _aiType.Contains("CHARGE", StringComparison.Ordinal)
        || _aiType.Contains("BUFF", StringComparison.Ordinal);

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_lastIssuedTick);
    public override void ReadState(CanonicalReader reader) => _lastIssuedTick = reader.ReadInt();
}
