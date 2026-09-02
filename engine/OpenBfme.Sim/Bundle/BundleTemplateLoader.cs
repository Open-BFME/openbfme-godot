using System.Reflection;

namespace OpenBfme.Sim;

public sealed record BundleTemplateFailure(int Index, string Template, string Reason);
public sealed record BundleReferenceIssue(string Template, string Name);
public sealed record BundleModuleGap(string Template, string Type, string Carrier);

/// <summary>
/// Deterministic, JSON-serializable accounting for one bundle load. Registered
/// module implementations use their declared tier. Unknown Body carriers are
/// structural; unknown modules on every other carrier are cosmetic load gaps.
/// </summary>
public sealed record BundleLoadReport(
    string ModuleTierPolicy,
    int TemplatesLoaded,
    IReadOnlyList<BundleTemplateFailure> TemplatesFailed,
    IReadOnlyDictionary<string, IReadOnlyList<BundleReferenceIssue>> UnresolvedReferences,
    IReadOnlyList<BundleModuleGap> Gaps,
    IReadOnlyDictionary<string, int> GapRowsByType,
    IReadOnlyList<string> AbsentTables,
    IReadOnlyList<string> Diagnostics,
    IReadOnlyList<string> Notes);

public sealed record BundleTemplateLoadResult(
    IReadOnlyList<ObjectTemplate> Templates,
    IReadOnlyDictionary<string, int> TemplateIndices,
    IReadOnlyList<HordeProductionTemplate> HordeTemplates,
    IReadOnlyList<WeaponTemplate> WeaponTemplates,
    IReadOnlyList<ArmorTemplate> ArmorTemplates,
    TechCatalog Tech,
    BundleLoadReport Report);

/// <summary>Resolves bundle-v1 rows directly into deterministic core templates.</summary>
public static class BundleTemplateLoader
{
    public static BundleTemplateLoadResult Load(
        BundleDocument document,
        ModuleRegistry registry,
        int tickMilliseconds)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(registry);
        if (tickMilliseconds < 1) throw new ArgumentOutOfRangeException(nameof(tickMilliseconds));

        var absent = new List<string>();
        if (document.Armors == null) absent.Add("armors");
        if (document.Hordes == null) absent.Add("hordes");
        if (document.Locomotors == null) absent.Add("locomotors");
        if (document.Weapons == null) absent.Add("weapons");
        if (document.Upgrades == null) absent.Add("upgrades");
        if (document.Sciences == null) absent.Add("sciences");
        if (document.SpecialPowers == null) absent.Add("special_powers");
        if (document.CommandButtons == null) absent.Add("command_buttons");
        if (document.CommandSets == null) absent.Add("command_sets");
        absent.Add("object_creation_lists");

        var diagnostics = document.Diagnostics
            .Select(item => $"{item.Template}: {item.Message}")
            .ToList();
        var notes = new List<string>();
        var gaps = new List<BundleModuleGap>();
        var unresolved = NewUnresolved();
        var tech = BuildTechCatalog(document, diagnostics, unresolved);
        var weapons = ParseWeapons(document.Weapons, tickMilliseconds, diagnostics);
        var armors = ParseArmors(document.Armors, diagnostics);
        var weaponNames = ReferenceNames(document.Weapons?.Select(row => row.Name));
        var armorNames = ReferenceNames(document.Armors?.Select(row => row.Name));
        _ = IndexRows(document.DamageFx, "damage_fx", diagnostics);
        var locomotors = IndexRows(document.Locomotors, "locomotor", diagnostics);
        var locomotorSets = IndexRows(document.LocomotorSets, "locomotor_set", diagnostics);
        var hordes = IndexHordes(document.Hordes, diagnostics);

