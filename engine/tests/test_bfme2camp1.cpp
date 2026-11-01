// OpenBFME unit tests: lane BFME2CAMP1, the BFME2 campaigns under RotWK: the script pieces MAP GOOD Rivendell needs on the way to its first objective.
// GPL-3.0.
//
// Expected values follow the RotWK functions cited at each check (GameLogic.h, ScriptActionsUnits.cpp), never the engine's own output.

#include "doctest.h"
#include "CampaignTestUtil.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/LinearCampaign.h"
#include "GameLogic/CampaignCarryover.h"
#include "Common/Money.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWaypointPath.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/RebuildHoleModules.h"
#include "GameClient/ControlBarCommands.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "Common/PlayerHeroList.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/Economy.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/WallSpan.h"
#include "Common/Upgrade.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <algorithm>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace scripttest;

namespace
{
const char *const kBuildIni = "Object Tower\n"
							  "  KindOf = SELECTABLE STRUCTURE\n"
							  "  Buildable = Yes\n"
							  "  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
							  "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 500\n  End\n"
							  "End\n"
							  "Object Wall\n"
							  "  KindOf = SELECTABLE STRUCTURE\n"
							  "  Buildable = No\n"
							  "  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
							  "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 500\n  End\n"
							  "End\n";

void run(ScriptEngine &engine, const ScriptActionRec &a)
{
	ScriptActions::execute(engine, a);
}
} // namespace

