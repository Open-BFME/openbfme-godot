namespace OpenBfme.Sim;

/// <summary>
/// Active body that applies an authored incoming-damage multiplier while its
/// containing horde has a non-default (porcupine/block) formation.
/// </summary>
[SageModule("PorcupineFormationBodyModule", ModuleTier.Structural)]
public sealed class PorcupineFormationBodyModule : ActiveBodyModule
{
    public new const string TypeName = "PorcupineFormationBodyModule";

    private readonly Fixed64 _formationMultiplier;
    private bool _formationActive;

    public PorcupineFormationBodyModule(ModuleSpec spec) : base(spec)
    {
        _formationMultiplier = Fixed64.Clamp(ReadMultiplier(spec), Fixed64.Zero, Fixed64.One);
    }

    public bool FormationActive => _formationActive;
    public Fixed64 FormationMultiplier => _formationMultiplier;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        _formationActive = world.Hordes.Any(horde =>
            horde.Formation != 0 && (horde.Id == self.Id || horde.Members.Contains(self.Id)));
    }

    public override long ModifyIncomingDamage(GameObject self, string damageType, long amount)
    {
        if (amount < 0) throw new ArgumentOutOfRangeException(nameof(amount));
        return _formationActive
            ? (Fixed64.FromInt64(amount) * _formationMultiplier).ToIntFloor()
            : amount;
    }

    internal Fixed64 ModifyIncomingDamage(Fixed64 amount) =>
        _formationActive ? amount * _formationMultiplier : amount;

    public override void WriteState(CanonicalWriter writer)
    {
        base.WriteState(writer);
        writer.WriteBool(_formationActive);
    }

    public override void ReadState(CanonicalReader reader)
    {
        base.ReadState(reader);
        _formationActive = reader.ReadBool();
    }

    private static Fixed64 ReadMultiplier(ModuleSpec spec)
    {
        foreach (var key in new[] { "FormationDamageMultiplier", "PorcupineDamageMultiplier", "DamageMultiplier" })
        {
            if (spec.Data.TryGetValue(key + "Raw", out var raw)) return Fixed64.FromRaw(raw);
            if (spec.Data.TryGetValue(key, out var integer)) return Fixed64.FromInt64(integer);
        }
        foreach (var key in new[] { "FormationDamagePercent", "DamageMultiplierPercent" })
        {
            if (spec.Data.TryGetValue(key, out var percent)) return Fixed64.FromFraction(percent, 100);
        }
        return Fixed64.One;
    }
}
