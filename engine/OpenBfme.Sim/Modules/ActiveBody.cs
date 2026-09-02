namespace OpenBfme.Sim;

/// <summary>
/// Health container in the shape of SAGE's ActiveBody (1,771 objects in the
/// measured corpus). P0 scope: max health from design data, damage intake,
/// death flagging for the world's removal sweep.
/// </summary>
[SageModule("ActiveBody", ModuleTier.Structural)]
public class ActiveBodyModule : ModuleBase
{
    public const string TypeName = "ActiveBody";

    public long Health { get; private set; }
    public long MaxHealth { get; }

    public ActiveBodyModule(ModuleSpec spec) : base(spec)
    {
        MaxHealth = spec.GetLong("MaxHealth", 100);
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