TEST_CASE("bfme2camp1 TECHTREE_MODIFY_BUILDABILITY_OBJECT (RW 0x7BCF6C -> GameLogic RW 0x62FEE6): the template's override, which getBuildable (RW "
		  "0x73BFE8) answers before the INI's Buildable; Player::canBuild (RW 0x6AC927) follows it")
{
	ScriptGame g;
	LogicModules::registerAll(g.lw.w.modules);
	const std::string err = g.lw.w.load(kBuildIni, INI_LOAD_OVERWRITE, "units.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	g.start();
	GameLogic &logic = *g.lw.logic;
	const ThingTemplate *tower = logic.things().findTemplate("Tower");
	const ThingTemplate *wall = logic.things().findTemplate("Wall");
	REQUIRE(tower);
	REQUIRE(wall);
	Player *alice = g.lw.players.findPlayerWithName("Alice");
	// no override: the INI's byte (Yes 0, No 2)
	CHECK(BuildAssistant::buildable(*tower, logic) == 0);
	CHECK(BuildAssistant::buildable(*wall, logic) == 2);
	CHECK(BuildAssistant::playerCanBuild(*alice, tower, logic));
	const std::uint32_t before = logic.computeStateHash();
	// Rivendell's "Button removals": 2 = No (the BuildableStatus order 0 Yes, 1 Ignore_Prerequisites, 2 No, 3 Only_By_AI)
	run(g.engine(), act("TECHTREE_MODIFY_BUILDABILITY_OBJECT", { sp(15, "Tower"), ip(36, 2) }));
	CHECK(BuildAssistant::buildable(*tower, logic) == 2);
	CHECK(BuildAssistant::buildable(*tower) == 0); // the template itself is unchanged
	CHECK_FALSE(BuildAssistant::playerCanBuild(*alice, tower, logic));
	CHECK(logic.computeStateHash() != before);
	// the entry is replaced, never removed: back to Yes is an entry of 0
	run(g.engine(), act("TECHTREE_MODIFY_BUILDABILITY_OBJECT", { sp(15, "Tower"), ip(36, 0) }));
	CHECK(BuildAssistant::playerCanBuild(*alice, tower, logic));
	CHECK(logic.buildableStatusOverrides().size() == 1);
	// Ignore_Prerequisites lifts a template whose INI says No
	run(g.engine(), act("TECHTREE_MODIFY_BUILDABILITY_OBJECT", { sp(15, "Wall"), ip(36, 1) }));
	CHECK(BuildAssistant::buildable(*wall, logic) == 1);
	// an unknown template: nothing (RW 0x7BCF7D)
	run(g.engine(), act("TECHTREE_MODIFY_BUILDABILITY_OBJECT", { sp(15, "NoSuchThing"), ip(36, 2) }));
	CHECK(logic.buildableStatusOverrides().size() == 2);
	CHECK(g.engine().stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 the buildability overrides travel in obfme:GameLogic (version 2): saved, loaded, the same hash and bytes")
{
	const std::vector<std::string> core = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random" };
	logictest::LogicWorld saved;
	LogicModules::registerAll(saved.w.modules);
	REQUIRE(saved.w.load(kBuildIni, INI_LOAD_OVERWRITE, "units.ini").empty());
	logictest::LogicWorld fresh;
	LogicModules::registerAll(fresh.w.modules);
	REQUIRE(fresh.w.load(kBuildIni, INI_LOAD_OVERWRITE, "units.ini").empty());
	saved.logic->setBuildableStatusOverride(saved.logic->things().findTemplate("Tower"), 2);
	saved.logic->setBuildableStatusOverride(saved.logic->things().findTemplate("Wall"), 3);
	saved.logic->setBuildableStatusOverride(nullptr, 1); // RW 0x62FEEC: a null template sets nothing
	saved.activateUpgrades();
	fresh.activateUpgrades();
	std::vector<std::uint8_t> stream;
	saved.logic->gameState().saveBlocks(core, stream);
	REQUIRE(fresh.logic->computeStateHash() != saved.logic->computeStateHash());
	fresh.logic->gameState().loadBlocks(core, stream);
	CHECK(fresh.logic->computeStateHash() == saved.logic->computeStateHash());
	CHECK(fresh.logic->buildableStatusOverrides() == std::map<std::string, std::int32_t>{ { "Tower", 2 }, { "Wall", 3 } });
	std::vector<std::uint8_t> again;
	fresh.logic->gameState().saveBlocks(core, again);
	CHECK(again == stream);
}

namespace
{
// the version byte of obfme:GameLogic in a saveBlocks stream: the token (length byte + text), "BLOK" and the block end's offset, then the block's
// XferVersion (GameLogic::xfer's first byte)
int gameLogicVersion(const std::vector<std::uint8_t> &stream)
{
	const std::string token = "obfme:GameLogic";
	auto at = std::search(stream.begin(), stream.end(), token.begin(), token.end());
	REQUIRE(at != stream.end());
	const size_t i = (size_t)(at - stream.begin()) + token.size() + 8;
	REQUIRE(i < stream.size()); // tolerance: an index guard (the byte read next), not a result
	return stream[i];
}
} // namespace

// The archive merge into lane/wotr2 (coordinator 10-30, binding): obfme:GameLogic version 3 is BFME2CAMP1's campaign state (the buildability overrides,
// the carry-over), as the archive writes it; version 4 adds the War of the Ring battle's army tags after it (it was 3 on feature/wotr, never released)
TEST_CASE("archive x wotr: obfme:GameLogic version 3 is the campaign state and loads with no War of the Ring tags; version 4 adds the tags after it")
{
	const std::vector<std::string> core = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random" };
	auto world = [] {
		auto w = std::make_unique<logictest::LogicWorld>();
		LogicModules::registerAll(w->w.modules);
		REQUIRE(w->w.load(kBuildIni, INI_LOAD_OVERWRITE, "units.ini").empty());
		w->activateUpgrades();
		return w;
	};
	auto tag = [](int army) {
		LivingWorldBattleTags::Tag t;
		t.armyId = army;
		t.spawnId = "Spawn" + std::to_string(army);
		t.buildingId = 0;
		return t;
	};
	// nothing of either: version 1 (the bytes of a game before both)
	{
		auto plain = world();
		std::vector<std::uint8_t> stream;
		plain->logic->gameState().saveBlocks(core, stream);
		CHECK(gameLogicVersion(stream) == 1);
	}
	// a campaign save: version 3, as the archive writes it; a game that held War of the Ring tags loads it with none (a version 3 has no tags)
	auto campaign = world();
	campaign->logic->setBuildableStatusOverride(campaign->logic->things().findTemplate("Tower"), 2);
	std::vector<std::uint8_t> v3;
	campaign->logic->gameState().saveBlocks(core, v3);
	CHECK(gameLogicVersion(v3) == 3);
	auto loader = world();
	loader->logic->livingWorldTags().active = true;
	loader->logic->livingWorldTags().objects[7] = tag(11);
	loader->logic->gameState().loadBlocks(core, v3);
	CHECK_FALSE(loader->logic->livingWorldTags().inUse());
	CHECK(loader->logic->buildableStatusOverrides() == std::map<std::string, std::int32_t>{ { "Tower", 2 } });
	CHECK(loader->logic->computeStateHash() == campaign->logic->computeStateHash());
	// a War of the Ring battle save: version 4, the campaign state and the tags both come back, and it saves to the same bytes
	auto battle = world();
	battle->logic->setBuildableStatusOverride(battle->logic->things().findTemplate("Wall"), 3);
	battle->logic->livingWorldTags().active = true;
	battle->logic->livingWorldTags().objects[5] = tag(21);
	std::vector<std::uint8_t> v4;
	battle->logic->gameState().saveBlocks(core, v4);
	CHECK(gameLogicVersion(v4) == 4);
	auto back = world();
	back->logic->gameState().loadBlocks(core, v4);
	CHECK(back->logic->livingWorldTags().active);
	REQUIRE(back->logic->livingWorldTags().object(5));
	CHECK(back->logic->livingWorldTags().object(5)->armyId == 21);
	CHECK(back->logic->livingWorldTags().object(5)->spawnId == "Spawn21");
	CHECK(back->logic->buildableStatusOverrides() == std::map<std::string, std::int32_t>{ { "Wall", 3 } });
	CHECK(back->logic->computeStateHash() == battle->logic->computeStateHash());
	std::vector<std::uint8_t> again;
	back->logic->gameState().saveBlocks(core, again);
	CHECK(again == v4);
	// War of the Ring tags without campaign state: still version 4 (the campaign part is written empty)
	auto tagsOnly = world();
	tagsOnly->logic->livingWorldTags().active = true;
	std::vector<std::uint8_t> v4b;
	tagsOnly->logic->gameState().saveBlocks(core, v4b);
	CHECK(gameLogicVersion(v4b) == 4);
}

TEST_CASE("bfme2camp1 CampaignCarryover: beginCampaign (RW 0x5EC4EC) makes one record of quantity 1 per CarryoverUnit in INI order; the mission's "
		  "DelayCarryoverSpawningOf holds a record back by its exact name (RW 0x5EAFDD)")
{
	const CampaignCarryover::State st = CampaignCarryover::beginCampaign({ "ElvenGlorfindel", "DwarvenGloin", "ElvenElrond" });
	const std::vector<CampaignCarryover::Record> &r = st.records;
	REQUIRE(r.size() == 3);
	CHECK(r[0].templateName == "ElvenGlorfindel");
	CHECK(r[2].templateName == "ElvenElrond");
	CHECK(r[1].quantity == 1);
	CHECK(r[1].experience == 0.0f); // the record ctor RW 0x7803B6
	CHECK_FALSE(r[1].upgrades.any());
	CHECK(st.used.empty());
	CHECK(st.skillPoints == 0.0f); // the LinearCampaign ctor RW 0x5EB223
	CHECK(st.bountyShare == 0.0f);
	const std::vector<std::string> delayed = { "ElvenElrond", "DwarvenGloin" };
	CHECK_FALSE(CampaignCarryover::isDelayed(delayed, r[0]));
	CHECK(CampaignCarryover::isDelayed(delayed, r[1]));
	CHECK_FALSE(CampaignCarryover::isDelayed({ "elvenglorfindel" }, r[0])); // AsciiString == (RW 0x4065D4): case-sensitive
}

TEST_CASE("bfme2camp1 CampaignCarryover: the spawn (RW 0x5EBD76) goes to the first human player, places at Player_1_Start facing Player_1_Start_Rally "
		  "(RW 0x781B17 / 0x781FAF), marks the object (Object + 0x458 bit 4) and moves the record to the used ones")
{
	ScriptGame g;
	LogicModules::registerAll(g.lw.w.modules);
	// templates without a body (the test world has no GameData damage thresholds)
	REQUIRE(g.lw.w.load("Object Tower\n  KindOf = SELECTABLE\nEnd\nObject Wall\n  KindOf = SELECTABLE\nEnd\n", INI_LOAD_OVERWRITE, "units.ini").empty());
	GameLogic &logic = *g.lw.logic;
	// no start waypoint: (0, 0, 0) and (1, 0, 0), false (RW 0x782339 .. / 0x781B17)
	Coord3D start, rally;
	CHECK_FALSE(CampaignCarryover::startPosWaypointLocations(logic, start, rally));
	CHECK(start.x == 0.0f);
	CHECK(rally.x == 1.0f);
	g.lw.players.findPlayerWithName("Alice")->setBountyPercent(0.5f);
	const std::uint32_t before = logic.computeStateHash();
	CampaignCarryover::State st = CampaignCarryover::beginCampaign({ "Tower", "Wall", "NoSuchThing" });
	st.used.push_back(st.records[0]); // a previous mission's used list: cleared first (RW 0x5EB533)
	st.bountyShare = 0.25f;
	const CampaignCarryover::SpawnResult r = CampaignCarryover::addCarryoverStuffToMap(logic, g.lw.players, st, { "Wall" });
	REQUIRE(r.player != nullptr);
	CHECK(r.player->getPlayerType() == PLAYER_HUMAN);
	REQUIRE(r.objects.size() == 1); // Wall delayed, NoSuchThing makes nothing (RW 0x7800DC) and stays
	CHECK(r.objects[0]->isCampaignCarryover());
	CHECK(r.objects[0]->getControllingPlayer() == r.player);
	REQUIRE(st.used.size() == 1);
	CHECK(st.used[0].templateName == "Tower");
	REQUIRE(st.records.size() == 2);
	CHECK(st.records[0].templateName == "Wall");
	CHECK(st.records[1].templateName == "NoSuchThing");
	{
		const std::vector<std::string> errors = logic.report().errors; // the missing template is reported, not dropped silently (lane BFME2CAMP1 review)
		CHECK(std::find(errors.begin(), errors.end(), "campaign carry-over: no template 'NoSuchThing' for a carried unit (kept, not created)") != errors.end());
	}
	CHECK(r.player->getBountyPercent() == 0.25f); // RW 0x6AA847(+ 0xB0)
	// the placer without a start: the object at (0, 0, 0), facing atan2(0, 1) = 0
	CHECK(r.objects[0]->getPosition()->x == 0.0f);
	CHECK(r.objects[0]->getOrientation() == 0.0f);
	CHECK(logic.computeStateHash() != before);
}

TEST_CASE("bfme2camp1 CampaignCarryover: a record's experience above the new object's is given (RW 0x7801E7 .. 0x79D8EF) with the leveled byte cleared, "
		  "and its upgrade mask is OR-ed in (RW 0x68CC6C)")
{
	ScriptGame g;
	LogicModules::registerAll(g.lw.w.modules);
	REQUIRE(g.lw.w.load(kBuildIni, INI_LOAD_OVERWRITE, "units.ini").empty());
	g.lw.logic->settings().bodyThresholdsLoaded = true; // GameData's UnitDamagedThreshold / UnitReallyDamagedThreshold (the bodies use the defaults)
	GameLogic &logic = *g.lw.logic;
	CampaignCarryover::State st = CampaignCarryover::beginCampaign({ "Tower" });
	st.records[0].experience = 250.0f;
	st.records[0].upgrades.set(5);
	const CampaignCarryover::SpawnResult r = CampaignCarryover::addCarryoverStuffToMap(logic, g.lw.players, st, {});
	REQUIRE(r.objects.size() == 1);
	const ExperienceTracker *xp = r.objects[0]->getExperienceTracker();
	REQUIRE(xp != nullptr);
	CHECK(xp->getExperience() == 250.0f);
	CHECK(r.objects[0]->getUpgradeMask().test(5));
	REQUIRE(st.used.size() == 1);
	CHECK(st.used[0].experience == 250.0f); // the record moves as it was
}

TEST_CASE("bfme2camp1 CampaignCarryover: the won mission (RW 0x5EBAEE) appends a record per live carry-over object (list order, quantity 1) after the "
		  "records left, then the revival lists' flagged entries; the used records are cleared and the first human's bounty share kept")
{
	ScriptGame g;
	LogicModules::registerAll(g.lw.w.modules);
	REQUIRE(g.lw.w.load(kBuildIni, INI_LOAD_OVERWRITE, "units.ini").empty());
	g.lw.logic->settings().bodyThresholdsLoaded = true; // GameData's UnitDamagedThreshold / UnitReallyDamagedThreshold (the bodies use the defaults)
	g.start();
	GameLogic &logic = *g.lw.logic;
	Player *alice = g.lw.players.findPlayerWithName("Alice");
	REQUIRE(alice);
	CampaignCarryover::State st = CampaignCarryover::beginCampaign({ "Tower", "Wall", "Tower" });
	CampaignCarryover::addCarryoverStuffToMap(logic, g.lw.players, st, { "Wall" });
	REQUIRE(st.used.size() == 2);
	std::vector<Object *> carried;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isCampaignCarryover())
		{
			carried.push_back(o);
		}
	}
	REQUIRE(carried.size() == 2);
	carried[0]->kill(DAMAGE_UNRESISTABLE, 0); // a dead carry-over object is not kept (Object + 0x458 bit 0, RW 0x5EBB29)
	REQUIRE(carried[0]->isEffectivelyDead());
	if (ExperienceTracker *xp = carried[1]->getExperienceTracker())
	{
		xp->setExperienceAndLevel(40.0f, true);
	}
	HeroRecord dead; // a revive entry made from a dying carry-over hero (RW 0x780E3F: + 0xB1)
	dead.templateName = "Wall";
	dead.experience = 7.0f;
	dead.rank = 2;
	dead.objectFlag = true;
	alice->heroes().addRecord(dead);
	HeroRecord other = dead; // an entry without the flag: not kept
	other.objectFlag = false;
	other.templateName = "Tower";
	alice->heroes().addRecord(other);
	alice->setBountyPercent(0.75f);
	CampaignCarryover::onCurrentMissionWon(logic, g.lw.players, st);
	REQUIRE(st.records.size() == 3);
	CHECK(st.records[0].templateName == "Wall"); // the delayed record left in + 0xB4 comes first
	CHECK(st.records[1].templateName == "Tower");
	CHECK(st.records[1].quantity == 1);
	CHECK(st.records[1].experience == carried[1]->getExperienceTracker()->getExperience());
	CHECK(st.records[2].templateName == "Wall");
	CHECK(st.records[2].experience == 7.0f);
	CHECK(st.records[2].rank == 2);
	CHECK(st.used.empty());
	CHECK(st.bountyShare == 0.75f);
	// a mission lost and replayed (RW 0x5EC5D1): the used records back after the left ones
	CampaignCarryover::State replay = CampaignCarryover::beginCampaign({ "Tower", "Wall" });
	CampaignCarryover::addCarryoverStuffToMap(logic, g.lw.players, replay, { "Wall" });
	CampaignCarryover::restartMission(replay);
	REQUIRE(replay.records.size() == 2);
	CHECK(replay.records[0].templateName == "Wall");
	CHECK(replay.records[1].templateName == "Tower");
	CHECK(replay.used.empty());
}

TEST_CASE("bfme2camp1 CampaignCarryover: the delayed spawn (RW 0x5EBF92) takes the first record named, copies it to the used ones and decrements or erases "
		  "it; HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE (RW 0x5EA888) asks for a record left; CREATE_DELAYED_CARRYOVER_UNIT_AT_WAYPOINT places and names it")
{
	ScriptGame g;
	LogicModules::registerAll(g.lw.w.modules);
	REQUIRE(g.lw.w.load(kBuildIni, INI_LOAD_OVERWRITE, "units.ini").empty());
	g.lw.logic->settings().bodyThresholdsLoaded = true; // GameData's UnitDamagedThreshold / UnitReallyDamagedThreshold (the bodies use the defaults)
	g.start();
	GameLogic &logic = *g.lw.logic;
	Player *alice = g.lw.players.findPlayerWithName("Alice");
	CampaignCarryover::State st = CampaignCarryover::beginCampaign({ "Tower", "Wall" });
	st.records[1].quantity = 2;
	CHECK(CampaignCarryover::hasDelayed(st, { "Wall" }));
	CHECK_FALSE(CampaignCarryover::hasDelayed(st, { "wall" }));
	Object *a = CampaignCarryover::spawnDelayed(logic, st, { "Wall", "Tower" }, *alice);
	REQUIRE(a != nullptr);
	CHECK(static_cast<const ThingTemplate *>(a->getTemplate())->getName() == "Tower"); // the first record named, not the first name
	CHECK(a->isCampaignCarryover());
	REQUIRE(st.records.size() == 1);
	Object *b = CampaignCarryover::spawnDelayed(logic, st, { "Wall" }, *alice);
	REQUIRE(b != nullptr);
	REQUIRE(st.records.size() == 1);
	CHECK(st.records[0].quantity == 1);
	REQUIRE(st.used.size() == 2);
	CHECK(st.used[1].quantity == 2); // the copy is taken before the decrement (RW 0x5EB78F)
	// the script pieces read the game's carry-over (the action's placement at a waypoint is the retail High Pass test's: this world has no map)
	CHECK_FALSE(ScriptConditions::evaluate(g.engine(), cond("HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE", { sp(61, "Wall"), sp(11, "Alice") }))); // no campaign
	logic.setCampaignCarryover(st);
	CHECK(ScriptConditions::evaluate(g.engine(), cond("HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE", { sp(61, "Wall"), sp(11, "Alice") })));
	CHECK_FALSE(ScriptConditions::evaluate(g.engine(), cond("HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE", { sp(61, "Tower"), sp(11, "Alice") })));
	// no such waypoint: nothing, the record stays (RW 0x7CAA99)
	run(g.engine(), act("CREATE_DELAYED_CARRYOVER_UNIT_AT_WAYPOINT", { sp(61, "Wall"), sp(11, "Alice"), sp(7, "Gate"), sp(54, "Gloin") }));
	CHECK(logic.campaignCarryover()->records.size() == 1);
	CHECK(g.engine().getUnitNamed("Gloin") == nullptr);
	CHECK(g.engine().stats().unportedActions.empty());
	CHECK(g.engine().stats().unportedConditions.empty());
}

TEST_CASE("bfme2camp1 CampaignCarryover: the state's text form reads back exactly (floats by their bits) and refuses anything malformed; the game "
		  "keeps it in obfme:GameLogic (version 3) and its hash")
{
	CampaignCarryover::State st = CampaignCarryover::beginCampaign({ "ElvenGlorfindel", "Dwarven Gloin" });
	st.records[0].experience = 0.1f;
	st.records[0].rank = 3;
	st.records[0].upgrades.set(0);
	st.records[0].upgrades.set(0x47F);
	st.used.push_back(st.records[1]);
	st.skillPoints = 1234.5f;
	st.bountyShare = -0.0f;
	st.sciences = { "SCIENCE_Elves", "SCIENCE_ElvenAllies" };
	st.upgrades = { "Upgrade_ElvenMithrilMail" };
	CampaignCarryover::State back;
	std::string why;
	REQUIRE_MESSAGE(CampaignCarryover::State::parse(st.serialize(), back, &why), why);
	CHECK(back == st);
	CHECK(back.records[1].templateName == "Dwarven Gloin"); // a name with a space
	CHECK(std::signbit(back.bountyShare));
	for (const std::string bad : { std::string(""), std::string("skill 0\n"), std::string("skill 0\nbounty 0\nrecord left 0 0 1 0 X\n"),
			 std::string("skill 0\nbounty 0\nbogus 1\n"), std::string("skill zz\nbounty 0\n"), std::string("skill 0\nskill 0\nbounty 0\n"),
			 std::string("skill 0\nbounty 0\nrecord left 0 0 1 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, X\n") })
	{
		CHECK_FALSE(CampaignCarryover::State::parse(bad, back, &why));
	}
	const std::vector<std::string> core = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random" };
	logictest::LogicWorld saved;
	LogicModules::registerAll(saved.w.modules);
	REQUIRE(saved.w.load(kBuildIni, INI_LOAD_OVERWRITE, "units.ini").empty());
	logictest::LogicWorld fresh;
	LogicModules::registerAll(fresh.w.modules);
	REQUIRE(fresh.w.load(kBuildIni, INI_LOAD_OVERWRITE, "units.ini").empty());
	saved.activateUpgrades();
	fresh.activateUpgrades();
	const std::uint32_t without = saved.logic->computeStateHash();
	saved.logic->setCampaignCarryover(st);
	CHECK(saved.logic->computeStateHash() != without);
	std::vector<std::uint8_t> stream;
	saved.logic->gameState().saveBlocks(core, stream);
	fresh.logic->gameState().loadBlocks(core, stream);
	REQUIRE(fresh.logic->campaignCarryover() != nullptr);
	CHECK(*fresh.logic->campaignCarryover() == st);
	CHECK(fresh.logic->computeStateHash() == saved.logic->computeStateHash());
}

// ---- the retail data (ROTWK_INSTALL + BFME2_INSTALL; SKIP without them) ----------------------------------------------------------------------------------
namespace
{
// a MARK_MISSION_OBJECTIVE_COMPLETED request for objective `n` (its INT parameter, the map.ini list's 1-based index)
bool objectiveMarked(const campaigntest::Mission &m, int n)
{
	for (const ScriptClientRequest &r : m.requests)
	{
		if (r.action == "MARK_MISSION_OBJECTIVE_COMPLETED" && !r.params.empty() && r.params[0].intValue == n)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("bfme2camp1 retail: BFME2's GOOD_CAMPAIGN (data\\ini\\linearcampaign.ini through BFME2's legend) follows RotWK's campaigns: eight missions from "
		  "Rivendell to Dol Guldur, EVIL_CAMPAIGN eight from Lothlorien to Rivendell")
{
	OPENBFME_REQUIRE_START(s);
	const LinearCampaignManager &m = s->world->linearCampaigns();
	const LinearCampaign *good = m.find("good_campaign"); // RW 0x5EB010: compareNoCase
	const LinearCampaign *evil = m.find("EVIL_CAMPAIGN");
	REQUIRE(good != nullptr);
	REQUIRE(evil != nullptr);
	std::vector<std::string> maps;
	for (const LinearCampaignMission &mi : good->missions)
	{
		maps.push_back(mi.map);
	}
	CHECK(maps == std::vector<std::string>{ "MAP GOOD Rivendell", "MAP GOOD High Pass", "MAP GOOD Ettenmoors", "MAP GOOD Blue Mountain", "MAP GOOD Grey Havens",
					  "MAP GOOD Celduin", "MAP GOOD Erebor", "MAP GOOD Dol Guldur" });
	CHECK(good->overallIntroMovie == "Good_Campaign_Intro");
	CHECK(good->missions[0].introMovie == "Good_Rivendell_Intro");
	CHECK(good->missions[0].loadScreenImage == "CampaignGood_Rivendell_LoadScreen");
	REQUIRE(evil->missions.size() == 8);
	CHECK(evil->missions[0].map == "MAP EVIL Lorien");
	CHECK(evil->missions[7].map == "MAP EVIL Rivendell");
	CHECK(evil->overallIntroMovie == "Evil_Campaign_Intro");
}

TEST_CASE("bfme2camp1 retail: MAP GOOD Rivendell plays from its start to its first objective (Glorfindel and his elves at the gate of Rivendell) by its own "
		  "scripts, the player's units walking there")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.loadCampaignMission(*s, "GOOD_CAMPAIGN", 0);
	REQUIRE(m.game->mapScriptsRunning());
	// the campaign's carry-over (RW 0x5EBD76): of the seven CarryoverUnit heroes Rivendell delays six; ElvenGlorfindel is made for the human player
	// PlyrElves at Player_1_Start, turned towards Player_1_Start_Rally (RW 0x781FAF)
	REQUIRE(m.game->report().carryoverObjects == 1);
	REQUIRE(m.logic().campaignCarryover() != nullptr);
	{
		const Object *g = nullptr;
		for (const Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->isCampaignCarryover() && static_cast<const ThingTemplate *>(o->getTemplate())->getName() == "ElvenGlorfindel")
			{
				g = o;
			}
		}
		REQUIRE(g != nullptr); // (Gloin is the map's delayed spawn at its first frame: 'Spawn Gloin', CREATE_DELAYED_CARRYOVER_UNIT_AT_WAYPOINT)
		CHECK(g->getControllingPlayer()->getPlayerName() == "PlyrElves");
		Coord3D start, rally;
		REQUIRE(CampaignCarryover::startPosWaypointLocations(m.logic(), start, rally));
		// Player_1_Start; no Player_1_Start_Rally on this map: the rally is the waypoint and the start 10 to its east (RW 0x781B17), facing west (pi)
		const float dx = g->getPosition()->x - start.x, dy = g->getPosition()->y - start.y;
		CHECK(dx * dx + dy * dy < 20.0f * 20.0f); // tolerance: read after the load's first logic frame, in which the hero's AI may already have stepped (the exact placement is the synthetic test's)
		CHECK(rally.x + 10.0f == start.x);
		MESSAGE("Glorfindel at (" << g->getPosition()->x << ", " << g->getPosition()->y << "), start (" << start.x << ", " << start.y << "), rally (" << rally.x
								  << ", " << rally.y << ")");
	}
	// the opening: 'Set Hero references' names Glorfindel (SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER), 'Intro Text' shows objective 1's box
	REQUIRE(m.until([&] { return m.unit("Glorfindel") != nullptr; }, 600));
	MESSAGE("Glorfindel named at frame " << m.frame() << ", " << m.engine().stats().scriptsFired << " scripts fired");
	m.step(5);
	// the opening's "Button removals" (TECHTREE_MODIFY_BUILDABILITY_OBJECT x7, RW 0x7BCF6C) made their overrides
	CHECK_FALSE(m.logic().buildableStatusOverrides().empty());
	CHECK(m.requested("DISPLAY_NOTIFICATION_BOX", "NewObjective"));
	Object *glorfindel = m.unit("Glorfindel");
	REQUIRE(glorfindel != nullptr);
	const Team *starting = ScriptConditions::team(m.engine(), "PlyrElves/Starting Units");
	REQUIRE(starting != nullptr);
	const Coord3D gate = m.centreOf("Gate of riv AREA");
	CHECK_FALSE(objectiveMarked(m, 1));
	// the player orders Glorfindel and the starting units to the gate (a player's move, CMD_FROM_PLAYER) and repeats it while they idle (a fight on the
	// way stops a move)
	auto order = [&] {
		std::vector<Object *> units = ScriptConditions::members(*starting);
		if (std::find(units.begin(), units.end(), glorfindel) == units.end())
		{
			units.push_back(glorfindel);
		}
		for (Object *o : units)
		{
			AIUpdateInterface *ai = o->getAIUpdateInterface();
			if (ai && !o->isEffectivelyDead() && ai->isIdle())
			{
				ai->aiMoveToPosition(gate, CMD_FROM_PLAYER);
			}
		}
	};
	glorfindel->getBodyModule()->setIndestructible(true); // the test plays objective 1 only: Glorfindel's death would lose the mission
	// the way to the gate crosses the river on the map's bridge (RivBridge, about (2869, 1702)), a wall top with ramps (lane BRIDGE-1, S-161): the units
	// walk over it on their own and on into "Gate of riv AREA"
	bool crossed = false;
	const bool done = m.until(
		[&] {
			if (m.frame() % 10 == 0)
			{
				order();
			}
			if (!crossed && glorfindel->getPosition()->y > 1810.0f) // tolerance: the far side of the river (the bridge's centre is at y 1702)
			{
				crossed = true;
				CHECK_FALSE(objectiveMarked(m, 1)); // not before the bridge
				MESSAGE("Glorfindel crossed the bridge at frame " << m.frame());
			}
			return objectiveMarked(m, 1);
		},
		6000);
	MESSAGE("objective 1 at frame " << m.frame());
	if (!done)
	{
		for (Object *o : ScriptConditions::members(*starting))
		{
			MESSAGE("left: " << static_cast<const ThingTemplate *>(o->getTemplate())->getName() << " at (" << o->getPosition()->x << ", " << o->getPosition()->y
							 << ", " << o->getPosition()->z << ")" << std::string(o->isEffectivelyDead() ? " dead" : "") << std::string(o->getContainedBy() ? " contained" : "")
							 << " idle " << (o->getAIUpdateInterface() && o->getAIUpdateInterface()->isIdle()));
		}
		MESSAGE("Glorfindel at (" << glorfindel->getPosition()->x << ", " << glorfindel->getPosition()->y << "), the gate area's centre (" << gate.x << ", " << gate.y << ")");
	}
	REQUIRE(done);
	{
		// 'First recruit' (a Lorien warrior horde of PlyrRivendell) joined "PlyrElves/Starting Units" by UNIT_SET_TEAM once it sighted the player's units:
		// Object::setTeam (RW 0x69954A) hands the team to its members too (horde interface slot 0x170, RW 0x870667), so they walked with it to the gate
		Object *recruit = m.unit("First recruit");
		REQUIRE(recruit != nullptr);
		CHECK(recruit->getControllingPlayer()->getPlayerName() == "PlyrElves");
		int members = 0;
		for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getContainedBy() == recruit)
			{
				++members;
				CHECK(o->getTeam() == recruit->getTeam());
			}
		}
		CHECK(members > 0); // tolerance: the horde had 8 members on its way, some may fall to the goblins before the gate
	}
	CHECK(m.engine().endRequests().empty()); // neither won nor lost
	// what follows the objective (the same script): objective 2 is shown and its timer opens the gate (GATE_OPEN "Rivendell Gate")
	bool shown2 = false;
	for (const ScriptClientRequest &r : m.requests)
	{
		shown2 = shown2 || (r.action == "SHOW_MISSION_OBJECTIVE" && !r.params.empty() && r.params[0].intValue == 2);
	}
	CHECK(shown2);
	CHECK(crossed);
	const ScriptEngine::Stats &st = m.engine().stats();
	for (const auto &kv : st.unportedConditions)
	{
		MESSAGE("unported condition " << kv.first << " x" << kv.second);
	}
	for (const auto &kv : st.unportedActions)
	{
		MESSAGE("unported action " << kv.first << " x" << kv.second);
	}
}

