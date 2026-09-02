namespace OpenBfme.Sim;

/// <summary>Horde idle acquisition plus member catch-up around the carrier.</summary>
[SageModule("HordeAIUpdate", ModuleTier.Cosmetic)]
public sealed class HordeAIUpdateModule : ModuleBase
{
    public const string TypeName = "HordeAIUpdate";

    private readonly bool _authoredAutoAcquire;
    private readonly long _moodAttackCheckMilliseconds;
    private readonly Fixed64 _catchUpRadius;
    private int _nextScanTick;
    private int _approvedScanTick = -1;
    private int _stanceAutoAcquire = -1;
    private Fixed64 _stanceGuardRadius;

    public HordeAIUpdateModule(ModuleSpec spec) : base(spec)
    {
        _authoredAutoAcquire = ReadYes(spec, "AutoAcquireEnemiesWhenIdle");
        _moodAttackCheckMilliseconds = Math.Max(0, spec.GetLong("MoodAttackCheckRate", 0));
        _catchUpRadius = ReadFixed(spec, "MemberCatchUpRadius", Fixed64.FromInt(8));
    }

    public string ComboLocomotorSet => Spec.GetString("ComboLocomotorSet", "");
    public bool AttackHorde => ReadYes(Spec, "AttackHorde") || _authoredAutoAcquire;
    public Fixed64 CatchUpRadius => _catchUpRadius;

    internal bool ShouldAutoAcquire(SimWorld world)
    {
        var enabled = _stanceAutoAcquire < 0 ? _authoredAutoAcquire : _stanceAutoAcquire != 0;
        if (!enabled || !AttackHorde) return false;
        if (_approvedScanTick == world.TickIndex) return true;
        if (world.TickIndex < _nextScanTick) return false;
        _approvedScanTick = world.TickIndex;
        var cadence = _moodAttackCheckMilliseconds <= 0
            ? 1
            : checked((int)Math.Max(1,
                (_moodAttackCheckMilliseconds + world.TickMilliseconds - 1) / world.TickMilliseconds));
        _nextScanTick = checked(world.TickIndex + cadence);
        return true;
    }

    internal void ApplyStanceProfile(bool autoAcquire, Fixed64 guardRadius)
    {
        _stanceAutoAcquire = autoAcquire ? 1 : 0;
        _stanceGuardRadius = guardRadius;
        _nextScanTick = 0;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        var horde = world.Hordes.FirstOrDefault(candidate => candidate.Id == self.Id);
        if (horde == null) return;
        var radius = _stanceGuardRadius > Fixed64.Zero ? _stanceGuardRadius : _catchUpRadius;
        var limit = radius * radius;
        foreach (var memberId in horde.Members)
        {
            if (!world.Objects.TryGetValue(memberId, out var member)
                || member.Position.DistanceSquaredTo(self.Position) <= limit) continue;
            if (member.FindModule<LocomotorModule>() is { } locomotor)
                locomotor.SetOrder(self.Position, MoveOrderKind.Move);
            else
                member.FindModule<LinearMoverModule>()?.SetDestination(self.Position);
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_nextScanTick);
        writer.WriteInt(_approvedScanTick);
        writer.WriteInt(_stanceAutoAcquire);
        writer.WriteFixed(_stanceGuardRadius);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _nextScanTick = reader.ReadInt();
        _approvedScanTick = reader.ReadInt();
        _stanceAutoAcquire = reader.ReadInt();
        _stanceGuardRadius = reader.ReadFixed();
    }

    private static bool ReadYes(ModuleSpec spec, string name)
    {
        if (spec.Data.TryGetValue(name, out var value)) return value != 0;
        var text = spec.GetString(name, "").Trim();
        return text.Equals("yes", StringComparison.OrdinalIgnoreCase)
            || text.StartsWith("yes ", StringComparison.OrdinalIgnoreCase)
            || text.Equals("true", StringComparison.OrdinalIgnoreCase);
    }

    private static Fixed64 ReadFixed(ModuleSpec spec, string name, Fixed64 fallback)
    {
        if (spec.Data.TryGetValue(name + "Raw", out var raw)) return Fixed64.FromRaw(raw);
        if (spec.Data.TryGetValue(name, out var integer)) return Fixed64.FromInt64(integer);
        return fallback;
    }
}
