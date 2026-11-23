// OpenBFME unit tests (Local 94): the last 8 condition / action types of the script map:
//   1. IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT (condition 187, RW 0x7ED34F -> 0x7EB582)
//   2. PLAYER_LOST_OBJECT_TYPE (condition 104, RW 0x7ECBDF -> 0x7EACCE, getObjectCount RW 0x605DB0, setObjectCount RW 0x607822)
//   3. DEBUG_MESSAGE_BOX / DEBUG_STRING (actions 0 / 104, RW 0x7CB007 / 0x7CB017 -> 0x604D85)
//   4. SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST (action 255, RW 0x7CD240 -> 0x7C4B06 -> 0x6AA5CA -> 0x8F481C / 0x8F66BD)
//   5. PLAYER_REPAIR_NAMED_STRUCTURE (action 257, RW 0x7CC332 -> 0x7C0402 -> 0x6AA671 -> 0x8F78EB, updateBridgeRepair RW 0x8F8F68)
//   6. OPTIONS_SET_DRAWICON_UI_MODE (action 300, RW 0x7CE222 -> 0x7BD55C, Drawable::drawIconUI RW 0x679134)
//   7. OPTIONS_SET_PARTICLE_CAP_MODE (action 301, RW 0x7CE2B6 -> 0x7BD56F, GameLogic::getParticleCapMode RW 0x441B52)
//   8. RESIZE_VIEW_GUARDBAND (action 331, RW 0x7CE53A -> 0x7BD6E9 -> 0x48B5FC)
// Synthetic tests only: no retail or mod files are required.
// GPL-3.0.

#include "doctest.h"

#include "IniTestUtil.h"
#include "PathfindTestUtil.h"
#include "ProdTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/DrawableIconUI.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/Eva.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/MusicScripts.h"
#include "GameClient/ScriptCameraDirector.h"
#include "GameClient/TacticalView.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <string>
#include <vector>

using namespace prodtest;
using namespace scripttest;

namespace
{

const char kLocal94Ini[] =
	"SpecialPower SpecialAbilityL94Darkness\n"
	"  Enum = SPECIAL_GENERAL_TARGETLESS\n"
	"  ReloadTime = 1000\n"
	"  RadiusCursorRadius = 80.0\n"
	"End\n"
	"Object L94Infantry\n"
	"  KindOf = PRELOAD SELECTABLE CAN_ATTACK INFANTRY\n"
	"  BuildCost = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10.0\n"
	"  GeometryHeight = 10.0\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object L94Horde\n"
	"  KindOf = PRELOAD SELECTABLE CAN_ATTACK INFANTRY HORDE\n"
	"  BuildCost = 400\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20.0\n"
	"  GeometryHeight = 10.0\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 400\n"
	"  End\n"
	"End\n"
	"Object L94CheapStructure\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  BuildCost = 200\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 15.0\n"
	"  GeometryHeight = 20.0\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object L94ValuableStructure\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  BuildCost = 1500\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20.0\n"
	"  GeometryHeight = 25.0\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"Object L94Porter\n"
	"  KindOf = PRELOAD SELECTABLE DOZER INFANTRY\n"
	"  BuildCost = 500\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10.0\n"
	"  GeometryHeight = 10.0\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 300\n"
	"  End\n"
	"  Behavior = DozerAIUpdate ModuleTag_DozerAI\n"
	"    RepairHealthPercentPerSecond = 20%\n"
	"    BoredTime = 5000\n"
	"    BoredRange = 150\n"
	"  End\n"
	"End\n"
	"Object L94Hero\n"
	"  KindOf = PRELOAD SELECTABLE CAN_ATTACK HERO INFANTRY\n"
	"  BuildCost = 2000\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10.0\n"
	"  GeometryHeight = 10.0\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Darkness\n"
	"    SpecialPowerTemplate = SpecialAbilityL94Darkness\n"
	"    StartsPaused = No\n"
	"  End\n"
	"End\n";

struct Local94Host : ScriptEngineHost
{
	ProdWorld &w;
	explicit Local94Host(ProdWorld &world) : w(world) {}
	Object *createObject(const ThingTemplate &tmpl, Team &team, const Coord3D &pos, float angle) override
	{
		Object *o = w.logic->newObject(&tmpl, &team, ObjectStatusMaskType{});
		if (o)
		{
			o->setPosition(&pos);
			o->setOrientation(angle);
			w.logic->partition().registerObject(*o);
		}
		return o;
	}
};

struct Local94Fx : ProdWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = nullptr;
	AIWorldConfig aiCfg;
	std::vector<ScriptList> lists = std::vector<ScriptList>(3);
	pathtest::SyntheticTerrain terrain{ 100, 100 };
	std::unique_ptr<AIWorld> ai;
	Local94Host host{ *this };
	Player *p0 = nullptr;
	Player *p1 = nullptr;

