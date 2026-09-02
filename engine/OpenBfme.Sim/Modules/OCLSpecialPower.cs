namespace OpenBfme.Sim;

[SageModule("OCLSpecialPower", ModuleTier.Structural)]
public sealed class OCLSpecialPowerModule : SpecialPowerEffectModuleBase
{
    public const string TypeName = "OCLSpecialPower";
    private readonly string[] _templates;

    public OCLSpecialPowerModule(ModuleSpec spec) : base(spec)
    {
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
        foreach (var template in _templates)
        {
            if (world.TryGetTemplate(template, out _))
                world.SpawnObject(template, caster.Team, targetPosition);
            else
                world.RecordTechGap(TypeName + ":" + template);
        }
    }
}
