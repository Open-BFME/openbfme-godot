namespace OpenBfme.Sim;

/// <summary>
/// KeepObjectDie (268 objects): the corpse stays in the world — forever when
/// KeepTicks is 0 (cleanup lanes come later), else for KeepTicks.
/// </summary>
[SageModule("KeepObjectDie", ModuleTier.Structural)]
public sealed class KeepObjectDieModule : TimedDeathModuleBase
{
    public const string TypeName = "KeepObjectDie";

    public KeepObjectDieModule(ModuleSpec spec)
        : base(spec, "KeepTicks", 0, zeroMeansForever: true)
    {
    }
}
