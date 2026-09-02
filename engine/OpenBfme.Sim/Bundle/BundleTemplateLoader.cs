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
    IReadOnlyList<WeaponTemplate> WeaponTemplates,
    IReadOnlyList<ArmorTemplate> ArmorTemplates,
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

        var diagnostics = document.Diagnostics
            .Select(item => $"{item.Template}: {item.Message}")
            .ToList();
        var notes = new List<string>();
        var gaps = new List<BundleModuleGap>();
        var unresolved = NewUnresolved();
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
                    specs.Add(spec);
                    if (spec.Gap)
                        templateGaps.Add(new BundleModuleGap(row.Name, spec.TypeName, spec.Carrier));
                }
                AddResolvedLocomotor(row.Name, effective.Blocks, specs, document.Locomotors,
                    locomotors, locomotorSets, unresolved, registry);
                var body = ResolveBodyHealth(effective, specs);
                templates.Add(new ObjectTemplate(
                    row.Name, specs, weaponSets, armorSets, body,
                    BuildEconomy(effective)));
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
            templates.ToArray(), indices, weapons.Values.ToArray(), armors.Values.ToArray(), report);
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
                row.KindOf.Count == 0 && parent != null ? parent.KindOf : row.KindOf,
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
        if (!known) tier = CarrierTier(row.Carrier);
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
        var commandSet = row.Fields.TryGetValue("CommandSet", out var authoredCommandSet)
            ? ValueStrings(authoredCommandSet)
                .SelectMany(value => value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
                .ToArray()
            : Array.Empty<string>();
        return new EconomyTemplate(
            row.BuildCost is { } cost ? ExactLong(cost, "BuildCost") : 0,
            row.BuildTime is { } time ? ExactMilliseconds(time, "BuildTime") : 0,
            row.CommandPoints ?? 0,
            commandSet);
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

    private static ModuleTier CarrierTier(string carrier) => carrier switch
    {
        "Body" => ModuleTier.Structural,
        _ => ModuleTier.Cosmetic,
    };

    private static SortedDictionary<string, List<BundleReferenceIssue>> NewUnresolved() =>
        new(StringComparer.Ordinal)
        {
            ["armor"] = new List<BundleReferenceIssue>(),
            ["horde member"] = new List<BundleReferenceIssue>(),
            ["locomotor"] = new List<BundleReferenceIssue>(),
            ["weapon"] = new List<BundleReferenceIssue>(),
        };

    private static IReadOnlySet<string> ReferenceNames(IEnumerable<string>? names) =>
        names?.ToHashSet(StringComparer.OrdinalIgnoreCase)
        ?? new HashSet<string>(StringComparer.OrdinalIgnoreCase);

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
