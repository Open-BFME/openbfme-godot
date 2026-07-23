namespace OpenBfme.Sim;

/// <summary>
/// Static per-template module description: the module's SAGE type name plus its
/// design-data dictionary (parsed upstream from INI via the importer's compiled
/// manifests). Data is config, not state — it is never mutated at runtime.
/// </summary>
public sealed class ModuleSpec
{
    public string TypeName { get; }
    public IReadOnlyDictionary<string, long> Data => _data;
    public IReadOnlyDictionary<string, string> StringData => _stringData;

    private readonly SortedDictionary<string, long> _data;
    private readonly SortedDictionary<string, string> _stringData;

    public ModuleSpec(
        string typeName,
        IEnumerable<KeyValuePair<string, long>>? data = null,
        IEnumerable<KeyValuePair<string, string>>? stringData = null)
    {
        TypeName = typeName ?? throw new ArgumentNullException(nameof(typeName));
        _data = new SortedDictionary<string, long>(StringComparer.Ordinal);
        if (data != null)
        {
            foreach (var pair in data)
            {
                _data.Add(pair.Key, pair.Value);
            }
        }
        _stringData = new SortedDictionary<string, string>(StringComparer.Ordinal);
        if (stringData != null)
        {
            foreach (var pair in stringData)
            {
                _stringData.Add(pair.Key, pair.Value);
            }
        }
    }

    public long GetLong(string key, long fallback) => _data.TryGetValue(key, out var value) ? value : fallback;

    public Fixed64 GetFixed(string key, Fixed64 fallback) =>
        _data.TryGetValue(key, out var value) ? Fixed64.FromRaw(value) : fallback;

    public string GetString(string key, string fallback) =>
        _stringData.TryGetValue(key, out var value) ? value : fallback;
}

public sealed class ObjectTemplate
{
    public string Name { get; }
    public IReadOnlyList<ModuleSpec> Modules { get; }

    public ObjectTemplate(string name, IReadOnlyList<ModuleSpec> modules)
    {
        Name = name ?? throw new ArgumentNullException(nameof(name));
        Modules = modules ?? throw new ArgumentNullException(nameof(modules));
    }
}

/// <summary>
/// Base class for simulation modules. Update order is the template's module order,
/// applied to objects in ascending id order — both fixed, both part of the
/// determinism contract. Mutable module state must round-trip through
/// WriteState/ReadState or the snapshot/hash gates will catch it.
/// </summary>
public abstract class ModuleBase
{
    protected ModuleBase(ModuleSpec spec) => Spec = spec;

    public ModuleSpec Spec { get; }

    public virtual void OnUpdate(SimWorld world, GameObject self)
    {
    }

    /// <summary>Returns true when the damage was consumed (e.g. by a body module).</summary>
    public virtual bool OnDamage(SimWorld world, GameObject self, long amount) => false;

    /// <summary>
    /// Armor-shaped pre-body hook: every module sees the incoming amount and may
    /// scale it. Runs in module order before the OnDamage chain.
    /// </summary>
    public virtual long ModifyIncomingDamage(GameObject self, string damageType, long amount) => amount;

    /// <summary>
    /// Death interception hook (SlowDeathBehavior-shaped). Returning true claims
    /// the death: the object stays in the world and the module owns its eventual
    /// removal. Returning false lets the next module try; if none claims it, the
    /// object is removed at end of tick.
    /// </summary>
    public virtual bool OnDeath(SimWorld world, GameObject self) => false;

    public virtual void WriteState(CanonicalWriter writer)
    {
    }

    public virtual void ReadState(CanonicalReader reader)
    {
    }
}

public sealed class ModuleRegistry
{
    private readonly SortedDictionary<string, Func<ModuleSpec, ModuleBase>> _factories = new(StringComparer.Ordinal);

    public void Register(string typeName, Func<ModuleSpec, ModuleBase> factory)
    {
        _factories.Add(typeName, factory);
    }

