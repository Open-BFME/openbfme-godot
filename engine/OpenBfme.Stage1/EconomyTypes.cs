using System.Text.Json;

namespace OpenBfme.Stage1;

public enum EconomyCommandKind
{
    Place = 0,
    Train = 1,
    Rally = 2,
}

public enum BuildingRole
{
    Fortress = 0,
    Resource = 1,
    Production = 2,
}

public enum SpawnDirection
{
    North = 0,
    East = 1,
    South = 2,
    West = 3,
}

public readonly record struct EconomyCommand(
    int ExecuteTick,
    int Sequence,
    EconomyCommandKind Kind,
    TeamId Team,
    int BuildingId,
    int TypeCode,
    WorldPos Position);

public sealed record EconomyRules
{
    public required int MaximumTrainQueue { get; init; }
    public required int ConstructionHealthRamp { get; init; }
    public required int BuildingBlocksNavigationAt { get; init; }
    public required int QueuedBattalionsCountTowardPopulation { get; init; }
    public required int SpawnSearchMaximumRadiusCells { get; init; }
    public required IReadOnlyList<SpawnDirection> SpawnSearchOrder { get; init; }
}

public sealed record FarmEfficiencyRules
{
    public required int RadiusSubcells { get; init; }
    public required int BasePermille { get; init; }
    public required int PenaltyPerNeighborPermille { get; init; }
    public required int MinimumPermille { get; init; }
}

public sealed record SideEconomyDefinition
{
    public required TeamId Team { get; init; }
    public required int StartingResources { get; init; }
    public required int PopulationCap { get; init; }
}

public sealed record BuildingDefinition
{
    public required int TypeCode { get; init; }
    public required string ObjectId { get; init; }
    public required BuildingRole Role { get; init; }
    public required int Cost { get; init; }
    public required int ConstructionTicks { get; init; }
    public required int MaximumHealth { get; init; }
    public required int FootprintWidthCells { get; init; }
    public required int FootprintHeightCells { get; init; }
    public required int BuildMenuSlot { get; init; }
    public required int IncomeAmount { get; init; }
    public required int IncomeIntervalTicks { get; init; }
    public required IReadOnlyList<int> Trains { get; init; }
}

public sealed record HordeBlueprint
{
    public required int TypeCode { get; init; }
    public required string Id { get; init; }
    public required string DisplayName { get; init; }
    public required int MemberCount { get; init; }
    public required int RangedCount { get; init; }
    public required int Cost { get; init; }
    public required int ProductionTicks { get; init; }
    public required int Population { get; init; }
    public required int TrainMenuSlot { get; init; }
}

public sealed record EconomyDefinition
{
    public required int RulesVersion { get; init; }
    public required EconomyRules Rules { get; init; }
    public required FarmEfficiencyRules FarmEfficiency { get; init; }
    public required IReadOnlyList<SideEconomyDefinition> Sides { get; init; }
    public required IReadOnlyList<BuildingDefinition> Buildings { get; init; }
    public required IReadOnlyList<HordeBlueprint> HordeBlueprints { get; init; }

    public static EconomyDefinition Load(string path)
    {
        using JsonDocument document = JsonDocument.Parse(File.ReadAllBytes(path));
        return Parse(document.RootElement);
    }

    public static EconomyDefinition Parse(JsonElement root)
    {
        Require(root.GetProperty("schema").GetString() == "openbfme.economy", "economy_schema");
        Require(root.GetProperty("schemaVersion").GetInt32() == 0, "economy_schema_version");

        JsonElement rules = root.GetProperty("rules");
        JsonElement efficiency = root.GetProperty("farmEfficiency");
        EconomyDefinition result = new()
        {
            RulesVersion = root.GetProperty("rulesVersion").GetInt32(),
            Rules = new EconomyRules
            {
                MaximumTrainQueue = Positive(rules, "maximumTrainQueue"),
                ConstructionHealthRamp = RuleCode(rules.GetProperty("constructionHealthRamp"),
                    ("linear-floor-minimum-one", 1), ("linear-from-one", 1), ("linear", 1), ("disabled", 0)),
                BuildingBlocksNavigationAt = RuleCode(rules.GetProperty("buildingBlocksNavigationAt"),
                    ("placement", 0), ("construction-start", 0), ("completion", 1)),
                QueuedBattalionsCountTowardPopulation = BooleanCode(rules.GetProperty("queuedBattalionsCountTowardPopulation")),
                SpawnSearchMaximumRadiusCells = Positive(rules, "spawnSearchMaximumRadiusCells"),
                SpawnSearchOrder = rules.GetProperty("spawnSearchOrder").EnumerateArray()
                    .Select(ParseDirection)
                    .ToArray(),
            },
            FarmEfficiency = new FarmEfficiencyRules
            {
                RadiusSubcells = Positive(efficiency, "radiusSubcells"),
                BasePermille = Positive(efficiency, "basePermille"),
                PenaltyPerNeighborPermille = NonNegative(efficiency, "penaltyPerNeighborPermille"),
                MinimumPermille = Positive(efficiency, "minimumPermille"),
            },
            Sides = root.GetProperty("sides").EnumerateArray().Select(ParseSide).OrderBy(static side => side.Team).ToArray(),
            Buildings = root.GetProperty("buildings").EnumerateArray().Select(ParseBuilding).OrderBy(static building => building.TypeCode).ToArray(),
            HordeBlueprints = root.GetProperty("hordeBlueprints").EnumerateArray().Select(ParseBlueprint).OrderBy(static blueprint => blueprint.TypeCode).ToArray(),
        };

        Validate(result);
        return result;
    }

