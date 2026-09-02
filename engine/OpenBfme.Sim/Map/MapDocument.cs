using System.Collections.ObjectModel;
using System.Globalization;
using System.Numerics;
using System.Text.Json;

namespace OpenBfme.Sim.Map;

public sealed record MapSource(string Path, string Sha256);
public sealed record MapWorldSize(int Width, int Height, int CellSize);
public sealed record MapHeightGrid(int Width, int Height, IReadOnlyList<ushort> Samples);
public sealed record MapPassabilityGrid(int Width, int Height, int RowStrideBytes, IReadOnlyList<bool> Impassable);
public sealed record MapPoint(Fixed64 X, Fixed64 Y);
public sealed record MapStartPosition(Fixed64 X, Fixed64 Y, Fixed64 Facing);
public sealed record MapWaterLayer(bool Impassable, IReadOnlyList<IReadOnlyList<MapPoint>> Polygons);
public sealed record MapPropertyValue(JsonValueKind Kind, string CanonicalJson);
public sealed record MapObjectPlacement(
    string Template,
    Fixed64 X,
    Fixed64 Y,
    Fixed64 Z,
    Fixed64 Angle,
    string Owner,
    string OriginalOwner,
    IReadOnlyDictionary<string, MapPropertyValue> Properties);
public sealed record MapPlot(int BaseIndex, int Index, Fixed64 X, Fixed64 Y, string Kind);

