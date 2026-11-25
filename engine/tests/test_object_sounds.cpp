// OpenBFME unit tests (lane OBJSOUND-1, stop S-707): the object sounds of a damage state change (ActiveBody::attemptDamage RW 0x8C4422 .. 0x8C44C2:
// SoundOnDamaged / SoundOnReallyDamaged) on synthetic data, the retail templates' rows against the retail audio table, and the binary facts the port
// relies on (RW_GAME_DAT), among them that the caller found for SoundPromotedVeteran / Elite / Hero is unreferenced. Lane OBJSOUND-2: SoundCrushing (RW 0x69324A),
// SoundImpact / SoundImpactCyclonic (RW 0x696974 / 0x696962) and SoundCreated (RW 0x8A2970).

#include "doctest.h"

#include "CombatTestUtil.h"
#include "HudTestUtil.h"
#include "IniTestUtil.h"
#include "ProdTestUtil.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "GameClient/SoundSelectorClientBehavior.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Construction.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/ObjectSounds.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace combattest;
using retailtest::PeImage;

namespace
{
std::string silentWav(double ms)
{
	const std::uint32_t rate = 22050, frames = (std::uint32_t)(ms * rate / 1000.0);
	std::string out;
	auto u32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			out.push_back((char)(v >> (8 * i)));
		}
	};
	auto u16 = [&](std::uint16_t v) {
		out.push_back((char)v);
		out.push_back((char)(v >> 8));
	};
	out += "RIFF";
	u32(36 + frames * 2);
	out += "WAVEfmt ";
	u32(16);
	u16(1);
	u16(1);
	u32(rate);
	u32(rate * 2);
	u16(2);
	u16(16);
	out += "data";
	u32(frames * 2);
	out.append(frames * 2, '\0');
	return out;
}

const char kAudio[] =
	"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n  SoundsExtension = wav\n"
	"  SampleCount2D = 4\n  SampleCount3D = 4\n  StreamCount = 3\n  MinSampleVolume = 2%\n  GlobalMinRange = 5000\n  GlobalMaxRange = 5000000\n  TimeToFadeAudio = 300\n"
	"  AmbientStreamHysteresisVolume = 10\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n  DefaultMusicVolume = 100%\n  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 4\n  MinRange = 160\n  MaxRange = 640\n  PitchShift = 0 0\n  PlayPercent = 100\n  Type = world everyone\n  Type = +DEFAULT\nEnd\n"
	"MusicTrack DefaultMusicTrack\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  SubmixSlider = music\nEnd\n"
	"DialogEvent DefaultDialog\n  Volume = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"AmbientStream DefaultAmbientStream\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  MinRange = 100\n  MaxRange = 1000\n  SubmixSlider = ambient\nEnd\n"
	"AudioEvent ProbeHurt\n  Sounds = probe\n  Limit = 0\nEnd\n"
	"AudioEvent ProbeDying\n  Sounds = probe\n  Limit = 0\nEnd\n"
	"AudioEvent ProbeVeteran\n  Sounds = probe\n  Limit = 0\nEnd\n"
	"AudioEvent ProbeCrush\n  Sounds = probe\n  Limit = 0\nEnd\n"
	"AudioEvent ProbeHordeCrush\n  Sounds = probe\n  Limit = 0\nEnd\n"
	"AudioEvent ProbeImpact\n  Sounds = probe\n  Limit = 0\nEnd\n"
	"AudioEvent ProbeCyclone\n  Sounds = probe\n  Limit = 0\nEnd\n"
	"AudioEvent ProbeCreated\n  Sounds = probe\n  Limit = 0\nEnd\n";

// the shapes of the retail rows: a plain event, the +SOUND: form (RW 0x73AC14), NoSound, a template without the rows
const char kProbes[] =
	"Object SoundProbe\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 75\n    MaxHealthReallyDamaged = 50\n  End\n"
	"  SoundOnDamaged = ProbeHurt\n"
	"  SoundOnReallyDamaged = +SOUND:ProbeDying\n"
	"  SoundPromotedVeteran = ProbeVeteran\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 10\n  GeometryHeight = 10\n"
	"End\n"
	"Object SilentProbe\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 75\n    MaxHealthReallyDamaged = 50\n  End\n"
	"  SoundOnDamaged = NoSound\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 10\n  GeometryHeight = 10\n"
	"End\n";

