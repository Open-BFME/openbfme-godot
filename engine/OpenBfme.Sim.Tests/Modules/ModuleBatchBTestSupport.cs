namespace OpenBfme.Sim.Tests.Modules;

internal static class ModuleBatchBTestSupport
{
    public static FixedVector2 At(int x, int y = 0) =>
        new(Fixed64.FromInt(x), Fixed64.FromInt(y));

    public static ModuleSpec Spec(
        string type,
        IReadOnlyDictionary<string, long>? data = null,
        IReadOnlyDictionary<string, string>? strings = null) => new(type, data, strings);

    public static ObjectTemplate Template(
        string name,
        IEnumerable<ModuleSpec> modules,
        int health = 100,
        IReadOnlyList<string>? kindOf = null,
        IReadOnlyList<WeaponSet>? weaponSets = null,
        string commandSet = "",
        EconomyTemplate? economy = null,
        bool techEnabled = false) => new(
            name,
            modules.ToArray(),
            weaponSets,
            bodyHealth: health > 0 ? new BodyHealthTemplate(Fixed64.FromInt(health)) : null,
            economy: economy,
            commandSetName: commandSet,
            techEnabled: techEnabled,
            kindOf: kindOf);

    public static SimWorld World(
        IEnumerable<ObjectTemplate> templates,
        IEnumerable<WeaponTemplate>? weapons = null,
        TechCatalog? tech = null,
        int tickMilliseconds = 100) => new(
            new SimConfig(templates, 991, 2, weaponTemplates: weapons, tech: tech),
            ModuleRegistry.CreateDefault(), tickMilliseconds);

    public static WeaponTemplate Weapon(string name, int damage, int radius = 0, DamageType type = DamageType.DEFAULT) =>
        new(name, Fixed64.FromInt(100), Fixed64.Zero, 0, 0, PreAttackType.PER_SHOT,
            0, 0, 0, new[] { new DamageNugget(Fixed64.FromInt(damage), Fixed64.FromInt(radius),
                0, type, "", "NORMAL") });
}