        var rawByName = new Dictionary<string, BundleTemplateRow>(StringComparer.OrdinalIgnoreCase);
        var duplicateNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var row in document.Templates)
        {
            if (!rawByName.TryAdd(row.Name, row)) duplicateNames.Add(row.Name);
            foreach (var module in row.Modules)
                if (module.Gap)
                    diagnostics.Add($"{row.Name}: unparseable {module.Carrier} module '{module.Type}' retained as a load gap");
        }

        var resolvedRows = new Dictionary<string, EffectiveTemplate>(StringComparer.OrdinalIgnoreCase);
        var resolving = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var failures = new List<BundleTemplateFailure>();
        var templates = new List<ObjectTemplate>();
        var indices = new SortedDictionary<string, int>(StringComparer.Ordinal);
        var authoredDirectives = document.Templates.Any(row => row.Modules.Any(HasDirective));
        if (!authoredDirectives)
        {
            notes.Add("RemoveModule/ReplaceModule/AddModule directives were not authored in bundle module rows");
        }

        foreach (var row in document.Templates)
        {
            try
            {
                if (duplicateNames.Contains(row.Name))
                    throw new BundleTemplateException($"duplicate template name '{row.Name}'");
                var effective = Resolve(row, rawByName, resolvedRows, resolving);
                var weaponSets = ParseWeaponSets(row.Name, effective.Blocks, document.Weapons != null,
                    weaponNames, unresolved, diagnostics);
                var armorSets = ParseArmorSets(row.Name, effective.Blocks, document.Armors != null,
                    armorNames, unresolved, diagnostics);
                var specs = new List<ModuleSpec>();
                var templateGaps = new List<BundleModuleGap>();
                foreach (var module in effective.Modules)
                {
                    var spec = ToModuleSpec(module, registry);
                    if (spec.TypeName == HordeContainModule.TypeName
                        && hordes.TryGetValue(row.Name, out var horde))
                    {
                        spec = AttachHorde(spec, horde);
                    }
                    specs.Add(spec);
                    if (spec.Gap)
                        templateGaps.Add(new BundleModuleGap(row.Name, spec.TypeName, spec.Carrier));
                }
                AddResolvedLocomotor(row.Name, effective.Blocks, specs, document.Locomotors,
                    locomotors, locomotorSets, unresolved, registry);
                ValidateStructuralModules(specs, registry);
                var body = ResolveBodyHealth(effective, specs);
                var commandSetName = effective.Fields.TryGetValue("CommandSet", out var commandSetValue)
                    ? ValueStrings(commandSetValue).FirstOrDefault() ?? ""
                    : "";
                if (commandSetName.Length > 0 && !tech.CommandSets.ContainsKey(commandSetName))
                    unresolved["command_set"].Add(new BundleReferenceIssue(row.Name, commandSetName));
                templates.Add(new ObjectTemplate(
                    row.Name, specs, weaponSets, armorSets, body,
                    BuildEconomy(effective), commandSetName, techEnabled: !tech.IsEmpty,
                    side: effective.Side ?? "", kindOf: effective.KindOf));
                indices.Add(row.Name, row.Index);
                gaps.AddRange(templateGaps);
            }
            catch (Exception exception) when (exception is BundleTemplateException
                or FormatException or OverflowException or ArgumentException)
            {
                failures.Add(new BundleTemplateFailure(row.Index, row.Name, exception.Message));
            }
        }

        var loadedNames = templates.Select(template => template.Name)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var horde in hordes.Values)
        {
            foreach (var memberType in horde.RankInfo.Select(rank => rank.UnitType)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(value => value, StringComparer.Ordinal))
            {
                if (!loadedNames.Contains(memberType))
                    unresolved["horde member"].Add(new BundleReferenceIssue(horde.Name, memberType));
            }
        }

        var gapCounts = new SortedDictionary<string, int>(StringComparer.Ordinal);
        foreach (var gap in gaps) Count(gapCounts, gap.Type);

        var report = new BundleLoadReport(
            "Registered implementations use their declared tier; unknown Body carriers are Structural; unknown modules on all other carriers are Cosmetic gaps.",
            templates.Count,
            failures.ToArray(),
            FreezeUnresolved(unresolved),
            gaps.ToArray(),
            new SortedDictionary<string, int>(gapCounts, StringComparer.Ordinal),
            absent.OrderBy(value => value, StringComparer.Ordinal).ToArray(),
            diagnostics.ToArray(),
            notes.ToArray());
        return new BundleTemplateLoadResult(
            templates.ToArray(), indices, BuildHordeTemplates(hordes.Values),
            weapons.Values.ToArray(), armors.Values.ToArray(), tech, report);
    }

    private static void ValidateStructuralModules(
        IReadOnlyList<ModuleSpec> specs,
        ModuleRegistry registry)
    {
        foreach (var spec in specs.Where(value => value.Tier == ModuleTier.Structural))
        {
            try
            {
                if (!registry.TryCreate(spec, out _))
                {
                    throw new BundleTemplateException(
                        $"unknown structural module '{spec.TypeName}' ({spec.Carrier})");
                }
            }
            catch (TargetInvocationException exception)
                when (exception.InnerException is ArgumentException or FormatException or OverflowException)
            {
                throw new BundleTemplateException(
                    $"structural module '{spec.TypeName}' is invalid: {exception.InnerException.Message}");
            }
            catch (Exception exception)
                when (exception is ArgumentException or FormatException or OverflowException)
            {
                throw new BundleTemplateException(
                    $"structural module '{spec.TypeName}' is invalid: {exception.Message}");
            }
        }
    }

    private static EffectiveTemplate Resolve(
        BundleTemplateRow row,
        IReadOnlyDictionary<string, BundleTemplateRow> rawByName,
        IDictionary<string, EffectiveTemplate> cache,
        ISet<string> resolving)
    {
        if (cache.TryGetValue(row.Name, out var cached)) return cached;
        if (!resolving.Add(row.Name))
            throw new BundleTemplateException($"inheritance cycle includes '{row.Name}'");
        try
        {
            EffectiveTemplate? parent = null;
            if (row.Parent != null)
            {
                if (!rawByName.TryGetValue(row.Parent, out var parentRow))
                    throw new BundleTemplateException($"parent '{row.Parent}' was not found");
                parent = Resolve(parentRow, rawByName, cache, resolving);
            }
            var fields = new SortedDictionary<string, BundleValue>(StringComparer.Ordinal);
            if (parent != null)
            {
                foreach (var pair in parent.Fields) fields.Add(pair.Key, pair.Value);
            }
            foreach (var pair in row.Fields) fields[pair.Key] = pair.Value;
            var modules = parent?.Modules.ToList() ?? new List<BundleModuleRow>();
            ApplyModules(modules, row.Modules);
            var blocks = parent?.Blocks.ToList() ?? new List<BundleBlock>();
            blocks.AddRange(row.Blocks);
            var effective = new EffectiveTemplate(
                row.Name,
                row.Side ?? parent?.Side,
                ResolveKindOf(parent?.KindOf, row.KindOf),
                fields,
                blocks.ToArray(),
                modules.ToArray(),
                row.BuildCost ?? parent?.BuildCost,
                row.BuildTime ?? parent?.BuildTime,
                row.CommandPoints ?? parent?.CommandPoints,
                row.Health ?? parent?.Health);
            cache.Add(row.Name, effective);
            return effective;
        }
        finally
        {
            resolving.Remove(row.Name);
        }
    }

    private static void ApplyModules(List<BundleModuleRow> inherited, IReadOnlyList<BundleModuleRow> authored)
    {
        foreach (var module in authored)
        {
            foreach (var tag in DirectiveTags(module, "RemoveModule"))
                inherited.RemoveAll(candidate => candidate.Tag.Equals(tag, StringComparison.OrdinalIgnoreCase));

            var replaceTags = DirectiveTags(module, "ReplaceModule");
            var replaceTag = replaceTags.FirstOrDefault();
            if (replaceTag != null)
            {
                var replaceIndex = inherited.FindIndex(candidate =>
                    candidate.Tag.Equals(replaceTag, StringComparison.OrdinalIgnoreCase));
                if (replaceIndex >= 0) inherited[replaceIndex] = module;
                else inherited.Add(module);
                continue;
            }
            var tagIndex = string.IsNullOrEmpty(module.Tag) ? -1 : inherited.FindIndex(candidate =>
                candidate.Tag.Equals(module.Tag, StringComparison.OrdinalIgnoreCase));
            if (tagIndex >= 0 && !module.Fields.ContainsKey("AddModule")) inherited[tagIndex] = module;
            else inherited.Add(module);
        }
    }

    private static bool HasDirective(BundleModuleRow row) =>
        row.Fields.ContainsKey("RemoveModule")
        || row.Fields.ContainsKey("ReplaceModule")
        || row.Fields.ContainsKey("AddModule");

    private static IReadOnlyList<string> DirectiveTags(BundleModuleRow row, string name)
    {
        if (!row.Fields.TryGetValue(name, out var value)) return Array.Empty<string>();
        return ValueStrings(value)
            .SelectMany(item => item.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
            .ToArray();
    }

    private static ModuleSpec ToModuleSpec(BundleModuleRow row, ModuleRegistry registry)
    {
        var known = registry.TryGetTier(row.Type, out var tier);
        if (!known) tier = CarrierTier(row.Carrier, row.Type);
        if ((!known || row.Gap) && tier == ModuleTier.Structural)
        {
            var state = row.Gap ? "unparseable" : "unknown";
            throw new BundleTemplateException($"{state} structural module '{row.Type}' ({row.Carrier})");
        }
        var data = new SortedDictionary<string, long>(StringComparer.Ordinal);
        var strings = new SortedDictionary<string, string>(StringComparer.Ordinal);
        MapFields(row.Fields, data, strings);
        return new ModuleSpec(
            row.Type, data, strings, tier, row.Fields, row.Blocks,
            row.Carrier, row.Tag, row.Gap || !known);
    }

    private static IReadOnlyList<string> ResolveKindOf(
        IReadOnlyList<string>? inherited,
        IReadOnlyList<string> authored)
    {
        if (authored.Count == 0) return inherited?.ToArray() ?? Array.Empty<string>();
        if (!authored.Any(value => value.StartsWith('+') || value.StartsWith('-')))
            return authored.ToArray();
        var result = inherited?.ToList() ?? new List<string>();
        foreach (var token in authored)
        {
            var remove = token.StartsWith('-');
            var normalized = token.StartsWith('+') || remove ? token[1..] : token;
            if (normalized.Length == 0) continue;
            result.RemoveAll(value => value.Equals(normalized, StringComparison.OrdinalIgnoreCase));
            if (!remove) result.Add(normalized);
        }
        return result.ToArray();
    }

    private static ModuleSpec AttachHorde(ModuleSpec spec, BundleHordeRow horde)
    {
        var data = new SortedDictionary<string, long>(StringComparer.Ordinal);
        foreach (var pair in spec.Data) data.Add(pair.Key, pair.Value);
        data["MemberCount"] = horde.RankInfo.Sum(value => (long)value.Positions.Count);
        var strings = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (var pair in spec.StringData) strings.Add(pair.Key, pair.Value);
        var memberTypes = horde.RankInfo.Select(value => value.UnitType)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();
        if (memberTypes.Length == 1) strings["MemberTemplate"] = memberTypes[0];
        return new ModuleSpec(
            spec.TypeName, data, strings, spec.Tier, spec.Fields, spec.Blocks,
            spec.Carrier, spec.Tag, spec.Gap);
    }

    private static IReadOnlyList<HordeProductionTemplate> BuildHordeTemplates(
        IEnumerable<BundleHordeRow> hordes) => hordes
        .Select(horde => new HordeProductionTemplate(
            horde.Name,
            horde.RankInfo.Select(rank => new HordeProductionRank(
                rank.Rank,
                rank.UnitType,
                rank.Positions.Select(position => new FixedVector2(position.X, position.Y)).ToArray()))))
        .ToArray();

    private static void MapFields(
        IReadOnlyDictionary<string, BundleValue> fields,
        IDictionary<string, long> data,
        IDictionary<string, string> strings)
    {
        foreach (var (name, value) in fields)
        {
            switch (value.Kind)
            {
                case BundleValueKind.Integer:
                    data[name] = value.Integer;
                    break;
                case BundleValueKind.Fixed:
                    data[name + "Raw"] = value.Fixed.Raw;
                    break;
                case BundleValueKind.Boolean:
                    data[name] = value.Boolean ? 1 : 0;
                    break;
                case BundleValueKind.String:
                    strings[name] = value.String!;
                    break;
                case BundleValueKind.List:
                    strings[name] = string.Join("\n", ValueStrings(value));
                    break;
            }
        }
    }

    private static BodyHealthTemplate? ResolveBodyHealth(
        EffectiveTemplate row,
        IReadOnlyList<ModuleSpec> specs)
    {
        var health = row.Health;
        if (health == null)
        {
            var body = specs.LastOrDefault(spec => spec.Carrier == "Body");
            if (body != null)
            {
                if (body.Data.TryGetValue("MaxHealthRaw", out var raw)) health = Fixed64.FromRaw(raw);
                else if (body.Data.TryGetValue("MaxHealth", out var integer)) health = Fixed64.FromInt64(integer);
            }
        }
        return health is { } value && value > Fixed64.Zero ? new BodyHealthTemplate(value) : null;
    }

    private static EconomyTemplate BuildEconomy(EffectiveTemplate row)
    {
        return new EconomyTemplate(
            row.BuildCost is { } cost ? ExactLong(cost, "BuildCost") : 0,
            row.BuildTime is { } time ? ExactMilliseconds(time, "BuildTime") : 0,
            row.CommandPoints ?? 0);
    }

    private static long ExactLong(Fixed64 value, string name)
    {
        if (value.Raw % Fixed64.OneRaw != 0)
            throw new BundleTemplateException($"{name} is not an exact integer");
        return value.Raw / Fixed64.OneRaw;
    }

    private static long ExactMilliseconds(Fixed64 seconds, string name)
    {
        var scaled = (System.Numerics.BigInteger)seconds.Raw * 1000;
        if (scaled % Fixed64.OneRaw != 0)
            throw new BundleTemplateException($"{name} cannot be represented as exact milliseconds");
        var milliseconds = scaled / Fixed64.OneRaw;
        if (milliseconds < long.MinValue || milliseconds > long.MaxValue)
            throw new OverflowException($"{name} millisecond conversion overflow");
        return (long)milliseconds;
    }

    private static IReadOnlyList<WeaponSet> ParseWeaponSets(
        string template,
        IReadOnlyList<BundleBlock> blocks,
        bool tablePresent,
        IReadOnlySet<string> authoredNames,
        IDictionary<string, List<BundleReferenceIssue>> unresolved,
        ICollection<string> diagnostics)
    {
        var result = new List<WeaponSet>();
        foreach (var block in blocks.Where(item => item.Type.Equals("WeaponSet", StringComparison.OrdinalIgnoreCase)))
        {
            try
            {
                var set = WeaponSet.Parse(ParserRow(block.Fields, block.Blocks));
                result.Add(set);
                foreach (var weapon in set.Weapons.Values)
                {
                    if (!weapon.Equals("None", StringComparison.OrdinalIgnoreCase)
                        && (!tablePresent || !authoredNames.Contains(weapon)))
                        unresolved["weapon"].Add(new BundleReferenceIssue(template, weapon));
                }
            }
            catch (Exception exception) when (exception is FormatException or ArgumentException or OverflowException)
            {
                diagnostics.Add($"{template}: WeaponSet could not load: {exception.Message}");
            }
        }
        return result.ToArray();
    }

    private static IReadOnlyList<ArmorSet> ParseArmorSets(
        string template,
        IReadOnlyList<BundleBlock> blocks,
        bool tablePresent,
        IReadOnlySet<string> authoredNames,
        IDictionary<string, List<BundleReferenceIssue>> unresolved,
        ICollection<string> diagnostics)
    {
        var result = new List<ArmorSet>();
        foreach (var block in blocks.Where(item => item.Type.Equals("ArmorSet", StringComparison.OrdinalIgnoreCase)))
        {
            try
            {
                var set = ArmorSet.Parse(ParserRow(block.Fields, block.Blocks));
                result.Add(set);
                if (!tablePresent || !authoredNames.Contains(set.ArmorName))
                    unresolved["armor"].Add(new BundleReferenceIssue(template, set.ArmorName));
            }
            catch (Exception exception) when (exception is FormatException or ArgumentException or OverflowException)
            {
                diagnostics.Add($"{template}: ArmorSet could not load: {exception.Message}");
            }
        }
        return result.ToArray();
    }

    private static void AddResolvedLocomotor(
        string template,
        IReadOnlyList<BundleBlock> blocks,
        ICollection<ModuleSpec> specs,
        IReadOnlyList<BundleNamedRow>? authoredTable,
        IReadOnlyDictionary<string, BundleNamedRow> table,
        IReadOnlyDictionary<string, BundleNamedRow> locomotorSets,
        IDictionary<string, List<BundleReferenceIssue>> unresolved,
        ModuleRegistry registry)
    {
        if (specs.Any(spec => spec.TypeName == LocomotorModule.TypeName)) return;
        var references = blocks
            .Where(item => item.Type.Equals("LocomotorSet", StringComparison.OrdinalIgnoreCase))
            .Where(item => item.Fields.ContainsKey("Locomotor"))
            .Select(item => (Reference: item.Fields["Locomotor"], Overrides: item.Fields))
            .ToList();
        if (references.Count == 0 && locomotorSets.TryGetValue(template, out var set)
            && set.Fields.TryGetValue("Locomotor", out var tableReference))
            references.Add((tableReference, set.Fields));
        foreach (var reference in references)
        {
            foreach (var line in ValueStrings(reference.Reference))
            {
                var tokens = line.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
                var name = tokens.Length >= 2 ? tokens[^1] : tokens.FirstOrDefault();
                if (string.IsNullOrWhiteSpace(name)) continue;
                if (authoredTable == null || !table.TryGetValue(name, out var row))
                {
                    unresolved["locomotor"].Add(new BundleReferenceIssue(template, name));
                    return;
                }
                var fields = new SortedDictionary<string, BundleValue>(StringComparer.Ordinal);
                foreach (var pair in row.Fields) fields.Add(pair.Key, pair.Value);
                foreach (var pair in reference.Overrides) fields[pair.Key] = pair.Value;
                var module = new BundleModuleRow("Behavior", LocomotorModule.TypeName,
                    "ModuleTag_BundleLocomotor", fields, Array.Empty<BundleBlock>(), false);
                specs.Add(ToModuleSpec(module, registry));
                return;
            }
        }
    }

    private static SortedDictionary<string, WeaponTemplate> ParseWeapons(
        IReadOnlyList<BundleWeaponRow>? rows,
        int tickMilliseconds,
        ICollection<string> diagnostics)
    {
        var result = new SortedDictionary<string, WeaponTemplate>(StringComparer.OrdinalIgnoreCase);
        if (rows == null) return result;
        foreach (var row in rows)
        {
            try
            {
                result.Add(row.Name, WeaponTemplate.Parse(row.Name,
                    ParserWeaponRow(row), tickMilliseconds));
            }
            catch (Exception exception) when (exception is FormatException or ArgumentException or OverflowException)
            {
                diagnostics.Add($"weapon {row.Name}: table row could not load: {exception.Message}");
            }
        }
        return result;
    }

    private static SortedDictionary<string, ArmorTemplate> ParseArmors(
        IReadOnlyList<BundleArmorRow>? rows,
        ICollection<string> diagnostics)
    {
        var result = new SortedDictionary<string, ArmorTemplate>(StringComparer.OrdinalIgnoreCase);
        if (rows == null) return result;
        foreach (var row in rows)
        {
            try
            {
                var multipliers = row.Entries.Select(entry =>
                    new KeyValuePair<DamageType, Fixed64>(
                        DamageTypeNames.Parse(entry.DamageType),
                        entry.Percent / Fixed64.FromInt(100)));
                result.Add(row.Name, new ArmorTemplate(row.Name, multipliers));
            }
            catch (Exception exception) when (exception is FormatException or ArgumentException or OverflowException)
            {
                diagnostics.Add($"armor {row.Name}: table row could not load: {exception.Message}");
            }
        }
        return result;
    }

    private static SortedDictionary<string, BundleNamedRow> IndexRows(
        IReadOnlyList<BundleNamedRow>? rows,
        string kind,
        ICollection<string> diagnostics)
    {
        var result = new SortedDictionary<string, BundleNamedRow>(StringComparer.OrdinalIgnoreCase);
        if (rows == null) return result;
        foreach (var row in rows)
        {
            if (!result.TryAdd(row.Name, row)) diagnostics.Add($"{kind} table has duplicate name '{row.Name}'");
        }
        return result;
    }

    private static SortedDictionary<string, BundleHordeRow> IndexHordes(
        IReadOnlyList<BundleHordeRow>? rows,
        ICollection<string> diagnostics)
    {
        var result = new SortedDictionary<string, BundleHordeRow>(StringComparer.OrdinalIgnoreCase);
        if (rows == null) return result;
        foreach (var row in rows)
        {
            if (!result.TryAdd(row.Name, row)) diagnostics.Add($"horde table has duplicate name '{row.Name}'");
        }
        return result;
    }

    private static IReadOnlyDictionary<string, object?> ParserRow(
        IReadOnlyDictionary<string, BundleValue> fields,
        IReadOnlyList<BundleBlock> blocks)
    {
        var converted = new SortedDictionary<string, object?>(StringComparer.Ordinal);
        foreach (var (name, value) in fields) converted.Add(name, value.ToObject());
        foreach (var group in blocks.GroupBy(block => block.Type, StringComparer.Ordinal))
        {
            var values = group.Select(BlockObject).ToArray();
            converted[group.Key] = values.Length == 1 ? values[0] : values;
        }
        return new SortedDictionary<string, object?>(StringComparer.Ordinal) { ["fields"] = converted };
    }

    private static IReadOnlyDictionary<string, object?> BlockObject(BundleBlock block) =>
        ParserRow(block.Fields, block.Blocks);

    private static IReadOnlyDictionary<string, object?> ParserWeaponRow(BundleWeaponRow row)
    {
        var converted = new SortedDictionary<string, object?>(StringComparer.Ordinal);
        foreach (var (name, value) in row.Fields) converted.Add(name, value.ToObject());
        foreach (var group in row.Nuggets
            .Where(nugget => nugget.Kind != "other")
            .GroupBy(nugget => nugget.Kind, StringComparer.Ordinal))
        {
            var values = group.Select(nugget =>
                (object?)new SortedDictionary<string, object?>(StringComparer.Ordinal)
                {
                    ["fields"] = nugget.Fields.ToDictionary(
                        pair => pair.Key, pair => pair.Value.ToObject(), StringComparer.Ordinal),
                }).ToArray();
            converted[group.Key] = values.Length == 1 ? values[0] : values;
        }
        return new SortedDictionary<string, object?>(StringComparer.Ordinal) { ["fields"] = converted };
    }

    private static IEnumerable<string> ValueStrings(BundleValue value)
    {
        if (value.Kind == BundleValueKind.List)
        {
            foreach (var item in value.Items)
                foreach (var text in ValueStrings(item)) yield return text;
            yield break;
        }
        yield return value.Kind switch
        {
            BundleValueKind.String => value.String!,
            BundleValueKind.Integer => value.Integer.ToString(System.Globalization.CultureInfo.InvariantCulture),
            BundleValueKind.Fixed => value.Fixed.ToString(),
            BundleValueKind.Boolean => value.Boolean ? "true" : "false",
            _ => "",
        };
    }

    private static ModuleTier CarrierTier(string carrier, string typeName) =>
        typeName.Contains("SpecialPower", StringComparison.Ordinal)
            || typeName.EndsWith("Upgrade", StringComparison.Ordinal)
            ? ModuleTier.Cosmetic
            : carrier switch
    {
        "Body" => ModuleTier.Structural,
        _ => ModuleTier.Cosmetic,
    };

    private static SortedDictionary<string, List<BundleReferenceIssue>> NewUnresolved() =>
        new(StringComparer.Ordinal)
        {
            ["armor"] = new List<BundleReferenceIssue>(),
            ["horde member"] = new List<BundleReferenceIssue>(),
            ["command_button"] = new List<BundleReferenceIssue>(),
            ["command_set"] = new List<BundleReferenceIssue>(),
            ["horde"] = new List<BundleReferenceIssue>(),
            ["locomotor"] = new List<BundleReferenceIssue>(),
            ["object_template"] = new List<BundleReferenceIssue>(),
            ["science"] = new List<BundleReferenceIssue>(),
            ["special_power"] = new List<BundleReferenceIssue>(),
            ["upgrade"] = new List<BundleReferenceIssue>(),
            ["weapon"] = new List<BundleReferenceIssue>(),
        };

    private static IReadOnlySet<string> ReferenceNames(IEnumerable<string>? names) =>
        names?.ToHashSet(StringComparer.OrdinalIgnoreCase)
        ?? new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    private static TechCatalog BuildTechCatalog(
        BundleDocument document,
        ICollection<string> diagnostics,
        IDictionary<string, List<BundleReferenceIssue>> unresolved)
    {
        var upgrades = IndexTech(document.Upgrades, value => value.Name, "upgrade", diagnostics);
        var sciences = IndexTech(document.Sciences, value => value.Name, "science", diagnostics);
        var powers = IndexTech(document.SpecialPowers, value => value.Name, "special_power", diagnostics);
        var buttons = IndexTech(document.CommandButtons, value => value.Name, "command_button", diagnostics);
        var sets = new List<CommandSetTemplate>();
        var setNames = new HashSet<string>(StringComparer.Ordinal);
        foreach (var row in document.CommandSets ?? Array.Empty<BundleCommandSetRow>())
        {
            if (!setNames.Add(row.Name))
            {
                diagnostics.Add($"command_set table has duplicate name '{row.Name}'");
                continue;
            }
            var entries = new List<CommandSetEntryTemplate>();
            foreach (var entry in row.Entries.OrderBy(value => value.Slot))
            {
                if (string.IsNullOrWhiteSpace(entry.Button)) continue;
                if (!buttons.TryGetValue(entry.Button, out var button))
                {
                    unresolved["command_button"].Add(new BundleReferenceIssue(row.Name, entry.Button));
                    continue;
                }
                entries.Add(new CommandSetEntryTemplate(entry.Slot, entry.Button, button));
            }
            sets.Add(new CommandSetTemplate(row.Name, entries.ToArray()));
        }

        foreach (var science in sciences.Values)
            foreach (var prerequisite in science.PrerequisiteSciences)
                if (!sciences.ContainsKey(prerequisite))
                    unresolved["science"].Add(new BundleReferenceIssue(science.Name, prerequisite));
        foreach (var power in powers.Values)
            foreach (var required in power.RequiredSciences)
                if (!sciences.ContainsKey(required))
                    unresolved["science"].Add(new BundleReferenceIssue(power.Name, required));
        foreach (var button in buttons.Values)
        {
            if (button.Upgrade.Length > 0 && !upgrades.ContainsKey(button.Upgrade))
                unresolved["upgrade"].Add(new BundleReferenceIssue(button.Name, button.Upgrade));
            if (button.Science.Length > 0 && !sciences.ContainsKey(button.Science))
                unresolved["science"].Add(new BundleReferenceIssue(button.Name, button.Science));
            if (button.SpecialPower.Length > 0 && !powers.ContainsKey(button.SpecialPower))
                unresolved["special_power"].Add(new BundleReferenceIssue(button.Name, button.SpecialPower));
            if (button.Object.Length > 0 && !document.Templates.Any(row => row.Name == button.Object))
                unresolved["object_template"].Add(new BundleReferenceIssue(button.Name, button.Object));
        }
        return new TechCatalog(upgrades.Values, sciences.Values, powers.Values, buttons.Values, sets);
    }

    private static SortedDictionary<string, T> IndexTech<T>(
        IReadOnlyList<T>? rows,
        Func<T, string> name,
        string kind,
        ICollection<string> diagnostics)
    {
        var result = new SortedDictionary<string, T>(StringComparer.Ordinal);
        foreach (var row in rows ?? Array.Empty<T>())
        {
            if (!result.TryAdd(name(row), row)) diagnostics.Add($"{kind} table has duplicate name '{name(row)}'");
        }
        return result;
    }

    private static IReadOnlyDictionary<string, IReadOnlyList<BundleReferenceIssue>> FreezeUnresolved(
        IReadOnlyDictionary<string, List<BundleReferenceIssue>> source)
    {
        var result = new SortedDictionary<string, IReadOnlyList<BundleReferenceIssue>>(StringComparer.Ordinal);
        foreach (var (kind, values) in source)
        {
            result.Add(kind, values
                .OrderBy(value => value.Template, StringComparer.Ordinal)
                .ThenBy(value => value.Name, StringComparer.Ordinal)
                .ToArray());
        }
        return result;
    }

    private static void Count(IDictionary<string, int> counts, string name) =>
        counts[name] = counts.TryGetValue(name, out var count) ? count + 1 : 1;

    private sealed record EffectiveTemplate(
        string Name,
        string? Side,
        IReadOnlyList<string> KindOf,
        IReadOnlyDictionary<string, BundleValue> Fields,
        IReadOnlyList<BundleBlock> Blocks,
        IReadOnlyList<BundleModuleRow> Modules,
        Fixed64? BuildCost,
        Fixed64? BuildTime,
        long? CommandPoints,
        Fixed64? Health);
}

public sealed class BundleTemplateException : Exception
{
    public BundleTemplateException(string message) : base(message) { }
}
