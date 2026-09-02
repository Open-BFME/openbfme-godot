namespace OpenBfme.Sim;

/// <summary>
/// SquishCollide marker (374 objects): declares the object crushable. Crush
/// damage is dropped by the world for objects without this module.
/// </summary>
[SageModule("SquishCollide", ModuleTier.Structural)]
public sealed class SquishCollideModule : ModuleBase
{
    public const string TypeName = "SquishCollide";

    public SquishCollideModule(ModuleSpec spec) : base(spec)
    {
    }
}