namespace
{
// MAP GOOD Rivendell from its start until its scripts name Glorfindel, who gets `experience` (as if he had fought on the way); then the mission is won
// (VICTORY, RW 0x7C45E0 -> RW 0x5EBAEE) and the campaign's carry-over after it is returned
CampaignCarryover::State rivendellWon(starttest::Shared &s, float experience, float *given)
{
	campaigntest::Mission m;
	m.loadCampaignMission(s, "GOOD_CAMPAIGN", 0);
	REQUIRE(m.until([&] { return m.unit("Glorfindel") != nullptr; }, 600));
	ExperienceTracker *xp = m.unit("Glorfindel")->getExperienceTracker();
	REQUIRE(xp != nullptr);
	xp->setExperienceAndLevel(experience, true);
	if (given)
	{
		*given = xp->getExperience();
	}
	ScriptActions::execute(m.engine(), act("VICTORY", {}));
	REQUIRE(m.logic().campaignCarryover() != nullptr);
	return *m.logic().campaignCarryover();
}

GameMessage selectMessage(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}

GameMessage constructMessage(int player, const ThingTemplate &t, const Coord3D &loc)
{
	GameMessage m(MSG_DOZER_CONSTRUCT, player);
	m.appendIntegerArgument((int)t.getTemplateID());
	m.appendLocationArgument(loc);
	m.appendRealArgument(0.0f);
	return m;
}

GameMessage trainMessage(int player, const ThingTemplate &t)
{
	GameMessage m(MSG_QUEUE_UNIT_CREATE, player);
	m.appendBooleanArgument(false);
	m.appendIntegerArgument((int)t.getTemplateID());
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(false);
	m.appendBooleanArgument(false);
	return m;
}
} // namespace

TEST_CASE("bfme2camp1 retail: MAP GOOD Rivendell plays from its start to its victory by its own scripts (the gate, Elrond's house, the two goblin waves "
		  "and Arwen, the ride out, the Wyrm), the player's units walking and attacking")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.loadCampaignMission(*s, "GOOD_CAMPAIGN", 0);
	REQUIRE(m.game->mapScriptsRunning());
	REQUIRE(m.until([&] { return m.unit("Glorfindel") != nullptr; }, 600));
	Object *glorfindel = m.unit("Glorfindel");
	const Player *me = glorfindel->getControllingPlayer();
	REQUIRE(me != nullptr);
	const Player *wild = m.logic().players().findPlayerWithName("PlyrWild");
	REQUIRE(wild != nullptr);
	// the player's army: its objects with an AI that a player can order (no horde members, nothing inside a container, no structures)
	auto army = [&] {
		std::vector<Object *> out;
		for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == me && o->getAIUpdateInterface() && !o->isEffectivelyDead() && !o->getContainedBy() &&
				!o->isKindOfName("STRUCTURE")) // a horde's members are contained by their horde
			{
				out.push_back(o);
			}
		}
		return out;
	};
	auto heroesSafe = [&] {
		for (Object *o : army())
		{
			if (o->isKindOfName("HERO") && o->getBodyModule())
			{
				o->getBodyModule()->setIndestructible(true); // the test plays the objectives, not the fights: a fallen hero would lose the mission
			}
		}
	};
	// an idle unit is ordered again (a fighting one is left to its fight); `guarding`: a hero guarding its spot in its stance too (the guard state is not
	// idle). Hordes are not re-ordered out of their guard: a new order every 10 frames keeps a horde from setting off
	auto moveIdle = [&](const std::vector<Object *> &units, const Coord3D &to, bool guarding = false) {
		for (Object *o : units)
		{
			AIUpdateInterface *ai = o->getAIUpdateInterface();
			if (ai && (ai->isIdle() || (guarding && ai->currentStateId() == (unsigned)AI_GUARD)))
			{
				ai->aiMoveToPosition(to, CMD_FROM_PLAYER);
			}
		}
	};
	auto where = [&](const char *stage) {
		const AIUpdateInterface *ai = glorfindel->getAIUpdateInterface();
		MESSAGE(std::string(stage) << ": frame " << m.frame() << ", Glorfindel at (" << glorfindel->getPosition()->x << ", " << glorfindel->getPosition()->y << ") state "
					  << (ai ? (int)ai->currentStateId() : -1) << ", input " << std::string(m.requested("DISABLE_INPUT") ? "disabled once" : "never disabled"));
	};
	// 1. the gate (objective 1)
	heroesSafe();
	const Coord3D gate = m.centreOf("Gate of riv AREA");
	const bool atGate = m.until(
		[&] {
			if (m.frame() % 10 == 0)
			{
				moveIdle(army(), gate);
			}
			return objectiveMarked(m, 1);
		},
		6000);
	if (!atGate)
	{
		where("the gate not reached");
		for (Object *o : army())
		{
			MESSAGE("  " << static_cast<const ThingTemplate *>(o->getTemplate())->getName() << " '" << o->getName() << "' at (" << o->getPosition()->x << ", "
						 << o->getPosition()->y << ") state " << (int)o->getAIUpdateInterface()->currentStateId());
		}
	}
	REQUIRE(atGate);
	MESSAGE("objective 1 (the gate) at frame " << m.frame());
	// 2. Elrond's house (objective 2): the gate opens on its timer, Glorfindel walks in
	const Coord3D house = m.centreOf("Glorfindel at house AREA");
	auto gateOpen = [&] {
		Object *g = m.unit("Rivendell Gate");
		const GateOpenAndCloseBehavior *b = g ? dynamic_cast<const GateOpenAndCloseBehavior *>(g->findModule("GateOpenAndCloseBehavior")) : nullptr;
		return b && b->isOpen();
	};
	REQUIRE(m.until([&] { return gateOpen(); }, 600)); // 'Move to the house': GATE_OPEN "Rivendell Gate" on its timer
	MESSAGE("the gate of Rivendell opens at frame " << m.frame());
	{
		// RivGate's `GeometryName = "Closed"` / "OpenLeft" / "OpenRight": INI::parseAsciiString drops the quotes (getNextAsciiString), so the open gate's
		// "Closed" shape is off and its door leaves on (GateOpenAndCloseBehavior's GeometryForOpen / GeometryForClosed defaults)
		Object *g = m.unit("Rivendell Gate");
		REQUIRE(g != nullptr);
		const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*g->getTemplate());
		REQUIRE(shapes.size() == 5);
		REQUIRE(g->geometryActive().size() == 5);
		CHECK(shapes[2].name == "Closed");
		CHECK(shapes[3].name == "OpenLeft");
		CHECK(shapes[4].name == "OpenRight");
		CHECK(g->geometryActive()[2] == 0);
		CHECK(g->geometryActive()[3] == 1);
		CHECK(g->geometryActive()[4] == 1);
	}
	std::vector<int> states;
	const bool atHouse = m.until(
		[&] {
			if (m.frame() % 10 == 0)
			{
				moveIdle({ glorfindel }, house, true);
				std::vector<Object *> others = army();
				others.erase(std::remove(others.begin(), others.end(), glorfindel), others.end());
				moveIdle(others, house); // the army follows him in (and leaves the gateway)
			}
			if (states.size() < 40)
			{
				states.push_back((int)glorfindel->getAIUpdateInterface()->currentStateId());
			}
			return objectiveMarked(m, 2);
		},
		6000);
	if (!atHouse)
	{
		where("Elrond's house not reached");
		MESSAGE("the house area's centre (" << house.x << ", " << house.y << "), the gate open: " << gateOpen());
		std::string seq;
		for (int st : states)
		{
			seq += std::to_string(st) + " ";
		}
		MESSAGE("Glorfindel's first states after the gate opened: " << seq);
	}
	REQUIRE(atHouse);
	MESSAGE("objective 2 (Elrond's house) at frame " << m.frame());
	// 3. the goblin waves: the army attacks a wave's closest member (TEAM_ATTACKED_BY_PLAYER: "Arwen Spawn!"); a wave the player has fought for 300 frames is
	// then destroyed by the test (the fight itself is not the point here)
	std::map<std::string, unsigned> fought;
	const char *const waves[] = { "PlyrWild/first wave baddies", "PlyrWild/Second Wave of Baddies" };
	auto fightWaves = [&] {
		heroesSafe();
		for (const char *wname : waves)
		{
			Team *t = ScriptConditions::team(m.engine(), wname);
			const std::vector<Object *> members = t ? ScriptConditions::members(*t) : std::vector<Object *>();
			if (members.empty())
			{
				continue;
			}
			if (!fought.count(wname))
			{
				fought[wname] = m.frame();
				MESSAGE(std::string(wname) << " met at frame " << m.frame());
			}
			if (m.frame() > fought[wname] + 300)
			{
				m.killTeam(wname);
				continue;
			}
			for (Object *o : army())
			{
				AIUpdateInterface *ai = o->getAIUpdateInterface();
				if (!ai || !ai->isIdle())
				{
					continue;
				}
				Object *best = nullptr;
				float bestD = 0.0f;
				for (Object *e : members)
				{
					const float dx = e->getPosition()->x - o->getPosition()->x, dy = e->getPosition()->y - o->getPosition()->y;
					const float d = dx * dx + dy * dy;
					if (!best || d < bestD)
					{
						best = e;
						bestD = d;
					}
				}
				if (best)
				{
					ai->aiAttackObject(best, CMD_FROM_PLAYER);
				}
			}
		}
	};
	REQUIRE(m.until(
		[&] {
			if (m.frame() % 10 == 0)
			{
				fightWaves();
			}
			return m.unit("Arwen") != nullptr;
		},
		20000));
	MESSAGE("Arwen comes at frame " << m.frame());
	REQUIRE(m.until(
		[&] {
			if (m.frame() % 10 == 0)
			{
				fightWaves();
			}
			return m.flag("PlyrWild/second wave dead FLAG");
		},
		6000));
	MESSAGE("the second wave is dead at frame " << m.frame());
	// 4. the defence is over (objective 3, 'Defense complete'); Arwen's cavalry rides in and joins the player ('Arwen switch teams': UNIT_SET_TEAM,
	// TEAM_TRANSFER_TO_PLAYER); her lines run, then the ride out shows objective 4
	REQUIRE(m.until([&] { return objectiveMarked(m, 3); }, 6000));
	MESSAGE("the defence (objective 3) at frame " << m.frame());
	REQUIRE(m.until([&] { return m.unit("Arwen") && m.unit("Arwen")->getControllingPlayer() == me; }, 3000));
	MESSAGE("Arwen joins the player at frame " << m.frame());
	auto objectiveShown = [&](int n) {
		for (const ScriptClientRequest &r : m.requests)
		{
			if (r.action == "SHOW_MISSION_OBJECTIVE" && !r.params.empty() && r.params[0].intValue == n)
			{
				return true;
			}
		}
		return false;
	};
	REQUIRE(m.until([&] { return objectiveShown(4); }, 3000));
	MESSAGE("ride out (objective 4 shown) at frame " << m.frame());
	// 5. the goblins' town falls: every PlyrWild object of "player wild LIST" (the caves, the fissure, the mills, the trove, the towers, their armies, the
	// Wyrm the fall summons) -> "Victory Condition" -> "Mission Won" -> the end-mission library's 15 s -> QUICKVICTORY
	const std::set<std::string> listed = { "GoblinCave", "WildTreasureTrove", "WildSentryTower", "GoblinFissure", "WildLumberMill", "GoblinArcherHorde",
		"GoblinFighterHorde", "WildMountainGiant", "WildPorter", "Wyrm" };
	REQUIRE(m.until(
		[&] {
			if (m.frame() % 25 == 0)
			{
				for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
				{
					if (o->getControllingPlayer() == wild && listed.count(static_cast<const ThingTemplate *>(o->getTemplate())->getName()) != 0)
					{
						m.kill(*o);
					}
				}
			}
			return m.flag("/Mission Won") || m.flag("PlyrElves/Mission Won");
		},
		6000));
	MESSAGE("mission won at frame " << m.frame());
	REQUIRE(m.until([&] { return !m.engine().endRequests().empty(); }, 600));
	const ScriptEngine::EndRequest &end = m.engine().endRequests().front();
	CHECK(end.victory);
	MESSAGE("victory (" << end.action << ") at frame " << end.frame);
	const ScriptEngine::Stats &st = m.engine().stats();
	for (const auto &kv : st.unportedConditions)
	{
		MESSAGE("unported condition " << kv.first << " x" << kv.second);
	}
	for (const auto &kv : st.unportedActions)
	{
		MESSAGE("unported action " << kv.first << " x" << kv.second);
	}
}

