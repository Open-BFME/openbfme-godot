namespace OpenBfme.Sim;

/// <summary>
/// SlowDeathBehavior-shaped death interception (1,194 objects in the corpus):
/// claims the death, keeps the object in the world for DeathTicks (presentation
/// plays the death sequence there), then releases it for removal.
/// </summary>
[SageModule("SlowDeath", ModuleTier.Structural)]
public sealed class SlowDeathModule : ModuleBase
{
    public const string TypeName = "SlowDeath";

    private readonly int _deathTicks;
    private int _ticksRemaining;
    private bool _dying;

    public SlowDeathModule(ModuleSpec spec) : base(spec)
    {
        _deathTicks = (int)Math.Max(1, spec.GetLong("DeathTicks", 30));
    }

    public bool IsDying => _dying;

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (_dying)
        {
            return true;
        }
        _dying = true;
        _ticksRemaining = _deathTicks;
        self.MarkDying();
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_dying)
        {
            return;
        }
        _ticksRemaining--;
        if (_ticksRemaining <= 0)
        {
            self.MarkDead();
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_dying);
        writer.WriteInt(_ticksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _dying = reader.ReadBool();
        _ticksRemaining = reader.ReadInt();
    }
}