	Local94Fx()
	{
		aiCfg.pathfind = pathtest::testConfig();
		ai = std::make_unique<AIWorld>(*logic, aiCfg, w.fx.env.macros);
		ai->attach();
		ai->newMap(terrain);
		savedPowers = TheSpecialPowerStore;
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		p0 = players.findPlayerWithName("Alice");
		p1 = players.findPlayerWithName("Bob");
		REQUIRE(p0 != nullptr);
		REQUIRE(p1 != nullptr);
		load(kLocal94Ini);
		logic->settings().buildRulesLoaded = true;
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
		logic->partition().setRegion(0.0f, 0.0f, 1200.0f, 1200.0f);
		logic->scriptEngine().setHost(&host);
	}

	~Local94Fx()
	{
		logic->reset();
		ai.reset();
		TheSpecialPowerStore = savedPowers;
	}

	Object *spawn(const char *tmplName, Team *team, float x, float y)
	{
		const ThingTemplate *tt = logic->things().findTemplate(tmplName);
		REQUIRE_MESSAGE(tt != nullptr, tmplName);
		Object *o = logic->newObject(tt, team, ObjectStatusMaskType{});
		REQUIRE(o != nullptr);
		const Coord3D pos{ x, y, 0.0f };
		o->setPosition(&pos);
		logic->partition().registerObject(*o);
		logic->partition().update();
		return o;
	}

	void execAction(const ScriptActionRec &a)
	{
		ScriptActions::execute(logic->scriptEngine(), a);
	}
	void runOnce(std::unique_ptr<Script> s, int sideIdx = 1)
	{
		lists[(size_t)sideIdx].items.clear();
		lists[(size_t)sideIdx].items.push_back(item(std::move(s)));
		std::vector<ScriptEngine::SideScripts> sides(3);
		const char *names[] = { "", "Alice", "Bob" };
		for (size_t i = 0; i < 3; ++i)
		{
			sides[i].sideName = names[i];
			sides[i].lists.push_back(&lists[i]);
		}
		logic->scriptEngine().newGame(sides, nullptr, nullptr, false);
		logic->scriptEngine().update();
	}
};

std::vector<std::uint8_t> wavBytes(double ms)
{
	const std::uint32_t rate = 22050, frames = (std::uint32_t)(ms * rate / 1000.0);
	std::vector<std::uint8_t> out;
	auto u32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			out.push_back((std::uint8_t)(v >> (8 * i)));
		}
	};
	auto u16 = [&](std::uint16_t v) {
		out.push_back((std::uint8_t)v);
		out.push_back((std::uint8_t)(v >> 8));
	};
	auto tag = [&](const char *t) { out.insert(out.end(), t, t + 4); };
	tag("RIFF");
	u32(36 + frames * 2);
	tag("WAVE");
	tag("fmt ");
	u32(16);
	u16(1);
	u16(1);
	u32(rate);
	u32(rate * 2);
	u16(2);
	u16(16);
	tag("data");
	u32(frames * 2);
	out.insert(out.end(), frames * 2, 0);
	return out;
}

