using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class MatchLaunchTests
{
    [Fact]
    public void GoldenFixtureParsesEveryValue()
    {
        var launch = MatchLaunch.Load(RepoPath("contracts", "fixtures", "match-launch-v1.json"));

        Assert.Equal(MatchLaunch.SchemaName, launch.Schema);
        Assert.Equal(20260901UL, launch.Seed);
        Assert.Equal("rotwk-full", launch.Pack.Id);
        Assert.Equal(new string('0', 64), launch.Pack.Sha256);
        Assert.Equal("maps/map mp fords of isen ii/map mp fords of isen ii.map", launch.Map.Path);
        Assert.Null(launch.Map.Sha256);
        Assert.Equal(33, launch.Rules.TickMilliseconds);
        Assert.Equal(1000, launch.Rules.StartingResources);
        Assert.Equal(Fixed64.One, launch.Rules.CommandPointMultiplier);
        Assert.True(launch.Rules.FogOfWar);
        Assert.Equal(Fixed64.One, launch.Rules.GameSpeed);
        Assert.Equal("annihilation", launch.Rules.Victory);
        Assert.False(launch.Rules.Classic);
        Assert.True(launch.Rules.Improvements["flow_field_pathing"]);
        Assert.True(launch.Rules.Improvements["queue_across_buildings"]);
        Assert.Equal(2, launch.Players.Count);
        Assert.Equal(new MatchLaunchPlayer(0, 0, "FactionMen", "human", null, 0, 0, null, null, "Player"), launch.Players[0]);
        Assert.Equal(new MatchLaunchPlayer(1, 1, "FactionMordor", "ai", "hard", 3, 1, null, null, null), launch.Players[1]);
        Assert.Equal("skirmish", launch.Mode);
        Assert.Null(launch.Mission);
    }

    [Fact]
    public void RequiredFieldsAndSchemaFailuresNameTheField()
    {
        var missing = Assert.Throws<MatchLaunchException>(() => MatchLaunch.Parse("{}"));
        Assert.Contains("schema", missing.Message, StringComparison.Ordinal);

        var wrongSchema = Assert.Throws<MatchLaunchException>(() => MatchLaunch.Parse("""
            { "schema": "wrong", "seed": 0, "pack": {}, "map": {}, "rules": {}, "players": [] }
            """));
        Assert.Contains("schema", wrongSchema.Message, StringComparison.Ordinal);

        var fixture = File.ReadAllText(RepoPath("contracts", "fixtures", "match-launch-v1.json"));
        var missingTick = fixture.Replace("\"tick_ms\": 33,", "", StringComparison.Ordinal);
        var tickError = Assert.Throws<MatchLaunchException>(() => MatchLaunch.Parse(missingTick));
        Assert.Contains("rules.tick_ms", tickError.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void SimWorldUsesLaunchTickSeedAndStartingResources()
    {
        var launch = MatchLaunch.Load(RepoPath("contracts", "fixtures", "match-launch-v1.json"));
        var first = new SimWorld(launch);
        var second = new SimWorld(launch);

        Assert.Equal(33, first.TickMilliseconds);
        Assert.Equal(1000, first.TeamResources(0));
        Assert.Equal(1000, first.TeamResources(1));
        Assert.Equal(first.NextRandomUInt32(), second.NextRandomUInt32());
    }

    internal static string RepoPath(params string[] parts)
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory != null && !File.Exists(Path.Combine(directory.FullName, "AGENTS.md")))
        {
            directory = directory.Parent;
        }
        Assert.NotNull(directory);
        return Path.Combine(new[] { directory!.FullName }.Concat(parts).ToArray());
    }
}
