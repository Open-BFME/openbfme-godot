using System.Reflection;

namespace OpenBfme.Sim;

public enum ModuleTier
{
    Structural,
    Cosmetic,
}
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class SageModuleAttribute : Attribute
{
    public SageModuleAttribute(string name, ModuleTier tier, bool kernel = false)
    {
        Name = name ?? throw new ArgumentNullException(nameof(name));
        Tier = tier;
        Kernel = kernel;
    }

    public string Name { get; }
    public ModuleTier Tier { get; }
    public bool Kernel { get; }
}

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
    public ModuleTier Tier { get; }

    private readonly SortedDictionary<string, long> _data;
    private readonly SortedDictionary<string, string> _stringData;

    public ModuleSpec(
        string typeName,
        IEnumerable<KeyValuePair<string, long>>? data = null,
        IEnumerable<KeyValuePair<string, string>>? stringData = null,
        ModuleTier tier = ModuleTier.Cosmetic)
    {
        TypeName = typeName ?? throw new ArgumentNullException(nameof(typeName));
        Tier = tier;
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
    public IReadOnlyList<WeaponSet> WeaponSets { get; }
    public IReadOnlyList<ArmorSet> ArmorSets { get; }
    public BodyHealthTemplate? BodyHealth { get; }

    public ObjectTemplate(
        string name,
        IReadOnlyList<ModuleSpec> modules,
        IReadOnlyList<WeaponSet>? weaponSets = null,
        IReadOnlyList<ArmorSet>? armorSets = null,
        BodyHealthTemplate? bodyHealth = null)
    {
        Name = name ?? throw new ArgumentNullException(nameof(name));
        Modules = modules ?? throw new ArgumentNullException(nameof(modules));
        WeaponSets = weaponSets?.ToArray() ?? Array.Empty<WeaponSet>();
        ArmorSets = armorSets?.ToArray() ?? Array.Empty<ArmorSet>();
        if (bodyHealth is { MaxHealth: var maximum, InitialHealth: var initial }
            && (maximum <= Fixed64.Zero || initial < Fixed64.Zero || initial > maximum))
        {
            throw new ArgumentOutOfRangeException(nameof(bodyHealth));
        }
        BodyHealth = bodyHealth;
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

    public IReadOnlyCollection<string> RegisteredNames => _factories.Keys;

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

    public static ModuleRegistry Discover(Assembly assembly)
    {
        ArgumentNullException.ThrowIfNull(assembly);
        var registry = new ModuleRegistry();
        var discovered = assembly.GetTypes()
            .Select(type => (Type: type, Attribute: type.GetCustomAttribute<SageModuleAttribute>()))
            .Where(item => item.Attribute != null)
            .OrderBy(item => item.Attribute!.Name, StringComparer.Ordinal)
            .ToArray();
        foreach (var item in discovered)
        {
            var type = item.Type;
            var attribute = item.Attribute!;
            if (!typeof(ModuleBase).IsAssignableFrom(type) || type.IsAbstract)
            {
                throw new InvalidOperationException(
                    $"[SageModule] type '{type.FullName}' must be a non-abstract ModuleBase");
            }
            var constructor = type.GetConstructor(new[] { typeof(ModuleSpec) })
                ?? throw new InvalidOperationException(
                    $"[SageModule] type '{type.FullName}' needs a public ModuleSpec constructor");
            registry.Register(attribute.Name, spec =>
                (ModuleBase)constructor.Invoke(new object[] { spec }));
        }
        return registry;
    }

    /// <summary>Discovers the P0/P1 vocabulary directly from module attributes.</summary>
    public static ModuleRegistry CreateDefault() => Discover(typeof(ModuleRegistry).Assembly);
}

/// <summary>
/// Shared shape for death-claiming modules that hold the object for a timer
/// (SlowDeathBehavior, StructureCollapseUpdate, KeepObjectDie). Subclasses differ only in
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
            world.CompleteClaimedDeath(self, this);
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
