using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

/// <summary>
/// RotWK banner-carrier level-2 spawn and fortress CastleBehavior unpack.
/// </summary>
public class BannerFortressModuleTests
{
    private static SimConfig BannerDeathConfig(bool destroyHorde, int? respawnTicks) => new(
        new[]
        {
            new ObjectTemplate("death-horde", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 500 }),
                new ModuleSpec(
                    BannerCarrierModule.TypeName,
                    new Dictionary<string, long>
                    {
                        ["MinLevel"] = 0,
                        ["DestroyHordeOnBannerDeath"] = destroyHorde ? 1 : 0,
                    }.Concat(respawnTicks.HasValue
                        ? new[] { new KeyValuePair<string, long>("RespawnTicks", respawnTicks.Value) }
                        : Array.Empty<KeyValuePair<string, long>>()),
                    new Dictionary<string, string> { ["BannerTemplate"] = "death-banner" }),
            }),
            new ObjectTemplate("death-banner", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 10 }),
            }),
        },
        randomSeed: 71,
        teamCount: 2);

    private static SimConfig Config() => new(
        new[]
        {
            new ObjectTemplate("horde", new[]
            {
                new ModuleSpec(HordeContainModule.TypeName, new Dictionary<string, long>
                {
                    ["MemberCount"] = 5,
                    ["MemberHealth"] = 100,
                }),
                new ModuleSpec(ExperienceLevelModule.TypeName, new Dictionary<string, long>
                {
                    ["LevelCap"] = 5,
                    ["RequiredExperience:2"] = 100,
                }),
                new ModuleSpec(
                    BannerCarrierModule.TypeName,
                    new Dictionary<string, long>
                    {
                        ["MinLevel"] = 2,
                        ["OffsetXRaw"] = Fixed64.FromInt(70).Raw,
                        ["OffsetYRaw"] = 0,
                    },
                    new Dictionary<string, string>
                    {
                        ["BannerTemplate"] = "banner",
                    }),
            }),
            new ObjectTemplate("banner", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 80 }),
            }),
            new ObjectTemplate("fortress", new[]
            {
                new ModuleSpec(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 5000 }),
                new ModuleSpec(
                    CastleBehaviorModule.TypeName,
                    new Dictionary<string, long>
                    {
                        ["PieceCount"] = 3,
                        ["OffsetXRaw:0"] = Fixed64.FromInt(10).Raw,
                        ["OffsetYRaw:0"] = 0,
                        ["OffsetZRaw:0"] = Fixed64.FromFraction(1, 2).Raw,
                        ["AngleRadiansRaw:0"] = Fixed64.FromFraction(1, 4).Raw,
                        ["OffsetXRaw:1"] = Fixed64.FromInt(-10).Raw,
                        ["OffsetYRaw:1"] = 0,
                        ["OffsetZRaw:1"] = Fixed64.FromInt(2).Raw,
                        ["AngleRadiansRaw:1"] = Fixed64.FromFraction(-1, 2).Raw,
                        ["OffsetXRaw:2"] = 0,
                        ["OffsetYRaw:2"] = 0,
                        ["OffsetZRaw:2"] = 0,
                        ["AngleRadiansRaw:2"] = 0,
                    },
                    new Dictionary<string, string>
                    {
                        ["PieceTemplate:0"] = "pad",
                        ["PieceTemplate:1"] = "pad",
                        ["PieceTemplate:2"] = "citadel",
                    }),
            }),
            new ObjectTemplate("citadel", new[]
            {
                new ModuleSpec(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 3000 }),
            }),
            new ObjectTemplate("pad", new[]
            {
                new ModuleSpec(StructureBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 500 }),
            }),
        },
        randomSeed: 42,
        teamCount: 2);

    [Fact]
    public void BannerSpawnsOnceWhenHordeReachesLevelTwo()
    {
        var world = new SimWorld(Config(), ModuleRegistry.CreateDefault());
        var horde = world.SpawnObject("horde", 0, FixedVector2.Zero);
        var exp = horde.FindModule<ExperienceLevelModule>()!;
        var bannerMod = horde.FindModule<BannerCarrierModule>()!;

        world.Tick();
        Assert.False(bannerMod.HasSpawned);
        Assert.Equal(1, exp.Level);
        Assert.Single(world.Objects);

        exp.GrantLevels(1); // level 2
        world.Tick();
        Assert.True(bannerMod.HasSpawned);
        Assert.Equal(2, world.Objects.Count);
        var banner = Assert.Single(world.Objects.Values, o => o.TemplateName == "banner");
        Assert.Equal(Fixed64.FromInt(70), banner.Position.X);
        Assert.Equal(Fixed64.Zero, banner.Position.Y);

        world.Tick();
        Assert.Equal(2, world.Objects.Count); // no second banner
    }

    [Fact]
    public void BannerSpawnsFromExperienceThreshold()
    {
        var world = new SimWorld(Config(), ModuleRegistry.CreateDefault());
        var horde = world.SpawnObject("horde", 0, FixedVector2.Zero);
        var exp = horde.FindModule<ExperienceLevelModule>()!;

        exp.GrantExperience(99);
        world.Tick();
        Assert.Equal(1, exp.Level);
        Assert.Single(world.Objects);

        exp.GrantExperience(1);
        Assert.Equal(2, exp.Level);
        world.Tick();
        Assert.Equal(2, world.Objects.Count);
    }

    [Fact]
    public void ExperienceUsesNextAuthoredRankRatherThanInventingMissingRanks()
    {
        var module = new ExperienceLevelModule(new ModuleSpec(
            ExperienceLevelModule.TypeName,
            new Dictionary<string, long>
            {
                ["LevelCap"] = 10,
                ["InitialLevel"] = 1,
                ["RequiredExperience:5"] = 25,
                ["RequiredExperience:10"] = 100,
            }));

        module.GrantExperience(25);
        Assert.Equal(5, module.Level);
        module.GrantExperience(75);
        Assert.Equal(10, module.Level);
    }

    [Fact]
    public void AuthoredDestroyHordeFlagExecutesWhenBannerDies()
    {
        var world = new SimWorld(BannerDeathConfig(destroyHorde: true, respawnTicks: 3), ModuleRegistry.CreateDefault());
        var horde = world.SpawnObject("death-horde", 0, FixedVector2.Zero);
        world.Tick();
        var banner = Assert.Single(world.Objects.Values, value => value.TemplateName == "death-banner");

        world.DealDamage(banner, 10);
        world.Tick();

        Assert.DoesNotContain(horde.Id, world.Objects.Keys);
        Assert.Empty(world.Objects);
    }

    [Fact]
    public void BannerRespawnsAfterRetailAuthoredLowerBound()
    {
        var world = new SimWorld(BannerDeathConfig(destroyHorde: false, respawnTicks: 3), ModuleRegistry.CreateDefault());
        var horde = world.SpawnObject("death-horde", 0, FixedVector2.Zero);
        world.Tick();
        var first = Assert.Single(world.Objects.Values, value => value.TemplateName == "death-banner");

        world.DealDamage(first, 10);
        world.Tick();
        var module = horde.FindModule<BannerCarrierModule>()!;
        Assert.False(module.HasLivingBanner);
        Assert.Equal(2, module.RespawnTicksRemaining);
        world.Tick();
        Assert.DoesNotContain(world.Objects.Values, value => value.TemplateName == "death-banner");

        world.Tick();
        var replacement = Assert.Single(world.Objects.Values, value => value.TemplateName == "death-banner");
        Assert.NotEqual(first.Id, replacement.Id);
        Assert.True(module.HasLivingBanner);
    }

    [Fact]
    public void BannerNeverRespawnsWhenRetailDidNotAuthorRespawnTimer()
    {
        var config = BannerDeathConfig(destroyHorde: false, respawnTicks: null);
        var world = new SimWorld(config, ModuleRegistry.CreateDefault());
        var horde = world.SpawnObject("death-horde", 0, FixedVector2.Zero);
        world.Tick();
        var banner = Assert.Single(world.Objects.Values, value => value.TemplateName == "death-banner");

        world.DealDamage(banner, 10);
        world.Advance(1_000);

        Assert.Contains(horde.Id, world.Objects.Keys);
        Assert.DoesNotContain(world.Objects.Values, value => value.TemplateName == "death-banner");
        Assert.False(horde.FindModule<BannerCarrierModule>()!.HasLivingBanner);
    }

    [Fact]
    public void BannerRespawnCountdownRoundTripsThroughSnapshot()
    {
        var config = BannerDeathConfig(destroyHorde: false, respawnTicks: 5);
        var registry = ModuleRegistry.CreateDefault();
        var original = new SimWorld(config, registry);
        original.SpawnObject("death-horde", 0, FixedVector2.Zero);
        original.Tick();
        var banner = Assert.Single(original.Objects.Values, value => value.TemplateName == "death-banner");
        original.DealDamage(banner, 10);
        original.Tick();
        var restored = SimWorld.Restore(original.Snapshot(), config, registry);

        for (var i = 0; i < 8; i++)
        {
            Assert.Equal(original.StateHash(), restored.StateHash());
            original.Tick();
            restored.Tick();
        }
    }

    [Fact]
    public void CastleBehaviorUnpacksCitadelAndPadsOnce()
    {
        var world = new SimWorld(Config(), ModuleRegistry.CreateDefault());
        var fortress = world.SpawnObject(
            "fortress",
            0,
            FixedVector2.Zero,
            Fixed64.FromInt(3),
            Fixed64.FromFraction(1, 8));
        var castle = fortress.FindModule<CastleBehaviorModule>()!;

        Assert.False(castle.HasUnpacked);
        world.Tick();
        Assert.True(castle.HasUnpacked);
        Assert.Equal(4, world.Objects.Count); // fortress + citadel + 2 pads
        Assert.Contains(world.Objects.Values, o => o.TemplateName == "citadel");
        Assert.Equal(2, world.Objects.Values.Count(o => o.TemplateName == "pad"));
        Assert.Contains(world.Objects.Values, o => o.TemplateName == "pad" && o.Position.X == Fixed64.FromInt(10));
        Assert.Contains(world.Objects.Values, o => o.TemplateName == "pad" && o.Position.X == Fixed64.FromInt(-10));
        var eastPad = Assert.Single(
            world.Objects.Values,
            o => o.TemplateName == "pad" && o.Position.X == Fixed64.FromInt(10));
        Assert.Equal(Fixed64.FromFraction(7, 2), eastPad.Elevation);
        Assert.Equal(Fixed64.FromFraction(3, 8), eastPad.HeadingRadians);
        var westPad = Assert.Single(
            world.Objects.Values,
            o => o.TemplateName == "pad" && o.Position.X == Fixed64.FromInt(-10));
        Assert.Equal(Fixed64.FromInt(5), westPad.Elevation);
        Assert.Equal(Fixed64.FromFraction(-3, 8), westPad.HeadingRadians);

        world.Tick();
        Assert.Equal(4, world.Objects.Count);
    }
}
