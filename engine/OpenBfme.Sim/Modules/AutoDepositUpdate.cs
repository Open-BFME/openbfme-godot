namespace OpenBfme.Sim;

/// <summary>
/// SAGE AutoDepositUpdate: a complete, living resource structure deposits an
/// authored amount on an authored period. The crowding multiplier is mutable
/// authoritative state so map economy rules can update it deterministically.
/// </summary>
[SageModule("AutoDepositUpdate", ModuleTier.Structural)]
public sealed class AutoDepositUpdateModule : ModuleBase
{
    public const string TypeName = "AutoDepositUpdate";

    private bool _initialized;
    private int _ticksUntilDeposit;
    private Fixed64 _crowdingMultiplier;
    private bool _crowdingSet;

    public AutoDepositUpdateModule(ModuleSpec spec) : base(spec)
    {
        _crowdingMultiplier = spec.GetFixed("CrowdingMultiplierRaw", Fixed64.One);
        _crowdingSet = spec.Data.ContainsKey("CrowdingMultiplierRaw");
        if (_crowdingMultiplier < Fixed64.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(spec), "CrowdingMultiplier must be non-negative");
        }
    }

    public Fixed64 CrowdingMultiplier => _crowdingMultiplier;

    public void SetCrowdingMultiplier(Fixed64 multiplier)
    {
        if (multiplier < Fixed64.Zero) throw new ArgumentOutOfRangeException(nameof(multiplier));
        _crowdingMultiplier = multiplier;
        _crowdingSet = true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying)
        {
            return;
        }
        var period = PeriodTicks(world, self);
        if (period <= 0)
        {
            return;
        }
        if (!_initialized)
        {
            if (!_crowdingSet) _crowdingMultiplier = self.Template.Economy.CrowdingMultiplier;
            _initialized = true;
            _ticksUntilDeposit = period;
        }
        _ticksUntilDeposit--;
        if (_ticksUntilDeposit > 0)
        {
            return;
        }
        _ticksUntilDeposit = period;
        var amount = Math.Max(0, Spec.GetLong("DepositAmount", self.Template.Economy.DepositAmount));
        world.AddTeamResources(self.Team, EconomyTemplate.ScaleInteger(amount, _crowdingMultiplier));
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_initialized);
        writer.WriteInt(_ticksUntilDeposit);
        writer.WriteFixed(_crowdingMultiplier);
        writer.WriteBool(_crowdingSet);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _initialized = reader.ReadBool();
        _ticksUntilDeposit = reader.ReadInt();
        _crowdingMultiplier = reader.ReadFixed();
        _crowdingSet = reader.ReadBool();
    }

    private int PeriodTicks(SimWorld world, GameObject self)
    {
        var authoredMilliseconds = Spec.GetLong(
            "DepositTiming",
            self.Template.Economy.DepositTimingMilliseconds);
        return authoredMilliseconds <= 0
            ? 0
            : EconomyTemplate.MillisecondsToTicks(authoredMilliseconds, world.TickMilliseconds);
    }
}