TEST_CASE("bfme2camp1 retail: a won MAP GOOD Rivendell carries Glorfindel (with his experience) and the heroes it held back to MAP GOOD High Pass, whose "
		  "intro scripts spawn them at their waypoints (HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE, CREATE_DELAYED_CARRYOVER_UNIT_AT_WAYPOINT)")
{
	OPENBFME_REQUIRE_START(s);
	float experience = 0.0f;
	const CampaignCarryover::State carry = rivendellWon(*s, 600.0f, &experience);
	// the records Rivendell left (its delayed heroes, in CarryoverUnit order) come first, then the objects it kept
	REQUIRE(!carry.records.empty());
	CHECK(carry.used.empty());
	const CampaignCarryover::Record *rec = nullptr;
	std::vector<std::string> names;
	for (const CampaignCarryover::Record &r : carry.records)
	{
		names.push_back(r.templateName);
		if (r.templateName == "ElvenGlorfindel")
		{
			rec = &r;
		}
	}
	REQUIRE(rec != nullptr);
	CHECK(rec->experience == experience);
	CHECK(rec->quantity == 1);
	CHECK(rec != &carry.records.front()); // the records Rivendell left come before the objects it kept
	MESSAGE("records after Rivendell: " << [&] { std::string t; for (const std::string &n : names) t += n + " "; return t; }());
	// MAP GOOD High Pass with that state: its DelayCarryoverSpawningOf holds every hero back, so the start spawns nothing; the intro scripts spawn them
	const LinearCampaign *good = s->world->linearCampaigns().find("GOOD_CAMPAIGN"); // looked up after Rivendell's game is gone (the world is reloaded with it)
	REQUIRE(good != nullptr);
	REQUIRE(good->missions.size() == 8);
	campaigntest::Mission hp;
	hp.loadCampaignMission(*s, "GOOD_CAMPAIGN", 1, 1, &carry);
	CHECK(hp.game->report().carryoverObjects == 0);
	REQUIRE(hp.until([&] { return hp.unit("Glorfindel") != nullptr && hp.unit("Gloin") != nullptr; }, 3000));
	MESSAGE("High Pass: Glorfindel and Gloin at frame " << hp.frame());
	Object *g = hp.unit("Glorfindel");
	CHECK(g->isCampaignCarryover());
	CHECK(static_cast<const ThingTemplate *>(g->getTemplate())->getName() == "ElvenGlorfindel");
	REQUIRE(g->getExperienceTracker() != nullptr);
	CHECK(g->getExperienceTracker()->getExperience() == experience); // RW 0x7801E7: above the new object's, given
	CHECK(g->getControllingPlayer()->getPlayerName() == "PlyrElves");
	CHECK(hp.unit("Gloin")->isCampaignCarryover());
	const CampaignCarryover::State *now = hp.logic().campaignCarryover();
	REQUIRE(now != nullptr);
	CHECK_FALSE(CampaignCarryover::hasDelayed(*now, { "ElvenGlorfindel" }));
	CHECK(now->used.size() >= 2); // tolerance: the intro spawns the other carried heroes in the same frames too; the two checked here are among them
	CHECK(hp.engine().stats().unportedActions.count("CREATE_DELAYED_CARRYOVER_UNIT_AT_WAYPOINT") == 0);
	CHECK(hp.engine().stats().unportedConditions.count("HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE") == 0);
	hp.step(1500); // the mission's opening minute: what its scripts ask for that is not ported (the next step's list)
	for (const auto &kv : hp.engine().stats().unportedConditions)
	{
		MESSAGE("High Pass unported condition " << kv.first << " x" << kv.second);
	}
	for (const auto &kv : hp.engine().stats().unportedActions)
	{
		MESSAGE("High Pass unported action " << kv.first << " x" << kv.second);
	}
}


TEST_CASE("bfme2camp1 retail: MAP GOOD High Pass after a won Rivendell plays to its first objective (the base: a Mallorn Tree, barracks, pasture, battle "
		  "tower, arrow tower, archers and lancers) by its own tutorial scripts, the player's Porter building and the buildings training")
{
	OPENBFME_REQUIRE_START(s);
	const CampaignCarryover::State carry = rivendellWon(*s, 0.0f, nullptr);
	campaigntest::Mission hp;
	hp.loadCampaignMission(*s, "GOOD_CAMPAIGN", 1, 1, &carry);
	GameLogic &logic = hp.logic();
	Player *elves = logic.players().findPlayerWithName("PlyrElves");
	REQUIRE(elves != nullptr);
	const int pi = elves->getPlayerIndex();
	REQUIRE(hp.until([&] { return hp.unit("IE - Porter A") != nullptr; }, 300));
	Object *porter = hp.unit("IE - Porter A");
	CHECK_FALSE(objectiveMarked(hp, 1));
	// what the player builds, in the tutorial's order; the producers train the hordes once they stand
	const char *structures[] = { "ElvenMallornTree", "ElvenBarracks", "ElvenGreenPasture", "ElvenBattleTower", "ElvenWatchtowerExpansion" };
	size_t next = 0;
	bool archers = false, lancers = false;
	auto findSite = [&](const ThingTemplate &tt, Coord3D &site) {
		const Coord3D from = *porter->getPosition();
		for (int ring = 3; ring < 40; ++ring)
		{
			for (int k = 0; k < 8 * ring; ++k)
			{
				const float a = 6.2831853f * (float)k / (float)(8 * ring);
				Coord3D c{ from.x + 15.0f * (float)ring * std::cos(a), from.y + 15.0f * (float)ring * std::sin(a), 0.0f };
				c.z = logic.getGroundHeight(c.x, c.y);
				if (BuildPlacement::isLocationLegalToBuild(logic, c, tt, 0.0f, LLF_TERRAIN_RESTRICTIONS | LLF_NO_OBJECT_OVERLAP, porter, elves) == LBC_OK)
				{
					site = c;
					return true;
				}
			}
		}
		return false;
	};
	auto owned = [&](const char *name) -> Object * {
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == elves && static_cast<const ThingTemplate *>(o->getTemplate())->getName() == name && !o->isEffectivelyDead())
			{
				return o;
			}
		}
		return nullptr;
	};
	std::vector<std::string> log;
	int orderedAt = -1;
	const bool done = hp.until(
		[&] {
			if (hp.frame() % 15 != 0 || porter->isEffectivelyDead())
			{
				return objectiveMarked(hp, 1);
			}
			if (next < std::size(structures))
			{
				Object *placed = owned(structures[next]);
				if (placed)
				{
					// its foundation stands: the next one once this one is built
					if (!placed->isUnderConstruction())
					{
						log.push_back(std::string(structures[next]) + " built at frame " + std::to_string(hp.frame()));
						++next;
						orderedAt = -1;
					}
				}
				else if (orderedAt < 0 || hp.frame() - orderedAt >= 300)
				{
					const ThingTemplate *tt = logic.things().findTemplate(structures[next]);
					REQUIRE_MESSAGE(tt != nullptr, structures[next]);
					Coord3D site;
					if (std::string(structures[next]) == "ElvenWatchtowerExpansion")
					{
						// the arrow tower is built on a fortress expansion pad (its CommandSet's FOUNDATION_CONSTRUCT button: the pad's own place)
						Object *pad = nullptr;
						std::string mine;
						for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
						{
							if (o->getControllingPlayer() != elves)
							{
								continue;
							}
							const std::string n = static_cast<const ThingTemplate *>(o->getTemplate())->getName();
							mine += n + " ";
							if (!pad && o->isKindOfName("BASE_FOUNDATION") && n.find("ExpansionPad") != std::string::npos)
							{
								pad = o;
							}
						}
						if (!pad)
						{
							log.push_back("no expansion pad of the player's: " + mine);
							orderedAt = (int)hp.frame();
							return objectiveMarked(hp, 1);
						}
						hp.game->commands().append(selectMessage(pi, pad->getID()));
						GameMessage f(MSG_FOUNDATION_CONSTRUCT, pi);
						f.appendIntegerArgument((int)tt->getTemplateID());
						f.appendLocationArgument(*pad->getPosition());
						f.appendRealArgument(0.0f);
						hp.game->commands().append(f);
						log.push_back("ElvenWatchtowerExpansion ordered on " + static_cast<const ThingTemplate *>(pad->getTemplate())->getName() + " at frame " +
									  std::to_string(hp.frame()));
						orderedAt = (int)hp.frame();
					}
					else if (!findSite(*tt, site))
					{
						log.push_back(std::string("no legal site for ") + structures[next] + " at frame " + std::to_string(hp.frame()));
					}
					else
					{
						if (orderedAt >= 0)
						{
							log.push_back(std::string(structures[next]) + ": no foundation 300 frames after the order (money " +
										  std::to_string(elves->getMoney()->countMoney()) + ", buildable " +
										  std::to_string(BuildAssistant::buildable(*tt, logic)) + ")");
						}
						hp.game->commands().append(selectMessage(pi, porter->getID()));
						hp.game->commands().append(constructMessage(pi, *tt, site));
						log.push_back(std::string(structures[next]) + " ordered at frame " + std::to_string(hp.frame()) + " at (" +
									  std::to_string((int)site.x) + ", " + std::to_string((int)site.y) + ")");
						orderedAt = (int)hp.frame();
					}
				}
			}
			Object *barracks = owned("ElvenBarracks");
			if (!archers && barracks && !barracks->isUnderConstruction())
			{
				hp.game->commands().append(selectMessage(pi, barracks->getID()));
				hp.game->commands().append(trainMessage(pi, *logic.things().findTemplate("ElvenLorienArcherHorde")));
				archers = true;
				log.push_back("archers queued at frame " + std::to_string(hp.frame()));
			}
			Object *pasture = owned("ElvenGreenPasture");
			if (!lancers && pasture && !pasture->isUnderConstruction())
			{
				hp.game->commands().append(selectMessage(pi, pasture->getID()));
				hp.game->commands().append(trainMessage(pi, *logic.things().findTemplate("ElvenRivendellLancerHorde")));
				lancers = true;
				log.push_back("lancers queued at frame " + std::to_string(hp.frame()));
			}
			return objectiveMarked(hp, 1);
		},
		15000);
	for (const std::string &l : log)
	{
		MESSAGE(l);
	}
	for (int n = 2; n <= 7; ++n)
	{
		MESSAGE("tutorial objective " << n << std::string(objectiveMarked(hp, n) ? " completed" : " open"));
	}
	MESSAGE("money " << elves->getMoney()->countMoney() << " at frame " << hp.frame());
	REQUIRE(done);
	MESSAGE("High Pass objective 1 at frame " << hp.frame());
	CHECK(next == std::size(structures));
	for (int n = 2; n <= 7; ++n)
	{
		CHECK(objectiveMarked(hp, n)); // the tutorial's six steps (Mallorn Tree .. archers and cavalry)
	}
	CHECK(hp.engine().endRequests().empty());
	for (const auto &kv : hp.engine().stats().unportedConditions)
	{
		MESSAGE("unported condition " << kv.first << " x" << kv.second);
	}
	for (const auto &kv : hp.engine().stats().unportedActions)
	{
		MESSAGE("unported action " << kv.first << " x" << kv.second);
	}
}

