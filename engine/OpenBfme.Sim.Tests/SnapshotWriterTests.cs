using System.Text.Json;
using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class SnapshotWriterTests
{
    [Fact]
    public void SnapshotMatchesContractAndTwinWorldBytesAtTickThreeHundred()
    {
        var launch = MatchLaunch.Load(MatchLaunchTests.RepoPath("contracts", "fixtures", "match-launch-v1.json"));
        var templates = new[]
        {
            new ObjectTemplate("soldier", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName, new Dictionary<string, long> { ["MaxHealth"] = 120 }),
                new ModuleSpec(LinearMoverModule.TypeName, new Dictionary<string, long>
                {
                    ["SpeedPerTickRaw"] = Fixed64.FromFraction(1, 4).Raw,
                }),
            }),
        };
        var first = BuildWorld(launch, templates);
        var second = BuildWorld(launch, templates);
        first.Advance(300);
        second.Advance(300);
        DecorateSnapshot(first);
        DecorateSnapshot(second);

        var firstBytes = SnapshotWriter.Write(first);
        var secondBytes = SnapshotWriter.Write(second);
        Assert.Equal(firstBytes, secondBytes);

        using var snapshot = JsonDocument.Parse(firstBytes);
        using var schema = JsonDocument.Parse(File.ReadAllText(
            MatchLaunchTests.RepoPath("contracts", "snapshot-v1.schema.json")));
        var root = snapshot.RootElement;
        foreach (var required in schema.RootElement.GetProperty("required").EnumerateArray())
        {
            Assert.True(root.TryGetProperty(required.GetString()!, out _), required.GetString());
        }
        Assert.Equal(300, root.GetProperty("tick").GetInt32());
        Assert.Equal(launch.Rules.TickMilliseconds, root.GetProperty("tick_ms").GetInt32());
        Assert.Equal(first.StateHash(), root.GetProperty("hash").GetString());
        Assert.Matches("^[0-9a-f]{64}$", root.GetProperty("hash").GetString()!);

        var objectCount = root.GetProperty("object_count").GetInt32();
        Assert.Equal(first.Objects.Count, objectCount);
        foreach (var property in root.GetProperty("objects").EnumerateObject())
        {
            Assert.Equal(objectCount, property.Value.GetArrayLength());
        }
        Assert.Equal(13, root.GetProperty("objects").EnumerateObject().Count());
        Assert.Equal(launch.Players.Count, root.GetProperty("players").GetArrayLength());
        Assert.Single(root.GetProperty("hordes").EnumerateArray());
        Assert.Equal(
            new[] { "damage", "sound" },
            root.GetProperty("events").EnumerateArray()
                .Select(item => item.GetProperty("kind").GetString())
                .ToArray());
    }

    private static SimWorld BuildWorld(MatchLaunch launch, IReadOnlyList<ObjectTemplate> templates)
    {
        var world = new SimWorld(launch, templates);
        var first = world.SpawnObject("soldier", 0, new FixedVector2(Fixed64.FromInt(1), Fixed64.FromInt(2)));
        first.FindModule<LinearMoverModule>()!.SetDestination(
            new FixedVector2(Fixed64.FromInt(30), Fixed64.FromInt(12)));
        world.SpawnObject("soldier", 1, new FixedVector2(Fixed64.FromInt(15), Fixed64.FromInt(20)));
        return world;
    }

    private static void DecorateSnapshot(SimWorld world)
    {
        world.AddHorde(new SnapshotHorde(100, 0, 0, new[] { 1, 2 }, 0));
        world.SetPlayerEconomy(0, 20, 300, 1);
        world.RaiseEvent(new SimEvent("damage", 1, 2, Fixed64.FromInt(25)));
        world.RaiseEvent(new SimEvent("sound", 1, Name: "GondorSoldierVoiceAttack"));
    }
}
