// OpenBFME unit tests: PORT-113: the light points of the War of the Ring.
//
// What RotWK 2.01 does:
//   * every Player holds a tracker at + 0x338: the points (+ 0x33C) and the indices of the levels the player was given (+ 0x340). RW 0x8309E3 adds a
//     signed amount, RW 0x6AA91D takes a cost, both stop at zero; RW 0x6AA931 zeroes the points and leaves the levels.
//   * TheLightPointSystem (RW 0xDE8C04) holds the `LightPointLevel` blocks (RW 0x830EB5, fields RW 0xC532C0): a name, a label and the ids of the special
//     powers the level names. It always exists in retail (RW 0x63BB7B).
//   * the script actions 351 .. 354 and 405 (RW 0x7BD2D0, 0x7BD311, 0x7BD34F, 0x7BD390, 0x7C36BD) and the condition 124 (RW 0x7E5B56) give, reset, level,
//     count and compare; RW 0x6AB148 plays the player template's LightPointsUpSound for a positive amount.
//   * a special power a level names needs the owner's points to reach its LightPointCost (RW 0x7B1E07 -> 0x6AA8FB -> 0x830AE3); a trigger takes the cost
//     (SpecialPowerModule RW 0x897A5E, SpecialAbilityUpdate RW 0x853FCB, both through RW 0x6AA91D). A power an ability update runs meets both: the
//     module's trigger when the preparation starts (RW 0x853E4A) and the update's own trigger, so it pays twice.
//   * OpenBFME's snapshot keeps the points and the levels (retail's xfer RW 0x830D3F drops the points: see LightPointTracker::xfer).
//
// No game files are needed: the worlds, the INI text and the sounds are synthetic.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "IniTestUtil.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioRequests.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/GameState.h"
#include "Common/LightPointSystem.h"
#include "Common/LightPointTracker.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SpellStores.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace scripttest;

namespace
{
// ---- the synthetic data ------------------------------------------------------------------------------------------------------------------------------

// three special powers and two levels: LevelOne names A (cost 3) and B (no cost), nothing names C (cost 5); "NoSuchPower" is not a special power
const char kPowers[] =
	"SpecialPower SpecialPowerLightA\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 1000\n  LightPointCost = 3\nEnd\n"
	"SpecialPower SpecialPowerLightB\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialPowerLightC\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 1000\n  LightPointCost = 5\nEnd\n";
const char kLevels[] =
	"LightPointLevel LevelOne\n  Name = LW:LevelOne\n  SpecialAbilities = SpecialPowerLightA NoSuchPower SpecialPowerLightB\nEnd\n"
	"LightPointLevel LevelTwo\n  Name = LW:LevelTwo\nEnd\n";
// the spell stores own the rank store too, and a saved player's rank level (1) has to be one of the data's ranks (Player::xfer): the game has two
const char kRanks[] = "Rank 1\n  SkillPointsNeededDefault = 0\nEnd\nRank 2\n  SkillPointsNeededDefault = 100\nEnd\n";

// each faction has its own sound (synthetic names: the mod's files are never read here)
const char kSoundTemplates[] =
	"PlayerTemplate FactionA\n  LightPointsUpSound = TestLightUpA\nEnd\n"
	"PlayerTemplate FactionB\n  LightPointsUpSound = TestLightUpB\nEnd\n";
const char kOneSoundTemplate[] = "PlayerTemplate FactionA\n  LightPointsUpSound = TestLightUpA\nEnd\n";

// TheLightPointSystem is null for the scope (the branches RW 0x6AA902, 0x6AA940, 0x6AA95E, 0x853FAC test it); whatever was selected comes back
struct WithoutSystem
{
	LightPointSystem *saved = TheLightPointSystem;
	WithoutSystem() { TheLightPointSystem = nullptr; }
	~WithoutSystem() { TheLightPointSystem = saved; }
};

// the logic fixture (0 the neutral player, 1 Alice on FactionA, 2 Bob on FactionB) with the spell stores, which own TheLightPointSystem, and one script
// list per side
struct LightGame
{
	logictest::LogicWorld lw;
	SpellStores stores;
	std::vector<ScriptList> lists;

	explicit LightGame(const char *templates = nullptr, bool levels = true)
		: lw(RandomAlgorithm::ZH_CarryChain, templates)
		, stores(lw.w.keys)
	{
		REQUIRE_MESSAGE(lw.loadError.empty(), lw.loadError);
		stores.install();
		SpellStores::registerBlocks(lw.w.fx.env.blocks);
		lists.resize(3);
		const std::string err = load(std::string(kRanks) + kPowers + (levels ? kLevels : ""));
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~LightGame() { stores.uninstall(); }

	// several games alive: the one about to be used selects its stores and its upgrade center
	void select()
	{
		stores.install();
		lw.activateUpgrades();
	}
	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, const std::string &file = "lightpoints.ini", int *code = nullptr)
	{
		return lw.w.load(text, type, file, code);
	}
	void start()
	{
		std::vector<ScriptEngine::SideScripts> sides(3);
		const char *names[] = { "", "Alice", "Bob" };
		for (size_t i = 0; i < 3; ++i)
		{
			sides[i].sideName = names[i];
			sides[i].lists.push_back(&lists[i]);
		}
		engine().newGame(sides, nullptr, nullptr, false);
	}
	ScriptEngine &engine() { return lw.logic->scriptEngine(); }
	void frame() { lw.logic->runLogicFrame(); }
	Player *player(const char *name)
	{
		Player *p = lw.players.findPlayerWithName(name);
		REQUIRE_MESSAGE(p != nullptr, name);
		return p;
	}
	Player *neutral() { return lw.players.getNthPlayer(0); }
	const SpecialPowerTemplate *power(const char *name)
	{
		const SpecialPowerTemplate *t = stores.specialPowers().findSpecialPowerTemplate(name);
		REQUIRE_MESSAGE(t != nullptr, name);
		return t;
	}
	std::int32_t counter(const std::string &q)
	{
		const ScriptEngine::Counter *c = engine().findCounter(q);
		return c ? c->value : -999;
	}
	std::uint32_t hash() { return lw.logic->computeStateHash(); }
};

void run(ScriptEngine &e, const ScriptActionRec &a)
{
	ScriptActions::execute(e, a);
}

bool eval(ScriptEngine &e, const ScriptCondition &c)
{
	return ScriptConditions::evaluate(e, c);
}

std::vector<int> levelsOf(const Player *p)
{
	return p->lightPoints().getLevels();
}

// ---- the audio rig: a manager on a simulated device over a synthetic archive (the fixture of test_port_93.cpp), installed for this thread ----------

// a PCM16 mono 22.05 kHz silent WAV, made here (the simulated device needs a real header; no sound file is in the repository)
std::string silentWav(double ms)
{
	const std::uint32_t rate = 22050, frames = (std::uint32_t)(ms * rate / 1000.0);
	std::string out;
	auto u32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			out.push_back((char)(std::uint8_t)(v >> (8 * i)));
		}
	};
	auto u16 = [&](std::uint16_t v) {
		out.push_back((char)(std::uint8_t)v);
		out.push_back((char)(std::uint8_t)(v >> 8));
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

const char kAudioBase[] =
	"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n"
	"  SoundsExtension = wav\n  SampleCount2D = 4\n  SampleCount3D = 4\n  StreamCount = 3\n  MinSampleVolume = 2%\n  GlobalMinRange = 5000\n"
	"  GlobalMaxRange = 5000000\n  TimeToFadeAudio = 300\n  AmbientStreamHysteresisVolume = 10\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n"
	"  DefaultMusicVolume = 100%\n  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 4\n  MinRange = 160\n  MaxRange = 640\n  PitchShift = 0 0\n  PlayPercent = 100\n"
	"  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"MusicTrack DefaultMusicTrack\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  SubmixSlider = music\nEnd\n"
	"DialogEvent DefaultDialog\n  Volume = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"AmbientStream DefaultAmbientStream\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  MinRange = 100\n  MaxRange = 1000\n  SubmixSlider = ambient\nEnd\n"
	// the two factions' sounds: `ui player` is heard by the player the event belongs to (RW 0x4533A9)
	"AudioEvent TestLightUpA\n  Sounds = lighta\n  Type = ui player\n  Limit = 0\nEnd\n"
	"AudioEvent TestLightUpB\n  Sounds = lightb\n  Type = ui player\n  Limit = 0\nEnd\n";

struct LightAudio
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	double t = 0.0;

	explicit LightAudio(int localPlayer)
	{
		fx.mount({ { "data\\audio\\sounds\\lighta.wav", silentWav(300.0) }, { "data\\audio\\sounds\\lightb.wav", silentWav(300.0) } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini", kAudioBase);
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 1u << 20);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 1234u);
		setLocalPlayer(localPlayer);
		mgr->update(0.0);
		AudioApi::install(mgr.get());
	}
	~LightAudio() { AudioApi::install(nullptr); }

	// whose machine this is: the three players of the fixture exist and are enemies of one another
	void setLocalPlayer(int index)
	{
		AudioWorldQueries q;
		q.localPlayerIndex = [index] { return index; };
		q.playerExists = [](int i) { return i == 0 || i == 1 || i == 2; };
		q.relationship = [](int, int) { return AR_ENEMIES; };
		mgr->setWorldQueries(q);
	}
	void run(double ms)
	{
		const double end = t + ms;
		while (t != end)
		{
			t += 10.0;
			device->setTime(t);
			mgr->update(t);
		}
	}
	// the sounds the device started whose file name holds `needle`
	size_t startsOf(const std::string &needle) const
	{
		size_t n = 0;
		for (const auto &e : device->log())
		{
			if (e.kind == SimulatedAudioDevice::LogEntry::Start && e.file.find(needle) != std::string::npos)
			{
				++n;
			}
		}
		return n;
	}
};

