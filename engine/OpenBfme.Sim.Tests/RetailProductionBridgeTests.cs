using OpenBfme.Sim;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

public sealed class RetailProductionBridgeTests
{
    private readonly ITestOutputHelper _output;

    public RetailProductionBridgeTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void FixtureHordeRanksDriveProductionMembersAndHordeEconomy()
    {
        var document = BundleDocument.Load(MatchLaunchTests.RepoPath(
            "contracts", "fixtures", "bundle-v1.json"));
        var loaded = BundleTemplateLoader.Load(document, ModuleRegistry.CreateDefault(), 33);
        var definition = Assert.Single(loaded.HordeTemplates);
        Assert.Equal("CookHorde", definition.Name);
        Assert.Equal(3, definition.MemberCount);
        Assert.All(definition.Ranks, rank => Assert.Equal("CookMember", rank.UnitType));

        var source = Assert.Single(loaded.Templates, value => value.Name == definition.Name);
        var trainable = new ObjectTemplate(
            source.Name,
            source.Modules,
            source.WeaponSets,
            source.ArmorSets,
            source.BodyHealth,
            new EconomyTemplate(buildCost: 333, buildTimeMilliseconds: 33, commandPoints: 17),
            source.CommandSetName,
            source.HasTechState,
            source.Side,
            source.KindOf);
        var producerTemplate = new ObjectTemplate(
            "FixtureProducer",
            new ModuleSpec[] { new(ProductionModule.TypeName) },
            economy: new EconomyTemplate(
                commandSet: new[] { definition.Name },
                productionExitOffset: At(5, 0)));
        var templates = loaded.Templates
            .Where(value => value.Name != definition.Name)
            .Append(trainable)
            .Append(producerTemplate);
        var config = new SimConfig(
            templates,
            randomSeed: 17,
            teamCount: 1,
            maxCommandPoints: 100,
            hordeTemplates: loaded.HordeTemplates);
        var world = new SimWorld(config, ModuleRegistry.CreateDefault(), 33);
        world.AddTeamResources(0, 1_000);
        var producer = world.SpawnObject(producerTemplate.Name, 0, At(10, 10));
        Assert.True(world.SubmitCommand(Rally(1, 0, producer.Id, At(40, 30))));
        Assert.True(world.SubmitCommand(Train(1, 1, producer.Id, definition.Name)));

        world.Tick();

        Assert.Equal(667, world.TeamResources(0));
        Assert.Equal(1, producer.FindModule<ProductionModule>()!.QueueLength);
        world.Tick();

        var horde = Assert.Single(world.Hordes);
        Assert.Equal(definition.MemberCount, horde.Members.Count);
        Assert.Equal(At(15, 10), world.Objects[horde.Id].Position);
        Assert.Equal(new[] { At(17, 10), At(17, 8), At(15, 11) },
            horde.Members.Select(id => world.Objects[id].Position));
        Assert.All(horde.Members, id => Assert.Equal("CookMember", world.Objects[id].TemplateName));
        Assert.Equal(17, world.CommandPointsUsed(0));
    }

    [Fact]
    public void CorpusGondorBarracksTrainsAuthoredFifteenMemberFighterHorde()
    {
        var path = MatchLaunchTests.RepoPath(
            "workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");
        if (!File.Exists(path))
        {
            _output.WriteLine($"SKIP: corpus bundle absent at {path}; retail production proof unavailable");
            return;
        }

        var document = BundleDocument.Load(path);
        var launch = HumanLaunch(startingResources: 1_000);
        var world = SimWorld.FromBundle(launch, document);
        var producer = world.SpawnObject("GondorBarracks", 0, At(100, 100));
        world.Advance(producer.Template.Economy.BuildTicks(world.TickMilliseconds) + 1);
        Assert.False(producer.IsUnderConstruction);
        var authored = Assert.Single(document.Hordes!, value => value.Name == "GondorFighterHorde");
        var authoredMembers = authored.RankInfo.Sum(rank => rank.Positions.Count);
        Assert.Equal(15, authoredMembers);
        Assert.DoesNotContain(world.BundleLoadReport!.TemplatesFailed,
            value => value.Template == authored.Name);
        var commandTick = world.TickIndex + 1;
        Assert.True(world.SubmitCommand(Rally(commandTick, 0, producer.Id, At(200, 200))));
        Assert.True(world.SubmitCommand(Train(commandTick, 1, producer.Id, authored.Name)));

        world.Tick();

        Assert.Equal(750, world.TeamResources(0));
        Assert.Equal(1, producer.FindModule<ProductionModule>()!.QueueLength);
        var buildTicks = EconomyTemplate.MillisecondsToTicks(20_000, 33);
        world.Advance(buildTicks);

        var horde = Assert.Single(world.Hordes);
        Assert.Equal(authoredMembers, horde.Members.Count);
        Assert.Equal(authored.Name, world.Objects[horde.Id].TemplateName);
        Assert.Equal(At(72, 68), world.Objects[horde.Id].Position);
        var expectedTypes = authored.RankInfo
            .OrderBy(rank => rank.Rank)
            .SelectMany(rank => rank.Positions.Select(_ => rank.UnitType));
        Assert.Equal(expectedTypes, horde.Members.Select(id => world.Objects[id].TemplateName));
        foreach (var memberId in horde.Members)
        {
            var locomotor = Assert.IsType<LocomotorModule>(world.Objects[memberId]
                .FindModule<LocomotorModule>());
            Assert.True(locomotor.HasOrder);
            Assert.Equal(At(200, 200), locomotor.Destination);
        }
        Assert.Equal(60, world.CommandPointsUsed(0));
        _output.WriteLine($"retail production: producer={producer.TemplateName} horde={authored.Name} " +
            $"members={horde.Members.Count} cost=250 build_ticks={buildTicks} command_points=60 exit=72:68");
    }

