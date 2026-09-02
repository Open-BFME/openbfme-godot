using System.Runtime.CompilerServices;
using OpenBfme.Sim.Pathing;

namespace OpenBfme.Sim;

/// <summary>Immutable per-match configuration: templates, seed, teams. Not part of the state hash.</summary>
public sealed class SimConfig
{
    public const long DefaultMaxCommandPoints = 1_000;
    public IReadOnlyDictionary<string, ObjectTemplate> Templates { get; }
    public ulong RandomSeed { get; }
    public int TeamCount { get; }
    public int MapWidthCells { get; }
    public int MapHeightCells { get; }
    public IReadOnlyDictionary<string, WeaponTemplate> WeaponTemplates { get; }
    public IReadOnlyDictionary<string, ArmorTemplate> ArmorTemplates { get; }
    public TechCatalog Tech { get; }
    public long MaxCommandPoints { get; }
    public IAiBuildListProvider? AiBuildLists { get; }
    private readonly IReadOnlyDictionary<string, int> _templateIndices;

    public SimConfig(
        IEnumerable<ObjectTemplate> templates,
        ulong randomSeed,
        int teamCount,
        int mapWidthCells = 512,
        int mapHeightCells = 512,
        IEnumerable<WeaponTemplate>? weaponTemplates = null,
        IEnumerable<ArmorTemplate>? armorTemplates = null,
        long maxCommandPoints = 0,
        IReadOnlyDictionary<string, int>? templateIndices = null,
        TechCatalog? tech = null,
        IAiBuildListProvider? aiBuildLists = null)
    {
        if (teamCount < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(teamCount));
        }
        var map = new SortedDictionary<string, ObjectTemplate>(StringComparer.Ordinal);
        foreach (var template in templates)
        {
            map.Add(template.Name, template);
        }
        if (mapWidthCells < 1 || mapHeightCells < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(mapWidthCells), "Map grid dimensions must be positive");
        }
        Templates = map;
        var indices = new SortedDictionary<string, int>(StringComparer.Ordinal);
        if (templateIndices == null)
        {
            var index = 0;
            foreach (var name in map.Keys) indices.Add(name, index++);
        }
        else
        {
            foreach (var name in map.Keys)
            {
                if (!templateIndices.TryGetValue(name, out var index) || index < 0)
                    throw new ArgumentException($"Template '{name}' has no valid source index", nameof(templateIndices));
                indices.Add(name, index);
            }
        }
        _templateIndices = indices;
        RandomSeed = randomSeed;
        TeamCount = teamCount;
        MapWidthCells = mapWidthCells;
        MapHeightCells = mapHeightCells;
        WeaponTemplates = NamedTemplates(weaponTemplates, value => value.Name);
        ArmorTemplates = NamedTemplates(armorTemplates, value => value.Name);
        Tech = tech ?? TechCatalog.Empty;
        if (maxCommandPoints < 0) throw new ArgumentOutOfRangeException(nameof(maxCommandPoints));
        MaxCommandPoints = maxCommandPoints;
        AiBuildLists = aiBuildLists;
    }

    public int TemplateIndexOf(string templateName) => _templateIndices[templateName];

    public ObjectTemplate TemplateAtIndex(int templateIndex)
    {
        if (templateIndex < 0) throw new ArgumentOutOfRangeException(nameof(templateIndex));
        foreach (var (name, index) in _templateIndices)
        {
            if (index == templateIndex) return Templates[name];
        }
        throw new ArgumentOutOfRangeException(nameof(templateIndex));
    }

    private static IReadOnlyDictionary<string, T> NamedTemplates<T>(
        IEnumerable<T>? values,
        Func<T, string> name)
    {
        var result = new SortedDictionary<string, T>(StringComparer.Ordinal);
        if (values != null)
        {
            foreach (var value in values) result.Add(name(value), value);
        }
        return result;
    }
}

/// <summary>
/// The deterministic simulation world: fixed integer ticks, command-queue
/// mutation only, canonical hash + snapshot. This is the P0 kernel the module
/// vocabulary grows into.
/// </summary>
public sealed partial class SimWorld
{
    public const int TicksPerSecond = 30;

