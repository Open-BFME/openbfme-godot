using System.Collections.ObjectModel;
using System.Numerics;
using OpenBfme.Sim.Pathing;

namespace OpenBfme.Sim.Map;

public sealed record MapLoadReport(
    int MapObjectCount,
    int SpawnedObjectCount,
    int StartingBaseSpawnedCount,
    IReadOnlyDictionary<string, int> UnknownTemplates,
    IReadOnlyDictionary<int, MapStartPosition> PlayerStartPositions,
    IReadOnlyDictionary<int, int> PlotsPerPlayer,
    IReadOnlyList<int> PlayersWithoutBases);

/// <summary>Applies immutable cooked map facts to a newly loaded simulation world.</summary>
public static class MapWorldBuilder
{
    private const string StartBaseMarkerTemplate = "PrimaryAIBaseMarker";

    public static SimWorld Build(MatchLaunch launch, BundleDocument bundle, MapDocument map)
    {
        ArgumentNullException.ThrowIfNull(launch);
        ArgumentNullException.ThrowIfNull(bundle);
        ArgumentNullException.ThrowIfNull(map);
        VerifyLaunchMap(launch, map);
        var grid = BuildPassabilityGrid(map);
        var world = SimWorld.FromBundle(launch, bundle, grid);
        var startToPlayer = StartToPlayer(launch, map);
        var playerStarts = new SortedDictionary<int, MapStartPosition>();
        foreach (var pair in startToPlayer)
            playerStarts.Add(pair.Value.PlayerIndex, map.StartPositions[pair.Key]);

        var unknown = new SortedDictionary<string, int>(StringComparer.Ordinal);
        var baseObjects = new SortedDictionary<int, int>();
        var spawned = 0;
        foreach (var placement in map.Objects)
        {
            if (!world.TryGetTemplate(placement.Template, out _))
            {
                unknown[placement.Template] = unknown.TryGetValue(placement.Template, out var count) ? count + 1 : 1;
                continue;
            }
            var mapStartIndex = OwnerStartIndex(placement.Owner, placement.OriginalOwner);
            var team = mapStartIndex.HasValue && startToPlayer.TryGetValue(mapStartIndex.Value, out var player)
                ? player.Team
                : -1;
            var gameObject = world.SpawnObject(
                placement.Template,
                team,
                new FixedVector2(placement.X, placement.Y),
                placement.Z,
                placement.Angle);
            spawned++;
            if (mapStartIndex.HasValue
                && startToPlayer.TryGetValue(mapStartIndex.Value, out player)
                && IsBaseTemplate(placement.Template)
                && !baseObjects.ContainsKey(player.PlayerIndex))
            {
                baseObjects.Add(player.PlayerIndex, gameObject.Id);
            }
        }

        var autoBases = 0;
        foreach (var (mapStartIndex, player) in startToPlayer)
        {
            if (baseObjects.ContainsKey(player.PlayerIndex)) continue;
            var template = StartBaseMarkerTemplate;
            if (!world.TryGetTemplate(template, out _)) continue;
            var start = map.StartPositions[mapStartIndex];
            var gameObject = world.SpawnObject(
                template,
                player.Team,
                new FixedVector2(start.X, start.Y),
                headingRadians: start.Facing);
            baseObjects.Add(player.PlayerIndex, gameObject.Id);
            autoBases++;
        }

        var plots = new List<BuildPlot>();
        var plotsPerPlayer = new SortedDictionary<int, int>();
        foreach (var mapPlot in map.Plots.OrderBy(value => value.BaseIndex).ThenBy(value => value.Index))
        {
            if (!startToPlayer.TryGetValue(mapPlot.BaseIndex, out var player)
                || !baseObjects.TryGetValue(player.PlayerIndex, out var baseObjectId)) continue;
            plots.Add(new BuildPlot(
                baseObjectId,
                mapPlot.Index,
                new FixedVector2(mapPlot.X, mapPlot.Y),
                new[] { mapPlot.Kind }));
            plotsPerPlayer[player.PlayerIndex] = plotsPerPlayer.TryGetValue(player.PlayerIndex, out var count)
                ? count + 1 : 1;
        }
        world.SetBuildPlots(plots);
        var withoutBases = playerStarts.Keys.Where(index => !baseObjects.ContainsKey(index)).ToArray();
        world.MapLoadReport = new MapLoadReport(
            map.Objects.Count,
            spawned,
            autoBases,
            new ReadOnlyDictionary<string, int>(unknown),
            new ReadOnlyDictionary<int, MapStartPosition>(playerStarts),
            new ReadOnlyDictionary<int, int>(plotsPerPlayer),
            Array.AsReadOnly(withoutBases));
        return world;
    }

