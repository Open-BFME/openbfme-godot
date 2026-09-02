namespace OpenBfme.Sim;

/// <summary>DestroyDie (385 objects): explicit immediate removal on death.</summary>
[SageModule("DestroyDie", ModuleTier.Structural)]
public sealed class DestroyDieModule : ModuleBase
{
    public const string TypeName = "DestroyDie";

    public DestroyDieModule(ModuleSpec spec) : base(spec)
    {
    }

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        self.MarkDead();
        return true;
    }
}