namespace
{
// the counter's value, or -1
int counterOf(campaigntest::Mission &m, const std::string &side, const std::string &name)
{
	for (const auto &kv : m.engine().allCounters())
	{
		if (kv.first.first == side && kv.first.second == name)
		{
			return kv.second.value;
		}
	}
	return -1;
}

// a point inside the trigger area: its vertices' mean when that is inside (the polygon test RW 0x70CF29), else the first point of a 16 x 16 grid over its
// bounds that is (a concave area's mean can fall outside it)
Coord3D insideOf(campaigntest::Mission &m, const std::string &area)
{
	const TriggerArea *t = m.engine().findTrigger(area);
	REQUIRE_MESSAGE(t != nullptr, area);
	Coord3D c = m.centreOf(area);
	if (ScriptConditions::pointInTrigger(*t, c.x, c.y))
	{
		return c;
	}
	float loX = t->points[0].x, hiX = loX, loY = t->points[0].y, hiY = loY;
	for (const Point2F &p : t->points)
	{
		loX = std::min(loX, p.x);
		hiX = std::max(hiX, p.x);
		loY = std::min(loY, p.y);
		hiY = std::max(hiY, p.y);
	}
	for (int i = 1; i < 16; ++i)
	{
		for (int j = 1; j < 16; ++j)
		{
			const float x = loX + (hiX - loX) * (float)i / 16.0f, y = loY + (hiY - loY) * (float)j / 16.0f;
			if (ScriptConditions::pointInTrigger(*t, x, y))
			{
				return Coord3D{ x, y, m.logic().getGroundHeight(x, y) };
			}
		}
	}
	FAIL("no point inside " << area);
	return c;
}

void printUnported(campaigntest::Mission &m, const char *map)
{
	const ScriptEngine::Stats &st = m.engine().stats();
	for (const auto &kv : st.unportedConditions)
	{
		MESSAGE(std::string(map) << " unported condition " << kv.first << " x" << kv.second);
	}
	for (const auto &kv : st.unportedActions)
	{
		MESSAGE(std::string(map) << " unported action " << kv.first << " x" << kv.second);
	}
}
} // namespace

TEST_CASE("bfme2camp1 retail: MAP GOOD High Pass after a won Rivendell plays to its victory by its own scripts (the goblin camp, Haldir's rescue, the "
		  "Watcher): the player's Glorfindel enters each act's area, the test destroys the teams the objectives name")
{
	OPENBFME_REQUIRE_START(s);
	const CampaignCarryover::State carry = rivendellWon(*s, 0.0f, nullptr);
	campaigntest::Mission hp;
	hp.loadCampaignMission(*s, "GOOD_CAMPAIGN", 1, 1, &carry);
	REQUIRE(hp.until([&] { return hp.unit("Glorfindel") != nullptr; }, 600));
	Object *glorfindel = hp.unit("Glorfindel");
	if (glorfindel->getBodyModule())
	{
		glorfindel->getBodyModule()->setIndestructible(true); // the test plays the objectives, not the fights
	}
	const std::string counter = "IN - Counter - Objectives Complete";
	auto done = [&] { return counterOf(hp, "", counter); };
	auto visit = [&](const char *area, int frames) {
		hp.place(*glorfindel, insideOf(hp, area));
		hp.step(frames);
		MESSAGE("Glorfindel in '" << std::string(area) << "' at frame " << hp.frame() << ", objectives complete " << done());
	};
	auto killTeamWhenThere = [&](const char *team, int frames) {
		const bool there = hp.until([&] {
			Team *t = ScriptConditions::team(hp.engine(), team);
			return t && !ScriptConditions::members(*t).empty();
		}, frames);
		const std::string what = std::string(team) + (there ? " exists" : " never appeared");
		MESSAGE(what << " at frame " << hp.frame());
		if (there)
		{
			hp.killTeam(team);
		}
		return there;
	};
	REQUIRE(hp.until([&] { return hp.requested("CAMERA_LETTERBOX_END") || hp.frame() > 400; }, 600)); // tolerance: the intro ends its letterbox or runs past frame 400, whichever is first (a precondition, not a measured value)
	// 1. the goblin camp: entering High Pass shows objective 8; the camp's team falls
	visit("IE - Area 1 - Enter High Pass", 60);
	CHECK(killTeamWhenThere("PlyrWild/IIIW - Goblin Base", 600));
	// the camp's Wild structures leave their holes on the camp's team (RebuildHoleExposeDie RW 0x889AAF); a hole is no PROJECTILE / INERT /
	// IGNORE_FOR_VICTORY member (WILD_HOLE_KINDOF; only the mine shaft's and treasure trove's add IGNORE_FOR_VICTORY), so TEAM_DESTROYED (RW 0x7A0956)
	// waits for them too: the player destroys them
	hp.step(2);
	hp.killTeam("PlyrWild/IIIW - Goblin Base");
	REQUIRE(hp.until([&] { return objectiveMarked(hp, 8); }, 600));
	MESSAGE("the camp (objective 8) at frame " << hp.frame() << ", objectives complete " << done());
	// 2. Haldir: the player reaches him (his team becomes the player's), then his attackers fall
	visit("IIE - Rescue Haldir - Change Teams", 60);
	CHECK(killTeamWhenThere("PlyrWild/IW - Haldir - Attackers", 600));
	REQUIRE(hp.until([&] { return objectiveMarked(hp, 9); }, 600));
	MESSAGE("Haldir (objective 9) at frame " << hp.frame() << ", objectives complete " << done());
	// 3. the Watcher: the player reaches its spawn area (the goblins summon it), the objective's completion waits 15 s; the Watcher falls
	// past the ice towers ('_Location - Player Past Ice Towers': the flag that lets '_Runner - Activate Act III' enable PlyrWild's Act III), then the spawn area
	visit("_Runner - Trigger - Enable Act III", 60);
	visit("IIIW - Watcher Spawn Area", 30);
	const bool watcher = hp.until([&] {
		for (Object *o = hp.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (static_cast<const ThingTemplate *>(o->getTemplate())->getName() == "WatcherHead" && !o->isEffectivelyDead())
			{
				return true;
			}
		}
		return false;
	}, 600);
	MESSAGE(std::string(watcher ? "the Watcher summoned" : "the Watcher never summoned") << " at frame " << hp.frame() << ", completion timer "
		<< counterOf(hp, "", "IIIW - Timer - Start Watcher Objective Completion") << ", objective timer "
		<< counterOf(hp, "", "IIIW - Timer - Start Watcher Objective"));
	hp.step(100); // past 'IIIW - Watcher - Enable Objective Completion' (15 s)
	for (Object *o = hp.logic().getFirstObject(); o; o = o->getNextObject())
	{
		const std::string n = static_cast<const ThingTemplate *>(o->getTemplate())->getName();
		if (n.rfind("Watcher", 0) == 0 && o->getControllingPlayer() && o->getControllingPlayer()->getPlayerName() == "PlyrWild")
		{
			hp.kill(*o);
		}
	}
	const bool won = hp.until([&] { return !hp.engine().endRequests().empty(); }, 1500);
	MESSAGE("objectives complete " << done() << ", Mission Won " << hp.flag("/Mission Won") << ", end at frame " << hp.frame());
	printUnported(hp, "High Pass");
	REQUIRE(won);
	CHECK(hp.engine().endRequests().front().victory);
}

TEST_CASE("bfme2camp1 retail: MAP GOOD Ettenmoors after won Rivendell and High Pass gets Glorfindel and Gloin from the carry-over and plays to its first "
		  "objective (the player's units at the encampment) by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	CampaignCarryover::State carry = rivendellWon(*s, 0.0f, nullptr);
	{
		campaigntest::Mission hp;
		hp.loadCampaignMission(*s, "GOOD_CAMPAIGN", 1, 1, &carry);
		REQUIRE(hp.until([&] { return hp.unit("Glorfindel") != nullptr && hp.unit("Gloin") != nullptr; }, 600));
		ScriptActions::execute(hp.engine(), act("VICTORY", {})); // High Pass won (RW 0x5EBAEE)
		REQUIRE(hp.logic().campaignCarryover() != nullptr);
		carry = *hp.logic().campaignCarryover();
	}
	CHECK(CampaignCarryover::hasDelayed(carry, { "ElvenGlorfindel" }));
	CHECK(CampaignCarryover::hasDelayed(carry, { "DwarvenGloin" }));
	campaigntest::Mission m;
	m.loadCampaignMission(*s, "GOOD_CAMPAIGN", 2, 1, &carry);
	REQUIRE(m.until([&] { return m.unit("IE - PlayerHero") != nullptr && m.unit("IE - Gloin") != nullptr; }, 600));
	MESSAGE("Ettenmoors: Glorfindel and Gloin at frame " << m.frame());
	CHECK(m.unit("IE - PlayerHero")->isCampaignCarryover());
	CHECK(static_cast<const ThingTemplate *>(m.unit("IE - PlayerHero")->getTemplate())->getName() == "ElvenGlorfindel");
	Player *elves = m.logic().players().findPlayerWithName("PlyrElves");
	REQUIRE(elves != nullptr);
	const Coord3D base = m.centreOf("IIE - Area 1 - Player Base");
	CHECK_FALSE(objectiveMarked(m, 1));
	// the player orders every unit of theirs to the encampment (a player's move), again while they idle
	const bool done = m.until(
		[&] {
			if (m.frame() % 30 == 0)
			{
				for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
				{
					AIUpdateInterface *ai = o->getAIUpdateInterface();
					if (o->getControllingPlayer() == elves && ai && !o->isEffectivelyDead() && ai->isIdle() && !o->isKindOfName("STRUCTURE"))
					{
						ai->aiMoveToPosition(base, CMD_FROM_PLAYER);
					}
				}
			}
			return objectiveMarked(m, 1);
		},
		9000);
	MESSAGE("Ettenmoors objective 1 at frame " << m.frame());
	REQUIRE(done);
	CHECK(m.engine().endRequests().empty());
	for (const auto &kv : m.engine().stats().unportedConditions)
	{
		MESSAGE("unported condition " << kv.first << " x" << kv.second);
	}
	for (const auto &kv : m.engine().stats().unportedActions)
	{
		MESSAGE("unported action " << kv.first << " x" << kv.second);
	}
}