// lane OBJSOUND-2: a crusher, a horde of crushers with a row of its own, a victim; things a shock wave throws (one of them shockwave-proof, one without the rows)
const char kCrushProbes[] =
	"Object CrushRider\n"
	"  KindOf = CAVALRY SELECTABLE\n"
	"  SoundCrushing = ProbeCrush\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n  GeometryHeight = 10\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n"
	"  LocomotorSet\n    Locomotor = WalkerLoco\n    Condition = SET_NORMAL\n    Speed = 50\n  End\n"
	"End\n"
	"Object CrushHorde\n"
	"  KindOf = HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  SoundCrushing = ProbeHordeCrush\n"
	"  Geometry = BOX\n  GeometryMajorRadius = 30\n  GeometryMinorRadius = 45\n  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n    MaxHealth = 1\n  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:CrushRider Position:X:50 Y:0 Position:X:50 Y:20\n"
	"    InitialPayload = CrushRider 2\n"
	"  End\n"
	"  LocomotorSet\n    Locomotor = HordeLoco\n    Condition = SET_NORMAL\n    Speed = 50\n  End\n"
	"End\n";
const char kShockProbes[] =
	"Object ThrownProbe\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  SoundImpact = ProbeImpact\n"
	"  SoundImpactCyclonic = ProbeCyclone\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n  GeometryHeight = 10\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n  End\n"
	"End\n"
	"Object ProofProbe\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  ShockwaveResistance = 100\n"
	"  SoundImpact = ProbeImpact\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n  GeometryHeight = 10\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n  End\n"
	"End\n"
	"Object NoPhysicsProbe\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  SoundImpact = ProbeImpact\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n  GeometryHeight = 10\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"End\n";

const char kSelectorProbes[] =
	"Object SelectorProbe\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  SoundImpact = ProbeImpact\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n  GeometryHeight = 10\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  ClientBehavior = ModelConditionSoundSelectorClientBehavior ModuleTag_Sounds\n"
	"    SoundState = MOUNTED\n"
	"      SoundImpact = ProbeCyclone\n"
	"      SoundCrushing = ProbeCrush\n"
	"    End\n"
	"  End\n"
	"End\n";

// an installed AudioManager whose request log shows what the logic asked for
struct SoundRig
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	AudioManager *saved = nullptr;
	SoundRig()
	{
		fx.mount({ { "data\\audio\\sounds\\probe.wav", silentWav(300) } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini", kAudio);
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 16u << 20);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 7);
		mgr->update(0.0);
		mgr->enableEventLog(true);
		saved = AudioApi::current();
		AudioApi::install(mgr.get());
	}
	~SoundRig() { AudioApi::install(saved); }
	// the request lines (play == false) since the last call
	std::vector<AudioLogEntry> requests()
	{
		std::vector<AudioLogEntry> out;
		for (AudioLogEntry &e : mgr->takeEventLog())
		{
			if (!e.play && !e.volume)
			{
				out.push_back(std::move(e));
			}
		}
		return out;
	}
};

DamageInfo hit(float amount, int type)
{
	DamageInfo d;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	return d;
}

// a 0-damage hit that carries a shock wave (RamPower's shape, RW 0x8BFF08)
DamageInfo shock(float amount, float radius, float cyclonicFactor)
{
	DamageInfo d = hit(0.0f, DAMAGE_CRUSH);
	d.m_input.m_shockWaveVector = Coord3D{ 1.0f, 0.0f, 0.0f };
	d.m_input.m_shockWaveAmount = amount;
	d.m_input.m_shockWaveRadius = radius;
	d.m_input.m_shockWaveTaperOff = 1.0f;
	d.m_input.m_shockWaveZMult = 1.0f;
	d.m_input.m_cyclonicFactor = cyclonicFactor;
	return d;
}

size_t countEvent(const std::vector<AudioLogEntry> &requests, const char *event)
{
	size_t n = 0;
	for (const AudioLogEntry &e : requests)
	{
		n += e.event == event ? 1 : 0;
	}
	return n;
}
} // namespace

TEST_CASE("object sounds: damageStateSound reads SoundOnDamaged / SoundOnReallyDamaged by the new state, NoSound and +SOUND: as RW 0x73AB45 left them")
{
	CombatWorld w(kProbes);
	const ThingTemplate &probe = *w.w.get("SoundProbe");
	CHECK(ActiveBody::damageStateSound(probe, BODY_PRISTINE).empty());
	CHECK(ActiveBody::damageStateSound(probe, BODY_DAMAGED) == "ProbeHurt");
	CHECK(ActiveBody::damageStateSound(probe, BODY_REALLYDAMAGED) == "ProbeDying"); // the +SOUND: prefix is dropped (RW 0x73AC14)
	CHECK(ActiveBody::damageStateSound(probe, BODY_RUBBLE).empty());               // RW 0x8C4460: only states 1 and 2 have a row
	// the retail case counts SoundPromoted* rows through findField and finds none: a set row is found that way
	const FieldValue *promotedRow = probe.findField("SoundPromotedVeteran");
	REQUIRE(promotedRow != nullptr);
	REQUIRE(std::get_if<RawTokens>(promotedRow) != nullptr);
	CHECK(std::get_if<RawTokens>(promotedRow)->tokens.front() == "ProbeVeteran");
	const ThingTemplate &silent = *w.w.get("SilentProbe");
	CHECK(ActiveBody::damageStateSound(silent, BODY_DAMAGED).empty());       // NoSound
	CHECK(ActiveBody::damageStateSound(silent, BODY_REALLYDAMAGED).empty()); // never set
}

