namespace OpenBfme.Sim;

/// <summary>
/// Retail horde BannerCarriersAllowed + BannerCarrierMinLevel: when the horde's
/// ExperienceLevel reaches MinLevel (default 2), spawn the first allowed banner
/// object at the authored exact fixed-point Pos X/Y offset. If that object
/// dies, the horde is destroyed only when retail authored the corresponding
/// HordeContain flag. Replacement is likewise enabled only when the banner's
/// retail BannerCarrierUpdate authored a respawn timer.
/// Design: MinLevel (default 2), OffsetXRaw, OffsetYRaw,
/// DestroyHordeOnBannerDeath, optional RespawnTicks, string BannerTemplate.
/// </summary>
[SageModule("BannerCarrierUpdate", ModuleTier.Structural)]
public sealed class BannerCarrierModule : ModuleBase
{
    public const string TypeName = "BannerCarrierUpdate";

    private readonly int _minLevel;
    private readonly Fixed64 _offsetX;
    private readonly Fixed64 _offsetY;
    private readonly string _bannerTemplate;
    private readonly bool _destroyHordeOnBannerDeath;
    private readonly bool _hasAuthoredRespawn;
    private readonly int _respawnTicks;
    private bool _spawned;
    private int _bannerObjectId;
    private int _respawnTicksRemaining = -1;

    public BannerCarrierModule(ModuleSpec spec) : base(spec)
    {
        _minLevel = (int)Math.Max(0, spec.GetLong("MinLevel", 2));
        // The loader converts authored decimal JSON directly to Q32.32. Do not
        // quantize through invented milli-units: source coordinates are not
        // contractually limited to three decimal places.
        _offsetX = Fixed64.FromRaw(spec.GetLong("OffsetXRaw", 0));
        _offsetY = Fixed64.FromRaw(spec.GetLong("OffsetYRaw", 0));
        _bannerTemplate = spec.GetString("BannerTemplate", "");
        _destroyHordeOnBannerDeath = spec.GetLong("DestroyHordeOnBannerDeath", 0) != 0;
        _hasAuthoredRespawn = spec.Data.ContainsKey("RespawnTicks");
        _respawnTicks = (int)Math.Clamp(spec.GetLong("RespawnTicks", 0), 0, int.MaxValue);
        if (string.IsNullOrEmpty(_bannerTemplate))
        {
            throw new ArgumentException("BannerCarrierUpdate requires BannerTemplate string data");
        }
    }

    public bool HasSpawned => _spawned;
    public int BannerObjectId => _bannerObjectId;
    public bool HasLivingBanner => _bannerObjectId != 0;
    public int RespawnTicksRemaining => _respawnTicksRemaining;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsDead || self.IsDying)
        {
            return;
        }
        if (_bannerObjectId != 0)
        {
            if (world.Objects.TryGetValue(_bannerObjectId, out var existing)
                && !existing.IsDead && !existing.IsDying)
            {
                return;
            }
            _bannerObjectId = 0;
            if (_destroyHordeOnBannerDeath)
            {
                world.HandleDeath(self);
                return;
            }
            if (!_hasAuthoredRespawn)
            {
                return;
            }
            _respawnTicksRemaining = _respawnTicks;
        }
        if (_spawned)
        {
            if (!_hasAuthoredRespawn || _respawnTicksRemaining < 0)
            {
                return;
            }
            if (_respawnTicksRemaining > 0)
            {
                _respawnTicksRemaining--;
                if (_respawnTicksRemaining > 0)
                {
                    return;
                }
            }
        }
        var level = self.FindModule<ExperienceLevelModule>()?.Level ?? 1;
        if (level < _minLevel)
        {
            return;
        }
        var position = new FixedVector2(
            self.Position.X + _offsetX,
            self.Position.Y + _offsetY);
        var banner = world.SpawnObject(_bannerTemplate, self.Team, position);
        _bannerObjectId = banner.Id;
        _spawned = true;
        _respawnTicksRemaining = -1;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_spawned);
        writer.WriteInt(_bannerObjectId);
        writer.WriteInt(_respawnTicksRemaining);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _spawned = reader.ReadBool();
        _bannerObjectId = reader.ReadInt();
        _respawnTicksRemaining = reader.ReadInt();
    }
}
