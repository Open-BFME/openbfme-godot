namespace OpenBfme.Sim;

public sealed class GameObject
{
    internal int StoreSlot { get; set; }
    public int Id { get; }
    public string TemplateName { get; }
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

    internal GameObject(
        int id,
        string templateName,
        int team,
        FixedVector2 position,
        IReadOnlyList<ModuleBase> modules,
        Fixed64 elevation = default,
        Fixed64 headingRadians = default)
    {
        Id = id;
        TemplateName = templateName;
        Team = team;
        Position = position;
        Elevation = elevation;
        HeadingRadians = headingRadians;
        Modules = modules;
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
        foreach (var module in Modules)
        {
            module.WriteState(writer);
        }
    }
}
