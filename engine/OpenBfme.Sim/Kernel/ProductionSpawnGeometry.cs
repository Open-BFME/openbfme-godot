namespace OpenBfme.Sim;

internal static class ProductionSpawnGeometry
{
    private const string ExitModuleType = "QueueProductionExitUpdate";

    public static FixedVector2 ResolveExitOffset(ObjectTemplate template, ModuleSpec productionSpec)
    {
        if (productionSpec.Data.ContainsKey("ExitOffsetXRaw")
            || productionSpec.Data.ContainsKey("ExitOffsetYRaw"))
        {
            return new FixedVector2(
                productionSpec.GetFixed("ExitOffsetXRaw", Fixed64.FromInt(2)),
                productionSpec.GetFixed("ExitOffsetYRaw", Fixed64.Zero));
        }
        if (template.Economy.ProductionExitOffset != FixedVector2.Zero)
        {
            return template.Economy.ProductionExitOffset;
        }
        var authoredExit = template.Modules.FirstOrDefault(value => value.TypeName == ExitModuleType);
        if (authoredExit != null
            && TryParseVector(authoredExit.GetString("UnitCreatePoint", ""), out var offset))
        {
            return offset;
        }
        return new FixedVector2(Fixed64.FromInt(2), Fixed64.Zero);
    }

    private static bool TryParseVector(string value, out FixedVector2 result)
    {
        result = FixedVector2.Zero;
        var hasX = false;
        var hasY = false;
        var x = Fixed64.Zero;
        var y = Fixed64.Zero;
        foreach (var token in value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
        {
            if (token.StartsWith("X:", StringComparison.OrdinalIgnoreCase))
                hasX = FixedJson.TryParse(token[2..], out x);
            else if (token.StartsWith("Y:", StringComparison.OrdinalIgnoreCase))
                hasY = FixedJson.TryParse(token[2..], out y);
        }
        if (!hasX || !hasY) return false;
        result = new FixedVector2(x, y);
        return true;
    }
}