TEST_CASE("bfme2camp1 retail: MAP GOOD Ettenmoors after won Rivendell and High Pass plays to its victory by its own scripts (the encampment taken and "
		  "cleared, the Entmoot freed, the Goblin King and his fortress destroyed): the player's Glorfindel reaches each area, the test destroys what the "
		  "objectives name")
{
	OPENBFME_REQUIRE_START(s);
	CampaignCarryover::State carry = rivendellWon(*s, 0.0f, nullptr);
	{
		campaigntest::Mission hp;
		hp.loadCampaignMission(*s, "GOOD_CAMPAIGN", 1, 1, &carry);
		REQUIRE(hp.until([&] { return hp.unit("Glorfindel") != nullptr && hp.unit("Gloin") != nullptr; }, 600));
		ScriptActions::execute(hp.engine(), act("VICTORY", {})); // High Pass won (RW 0x5EBAEE)
		REQUIRE(hp.logic().campaignCarryover() != nullptr);
		carry = *hp.logic().campaignCarryover();
	}
	campaigntest::Mission m;
	m.loadCampaignMission(*s, "GOOD_CAMPAIGN", 2, 1, &carry);
	REQUIRE(m.until([&] { return m.unit("IE - PlayerHero") != nullptr; }, 600));
	Object *hero = m.unit("IE - PlayerHero");
	if (hero->getBodyModule())
	{
		hero->getBodyModule()->setIndestructible(true); // the test plays the objectives, not the fights
	}
	const std::string counter = "IN - Counter - Objectives - Victory Conditions";
	auto done = [&] { return counterOf(m, "", counter); };
	auto visit = [&](const char *area, int frames) {
		m.place(*hero, insideOf(m, area));
		m.step(frames);
		MESSAGE("Glorfindel in '" << std::string(area) << "' at frame " << m.frame() << ", objectives complete " << done());
	};
	// the wild side's objects inside an area fall (the fights the player would win)
	auto clearWild = [&](const char *area) {
		const TriggerArea *t = m.engine().findTrigger(area);
		REQUIRE(t != nullptr);
		int n = 0;
		for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() && o->getControllingPlayer()->getPlayerName() == "PlyrWild" && !o->isEffectivelyDead() &&
				ScriptConditions::pointInTrigger(*t, o->getPosition()->x, o->getPosition()->y))
			{
				m.kill(*o);
				++n;
			}
		}
		return n;
	};
	// 1. the encampment: the player's units in it
	visit("IIE - Area 1 - Player Base", 30);
	REQUIRE(m.until([&] { return objectiveMarked(m, 1); }, 600));
	MESSAGE("objective 1 at frame " << m.frame() << ", objectives complete " << done());
	// 2. the encampment cleared of the wild side
	const bool cleared = m.until([&] {
		if (m.frame() % 30 == 0)
		{
			clearWild("IIE - Area 1 - Player Base");
		}
		return objectiveMarked(m, 2);
	}, 3000);
	MESSAGE(std::string(cleared ? "objective 2" : "objective 2 NOT done") << " at frame " << m.frame() << ", objectives complete " << done());
	CHECK(cleared);
	// 3. the Entmoot: its guard falls, the player reaches it
	if (Object *guard = m.unit("IIIG - Area 3 - Entmoot Guard"))
	{
		m.kill(*guard);
	}
	visit("IIE - Area 3 - Entmoot", 30);
	const bool entmoot = m.until([&] { return objectiveMarked(m, 3); }, 1500);
	MESSAGE(std::string(entmoot ? "objective 3" : "objective 3 NOT done") << " at frame " << m.frame() << ", objectives complete " << done());
	CHECK(entmoot);
	// 4. the Goblin King's finale team, 5. his fortress
	m.killTeam("PlyrWild/IIIG - Goblin King - Finale");
	const bool king = m.until([&] { return objectiveMarked(m, 4); }, 600);
	MESSAGE(std::string(king ? "objective 4" : "objective 4 NOT done") << " at frame " << m.frame() << ", objectives complete " << done());
	CHECK(king);
	if (Object *fortress = m.unit("IIIG - GK Fortress"))
	{
		m.kill(*fortress);
	}
	const bool won = m.until([&] { return !m.engine().endRequests().empty(); }, 3000);
	MESSAGE("objectives complete " << done() << ", end at frame " << m.frame());
	printUnported(m, "Ettenmoors");
	CHECK(done() == 5);
	REQUIRE(won);
	CHECK(m.engine().endRequests().front().victory);
}

namespace
{
// lane BFME2CAMP1: the HUD's view as the test plays it (LiveGame::setScriptLocalView): the camera scrolled `scroll`, the objectives screen `open`
struct TutorialView : ScriptLocalView
{
	float scroll = 0.0f;
	bool open = false;
	float cameraScrollDistance() const override { return scroll; }
	bool objectivesScreenOpen() const override { return open; }
};
} // namespace