// ---- the casting world: CombatTestUtil's CombatWorld with the special power store and, when asked, TheLightPointSystem -------------------------------

#define P113_HERO(name, power)                                                                                                                  \
	"Object " name "\n"                                                                                                                         \
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"                                                                                           \
	" Geometry = CYLINDER\n GeometryMajorRadius = 8\n GeometryMinorRadius = 8\n GeometryHeight = 20\n"                                          \
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"                                                                                     \
	" Behavior = AIUpdateInterface AiTag\n End\n"                                                                                               \
	" LocomotorSet\n  Locomotor = WalkerLoco\n  Condition = SET_NORMAL\n  Speed = 55\n End\n"                                                   \
	" Behavior = AttributeModifierPoolUpdate PoolTag\n End\n"                                                                                   \
	" Behavior = SpecialPowerModule PowerTag\n  SpecialPowerTemplate = " power "\n End\n"                                                       \
	"End\n"

// SpecialPowerLightCry costs 3 and a level names it; SpecialPowerLightFree costs 4 and no level names it; SpecialPowerLightPlain has no cost; the storm
// is the update-driven ability of test_port_93.cpp with a cost of 2
const char kCastPowers[] =
	"SpecialPower SpecialPowerLightCry\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 1000\n  LightPointCost = 3\nEnd\n"
	"SpecialPower SpecialPowerLightFree\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 1000\n  LightPointCost = 4\nEnd\n"
	"SpecialPower SpecialPowerLightPlain\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityLightStorm\n  Enum = SPECIAL_ARROW_STORM\n  ReloadTime = 1000\n  LightPointCost = 2\nEnd\n";
const char kCastLevels[] = "LightPointLevel LevelCry\n  SpecialAbilities = SpecialPowerLightCry\nEnd\n";
const char kCastObjects[] =
	"Weapon StormWeapon\n  AttackRange = 400\n  DelayBetweenShots = 100\n  DamageNugget\n    Damage = 10\n    Radius = 0.0\n    DelayTime = 0\n"
	"    DamageType = SLASH\n    DeathType = NORMAL\n  End\nEnd\n"
	P113_HERO("LightHero", "SpecialPowerLightCry")
	P113_HERO("FreeHero", "SpecialPowerLightFree")
	P113_HERO("PlainHero", "SpecialPowerLightPlain")
	"Object StormHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule StormPowerTag\n  SpecialPowerTemplate = SpecialAbilityLightStorm\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = ArrowStormUpdate StormTag\n"
	"  SpecialPowerTemplate = SpecialAbilityLightStorm\n  StartAbilityRange = 300\n  UnpackTime = 200\n  PreparationTime = 200\n"
	"  PackTime = 200\n  IgnoreFacingCheck = Yes\n  WeaponTemplate = StormWeapon\n  TargetRadius = 60\n  ShotsPerTarget = 1\n"
	"  ShotsPerBurst = 2\n  MaxShots = 5\n"
	" End\n"
	"End\n"
	"Object StormVictim\n  KindOf = INFANTRY SELECTABLE\n  Body = ActiveBody BodyTag\n  MaxHealth = 1000\n End\n  Behavior = AIUpdateInterface AiTag\n End\nEnd\n";

