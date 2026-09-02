using System.Buffers.Binary;
using System.IO.Compression;
using System.Text;
using System.Text.Json;

namespace OpenBfme.Sim;

/// <summary>
/// Compact presentation transport for snapshot-v1 object columns. The regular
/// SnapshotWriter remains the tooling and schema contract; this writer carries
/// the same values with six little-endian int32 columns followed by seven
/// little-endian float32 columns.
/// </summary>
public static class PackedSnapshotWriter
{
    public const string ObjectFormat = "openbfme.snapshot.objects.packed.v1";
    public const int ColumnCount = 13;
    public const int ColumnWidthBytes = 4;

    public static byte[] Write(SimWorld world)
    {
        ArgumentNullException.ThrowIfNull(world);
        world.SynchronizeObjectStore();
        var store = world.ObjectStore;
        var slots = store.LiveSlots().ToArray();
        var packed = PackObjects(store, slots);
		return WriteEnvelope(world, slots.Length, packed, full: true, Array.Empty<int>(), -1);
	}

	internal static byte[] WriteEnvelope(
		SimWorld world,
		int objectCount,
		byte[] packed,
		bool full,
		IReadOnlyList<int> changedSlots,
		int baseTick)
	{
		var compressed = Compress(packed);

        using var stream = new MemoryStream();
        using var writer = new Utf8JsonWriter(stream);
        writer.WriteStartObject();
        writer.WriteString("schema", SnapshotWriter.SchemaName);
        writer.WriteNumber("tick", world.TickIndex);
        writer.WriteNumber("tick_ms", world.TickMilliseconds);
        writer.WriteString("hash", world.StateHash());
        writer.WriteNumber("object_count", objectCount);
        writer.WritePropertyName("objects");
        writer.WriteStartObject();
        writer.WriteString("format", ObjectFormat);
        writer.WriteString("encoding", "base64+brotli");
        writer.WriteNumber("column_width_bytes", ColumnWidthBytes);
		writer.WriteNumber("uncompressed_bytes", packed.Length);
		writer.WriteBoolean("full", full);
		if (!full)
		{
			writer.WriteNumber("base_tick", baseTick);
			writer.WritePropertyName("slots");
			writer.WriteStartArray();
			foreach (var slot in changedSlots) writer.WriteNumberValue(slot);
			writer.WriteEndArray();
		}
        writer.WriteString("data", Convert.ToBase64String(compressed));
        writer.WriteEndObject();
        WriteHordes(writer, world.Hordes);
        WritePlayers(writer, world.SnapshotPlayers());
        WriteEvents(writer, world.EventsThisTick);
        writer.WriteEndObject();
        writer.Flush();
        return stream.ToArray();
    }

	private static byte[] Compress(byte[] source)
	{
		using var output = new MemoryStream();
		using (var brotli = new BrotliStream(output, CompressionLevel.Optimal, leaveOpen: true))
		{
			brotli.Write(source);
		}
		return output.ToArray();
	}

    public static string WriteJson(SimWorld world) => Encoding.UTF8.GetString(Write(world));

    internal static byte[] PackObjects(ObjectStore store, IReadOnlyList<int> slots)
    {
        var result = new byte[checked(slots.Count * ColumnCount * ColumnWidthBytes)];
        WriteIntColumn(result, 0, slots, slot => store.Id[slot]);
        WriteIntColumn(result, 1, slots, slot => store.TemplateIndex[slot]);
        WriteIntColumn(result, 2, slots, slot => store.Owner[slot]);
        WriteIntColumn(result, 3, slots, slot => store.State[slot]);
        WriteIntColumn(result, 4, slots, slot => store.Anim[slot]);
        WriteIntColumn(result, 5, slots, slot => store.Flags[slot]);
        WriteFloatColumn(result, 6, slots, slot => ToFloat(store.X[slot]));
        WriteFloatColumn(result, 7, slots, slot => ToFloat(store.Y[slot]));
        WriteFloatColumn(result, 8, slots, slot => ToFloat(store.Z[slot]));
        WriteFloatColumn(result, 9, slots, slot => ToFloat(store.Yaw[slot]));
        WriteFloatColumn(result, 10, slots, slot => ToFloat(store.Health[slot]));
        WriteFloatColumn(result, 11, slots, slot => ToFloat(store.MaxHealth[slot]));
        WriteFloatColumn(result, 12, slots, slot => ToFloat(store.AnimFrame[slot]));
        return result;
    }

