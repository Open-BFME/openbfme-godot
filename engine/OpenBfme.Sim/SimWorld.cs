namespace OpenBfme.Sim;

/// <summary>Immutable per-match configuration: templates, seed, teams. Not part of the state hash.</summary>
public sealed class SimConfig
{
    public IReadOnlyDictionary<string, ObjectTemplate> Templates { get; }
    public ulong RandomSeed { get; }
    public int TeamCount { get; }
    private readonly IReadOnlyDictionary<string, int> _templateIndices;

    public SimConfig(IEnumerable<ObjectTemplate> templates, ulong randomSeed, int teamCount)
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
        Templates = map;
        var indices = new SortedDictionary<string, int>(StringComparer.Ordinal);
        var index = 0;
        foreach (var name in map.Keys)
        {
            indices.Add(name, index++);
        }
        _templateIndices = indices;
        RandomSeed = randomSeed;
        TeamCount = teamCount;
    }

    public int TemplateIndexOf(string templateName) => _templateIndices[templateName];
}

/// <summary>
/// The deterministic simulation world: fixed integer ticks, command-queue
/// mutation only, canonical hash + snapshot. This is the P0 kernel the module
/// vocabulary grows into.
/// </summary>
public sealed class SimWorld
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
    /// <summary>Module type names that had no registered implementation, with occurrence counts. Fail-closed accounting.</summary>
    public IReadOnlyDictionary<string, int> ModuleGaps => _moduleGaps;

    public SimWorld(SimConfig config, ModuleRegistry registry)
        : this(config, registry, tickMilliseconds: 33)
    {
    }

    private SimWorld(SimConfig config, ModuleRegistry registry, int tickMilliseconds)
    {
        _config = config ?? throw new ArgumentNullException(nameof(config));
        _registry = registry ?? throw new ArgumentNullException(nameof(registry));
        if (tickMilliseconds < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(tickMilliseconds));
        }
        TickMilliseconds = tickMilliseconds;
        _teamResources = new long[config.TeamCount];
        _playerTeams = Enumerable.Range(0, config.TeamCount).ToArray();
        _commandPoints = new long[config.TeamCount];
        _commandPointsMax = new long[config.TeamCount];
        _powerPoints = new long[config.TeamCount];
        _random = new DeterministicRandom(config.RandomSeed);
    }

    public SimWorld(
        MatchLaunch launch,
        IEnumerable<ObjectTemplate>? templates = null,
        ModuleRegistry? registry = null)
        : this(
            CreateConfig(launch, templates),
            registry ?? ModuleRegistry.CreateDefault(),
            launch?.Rules.TickMilliseconds ?? throw new ArgumentNullException(nameof(launch)))
    {
        _playerTeams = launch.Players.Select(player => player.Team).ToArray();
        _commandPoints = new long[launch.Players.Count];
        _commandPointsMax = new long[launch.Players.Count];
        _powerPoints = new long[launch.Players.Count];
        for (var team = 0; team < _teamResources.Length; team++)
        {
            if (_playerTeams.Contains(team))
            {
                _teamResources[team] = launch.Rules.StartingResources;
            }
        }
    }

    private static SimConfig CreateConfig(MatchLaunch launch, IEnumerable<ObjectTemplate>? templates)
    {
        ArgumentNullException.ThrowIfNull(launch);
        var teamCount = launch.Players.Max(player => player.Team) + 1;
        return new SimConfig(templates ?? Array.Empty<ObjectTemplate>(), launch.Seed, teamCount);
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
                _commandPoints[index],
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
        if (_hordes.Count > 0 && horde.Id <= _hordes[^1].Id)
        {
            throw new ArgumentException("Hordes must be added in ascending id order", nameof(horde));
        }
        _hordes.Add(horde with { Members = horde.Members.ToArray() });
    }

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
        ValidateTeam(team);
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
            _nextObjectId++, templateName, team, position, modules, elevation, headingRadians);
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
        RaiseEvent(new SimEvent("spawn", gameObject.Id));
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
        TickIndex++;
        ApplyPendingCommands();
        // The object dictionary is frozen for the whole sweep: mid-sweep spawns
        // divert to _pendingSpawns (so modules scanning Objects never see it
        // mutate) and join afterwards, first updating next tick. Dead objects
        // never update.
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
                    module.OnUpdate(this, gameObject);
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
        SynchronizeObjectStore();
    }

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
        commands.Sort(static (a, b) =>
        {
            var byTeam = a.Team.CompareTo(b.Team);
            return byTeam != 0 ? byTeam : a.Seq.CompareTo(b.Seq);
        });
        foreach (var command in commands)
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
                if (_objects.TryGetValue((int)command.GetLong("id"), out var mover) && mover.Team == command.Team)
                {
                    mover.FindModule<LinearMoverModule>()?.SetDestination(
                        new FixedVector2(command.GetFixed("x"), command.GetFixed("y")));
                }
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
        foreach (var id in deadIds)
        {
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
    }

    public string StateHash()
    {
        SynchronizeObjectStore();
        var writer = new CanonicalWriter();
        WriteAuthoritativeState(writer);
        return writer.ToSha256Hex();
    }

    public byte[] Snapshot()
    {
        SynchronizeObjectStore();
        var writer = new CanonicalWriter();
        WriteAuthoritativeState(writer);
        return writer.ToArray();
    }

    /// <summary>Reconstructs a world from a snapshot. Requires the same config and registry the snapshot was taken with.</summary>
    public static SimWorld Restore(byte[] snapshot, SimConfig config, ModuleRegistry registry)
    {
        var world = new SimWorld(config, registry);
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
            id, templateName, team, position, modules, elevation, headingRadians);
        gameObject.StoreSlot = ObjectStore.Allocate(id);
        foreach (var module in modules)
        {
            module.ReadState(reader);
        }
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
        ObjectStore.Y[slot] = gameObject.Position.Y;
        ObjectStore.Z[slot] = gameObject.Elevation;
        ObjectStore.Yaw[slot] = gameObject.HeadingRadians;
        var (health, maximumHealth) = ReadHealth(gameObject);
        ObjectStore.Health[slot] = health;
        ObjectStore.MaxHealth[slot] = maximumHealth;
        ObjectStore.Flags[slot] = (ObjectStore.Flags[slot] & ~4) | (gameObject.IsDying ? 4 : 0);
    }

    private static (Fixed64 Health, Fixed64 MaxHealth) ReadHealth(GameObject gameObject)
    {
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

public sealed class ModuleLoadException : Exception
{
    public ModuleLoadException(string message) : base(message)
    {
    }
}