    [Fact]
    public void CorpusProducedOpposingHordesReformMoveAndDealDamage()
    {
        var path = MatchLaunchTests.RepoPath(
            "workspace", "logs", "lane-cook-c", "corpus-bundle-full.json");
        if (!File.Exists(path))
        {
            _output.WriteLine($"SKIP: corpus bundle absent at {path}; retail horde movement proof unavailable");
            return;
        }

        var document = BundleDocument.Load(path);
        var world = SimWorld.FromBundle(TwoHumanLaunch(), document);
        var leftProducer = world.SpawnObject("GondorBarracks", 0, At(20, 50));
        var rightProducer = world.SpawnObject("MordorOrcPit", 1, At(80, 50));
        world.SpawnProducedObject(leftProducer, "GondorFighterHorde", At(20, 50), null);
        world.SpawnProducedObject(rightProducer, "MordorFighterHorde", At(80, 50), null);
        var left = Assert.Single(world.Hordes, value => value.Owner == 0);
        var right = Assert.Single(world.Hordes, value => value.Owner == 1);
        Assert.True(world.SubmitCommand(AttackMove(1, 0, left.Id, At(50, 50))));
        Assert.True(world.SubmitCommand(AttackMove(1, 0, right.Id, At(50, 50), team: 1)));

        var damageEvents = 0;
        for (var tick = 0; tick < 600; tick++)
        {
            world.Tick();
            damageEvents += world.EventsThisTick.Count(value => value.Kind == "damage");
        }

        Assert.True(damageEvents > 0, "produced retail hordes never dealt damage");
    }

    private static MatchLaunch HumanLaunch(long startingResources) => new(
        MatchLaunch.SchemaName,
        17UL,
        new MatchLaunchPack("retail-production", new string('0', 64)),
        new MatchLaunchMap("maps/retail-production/retail-production.map", null),
        new MatchLaunchRules(
            33,
            startingResources,
            Fixed64.One,
            false,
            Fixed64.One,
            "annihilation",
            false,
            new SortedDictionary<string, bool>(StringComparer.Ordinal)),
        new[] { new MatchLaunchPlayer(0, 0, "FactionMen", "human", null, null, 0, null, null, "Men") },
        "skirmish",
        null);

    private static MatchLaunch TwoHumanLaunch() => new(
        MatchLaunch.SchemaName,
        19UL,
        new MatchLaunchPack("retail-horde-combat", new string('0', 64)),
        new MatchLaunchMap("maps/retail-horde-combat/retail-horde-combat.map", null),
        new MatchLaunchRules(
            33,
            10_000,
            Fixed64.One,
            false,
            Fixed64.One,
            "annihilation",
            false,
            new SortedDictionary<string, bool>(StringComparer.Ordinal)),
        new[]
        {
            new MatchLaunchPlayer(0, 0, "FactionMen", "human", null, null, 0, null, null, "Men"),
            new MatchLaunchPlayer(1, 1, "FactionMordor", "human", null, null, 1, null, null, "Mordor"),
        },
        "skirmish",
        null);

    private static SimCommand Train(int tick, int sequence, int producerId, string template) =>
        TestWorlds.Command(tick, 0, sequence, "train",
            ("objects", CommandValue.OfLongList(new long[] { producerId })),
            ("template", CommandValue.OfString(template)),
            ("count", CommandValue.OfLong(1)));

    private static SimCommand Rally(
        int tick,
        int sequence,
        int producerId,
        FixedVector2 destination) =>
        TestWorlds.Command(tick, 0, sequence, "rally",
            ("objects", CommandValue.OfLongList(new long[] { producerId })),
            ("x", CommandValue.OfFixed(destination.X)),
            ("y", CommandValue.OfFixed(destination.Y)));

    private static SimCommand AttackMove(
        int tick,
        int sequence,
        int hordeId,
        FixedVector2 destination,
        int team = 0) =>
        TestWorlds.Command(tick, team, sequence, "attack_move",
            ("objects", CommandValue.OfLongList(new long[] { hordeId })),
            ("x", CommandValue.OfFixed(destination.X)),
            ("y", CommandValue.OfFixed(destination.Y)));

    private static FixedVector2 At(int x, int y) =>
        new(Fixed64.FromInt(x), Fixed64.FromInt(y));
}