    private readonly SimConfig _config;
    private readonly ModuleRegistry _registry;
    private readonly SortedDictionary<int, GameObject> _objects = new();
    private readonly SortedDictionary<int, List<SimCommand>> _pendingCommands = new();
    private readonly long[] _teamResources;
    private readonly SortedDictionary<string, int> _moduleGaps = new(StringComparer.Ordinal);
    private readonly List<SimEvent> _eventsThisTick = new();
    private readonly List<SnapshotHorde> _hordes = new();
    private int[] _playerTeams;
    private long[] _commandPoints;
    private long[] _commandPointsMax;
    private long[] _powerPoints;
    private DeterministicRandom _random;
    private int _nextObjectId = 1;
    private bool _inUpdateSweep;
    private readonly List<GameObject> _pendingSpawns = new();
    internal ISimTickPhaseObserver? TickPhaseObserver { get; set; }
    // Aura armor table: summed basis points of incoming-damage reduction per
    // object id. DERIVED state — rebuilt at end of every tick (and after
    // Restore) from AttributeModifierAuraModule caches in ascending carrier id
    // order, so it is deliberately NOT serialized or hashed.
    private readonly SortedDictionary<int, long> _auraArmorBonusBp = new();

    public int TickIndex { get; private set; }
    public int TickMilliseconds { get; }
    public IReadOnlyDictionary<int, GameObject> Objects => _objects;
    public ObjectStore ObjectStore { get; } = new();
    public IReadOnlyList<SimEvent> EventsThisTick => _eventsThisTick;
    public IReadOnlyList<SnapshotHorde> Hordes => _hordes;
    public MovementSystem Movement { get; }
    public CombatSystem Combat { get; }
    public PassabilityGrid PassabilityGrid => Movement.Grid;
    /// <summary>Module type names that had no registered implementation, with occurrence counts. Fail-closed accounting.</summary>
    public IReadOnlyDictionary<string, int> ModuleGaps => _moduleGaps;
    public BundleLoadReport? BundleLoadReport { get; private set; }
    public Map.MapLoadReport? MapLoadReport { get; internal set; }

    public SimWorld(SimConfig config, ModuleRegistry registry)
        : this(config, registry, tickMilliseconds: 33, passabilityGrid: null)
    {
    }

