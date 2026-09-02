namespace OpenBfme.Sim;

/// <summary>
/// GettingBuiltBehavior-shaped construction gate. Placed BFME2 structures
/// self-build from InitialHealth to MaxHealth over their authored BuildTime.
/// </summary>
[SageModule("GettingBuiltBehavior", ModuleTier.Structural)]
public sealed class GettingBuiltModule : ModuleBase
{
    public const string TypeName = "GettingBuiltBehavior";

    private bool _started;
    private bool _skipNextUpdate;
    private int _constructionTicks;
    private int _elapsedTicks;
    private Fixed64 _initialHealth;
    private Fixed64 _maximumHealth;

    public GettingBuiltModule(ModuleSpec spec) : base(spec)
    {
    }

    public void StartConstruction(SimWorld world, GameObject self)
    {
        if (_started) return;
        Begin(world, self);
        _skipNextUpdate = true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_started)
        {
            Begin(world, self);
        }
        if (!self.IsUnderConstruction)
        {
            return;
        }
        if (_skipNextUpdate)
        {
            _skipNextUpdate = false;
            return;
        }
        _elapsedTicks++;
        var health = _initialHealth
            + (_maximumHealth - _initialHealth)
            * Fixed64.FromFraction(_elapsedTicks, _constructionTicks);
        self.SetConstructionHealth(Fixed64.Min(health, _maximumHealth));
        if (_elapsedTicks >= _constructionTicks)
        {
            self.SetUnderConstruction(false);
            world.RaiseEvent(new SimEvent("build_done", self.Id, Name: self.TemplateName));
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_started);
        writer.WriteBool(_skipNextUpdate);
        writer.WriteInt(_constructionTicks);
        writer.WriteInt(_elapsedTicks);
        writer.WriteFixed(_initialHealth);
        writer.WriteFixed(_maximumHealth);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _started = reader.ReadBool();
        _skipNextUpdate = reader.ReadBool();
        _constructionTicks = reader.ReadInt();
        _elapsedTicks = reader.ReadInt();
        _initialHealth = reader.ReadFixed();
        _maximumHealth = reader.ReadFixed();
    }

    private void Begin(SimWorld world, GameObject self)
    {
        _started = true;
        _constructionTicks = Spec.Data.ContainsKey("ConstructionTicks")
            ? checked((int)Math.Max(1, Spec.GetLong("ConstructionTicks", 1)))
            : Math.Max(1, self.Template.Economy.BuildTicks(world.TickMilliseconds));
        (_initialHealth, _maximumHealth) = self.ConstructionHealthBounds();
        self.SetConstructionHealth(_initialHealth);
        self.SetUnderConstruction(true);
    }
}
