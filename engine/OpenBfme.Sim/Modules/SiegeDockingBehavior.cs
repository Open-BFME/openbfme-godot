namespace OpenBfme.Sim;

/// <summary>
/// No-field capability module for siege docking. Dock/Undock maintain the
/// authoritative docked object id and DOCKED condition with canonical state.
/// Automatic approach animation and containment visuals are deferred to the
/// future siege command/pathing integration.
/// </summary>
[SageModule("SiegeDockingBehavior", ModuleTier.Structural)]
public sealed class SiegeDockingBehaviorModule : ModuleBase
{
    public const string TypeName = "SiegeDockingBehavior";
    private int _dockedObjectId;

    public SiegeDockingBehaviorModule(ModuleSpec spec) : base(spec) { }
    public int DockedObjectId => _dockedObjectId;

    public bool Dock(GameObject self, int objectId)
    {
        if (objectId < 1 || _dockedObjectId != 0) return false;
        _dockedObjectId = objectId;
        self.SetConditionToken("DOCKED");
        return true;
    }

    public void Undock(GameObject self)
    {
        _dockedObjectId = 0;
        self.SetConditionToken("DOCKED", false);
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_dockedObjectId);
    public override void ReadState(CanonicalReader reader) => _dockedObjectId = reader.ReadInt();
}
