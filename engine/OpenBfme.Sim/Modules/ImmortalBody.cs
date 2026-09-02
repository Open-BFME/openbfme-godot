namespace OpenBfme.Sim;

/// <summary>ImmortalBody (229 objects): takes damage down to 1 health, never dies.</summary>
[SageModule("ImmortalBody", ModuleTier.Structural)]
public sealed class ImmortalBodyModule : ModuleBase
{
    public const string TypeName = "ImmortalBody";

    public long Health { get; private set; }
    public long MaxHealth { get; }

    public ImmortalBodyModule(ModuleSpec spec) : base(spec)
    {
        MaxHealth = spec.GetLong("MaxHealth", 100);
        Health = MaxHealth;
    }

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (amount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(amount), "Damage must be non-negative");
        }
        Health = Math.Max(1, Health - amount);
        return true;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteLong(Health);
    public override void ReadState(CanonicalReader reader) => Health = reader.ReadLong();
}