struct CastWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	LightPointSystem lightPoints;
	LightPointSystem *savedLightPoints = TheLightPointSystem;
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;

	explicit CastWorld(bool withSystem)
	{
		TheSpecialPowerStore = &powers;
		TheLightPointSystem = withSystem ? &lightPoints : nullptr;
		TheFXListStore = &fxLists;
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		w.fx.env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("LightPointLevel", [](INI *ini) { LightPointSystem::parseLightPointLevelDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		const std::string err = w.load(std::string(kCastPowers) + (withSystem ? kCastLevels : "") + kCastObjects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~CastWorld()
	{
		TheSpecialPowerStore = savedPowers;
		TheLightPointSystem = savedLightPoints;
		TheFXListStore = savedFx;
		TheAttributeModifierStore = savedModifiers;
	}
	const SpecialPowerTemplate *power(const char *name)
	{
		const SpecialPowerTemplate *t = powers.findSpecialPowerTemplate(name);
		REQUIRE_MESSAGE(t != nullptr, name);
		return t;
	}
};

SpecialPowerModule *powerOf(Object *o)
{
	SpecialPowerModule *m = dynamic_cast<SpecialPowerModule *>(o->findModule("SpecialPowerModule"));
	REQUIRE(m != nullptr);
	return m;
}

SpecialAbilityUpdate *abilityOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (SpecialAbilityUpdate *u = dynamic_cast<SpecialAbilityUpdate *>(m.get()))
		{
			return u;
		}
	}
	return nullptr;
}

// The storm hero's ability is ordered and the logic runs until the frame of the ability's trigger (RW 0x853EDF). The result is the owner's light points
// before the order, then each new total a logic frame left; `moduleTriggersBefore` gets the power module's trigger count in the last frame before the
// ability's trigger, `triggered` whether the ability's trigger came.
std::vector<int> stormPoints(CastWorld &f, Object *hero, Player *owner, unsigned &moduleTriggersBefore, bool &triggered)
{
	SpecialAbilityUpdate *u = abilityOf(hero);
	REQUIRE(u != nullptr);
	SpecialPowerModule *module = dynamic_cast<SpecialPowerModule *>(hero->findModule("SpecialPowerModule"));
	REQUIRE(module != nullptr);
	std::vector<int> seen{ owner->getLightPoints() };
	const Coord3D at = { 385.0f, 305.0f, 0.0f };
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	moduleTriggersBefore = module->triggers();
	for (int i = 0; i != 80 && u->abilitiesTriggered() == 0; ++i)
	{
		moduleTriggersBefore = module->triggers();
		f.frames(1);
		if (owner->getLightPoints() != seen.back())
		{
			seen.push_back(owner->getLightPoints());
		}
	}
	triggered = u->abilitiesTriggered() == 1;
	return seen;
}

// ---- the snapshot helpers -----------------------------------------------------------------------------------------------------------------------------

const std::vector<std::string> kCoreBlocks = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random" };

std::vector<std::uint8_t> saved(LightPointTracker &t)
{
	XferSave x;
	t.xfer(x);
	return x.data();
}

// the tracker's xfer over `bytes`: "" when it loads, the XferException's text when it is refused
std::string loaded(LightPointTracker &t, const std::vector<std::uint8_t> &bytes)
{
	try
	{
		XferLoad x(bytes);
		t.xfer(x);
	}
	catch (const XferException &e)
	{
		return e.what();
	}
	return std::string();
}

std::string loadedBlocks(LightGame &g, const std::vector<std::uint8_t> &bytes)
{
	try
	{
		g.lw.logic->gameState().loadBlocks(kCoreBlocks, bytes);
	}
	catch (const XferException &e)
	{
		return e.what();
	}
	return std::string();
}

void putI32(std::vector<std::uint8_t> &bytes, size_t at, std::int32_t v)
{
	for (int i = 0; i < 4; ++i)
	{
		bytes[at + (size_t)i] = (std::uint8_t)((std::uint32_t)v >> (8 * i));
	}
}
} // namespace

// ---- the tracker (Player + 0x338) ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("PORT-113 tracker: the points move by a signed amount and stop at zero (RW 0x8309E3, 0x6AA91D); the level list holds each index once "
		  "(RW 0x830D1E, 0x830AB9); a reset of the points keeps the levels (RW 0x6AA931)")
{
	LightPointTracker t;
	CHECK(t.isUntouched());
	CHECK(t.getPoints() == 0);
	t.addPoints(5);
	CHECK(t.getPoints() == 5);
	t.addPoints(-3); // RW 0x8309E3: `add [ecx + 4], amount` takes a negative amount as it is
	CHECK(t.getPoints() == 2);
	t.addPoints(-10); // RW 0x8309EC: a negative total becomes 0
	CHECK(t.getPoints() == 0);
	t.addPoints(7);
	t.subtractPoints(3); // RW 0x6AA91D
	CHECK(t.getPoints() == 4);
	t.subtractPoints(10); // RW 0x6AA92B: a negative total becomes 0
	CHECK(t.getPoints() == 0);
	t.subtractPoints(-4); // `sub` of a negative cost adds
	CHECK(t.getPoints() == 4);
	// the add is a 32-bit one (RW 0x8309E7) and only the sign of its result is tested (RW 0x8309EA jns): a total past INT_MAX wraps negative and becomes 0
	t.addPoints(INT_MAX - 4);
	CHECK(t.getPoints() == INT_MAX);
	t.addPoints(1);
	CHECK(t.getPoints() == 0);
	CHECK(t.isUntouched());

	t.addLevel(2);
	t.addLevel(0);
	t.addLevel(2); // RW 0x830D1E: appended only when the player does not have it (RW 0x8309F3)
	CHECK(t.getLevels() == std::vector<int>{ 2, 0 });
	CHECK(t.hasLevel(0));
	CHECK(t.hasLevel(2));
	CHECK_FALSE(t.hasLevel(1));
	CHECK_FALSE(t.isUntouched()); // a level alone makes the tracker state
	t.removeLevel(7); // RW 0x830AB9: none equal: nothing
	CHECK(t.getLevels() == std::vector<int>{ 2, 0 });
	t.removeLevel(2);
	CHECK(t.getLevels() == std::vector<int>{ 0 });
	t.addPoints(9);
	t.resetPoints(); // RW 0x6AA931 writes + 0x33C only
	CHECK(t.getPoints() == 0);
	CHECK(t.getLevels() == std::vector<int>{ 0 });
	t.addPoints(9);
	t.reset(); // Player::init: RW 0x6B0355 the levels, RW 0x6B0362 the points
	CHECK(t.getPoints() == 0);
	CHECK(t.getLevels().empty());
	CHECK(t.isUntouched());
}

// ---- TheLightPointSystem and the LightPointLevel block ------------------------------------------------------------------------------------------------

TEST_CASE("PORT-113 LightPointLevel: the block (RW 0x830EB5, fields RW 0xC532C0) stores a level with its label and the ids of its SpecialAbilities; a name "
		  "that is no special power is skipped (RW 0x73B51E); the spell stores own the system and select it")
{
	LightPointSystem *before = TheLightPointSystem;
	{
		LightGame g;
		REQUIRE(TheLightPointSystem == &g.stores.lightPoints());
		const LightPointSystem &system = g.stores.lightPoints();
		const SpecialPowerTemplate *a = g.power("SpecialPowerLightA"), *b = g.power("SpecialPowerLightB"), *c = g.power("SpecialPowerLightC");
		REQUIRE(system.getNumLevels() == 2);
		const LightPointLevel *one = system.getLevelByIndex(0), *two = system.getLevelByIndex(1);
		REQUIRE(one != nullptr);
		REQUIRE(two != nullptr);
		CHECK(system.getLevelByIndex(2) == nullptr);
		CHECK(one->getName() == "LevelOne");
		CHECK(one->m_displayName == "LW:LevelOne"); // `Name` (RW 0x73B192: the label; kept as written, S-520)
		CHECK(one->getSpecialAbilities() == std::vector<unsigned>{ a->getID(), b->getID() }); // RW 0x73B534: the ids, in the row's order
		CHECK_FALSE(one->m_isOverride);
		CHECK(one->getFinalOverride() == one);
		CHECK(two->getName() == "LevelTwo");
		CHECK(two->m_displayName == "LW:LevelTwo");
		CHECK(two->getSpecialAbilities().empty());
		// RW 0x830A3B: the first level whose list holds the power's id; -1 for a power no level names
		CHECK(system.findLevelIndexOfSpecialPower(a) == 0);
		CHECK(system.findLevelIndexOfSpecialPower(b) == 0);
		CHECK(system.findLevelIndexOfSpecialPower(c) == -1);
	}
	CHECK(TheLightPointSystem == before); // the stores hand the slot back
}