/// <summary>Strict immutable representation of contracts/map-v1.schema.json.</summary>
public sealed record MapDocument(
    string Schema,
    MapSource Source,
    MapWorldSize World,
    MapHeightGrid HeightGrid,
    MapPassabilityGrid PassabilityGrid,
    MapWaterLayer? Water,
    IReadOnlyDictionary<int, MapStartPosition> StartPositions,
    IReadOnlyDictionary<string, MapPoint> Waypoints,
    IReadOnlyList<MapObjectPlacement> Objects,
    IReadOnlyList<MapPlot> Plots)
{
    public const string SchemaName = "openbfme.map.v1";

    public static MapDocument Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        return Parse(File.ReadAllText(path));
    }

    public static MapDocument Parse(string json)
    {
        ArgumentNullException.ThrowIfNull(json);
        try
        {
            using var document = JsonDocument.Parse(json);
            return ParseRoot(document.RootElement);
        }
        catch (JsonException exception)
        {
            throw new MapDocumentException("map JSON is invalid", exception);
        }
        catch (FormatException exception)
        {
            throw new MapDocumentException("map binary data is invalid", exception);
        }
    }

    private static MapDocument ParseRoot(JsonElement root)
    {
        RequireObjectWithOptional(root, "$", new[] { "water" }, "schema", "source", "world", "height_grid",
            "passability_grid", "start_positions", "waypoints", "objects", "plots");
        var schema = String(root, "schema", "schema", nonEmpty: true);
        if (schema != SchemaName)
        {
            throw new MapDocumentException($"field 'schema' must be '{SchemaName}'");
        }
        var sourceElement = Object(root, "source", "source", "path", "sha256");
        var source = new MapSource(
            String(sourceElement, "path", "source.path", nonEmpty: true),
            Sha256(sourceElement, "sha256", "source.sha256"));
        var worldElement = Object(root, "world", "world", "width", "height", "cell_size");
        var world = new MapWorldSize(
            Int(worldElement, "width", "world.width", 1),
            Int(worldElement, "height", "world.height", 1),
            Int(worldElement, "cell_size", "world.cell_size", 1));
        var height = ParseHeight(Object(root, "height_grid", "height_grid",
            "width", "height", "encoding", "data_base64"));
        var passability = ParsePassability(Object(root, "passability_grid", "passability_grid",
            "width", "height", "encoding", "row_stride_bytes", "data_base64"));
        if (height.Width != passability.Width || height.Height != passability.Height)
        {
            throw new MapDocumentException("height and passability grid shapes differ");
        }
        if (world.Width != checked(height.Width * world.CellSize)
            || world.Height != checked(height.Height * world.CellSize))
        {
            throw new MapDocumentException("world dimensions do not equal grid dimensions times cell_size");
        }
        var water = root.TryGetProperty("water", out var waterElement)
            ? ParseWater(waterElement)
            : null;
        return new MapDocument(
            schema, source, world, height, passability, water,
            ParseStarts(Property(root, "start_positions", "start_positions")),
            ParseWaypoints(Property(root, "waypoints", "waypoints")),
            ParseObjects(Property(root, "objects", "objects")),
            ParsePlots(Property(root, "plots", "plots")));
    }

    private static MapHeightGrid ParseHeight(JsonElement value)
    {
        var width = Int(value, "width", "height_grid.width", 1);
        var height = Int(value, "height", "height_grid.height", 1);
        if (String(value, "encoding", "height_grid.encoding") != "uint16-little-endian-row-major")
            throw new MapDocumentException("height_grid.encoding is unsupported");
        var bytes = DecodeBase64(value, "data_base64", "height_grid.data_base64");
        if (bytes.Length != checked(width * height * 2))
            throw new MapDocumentException("height_grid data length does not match its shape");
        var samples = new ushort[width * height];
        for (var index = 0; index < samples.Length; index++)
            samples[index] = (ushort)(bytes[index * 2] | bytes[index * 2 + 1] << 8);
        return new MapHeightGrid(width, height, Array.AsReadOnly(samples));
    }

    private static MapPassabilityGrid ParsePassability(JsonElement value)
    {
        var width = Int(value, "width", "passability_grid.width", 1);
        var height = Int(value, "height", "passability_grid.height", 1);
        if (String(value, "encoding", "passability_grid.encoding") != "one-is-impassable-lsb-first-row-padded")
            throw new MapDocumentException("passability_grid.encoding is unsupported");
        var stride = Int(value, "row_stride_bytes", "passability_grid.row_stride_bytes", (width + 7) / 8);
        var bytes = DecodeBase64(value, "data_base64", "passability_grid.data_base64");
        if (bytes.Length != checked(stride * height))
            throw new MapDocumentException("passability_grid data length does not match its shape");
        var cells = new bool[width * height];
        for (var y = 0; y < height; y++)
            for (var x = 0; x < width; x++)
                cells[y * width + x] = (bytes[y * stride + x / 8] & (1 << (x & 7))) != 0;
        return new MapPassabilityGrid(width, height, stride, Array.AsReadOnly(cells));
    }

    private static MapWaterLayer ParseWater(JsonElement value)
    {
        RequireObject(value, "water", "impassable", "polygons");
        var impassable = Boolean(value, "impassable", "water.impassable");
        var polygonsElement = Property(value, "polygons", "water.polygons");
        RequireKind(polygonsElement, JsonValueKind.Array, "water.polygons");
        var polygons = new List<IReadOnlyList<MapPoint>>();
        var polygonIndex = 0;
        foreach (var polygonElement in polygonsElement.EnumerateArray())
        {
            RequireKind(polygonElement, JsonValueKind.Array, $"water.polygons[{polygonIndex}]");
            var points = new List<MapPoint>();
            var pointIndex = 0;
            foreach (var pointElement in polygonElement.EnumerateArray())
            {
                points.Add(ParsePoint(pointElement, $"water.polygons[{polygonIndex}][{pointIndex++}]"));
            }
            if (points.Count < 3) throw new MapDocumentException($"water.polygons[{polygonIndex}] needs at least three points");
            polygons.Add(Array.AsReadOnly(points.ToArray()));
            polygonIndex++;
        }
        return new MapWaterLayer(impassable, Array.AsReadOnly(polygons.ToArray()));
    }

    private static IReadOnlyDictionary<int, MapStartPosition> ParseStarts(JsonElement value)
    {
        RequireKind(value, JsonValueKind.Object, "start_positions");
        var result = new SortedDictionary<int, MapStartPosition>();
        foreach (var property in value.EnumerateObject())
        {
            if (!int.TryParse(property.Name, NumberStyles.None, CultureInfo.InvariantCulture, out var index) || index < 0)
                throw new MapDocumentException($"start_positions key '{property.Name}' is not a non-negative integer");
            RequireObject(property.Value, $"start_positions.{property.Name}", "x", "y", "facing");
            result.Add(index, new MapStartPosition(
                Fixed(property.Value, "x", $"start_positions.{property.Name}.x"),
                Fixed(property.Value, "y", $"start_positions.{property.Name}.y"),
                Fixed(property.Value, "facing", $"start_positions.{property.Name}.facing")));
        }
        return new ReadOnlyDictionary<int, MapStartPosition>(result);
    }

    private static IReadOnlyDictionary<string, MapPoint> ParseWaypoints(JsonElement value)
    {
        RequireKind(value, JsonValueKind.Object, "waypoints");
        var result = new SortedDictionary<string, MapPoint>(StringComparer.Ordinal);
        foreach (var property in value.EnumerateObject())
        {
            if (property.Name.Length == 0) throw new MapDocumentException("waypoint names must not be empty");
            result.Add(property.Name, ParsePoint(property.Value, $"waypoints.{property.Name}"));
        }
        return new ReadOnlyDictionary<string, MapPoint>(result);
    }

    private static IReadOnlyList<MapObjectPlacement> ParseObjects(JsonElement value)
    {
        RequireKind(value, JsonValueKind.Array, "objects");
        var result = new List<MapObjectPlacement>();
        var index = 0;
        foreach (var row in value.EnumerateArray())
        {
            var path = $"objects[{index++}]";
            RequireObject(row, path, "template", "x", "y", "z", "angle", "owner", "original_owner", "properties");
            var propertiesElement = Property(row, "properties", path + ".properties");
            RequireKind(propertiesElement, JsonValueKind.Object, path + ".properties");
            var properties = new SortedDictionary<string, MapPropertyValue>(StringComparer.Ordinal);
            foreach (var property in propertiesElement.EnumerateObject())
                properties.Add(property.Name, new MapPropertyValue(property.Value.ValueKind, property.Value.GetRawText()));
            result.Add(new MapObjectPlacement(
                String(row, "template", path + ".template", nonEmpty: true),
                Fixed(row, "x", path + ".x"), Fixed(row, "y", path + ".y"),
                Fixed(row, "z", path + ".z"), Fixed(row, "angle", path + ".angle"),
                String(row, "owner", path + ".owner"),
                String(row, "original_owner", path + ".original_owner"),
                new ReadOnlyDictionary<string, MapPropertyValue>(properties)));
        }
        return Array.AsReadOnly(result.ToArray());
    }

    private static IReadOnlyList<MapPlot> ParsePlots(JsonElement value)
    {
        RequireKind(value, JsonValueKind.Array, "plots");
        var result = new List<MapPlot>();
        var keys = new HashSet<(int, int)>();
        var rowIndex = 0;
        foreach (var row in value.EnumerateArray())
        {
            var path = $"plots[{rowIndex++}]";
            RequireObject(row, path, "base_index", "index", "x", "y", "kind");
            var item = new MapPlot(
                Int(row, "base_index", path + ".base_index", 0),
                Int(row, "index", path + ".index", 0),
                Fixed(row, "x", path + ".x"), Fixed(row, "y", path + ".y"),
                String(row, "kind", path + ".kind", nonEmpty: true));
            if (!keys.Add((item.BaseIndex, item.Index)))
                throw new MapDocumentException($"duplicate plot ({item.BaseIndex}, {item.Index})");
            result.Add(item);
        }
        return Array.AsReadOnly(result.ToArray());
    }

    private static MapPoint ParsePoint(JsonElement value, string path)
    {
        RequireObject(value, path, "x", "y");
        return new MapPoint(Fixed(value, "x", path + ".x"), Fixed(value, "y", path + ".y"));
    }

    private static JsonElement Property(JsonElement owner, string name, string path) =>
        owner.TryGetProperty(name, out var value) ? value : throw new MapDocumentException($"missing required field '{path}'");

    private static JsonElement Object(JsonElement owner, string name, string path, params string[] fields)
    {
        var value = Property(owner, name, path);
        RequireObject(value, path, fields);
        return value;
    }

    private static void RequireObject(JsonElement value, string path, params string[] required)
    {
        RequireKind(value, JsonValueKind.Object, path);
        var allowed = new HashSet<string>(required, StringComparer.Ordinal);
        foreach (var field in value.EnumerateObject())
            if (!allowed.Contains(field.Name)) throw new MapDocumentException($"unknown field '{path}.{field.Name}'");
        foreach (var name in required)
            if (!value.TryGetProperty(name, out _)) throw new MapDocumentException($"missing required field '{path}.{name}'");
    }

    private static void RequireObjectWithOptional(
        JsonElement value,
        string path,
        IReadOnlyCollection<string> optional,
        params string[] required)
    {
        RequireKind(value, JsonValueKind.Object, path);
        var allowed = new HashSet<string>(required, StringComparer.Ordinal);
        allowed.UnionWith(optional);
        foreach (var field in value.EnumerateObject())
            if (!allowed.Contains(field.Name)) throw new MapDocumentException($"unknown field '{path}.{field.Name}'");
        foreach (var name in required)
            if (!value.TryGetProperty(name, out _)) throw new MapDocumentException($"missing required field '{path}.{name}'");
    }

    private static void RequireKind(JsonElement value, JsonValueKind kind, string path)
    {
        if (value.ValueKind != kind) throw new MapDocumentException($"field '{path}' must be {kind}");
    }

    private static string String(JsonElement owner, string name, string path, bool nonEmpty = false)
    {
        var value = Property(owner, name, path);
        RequireKind(value, JsonValueKind.String, path);
        var text = value.GetString()!;
        if (nonEmpty && text.Length == 0) throw new MapDocumentException($"field '{path}' must not be empty");
        return text;
    }

    private static bool Boolean(JsonElement owner, string name, string path)
    {
        var value = Property(owner, name, path);
        if (value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            throw new MapDocumentException($"field '{path}' must be boolean");
        return value.GetBoolean();
    }

    private static int Int(JsonElement owner, string name, string path, int minimum)
    {
        var value = Property(owner, name, path);
        if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt32(out var number) || number < minimum)
            throw new MapDocumentException($"field '{path}' must be an integer >= {minimum}");
        return number;
    }

    private static Fixed64 Fixed(JsonElement owner, string name, string path)
    {
        var value = Property(owner, name, path);
        if (value.ValueKind != JsonValueKind.Number || !TryParseFixed(value.GetRawText(), out var number))
            throw new MapDocumentException($"field '{path}' must be an exact fixed-point number");
        return number;
    }

    private static bool TryParseFixed(string raw, out Fixed64 value)
    {
        value = Fixed64.Zero;
        if (!decimal.TryParse(raw, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed)) return false;
        var bits = decimal.GetBits(parsed);
        var scale = (bits[3] >> 16) & 0xff;
        var negative = (bits[3] & unchecked((int)0x80000000)) != 0;
        var numerator = ((BigInteger)(uint)bits[2] << 64) | ((BigInteger)(uint)bits[1] << 32) | (uint)bits[0];
        if (negative) numerator = -numerator;
        var fixedRaw = (numerator << Fixed64.FractionBits) / BigInteger.Pow(10, scale);
        if (fixedRaw > long.MaxValue || fixedRaw < long.MinValue) return false;
        value = Fixed64.FromRaw((long)fixedRaw);
        return true;
    }

    private static byte[] DecodeBase64(JsonElement owner, string name, string path)
    {
        var text = String(owner, name, path);
        return Convert.FromBase64String(text);
    }

    private static string Sha256(JsonElement owner, string name, string path)
    {
        var value = String(owner, name, path);
        if (value.Length != 64 || value.Any(character => character is not (>= '0' and <= '9') and not (>= 'a' and <= 'f')))
            throw new MapDocumentException($"field '{path}' must be lowercase sha256");
        return value;
    }
}

public sealed class MapDocumentException : Exception
{
    public MapDocumentException(string message) : base(message) { }
    public MapDocumentException(string message, Exception inner) : base(message, inner) { }
}