const char kEvaAudioIni[] =
	"AudioSettings\n"
	"  AudioRoot = Data\\Audio\n"
	"  SoundsFolder = Sounds\n"
	"  MusicFolder = Tracks\n"
	"  StreamingFolder = Speech\n"
	"  AmbientStreamFolder = AmbientStreams\n"
	"  SoundsExtension = wav\n"
	"  SampleCount2D = 4\n"
	"  SampleCount3D = 4\n"
	"  StreamCount = 3\n"
	"End\n"
	"AudioEvent DefaultSoundEffect\n"
	"  Volume = 100\n"
	"  Priority = high\n"
	"  Limit = 4\n"
	"  MinRange = 160\n"
	"  MaxRange = 640\n"
	"  PlayPercent = 100\n"
	"  Type = ui everyone\n"
	"  Type = +DEFAULT\n"
	"End\n"
	"DialogEvent DefaultDialog\n"
	"  Volume = 100\n"
	"  Type = ui everyone\n"
	"  Type = +DEFAULT\n"
	"End\n"
	"DialogEvent EvaCueSound\n"
	"  Filename = evacue.wav\n"
	"End\n"
	"PredefinedEvaEvent CampDestroyed\n"
	"  Priority = 50\n"
	"  TimeBetweenEventsMS = 1000\n"
	"  ExpirationTimeMS = 5000\n"
	"  SideSound\n"
	"    Side = Men\n"
	"    Sound = EvaCueSound\n"
	"  End\n"
	"End\n";

} // namespace

