namespace OpenBfme.Sim;

public sealed class GameObject
{
    internal int StoreSlot { get; set; }
    public int Id { get; }
    public string TemplateName { get; }
    public ObjectTemplate Template { get; }
    public int Team { get; }
    public FixedVector2 Position { get; private set; }
    /// <summary>Authoritative vertical placement in world units.</summary>
    public Fixed64 Elevation { get; private set; }
    /// <summary>Authoritative counter-clockwise facing in radians.</summary>
    public Fixed64 HeadingRadians { get; private set; }
    public bool IsDead { get; private set; }
    /// <summary>Death claimed by a module (SlowDeath): still in the world, no longer operational.</summary>
    public bool IsDying { get; private set; }
    public bool IsUnderConstruction { get; private set; }
    public IReadOnlyList<ModuleBase> Modules { get; }
    internal CombatState? Combat { get; }

    internal GameObject(
        int id,
        ObjectTemplate template,
        int team,
        FixedVector2 position,
        IReadOnlyList<ModuleBase> modules,
        Fixed64 elevation = default,
        Fixed64 headingRadians = default)
    {
        Id = id;
        Template = template ?? throw new ArgumentNullException(nameof(template));
        TemplateName = template.Name;
        Team = team;
        Position = position;
        Elevation = elevation;
        HeadingRadians = headingRadians;
        Modules = modules;
        if (template.BodyHealth != null || template.WeaponSets.Count > 0 || template.ArmorSets.Count > 0)
        {
            Combat = new CombatState(template);
        }
    }

    public void SetPosition(FixedVector2 position) => Position = position;

    public void SetTransform(FixedVector2 position, Fixed64 elevation, Fixed64 headingRadians)
    {
        Position = position;
        Elevation = elevation;
        HeadingRadians = headingRadians;
    }

    public void MarkDead() => IsDead = true;

    public void MarkDying() => IsDying = true;

    public void SetUnderConstruction(bool value) => IsUnderConstruction = value;

    internal (Fixed64 Initial, Fixed64 Maximum) ConstructionHealthBounds()
    {
        if (Template.BodyHealth is { } body) return (body.InitialHealth, body.MaxHealth);
        if (FindModule<StructureBodyModule>() is { } structure)
        {
            return (
                Fixed64.FromInt64(Math.Clamp(
                    structure.Spec.GetLong("InitialHealth", structure.MaxHealth),
                    0,
                    structure.MaxHealth)),
                Fixed64.FromInt64(structure.MaxHealth));
        }
        if (FindModule<ActiveBodyModule>() is { } active)
        {
            return (
                Fixed64.FromInt64(Math.Clamp(
                    active.Spec.GetLong("InitialHealth", active.MaxHealth),
                    0,
                    active.MaxHealth)),
                Fixed64.FromInt64(active.MaxHealth));
        }
        return (Fixed64.Zero, Fixed64.Zero);
    }

    internal void SetConstructionHealth(Fixed64 health)
    {
        if (Combat is { HasBody: true } combat)
        {
            combat.Health = Fixed64.Min(combat.MaxHealth, Fixed64.Max(Fixed64.Zero, health));
            return;
        }
        var integerHealth = Math.Max(0, health.Raw >> Fixed64.FractionBits);
        FindModule<StructureBodyModule>()?.SetConstructionHealth(integerHealth);
        FindModule<ActiveBodyModule>()?.SetConstructionHealth(integerHealth);
    }

    public void SetConditionToken(string token, bool enabled = true) =>
        (Combat ?? throw new InvalidOperationException("Object template has no combat data"))
            .SetCondition(token, enabled);

    public Fixed64 Health => Combat?.HasBody == true ? Combat.Health : Fixed64.Zero;
    public Fixed64 MaxHealth => Combat?.HasBody == true ? Combat.MaxHealth : Fixed64.Zero;

    public T? FindModule<T>() where T : ModuleBase
    {
        foreach (var module in Modules)
        {
            if (module is T typed)
            {
                return typed;
            }
        }
        return null;
    }

    internal void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(Id);
        writer.WriteString(TemplateName);
        writer.WriteInt(Team);
        writer.WriteVector(Position);
        writer.WriteLong(Elevation.Raw);
        writer.WriteLong(HeadingRadians.Raw);
        writer.WriteBool(IsDead);
        writer.WriteBool(IsDying);
        writer.WriteBool(IsUnderConstruction);
        Combat?.Write(writer);
        foreach (var module in Modules)
        {
            module.WriteState(writer);
        }
    }
}