    private static SideEconomyDefinition ParseSide(JsonElement item) => new()
    {
        Team = (TeamId)item.GetProperty("team").GetInt32(),
        StartingResources = NonNegative(item, "startingResources"),
        PopulationCap = Positive(item, "populationCap"),
    };

    private static BuildingDefinition ParseBuilding(JsonElement item)
    {
        JsonElement footprint = item.GetProperty("footprint");
        int incomeAmount = 0;
        int incomeInterval = 0;
        if (item.TryGetProperty("income", out JsonElement income) && income.ValueKind != JsonValueKind.Null)
        {
            incomeAmount = NonNegative(income, "amount");
            incomeInterval = NonNegative(income, "intervalTicks");
        }

        return new BuildingDefinition
        {
            TypeCode = Positive(item, "typeCode"),
            ObjectId = item.GetProperty("objectId").GetString() ?? string.Empty,
            Role = ParseRole(item.GetProperty("role")),
            Cost = NonNegative(item, "cost"),
            ConstructionTicks = NonNegative(item, "constructionTicks"),
            MaximumHealth = Positive(item, "maximumHealth"),
            FootprintWidthCells = Positive(footprint, "widthCells"),
            FootprintHeightCells = Positive(footprint, "heightCells"),
            BuildMenuSlot = item.GetProperty("buildMenuSlot").GetInt32(),
            IncomeAmount = incomeAmount,
            IncomeIntervalTicks = incomeInterval,
            Trains = item.GetProperty("trains").EnumerateArray().Select(static value => value.GetInt32()).Order().ToArray(),
        };
    }

    private static HordeBlueprint ParseBlueprint(JsonElement item) => new()
    {
        TypeCode = Positive(item, "typeCode"),
        Id = item.GetProperty("id").GetString() ?? string.Empty,
        DisplayName = item.GetProperty("displayName").GetString() ?? string.Empty,
        MemberCount = Positive(item, "memberCount"),
        RangedCount = NonNegative(item, "rangedCount"),
        Cost = NonNegative(item, "cost"),
        ProductionTicks = Positive(item, "productionTicks"),
        Population = Positive(item, "population"),
        TrainMenuSlot = NonNegative(item, "trainMenuSlot"),
    };