    private static void WriteIntColumn(
        Span<byte> destination,
        int column,
        IReadOnlyList<int> slots,
        Func<int, int> value)
    {
        var offset = column * slots.Count * ColumnWidthBytes;
        foreach (var slot in slots)
        {
            BinaryPrimitives.WriteInt32LittleEndian(destination[offset..], value(slot));
            offset += ColumnWidthBytes;
        }
    }

    private static void WriteFloatColumn(
        Span<byte> destination,
        int column,
        IReadOnlyList<int> slots,
        Func<int, float> value)
    {
        var offset = column * slots.Count * ColumnWidthBytes;
        foreach (var slot in slots)
        {
            BinaryPrimitives.WriteInt32LittleEndian(
                destination[offset..], BitConverter.SingleToInt32Bits(value(slot)));
            offset += ColumnWidthBytes;
        }
    }

    private static float ToFloat(Fixed64 value) => (float)((decimal)value.Raw / Fixed64.OneRaw);

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
            foreach (var member in horde.Members) writer.WriteNumberValue(member);
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
            if (simEvent.Target.HasValue) writer.WriteNumber("target", simEvent.Target.Value);
            if (simEvent.Amount.HasValue)
                writer.WriteNumber("amount", (decimal)simEvent.Amount.Value.Raw / Fixed64.OneRaw);
            if (simEvent.Name != null) writer.WriteString("name", simEvent.Name);
            writer.WriteEndObject();
        }
        writer.WriteEndArray();
    }
}

/// <summary>Stateful packed stream which emits full frames only when slot identity changes.</summary>
public sealed class PackedSnapshotStreamWriter
{
	private byte[]? _previous;
	private int[]? _previousIds;
	private int _previousTick = -1;

	public void Reset()
	{
		_previous = null;
		_previousIds = null;
		_previousTick = -1;
	}

	public string WriteJson(SimWorld world)
	{
		ArgumentNullException.ThrowIfNull(world);
		world.SynchronizeObjectStore();
		var store = world.ObjectStore;
		var liveSlots = store.LiveSlots().ToArray();
		var ids = liveSlots.Select(slot => store.Id[slot]).ToArray();
		var current = PackedSnapshotWriter.PackObjects(store, liveSlots);
		var full = _previous == null || _previousIds == null
			|| !_previousIds.AsSpan().SequenceEqual(ids);
		var changedSlots = full ? Array.Empty<int>() : ChangedSlots(_previous!, current, ids.Length);
		if (!full && changedSlots.Length * 2 >= ids.Length)
		{
			full = true;
			changedSlots = Array.Empty<int>();
		}
		var payload = full ? current : SelectColumns(current, ids.Length, changedSlots);
		var baseTick = _previousTick;
		_previous = current;
		_previousIds = ids;
		_previousTick = world.TickIndex;
		return Encoding.UTF8.GetString(PackedSnapshotWriter.WriteEnvelope(
			world, ids.Length, payload, full, changedSlots, baseTick));
	}

	private static int[] ChangedSlots(byte[] previous, byte[] current, int count)
	{
		var changed = new List<int>();
		for (var slot = 0; slot < count; slot++)
		{
			for (var column = 0; column < PackedSnapshotWriter.ColumnCount; column++)
			{
				var offset = (column * count + slot) * PackedSnapshotWriter.ColumnWidthBytes;
				if (previous.AsSpan(offset, 4).SequenceEqual(current.AsSpan(offset, 4))) continue;
				changed.Add(slot);
				break;
			}
		}
		return changed.ToArray();
	}

	private static byte[] SelectColumns(byte[] source, int count, IReadOnlyList<int> slots)
	{
		var result = new byte[checked(
			slots.Count * PackedSnapshotWriter.ColumnCount * PackedSnapshotWriter.ColumnWidthBytes)];
		for (var column = 0; column < PackedSnapshotWriter.ColumnCount; column++)
		{
			for (var index = 0; index < slots.Count; index++)
			{
				var sourceOffset = (column * count + slots[index]) * 4;
				var targetOffset = (column * slots.Count + index) * 4;
				source.AsSpan(sourceOffset, 4).CopyTo(result.AsSpan(targetOffset, 4));
			}
		}
		return result;
	}
}
