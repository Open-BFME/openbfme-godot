namespace OpenBfme.Sim;

/// <summary>Spawns a directly named loaded template once for the dying object's owner.</summary>
[SageModule("CreateObjectDie", ModuleTier.Cosmetic)]
public sealed class CreateObjectDieModule : ModuleBase
{
    public const string TypeName = "CreateObjectDie";
    private bool _created;

    public CreateObjectDieModule(ModuleSpec spec) : base(spec) { }

    public bool HasCreatedObject => _created;

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (_created) return false;
        var required = Spec.GetString("UpgradeRequired", "");
        if (required.Length > 0 && !world.ObjectHasUpgrade(self, required)) return false;
        var name = Spec.GetString("ObjectTemplate",
            Spec.GetString("Template", Spec.GetString("CreationList", "")));
        if (name.Length > 0 && world.TryGetTemplate(name, out _))
        {
            world.SpawnObject(name, self.Team, self.Position, self.Elevation, self.HeadingRadians);
            _created = true;
        }
        return false;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteBool(_created);
    public override void ReadState(CanonicalReader reader) => _created = reader.ReadBool();
}