TEST_CASE("LOCAL-94 IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT checks unit kinds and Eva position (RW 0x7EB582)")
{
	Local94Fx fx;
	initest::Fixture audioFx;
	const std::vector<std::uint8_t> wav = wavBytes(400.0);
	audioFx.mount({ { "data\\audio\\speech\\evacue.wav", std::string((const char *)wav.data(), wav.size()) } });
	AudioIniState audioIni;
	audioIni.registerBlocks(audioFx.env.blocks);
	const std::string err = initest::loadError(audioFx.env, "eva.ini", kEvaAudioIni);
	REQUIRE_MESSAGE(err.empty(), err);
	AudioAssetCache cache(&audioFx.fsys, 4u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager audioMgr(audioIni, cache, device, RandomAlgorithm::ZH_CarryChain, 7);
	Eva eva(audioIni, audioMgr);
	eva.setLocalSide("Men");
	eva.setLocalPlayerIndex(fx.p0->getPlayerIndex());

	struct EvaInstallScope
	{
		explicit EvaInstallScope(Eva *e) { AudioApi::installEva(e); }
		~EvaInstallScope() { AudioApi::installEva(nullptr); }
	} evaScope(&eva);

	// Spawn 2 INFANTRY units of Alice near (200, 200), plus 1 STRUCTURE and 1 HORDE of Alice (must be rejected by PartitionFilterIsKindOfUnit RW 0x6618E6),
	// 1 INFANTRY of Bob near (200, 200) (rejected by PartitionFilterPlayerAffiliation RW 0x6610B0), and 1 INFANTRY of Alice far away at (600, 600).
	fx.spawn("L94Infantry", fx.p0->getDefaultTeam(), 190.0f, 200.0f);
	fx.spawn("L94Infantry", fx.p0->getDefaultTeam(), 220.0f, 210.0f);
	fx.spawn("L94CheapStructure", fx.p0->getDefaultTeam(), 200.0f, 200.0f);
	fx.spawn("L94Horde", fx.p0->getDefaultTeam(), 205.0f, 200.0f);
	fx.spawn("L94Infantry", fx.p1->getDefaultTeam(), 200.0f, 205.0f);
	fx.spawn("L94Infantry", fx.p0->getDefaultTeam(), 600.0f, 600.0f);

	auto evalCond = [&](const char *evaEvent, int cmpOp, int targetCount) {
		ScriptCondition c = cond("IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT", {
			sp(11, "Alice"),
			rp(100.0f),
			sp(76, evaEvent),
			ip(6, cmpOp),
			ip(0, targetCount),
		});
		return ScriptConditions::evaluate(fx.logic->scriptEngine(), c);
	};

	// Before CampDestroyed plays, lastPlayedPosition is false -> count is 0
	CHECK_FALSE(evalCond("CampDestroyed", 3, 1)); // 0 >= 1 is false
	CHECK(evalCond("CampDestroyed", 2, 0));       // 0 == 0 is true

	// Report CampDestroyed at (200, 200, 0) and step Eva so it plays
	const Coord3D evaPos{ 200.0f, 200.0f, 0.0f };
	REQUIRE(eva.reportEventByName("CampDestroyed", &evaPos, 100.0));
	device.setTime(200.0);
	audioMgr.update(200.0);
	eva.update(200.0);
	Coord3D playedAt{};
	REQUIRE(eva.lastPlayedPosition(eva.findEvent("CampDestroyed"), &playedAt));

	// Only the 2 INFANTRY units of Alice within 100.0f count (STRUCTURE, HORDE, enemy, and out-of-range are excluded)
	CHECK(evalCond("CampDestroyed", 2, 2));       // 2 == 2 is true
	CHECK(evalCond("CampDestroyed", 3, 2));       // 2 >= 2 is true
	CHECK_FALSE(evalCond("CampDestroyed", 3, 3)); // 2 >= 3 is false
	CHECK_FALSE(evalCond("CampDestroyed", 0, 2)); // 2 < 2 is false
	CHECK_FALSE(evalCond("UnknownEvaEvent", 3, 1));
	CHECK(fx.logic->scriptEngine().stats().unportedConditions.empty());

	// Also verify MusicScripts handles ordinal 187 under C_IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT
	PlayerScriptsList psl;
	psl.lists.resize(1);
	ScriptItem item;
	item.script = std::make_unique<Script>();
	item.script->name = "MusicEvaNear";
	item.script->isActive = true;
	item.script->isOneShot = true;
	item.script->easy = item.script->normal = item.script->hard = true;
	OrCondition orc;
	orc.conditions.push_back(cond("IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT", {
		sp(11, "Alice"),
		rp(100.0f),
		sp(76, "CampDestroyed"),
		ip(6, 3),
		ip(0, 2),
	}));
	item.script->orConditions.push_back(std::move(orc));
	item.script->actions.push_back(act("SET_FLAG", { sp(5, "EvaNearMatched"), ip(8, 1) }));
	psl.lists[0].items.push_back(std::move(item));

	MusicScripts music(audioMgr, *fx.logic, fx.players, &eva);
	music.loadScripts(psl);
	music.update(1);
	CHECK(music.flag("EvaNearMatched"));
	CHECK(music.stats().unported.empty());
}

TEST_CASE("LOCAL-94 PLAYER_LOST_OBJECT_TYPE tracks per-player object counts, crc, and xfer (RW 0x7EACCE / 0x605DB0 / 0x607822)")
{
	Local94Fx fx;
	std::vector<ScriptEngine::SideScripts> sides(3);
	sides[0].sideName = "";
	sides[1].sideName = "Alice";
	sides[2].sideName = "Bob";
	fx.logic->scriptEngine().newGame(sides, nullptr, nullptr, false);

	Object *u1 = fx.spawn("L94Infantry", fx.p0->getDefaultTeam(), 100.0f, 100.0f);
	Object *u2 = fx.spawn("L94Infantry", fx.p0->getDefaultTeam(), 130.0f, 100.0f);
	Object *underConstruction = fx.spawn("L94Infantry", fx.p0->getDefaultTeam(), 160.0f, 100.0f);
	underConstruction->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true); // ignored by countPlayerObjects(..., ignoreDead=true)

	ScriptCondition lostCond = cond("PLAYER_LOST_OBJECT_TYPE", {
		sp(11, "Alice"),
		sp(61, "L94Infantry"),
	});

	auto hashEngine = [&]() {
		StateHasher h;
		fx.logic->scriptEngine().crc(h);
		return h.value();
	};
	const std::uint32_t hashBefore = hashEngine();

	// First check: sumOfObjs = 2, currentCount = 0 -> records 2 in m_objectCounts, returns false (2 < 0 is false)
	CHECK_FALSE(ScriptConditions::evaluate(fx.logic->scriptEngine(), lostCond));
	CHECK(fx.logic->scriptEngine().getObjectCount(fx.p0->getPlayerIndex(), "L94Infantry") == 2);
	const std::uint32_t hashWithCount = hashEngine();
	CHECK(hashWithCount != hashBefore);

	// Save / load round-trip of ScriptEngine (version 8 carries m_objectCounts)
	XferSave xs;
	fx.logic->scriptEngine().xfer(xs);
	fx.logic->scriptEngine().setObjectCount(fx.p0->getPlayerIndex(), "L94Infantry", 0);
	REQUIRE(fx.logic->scriptEngine().getObjectCount(fx.p0->getPlayerIndex(), "L94Infantry") == 0);
	XferLoad xl(xs.data());
	fx.logic->scriptEngine().xfer(xl);
	fx.logic->scriptEngine().finishLoad();
	CHECK(fx.logic->scriptEngine().getObjectCount(fx.p0->getPlayerIndex(), "L94Infantry") == 2);
	CHECK(hashEngine() == hashWithCount);

	// Second check with no loss: sumOfObjs = 2, currentCount = 2 -> false
	CHECK_FALSE(ScriptConditions::evaluate(fx.logic->scriptEngine(), lostCond));

	// Lose one unit: sumOfObjs = 1 < currentCount (2) -> updates count to 1 and returns true
	u1->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	CHECK(ScriptConditions::evaluate(fx.logic->scriptEngine(), lostCond));
	CHECK(fx.logic->scriptEngine().getObjectCount(fx.p0->getPlayerIndex(), "L94Infantry") == 1);

	// Next frame without another loss: sumOfObjs = 1 == currentCount (1) -> returns false
	CHECK_FALSE(ScriptConditions::evaluate(fx.logic->scriptEngine(), lostCond));

	// Lose the second unit: sumOfObjs = 0 < currentCount (1) -> updates count to 0 and returns true
	u2->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	CHECK(ScriptConditions::evaluate(fx.logic->scriptEngine(), lostCond));
	CHECK(fx.logic->scriptEngine().getObjectCount(fx.p0->getPlayerIndex(), "L94Infantry") == 0);

	// Defeated player returns 0 in getObjectCount (RW 0x605DB0 checks Player::isPlayerActive RW 0x6AAC52)
	fx.logic->scriptEngine().setObjectCount(fx.p0->getPlayerIndex(), "L94Infantry", 5);
	CHECK(fx.logic->scriptEngine().getObjectCount(fx.p0->getPlayerIndex(), "L94Infantry") == 5);
	fx.p0->setDefeated(true);
	CHECK(fx.logic->scriptEngine().getObjectCount(fx.p0->getPlayerIndex(), "L94Infantry") == 0);
	CHECK(fx.logic->scriptEngine().stats().unportedConditions.empty());
}