    public bool TryCreate(ModuleSpec spec, out ModuleBase? module)
    {
        if (_factories.TryGetValue(spec.TypeName, out var factory))
        {
            module = factory(spec);
            return true;
        }
        module = null;
        return false;
    }

    /// <summary>The P0/P1 vocabulary. Grows toward the measured ~350-type corpus tables.</summary>
    public static ModuleRegistry CreateDefault()
    {
        var registry = new ModuleRegistry();
        registry.Register(ActiveBodyModule.TypeName, spec => new ActiveBodyModule(spec));
        registry.Register(ResourceGeneratorModule.TypeName, spec => new ResourceGeneratorModule(spec));
        registry.Register(LinearMoverModule.TypeName, spec => new LinearMoverModule(spec));
        registry.Register(SlowDeathModule.TypeName, spec => new SlowDeathModule(spec));
        registry.Register(ProductionModule.TypeName, spec => new ProductionModule(spec));
        registry.Register(GettingBuiltModule.TypeName, spec => new GettingBuiltModule(spec));
        registry.Register(StructureBodyModule.TypeName, spec => new StructureBodyModule(spec));
        registry.Register(ImmortalBodyModule.TypeName, spec => new ImmortalBodyModule(spec));
        registry.Register(LifetimeModule.TypeName, spec => new LifetimeModule(spec));
        registry.Register(DestroyDieModule.TypeName, spec => new DestroyDieModule(spec));
        registry.Register(KeepObjectDieModule.TypeName, spec => new KeepObjectDieModule(spec));
        registry.Register(StructureCollapseModule.TypeName, spec => new StructureCollapseModule(spec));
        registry.Register(ArmorModule.TypeName, spec => new ArmorModule(spec));
        registry.Register(SquishCollideModule.TypeName, spec => new SquishCollideModule(spec));
        registry.Register(WeaponModule.TypeName, spec => new WeaponModule(spec));
        registry.Register(AiCombatModule.TypeName, spec => new AiCombatModule(spec));
        registry.Register(HordeContainModule.TypeName, spec => new HordeContainModule(spec));
        registry.Register(BezierProjectileModule.TypeName, spec => new BezierProjectileModule(spec));
        registry.Register(AttributeModifierAuraModule.TypeName, spec => new AttributeModifierAuraModule(spec));
        return registry;
    }
}

/// <summary>
/// Health container in the shape of SAGE's ActiveBody (1,771 objects in the
/// measured corpus). P0 scope: max health from design data, damage intake,
/// death flagging for the world's removal sweep.
/// </summary>
public sealed class ActiveBodyModule : ModuleBase
{
    public const string TypeName = "ActiveBody";

    public long Health { get; private set; }
    public long MaxHealth { get; }

    public ActiveBodyModule(ModuleSpec spec) : base(spec)
    {
        MaxHealth = spec.GetLong("MaxHealth", 100);
        Health = MaxHealth;
    }

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (amount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(amount), "Damage must be non-negative");
        }
        if (Health == 0)
        {
            return true;
        }
        Health = Math.Max(0, Health - amount);
        if (Health == 0)
        {
            world.HandleDeath(self);
        }
        return true;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteLong(Health);
    public override void ReadState(CanonicalReader reader) => Health = reader.ReadLong();
}

/// <summary>
/// Periodic team income in the shape of TerrainResourceBehavior (152 objects in
/// the corpus): every IntervalTicks, credit Amount to the owner's resources.
/// </summary>
public sealed class ResourceGeneratorModule : ModuleBase
{
    public const string TypeName = "ResourceGenerator";

    private readonly int _intervalTicks;
    private readonly long _amount;
    private int _ticksUntilPayout;