TEST_CASE("bfme2camp1 retail: MAP BEGINNER TUTORIAL (mode 6) plays its first lessons by its own scripts: the player scrolls the camera, selects and moves "
		  "the fighters, wins the fights, opens and closes the objectives screen, and the scripts mark each lesson's objective")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.loadTutorial(*s, "map beginner tutorial");
	CHECK(m.logic().economy().context().gameMode == EconomyContext::MODE_TUTORIAL);
	TutorialView view;
	m.game->setScriptLocalView(&view);
	GameLogic &logic = m.logic();
	Player *me = logic.players().getLocalPlayer();
	REQUIRE(me != nullptr);
	const int pi = me->getPlayerIndex();
	MESSAGE("the local player is " << me->getPlayerName());
	std::vector<std::string> log;
	int lastMarked = 0;
	auto marked = [&] {
		int n = 0;
		for (int k = 1; k <= 11; ++k)
		{
			if (objectiveMarked(m, k))
			{
				++n;
			}
		}
		return n;
	};
	// the player's moves: every unit of the player's that idles walks to the lesson's spot
	auto moveMine = [&](const Coord3D &to) {
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			AIUpdateInterface *ai = o->getAIUpdateInterface();
			if (o->getControllingPlayer() == me && ai && !o->isEffectivelyDead() && ai->isIdle() && !o->isKindOfName("STRUCTURE") && !o->isKindOfName("DOZER"))
			{
				ai->aiMoveToPosition(to, CMD_FROM_PLAYER);
			}
		}
	};
	auto selectMine = [&] {
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, pi);
		sel.appendBooleanArgument(true);
		int n = 0;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == me && !o->isEffectivelyDead())
			{
				sel.appendObjectIDArgument(o->getID());
				++n;
			}
		}
		if (n)
		{
			m.game->commands().append(sel);
		}
	};
	auto killNamed = [&](const char *name) {
		if (Object *o = m.unit(name))
		{
			if (!o->isEffectivelyDead())
			{
				m.kill(*o);
				log.push_back(std::string("killed ") + name + " at frame " + std::to_string(m.frame()));
			}
		}
	};
	// ---- the base-building lessons (objectives 6 .. 9) and the final attack (10): what the player builds, trains and recruits, in the lessons' order ----
	auto mineOf = [&](const char *name) -> Object * {
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == me && !o->isEffectivelyDead() && static_cast<const ThingTemplate *>(o->getTemplate())->getName() == name)
			{
				return o;
			}
		}
		return nullptr;
	};
	auto countMine = [&](const char *name) {
		int n = 0;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			n += o->getControllingPlayer() == me && !o->isEffectivelyDead() && static_cast<const ThingTemplate *>(o->getTemplate())->getName() == name ? 1 : 0;
		}
		return n;
	};
	// a legal site for `tt` inside the trigger area (its interior points on a 4-unit grid, nearest the area's inside point first)
	auto siteIn = [&](const ThingTemplate &tt, const char *area, Object *dozer, Coord3D &site) {
		const TriggerArea *t = m.engine().findTrigger(area);
		if (!t)
		{
			return false;
		}
		const Coord3D c = insideOf(m, area);
		for (int ring = 0; ring < 40; ++ring)
		{
			for (int k = 0; k < (ring == 0 ? 1 : 8 * ring); ++k)
			{
				const float a = ring == 0 ? 0.0f : 6.2831853f * (float)k / (float)(8 * ring);
				Coord3D p{ c.x + 4.0f * (float)ring * std::cos(a), c.y + 4.0f * (float)ring * std::sin(a), 0.0f };
				if (!ScriptConditions::pointInTrigger(*t, p.x, p.y))
				{
					continue;
				}
				p.z = logic.getGroundHeight(p.x, p.y);
				if (BuildPlacement::isLocationLegalToBuild(logic, p, tt, 0.0f, LLF_TERRAIN_RESTRICTIONS | LLF_NO_OBJECT_OVERLAP, dozer, me) == LBC_OK)
				{
					site = p;
					return true;
				}
			}
		}
		return false;
	};
	// the player's dozer (the Porter) whose command set builds `name` (a DOZER_CONSTRUCT button of that object, or of one whose name contains it)
	auto dozerFor = [&](const char *name, const ThingTemplate *&what) -> Object * {
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() != me || o->isEffectivelyDead() || !o->isKindOfName("DOZER"))
			{
				continue;
			}
			const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(o->getCommandSetName()) : nullptr;
			for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *b = set->getCommandButton(i);
				if (b && b->m_command == GUI_COMMAND_DOZER_CONSTRUCT && (b->m_objectName == name || b->m_objectName.find(name) != std::string::npos))
				{
					what = logic.things().findTemplate(b->m_objectName);
					if (what)
					{
						return o;
					}
				}
			}
		}
		return nullptr;
	};
	int orderedAt = -1, trebuchetTries = 0;
	std::string ordered;
	auto order = [&](const std::string &what) {
		if (ordered == what && orderedAt >= 0 && (int)m.frame() - orderedAt < 450)
		{
			return false; // the last order of it is under way
		}
		ordered = what;
		orderedAt = (int)m.frame();
		log.push_back(what + " ordered at frame " + std::to_string(m.frame()) + " (money " + std::to_string(me->getMoney()->countMoney()) + ")");
		return true;
	};
	auto buildInArea = [&](const char *name, const char *area) {
		const ThingTemplate *what = nullptr;
		Object *dozer = dozerFor(name, what);
		Coord3D site;
		if (!dozer)
		{
			if (order(std::string("no dozer button for ") + name)) {}
			return;
		}
		if (!siteIn(*what, area, dozer, site))
		{
			if (order(std::string("no legal site for ") + what->getName() + " in " + area)) {}
			return;
		}
		if (order(what->getName() + " in " + area))
		{
			m.game->commands().append(selectMessage(pi, dozer->getID()));
			m.game->commands().append(constructMessage(pi, *what, site));
		}
	};
	// a FOUNDATION_CONSTRUCT on the player's object `base` (an expansion pad or a wall hub): its command set's button that makes `name`
	auto buildOn = [&](Object *base, const char *name) {
		if (!base)
		{
			return;
		}
		const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(base->getCommandSetName()) : nullptr;
		for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
		{
			const CommandButton *b = set->getCommandButton(i);
			const ThingTemplate *what = b && b->m_command == GUI_COMMAND_FOUNDATION_CONSTRUCT ? logic.things().findTemplate(b->m_objectName) : nullptr;
			if (what && what->getName() == name)
			{
				if (order(std::string(name) + " on " + static_cast<const ThingTemplate *>(base->getTemplate())->getName()))
				{
					m.game->commands().append(selectMessage(pi, base->getID()));
					GameMessage f(MSG_FOUNDATION_CONSTRUCT, pi);
					f.appendIntegerArgument((int)what->getTemplateID());
					f.appendLocationArgument(*base->getPosition());
					f.appendRealArgument(0.0f);
					m.game->commands().append(f);
				}
				return;
			}
		}
		if (order(std::string("no ") + name + " button on " + static_cast<const ThingTemplate *>(base->getTemplate())->getName())) {}
	};
	auto train = [&](const char *producer, const char *unit) {
		Object *p = mineOf(producer);
		const ThingTemplate *tt = logic.things().findTemplate(unit);
		if (p && tt && !p->isUnderConstruction() && order(std::string(unit) + " at " + producer + " #" + std::to_string(countMine(unit))))
		{
			m.game->commands().append(selectMessage(pi, p->getID()));
			m.game->commands().append(trainMessage(pi, *tt));
		}
	};
	auto completeIn = [&](const char *name, const char *area) {
		const TriggerArea *t = m.engine().findTrigger(area);
		for (Object *o = logic.getFirstObject(); o && t; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == me && !o->isEffectivelyDead() && static_cast<const ThingTemplate *>(o->getTemplate())->getName() == name &&
				ScriptConditions::pointInTrigger(*t, o->getPosition()->x, o->getPosition()->y))
			{
				if (!o->isUnderConstruction())
				{
					return true; // one completely built (the script's PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT >= 1)
				}
			}
		}
		return false;
	};
	auto buildStep = [&] {
		if (m.frame() % 30 != 0)
		{
			return;
		}
		if (!objectiveMarked(m, 6))
		{
			if (!mineOf("MenFortressCitadel") && !mineOf("MenFortress") && !mineOf("MenFortress_Tutorial"))
			{
				buildInArea("Fortress", "Area Trigger - Build Fortress Here");
			}
			return;
		}
		if (!objectiveMarked(m, 7))
		{
			const struct { const char *name, *area; } plan[] = { { "GondorFarm", "Area Trigger - Build Farm Here" },
				{ "GondorBarracks", "Area Trigger - Build Barracks Here" }, { "GondorArcherRange", "Area Trigger - Build Archery Range Here" } };
			for (const auto &p : plan)
			{
				if (!completeIn(p.name, p.area))
				{
					if (!mineOf(p.name))
					{
						buildInArea(p.name, p.area);
					}
					return;
				}
			}
			return;
		}
		if (!objectiveMarked(m, 8))
		{
			// the walls the script asks for (Base Building - 26 .. 32): a hub on the corner pad, a span from it to each "Build Wall Hub" area (the Begin Wall
			// Span button, Command_GondorCastleWallHub_BeginWallSpan: MSG_WALL_HUB_CONSTRUCT_SPAN, its cap a MenWallHubSmall), the second corner pad's hub
			// once hub 02 stands (the script's Spotlight 12), then a segment upgraded to the trebuchet (Command_MenWallUpgradeToTrebuchet: Upgrade_MenWallTrebuchet,
			// MenWallSegmentSmall's ReplaceSelfUpgrade)
			if (!mineOf("MenWallHubSmallExpansion"))
			{
				buildOn(mineOf("MenFortressExpansionPadCorner_Tutorial1"), "MenWallHubSmallExpansion");
				return;
			}
			for (int k = 1; k <= 4; ++k)
			{
				const std::string area = "Area Trigger - Build Wall Hub 0" + std::to_string(k);
				if (completeIn("MenWallHubSmall", area.c_str()))
				{
					continue;
				}
				if (k >= 3 && countMine("MenWallHubSmallExpansion") < 2)
				{
					buildOn(mineOf("MenFortressExpansionPadCorner_Tutorial2"), "MenWallHubSmallExpansion");
					return;
				}
				const Coord3D aim = insideOf(m, area.c_str());
				Object *hub = nullptr;
				float best = 0.0f;
				for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
				{
					const std::string n = static_cast<const ThingTemplate *>(o->getTemplate())->getName();
					if (o->getControllingPlayer() == me && !o->isEffectivelyDead() && !o->isUnderConstruction() && (n == "MenWallHubSmall" || n == "MenWallHubSmallExpansion") &&
						!ScriptConditions::pointInTrigger(*m.engine().findTrigger(area), o->getPosition()->x, o->getPosition()->y))
					{
						const float dx = o->getPosition()->x - aim.x, dy = o->getPosition()->y - aim.y, d = dx * dx + dy * dy;
						if (!hub || d < best)
						{
							hub = o;
							best = d;
						}
					}
				}
				const ThingTemplate *cap = logic.things().findTemplate("MenWallHubSmall");
				if (!hub || !cap)
				{
					return; // the expansion hub is still going up
				}
				const unsigned optionOne = 1u << 13; // OPTION_ONE (RW 0xDAE268 index 13), the Begin Wall Span button's Options
				if (ordered == "a wall span to " + area && (int)m.frame() - orderedAt < 450)
				{
					return; // the span is going up
				}
				// the end point: the shortest legal plan (worst code 0) whose last tile, the cap, stands in the area (a 16 x 16 grid over its bounds)
				const TriggerArea *t = m.engine().findTrigger(area);
				float loX = t->points[0].x, hiX = loX, loY = t->points[0].y, hiY = loY;
				for (const Point2F &q : t->points)
				{
					loX = std::min(loX, q.x);
					hiX = std::max(hiX, q.x);
					loY = std::min(loY, q.y);
					hiY = std::max(hiY, q.y);
				}
				WallSpan::Plan plan;
				Coord3D end;
				bool planned = false;
				float shortest = 0.0f;
				for (int i = 0; i <= 16; ++i)
				{
					for (int j = 0; j <= 16; ++j)
					{
						Coord3D e{ loX + (hiX - loX) * (float)i / 16.0f, loY + (hiY - loY) * (float)j / 16.0f, 0.0f };
						if (!ScriptConditions::pointInTrigger(*t, e.x, e.y))
						{
							continue;
						}
						e.z = logic.getGroundHeight(e.x, e.y);
						WallSpan::Plan p;
						if (!WallSpan::plan(logic, *hub, *hub->getPosition(), e, optionOne, p) || p.worstCode != 0 || p.tiles.empty() ||
							!ScriptConditions::pointInTrigger(*t, p.tiles.back().pos.x, p.tiles.back().pos.y))
						{
							continue;
						}
						const float dx = e.x - hub->getPosition()->x, dy = e.y - hub->getPosition()->y;
						if (!planned || dx * dx + dy * dy < shortest)
						{
							plan = p;
							end = e;
							planned = true;
							shortest = dx * dx + dy * dy;
						}
					}
				}
				if (!planned)
				{
					if (order("no legal wall span to " + area)) {}
					return;
				}
				if (order("a wall span to " + area))
				{
					log.back() += std::string(" from ") + static_cast<const ThingTemplate *>(hub->getTemplate())->getName() + " (" + std::to_string(plan.tiles.size()) + " tiles)";
					GameMessage w(MSG_WALL_HUB_CONSTRUCT_SPAN, pi);
					w.appendIntegerArgument((int)cap->getFinalOverride()->getTemplateID());
					w.appendLocationArgument(*hub->getPosition());
					w.appendLocationArgument(end);
					w.appendIntegerArgument((int)optionOne);
					w.appendObjectIDArgument(hub->getID());
					m.game->commands().append(w);
				}
				return;
			}
			if (!mineOf("MenWallTrebuchetSmall"))
			{
				const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade("Upgrade_MenWallTrebuchet") : nullptr;
				std::vector<Object *> segments;
				for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
				{
					if (o->getControllingPlayer() == me && !o->isEffectivelyDead() && !o->isUnderConstruction() &&
						static_cast<const ThingTemplate *>(o->getTemplate())->getName() == "MenWallSegmentSmall")
					{
						segments.push_back(o);
					}
				}
				if (!u || segments.empty())
				{
					if (order(std::string("no ") + (u ? "MenWallSegmentSmall" : "Upgrade_MenWallTrebuchet") + " for the trebuchet")) {}
					return;
				}
				// another segment each try (ReplaceSelfUpgrade's placement test may refuse one)
				Object *seg = segments[(size_t)(trebuchetTries % (int)segments.size())];
				if (order("Upgrade_MenWallTrebuchet on segment " + std::to_string(trebuchetTries) + " of " + std::to_string(segments.size())))
				{
					++trebuchetTries;
					m.game->commands().append(selectMessage(pi, seg->getID()));
					GameMessage q(MSG_QUEUE_UPGRADE, pi);
					q.appendObjectIDArgument(seg->getID());
					q.appendIntegerArgument(u->getMaskBit());
					m.game->commands().append(q);
				}
			}
			return;
		}
		if (!objectiveMarked(m, 9))
		{
			if (countMine("GondorFighterHorde") < 4)
			{
				train("GondorBarracks", "GondorFighterHorde");
			}
			else if (countMine("GondorArcherHorde") < 2)
			{
				train("GondorArcherRange", "GondorArcherHorde");
			}
			else if (!mineOf("GondorBoromir"))
			{
				// the revival entry the map made (CREATE_UNIT_REVIVAL_ENTRY_AT_LEVEL): the fortress recruits it by its hero list index
				Object *fortress = mineOf("MenFortressCitadel") ? mineOf("MenFortressCitadel") : mineOf("MenFortress_Tutorial");
				const ThingTemplate *boromir = logic.things().findTemplate("GondorBoromir");
				int index = -1; // the record CREATE_UNIT_REVIVAL_ENTRY_AT_LEVEL appended: the last GondorBoromir one
				for (int i = 0; boromir && i < (int)me->heroes().size(); ++i)
				{
					index = me->heroes().at(i)->templateName == "GondorBoromir" ? i : index;
				}
				if (!fortress || index < 0)
				{
					if (order(std::string("no ") + (fortress ? "GondorBoromir record" : "fortress") + " to recruit Boromir")) {}
				}
				if (fortress && index >= 0 && order("GondorBoromir recruited (hero list " + std::to_string(index) + ")"))
				{
					m.game->commands().append(selectMessage(pi, fortress->getID()));
					GameMessage q(MSG_QUEUE_UNIT_CREATE, pi);
					q.appendBooleanArgument(true);
					q.appendIntegerArgument(index);
					q.appendIntegerArgument(-1);
					q.appendBooleanArgument(false);
					q.appendBooleanArgument(false);
					m.game->commands().append(q);
				}
			}
			return;
		}
		// the final attack: the wild side's team falls once it comes (the fight the player wins)
		Team *attackers = ScriptConditions::team(m.engine(), "PlyrWild/Final Attack Team");
		if (attackers && !ScriptConditions::members(*attackers).empty() && order("the final attack team destroyed"))
		{
			m.killTeam("PlyrWild/Final Attack Team");
		}
	};
	const bool done = m.until(
		[&] {
			const int n = marked();
			if (n != lastMarked)
			{
				log.push_back("objectives marked " + std::to_string(n) + " at frame " + std::to_string(m.frame()));
				lastMarked = n;
			}
			if (m.frame() % 15 != 0)
			{
				return objectiveMarked(m, 10);
			}
			view.scroll = m.frame() > 300 ? 1000.0f : 0.0f; // the player scrolls about once the controls are given
			view.open = !objectiveMarked(m, 11);              // open for the Palantir lesson, closed after it
			selectMine();
			if (!objectiveMarked(m, 1))
			{
				const TriggerArea *two = m.engine().findTrigger("Area Trigger - Move Fighters Here 2");
				const Object *f1 = m.unit("Fighters 01");
				const bool firstDone = f1 && m.engine().findTrigger("Area Trigger - Move Fighters Here") &&
					ScriptConditions::pointInTrigger(*m.engine().findTrigger("Area Trigger - Move Fighters Here"), f1->getPosition()->x, f1->getPosition()->y);
				moveMine(insideOf(m, firstDone && two ? "Area Trigger - Move Fighters Here 2" : "Area Trigger - Move Fighters Here"));
			}
			else if (!objectiveMarked(m, 2))
			{
				killNamed("Goblin Horde 01");
				killNamed("Goblin Horde 02");
			}
			else if (!objectiveMarked(m, 3))
			{
				for (const char *n : { "Goblin Horde 03", "Goblin Archer Horde 01", "Goblin Horde 04", "Goblin Horde 05", "Goblin Horde 06", "Goblin Archer Horde 02",
						 "Goblin Archer Horde 03" })
				{
					killNamed(n);
				}
				moveMine(insideOf(m, "Area Trigger - Move Porter Up Closer"));
			}
			else if (!objectiveMarked(m, 4))
			{
				// the lairs fall and leave their holes (RebuildHoleExposeDie): 'Creeps - 03' waits for a SpiderLairHole of SpiderCreeps
				killNamed("Spider Pit 01");
				killNamed("Spider Pit 02");
			}
			else if (!objectiveMarked(m, 5) && (m.unit("Spider Pit 01") || m.unit("Spider Pit 02")))
			{
				// the holes carry the lairs' names: 'Tech Structures - 01' waits for both destroyed
				killNamed("Spider Pit 01");
				killNamed("Spider Pit 02");
			}
			else if (!objectiveMarked(m, 5) && m.frame() % 150 == 0)
			{
				// the outpost is captured the way a right click on it does (CommandXlat: a selected unit's SPECIAL_POWER button of power type 0x1D,
				// MSG_DO_SPECIAL_POWER_AT_OBJECT { the power, the target, the button's options, no source, the target's position })
				Object *outpost = m.unit("Outpost");
				Object *flag = m.unit("Capture Flag 01");
				Object *target = outpost && outpost->isKindOfName("CAPTURABLE") ? outpost : flag;
				if (target)
				{
					for (Object *o = logic.getFirstObject(); o && target; o = o->getNextObject())
					{
						if (o->getControllingPlayer() != me || o->isEffectivelyDead())
						{
							continue;
						}
						const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(o->getCommandSetName()) : nullptr;
						for (int i = 0; set && i < CommandSet::MAX_BUTTONS && target; ++i)
						{
							const CommandButton *b = set->getCommandButton(i);
							const SpecialPowerTemplate *t = b && b->m_command == GUI_COMMAND_SPECIAL_POWER && TheSpecialPowerStore && !b->m_specialPowerName.empty()
								? TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName) : nullptr;
							if (t && t->getSpecialPowerType() == 0x1D)
							{
								m.game->commands().append(selectMessage(pi, o->getID()));
								GameMessage g(MSG_DO_SPECIAL_POWER_AT_OBJECT, pi);
								g.appendIntegerArgument((int)t->getID());
								g.appendObjectIDArgument(target->getID());
								g.appendIntegerArgument((int)b->m_options);
								g.appendObjectIDArgument(INVALID_ID);
								g.appendLocationArgument(*target->getPosition());
								m.game->commands().append(g);
								log.push_back(static_cast<const ThingTemplate *>(o->getTemplate())->getName() + " captures " +
											  static_cast<const ThingTemplate *>(target->getTemplate())->getName() + " at frame " + std::to_string(m.frame()));
								target = nullptr;
							}
						}
					}
					if (target)
					{
						log.push_back("no unit of the player's has a capture power at frame " + std::to_string(m.frame()));
					}
				}
			}
			else if (objectiveMarked(m, 5) && !objectiveMarked(m, 10))
			{
				buildStep();
			}
			return objectiveMarked(m, 10);
		},
		30000);
	for (const std::string &l : log)
	{
		MESSAGE(l);
	}
	for (int k = 1; k <= 11; ++k)
	{
		MESSAGE("tutorial objective " << k << std::string(objectiveMarked(m, k) ? " completed" : " open"));
	}
	printUnported(m, "Beginner Tutorial");
	CHECK(objectiveMarked(m, 1));  // camera, selection and movement
	CHECK(objectiveMarked(m, 2));  // attacking
	CHECK(objectiveMarked(m, 3));  // saving the outposts
	CHECK(objectiveMarked(m, 11)); // the Palantir (the objectives screen opened and closed)
	CHECK(objectiveMarked(m, 4));  // the creeps: the lairs' holes (RebuildHoleExposeDie)
	for (int k = 5; k <= 10; ++k)
	{
		CHECK_MESSAGE(objectiveMarked(m, k), "tutorial objective " << k);
	}
	CHECK(done);
	// the end: 'Final Attack - 07' sets PlyrCivilian's "Flag - Mission Completed", the map's 'Victory Condition (OVERRIDE ME)' sets "Mission Won", the
	// end-mission library ends the game won
	const bool ended = done && m.until([&] { return !m.engine().endRequests().empty(); }, 1500);
	MESSAGE("Mission Won " << m.flag("/Mission Won") << ", end requests " << m.engine().endRequests().size() << ", frame " << m.frame());
	REQUIRE(ended);
	CHECK(m.engine().endRequests().front().victory);
	m.game->setScriptLocalView(nullptr);
}