TEST_CASE("LOCAL-94 DEBUG_MESSAGE_BOX and DEBUG_STRING are retail no-ops without unported warnings (RW 0x7CB007 / 0x7CB017 / 0x604D85)")
{
	Local94Fx fx;
	fx.execAction(act("DEBUG_MESSAGE_BOX", { sp(10, "WHAYT") }));
	fx.execAction(act("DEBUG_STRING", { sp(10, "trace") }));
	CHECK(fx.logic->scriptEngine().stats().unportedActions.empty());
	CHECK(fx.logic->scriptEngine().pendingClientRequests().empty());
}

TEST_CASE("LOCAL-94 SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST targets highest-value enemy cluster and fires power (RW 0x7C4B06 / 0x8F66BD / 0x8F5BDB)")
{
	Local94Fx fx;
	// Alice is PLAYER_COMPUTER (current script player), Bob is PLAYER_HUMAN (resolved by getSkirmishEnemyPlayer RW 0x758AB1)
	fx.p0->setPlayerType(PLAYER_COMPUTER, true);
	fx.p1->setPlayerType(PLAYER_HUMAN, false);
	

	// Low-value structures of Bob at (100, 100) and (500, 500) (cost 200 each) and high-value structure cluster at (300, 300) (two structures of cost 1500 each)
	fx.spawn("L94CheapStructure", fx.p1->getDefaultTeam(), 100.0f, 100.0f);
	fx.spawn("L94ValuableStructure", fx.p1->getDefaultTeam(), 300.0f, 300.0f);
	fx.spawn("L94ValuableStructure", fx.p1->getDefaultTeam(), 310.0f, 300.0f);
	fx.spawn("L94CheapStructure", fx.p1->getDefaultTeam(), 500.0f, 500.0f);

	const SpecialPowerTemplate *spDarkness = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityL94Darkness");
	REQUIRE(spDarkness != nullptr);

	Coord3D targetPos{ 0.0f, 0.0f, 0.0f };
	fx.p0->computeSuperweaponTarget(*fx.logic, spDarkness, targetPos, fx.p1->getPlayerIndex(), 80.0f);
	// Target must land on the (300, 300) high-value cluster, not on the cheap corners
	CHECK(std::fabs(targetPos.x - 305.0f) < 60.0f); // tolerance: 11x11 fine grid step is 0.1*r around the two-building cluster at (300..310, 300)
	CHECK(std::fabs(targetPos.y - 300.0f) < 60.0f); // tolerance: 11x11 fine grid step is 0.1*r around the two-building cluster at (300..310, 300)

	Object *caster = fx.spawn("L94Hero", fx.p0->getDefaultTeam(), 300.0f, 300.0f);
	SpecialPowerModuleInterface *mod = SpecialPowerModules::findModule(*caster, spDarkness);
	REQUIRE(mod != nullptr);
	REQUIRE(mod->isReady());

	fx.runOnce(script("FireDarkness", { { cond("CONDITION_TRUE", {}) } }, {
		act("SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST", {
			sp(11, "Alice"),
			sp(31, "SpecialAbilityL94Darkness"),
		}),
	}));

	CHECK_FALSE(mod->isReady());
	CHECK(fx.logic->scriptEngine().stats().unportedActions.empty());
}