    internal static void Validate(EconomyDefinition definition)
    {
        Require(definition.RulesVersion >= 1, "rules_version");
        Require(definition.Rules.ConstructionHealthRamp == 1, "construction_health_ramp");
        Require(definition.Rules.BuildingBlocksNavigationAt == 0, "building_block_timing");
        Require(definition.Rules.QueuedBattalionsCountTowardPopulation == 1, "queued_population");
        Require(definition.Rules.SpawnSearchOrder.Count == 4 && definition.Rules.SpawnSearchOrder.Distinct().Count() == 4, "spawn_search_order");
        Require(definition.FarmEfficiency.MinimumPermille <= definition.FarmEfficiency.BasePermille, "farm_efficiency_range");
        Require(definition.Sides.Count == 2 && definition.Sides.All(static side => side.Team is TeamId.Blue or TeamId.Red), "economy_sides");
        Require(definition.Sides.Select(static side => side.Team).Distinct().Count() == definition.Sides.Count, "economy_side_duplicate");
        Require(definition.Buildings.Count > 0 && definition.Buildings.Select(static item => item.TypeCode).Distinct().Count() == definition.Buildings.Count, "building_type_duplicate");
        Require(definition.HordeBlueprints.Count > 0 && definition.HordeBlueprints.Select(static item => item.TypeCode).Distinct().Count() == definition.HordeBlueprints.Count, "blueprint_type_duplicate");

        HashSet<int> blueprints = definition.HordeBlueprints.Select(static item => item.TypeCode).ToHashSet();
        foreach (BuildingDefinition building in definition.Buildings)
        {
            Require((building.FootprintWidthCells & 1) == 1 && (building.FootprintHeightCells & 1) == 1, "building_footprint_odd");
            Require(building.Role == BuildingRole.Fortress || building.ConstructionTicks > 0, "buildable_construction_ticks");
            Require(building.Trains.All(blueprints.Contains), "building_train_reference");
            Require(building.Role == BuildingRole.Resource
                ? building.IncomeAmount > 0 && building.IncomeIntervalTicks > 0
                : building.IncomeAmount == 0 && building.IncomeIntervalTicks == 0, "building_income_role");
            Require(building.Role == BuildingRole.Production ? building.Trains.Count > 0 : building.Trains.Count == 0, "building_train_role");
        }

        foreach (HordeBlueprint blueprint in definition.HordeBlueprints)
        {
            Require(blueprint.RangedCount <= blueprint.MemberCount, "blueprint_ranged_count");
        }
    }

    private static BuildingRole ParseRole(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Number)
        {
            return (BuildingRole)element.GetInt32();
        }

        return element.GetString() switch
        {
            "fortress" => BuildingRole.Fortress,
            "resource" => BuildingRole.Resource,
            "production" => BuildingRole.Production,
            _ => throw new InvalidDataException("bundle_contract=building_role"),
        };
    }

    private static SpawnDirection ParseDirection(JsonElement element) => element.GetString() switch
    {
        "north" => SpawnDirection.North,
        "east" => SpawnDirection.East,
        "south" => SpawnDirection.South,
        "west" => SpawnDirection.West,
        _ => throw new InvalidDataException("bundle_contract=spawn_direction"),
    };

    private static int RuleCode(JsonElement element, params (string Name, int Code)[] names)
    {
        if (element.ValueKind == JsonValueKind.Number)
        {
            return element.GetInt32();
        }

        if (element.ValueKind is JsonValueKind.True or JsonValueKind.False)
        {
            return element.GetBoolean() ? 1 : 0;
        }

        string? value = element.GetString();
        foreach ((string name, int code) in names)
        {
            if (value == name)
            {
                return code;
            }
        }

        throw new InvalidDataException("bundle_contract=economy_rule_code");
    }

    private static int BooleanCode(JsonElement element) => element.ValueKind switch
    {
        JsonValueKind.True => 1,
        JsonValueKind.False => 0,
        JsonValueKind.Number => element.GetInt32(),
        _ => throw new InvalidDataException("bundle_contract=economy_rule_boolean"),
    };

    private static int Positive(JsonElement parent, string property)
    {
        int value = parent.GetProperty(property).GetInt32();
        Require(value > 0, property);
        return value;
    }

    private static int NonNegative(JsonElement parent, string property)
    {
        int value = parent.GetProperty(property).GetInt32();
        Require(value >= 0, property);
        return value;
    }

    private static void Require(bool condition, string code)
    {
        if (!condition)
        {
            throw new InvalidDataException($"bundle_contract={code}");
        }
    }
}

public sealed class TeamEconomy
{
    public required TeamId Team { get; init; }
    public required int Resources { get; set; }
    public required int TotalEarned { get; set; }
    public required int PopulationUsed { get; set; }
    public required int PopulationReserved { get; set; }
    public required int PopulationCap { get; init; }
}

public sealed class ProductionJob
{
    public required int JobId { get; init; }
    public required int BlueprintTypeCode { get; init; }
    public required int RemainingTicks { get; set; }
    public required int EnqueuedTick { get; init; }
    public required int ReservedPopulation { get; init; }
}

public sealed class EconomyBuilding
{
    public required int EntityId { get; init; }
    public required TeamId Team { get; init; }
    public required int TypeCode { get; init; }
    public required WorldPos Position { get; init; }
    public required int Health { get; set; }
    public required int ConstructionHealthCap { get; set; }
    public required int ConstructionProgressTicks { get; set; }
    public required bool IsCompleted { get; set; }
    public required bool HasRallyPoint { get; set; }
    public required WorldPos RallyPoint { get; set; }
    public required int NextIncomeTick { get; set; }
    public List<ProductionJob> Jobs { get; } = [];
    public bool IsDestroyed => Health <= 0;
}