TEST_CASE("bfme2camp1 retail: a destroyed creep lair leaves its hole and is rebuilt (RebuildHoleExposeDie RW 0x889AAF, RebuildHoleBehavior RW 0x8868C2): "
		  "MAP BEGINNER TUTORIAL's Spider Pit 01 (SpiderLair) leaves a SpiderLairHole with its name and HoleMaxHealth 500, the hole counts WorkerRespawnDelay "
		  "(120 s = 600 frames) down and rebuilds the lair through the build assistant, then goes when the lair stands")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.loadTutorial(*s, "map beginner tutorial");
	m.step(2);
	Object *pit = m.unit("Spider Pit 01");
	REQUIRE(pit != nullptr);
	REQUIRE(static_cast<const ThingTemplate *>(pit->getTemplate())->getName() == "SpiderLair");
	const Coord3D at = *pit->getPosition();
	const ObjectID pitId = pit->getID();
	Player *creeps = pit->getControllingPlayer();
	REQUIRE(creeps != nullptr);
	auto holeAt = [&]() -> Object * {
		for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (static_cast<const ThingTemplate *>(o->getTemplate())->getName() == "SpiderLairHole" && o->getPosition()->x == at.x && o->getPosition()->y == at.y)
			{
				return o;
			}
		}
		return nullptr;
	};
	m.kill(*pit);
	m.step(1);
	Object *hole = holeAt();
	REQUIRE(hole != nullptr);
	CHECK(hole->getControllingPlayer() == creeps);                     // the dead lair's team
	CHECK(hole->getName() == "Spider Pit 01");                         // RW 0x759467
	CHECK(m.unit("Spider Pit 01") == hole);                            // the named cache follows the name
	CHECK(hole->getBodyModule()->getMaxHealth() == 500.0f);            // HoleMaxHealth (body slot 0x5C)
	RebuildHoleBehavior *rh = RebuildHoleBehavior::of(*hole);
	REQUIRE(rh != nullptr);
	CHECK(rh->getSpawnerID() == pitId);
	REQUIRE(rh->getRebuildTemplate() != nullptr);
	CHECK(rh->getRebuildTemplate()->getName() == "SpiderLair");
	const unsigned c1 = rh->workerWaitCounter();
	REQUIRE(c1 <= 600u); // tolerance: a precondition (_ftol2(120000 ms * 0.005) = 600, less the frames the hole already updated), not the measured value
	m.step(10);
	CHECK(rh->workerWaitCounter() == c1 - 10u); // one frame each update
	// the counter's end: the lair is rebuilt at the hole (TheBuildAssistant's build-now, then GettingBuiltBehavior builds it up from 1%)
	REQUIRE(m.until([&] { return rh->getReconstructedBuildingID() != INVALID_ID; }, (int)c1));
	Object *lair = m.logic().findObjectByID(rh->getReconstructedBuildingID());
	REQUIRE(lair != nullptr);
	CHECK(static_cast<const ThingTemplate *>(lair->getTemplate())->getName() == "SpiderLair");
	CHECK(lair->getPosition()->x == at.x);
	CHECK(lair->getPosition()->y == at.y);
	CHECK(lair->getControllingPlayer() == creeps);
	MESSAGE("the lair rebuilt at frame " << m.frame() << " with health " << lair->getBodyModule()->getHealth() << " of " << lair->getBodyModule()->getMaxHealth());
	// when the lair stands the hole goes and its name goes to the lair
	const ObjectID holeId = hole->getID();
	const bool done = m.until([&] {
		const Object *h = m.logic().findObjectByID(holeId);
		return !h || h->isEffectivelyDead();
	}, 6000);
	MESSAGE("the hole gone at frame " << m.frame());
	CHECK(done);
	CHECK(m.unit("Spider Pit 01") == lair);
	CHECK(lair->getName() == "Spider Pit 01");
	CHECK(!lair->isUnderConstruction());
}

TEST_CASE("bfme2camp1 retail: a defeated player's structures leave no holes (Player + 0x754 is set before killPlayer's kill pass, RW 0x6ABE7C; "
		  "RebuildHoleExposeDie's live-player test RW 0x889AF6): the creeps' GoblinCave and Spider Pits die without a hole, and nothing is rebuilt")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.loadTutorial(*s, "map beginner tutorial");
	m.step(2);
	Object *pit = m.unit("Spider Pit 01");
	REQUIRE(pit != nullptr);
	Player *creeps = pit->getControllingPlayer();
	REQUIRE(creeps != nullptr);
	const ThingTemplate *caveTmpl = m.logic().things().findTemplate("GoblinCave");
	REQUIRE(caveTmpl != nullptr);
	Object *cave = m.logic().newObject(caveTmpl, pit->getTeam(), ObjectStatusMaskType{});
	REQUIRE(cave != nullptr);
	Coord3D at = *pit->getPosition();
	at.x += 250.0f;
	at.z = m.logic().getGroundHeight(at.x, at.y);
	cave->setPosition(&at);
	m.step(1);
	auto holes = [&] {
		int n = 0;
		for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			n += (!o->isEffectivelyDead() && RebuildHoleBehavior::of(*o)) ? 1 : 0;
		}
		return n;
	};
	REQUIRE(holes() == 0); // precondition: no hole before
	m.logic().victory().killPlayer(creeps);
	CHECK(creeps->isDefeated());
	m.step(1);
	CHECK(holes() == 0); // the player was defeated before its objects died: no RebuildHoleExposeDie fired
	m.step(800);         // past WorkerRespawnDelay (600 frames)
	int rebuilt = 0;
	for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
	{
		const std::string n = static_cast<const ThingTemplate *>(o->getTemplate())->getName();
		rebuilt += (o->getControllingPlayer() == creeps && !o->isEffectivelyDead() && (n == "GoblinCave" || n == "SpiderLair")) ? 1 : 0;
	}
	CHECK(holes() == 0);
	CHECK(rebuilt == 0);
}

TEST_CASE("bfme2camp1 retail: a hole with a WorkerObjectName rebuilds through its worker (RebuildHoleBehavior RW 0x8868C2 -> the worker's AI slot 0x1F8, "
		  "DozerAIUpdate::construct RW 0x88C217 with the rebuild flag): a GoblinCave leaves a WildCaveHole whose MordorWorkerNoSelect builds the cave again, "
		  "unpaid, and the hole goes when it stands")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.loadTutorial(*s, "map beginner tutorial");
	m.step(2);
	Object *pit = m.unit("Spider Pit 01");
	REQUIRE(pit != nullptr);
	Player *creeps = pit->getControllingPlayer();
	REQUIRE(creeps != nullptr);
	const ThingTemplate *caveTmpl = m.logic().things().findTemplate("GoblinCave");
	REQUIRE(caveTmpl != nullptr);
	// a cave of the creeps' beside their pit
	Object *cave = m.logic().newObject(caveTmpl, pit->getTeam(), ObjectStatusMaskType{});
	REQUIRE(cave != nullptr);
	Coord3D at = *pit->getPosition();
	at.x += 250.0f;
	at.z = m.logic().getGroundHeight(at.x, at.y);
	cave->setPosition(&at);
	if (AIWorld *ai = m.logic().aiWorld())
	{
		ai->addObjectToPathfindMap(*cave);
	}
	m.step(1);
	const ObjectID caveId = cave->getID();
	auto holeAt = [&]() -> Object * {
		for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (static_cast<const ThingTemplate *>(o->getTemplate())->getName() == "WildCaveHole" && o->getPosition()->x == at.x && o->getPosition()->y == at.y)
			{
				return o;
			}
		}
		return nullptr;
	};
	m.kill(*cave);
	m.step(1);
	Object *hole = holeAt();
	REQUIRE(hole != nullptr);
	CHECK(hole->getBodyModule()->getMaxHealth() == 500.0f); // the cave's HoleMaxHealth
	RebuildHoleBehavior *rh = RebuildHoleBehavior::of(*hole);
	REQUIRE(rh != nullptr);
	CHECK(rh->getSpawnerID() == caveId);
	const unsigned c1 = rh->workerWaitCounter();
	REQUIRE(c1 <= 600u); // tolerance: a precondition (WorkerRespawnDelay 120000 ms = 600 frames, less the frames already updated)
	const unsigned money = creeps->getMoney()->countMoney();
	REQUIRE(m.until([&] { return rh->getReconstructedBuildingID() != INVALID_ID; }, (int)c1 + 5));
	Object *worker = m.logic().findObjectByID(rh->getWorkerID());
	REQUIRE(worker != nullptr);
	CHECK(static_cast<const ThingTemplate *>(worker->getTemplate())->getName() == "MordorWorkerNoSelect");
	CHECK(worker->getControllingPlayer() == creeps);
	Object *rebuilt = m.logic().findObjectByID(rh->getReconstructedBuildingID());
	REQUIRE(rebuilt != nullptr);
	CHECK(static_cast<const ThingTemplate *>(rebuilt->getTemplate())->getName() == "GoblinCave");
	CHECK(rebuilt->getBuilderID() == worker->getID()); // the worker's construct (RW 0x88D307)
	CHECK(rebuilt->getProducerID() == hole->getID());  // then the hole (RW 0x68B6A1)
	CHECK(creeps->getMoney()->countMoney() == money);  // a rebuild pays nothing (RW 0x88D2F9)
	MESSAGE("the worker's rebuild began at frame " << m.frame());
	// the phantom is placed when the worker arrives (RW 0x88D44F): no legality test (the hole stands there), UNDER_CONSTRUCTION and RECONSTRUCTING
	static const int reconstructing = ObjectTemplateInfoBuilder::objectStatusIndex("RECONSTRUCTING");
	REQUIRE(m.until([&] { return rebuilt->isUnderConstruction(); }, 600));
	CHECK(rebuilt->testStatus((unsigned)reconstructing));
	MESSAGE("the worker placed it at frame " << m.frame());
	// the worker builds it up; the hole goes when it stands
	const ObjectID holeId = hole->getID();
	const bool done = m.until([&] {
		const Object *h = m.logic().findObjectByID(holeId);
		return !h || h->isEffectivelyDead();
	}, 6000);
	MESSAGE("the hole gone at frame " << m.frame());
	REQUIRE(done);
	CHECK(!rebuilt->isUnderConstruction());
	CHECK(rebuilt->getConstructionPercent() == -1.0f); // complete (RW 0x88DEE0 ff: percent -1)
}

TEST_CASE("bfme2camp1 retail: MAP BEGINNER TUTORIAL's setup: CREATE_UNIT_REVIVAL_ENTRY_AT_LEVEL (RW 0x7C6D8E) gives the local player a GondorBoromir record "
		  "of level 7 (its rank and RequiredExperience), HUMAN_IMPASSABLE_AREA (RW 0x7C4040 -> RW 0x934D51) marks the area's ground cells impassable to "
		  "players (bit 18)")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.loadTutorial(*s, "map beginner tutorial");
	m.step(2);
	Player *me = m.logic().players().getLocalPlayer();
	REQUIRE(me != nullptr);
	const ThingTemplate *boromir = m.logic().things().findTemplate("GondorBoromir");
	REQUIRE(boromir != nullptr);
	// the action's record is appended (RW 0x781861) after the player's buildable heroes (HeroSystem::initPlayer RW 0x6B16EC: a tutorial is game kind 3 with
	// no current campaign, so the Men's BuildableHeroesMP records come first): the last GondorBoromir record
	int index = -1;
	for (int i = 0; i < (int)me->heroes().size(); ++i)
	{
		index = me->heroes().at(i)->templateName == "GondorBoromir" ? i : index;
	}
	REQUIRE(index >= 0); // tolerance: a precondition (the record exists), its index is not what is tested
	const HeroRecord *r = me->heroes().at(index);
	REQUIRE(r != nullptr);
	// the expected values from the experience levels themselves: the first level of GondorBoromir's list valid for a single player game with rank 7
	const ExperienceLevelTemplate *level7 = nullptr;
	REQUIRE(TheExperienceLevelSystem != nullptr);
	for (const ExperienceLevelTemplate *l : *TheExperienceLevelSystem->levelsFor("GondorBoromir"))
	{
		const ExperienceLevelTemplate *f = l->getFinal();
		if (!f->m_multiPlayerOnly && f->m_rank == 7)
		{
			level7 = f;
			break;
		}
	}
	REQUIRE(level7 != nullptr);
	CHECK(r->rank == 7);
	CHECK(r->baseRank == 7);
	CHECK(r->experience == (float)level7->m_requiredExperience);
	CHECK(r->templateName == "GondorBoromir");
	// the impassable area: the ground cell under a point inside it
	AIWorld *ai = m.logic().aiWorld();
	REQUIRE(ai != nullptr);
	const TriggerArea *area = m.engine().findTrigger("Area Trigger - Impassable Area");
	REQUIRE(area != nullptr);
	float loX = area->points[0].x, loY = area->points[0].y, hiX = loX, hiY = loY;
	for (const Point2F &q : area->points)
	{
		loX = std::min(loX, q.x);
		loY = std::min(loY, q.y);
		hiX = std::max(hiX, q.x);
		hiY = std::max(hiY, q.y);
	}
	int inside = 0, marked = 0;
	for (int cx = (int)loX / 10; cx <= (int)hiX / 10 + 1; ++cx)
	{
		for (int cy = (int)loY / 10; cy <= (int)hiY / 10 + 1; ++cy)
		{
			const PathfindCell *cell = ai->pathfinder().getCell(LAYER_GROUND, cx, cy);
			if (cell && ScriptConditions::pointInTrigger(*area, (float)(cx * 10 + 5), (float)(cy * 10 + 5)))
			{
				++inside;
				marked += cell->getImpassableToPlayers() ? 1 : 0;
			}
		}
	}
	MESSAGE("the impassable area's cells: " << inside);
	REQUIRE(inside > 0); // tolerance: a precondition (the area covers cells)
	CHECK(marked == inside); // every ground cell whose centre is in the area
	CHECK(m.engine().stats().unportedActions.count("HUMAN_IMPASSABLE_AREA") == 0);
	CHECK(m.engine().stats().unportedActions.count("CREATE_UNIT_REVIVAL_ENTRY_AT_LEVEL") == 0);
}

// lane STABLEQA-1 (the stable candidate's campaign sweep): BFME2's Evil Erebor (EVIL_CAMPAIGN mission 7) marches its Dwarven porters along a team waypoint
// path in formation, so each keeps its formation offset as the path's group offset (+ 0x4C / + 0x50, RW 0x750790 .. 0x7507A2) and adds it to every later
// waypoint's goal. The offset was hashed but not saved: a save taken during the march loaded with no offset, and the save's own hash check refused it.
// The snapshot round trip must give the saved game back exactly (AIFollowWaypointPathState xfer version 2)
TEST_CASE("stableqa1: Evil Erebor's porters marching on a team path in formation keep their group offset through a save and load")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.loadCampaignMission(*s, "EVIL_CAMPAIGN", 6);
	m.step(900);
	int marching = 0;
	for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
	{
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		const AIStateMachine *sm = ai ? ai->stateMachineOrNull() : nullptr;
		marching += sm && dynamic_cast<const AIFollowWaypointPathState *>(sm->currentState()) && ai->formationId() != 0 ? 1 : 0;
	}
	REQUIRE(marching > 0); // tolerance: a precondition (the scene: units on a team path in formation at frame 900)
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(m.logic().saveSnapshot(bytes, &error), error);
	const std::uint32_t saved = m.logic().computeStateHash();
	m.step(30);
	REQUIRE(m.logic().computeStateHash() != saved); // tolerance: a precondition (the game moved on)
	REQUIRE_MESSAGE(m.game->loadSnapshot(bytes, &error), error);
	CHECK(m.logic().computeStateHash() == saved);
}