TEST_CASE("LOCAL-94 PLAYER_REPAIR_NAMED_STRUCTURE queues damaged structure, dispatches AI dozer, and round-trips crc/xfer (RW 0x7C0402 / 0x8F78EB / 0x8F8F68)")
{
	Local94Fx fx;
	fx.p0->setPlayerType(PLAYER_COMPUTER, false);

	Object *inn = fx.spawn("L94CheapStructure", fx.p0->getDefaultTeam(), 250.0f, 200.0f);
	inn->setName("InnAI");
	Object *porter = fx.spawn("L94Porter", fx.p0->getDefaultTeam(), 150.0f, 200.0f);
	DozerAIUpdate *dozerAI = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	REQUIRE(dozerAI != nullptr);

	auto hashPlayer = [&]() {
		StateHasher h;
		fx.p0->crc(h);
		return h.value();
	};
	const std::uint32_t pristineHash = hashPlayer();

	// Pristine structure is ignored by AIPlayer::repairStructure (RW 0x8F78EB checks getDamageState() != BODY_PRISTINE)
	REQUIRE(inn->getBodyModule()->getDamageState() == BODY_PRISTINE);
	fx.execAction(act("PLAYER_REPAIR_NAMED_STRUCTURE", {
		sp(11, "Alice"),
		sp(14, "InnAI"),
	}));
	CHECK(hashPlayer() == pristineHash);
	CHECK_FALSE(dozerAI->isTaskPending(DozerAIUpdate::DOZER_TASK_REPAIR));

	// Damage InnAI so getDamageState() != BODY_PRISTINE
	DamageInfo dmg;
	dmg.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	dmg.m_input.m_amount = 300.0f;
	inn->attemptDamage(dmg);
	REQUIRE(inn->getBodyModule()->getDamageState() != BODY_PRISTINE);

	fx.execAction(act("PLAYER_REPAIR_NAMED_STRUCTURE", {
		sp(11, "Alice"),
		sp(14, "InnAI"),
	}));
	const std::uint32_t queuedHash = hashPlayer();
	CHECK(queuedHash != pristineHash);
	CHECK(fx.logic->scriptEngine().stats().unportedActions.empty());

	// Duplicate repair request on the same structure is ignored (RW 0x8F78EB deduplicates)
	fx.execAction(act("PLAYER_REPAIR_NAMED_STRUCTURE", {
		sp(11, "Alice"),
		sp(14, "InnAI"),
	}));
	CHECK(hashPlayer() == queuedHash);

	// Save / load round-trip of Player (version 6 carries the AIPlayer repair queue)
	XferSave xs;
	fx.p0->xfer(xs, fx.teams);
	XferLoad xl(xs.data());
	fx.p0->xfer(xl, fx.teams);
	CHECK(hashPlayer() == queuedHash);

	// Running a logic frame ticks Player::updateAIPlayerRepair (RW 0x8F8F68) and orders the idle Porter to repair InnAI
	fx.logic->runLogicFrame();
	CHECK(dozerAI->isTaskPending(DozerAIUpdate::DOZER_TASK_REPAIR));
	CHECK(dozerAI->getTaskTarget(DozerAIUpdate::DOZER_TASK_REPAIR) == inn->getID());
}

