namespace OpenBfme.Sim;

/// <summary>AI planner-side special-power trigger using the authored command button and target policy.</summary>
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
            || !world.AiConfig.Tech.CommandButtons.TryGetValue(_buttonName, out var button)
            || button.SpecialPower.Length == 0
            || !world.AiConfig.Tech.SpecialPowers.ContainsKey(button.SpecialPower)
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
