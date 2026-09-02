using System.Text;
using System.Text.Json;

namespace OpenBfme.Sim;

/// <summary>Deterministic UTF-8 writer for contracts/snapshot-v1.</summary>
public static class SnapshotWriter
{
    public const string SchemaName = "openbfme.snapshot.v1";

    public static byte[] Write(SimWorld world)
    {
        ArgumentNullException.ThrowIfNull(world);
        using var stream = new MemoryStream();
        Write(world, stream);
        return stream.ToArray();
    }

    public static string WriteJson(SimWorld world) => Encoding.UTF8.GetString(Write(world));

    public static void Write(SimWorld world, Stream destination)
    {
        ArgumentNullException.ThrowIfNull(world);
        ArgumentNullException.ThrowIfNull(destination);
        world.SynchronizeObjectStore();
        var store = world.ObjectStore;
        var slots = store.LiveSlots().ToArray();
        using var writer = new Utf8JsonWriter(destination, new JsonWriterOptions
        {
            Indented = false,
            SkipValidation = false,
        });
        writer.WriteStartObject();
        writer.WriteString("schema", SchemaName);
        writer.WriteNumber("tick", world.TickIndex);
        writer.WriteNumber("tick_ms", world.TickMilliseconds);
        writer.WriteString("hash", world.StateHash());
        writer.WriteNumber("object_count", slots.Length);
        WriteObjects(writer, store, slots);
        WriteHordes(writer, world.Hordes);
        WritePlayers(writer, world.SnapshotPlayers());
        WriteEvents(writer, world.EventsThisTick);
        writer.WriteEndObject();
        writer.Flush();
    }

    private static void WriteObjects(Utf8JsonWriter writer, ObjectStore store, IReadOnlyList<int> slots)
    {
        writer.WritePropertyName("objects");
        writer.WriteStartObject();
        WriteArray(writer, "id", slots, slot => writer.WriteNumberValue(store.Id[slot]));
        WriteArray(writer, "template", slots, slot => writer.WriteNumberValue(store.TemplateIndex[slot]));
        WriteArray(writer, "owner", slots, slot => writer.WriteNumberValue(store.Owner[slot]));
        WriteArray(writer, "x", slots, slot => WriteFixed(writer, store.X[slot]));
        WriteArray(writer, "y", slots, slot => WriteFixed(writer, store.Y[slot]));
        WriteArray(writer, "z", slots, slot => WriteFixed(writer, store.Z[slot]));
        WriteArray(writer, "yaw", slots, slot => WriteFixed(writer, store.Yaw[slot]));
        WriteArray(writer, "health", slots, slot => WriteFixed(writer, store.Health[slot]));
        WriteArray(writer, "max_health", slots, slot => WriteFixed(writer, store.MaxHealth[slot]));
        WriteArray(writer, "state", slots, slot => writer.WriteNumberValue(store.State[slot]));
        WriteArray(writer, "anim", slots, slot => writer.WriteNumberValue(store.Anim[slot]));
        WriteArray(writer, "anim_frame", slots, slot => WriteFixed(writer, store.AnimFrame[slot]));
        WriteArray(writer, "flags", slots, slot => writer.WriteNumberValue(store.Flags[slot]));
        writer.WriteEndObject();
    }

    private static void WriteArray(
        Utf8JsonWriter writer,
        string name,
        IReadOnlyList<int> slots,
        Action<int> writeValue)
    {
        writer.WritePropertyName(name);
        writer.WriteStartArray();
        foreach (var slot in slots)
        {
            writeValue(slot);
        }
        writer.WriteEndArray();
    }

    private static void WriteHordes(Utf8JsonWriter writer, IReadOnlyList<SnapshotHorde> hordes)
    {
        writer.WritePropertyName("hordes");
        writer.WriteStartArray();
        foreach (var horde in hordes)
        {
            writer.WriteStartObject();
            writer.WriteNumber("id", horde.Id);
            writer.WriteNumber("owner", horde.Owner);
            writer.WriteNumber("template", horde.TemplateIndex);
            writer.WritePropertyName("members");
            writer.WriteStartArray();
            foreach (var member in horde.Members)
            {
                writer.WriteNumberValue(member);
            }
            writer.WriteEndArray();
            writer.WriteNumber("formation", horde.Formation);
            writer.WriteEndObject();
        }
        writer.WriteEndArray();
    }

    private static void WritePlayers(Utf8JsonWriter writer, IReadOnlyList<SnapshotPlayer> players)
    {
        writer.WritePropertyName("players");
        writer.WriteStartArray();
        foreach (var player in players)
        {
            writer.WriteStartObject();
            writer.WriteNumber("index", player.Index);
            writer.WriteNumber("resources", player.Resources);
            writer.WriteNumber("command_points", player.CommandPoints);
            writer.WriteNumber("command_points_max", player.CommandPointsMax);
            writer.WriteNumber("power_points", player.PowerPoints);
            writer.WriteEndObject();
        }
        writer.WriteEndArray();
    }

    private static void WriteEvents(Utf8JsonWriter writer, IReadOnlyList<SimEvent> events)
    {
        writer.WritePropertyName("events");
        writer.WriteStartArray();
        foreach (var simEvent in events)
        {
            writer.WriteStartObject();
            writer.WriteString("kind", simEvent.Kind);
            writer.WriteNumber("object", simEvent.Object);
            if (simEvent.Target.HasValue)
            {
                writer.WriteNumber("target", simEvent.Target.Value);
            }
            if (simEvent.Amount.HasValue)
            {
                writer.WritePropertyName("amount");
                WriteFixed(writer, simEvent.Amount.Value);
            }
            if (simEvent.Name != null)
            {
                writer.WriteString("name", simEvent.Name);
            }
            writer.WriteEndObject();
        }
        writer.WriteEndArray();
    }

    private static void WriteFixed(Utf8JsonWriter writer, Fixed64 value) =>
        writer.WriteNumberValue((decimal)value.Raw / Fixed64.OneRaw);
}
