using Xunit;

namespace OpenBfme.Sim.Tests;

public sealed class ModuleBatchCUpgradeContainTests
{
    [Fact]
    public void RefundDieHonorsRequiredUpgradeBuildingAndPercent()
    {
        var refund = new ModuleSpec(RefundDieModule.TypeName,
            stringData: new Dictionary<string, string>
            {
                ["BuildingRequired"] = "ANY +MARKETPLACE",
                ["RefundPercent"] = "50%",
                ["UpgradeRequired"] = "Upgrade_Defiance",
            });
        var victimTemplate = new ObjectTemplate("victim", new ModuleSpec[]
        {
            new(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 100 }), refund,
        }, bodyHealth: Body(100), economy: new EconomyTemplate(200), techEnabled: true);
        var market = new ObjectTemplate("market", Array.Empty<ModuleSpec>(), kindOf: new[] { "STRUCTURE", "MARKETPLACE" });
        var world = World(new[] { victimTemplate, market });
        world.SpawnObject("market", 0, At(10));
        var victim = world.SpawnObject("victim", 0, At(0));
        Assert.True(world.GrantObjectUpgrade(victim, "Upgrade_Defiance"));

        world.DealDamage(victim, 100);

        Assert.Equal(100, world.TeamResources(0));
        Assert.True(victim.FindModule<RefundDieModule>()!.Refunded);
    }

    [Fact]
    public void SlavedUpdateDiesWithItsAssignedMaster()
    {
        var slaved = new ModuleSpec(SlavedUpdateModule.TypeName,
            data: new Dictionary<string, long> { ["DieOnMastersDeath"] = 1, ["MarkUnselectable"] = 1 });
        var masterTemplate = new ObjectTemplate("master", Array.Empty<ModuleSpec>());
        var slaveTemplate = new ObjectTemplate("slave", new[] { slaved }, bodyHealth: Body(100));
        var world = World(new[] { masterTemplate, slaveTemplate });
        var master = world.SpawnObject("master", 0, At(0));
        var slave = world.SpawnObject("slave", 0, At(2));
        slave.FindModule<SlavedUpdateModule>()!.SetMaster(master.Id);

        world.HandleDeath(master);
        world.Tick();

        Assert.Empty(world.Objects);
    }

    [Fact]
    public void ToggleMountedSpecialAbilityUsesPowerCommandAndConditionState()
    {
        var power = new SpecialPowerTemplate("Mount", "SPECIAL_POWER_MOUNT", 0, Array.Empty<string>(), false);
        var button = new CommandButtonTemplate("MountButton", "SPECIAL_POWER", "", "", "", power.Name);
        var set = new CommandSetTemplate("MountSet", new[] { new CommandSetEntryTemplate(1, button.Name, button) });
        var toggle = new ModuleSpec(ToggleMountedSpecialAbilityUpdateModule.TypeName,
            data: new Dictionary<string, long> { ["AwardXPForTriggering"] = 0, ["PackTime"] = 2_000,
                ["PreparationTime"] = 1, ["UnpackTime"] = 2_000 },
            stringData: new Dictionary<string, string> { ["SpecialPowerTemplate"] = power.Name });
        var template = new ObjectTemplate("hero", new[] { toggle }, bodyHealth: Body(100), commandSetName: set.Name);
        var world = new SimWorld(new SimConfig(new[] { template }, 11, 2, tech:
            new TechCatalog(specialPowers: new[] { power }, commandButtons: new[] { button },
                commandSets: new[] { set })), ModuleRegistry.CreateDefault(), 33);
        var hero = world.SpawnObject("hero", 0, At(0));
        Assert.True(world.SubmitCommand(TestWorlds.Command(1, 0, 0, "power",
            ("objects", CommandValue.OfLongList(new long[] { hero.Id })),
            ("name", CommandValue.OfString(power.Name)))));

        world.Tick();

        Assert.True(hero.FindModule<ToggleMountedSpecialAbilityUpdateModule>()!.IsMounted);
        Assert.True(hero.HasConditionToken("MOUNTED"));
    }

    [Fact]
    public void CitadelSlaughterConsumesPassengerAndPaysAuthoredCashback()
    {
        var contain = new ModuleSpec(CitadelSlaughterHordeContainModule.TypeName,
            data: new Dictionary<string, long> { ["AllowOwnPlayerInsideOverride"] = 1, ["ContainMax"] = 99 },
            stringData: new Dictionary<string, string> { ["CashBackPercent"] = "200%",
                ["PassengerFilter"] = "ANY +INFANTRY +CAVALRY -HERO",
                ["ObjectStatusOfContained"] = "UNSELECTABLE ENCLOSED" });
        var citadelTemplate = new ObjectTemplate("citadel", new[] { contain }, bodyHealth: Body(1_000));
        var passengerTemplate = new ObjectTemplate("orc", Array.Empty<ModuleSpec>(),
            economy: new EconomyTemplate(50), kindOf: new[] { "INFANTRY" });
        var world = World(new[] { citadelTemplate, passengerTemplate });
        var citadel = world.SpawnObject("citadel", 0, At(0));
        var passenger = world.SpawnObject("orc", 0, At(1));

        Assert.True(citadel.FindModule<CitadelSlaughterHordeContainModule>()!
            .TryEnter(world, citadel, passenger));
        world.Tick();

        Assert.Equal(100, world.TeamResources(0));
        Assert.DoesNotContain(passenger.Id, world.Objects.Keys);
    }

    [Fact]
    public void CastleUpgradePropagatesAuthoredUpgradeWithinWallRadius()
    {
        var castleUpgrade = new ModuleSpec(CastleUpgradeModule.TypeName,
            data: new Dictionary<string, long> { ["WallUpgradeRadiusRaw"] = Fixed64.FromInt(20).Raw },
            stringData: new Dictionary<string, string> { ["TriggeredBy"] = "Upgrade_Trigger",
                ["Upgrade"] = "Upgrade_Wall" });
        var wallUpgrade = new ModuleSpec(ModelConditionUpgradeModule.TypeName,
            stringData: new Dictionary<string, string> { ["TriggeredBy"] = "Upgrade_Wall",
                ["AddConditionFlags"] = "WALL_UPGRADED" });
        var castle = new ObjectTemplate("castle", new[] { castleUpgrade }, bodyHealth: Body(1_000),
            kindOf: new[] { "STRUCTURE" });
        var wall = new ObjectTemplate("wall", new[] { wallUpgrade }, bodyHealth: Body(500),
            kindOf: new[] { "STRUCTURE", "WALL" });
        var world = World(new[] { castle, wall });
        var castleObject = world.SpawnObject("castle", 0, At(0));
        var wallObject = world.SpawnObject("wall", 0, At(10));

        world.CompleteUpgrade(castleObject, ObjectUpgrade("Upgrade_Trigger"));

        Assert.Contains("Upgrade_Wall", wallObject.OwnedUpgrades);
        Assert.True(wallObject.HasConditionToken("WALL_UPGRADED"));
    }

    private static SimWorld World(IEnumerable<ObjectTemplate> templates) =>
        new(new SimConfig(templates, 23, 2), ModuleRegistry.CreateDefault(), 33);
    private static UpgradeTemplate ObjectUpgrade(string name) =>
        new(name, UpgradeType.Object, 0, 0, Array.Empty<string>());
    private static BodyHealthTemplate Body(int health) => new(Fixed64.FromInt(health));
    private static FixedVector2 At(int x, int y = 0) => new(Fixed64.FromInt(x), Fixed64.FromInt(y));
}