    public ResourceGeneratorModule(ModuleSpec spec) : base(spec)
    {
        _intervalTicks = (int)Math.Max(1, spec.GetLong("IntervalTicks", 30));
        _amount = spec.GetLong("Amount", 10);
        _ticksUntilPayout = _intervalTicks;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying)
        {
            return;
        }
        _ticksUntilPayout--;
        if (_ticksUntilPayout > 0)
        {
            return;
        }
        _ticksUntilPayout = _intervalTicks;
        world.AddTeamResources(self.Team, _amount);
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_ticksUntilPayout);
    public override void ReadState(CanonicalReader reader) => _ticksUntilPayout = reader.ReadInt();
}

/// <summary>
/// Minimal straight-line locomotion: moves toward Destination at Speed units per
/// tick. Stands in for the locomotor lane until P2 brings pathing; exists in P0
/// so the determinism gates exercise continuous fixed-point state every tick.
/// </summary>
public sealed class LinearMoverModule : ModuleBase
{
    public const string TypeName = "LinearMover";

    private readonly Fixed64 _speedPerTick;
    private FixedVector2 _destination;
    private bool _moving;

    public LinearMoverModule(ModuleSpec spec) : base(spec)
    {
        _speedPerTick = spec.GetFixed("SpeedPerTickRaw", Fixed64.FromFraction(1, 10));
    }

    public void SetDestination(FixedVector2 destination)
    {
        _destination = destination;
        _moving = true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_moving)
        {
            return;
        }
        var offset = _destination - self.Position;
        var distanceSquared = offset.LengthSquared();
        if (distanceSquared == Fixed64.Zero)
        {
            // Below Q32.32 squared-length precision the residual offset can be
            // nonzero yet unrepresentable as progress — snap instead of stalling
            // a hair short of the destination.
            self.SetPosition(_destination);
            _moving = false;
            return;
        }
        var distance = Fixed64.Sqrt(distanceSquared);
        if (distance <= _speedPerTick)
        {
            self.SetPosition(_destination);
            _moving = false;
            return;
        }
        var step = offset * (_speedPerTick / distance);
        self.SetPosition(self.Position + step);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteVector(_destination);
        writer.WriteBool(_moving);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _destination = reader.ReadVector();
        _moving = reader.ReadBool();
    }
}

/// <summary>
/// SlowDeathBehavior-shaped death interception (1,194 objects in the corpus):
/// claims the death, keeps the object in the world for DeathTicks (presentation
/// plays the death sequence there), then releases it for removal.
/// </summary>
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

/// <summary>
/// ProductionUpdate-shaped queue (385 objects in the corpus). Design data encodes
/// buildable entries as "Build:template-name" = build ticks and optional
/// "Cost:template-name" = resource cost (default 0). TryQueue debits the owning
/// team's resources and rejects unaffordable requests; TryCancel refunds.
///
/// Spawn phase matches the retail sim (dual-run oracle finding): a unit queued
/// by a command applied at tick T spawns DURING tick T + build_ticks — build
/// ticks T .. T+build_ticks-1 count down, and the completed head spawns on the
/// following update. Optional "RallyXRaw"/"RallyYRaw" design data (producer-
/// relative, Fixed64 raw) sends a spawned unit with a LinearMover walking from
/// the exit point to the rally point — the retail door-walk shape.
/// </summary>
public sealed class ProductionModule : ModuleBase
{
    public const string TypeName = "Production";

    public const int MaxQueueLength = 9;

    private readonly FixedVector2 _exitOffset;
    private readonly List<(string Template, int TicksRemaining)> _queue = new();

    public ProductionModule(ModuleSpec spec) : base(spec)
    {
        _exitOffset = new FixedVector2(
            spec.GetFixed("ExitOffsetXRaw", Fixed64.FromInt(2)),
            spec.GetFixed("ExitOffsetYRaw", Fixed64.Zero));
    }

    public int QueueLength => _queue.Count;

    public long CostOf(string templateName) => Math.Max(0, Spec.GetLong("Cost:" + templateName, 0));

