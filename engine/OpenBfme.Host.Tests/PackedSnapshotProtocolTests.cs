using System.Buffers.Binary;
using System.IO.Compression;
using System.Text.Json;
using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Host.Tests;

public sealed class PackedSnapshotProtocolTests
{
    private static readonly string[] IntColumns =
        ["id", "template", "owner", "state", "anim", "flags"];
    private static readonly string[] FloatColumns =
        ["x", "y", "z", "yaw", "health", "max_health", "anim_frame"];

    [Fact]
    public void PackedObjectColumnsDecodeToTheJsonSnapshotForTheSameTick()
    {
        var templates = new[]
        {
            new ObjectTemplate("unit", new[]
            {
                new ModuleSpec(ActiveBodyModule.TypeName,
                    new Dictionary<string, long> { ["MaxHealth"] = 125 }),
            }),
        };
        var world = new SimWorld(new SimConfig(templates, randomSeed: 17, teamCount: 2),
            ModuleRegistry.CreateDefault());
        world.SpawnObject("unit", 1,
            new FixedVector2(Fixed64.FromFraction(17, 4), Fixed64.FromFraction(-9, 2)),
            Fixed64.FromFraction(7, 8), Fixed64.FromFraction(3, 16));
        world.Tick();

        using var json = JsonDocument.Parse(SnapshotWriter.WriteJson(world));
        using var packed = JsonDocument.Parse(PackedSnapshotWriter.WriteJson(world));
        var expected = json.RootElement;
        var actual = packed.RootElement;

        foreach (var name in new[] { "schema", "tick", "tick_ms", "hash", "object_count" })
            Assert.Equal(expected.GetProperty(name).GetRawText(), actual.GetProperty(name).GetRawText());
        foreach (var name in new[] { "hordes", "players", "events" })
            Assert.Equal(expected.GetProperty(name).GetRawText(), actual.GetProperty(name).GetRawText());

        AssertObjectColumnsEqual(expected, actual);
    }

    internal static void AssertObjectColumnsEqual(JsonElement expected, JsonElement actual)
    {
        var objectCount = expected.GetProperty("object_count").GetInt32();
        var packedObjects = actual.GetProperty("objects");
        Assert.Equal(PackedSnapshotWriter.ObjectFormat,
            packedObjects.GetProperty("format").GetString());
        Assert.True(packedObjects.GetProperty("full").GetBoolean());
        var compressed = Convert.FromBase64String(packedObjects.GetProperty("data").GetString()!);
        using var input = new MemoryStream(compressed);
        using var brotli = new BrotliStream(input, CompressionMode.Decompress);
        using var output = new MemoryStream();
        brotli.CopyTo(output);
        var bytes = output.ToArray();
        Assert.Equal(objectCount * PackedSnapshotWriter.ColumnCount * 4, bytes.Length);

        var expectedObjects = expected.GetProperty("objects");
        for (var column = 0; column < IntColumns.Length; column++)
        {
            var values = expectedObjects.GetProperty(IntColumns[column]);
            for (var slot = 0; slot < objectCount; slot++)
            {
                var offset = (column * objectCount + slot) * 4;
                Assert.Equal(values[slot].GetInt32(),
                    BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(offset, 4)));
            }
        }
        for (var index = 0; index < FloatColumns.Length; index++)
        {
            var column = IntColumns.Length + index;
            var values = expectedObjects.GetProperty(FloatColumns[index]);
            for (var slot = 0; slot < objectCount; slot++)
            {
                var offset = (column * objectCount + slot) * 4;
                var decoded = BitConverter.Int32BitsToSingle(
                    BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(offset, 4)));
                Assert.Equal(values[slot].GetSingle(), decoded);
            }
        }
    }
}
