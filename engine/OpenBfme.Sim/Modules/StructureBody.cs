namespace OpenBfme.Sim;

/// <summary>
/// StructureBody variant of ActiveBody (373 objects): identical health
/// semantics today; exists as its own type so templates map 1:1 to the SAGE
/// vocabulary and structure-specific armor rules have a home in P2.
/// </summary>
[SageModule("StructureBody", ModuleTier.Structural)]
public sealed class StructureBodyModule : ModuleBase
{
    public const string TypeName = "StructureBody";

    public long Health { get; private set; }
    public long MaxHealth { get; }

    public StructureBodyModule(ModuleSpec spec) : base(spec)
    {
        MaxHealth = spec.GetLong("MaxHealth", 500);
        Health = MaxHealth;
    }

    internal void SetConstructionHealth(long health) => Health = Math.Clamp(health, 0, MaxHealth);

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (amount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(amount), "Damage must be non-negative");
        }
        if (Health == 0)
        {
            return true;
        }
        Health = Math.Max(0, Health - amount);
        if (Health == 0)
        {
            world.HandleDeath(self);
        }
        return true;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteLong(Health);
    public override void ReadState(CanonicalReader reader) => Health = reader.ReadLong();
}