TEST_CASE("PORT-113 LightPointLevel: a second block with a known name throws (RW 0x830E06, text RW 0xC53284); in load type 2 a known level gets an "
		  "override that copies it and an unknown one throws (RW 0x830EEA .. 0x830F7F, text RW 0xC532F0); the reset deletes the overrides (RW 0x830A13)")
{
	LightGame g;
	LightPointSystem &system = g.stores.lightPoints();
	const SpecialPowerTemplate *a = g.power("SpecialPowerLightA"), *b = g.power("SpecialPowerLightB"), *c = g.power("SpecialPowerLightC");
	const LightPointLevel *stored = system.getLevelByIndex(0);
	REQUIRE(stored != nullptr);

	// a block of a name the system has: the INI error, and no level is added
	int code = 0;
	std::string err = g.load("LightPointLevel LevelOne\n  Name = LW:Again\nEnd\n", INI_LOAD_OVERWRITE, "lightpoints2.ini", &code);
	CHECK(err == "A light point level LevelOne already exists.\n\nError parsing INI block 'LightPointLevel' in file 'lightpoints2.ini'.");
	CHECK(code == 3);
	CHECK(system.getNumLevels() == 2);
	CHECK(stored->m_displayName == "LW:LevelOne");

	// map.ini (load type 2): the override copies the level (RW 0x830CE5) and its rows are read over the copy; SpecialAbilities appends (RW 0x73B534)
	err = g.load("LightPointLevel LevelOne\n  SpecialAbilities = SpecialPowerLightC\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	CHECK(system.getNumLevels() == 2); // the override is not a new entry of the list
	const LightPointLevel *first = stored->getFinalOverride();
	REQUIRE(first != stored);
	CHECK(first->m_isOverride);       // RW 0x830F7B
	CHECK_FALSE(stored->m_isOverride);
	CHECK(first->getName() == "LevelOne");
	CHECK(first->m_displayName == "LW:LevelOne");
	CHECK(first->getSpecialAbilities() == std::vector<unsigned>{ a->getID(), b->getID(), c->getID() });
	CHECK(stored->getSpecialAbilities() == std::vector<unsigned>{ a->getID(), b->getID() });
	// RW 0x830A5E reads the list of the level as stored, not of its override: the power the map added is still a power no level names
	CHECK(system.findLevelIndexOfSpecialPower(c) == -1);
	CHECK(g.player("Alice")->hasLightPointsForSpecialPower(c));

	// a second map block chains after the first (RW 0x830F59 .. 0x830F77: the final override is the one copied and linked)
	err = g.load("LightPointLevel LevelOne\n  Name = LW:Changed\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	const LightPointLevel *second = stored->getFinalOverride();
	REQUIRE(second != first);
	CHECK(first->getFinalOverride() == second);
	CHECK(second->m_displayName == "LW:Changed");
	CHECK(second->getSpecialAbilities() == std::vector<unsigned>{ a->getID(), b->getID(), c->getID() });

	// a level the system does not have, in load type 2
	code = 0;
	err = g.load("LightPointLevel LevelNine\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini", &code);
	CHECK(err == "Light point level LevelNine not found in map.ini\n\nError parsing INI block 'LightPointLevel' in file 'map.ini'.");
	CHECK(code == 3);
	CHECK(system.getNumLevels() == 2);

	// between maps (SpellStores::resetOverrides -> RW 0x830A13): the overrides go, the levels stay as the game's INI made them
	g.stores.resetOverrides();
	CHECK(system.getNumLevels() == 2);
	CHECK(stored->getFinalOverride() == stored);
	CHECK(stored->m_displayName == "LW:LevelOne");
	CHECK(stored->getSpecialAbilities() == std::vector<unsigned>{ a->getID(), b->getID() });
}

// ---- the Player's test (RW 0x6AA8FB) -------------------------------------------------------------------------------------------------------------------

TEST_CASE("PORT-113 Player: a special power no level names passes without points, one a level names needs the points to reach its LightPointCost "
		  "(RW 0x6AA8FB -> 0x830AE3, setge at RW 0x830B07); the levels a player holds are not read; without the system every power passes (RW 0x6AA902)")
{
	LightGame g;
	Player *alice = g.player("Alice"), *bob = g.player("Bob");
	const SpecialPowerTemplate *a = g.power("SpecialPowerLightA"), *b = g.power("SpecialPowerLightB"), *c = g.power("SpecialPowerLightC");
	REQUIRE(a->m_lightPointCost == 3); // SpecialPower LightPointCost (+ 0x30)
	REQUIRE(b->m_lightPointCost == 0);
	REQUIRE(c->m_lightPointCost == 5);
	CHECK_FALSE(alice->hasLightPointsForSpecialPower(a)); // named, 0 points against a cost of 3
	CHECK(alice->hasLightPointsForSpecialPower(b));       // named, no cost: 0 reaches 0
	CHECK(alice->hasLightPointsForSpecialPower(c));       // RW 0x830AEE: no level names it, its cost of 5 is not looked at
	alice->giveLightPoints(2);
	CHECK_FALSE(alice->hasLightPointsForSpecialPower(a));
	alice->giveLightPoints(1);
	CHECK(alice->hasLightPointsForSpecialPower(a)); // 3 reaches 3
	CHECK_FALSE(bob->hasLightPointsForSpecialPower(a)); // each player has its own tracker
	// RW 0x830AE3 reads the points only: holding the level that names the power changes nothing
	bob->giveLightPointLevel("LevelOne");
	REQUIRE(levelsOf(bob) == std::vector<int>{ 0 });
	CHECK_FALSE(bob->hasLightPointsForSpecialPower(a));
	{
		WithoutSystem none;
		CHECK(bob->hasLightPointsForSpecialPower(a));
	}
	CHECK_FALSE(bob->hasLightPointsForSpecialPower(a));

	// Player::init (the slot starts a game) empties the tracker: RW 0x6B0355 the levels, RW 0x6B0362 the points
	bob->giveLightPoints(6);
	REQUIRE(bob->getLightPoints() == 6);
	REQUIRE(levelsOf(bob) == std::vector<int>{ 0 });
	bob->init(bob->getPlayerTemplate(), 0);
	CHECK(bob->getLightPoints() == 0);
	CHECK(levelsOf(bob).empty());
	CHECK(bob->lightPoints().isUntouched());
}

// ---- the script actions and the condition ---------------------------------------------------------------------------------------------------------------

TEST_CASE("PORT-113 scripts: PLAYER_GIVE_LIGHT_POINTS (351, RW 0x7BD2D0) adds to every player of its parameter, PLAYER_RESET_LIGHT_POINTS (352, "
		  "RW 0x7BD311) zeroes them, SET_PLAYER_LIGHT_POINTS_TO_COUNTER (405, RW 0x7C36BD) sums them into a counter, PLAYER_COMPARE_LIGHT_POINTS "
		  "(124, RW 0x7E5B56) compares the sum with a number")
{
	LightGame g;
	g.start();
	ScriptEngine &e = g.engine();
	REQUIRE(g.lw.players.getPlayerCount() == 3);
	Player *alice = g.player("Alice"), *bob = g.player("Bob"), *neutral = g.neutral();
	REQUIRE(neutral != nullptr);

	run(e, act("PLAYER_GIVE_LIGHT_POINTS", { sp(11, "Alice"), ip(0, 5) }));
	CHECK(alice->getLightPoints() == 5);
	CHECK(bob->getLightPoints() == 0);
	CHECK(neutral->getLightPoints() == 0);
	// a parameter that names several players: each of them (RW 0x6A85EE walks the mask)
	run(e, act("PLAYER_GIVE_LIGHT_POINTS", { sp(11, "<All Players>"), ip(0, 2) }));
	CHECK(alice->getLightPoints() == 7);
	CHECK(bob->getLightPoints() == 2);
	CHECK(neutral->getLightPoints() == 2);
	// a negative amount takes points away (RW 0x8309E3), never below zero (RW 0x8309EC)
	run(e, act("PLAYER_GIVE_LIGHT_POINTS", { sp(11, "Bob"), ip(0, -1) }));
	CHECK(bob->getLightPoints() == 1);
	run(e, act("PLAYER_GIVE_LIGHT_POINTS", { sp(11, "Bob"), ip(0, -50) }));
	CHECK(bob->getLightPoints() == 0);

	run(e, act("SET_PLAYER_LIGHT_POINTS_TO_COUNTER", { sp(11, "Alice"), sp(4, "lp") }));
	CHECK(e.counter("lp").value == 7);
	run(e, act("SET_PLAYER_LIGHT_POINTS_TO_COUNTER", { sp(11, "<All Players>"), sp(4, "lp") }));
	CHECK(e.counter("lp").value == 9); // RW 0x7C36F8: 7 + 0 + 2

	// (the sum op the number), RW 0x7E5BAF .. 0x7E5BE6: 0 <, 1 <=, 2 ==, 3 >=, 4 >, 5 !=; Alice has 7
	auto alicePoints = [&](int op, int n) { return eval(e, cond("PLAYER_COMPARE_LIGHT_POINTS", { sp(11, "Alice"), ip(6, op), ip(0, n) })); };
	CHECK(alicePoints(0, 8));
	CHECK_FALSE(alicePoints(0, 7));
	CHECK(alicePoints(1, 7));
	CHECK_FALSE(alicePoints(1, 6));
	CHECK(alicePoints(2, 7));
	CHECK_FALSE(alicePoints(2, 8));
	CHECK(alicePoints(3, 7));
	CHECK_FALSE(alicePoints(3, 8));
	CHECK(alicePoints(4, 6));
	CHECK_FALSE(alicePoints(4, 7));
	CHECK(alicePoints(5, 6));
	CHECK_FALSE(alicePoints(5, 7));
	CHECK_FALSE(alicePoints(6, 7)); // RW 0x7E5B99: any other comparison is false
	// several players: their sum (RW 0x7E5B8B)
	CHECK(eval(e, cond("PLAYER_COMPARE_LIGHT_POINTS", { sp(11, "<All Players>"), ip(6, 2), ip(0, 9) })));
	CHECK_FALSE(eval(e, cond("PLAYER_COMPARE_LIGHT_POINTS", { sp(11, "<All Players>"), ip(6, 2), ip(0, 7) })));

	run(e, act("PLAYER_RESET_LIGHT_POINTS", { sp(11, "Alice") }));
	CHECK(alice->getLightPoints() == 0);
	CHECK(neutral->getLightPoints() == 2); // only the players of the parameter
	run(e, act("SET_PLAYER_LIGHT_POINTS_TO_COUNTER", { sp(11, "Alice"), sp(4, "lp") }));
	CHECK(e.counter("lp").value == 0);

	CHECK(e.stats().unportedActions.empty());
	CHECK(e.stats().unportedConditions.empty());
}

TEST_CASE("PORT-113 scripts: PLAYER_GIVE_LIGHT_POINT_LEVEL / PLAYER_REMOVE_LIGHT_POINT_LEVEL (353 / 354, RW 0x7BD34F / 0x7BD390 -> Player RW 0x6AA939 / "
		  "0x6AA957) add and remove the index of the level with that name (RW 0x830E63 / 0x830BD5); an unknown name and a game without the system do nothing")
{
	LightGame g;
	g.start();
	ScriptEngine &e = g.engine();
	Player *alice = g.player("Alice"), *bob = g.player("Bob");

	run(e, act("PLAYER_GIVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "LevelTwo") }));
	CHECK(levelsOf(alice) == std::vector<int>{ 1 }); // the level's place in the system's list
	run(e, act("PLAYER_GIVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "LevelTwo") }));
	CHECK(levelsOf(alice) == std::vector<int>{ 1 }); // RW 0x830D1E: once
	run(e, act("PLAYER_GIVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "LevelOne") }));
	CHECK(levelsOf(alice) == std::vector<int>{ 1, 0 });
	run(e, act("PLAYER_GIVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "LevelNine") }));
	CHECK(levelsOf(alice) == std::vector<int>{ 1, 0 }); // no such level: the loop ends without a call (RW 0x830EA4)
	run(e, act("PLAYER_GIVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "levelone") }));
	CHECK(levelsOf(alice) == std::vector<int>{ 1, 0 }); // RW 0x4065AA compares the bytes: the case counts
	CHECK(levelsOf(bob).empty());

	// the points and the levels are apart: a reset of the points leaves the levels (RW 0x6AA931)
	run(e, act("PLAYER_GIVE_LIGHT_POINTS", { sp(11, "Alice"), ip(0, 3) }));
	run(e, act("PLAYER_RESET_LIGHT_POINTS", { sp(11, "Alice") }));
	CHECK(alice->getLightPoints() == 0);
	CHECK(levelsOf(alice) == std::vector<int>{ 1, 0 });

	run(e, act("PLAYER_REMOVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "LevelTwo") }));
	CHECK(levelsOf(alice) == std::vector<int>{ 0 });
	run(e, act("PLAYER_REMOVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "LevelTwo") }));
	CHECK(levelsOf(alice) == std::vector<int>{ 0 }); // RW 0x830AB9: not held: nothing
	run(e, act("PLAYER_REMOVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "LevelNine") }));
	CHECK(levelsOf(alice) == std::vector<int>{ 0 });

	// several players at once
	run(e, act("PLAYER_GIVE_LIGHT_POINT_LEVEL", { sp(11, "<All Players>"), sp(10, "LevelTwo") }));
	CHECK(levelsOf(alice) == std::vector<int>{ 0, 1 });
	CHECK(levelsOf(bob) == std::vector<int>{ 1 });

	{
		WithoutSystem none; // RW 0x6AA940 / 0x6AA95E: the Player bodies test TheLightPointSystem first
		run(e, act("PLAYER_GIVE_LIGHT_POINT_LEVEL", { sp(11, "Bob"), sp(10, "LevelOne") }));
		run(e, act("PLAYER_REMOVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "LevelOne") }));
		CHECK(levelsOf(bob) == std::vector<int>{ 1 });
		CHECK(levelsOf(alice) == std::vector<int>{ 0, 1 });
	}
	CHECK(e.stats().unportedActions.empty());
}

// ---- the issue's check: a map script gives light points and the player hears the faction's sound -------------------------------------------------------

TEST_CASE("PORT-113 a map script gives light points: the script engine runs the actions, the player whose machine it is hears the LightPointsUpSound of "
		  "the own faction once, and a condition on the points fires the next script (RW 0x7BD2D0 -> 0x6AB148 -> 0x6AB1B6, RW 0x7E5B56)")
{
	LightGame g(kSoundTemplates);
	Player *alice = g.player("Alice"), *bob = g.player("Bob");
	REQUIRE(alice->getPlayerTemplate() != nullptr);
	REQUIRE(alice->getPlayerTemplate()->m_lightPointsUpSound == "TestLightUpA");
	REQUIRE(bob->getPlayerTemplate()->m_lightPointsUpSound == "TestLightUpB");
	LightAudio audio(alice->getPlayerIndex()); // Alice's machine

	// Alice's scripts: one gives her 5 points and the first level, once; the other waits for 5 points and writes them to a counter
	g.lists[1].items.push_back(item(script("GiveLight", { { cond("CONDITION_TRUE", {}) } },
		{ act("PLAYER_GIVE_LIGHT_POINTS", { sp(11, "Alice"), ip(0, 5) }), act("PLAYER_GIVE_LIGHT_POINT_LEVEL", { sp(11, "Alice"), sp(10, "LevelOne") }) }, {}, true)));
	g.lists[1].items.push_back(item(script("CountLight", { { cond("PLAYER_COMPARE_LIGHT_POINTS", { sp(11, "Alice"), ip(6, 3), ip(0, 5) }) } },
		{ act("SET_PLAYER_LIGHT_POINTS_TO_COUNTER", { sp(11, "Alice"), sp(4, "lp") }) }, {}, true)));
	// Bob's script gives Bob 4 points
	g.lists[2].items.push_back(item(script("GiveBob", { { cond("CONDITION_TRUE", {}) } }, { act("PLAYER_GIVE_LIGHT_POINTS", { sp(11, "Bob"), ip(0, 4) }) }, {}, true)));
	g.start();
	CHECK(g.counter("Alice/lp") == -999); // no such counter yet
	g.frame();
	g.frame();
	audio.run(50.0);

	CHECK(alice->getLightPoints() == 5);
	CHECK(levelsOf(alice) == std::vector<int>{ 0 });
	CHECK(bob->getLightPoints() == 4);
	CHECK(g.counter("Alice/lp") == 5);
	// the sounds: Alice's faction sound started once on her machine; Bob's belongs to Bob (`ui player` of another owner is not for the local player)
	CHECK(audio.startsOf("lighta") == 1);
	CHECK(audio.startsOf("lightb") == 0);
	CHECK(g.engine().stats().unportedActions.empty());
	CHECK(g.engine().stats().unportedConditions.empty());

	// the one-shot scripts are done: more frames give nothing and play nothing
	g.frame();
	g.frame();
	audio.run(50.0);
	CHECK(alice->getLightPoints() == 5);
	CHECK(bob->getLightPoints() == 4);
	CHECK(audio.startsOf("lighta") == 1);

	// on Bob's machine the same action is heard as his faction's sound
	audio.setLocalPlayer(bob->getPlayerIndex());
	run(g.engine(), act("PLAYER_GIVE_LIGHT_POINTS", { sp(11, "<All Players>"), ip(0, 1) }));
	audio.run(50.0);
	CHECK(alice->getLightPoints() == 6);
	CHECK(bob->getLightPoints() == 5);
	CHECK(audio.startsOf("lightb") == 1);
	CHECK(audio.startsOf("lighta") == 1); // Alice's is not heard there
}

TEST_CASE("PORT-113 the sound request: one per positive amount for a template with LightPointsUpSound; none for zero or a negative amount "
		  "(RW 0x6AB176 jle), for a template without the row (RW 0x6AB17F) or for a player without a template (RW 0x6AB171); the points move in every case")
{
	LightGame g(kOneSoundTemplate);
	Player *alice = g.player("Alice"), *bob = g.player("Bob"), *neutral = g.neutral();
	REQUIRE(alice->getPlayerTemplate() != nullptr);
	REQUIRE(alice->getPlayerTemplate()->m_lightPointsUpSound == "TestLightUpA");
	REQUIRE(bob->getPlayerTemplate() != nullptr);
	REQUIRE(bob->getPlayerTemplate()->m_lightPointsUpSound.empty());
	REQUIRE(neutral != nullptr);
	REQUIRE(neutral->getPlayerTemplate() == nullptr);

	// no audio manager on this thread: every request the logic makes is counted by the entry point
	AudioApi::install(nullptr);
	const std::uint64_t before = AudioApi::callsWithoutAudio();
	alice->giveLightPoints(3);
	CHECK(alice->getLightPoints() == 3);
	CHECK(AudioApi::callsWithoutAudio() == before + 1);
	alice->giveLightPoints(0);
	CHECK(alice->getLightPoints() == 3);
	CHECK(AudioApi::callsWithoutAudio() == before + 1);
	alice->giveLightPoints(-2); // RW 0x6AB165: the tracker takes the amount before the sign is tested
	CHECK(alice->getLightPoints() == 1);
	CHECK(AudioApi::callsWithoutAudio() == before + 1);
	bob->giveLightPoints(3);
	CHECK(bob->getLightPoints() == 3);
	CHECK(AudioApi::callsWithoutAudio() == before + 1);
	neutral->giveLightPoints(3);
	CHECK(neutral->getLightPoints() == 3);
	CHECK(AudioApi::callsWithoutAudio() == before + 1);
	alice->giveLightPoints(1);
	CHECK(AudioApi::callsWithoutAudio() == before + 2);
	// the other paths of the tracker ask for no sound: the cost of a power and the reset
	alice->spendLightPoints(1);
	alice->resetLightPoints();
	CHECK(alice->getLightPoints() == 0);
	CHECK(AudioApi::callsWithoutAudio() == before + 2);
}

// ---- the special powers ------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("PORT-113 casting: a power a level names is refused while its owner's light points are below its LightPointCost (canUseSpecialPower "
		  "RW 0x7B1D79 -> RW 0x7B1E07 -> 0x6AA8FB) and its trigger takes the cost from the owner (RW 0x897A5E -> 0x6AA91D)")
{
	CastWorld f(true);
	Object *hero = f.unit("LightHero", 'A', 300, 300);
	Object *freeHero = f.unit("FreeHero", 'A', 400, 300);
	Player *alice = f.playerOf('A'), *bob = f.playerOf('B');
	const SpecialPowerTemplate *cry = f.power("SpecialPowerLightCry"), *freePower = f.power("SpecialPowerLightFree");
	REQUIRE(cry->m_lightPointCost == 3);
	REQUIRE(freePower->m_lightPointCost == 4);
	REQUIRE(f.lightPoints.findLevelIndexOfSpecialPower(cry) == 0);
	REQUIRE(f.lightPoints.findLevelIndexOfSpecialPower(freePower) == -1);
	SpecialPowerModule *power = powerOf(hero);

	// no points: the command path refuses the named power (Object::doSpecialPower RW 0x68E754 asks canUseSpecialPower)
	CHECK_FALSE(SpecialPowerModules::canUseSpecialPower(*hero, cry));
	CHECK_FALSE(SpecialPowerModules::doSpecialPower(*hero, cry, 0, false));
	CHECK(power->triggers() == 0);
	alice->giveLightPoints(2);
	CHECK_FALSE(SpecialPowerModules::canUseSpecialPower(*hero, cry));
	bob->giveLightPoints(50); // another player's points do not count
	CHECK_FALSE(SpecialPowerModules::canUseSpecialPower(*hero, cry));
	alice->giveLightPoints(1);
	CHECK(SpecialPowerModules::canUseSpecialPower(*hero, cry)); // 3 reaches the cost of 3

	// the cast: the trigger takes the cost
	REQUIRE(SpecialPowerModules::doSpecialPower(*hero, cry, 0, false));
	CHECK(power->triggers() == 1);
	CHECK(alice->getLightPoints() == 0);
	CHECK(bob->getLightPoints() == 50);
	// the cost is paid, not counted as an effect the port lacks: one trigger of this power counts what one trigger of a power without a cost counts
	Object *plainHero = f.unit("PlainHero", 'A', 500, 300);
	REQUIRE(SpecialPowerModules::doSpecialPower(*plainHero, f.power("SpecialPowerLightPlain"), 0, false));
	REQUIRE(powerOf(plainHero)->triggers() == 1);
	CHECK(power->unportedEffects() == powerOf(plainHero)->unportedEffects());
	CHECK(alice->getLightPoints() == 0); // no cost: nothing to take, nothing given

	// past the recharge the power is refused again: the points are gone
	f.frames(10);
	CHECK_FALSE(SpecialPowerModules::canUseSpecialPower(*hero, cry));
	CHECK_FALSE(SpecialPowerModules::doSpecialPower(*hero, cry, 0, false));
	CHECK(power->triggers() == 1);
	// a forced cast (the script orders) skips the test; its trigger takes what there is (RW 0x6AA92B: never below zero)
	alice->giveLightPoints(1);
	REQUIRE(SpecialPowerModules::doSpecialPower(*hero, cry, 0, true));
	CHECK(power->triggers() == 2);
	CHECK(alice->getLightPoints() == 0);

	// a power no level names is never refused for points, and its trigger takes its cost all the same (RW 0x897A5E has no test of the level)
	alice->giveLightPoints(6);
	CHECK(SpecialPowerModules::canUseSpecialPower(*freeHero, freePower));
	REQUIRE(SpecialPowerModules::doSpecialPower(*freeHero, freePower, 0, false));
	CHECK(powerOf(freeHero)->triggers() == 1);
	CHECK(alice->getLightPoints() == 2); // 6 - 4
	f.frames(10);
	CHECK(SpecialPowerModules::canUseSpecialPower(*freeHero, freePower)); // 2 points against a cost of 4: not named, not tested
	REQUIRE(SpecialPowerModules::doSpecialPower(*freeHero, freePower, 0, false));
	CHECK(alice->getLightPoints() == 0); // 2 - 4 stops at zero
}

TEST_CASE("PORT-113 special ability: a power a SpecialAbilityUpdate runs takes its LightPointCost twice: the power module's trigger when the preparation "
		  "starts (RW 0x853E4A -> 0x897987 -> 0x897A5E) and the ability's own trigger (RW 0x853EDF: RW 0x853FA5 .. 0x853FCB)")
{
	CastWorld f(true);
	Object *hero = f.unit("StormHero", 'A', 300, 300);
	Object *victim = f.unit("StormVictim", 'B', 380, 300);
	REQUIRE(hero != nullptr);
	REQUIRE(victim != nullptr);
	Player *alice = f.playerOf('A'), *bob = f.playerOf('B');
	REQUIRE(f.power("SpecialAbilityLightStorm")->m_lightPointCost == 2);
	alice->giveLightPoints(5);
	bob->giveLightPoints(5);
	unsigned moduleTriggers = 99;
	bool triggered = false;
	const std::vector<int> seen = stormPoints(f, hero, alice, moduleTriggers, triggered);
	REQUIRE(triggered);
	// 5, then 3 while the ability prepares (the module's trigger ran: one cost), then 1 at the ability's trigger (the second cost)
	CHECK(seen == std::vector<int>{ 5, 3, 1 });
	CHECK(moduleTriggers == 1);
	CHECK(powerOf(hero)->triggers() == 1); // the ability's trigger starts the module's recharge only (RW 0x853F41), it does not trigger it again
	CHECK(bob->getLightPoints() == 5);     // the owner of the hero pays
	// the rest of the ability takes nothing more
	f.frames(60);
	CHECK(abilityOf(hero)->abilitiesTriggered() == 1);
	CHECK(alice->getLightPoints() == 1);
}

TEST_CASE("PORT-113 without TheLightPointSystem: the special ability's own trigger takes nothing (RW 0x853FAC) while the special power module's trigger "
		  "still takes the cost (RW 0x897A41 .. 0x897A5E has no test of the system), and no power is refused for points (RW 0x6AA902)")
{
	CastWorld f(false);
	REQUIRE(TheLightPointSystem == nullptr);
	Object *storm = f.unit("StormHero", 'A', 300, 300);
	Object *victim = f.unit("StormVictim", 'B', 380, 300);
	Object *hero = f.unit("LightHero", 'A', 300, 400);
	REQUIRE(storm != nullptr);
	REQUIRE(victim != nullptr);
	Player *alice = f.playerOf('A');
	const SpecialPowerTemplate *cry = f.power("SpecialPowerLightCry");

	CHECK(SpecialPowerModules::canUseSpecialPower(*hero, cry)); // no points, no system: not refused
	alice->giveLightPoints(5);
	unsigned moduleTriggers = 99;
	bool triggered = false;
	const std::vector<int> seen = stormPoints(f, storm, alice, moduleTriggers, triggered);
	REQUIRE(triggered);
	CHECK(seen == std::vector<int>{ 5, 3 }); // the module's trigger at the start of the preparation alone: one cost of 2
	CHECK(moduleTriggers == 1);
	REQUIRE(SpecialPowerModules::doSpecialPower(*hero, cry, 0, false));
	CHECK(powerOf(hero)->triggers() == 1);
	CHECK(alice->getLightPoints() == 0); // 3 - 3: the module's trigger of the plain power
}

// ---- the snapshot and the state hash -----------------------------------------------------------------------------------------------------------------------

TEST_CASE("PORT-113 save: the tracker's xfer writes the points and the level indices, loads them back, saves the same bytes again and refuses, by "
		  "name, a negative total, a level index the game does not have, a repeated index, an unknown version and a count the stream cannot hold")
{
	LightGame g; // two levels
	LightPointTracker t;
	t.addPoints(7);
	t.addLevel(1);
	t.addLevel(0);
	const std::vector<std::uint8_t> bytes = saved(t);
	// the version, the points, the count, the indices (little-endian)
	CHECK(bytes == std::vector<std::uint8_t>{ 1, 7, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 });

	LightPointTracker back;
	back.addPoints(99); // what the destination held does not survive a load
	back.addLevel(0);
	REQUIRE(loaded(back, bytes) == "");
	CHECK(back.getPoints() == 7);
	CHECK(back.getLevels() == std::vector<int>{ 1, 0 });
	CHECK(saved(back) == bytes);

	// an untouched tracker: no points, no levels
	LightPointTracker empty;
	const std::vector<std::uint8_t> emptyBytes = saved(empty);
	CHECK(emptyBytes == std::vector<std::uint8_t>{ 1, 0, 0, 0, 0, 0, 0, 0, 0 });
	REQUIRE(loaded(back, emptyBytes) == "");
	CHECK(back.isUntouched());

	// the refusals; the destination keeps what it held (nothing is assigned before the whole tracker is read)
	LightPointTracker kept;
	kept.addPoints(4);
	kept.addLevel(1);
	auto refusal = [&](std::vector<std::uint8_t> bad) {
		const std::string text = loaded(kept, bad);
		CHECK(kept.getPoints() == 4);
		CHECK(kept.getLevels() == std::vector<int>{ 1 });
		return text;
	};
	std::vector<std::uint8_t> bad = bytes;
	putI32(bad, 1, -1);
	CHECK(refusal(bad) == "light points: a total of -1 (the tracker never holds a negative one)");
	bad = bytes;
	putI32(bad, 9, 2);
	CHECK(refusal(bad) == "light points: level index 2 is not one of this game's 2 levels");
	bad = bytes;
	putI32(bad, 13, -1);
	CHECK(refusal(bad) == "light points: level index -1 is not one of this game's 2 levels");
	bad = bytes;
	putI32(bad, 13, 1);
	CHECK(refusal(bad) == "light points: level index 1 is listed twice");
	bad = bytes;
	bad[0] = 2;
	CHECK(refusal(bad) == "Don't know how to load version 2 (most recent is 1)");
	bad = bytes;
	bad[0] = 0;
	CHECK(refusal(bad) == "Found version 0 but the earliest supported is 1");
	// a count of 0x7FFFFFFF entries cannot fit the 8 bytes that follow it: refused before anything is allocated (Xfer::xferVectorSize)
	bad = bytes;
	putI32(bad, 5, INT_MAX);
	CHECK(refusal(bad) == "a list declares 2147483647 entries of at least 4 bytes, more than the 8 bytes left");
	// a stream cut inside the last index: the two declared entries no longer fit
	bad = bytes;
	bad.resize(bad.size() - 2);
	CHECK(refusal(bad) == "a list declares 2 entries of at least 4 bytes, more than the 6 bytes left");
	// a stream cut inside the points
	bad = bytes;
	bad.resize(3);
	CHECK(refusal(bad) == "the snapshot ends early: 4 bytes wanted at offset 1, 2 left in the stream");

	// the indices are places in THIS game's level list: with no system there is none
	{
		WithoutSystem none;
		CHECK(refusal(bytes) == "light points: level index 1 is not one of this game's 0 levels");
		REQUIRE(loaded(back, emptyBytes) == ""); // a tracker without levels loads anywhere
	}
}

TEST_CASE("PORT-113 save and hash: a game nobody gave light points in saves the Player bytes it saved before and hashes as before; points and levels "
		  "enter the state hash, travel in obfme:Players and come back in a second game with the same hash; a bad level index is refused by name")
{
	LightGame a;
	Player *alice = a.player("Alice"), *bob = a.player("Bob");
	std::vector<std::uint8_t> untouched;
	a.lw.logic->gameState().saveBlocks(kCoreBlocks, untouched);
	const std::uint32_t untouchedHash = a.hash();

	// points given and taken back, a level given and removed: the tracker is as it started, and so are the bytes and the hash
	alice->giveLightPoints(5);
	CHECK(a.hash() != untouchedHash);
	alice->giveLightPoints(-5);
	alice->giveLightPointLevel("LevelTwo");
	CHECK(a.hash() != untouchedHash); // a level alone is state
	alice->removeLightPointLevel("LevelTwo");
	CHECK(a.hash() == untouchedHash);
	std::vector<std::uint8_t> again;
	a.lw.logic->gameState().saveBlocks(kCoreBlocks, again);
	CHECK(again == untouched);

	// Alice holds points and a level: only her record grows, by the tracker's 13 bytes (version 1, the points, the count 1, the index 1)
	alice->giveLightPoints(0x00ABCDEF);
	alice->giveLightPointLevel("LevelTwo");
	const std::uint32_t pointsHash = a.hash();
	CHECK(pointsHash != untouchedHash);
	std::vector<std::uint8_t> touched;
	a.lw.logic->gameState().saveBlocks(kCoreBlocks, touched);
	CHECK(touched.size() == untouched.size() + 13);
	const std::vector<std::uint8_t> record = { 1, 0xEF, 0xCD, 0xAB, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
	const auto at = std::search(touched.begin(), touched.end(), record.begin(), record.end());
	REQUIRE(at != touched.end());
	CHECK(std::search(at + 1, touched.end(), record.begin(), record.end()) == touched.end()); // once: Bob and the neutral player save no tracker
	CHECK(std::search(untouched.begin(), untouched.end(), record.begin(), record.end()) == untouched.end());
	// the points count for the hash by their amount, the level by its index
	alice->giveLightPoints(1);
	CHECK(a.hash() != pointsHash);
	alice->giveLightPoints(-1);
	CHECK(a.hash() == pointsHash);
	alice->removeLightPointLevel("LevelTwo");
	alice->giveLightPointLevel("LevelOne");
	CHECK(a.hash() != pointsHash);
	alice->removeLightPointLevel("LevelOne");
	alice->giveLightPointLevel("LevelTwo");
	CHECK(a.hash() == pointsHash);

	// a second game of the same data loads the blocks: the same points, levels and hash, and it saves the same bytes
	LightGame b;
	Player *alice2 = b.player("Alice"), *bob2 = b.player("Bob");
	bob2->giveLightPoints(9); // what the loading game held is replaced, for a player saved without light points too
	bob2->giveLightPointLevel("LevelOne");
	REQUIRE(loadedBlocks(b, touched) == "");
	CHECK(alice2->getLightPoints() == 0x00ABCDEF);
	CHECK(levelsOf(alice2) == std::vector<int>{ 1 });
	CHECK(bob2->getLightPoints() == 0);
	CHECK(levelsOf(bob2).empty());
	CHECK(b.hash() == pointsHash);
	std::vector<std::uint8_t> resaved;
	b.lw.logic->gameState().saveBlocks(kCoreBlocks, resaved);
	CHECK(resaved == touched);

	// the whole snapshot: saved by the first game, loaded by the second
	a.select();
	bob->giveLightPoints(12);
	const std::uint32_t bothHash = a.hash();
	std::vector<std::uint8_t> snapshot;
	std::string err;
	REQUIRE_MESSAGE(a.lw.logic->saveSnapshot(snapshot, &err), err);
	b.select();
	REQUIRE_MESSAGE(b.lw.logic->loadSnapshot(snapshot, &err), err);
	CHECK(alice2->getLightPoints() == 0x00ABCDEF);
	CHECK(bob2->getLightPoints() == 12);
	CHECK(b.hash() == bothHash);

	// the blocks of the game without light points, loaded over a game that has them: the players are back at nothing
	REQUIRE(loadedBlocks(b, untouched) == "");
	CHECK(alice2->getLightPoints() == 0);
	CHECK(levelsOf(alice2).empty());
	CHECK(bob2->getLightPoints() == 0);
	CHECK(b.hash() == untouchedHash);

	// Alice's saved level index changed to 9: the load is refused and names the index
	std::vector<std::uint8_t> bad = touched;
	putI32(bad, (size_t)(at - touched.begin()) + 9, 9);
	CHECK(loadedBlocks(b, bad) == "block 'obfme:Players': light points: level index 9 is not one of this game's 2 levels");
	// the refused load is not a state of the game to go on from: the players are loaded again from the good blocks
	REQUIRE(loadedBlocks(b, touched) == "");
	CHECK(alice2->getLightPoints() == 0x00ABCDEF);
	CHECK(levelsOf(alice2) == std::vector<int>{ 1 });
	CHECK(b.hash() == pointsHash);
}
