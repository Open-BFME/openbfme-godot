namespace OpenBfme.Sim;

/// <summary>A body-less world object: it cannot be targeted and consumes all damage.</summary>
[SageModule("InactiveBody", ModuleTier.Structural)]
public sealed class InactiveBodyModule : ModuleBase
{
    public const string TypeName = "InactiveBody";

    public InactiveBodyModule(ModuleSpec spec) : base(spec) { }

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (amount < 0) throw new ArgumentOutOfRangeException(nameof(amount));
        return true;
    }
}