    public SimWorld(
        SimConfig config,
        ModuleRegistry registry,
        int tickMilliseconds,
        PassabilityGrid? passabilityGrid = null)
    {
        _config = config ?? throw new ArgumentNullException(nameof(config));
        _registry = registry ?? throw new ArgumentNullException(nameof(registry));
        if (tickMilliseconds < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(tickMilliseconds));
        }
        TickMilliseconds = tickMilliseconds;
        Movement = new MovementSystem(passabilityGrid
            ?? PassabilityGrid.Uniform(config.MapWidthCells, config.MapHeightCells));
        Combat = new CombatSystem(config);
        _teamResources = new long[config.TeamCount];
        _playerTeams = Enumerable.Range(0, config.TeamCount).ToArray();
        _commandPoints = new long[config.TeamCount];
        _commandPointsMax = Enumerable.Repeat(config.MaxCommandPoints, config.TeamCount).ToArray();
        _powerPoints = new long[config.TeamCount];
        InitializeTechState();
        _random = new DeterministicRandom(config.RandomSeed);
    }

    public SimWorld(
        MatchLaunch launch,
        IEnumerable<ObjectTemplate>? templates = null,
        ModuleRegistry? registry = null,
        PassabilityGrid? passabilityGrid = null,
        long baseCommandPoints = SimConfig.DefaultMaxCommandPoints)
        : this(
            CreateConfig(launch, templates, baseCommandPoints),
            registry ?? ModuleRegistry.CreateDefault(),
            launch?.Rules.TickMilliseconds ?? throw new ArgumentNullException(nameof(launch)),
            passabilityGrid)
    {
        InitializeLaunch(launch);
    }

    public SimWorld(
        MatchLaunch launch,
        SimConfig config,
        ModuleRegistry? registry = null,
        PassabilityGrid? passabilityGrid = null)
        : this(
            config ?? throw new ArgumentNullException(nameof(config)),
            registry ?? ModuleRegistry.CreateDefault(),
            launch?.Rules.TickMilliseconds ?? throw new ArgumentNullException(nameof(launch)),
            passabilityGrid)
    {
        if (launch.Players.Max(player => player.Team) >= config.TeamCount)
            throw new ArgumentException("Launch player team is outside the supplied SimConfig", nameof(launch));
        InitializeLaunch(launch);
    }

    private void InitializeLaunch(MatchLaunch launch)
    {
        _playerTeams = launch.Players.Select(player => player.Team).ToArray();
        _commandPoints = new long[launch.Players.Count];
        _commandPointsMax = Enumerable.Repeat(_config.MaxCommandPoints, launch.Players.Count).ToArray();
        _powerPoints = new long[launch.Players.Count];
        foreach (var player in launch.Players)
        {
            _seatTeams.Add(player.Seat, player.Team);
        }
        for (var team = 0; team < _teamResources.Length; team++)
        {
            if (_playerTeams.Contains(team))
            {
                _teamResources[team] = launch.Rules.StartingResources;
            }
        }
        InitializeAi(launch);
    }

    public static SimWorld FromBundle(
        MatchLaunch launch,
        BundleDocument document,
        PassabilityGrid? passabilityGrid = null)
    {
        ArgumentNullException.ThrowIfNull(launch);
        ArgumentNullException.ThrowIfNull(document);
        var registry = ModuleRegistry.CreateDefault();
        var loaded = BundleTemplateLoader.Load(document, registry, launch.Rules.TickMilliseconds);
        var teamCount = launch.Players.Max(player => player.Team) + 1;
        var scaledCap = EconomyTemplate.ScaleInteger(
            SimConfig.DefaultMaxCommandPoints, launch.Rules.CommandPointMultiplier);
        var config = new SimConfig(
            loaded.Templates,
            launch.Seed,
            teamCount,
            weaponTemplates: loaded.WeaponTemplates,
            armorTemplates: loaded.ArmorTemplates,
            maxCommandPoints: scaledCap,
            templateIndices: loaded.TemplateIndices,
            tech: loaded.Tech);
        var world = new SimWorld(config, registry, launch.Rules.TickMilliseconds, passabilityGrid)
        {
            BundleLoadReport = loaded.Report,
        };
        world.InitializeLaunch(launch);
        return world;
    }

    public static SimWorld FromBundle(
        MatchLaunch launch,
        BundleDocument document,
        Map.MapDocument map) => Map.MapWorldBuilder.Build(launch, document, map);

    private static SimConfig CreateConfig(
        MatchLaunch launch,
        IEnumerable<ObjectTemplate>? templates,
        long baseCommandPoints)
    {
        ArgumentNullException.ThrowIfNull(launch);
        var teamCount = launch.Players.Max(player => player.Team) + 1;
        var scaledCap = EconomyTemplate.ScaleInteger(baseCommandPoints, launch.Rules.CommandPointMultiplier);
        return new SimConfig(
            templates ?? Array.Empty<ObjectTemplate>(),
            launch.Seed,
            teamCount,
            maxCommandPoints: scaledCap);
    }

    public long TeamResources(int team) => _teamResources[ValidateTeam(team)];

    public void AddTeamResources(int team, long amount) => _teamResources[ValidateTeam(team)] += amount;

    public uint NextRandomUInt32() => _random.NextUInt32();

    public void SetPlayerEconomy(
        int playerIndex,
        long commandPoints,
        long commandPointsMax,
        long powerPoints)
    {
        if ((uint)playerIndex >= (uint)_playerTeams.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(playerIndex));
        }
        if (commandPoints < 0 || commandPointsMax < 0 || powerPoints < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(commandPoints));
        }
        _commandPoints[playerIndex] = commandPoints;
        _commandPointsMax[playerIndex] = commandPointsMax;
        _powerPoints[playerIndex] = powerPoints;
    }

    public IReadOnlyList<SnapshotPlayer> SnapshotPlayers()
    {
        var players = new SnapshotPlayer[_playerTeams.Length];
        for (var index = 0; index < players.Length; index++)
        {
            players[index] = new SnapshotPlayer(
                index,
                _teamResources[_playerTeams[index]],
                CommandPointsUsed(_playerTeams[index]),
                _commandPointsMax[index],
                _powerPoints[index]);
        }
        return players;
    }

    public void AddHorde(SnapshotHorde horde)
    {
        ArgumentNullException.ThrowIfNull(horde);
        if (horde.Id < 1 || horde.Owner < -1 || horde.TemplateIndex < 0 || horde.Formation < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(horde));
        }
        var insertAt = _hordes.BinarySearch(horde, SnapshotHordeIdComparer.Instance);
        if (insertAt >= 0) throw new ArgumentException($"Horde id {horde.Id} already exists", nameof(horde));
        _hordes.Insert(~insertAt, horde with { Members = horde.Members.ToArray() });
    }

    public void SetPassabilityGrid(PassabilityGrid grid) => Movement.ReplaceGrid(grid);

    public void RaiseEvent(SimEvent simEvent)
    {
        ArgumentNullException.ThrowIfNull(simEvent);
        simEvent.Validate();
        _eventsThisTick.Add(simEvent);
    }

    public GameObject SpawnObject(
        string templateName,
        int team,
        FixedVector2 position,
        Fixed64 elevation = default,
        Fixed64 headingRadians = default)
    {
        if (team < -1 || team >= _config.TeamCount)
        {
            throw new ArgumentOutOfRangeException(nameof(team),
                $"Team {team} outside neutral (-1) or 0..{_config.TeamCount - 1}");
        }
        if (!_config.Templates.TryGetValue(templateName, out var template))
        {
            throw new KeyNotFoundException($"Unknown object template '{templateName}'");
        }
        var modules = new List<ModuleBase>(template.Modules.Count);
        foreach (var spec in template.Modules)
        {
            if (_registry.TryCreate(spec, out var module))
            {
                modules.Add(module!);
            }
            else if (spec.Tier == ModuleTier.Structural)
            {
                throw new ModuleLoadException(
                    $"Template '{templateName}' requires unknown structural module '{spec.TypeName}'");
            }
            else
            {
                _moduleGaps[spec.TypeName] = _moduleGaps.TryGetValue(spec.TypeName, out var count) ? count + 1 : 1;
            }
        }
        var gameObject = new GameObject(
            _nextObjectId++, template, team, position, modules, elevation, headingRadians,
            techEnabled: !_config.Tech.IsEmpty);
        gameObject.StoreSlot = ObjectStore.Allocate(gameObject.Id);
        SynchronizeObject(gameObject);
        if (_inUpdateSweep)
        {
            // Spawns requested by modules mid-sweep (production, death rubble)
            // are deferred so scanning modules never see the object dictionary
            // mutate under them; newcomers join at end of sweep, first update
            // next tick — deterministically.
            _pendingSpawns.Add(gameObject);
        }
        else
        {
            _objects.Add(gameObject.Id, gameObject);
        }
        ApplyOwnedPlayerUpgrades(gameObject);
        RaiseEvent(new SimEvent("spawn", gameObject.Id, Name: templateName));
        return gameObject;
    }

    public bool SubmitCommand(SimCommand command)
    {
        if (command.Tick <= TickIndex)
        {
            return false;
        }
        if (command.Team < 0 || command.Team >= _config.TeamCount)
        {
            return false;
        }
        if (!_pendingCommands.TryGetValue(command.Tick, out var list))
        {
            list = new List<SimCommand>();
            _pendingCommands.Add(command.Tick, list);
        }
        list.Add(command);
        return true;
    }

    public void Tick()
    {
        _eventsThisTick.Clear();
        RunAiForTick(checked(TickIndex + 1));
        TickIndex++;
        BeginPhase(SimTickPhase.Commands);
        ApplyPendingCommands();
        EndPhase(SimTickPhase.Commands);
        BeginPhase(SimTickPhase.Movement);
        Combat.PrepareMovement(this);
        Movement.Tick(this);
        EndPhase(SimTickPhase.Movement);
        BeginPhase(SimTickPhase.Combat);
        Combat.Resolve(this);
        EndPhase(SimTickPhase.Combat);
        // The object dictionary is frozen for the whole sweep: mid-sweep spawns
        // divert to _pendingSpawns (so modules scanning Objects never see it
        // mutate) and join afterwards, first updating next tick. Dead objects
        // never update.
        BeginPhase(SimTickPhase.Modules);
        var updateList = new List<GameObject>(_objects.Values);
        _inUpdateSweep = true;
        try
        {
            foreach (var gameObject in updateList)
            {
                if (gameObject.IsDead)
                {
                    continue;
                }
                foreach (var module in gameObject.Modules)
                {
                    var economyModule = TickPhaseObserver != null
                        && module is ProductionModule or AutoDepositUpdateModule or GettingBuiltModule;
                    if (economyModule) BeginPhase(SimTickPhase.Economy);
                    module.OnUpdate(this, gameObject);
                    if (economyModule) EndPhase(SimTickPhase.Economy);
                    if (gameObject.IsDead)
                    {
                        break;
                    }
                }
            }
        }
        finally
        {
            _inUpdateSweep = false;
        }
        foreach (var spawned in _pendingSpawns)
        {
            _objects.Add(spawned.Id, spawned);
        }
        _pendingSpawns.Clear();
        RemoveDeadObjects();
        RebuildAuraTable();
        EndPhase(SimTickPhase.Modules);
        BeginPhase(SimTickPhase.StoreSync);
        SynchronizeObjectStore();
        EndPhase(SimTickPhase.StoreSync);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void BeginPhase(SimTickPhase phase) => TickPhaseObserver?.Begin(phase);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void EndPhase(SimTickPhase phase) => TickPhaseObserver?.End(phase);

    /// <summary>
    /// Rebuilds the aura armor table from every living, non-dying, constructed
    /// carrier's cached member ids (ascending carrier id order — deterministic).
    /// Runs at end of tick so the table is stable for the whole following tick.
    /// </summary>
    private void RebuildAuraTable()
    {
        _auraArmorBonusBp.Clear();
        foreach (var gameObject in _objects.Values)
        {
            if (gameObject.IsDying || gameObject.IsUnderConstruction)
            {
                continue;
            }
            foreach (var module in gameObject.Modules)
            {
                if (module is AttributeModifierAuraModule aura)
                {
                    aura.ContributeTo(_auraArmorBonusBp, this);
                }
            }
        }
    }

    /// <summary>Summed aura armor basis points currently granted to an object (0 if none).</summary>
    public long AuraArmorBonusBp(int objectId) =>
        _auraArmorBonusBp.TryGetValue(objectId, out var bp) ? bp : 0;

    public void Advance(int ticks)
    {
        for (var i = 0; i < ticks; i++)
        {
            Tick();
        }
    }

    private void ApplyPendingCommands()
    {
        if (!_pendingCommands.TryGetValue(TickIndex, out var commands))
        {
            return;
        }
        _pendingCommands.Remove(TickIndex);
        foreach (var command in commands
            .OrderBy(command => command.Team)
            .ThenBy(command => command.Seq))
        {
            ApplyCommand(command);
        }
    }

    private void ApplyCommand(SimCommand command)
    {
        switch (command.Type)
        {
            case "spawn":
                SpawnObject(command.GetString("template"), command.Team,
                    new FixedVector2(command.GetFixed("x"), command.GetFixed("y")));
                break;
            case "move":
                ApplyMovementCommand(command);
                Combat.ApplyCommand(this, command, ownershipAlreadyChecked: true);
                break;
            case "attack_move":
            case "stop":
                ApplyMovementCommand(command);
                Combat.ApplyCommand(this, command, ownershipAlreadyChecked: true);
                break;
            case "attack":
            case "stance":
                Combat.ApplyCommand(this, command);
                break;
            case "damage":
                if (_objects.TryGetValue((int)command.GetLong("id"), out var victim))
                {
                    DealDamage(victim, command.GetLong("amount"));
                }
                break;
            case "set_resources":
                _teamResources[command.Team] = command.GetLong("amount");
                break;
            case "roll":
                // Consumes randomness so tests prove the RNG stream is authoritative state.
                _teamResources[command.Team] += _random.NextBelow(100);
                break;
            case "queue_production":
                if (_objects.TryGetValue((int)command.GetLong("id"), out var producer)
                    && producer.Team == command.Team)
                {
                    // Pass-through: cost debit/affordability live in TryQueue.
                    producer.FindModule<ProductionModule>()?.TryQueue(this, producer, command.GetString("template"));
                }
                break;
            case "cancel_production":
                if (_objects.TryGetValue((int)command.GetLong("id"), out var canceller)
                    && canceller.Team == command.Team)
                {
                    canceller.FindModule<ProductionModule>()?.TryCancel(this, canceller, (int)command.GetLong("index"));
                }
                break;
            case "train":
                ApplyTrainCommand(command);
                break;
            case "cancel":
                ApplyCancelCommand(command);
                break;
            case "rally":
                ApplyRallyCommand(command);
                break;
            case "build":
                ApplyBuildCommand(command);
                break;
            case "sell":
                ApplySellCommand(command);
                break;
            case "upgrade":
                ApplyUpgradeCommand(command);
                break;
            case "power":
                ApplyPowerCommand(command);
                break;
            default:
                // Unknown command types are ignored deterministically (validated upstream
                // by the lockstep layer); they still affected the hash while queued.
                break;
        }
    }

    public void DealDamage(GameObject target, long amount) => DealDamage(target, amount, DamageTypes.Default);

    public void DealDamage(GameObject target, long amount, string damageType)
    {
        if (target.IsDead || target.IsDying)
        {
            return;
        }
        // Crush damage only lands on objects that declare themselves crushable.
        if (damageType == DamageTypes.Crush && target.FindModule<SquishCollideModule>() == null)
        {
            return;
        }
        foreach (var module in target.Modules)
        {
            amount = module.ModifyIncomingDamage(target, damageType, amount);
        }
        // Aura armor applies after the per-module chain. Stacked contributions
        // add; the sum is clamped to [0, 10000] bp (full immunity, never a heal).
        var auraBp = Math.Clamp(AuraArmorBonusBp(target.Id), 0, 10_000);
        if (auraBp > 0)
        {
            amount -= amount * auraBp / 10_000;
        }
        if (amount <= 0)
        {
            return;
        }
        RaiseEvent(new SimEvent("damage", target.Id, Amount: Fixed64.FromInt64(amount)));
        foreach (var module in target.Modules)
        {
            if (module.OnDamage(this, target, amount))
            {
                return;
            }
        }
    }

    /// <summary>
    /// Death pipeline: the first module claiming the death (SlowDeath-shaped)
    /// owns removal timing; otherwise the object is removed at end of tick.
    /// </summary>
    public void HandleDeath(GameObject target)
    {
        if (target.IsDead || target.IsDying)
        {
            return;
        }
        RaiseEvent(new SimEvent("death", target.Id));
        foreach (var module in target.Modules)
        {
            if (module.OnDeath(this, target))
            {
                return;
            }
        }
        target.MarkDead();
    }

    private void RemoveDeadObjects()
    {
        List<int>? deadIds = null;
        foreach (var (id, gameObject) in _objects)
        {
            if (gameObject.IsDead)
            {
                (deadIds ??= new List<int>()).Add(id);
            }
        }
        if (deadIds == null)
        {
            return;
        }
        ExpandHordeDeaths(deadIds);
        PruneDeadHordeMembers(deadIds);
        foreach (var id in deadIds)
        {
            ReleasePlotForObject(id);
            _objects[id].FindModule<ProductionModule>()?.RefundAll(this, _objects[id]);
            ObjectStore.Free(_objects[id].StoreSlot);
            _objects.Remove(id);
        }
    }

    private int ValidateTeam(int team) =>
        team >= 0 && team < _config.TeamCount
            ? team
            : throw new ArgumentOutOfRangeException(nameof(team), $"Team {team} outside 0..{_config.TeamCount - 1}");

    private void WriteAuthoritativeState(CanonicalWriter writer)
    {
        writer.WriteInt(TickIndex);
        writer.WriteInt(_nextObjectId);
        writer.WriteInt(_teamResources.Length);
        foreach (var resources in _teamResources)
        {
            writer.WriteLong(resources);
        }
        var (randomState, randomIncrement) = _random.Serialize();
        writer.WriteLong(unchecked((long)randomState));
        writer.WriteLong(unchecked((long)randomIncrement));
        writer.WriteInt(_objects.Count);
        foreach (var gameObject in _objects.Values)
        {
            gameObject.WriteState(writer);
        }
        writer.WriteInt(_pendingCommands.Count);
        foreach (var (tick, commands) in _pendingCommands)
        {
            writer.WriteInt(tick);
            writer.WriteInt(commands.Count);
            foreach (var command in commands)
            {
                command.WriteTo(writer);
            }
        }
        WriteMovementExtension(writer);
        WriteEconomyExtension(writer);
        WriteTechExtension(writer);
        WriteAiExtension(writer);
    }

    public string StateHash()
    {
        BeginPhase(SimTickPhase.Hash);
        SynchronizeObjectStore();
        var writer = new CanonicalWriter();
        WriteAuthoritativeState(writer);
        var hash = writer.ToSha256Hex();
        EndPhase(SimTickPhase.Hash);
        return hash;
    }

    public byte[] Snapshot()
    {
        SynchronizeObjectStore();
        var writer = new CanonicalWriter();
        WriteAuthoritativeState(writer);
        return writer.ToArray();
    }

    /// <summary>Reconstructs a world from a snapshot. Requires the same config and registry the snapshot was taken with.</summary>
    public static SimWorld Restore(
        byte[] snapshot,
        SimConfig config,
        ModuleRegistry registry,
        PassabilityGrid? passabilityGrid = null)
    {
        var world = new SimWorld(config, registry, tickMilliseconds: 33, passabilityGrid);
        var reader = new CanonicalReader(snapshot);
        world.TickIndex = reader.ReadInt();
        world._nextObjectId = reader.ReadInt();
        var teamCount = reader.ReadInt();
        if (teamCount != config.TeamCount)
        {
            throw new InvalidDataException($"Snapshot has {teamCount} teams, config has {config.TeamCount}");
        }
        for (var i = 0; i < teamCount; i++)
        {
            world._teamResources[i] = reader.ReadLong();
        }
        var randomState = unchecked((ulong)reader.ReadLong());
        var randomIncrement = unchecked((ulong)reader.ReadLong());
        world._random = DeterministicRandom.Deserialize(randomState, randomIncrement);
        var objectCount = reader.ReadInt();
        for (var i = 0; i < objectCount; i++)
        {
            world.ReadObject(reader);
        }
        var pendingTickCount = reader.ReadInt();
        for (var i = 0; i < pendingTickCount; i++)
        {
            var tick = reader.ReadInt();
            var commandCount = reader.ReadInt();
            var commands = new List<SimCommand>(commandCount);
            for (var j = 0; j < commandCount; j++)
            {
                commands.Add(SimCommand.ReadFrom(reader));
            }
            world._pendingCommands.Add(tick, commands);
        }
        while (reader.HasRemaining) world.ReadStateExtension(reader);
        reader.ExpectEnd();
        world.RebuildAuraTable(); // derived state: not in the snapshot, rebuilt from module caches
        return world;
    }

    private void ReadObject(CanonicalReader reader)
    {
        var id = reader.ReadInt();
        var templateName = reader.ReadString();
        var team = reader.ReadInt();
        var position = reader.ReadVector();
        var elevation = Fixed64.FromRaw(reader.ReadLong());
        var headingRadians = Fixed64.FromRaw(reader.ReadLong());
        var isDead = reader.ReadBool();
        var isDying = reader.ReadBool();
        var isUnderConstruction = reader.ReadBool();
        if (!_config.Templates.TryGetValue(templateName, out var template))
        {
            throw new InvalidDataException($"Snapshot references unknown template '{templateName}'");
        }
        var modules = new List<ModuleBase>(template.Modules.Count);
        foreach (var spec in template.Modules)
        {
            if (_registry.TryCreate(spec, out var module))
            {
                modules.Add(module!);
            }
            else if (spec.Tier == ModuleTier.Structural)
            {
                throw new ModuleLoadException(
                    $"Snapshot template '{templateName}' requires unknown structural module '{spec.TypeName}'");
            }
            else
            {
                _moduleGaps[spec.TypeName] = _moduleGaps.TryGetValue(spec.TypeName, out var count) ? count + 1 : 1;
            }
        }
        var gameObject = new GameObject(
            id, template, team, position, modules, elevation, headingRadians,
            techEnabled: !_config.Tech.IsEmpty);
        gameObject.StoreSlot = ObjectStore.Allocate(id);
        gameObject.Combat?.Read(reader);
        foreach (var module in modules)
        {
            module.ReadState(reader);
        }
        gameObject.ReadTechState(reader);
        if (isDead)
        {
            gameObject.MarkDead();
        }
        if (isDying)
        {
            gameObject.MarkDying();
        }
        gameObject.SetUnderConstruction(isUnderConstruction);
        _objects.Add(id, gameObject);
        SynchronizeObject(gameObject);
    }

    internal void SynchronizeObjectStore()
    {
        foreach (var gameObject in _objects.Values)
        {
            SynchronizeObject(gameObject);
        }
    }

    private void SynchronizeObject(GameObject gameObject)
    {
        var slot = gameObject.StoreSlot;
        ObjectStore.Id[slot] = gameObject.Id;
        ObjectStore.TemplateIndex[slot] = _config.TemplateIndexOf(gameObject.TemplateName);
        ObjectStore.Owner[slot] = gameObject.Team;
        ObjectStore.X[slot] = gameObject.Position.X;
        ObjectStore.Y[slot] = gameObject.Elevation;
        ObjectStore.Z[slot] = gameObject.Position.Y;
        ObjectStore.Yaw[slot] = gameObject.HeadingRadians;
        var (health, maximumHealth) = ReadHealth(gameObject);
        ObjectStore.Health[slot] = health;
        ObjectStore.MaxHealth[slot] = maximumHealth;
        ObjectStore.Flags[slot] = (ObjectStore.Flags[slot] & ~4) | (gameObject.IsDying ? 4 : 0);
    }

    private static (Fixed64 Health, Fixed64 MaxHealth) ReadHealth(GameObject gameObject)
    {
        if (gameObject.Combat is { HasBody: true } combat)
        {
            return (combat.Health, combat.MaxHealth);
        }
        if (gameObject.FindModule<ActiveBodyModule>() is { } active)
        {
            return (Fixed64.FromInt64(active.Health), Fixed64.FromInt64(active.MaxHealth));
        }
        if (gameObject.FindModule<StructureBodyModule>() is { } structure)
        {
            return (Fixed64.FromInt64(structure.Health), Fixed64.FromInt64(structure.MaxHealth));
        }
        if (gameObject.FindModule<ImmortalBodyModule>() is { } immortal)
        {
            return (Fixed64.FromInt64(immortal.Health), Fixed64.FromInt64(immortal.MaxHealth));
        }
        if (gameObject.FindModule<HordeContainModule>() is { } horde)
        {
            return (
                Fixed64.FromInt64(horde.TotalHealth),
                Fixed64.FromInt64(checked(horde.MemberCount * horde.MemberMaxHealth)));
        }
        return (Fixed64.Zero, Fixed64.Zero);
    }
}

internal sealed class SnapshotHordeIdComparer : IComparer<SnapshotHorde>
{
    public static SnapshotHordeIdComparer Instance { get; } = new();

    public int Compare(SnapshotHorde? left, SnapshotHorde? right) =>
        (left ?? throw new ArgumentNullException(nameof(left))).Id.CompareTo(
            (right ?? throw new ArgumentNullException(nameof(right))).Id);
}

public sealed class ModuleLoadException : Exception
{
    public ModuleLoadException(string message) : base(message)
    {
    }
}
