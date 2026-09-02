namespace OpenBfme.Sim;

/// <summary>
/// ObjectCreationList entries referenced by loaded retail module rows. The
/// bundle currently carries the reference name but not the OCL declaration.
/// Keep this deterministic translation sourced from retail objectcreationlist.ini.
/// </summary>
internal static class ObjectCreationListCatalog
{
    private static readonly string[] MinisWallTrebuchetUpgrade = { "GondorTrebuchetWall" };

    public static string[] Resolve(string name) => name switch
    {
        "OCL_MinisWallBTTrebuchetUpgrade" => MinisWallTrebuchetUpgrade,
        "" => Array.Empty<string>(),
        _ when !name.StartsWith("OCL_", StringComparison.Ordinal) => new[] { name },
        _ => Array.Empty<string>(),
    };
}
