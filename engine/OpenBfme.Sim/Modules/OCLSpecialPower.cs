namespace OpenBfme.Sim;

/// <summary>
/// Routes an authored OCL special power through command-v1 and emits a stable
/// `ocl` simulation event at USE_OWNER_OBJECT or the supplied target location.
/// Direct template aliases are spawned immediately. Expansion of named Object
/// Creation Lists is deferred because bundle-v1 deliberately reports the
/// object_creation_lists table absent; the event is the deterministic handoff.
/// Weather and FX fields are presentation-owned.
/// </summary>
[SageModule("OCLSpecialPower", ModuleTier.Structural)]
public sealed class OCLSpecialPowerModule : SpecialPowerEffectModuleBase
{
    public const string TypeName = "OCLSpecialPower";
    private readonly string[] _templates;
    private readonly string _ocl;
    private readonly bool _useOwner;

    public OCLSpecialPowerModule(ModuleSpec spec) : base(spec)
    {
        _ocl = spec.GetString("OCL", "");
        _useOwner = spec.GetString("CreateLocation", "")
            .Equals("USE_OWNER_OBJECT", StringComparison.OrdinalIgnoreCase);
        _templates = new[] { "ObjectNames", "CreateObject", "TemplateNames", "ObjectTemplate" }
            .SelectMany(name => spec.GetString(name, "")
                .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
            .ToArray();
    }

    internal override void Cast(
        SimWorld world,
        GameObject caster,
        int targetId,
        FixedVector2 targetPosition)
    {
        var position = _useOwner ? caster.Position : targetPosition;
        if (_ocl.Length > 0)
        {
            if (world.TryGetTemplate(_ocl, out _)) world.SpawnObject(_ocl, caster.Team, position);
            world.RaiseEvent(new SimEvent("ability", caster.Id, targetId == 0 ? null : targetId, Name: _ocl));
        }
        foreach (var template in _templates)
        {
            if (world.TryGetTemplate(template, out _))
                world.SpawnObject(template, caster.Team, position);
        }
    }
}