    public bool TryQueue(SimWorld world, GameObject self, string templateName)
    {
        var buildTicks = Spec.GetLong("Build:" + templateName, -1);
        if (buildTicks <= 0 || _queue.Count >= MaxQueueLength)
        {
            return false;
        }
        var cost = CostOf(templateName);
        if (cost > 0)
        {
            if (world.TeamResources(self.Team) < cost)
            {
                return false;
            }
            world.AddTeamResources(self.Team, -cost);
        }
        _queue.Add((templateName, (int)buildTicks));
        return true;
    }

    /// <summary>Cancels the queue entry at <paramref name="index"/> and refunds its cost.</summary>
    public bool TryCancel(SimWorld world, GameObject self, int index)
    {
        if (index < 0 || index >= _queue.Count)
        {
            return false;
        }
        var (template, _) = _queue[index];
        _queue.RemoveAt(index);
        world.AddTeamResources(self.Team, CostOf(template));
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying || _queue.Count == 0)
        {
            return;
        }
        var (template, ticksRemaining) = _queue[0];
        if (ticksRemaining > 0)
        {
            _queue[0] = (template, ticksRemaining - 1);
            return;
        }
        // ticksRemaining == 0: the head completed last tick; spawn this tick
        // (command_tick + build_ticks). The next entry starts counting next tick.
        _queue.RemoveAt(0);
        var spawned = world.SpawnObject(template, self.Team, self.Position + _exitOffset);
        if (Spec.Data.ContainsKey("RallyXRaw") || Spec.Data.ContainsKey("RallyYRaw"))
        {
            var rally = self.Position + new FixedVector2(
                Spec.GetFixed("RallyXRaw", Fixed64.Zero),
                Spec.GetFixed("RallyYRaw", Fixed64.Zero));
            spawned.FindModule<LinearMoverModule>()?.SetDestination(rally);
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_queue.Count);
        foreach (var (template, ticksRemaining) in _queue)
        {
            writer.WriteString(template);
            writer.WriteInt(ticksRemaining);
        }
    }

    public override void ReadState(CanonicalReader reader)
    {
        _queue.Clear();
        var count = reader.ReadInt();
        for (var i = 0; i < count; i++)
        {
            var template = reader.ReadString();
            var ticksRemaining = reader.ReadInt();
            _queue.Add((template, ticksRemaining));
        }
    }
}

/// <summary>
/// StructureBody variant of ActiveBody (373 objects): identical health
/// semantics today; exists as its own type so templates map 1:1 to the SAGE
/// vocabulary and structure-specific armor rules have a home in P2.
/// </summary>
public sealed class StructureBodyModule : ModuleBase
{
    public const string TypeName = "StructureBody";

    public long Health { get; private set; }
    public long MaxHealth { get; }

    public StructureBodyModule(ModuleSpec spec) : base(spec)
    {
        MaxHealth = spec.GetLong("MaxHealth", 500);
        Health = MaxHealth;
    }

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (amount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(amount), "Damage must be non-negative");
        }
        if (Health == 0)
        {
            return true;
        }
        Health = Math.Max(0, Health - amount);
        if (Health == 0)
        {
            world.HandleDeath(self);
        }
        return true;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteLong(Health);
    public override void ReadState(CanonicalReader reader) => Health = reader.ReadLong();
}

/// <summary>ImmortalBody (229 objects): takes damage down to 1 health, never dies.</summary>
public sealed class ImmortalBodyModule : ModuleBase
{
    public const string TypeName = "ImmortalBody";

    public long Health { get; private set; }
    public long MaxHealth { get; }

    public ImmortalBodyModule(ModuleSpec spec) : base(spec)
    {
        MaxHealth = spec.GetLong("MaxHealth", 100);
        Health = MaxHealth;
    }

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (amount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(amount), "Damage must be non-negative");
        }
        Health = Math.Max(1, Health - amount);
        return true;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteLong(Health);
    public override void ReadState(CanonicalReader reader) => Health = reader.ReadLong();
}