TEST_CASE("LOCAL-94 OPTIONS_SET_DRAWICON_UI_MODE and OPTIONS_SET_PARTICLE_CAP_MODE update GameLogic, LogicSnapshot, and DrawableIconUI (RW 0x7BD55C / 0x7BD56F / 0x679134)")
{
	Local94Fx fx;
	CHECK(fx.logic->getShowDrawableIconUI());
	CHECK(fx.logic->getParticleCapMode());

	Object *bldg = fx.spawn("L94CheapStructure", fx.p0->getDefaultTeam(), 400.0f, 400.0f);
	bldg->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	bldg->setConstructionPercent(25.0f);

	GameTextTableSource text;
	text.table.setOverride("CONTROLBAR:UnderConstructionDesc", "Building: %.0f%%");
	Win32BIGFileSystem emptyFs;
	ArchiveW3DFileSource source(emptyFs);
	WW3DAssetManager assets(source);
	DrawableManager drawables(assets, *fx.logic);
	ClientEventRecorder recorder(*fx.logic);
	recorder.objectCreated(*bldg);
	drawables.applyEvents(recorder.take());
	REQUIRE(drawables.findByObject(bldg->getID()) != nullptr);

	PinholeView view({ 400.0f, 100.0f, 400.0f }, { 400.0f, 400.0f, 0.0f }, 1024, 768, 0.7f);
	IconUISettings settings;
	settings.text = &text;
	DrawableIconUI ui;
	std::vector<IconUIOp> ops;

	ui.build(*fx.logic, drawables, view, settings, {}, 0, ops);
	CHECK_FALSE(ops.empty());

	// Disable DrawIcon UI and Particle Cap Mode via script actions 300 and 301
	fx.execAction(act("OPTIONS_SET_DRAWICON_UI_MODE", { ip(8, 0) }));
	fx.execAction(act("OPTIONS_SET_PARTICLE_CAP_MODE", { ip(8, 0) }));
	CHECK_FALSE(fx.logic->getShowDrawableIconUI());
	CHECK_FALSE(fx.logic->getParticleCapMode());

	auto snapOff = LogicSnapshot::build(*fx.logic, nullptr, false, 0, fx.p0->getPlayerIndex(), false);
	REQUIRE(snapOff.get() != nullptr);
	CHECK_FALSE(snapOff->showDrawableIconUI);
	CHECK_FALSE(snapOff->particleCapMode);

	ui.build(*fx.logic, drawables, view, settings, {}, 0, ops);
	CHECK(ops.empty());

	settings.snapshot = snapOff.get();
	ui.build(*fx.logic, drawables, view, settings, {}, 0, ops);
	CHECK(ops.empty());

	// Re-enable both modes
	fx.execAction(act("OPTIONS_SET_DRAWICON_UI_MODE", { ip(8, 1) }));
	fx.execAction(act("OPTIONS_SET_PARTICLE_CAP_MODE", { ip(8, 1) }));
	CHECK(fx.logic->getShowDrawableIconUI());
	CHECK(fx.logic->getParticleCapMode());

	auto snapOn = LogicSnapshot::build(*fx.logic, nullptr, false, 0, fx.p0->getPlayerIndex(), false);
	REQUIRE(snapOn.get() != nullptr);
	CHECK(snapOn->showDrawableIconUI);
	CHECK(snapOn->particleCapMode);
	settings.snapshot = snapOn.get();
	ui.build(*fx.logic, drawables, view, settings, {}, 0, ops);
	CHECK_FALSE(ops.empty());
	CHECK(fx.logic->scriptEngine().stats().unportedActions.empty());
}

TEST_CASE("LOCAL-94 RESIZE_VIEW_GUARDBAND dispatches client request to ScriptCameraDirector (RW 0x7BD6E9 / 0x48B5FC)")
{
	Local94Fx fx;
	fx.execAction(act("RESIZE_VIEW_GUARDBAND", {
		rp(12.5f),
		rp(-8.0f),
	}));
	CHECK(fx.logic->scriptEngine().stats().unportedActions.empty());
	const std::vector<ScriptClientRequest> reqs = fx.logic->scriptEngine().takeClientRequests();
	REQUIRE(reqs.size() == 1);
	CHECK(reqs[0].action == "RESIZE_VIEW_GUARDBAND");

	ScriptCameraDirector director;
	director.apply(reqs[0], Coord3D{ 0.0f, 0.0f, 0.0f }, 0.0f);
	CHECK(director.guardBandBiasX() == 12.5f);
	CHECK(director.guardBandBiasY() == -8.0f);
	CHECK(director.unhandled().empty());
}