    public static PassabilityGrid BuildPassabilityGrid(MapDocument map)
    {
        ArgumentNullException.ThrowIfNull(map);
        var width = map.PassabilityGrid.Width;
        var height = map.PassabilityGrid.Height;
        var passable = map.PassabilityGrid.Impassable.Select(value => !value).ToArray();
        if (map.Water is { Impassable: true })
        {
            for (var y = 0; y < height; y++)
            {
                for (var x = 0; x < width; x++)
                {
                    if (!passable[y * width + x]) continue;
                    var center = CellCenter(x, y, map.World.CellSize);
                    if (map.Water.Polygons.Any(polygon => Contains(polygon, center)))
                        passable[y * width + x] = false;
                }
            }
        }
        return new PassabilityGrid(
            width,
            height,
            passable,
            Enumerable.Repeat(1, passable.Length).ToArray(),
            map.World.CellSize);
    }

    private static void VerifyLaunchMap(MatchLaunch launch, MapDocument map)
    {
        if (!string.Equals(launch.Map.Path.Replace('\\', '/'), map.Source.Path.Replace('\\', '/'), StringComparison.OrdinalIgnoreCase))
            throw new MapDocumentException($"launch map path '{launch.Map.Path}' does not match map source '{map.Source.Path}'");
        if (launch.Map.Sha256 != null && !string.Equals(launch.Map.Sha256, map.Source.Sha256, StringComparison.Ordinal))
            throw new MapDocumentException("launch map sha256 does not match map document");
    }

    private static SortedDictionary<int, PlayerBinding> StartToPlayer(MatchLaunch launch, MapDocument map)
    {
        var result = new SortedDictionary<int, PlayerBinding>();
        for (var playerIndex = 0; playerIndex < launch.Players.Count; playerIndex++)
        {
            var player = launch.Players[playerIndex];
            if (!player.StartPosition.HasValue) continue;
            var startIndex = player.StartPosition.Value;
            if (!map.StartPositions.ContainsKey(startIndex))
                throw new MapDocumentException($"player {playerIndex} selects missing start_position {startIndex}");
            if (!result.TryAdd(startIndex, new PlayerBinding(playerIndex, player.Team, player.Faction)))
                throw new MapDocumentException($"more than one player selects start_position {startIndex}");
        }
        return result;
    }

    private static int? OwnerStartIndex(string owner, string originalOwner)
    {
        foreach (var text in new[] { owner, originalOwner })
        {
            var marker = text.IndexOf("Player_", StringComparison.OrdinalIgnoreCase);
            if (marker < 0) continue;
            marker += "Player_".Length;
            var end = marker;
            while (end < text.Length && char.IsAsciiDigit(text[end])) end++;
            if (end > marker
                && int.TryParse(text.AsSpan(marker, end - marker), out var oneBased)
                && oneBased > 0) return oneBased - 1;
        }
        return null;
    }

    private static bool IsBaseTemplate(string template) =>
        template.Contains("Fortress", StringComparison.OrdinalIgnoreCase)
        || template.Contains("CastleBase", StringComparison.OrdinalIgnoreCase)
        || template.EndsWith("Base", StringComparison.OrdinalIgnoreCase);

    private static MapPoint CellCenter(int x, int y, int cellSize) => new(
        Fixed64.FromFraction(checked((long)x * cellSize * 2 + cellSize), 2),
        Fixed64.FromFraction(checked((long)y * cellSize * 2 + cellSize), 2));

    private static bool Contains(IReadOnlyList<MapPoint> polygon, MapPoint point)
    {
        var inside = false;
        for (int current = 0, previous = polygon.Count - 1; current < polygon.Count; previous = current++)
        {
            var a = polygon[current];
            var b = polygon[previous];
            var crosses = (a.Y > point.Y) != (b.Y > point.Y);
            if (!crosses) continue;
            var left = (BigInteger)(point.X.Raw - a.X.Raw) * (b.Y.Raw - a.Y.Raw);
            var right = (BigInteger)(b.X.Raw - a.X.Raw) * (point.Y.Raw - a.Y.Raw);
            if ((b.Y > a.Y && left < right) || (b.Y < a.Y && left > right)) inside = !inside;
        }
        return inside;
    }

    private sealed record PlayerBinding(int PlayerIndex, int Team, string Faction);
}
