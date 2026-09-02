namespace OpenBfme.Sim;

/// <summary>One-shot authored geometry/profile switch driven by TriggeredBy.</summary>
[SageModule("GeometryUpgrade", ModuleTier.Structural)]
public sealed class GeometryUpgradeModule : UpgradeTriggeredModuleBase
{
    public const string TypeName = "GeometryUpgrade";
    private Fixed64 _majorRadius;
    private Fixed64 _minorRadius;
    private string _shown = "";
    private string _hidden = "";

    public GeometryUpgradeModule(ModuleSpec spec) : base(spec) { }

    public Fixed64 MajorRadius => _majorRadius;
    public Fixed64 MinorRadius => _minorRadius;
    public string ShownGeometry => _shown;
    public string HiddenGeometry => _hidden;

    protected override void ApplyEffect(SimWorld world, GameObject self)
    {
        _majorRadius = ModuleRuntime.ReadFixed(Spec, "GeometryMajorRadius",
            ModuleRuntime.ReadFixed(Spec, "MajorRadius", Fixed64.Zero));
        _minorRadius = ModuleRuntime.ReadFixed(Spec, "GeometryMinorRadius",
            ModuleRuntime.ReadFixed(Spec, "MinorRadius", Fixed64.Zero));
        _shown = Spec.GetString("ShowGeometry", "");
        _hidden = Spec.GetString("HideGeometry", "");
        foreach (var token in ModuleRuntime.Tokens(_shown)) self.TrySetConditionToken("GEOMETRY_SHOW:" + token);
        foreach (var token in ModuleRuntime.Tokens(_hidden)) self.TrySetConditionToken("GEOMETRY_HIDE:" + token);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        base.WriteState(writer);
        writer.WriteFixed(_majorRadius);
        writer.WriteFixed(_minorRadius);
        writer.WriteString(_shown);
        writer.WriteString(_hidden);
    }

    public override void ReadState(CanonicalReader reader)
    {
        base.ReadState(reader);
        _majorRadius = reader.ReadFixed();
        _minorRadius = reader.ReadFixed();
        _shown = reader.ReadString();
        _hidden = reader.ReadString();
    }
}