TEST_CASE("object sounds: a hit that changes the damage state plays the new state's sound at the object for its owner (RW 0x8C4422 .. 0x8C44C2)")
{
	SoundRig r;
	CombatWorld w(kProbes);
	Object *o = w.unit("SoundProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	r.requests(); // whatever the spawn asked for
	const int bob = w.playerOf('B')->getPlayerIndex();

	DamageInfo scratch = hit(10.0f, DAMAGE_SLASH); // 100 -> 90: still PRISTINE
	o->attemptDamage(scratch);
	CHECK(r.requests().empty());

	DamageInfo first = hit(20.0f, DAMAGE_SLASH); // 90 -> 70: DAMAGED
	o->attemptDamage(first);
	REQUIRE(o->getBodyModule()->getDamageState() == BODY_DAMAGED);
	std::vector<AudioLogEntry> req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeHurt");
	CHECK(req[0].objectId == o->getID()); // attached to the object (setObjectID RW 0x6DA6A7)
	CHECK(req[0].player == bob);          // the object's controlling player (RW 0x6DB566)

	DamageInfo again = hit(5.0f, DAMAGE_SLASH); // 70 -> 65: DAMAGED stays, no sound
	o->attemptDamage(again);
	CHECK(r.requests().empty());

	DamageInfo second = hit(30.0f, DAMAGE_SLASH); // 65 -> 35: REALLYDAMAGED
	o->attemptDamage(second);
	req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeDying");
	CHECK(req[0].objectId == o->getID());

	// healing back to DAMAGED and PRISTINE plays nothing: attemptHealing (RW 0x8C3131) has no such call
	DamageInfo heal = hit(60.0f, DAMAGE_HEALING);
	o->attemptDamage(heal);
	CHECK(o->getBodyModule()->getDamageState() == BODY_PRISTINE);
	CHECK(r.requests().empty());
}

TEST_CASE("object sounds: a hit straight from PRISTINE to REALLYDAMAGED plays only SoundOnReallyDamaged; a state change outside attemptDamage plays nothing")
{
	SoundRig r;
	CombatWorld w(kProbes);
	Object *o = w.unit("SoundProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	r.requests();
	DamageInfo big = hit(60.0f, DAMAGE_SLASH); // 100 -> 40
	o->attemptDamage(big);
	REQUIRE(o->getBodyModule()->getDamageState() == BODY_REALLYDAMAGED);
	std::vector<AudioLogEntry> req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeDying");

	Object *p = w.unit("SoundProbe", 'A', 500, 300);
	REQUIRE(p != nullptr);
	r.requests();
	ActiveBody *body = dynamic_cast<ActiveBody *>(p->getBodyModule());
	REQUIRE(body != nullptr);
	body->internalChangeHealth(-60.0f); // construction / repair / scripts: setCorrectDamageState (RW 0x8C2A5D) alone
	CHECK(body->getDamageState() == BODY_REALLYDAMAGED);
	CHECK(r.requests().empty());

	Object *s = w.unit("SilentProbe", 'A', 600, 300);
	REQUIRE(s != nullptr);
	r.requests();
	DamageInfo cut = hit(30.0f, DAMAGE_SLASH); // DAMAGED with SoundOnDamaged = NoSound
	s->attemptDamage(cut);
	CHECK(s->getBodyModule()->getDamageState() == BODY_DAMAGED);
	CHECK(r.requests().empty());
}

TEST_CASE("object sounds: a crush plays the crusher's SoundCrushing at the crusher, a horde member's own and then its horde's (RW 0x693231 .. 0x6932B8)")
{
	SoundRig r;
	CombatWorld w(kCrushProbes);
	REQUIRE(w.w.load(kProbes, INI_LOAD_OVERWRITE, "probes.ini").empty());
	Object *rider = w.unit("CrushRider", 'A', 200, 300);
	Object *victim = w.unit("SilentProbe", 'B', 300, 300);
	REQUIRE(rider != nullptr);
	REQUIRE(victim != nullptr);
	const int alice = w.playerOf('A')->getPlayerIndex();
	r.requests();
	ObjectCrush::onCrush(*rider, *victim);
	std::vector<AudioLogEntry> req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeCrush");
	CHECK(req[0].objectId == rider->getID());
	CHECK(req[0].player == alice);

	// a crusher without the row is silent
	ObjectCrush::onCrush(*victim, *rider);
	CHECK(r.requests().empty());

	Object *horde = w.unit("CrushHorde", 'A', 600, 300);
	REQUIRE(horde != nullptr);
	w.frames(2);
	Object *member = nullptr;
	for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (!member && o->getContainedBy() == horde)
		{
			member = o;
		}
	}
	REQUIRE(member != nullptr);
	r.requests();
	ObjectCrush::onCrush(*member, *victim);
	req = r.requests();
	REQUIRE(req.size() == 2);
	CHECK(req[0].event == "ProbeCrush");
	CHECK(req[0].objectId == member->getID());
	CHECK(req[1].event == "ProbeHordeCrush");
	CHECK(req[1].objectId == horde->getID());
	CHECK(req[1].player == alice);

	// RW 0x693222: a dead victim plays nothing
	DamageInfo kill = hit(1000.0f, DAMAGE_SLASH);
	victim->attemptDamage(kill);
	REQUIRE(victim->isEffectivelyDead());
	r.requests();
	ObjectCrush::onCrush(*rider, *victim);
	CHECK(r.requests().empty());
}

TEST_CASE("object sounds: a shock wave hit plays SoundImpact, a cyclonic one SoundImpactCyclonic, unless the object is stunned (RW 0x696947 .. 0x6969F1)")
{
	SoundRig r;
	CombatWorld w(kShockProbes);
	Object *o = w.unit("ThrownProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	PhysicsBehavior *phys = PhysicsBehavior::find(*o);
	REQUIRE(phys != nullptr);
	const int bob = w.playerOf('B')->getPlayerIndex();
	r.requests();

	DamageInfo none = shock(0.0f, 10.0f, 0.0f); // RW 0x6968F3: no amount
	ObjectKnockback::shockWave(*o, none);
	DamageInfo noRadius = shock(10.0f, 0.0f, 0.0f); // RW 0x696901: no radius
	ObjectKnockback::shockWave(*o, noRadius);
	CHECK(r.requests().empty());

	DamageInfo first = shock(10.0f, 10.0f, 0.0f);
	ObjectKnockback::shockWave(*o, first);
	std::vector<AudioLogEntry> req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeImpact");
	CHECK(req[0].objectId == o->getID());
	CHECK(req[0].player == bob);
	REQUIRE(phys->isStunned()); // the hit stunned it

	DamageInfo second = shock(10.0f, 10.0f, 0.0f); // RW 0x69693D: a stunned object takes no plain shock wave
	ObjectKnockback::shockWave(*o, second);
	DamageInfo third = shock(10.0f, 10.0f, 1.0f); // RW 0x696951: a cyclonic one throws it, silently
	ObjectKnockback::shockWave(*o, third);
	CHECK(r.requests().empty());

	Object *c = w.unit("ThrownProbe", 'A', 600, 300);
	REQUIRE(c != nullptr);
	r.requests();
	DamageInfo cyclone = shock(10.0f, 10.0f, 1.0f);
	ObjectKnockback::shockWave(*c, cyclone);
	req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeCyclone");
	CHECK(req[0].objectId == c->getID());

	// the sound comes before the resistance test (RW 0x696A18): a shockwave-proof object plays it and stays
	Object *proof = w.unit("ProofProbe", 'A', 700, 300);
	REQUIRE(proof != nullptr);
	r.requests();
	DamageInfo resisted = shock(10.0f, 10.0f, 0.0f);
	ObjectKnockback::shockWave(*proof, resisted);
	CHECK(countEvent(r.requests(), "ProbeImpact") == 1);
	CHECK_FALSE(PhysicsBehavior::find(*proof)->isStunned());

	Object *still = w.unit("NoPhysicsProbe", 'A', 800, 300); // RW 0x696933: nothing without a PhysicsBehavior
	REQUIRE(still != nullptr);
	r.requests();
	DamageInfo ignored = shock(10.0f, 10.0f, 0.0f);
	ObjectKnockback::shockWave(*still, ignored);
	CHECK(r.requests().empty());
}

TEST_CASE("object sounds: the row is the sound selectors' before the template's (RW 0x676A8A): a model condition's")
{
	SoundRig r;
	struct HookScope // the selector rows are checked against the audio table at parse
	{
		std::function<bool(const std::string &)> saved = TheContainParseHooks().audioEventExists;
		HookScope()
		{
			TheContainParseHooks().audioEventExists = [](const std::string &) { return true; };
		}
		~HookScope() { TheContainParseHooks().audioEventExists = saved; }
	} hook;
	CombatWorld w;
	SoundSelectorClientBehavior::registerClasses(w.w.modules);
	const std::string err = w.w.load(kSelectorProbes, INI_LOAD_OVERWRITE, "selectors.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	Object *o = w.unit("SelectorProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	CHECK(ObjectSounds::objectRow(*o, "SoundImpact") == "ProbeImpact"); // the template's
	CHECK(ObjectSounds::objectRow(*o, "SoundCrushing").empty());        // set nowhere
	const int mounted = Construction::modelConditionIndex("MOUNTED");
	o->setModelConditionState(mounted, true);
	CHECK(ObjectSounds::objectRow(*o, "SoundImpact") == "ProbeCyclone"); // the SoundState's
	CHECK(ObjectSounds::objectRow(*o, "SoundCrushing") == "ProbeCrush");
	r.requests();
	ObjectSounds::playRow(*o, "SoundImpact");
	std::vector<AudioLogEntry> req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeCyclone");
	CHECK(req[0].objectId == o->getID());
	o->setModelConditionState(mounted, false);
	CHECK(ObjectSounds::objectRow(*o, "SoundImpact") == "ProbeImpact");
}

TEST_CASE("object sounds: a produced unit plays SoundCreated at itself for the producer's player (RW 0x8A2970 .. 0x8A29D6)")
{
	SoundRig r;
	prodtest::ProdWorld f;
	std::string objects = prodtest::kBarracksObjects;
	const std::string soldierKind = "  CommandPoints = 4\n";
	const size_t at = objects.find(soldierKind);
	REQUIRE(at != std::string::npos);
	objects.insert(at, "  SoundCreated = ProbeCreated\n");
	f.load(objects);
	f.load(prodtest::kBarracksCommands);
	Object *barracks = f.make("Barracks", f.teamOf("Alice"));
	REQUIRE(barracks != nullptr);
	const Coord3D pos{ 1000.0f, 2000.0f, 0.0f };
	barracks->setPosition(&pos);
	ProductionUpdateInterface *pu = f.pu(barracks);
	REQUIRE(pu != nullptr);
	REQUIRE(pu->queueCreateUnit(f.w.get("Soldier"), -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	r.requests();
	std::vector<AudioLogEntry> created;
	const Object *soldier = nullptr;
	for (int i = 0; i < 80; ++i)
	{
		f.frames(1);
		for (AudioLogEntry &e : r.requests())
		{
			if (e.event == "ProbeCreated")
			{
				created.push_back(std::move(e));
			}
		}
	}
	for (const Object *o = f.logic->getFirstObject(); o; o = o->getNextObject())
	{
		soldier = o->getTemplate()->getName() == "Soldier" ? o : soldier;
	}
	REQUIRE(soldier != nullptr);
	REQUIRE(created.size() == 1);
	CHECK(created[0].objectId == soldier->getID());
	CHECK(created[0].player == f.alice()->getPlayerIndex());
	// an object made any other way plays none: ProductionUpdate is the row's one reader (RW 0x8A0885's one caller)
	REQUIRE(f.make("Soldier", f.teamOf("Alice")) != nullptr);
	CHECK(countEvent(r.requests(), "ProbeCreated") == 0);
}

TEST_CASE("object sounds retail: the crush, impact and created rows name retail audio events; the rows that stay unported are set by no template (S-707)")
{
	if (!hudtest::haveWorld("object sounds rows retail"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	const AudioIniState &audio = s.world->audio();
	const char *const ported[] = { "SoundCrushing", "SoundImpact", "SoundImpactCyclonic", "SoundCreated" };
	const char *const open[] = { "SoundEnter", "SoundExit", "SoundStealthOn", "SoundStealthOff", "SoundFallingFromPlane", "SoundAmbientBattle" };
	std::map<std::string, size_t> count;
	std::map<std::string, size_t> events; // row=event
	std::vector<std::string> unknown, createdBy;
	std::map<std::string, size_t> overrides; // template: row=event, the selector entries that set a ported row
	size_t randomOverrides = 0;
	std::vector<int> rowIndices;
	for (const char *row : ported)
	{
		rowIndices.push_back(SoundSelector::rowIndex(row));
	}
	CHECK(rowIndices == std::vector<int>{ 55, 53, 54, 44 });
	for (const ThingTemplate *tt : s.world->things().templates())
	{
		for (const char *row : ported)
		{
			const std::string e = ObjectSounds::templateRow(*tt, row);
			if (e.empty())
			{
				continue;
			}
			++count[row];
			++events[std::string(row) + "=" + e];
			if (!audio.infos.find(e))
			{
				unknown.push_back(tt->getName() + "=" + e);
			}
			if (std::string(row) == "SoundCreated")
			{
				createdBy.push_back(tt->getName());
			}
		}
		for (const char *row : open)
		{
			count[row] += ObjectSounds::templateRow(*tt, row).empty() ? 0 : 1;
		}
		for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->clientBehaviorModules().nuggets())
		{
			const ModuleData *data = n.data.get();
			auto sets = [&](const SoundSelector::SoundSet &set, bool random) {
				for (int row : rowIndices)
				{
					const std::string &e = set.rows[(size_t)row].sound;
					if (e.empty())
					{
						continue;
					}
					randomOverrides += random ? 1 : 0;
					++overrides[tt->getName() + ": " + SoundSelector::rowName(row) + "=" + e];
					if (!audio.infos.find(e))
					{
						unknown.push_back(tt->getName() + "=" + e);
					}
				}
			};
			if (const auto *mc = dynamic_cast<const ModelConditionSoundSelectorClientBehaviorModuleData *>(data))
			{
				for (const auto &e : mc->m_entries)
				{
					sets(e.sounds, false);
				}
			}
			else if (const auto *up = dynamic_cast<const UpgradeSoundSelectorClientBehaviorModuleData *>(data))
			{
				for (const auto &e : up->m_entries)
				{
					sets(e.sounds, false);
				}
			}
			else if (const auto *rnd = dynamic_cast<const RandomSoundSelectorClientBehaviorModuleData *>(data))
			{
				sets(rnd->m_sounds, true);
			}
		}
	}
	for (const auto &kv : overrides)
	{
		std::printf("  selector %s x%zu\n", kv.first.c_str(), kv.second);
	}
	for (const auto &kv : count)
	{
		std::printf("object sounds rows retail: %s %zu\n", kv.first.c_str(), kv.second);
	}
	for (const auto &kv : events)
	{
		std::printf("  %s %zu\n", kv.first.c_str(), kv.second);
	}
	for (const char *row : open)
	{
		INFO(row);
		CHECK(count[row] == 0);
	}
	CHECK(unknown.empty());
	CHECK(randomOverrides == 0); // ObjectSounds::objectRow does not ask the random selectors
	// Create-a-Hero names its SoundImpact in the selectors only (one SoundUpgrade per class and subclass)
	const std::map<std::string, size_t> expectedOverrides = { { "CreateAHero: SoundImpact=ImpactHorse", 17 }, { "CreateAHeroMounted: SoundImpact=ImpactHorse", 17 },
		{ "GondorFaramir: SoundImpact=ImpactHorse", 1 }, { "GondorFaramirCavalry: SoundImpact=ImpactHorse", 1 } };
	CHECK(overrides == expectedOverrides);
	CHECK(count["SoundImpactCyclonic"] == 0);
	CHECK(createdBy == std::vector<std::string>{ "EyeOfSauron" }); // made by SUPERWEAPON_SpawnEyeOfSauron, not by a ProductionUpdate
	CHECK(count["SoundCrushing"] == 16); // RotWK 2.01
	CHECK(count["SoundImpact"] == 404);
}

TEST_CASE("object sounds binary facts: the crush, impact and created rows' call sites (RW_GAME_DAT)")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("object sounds rows binary facts (RW_GAME_DAT unset)");
		return;
	}
	CHECK(pe->hex(0x8A0885, 14) == "558bec518365fc006a2cff7508e8"); // SoundCreated (44)
	CHECK(pe->hex(0x68FFBD, 14) == "558bec518365fc006a35ff7508e8"); // SoundImpact (53)
	CHECK(pe->hex(0x68FFD6, 14) == "558bec518365fc006a36ff7508e8"); // SoundImpactCyclonic (54)
	CHECK(pe->hex(0x68FFEF, 14) == "558bec518365fc006a37ff7508e8"); // SoundCrushing (55)
	// onCrush: the victim's dead bit; SoundCrushing; TheAudio vslot 0x64; the member's call on its horde comes after it
	CHECK(pe->hex(0x693222, 7) == "f6875804000001");
	CHECK(pe->hex(0x69324A, 5) == "e8a0cdffff"); // call RW 0x68FFEF
	CHECK(pe->hex(0x69328F, 3) == "ff5064");
	CHECK(pe->hex(0x6932B8, 5) == "e850ffffff"); // call RW 0x69320D
	// the shock wave handler: al = the PhysicsBehavior's + 0x5C (stunned) skips the sound; the cyclonic and the plain row; the resistance test after it
	CHECK(pe->hex(0x69692A, 3) == "8a415c");
	CHECK(pe->hex(0x69694F, 8) == "84c00f85bc000000");
	CHECK(pe->hex(0x696962, 5) == "e86f96ffff"); // call RW 0x68FFD6
	CHECK(pe->hex(0x696974, 5) == "e84496ffff"); // call RW 0x68FFBD
	CHECK(pe->hex(0x6969F1, 3) == "ff5064");
	CHECK(pe->hex(0x696A18, 5) == "e84d77ffff"); // call RW 0x68E16A
	// ProductionUpdate::update: module + 0x110 = the new object's id; SoundCreated; setObjectID(that); the producer's player's + 0x54 into the event's + 0x6C
	CHECK(pe->hex(0x8A2796, 6) == "898610010000");
	CHECK(pe->hex(0x8A2970, 5) == "e810dfffff"); // call RW 0x8A0885
	CHECK(pe->hex(0x8A29A0, 6) == "ffb610010000");
	CHECK(pe->hex(0x8A29B9, 3) == "ff7054");
	CHECK(pe->hex(0x8A29C2, 5) == "e81ac3dcff"); // call RW 0x66ECE1
	CHECK(pe->hex(0x66ECE1, 8) == "8b44240489416cc2");
	CHECK(pe->hex(0x8A29D6, 3) == "ff5064");
}

TEST_CASE("object sounds retail: the damage rows name retail audio events, and no sound selector overrides them (S-707)")
{
	if (!hudtest::haveWorld("object sounds retail"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	const AudioIniState &audio = s.world->audio();
	const int rows[] = { SoundSelector::rowIndex("SoundOnDamaged"), SoundSelector::rowIndex("SoundOnReallyDamaged"), SoundSelector::rowIndex("SoundPromotedVeteran"),
		SoundSelector::rowIndex("SoundPromotedElite"), SoundSelector::rowIndex("SoundPromotedHero") };
	REQUIRE(rows[0] == 45); // RW 0x8C22C0 pushes 0x2D
	REQUIRE(rows[1] == 46); // RW 0x8C22D9 pushes 0x2E
	REQUIRE(rows[2] == 49); // RW 0x8C22F2 pushes 0x31
	REQUIRE(rows[3] == 50); // RW 0x8C230B pushes 0x32
	REQUIRE(rows[4] == 51); // RW 0x8C2324 pushes 0x33
	size_t damaged = 0, reallyDamaged = 0, either = 0, promoted = 0, selectorOverrides = 0;
	std::vector<std::string> unknown;
	std::map<std::string, size_t> eventTypes; // the Type flags of the damage events (who may hear them: shouldPlayLocally RW 0x4533A9)
	std::map<std::string, size_t> eventNames;
	for (const ThingTemplate *tt : s.world->things().templates())
	{
		const std::string d = ActiveBody::damageStateSound(*tt, BODY_DAMAGED);
		const std::string rd = ActiveBody::damageStateSound(*tt, BODY_REALLYDAMAGED);
		damaged += d.empty() ? 0 : 1;
		reallyDamaged += rd.empty() ? 0 : 1;
		either += d.empty() && rd.empty() ? 0 : 1;
		for (const std::string &e : { d, rd })
		{
			if (e.empty())
			{
				continue;
			}
			const auto info = audio.infos.find(e);
			if (!info)
			{
				unknown.push_back(tt->getName() + "=" + e);
				continue;
			}
			char flags[16];
			std::snprintf(flags, sizeof flags, "0x%x", (unsigned)info->type);
			++eventTypes[flags];
			++eventNames[e];
		}
		for (const char *p : { "SoundPromotedVeteran", "SoundPromotedElite", "SoundPromotedHero" })
		{
			const FieldValue *f = tt->findField(p);
			const RawTokens *raw = f ? std::get_if<RawTokens>(f) : nullptr;
			promoted += raw && !raw->tokens.empty() && AsciiStringUtil::compareNoCase(raw->tokens.front(), "NoSound") != 0 ? 1 : 0;
		}
		// RW 0x676A8A asks the drawable's selectors first: any selector entry that sets one of these rows would win over the template
		for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->clientBehaviorModules().nuggets())
		{
			const ModuleData *data = n.data.get();
			auto sets = [&](const SoundSelector::SoundSet &set) {
				for (int row : rows)
				{
					selectorOverrides += set.rows[(size_t)row].sound.empty() ? 0 : 1;
				}
			};
			if (const auto *mc = dynamic_cast<const ModelConditionSoundSelectorClientBehaviorModuleData *>(data))
			{
				for (const auto &e : mc->m_entries)
				{
					sets(e.sounds);
				}
			}
			else if (const auto *up = dynamic_cast<const UpgradeSoundSelectorClientBehaviorModuleData *>(data))
			{
				for (const auto &e : up->m_entries)
				{
					sets(e.sounds);
				}
			}
			else if (const auto *rnd = dynamic_cast<const RandomSoundSelectorClientBehaviorModuleData *>(data))
			{
				sets(rnd->m_sounds);
			}
		}
	}
	std::printf("object sounds retail: %zu templates with SoundOnDamaged, %zu with SoundOnReallyDamaged (%zu either), %zu SoundPromoted* rows (not ported), "
				"%zu selector overrides, %zu unknown events\n",
		damaged, reallyDamaged, either, promoted, selectorOverrides, unknown.size());
	for (const auto &kv : eventTypes)
	{
		std::printf("  damage event Type %s: %zu\n", kv.first.c_str(), kv.second);
	}
	std::vector<std::pair<size_t, std::string>> common;
	for (const auto &kv : eventNames)
	{
		common.emplace_back(kv.second, kv.first);
	}
	std::sort(common.rbegin(), common.rend());
	std::printf("  %zu distinct damage events, the most used:", eventNames.size());
	for (size_t i = 0; i < common.size() && i < 6; ++i)
	{
		std::printf(" %s (%zu)", common[i].second.c_str(), common[i].first);
	}
	std::printf("\n");
	for (size_t i = 0; i < unknown.size() && i < 10; ++i)
	{
		std::printf("  unknown %s\n", unknown[i].c_str());
	}
	CHECK(damaged == 1062); // RotWK 2.01
	CHECK(reallyDamaged == 1065);
	CHECK(either == 1069);
	CHECK(promoted == 0);
	CHECK(unknown.empty());          // retail validates a sound row at parse time (RW 0x73AC45 "Invalid Sound")
	CHECK(selectorOverrides == 0);   // the template row is what plays (ActiveBody::damageStateSound; the hit asks the selectors first, ObjectSounds)
}

TEST_CASE("object sounds binary facts: the damage rows' call sites, and the caller found for SoundPromoted* is unreferenced (RW_GAME_DAT)")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("object sounds binary facts (RW_GAME_DAT unset)");
		return;
	}
	// the drawable's row getters: push ebp; mov ebp, esp; push ecx; and [ebp-4], 0; push <row>; push [ebp+8]; call RW 0x676A8A
	CHECK(pe->hex(0x8C22C0, 14) == "558bec518365fc006a2dff7508e8"); // SoundOnDamaged (45)
	CHECK(pe->hex(0x8C22D9, 14) == "558bec518365fc006a2eff7508e8"); // SoundOnReallyDamaged (46)
	CHECK(pe->hex(0x8C22F2, 14) == "558bec518365fc006a31ff7508e8"); // SoundPromotedVeteran (49)
	CHECK(pe->hex(0x8C230B, 14) == "558bec518365fc006a32ff7508e8"); // SoundPromotedElite (50)
	CHECK(pe->hex(0x8C2324, 14) == "558bec518365fc006a33ff7508e8"); // SoundPromotedHero (51)
	// attemptDamage: mov edx, [esi+0x20] (the new state); cmp edx, 1; jne -> SoundOnDamaged; cmp edx, 2 -> SoundOnReallyDamaged; TheAudio vslot 0x64
	CHECK(pe->hex(0x8C4436, 6) == "8b562083fa01");
	CHECK(pe->hex(0x8C4442, 5) == "e879deffff"); // call RW 0x8C22C0
	CHECK(pe->hex(0x8C4460, 3) == "83fa02");
	CHECK(pe->hex(0x8C4469, 5) == "e86bdeffff"); // call RW 0x8C22D9
	CHECK(pe->hex(0x8C44B5, 3) == "ff5064");
	// the promoted rows: ActiveBody's body interface slot 0x30 (RW 0x8C261D, the vtable at RW 0xC71ED0 whose slot 0x5C is setMaxHealth RW 0x8C1CD5)
	CHECK(pe->u32At(0xC71ED0 + 0x30) == 0x8C261D);
	CHECK(pe->u32At(0xC71ED0 + 0x5C) == 0x8C1CD5);
	CHECK(pe->hex(0x8C268F, 5) == "e890fcffff"); // call RW 0x8C2324
	CHECK(pe->hex(0x8C26B2, 5) == "e854fcffff"); // call RW 0x8C230B
	CHECK(pe->hex(0x8C26D5, 5) == "e818fcffff"); // call RW 0x8C22F2
	// the caller found: Object + 0x25C -> vslot 0x30 in RW 0x6957B9 (ZH Object::onVeterancyLevelChanged), without a direct call, jump or pointer to it
	CHECK(pe->hex(0x6957FD, 6) == "ff7508ff5030");
	const std::uint32_t textStart = 0x401000, textSize = 0x7CF000;
	std::vector<std::uint8_t> text;
	REQUIRE(pe->read(textStart, textSize, &text));
	size_t calls = 0;
	for (size_t i = 0; i + 5 <= text.size(); ++i)
	{
		if (text[i] != 0xE8 && text[i] != 0xE9)
		{
			continue;
		}
		const std::int32_t rel = (std::int32_t)((std::uint32_t)text[i + 1] | ((std::uint32_t)text[i + 2] << 8) | ((std::uint32_t)text[i + 3] << 16) | ((std::uint32_t)text[i + 4] << 24));
		calls += textStart + (std::uint32_t)i + 5u + (std::uint32_t)rel == 0x6957B9u ? 1 : 0;
	}
	CHECK(calls == 0);
	size_t pointers = 0;
	for (size_t i = 0; i + 4 <= text.size(); ++i)
	{
		pointers += ((std::uint32_t)text[i] | ((std::uint32_t)text[i + 1] << 8) | ((std::uint32_t)text[i + 2] << 16) | ((std::uint32_t)text[i + 3] << 24)) == 0x6957B9u ? 1 : 0;
	}
	CHECK(pointers == 0);
	std::vector<std::uint8_t> rdata; // the vtables
	REQUIRE(pe->read(0xBD0000, 0x1B9000, &rdata));
	size_t slots = 0;
	for (size_t i = 0; i + 4 <= rdata.size(); ++i)
	{
		slots += ((std::uint32_t)rdata[i] | ((std::uint32_t)rdata[i + 1] << 8) | ((std::uint32_t)rdata[i + 2] << 16) | ((std::uint32_t)rdata[i + 3] << 24)) == 0x6957B9u ? 1 : 0;
	}
	CHECK(slots == 0);
}