/// <summary>
/// LifetimeUpdate (198 objects): the object expires after LifetimeTicks,
/// routed through the normal death pipeline so SlowDeath-class modules can
/// still claim the removal.
/// </summary>
public sealed class LifetimeModule : ModuleBase
{
    public const string TypeName = "Lifetime";

    private int _ticksRemaining;
    private bool _expired;

    public LifetimeModule(ModuleSpec spec) : base(spec)
    {
        _ticksRemaining = (int)Math.Max(1, spec.GetLong("LifetimeTicks", 300));
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_expired || self.IsDying)
        {
            return;
        }
        _ticksRemaining--;
        if (_ticksRemaining <= 0)
        {
            _expired = true;
            world.HandleDeath(self);
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksRemaining);
        writer.WriteBool(_expired);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksRemaining = reader.ReadInt();
        _expired = reader.ReadBool();
    }
}

/// <summary>
/// Shared shape for death-claiming modules that hold the object for a timer
/// (SlowDeath, StructureCollapse, KeepObjectDie). Subclasses differ only in
/// type name, default duration, and whether zero duration means forever.
/// </summary>
public abstract class TimedDeathModuleBase : ModuleBase
{
    private readonly int _holdTicks;
    private readonly bool _zeroMeansForever;
    private int _ticksRemaining;
    private bool _dying;

    protected TimedDeathModuleBase(ModuleSpec spec, string dataKey, long defaultTicks, bool zeroMeansForever)
        : base(spec)
    {
        var configured = spec.GetLong(dataKey, defaultTicks);
        _zeroMeansForever = zeroMeansForever && configured == 0;
        _holdTicks = (int)Math.Max(_zeroMeansForever ? 0 : 1, configured);
    }

    public bool IsDying => _dying;

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (_dying)
        {
            return true;
        }
        _dying = true;
        _ticksRemaining = _holdTicks;
        self.MarkDying();
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_dying || _zeroMeansForever)
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

/// <summary>DestroyDie (385 objects): explicit immediate removal on death.</summary>
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

/// <summary>
/// KeepObjectDie (268 objects): the corpse stays in the world — forever when
/// KeepTicks is 0 (cleanup lanes come later), else for KeepTicks.
/// </summary>
public sealed class KeepObjectDieModule : TimedDeathModuleBase
{
    public const string TypeName = "KeepObjectDie";

    public KeepObjectDieModule(ModuleSpec spec)
        : base(spec, "KeepTicks", 0, zeroMeansForever: true)
    {
    }
}

/// <summary>
/// StructureCollapseUpdate (656 objects): the collapsing building holds for
/// its rubble sequence, then releases for removal.
/// </summary>
public sealed class StructureCollapseModule : TimedDeathModuleBase
{
    public const string TypeName = "StructureCollapse";

    public StructureCollapseModule(ModuleSpec spec)
        : base(spec, "CollapseTicks", 45, zeroMeansForever: false)
    {
    }
}

/// <summary>
/// GettingBuiltBehavior-shaped construction gate (350 objects in the corpus):
/// the object spawns under construction and other economic modules idle until
/// the build completes. P1 scope: time-driven completion; builder escorting
/// arrives with the worker lane.
/// </summary>
public sealed class GettingBuiltModule : ModuleBase
{
    public const string TypeName = "GettingBuilt";

    private int _ticksRemaining;
    private bool _started;

    public GettingBuiltModule(ModuleSpec spec) : base(spec)
    {
        _ticksRemaining = (int)Math.Max(1, spec.GetLong("ConstructionTicks", 60));
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_started)
        {
            _started = true;
            self.SetUnderConstruction(true);
        }
        if (!self.IsUnderConstruction)
        {
            return;
        }
        _ticksRemaining--;
        if (_ticksRemaining <= 0)
        {
            self.SetUnderConstruction(false);
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_started);
        writer.WriteInt(_ticksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _started = reader.ReadBool();
        _ticksRemaining = reader.ReadInt();
    }
}
