// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-4b (docs/SAVES.md section 6, tests T1, T2, T4, T5 and T8 as far as SAVE-4b's blocks allow): the objects and their modules (obfme:Objects),
// the scheduler (obfme:UpdateScheduler) and the object-side subsystems (obfme:TunnelTracker, obfme:Combat, obfme:SellList, obfme:Experience,
// obfme:Invisibility, obfme:DamageOverTime) on synthetic games: every ported behaviour module class saved through its own xfer and loaded into another
// game (T4), an object game loaded into the same START's game with other objects (T1 / T2), each subsystem block with non-default values, every load
// failure named (T5), the same bytes under Uber at 1, 4 and 16 threads (T8), and the stops reported.

#include "doctest.h"

#include "LogicTestUtil.h"

#include "Common/GameState.h"
#include "Common/JobSystem.h"
#include "Common/Snapshot.h"
#include "Common/StateHash.h"
#include "Common/Thing/RwFieldTables.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/CommandLogLoad.h"
#include "GameClient/MapHordeSpawn.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/BuildAssistantSell.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/ExperienceWorld.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/TunnelContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/System/DOTManager.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/SnapshotXfer.h"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
const std::vector<std::string> kCore = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random" };
const std::vector<std::string> kSave4b = { "obfme:Objects", "obfme:UpdateScheduler", "obfme:TunnelTracker", "obfme:Combat", "obfme:SellList",
	"obfme:Experience", "obfme:Invisibility", "obfme:DamageOverTime" };

std::vector<std::string> coreAndSave4b()
{
	std::vector<std::string> v = kCore;
	v.insert(v.end(), kSave4b.begin(), kSave4b.end());
	return v;
}

void runFrames(GameLogic &logic, int n)
{
	for (int i = 0; i < n; ++i)
	{
		for (int phase = 1; phase <= 6; ++phase)
		{
			logic.update(phase);
		}
	}
}

std::map<std::string, std::uint32_t> sectionValues(const GameLogic &logic)
{
	std::vector<GameLogic::StateHashSection> sections;
	logic.computeStateHashBreakdown(sections, nullptr);
	std::map<std::string, std::uint32_t> out;
	for (const GameLogic::StateHashSection &s : sections)
	{
		out[s.name] = s.value;
	}
	return out;
}

struct ObjectRecord
{
	ObjectID id;
	std::string templateName;
	std::uint32_t hash;
	bool operator==(const ObjectRecord &o) const { return id == o.id && templateName == o.templateName && hash == o.hash; }
};

std::vector<ObjectRecord> objectRecords(const GameLogic &logic)
{
	std::vector<ObjectRecord> out;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		StateHasher h;
		o->crc(h);
		out.push_back(ObjectRecord{ o->getID(), o->getTemplate()->getName(), h.value() });
	}
	return out;
}

// a LogicWorld with the ported module runtimes (RetailObjectWorld's registry: the horde contain data, LogicModules::registerAll) and `objects` loaded
struct S4World : logictest::LogicWorld
{
	explicit S4World(const std::string &objects)
	{
		MapHordeSpawn::bindHordeContainData(w.modules);
		LogicModules::registerAll(w.modules);
		logic->settings().bodyThresholdsLoaded = true; // GameData's (ActiveBody's default damage thresholds read them)
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.35f;
		ProductionSettings &prod = logic->productionSettings(); // GameData's build time settings (ProductionUpdate reads them while it runs)
		prod.loaded = true;
		prod.minLowEnergyProductionSpeed = 0.5f;
		prod.maxLowEnergyProductionSpeed = 0.8f;
		prod.lowEnergyPenaltyModifier = 1.0f;
		prod.multipleFactory = 1.0f;
		for (int i = 0; i < 8; ++i)
		{
			prod.multiPlayUnitSpeedMult[i] = 1.0f;
			prod.multiPlayBuildingSpeedMult[i] = 1.0f;
		}
		prod.multiPlayEntries = 8;
		const std::string err = w.load(objects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
};

std::string loadError(logictest::LogicWorld &w, const std::vector<std::string> &tokens, const std::vector<std::uint8_t> &stream)
{
	w.activateUpgrades();
	try
	{
		w.logic->gameState().loadBlocks(tokens, stream);
		return "(loaded)";
	}
	catch (const XferException &e)
	{
		return e.what();
	}
}

const char kObjects[] =
	"Object Keep\n"
	"  KindOf = STRUCTURE\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = LifetimeUpdate ModuleTag_Life\n"
	"    MinLifetime = 600000\n"
	"    MaxLifetime = 600000\n"
	"  End\n"
	"End\n"
	"Object Soldier\n"
	"  KindOf = INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = DeletionUpdate ModuleTag_Delete\n"
	"    MinLifetime = 400000\n"
	"    MaxLifetime = 400000\n"
	"  End\n"
	"End\n"
	"Object Rock\n"
	"  Body = InactiveBody ModuleTag_Body\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("save4b: the stops S-4190 .. S-4193 are reported with the snapshot's (CommandLogLoad::stopLines)")
{
	const std::vector<std::string> mine = GameLogic::objectSnapshotStopLines();
	REQUIRE(mine.size() == 4);
	const char *ids[4] = { "[S-4190] ", "[S-4191] ", "[S-4192] ", "[S-4193] " };
	const std::vector<std::string> all = CommandLogLoad::stopLines();
	for (size_t i = 0; i < 4; ++i)
	{
		CHECK(mine[i].rfind(ids[i], 0) == 0);
		CHECK(std::count(all.begin(), all.end(), mine[i]) == 1);
	}
}

TEST_CASE("save4b T4: every ported behaviour module class of the registry, on an object of its own, saves through an xfer of its own, loads into "
		  "another game of the same data with every object made again, hashes as saved and saves the same bytes; the AI update family is SAVE-4c's")
{
	// one object per class: every ported behaviour class of the registry (ModuleFactory::ModuleTemplate::portedModule), each with an ActiveBody unless it is
	// a body itself
	std::string ini;
	std::vector<std::string> classes;
	{
		S4World probe("");
		for (const RwClass &cls : RwBinaryData::embedded().classes)
		{
			if (cls.type != MODULETYPE_BEHAVIOR)
			{
				continue;
			}
			const ModuleFactory::ModuleTemplate *mt = probe.w.modules.findModuleTemplate(cls.name, MODULETYPE_BEHAVIOR);
			REQUIRE(mt);
			if (!mt->portedModule || std::find(classes.begin(), classes.end(), cls.name) != classes.end())
			{
				continue;
			}
			classes.push_back(cls.name);
			const bool body = (mt->interfaceMask & MODULEINTERFACE_BODY) != 0;
			ini += "Object T4_" + cls.name + "\n";
			if (!body)
			{
				ini += "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n";
			}
			ini += std::string(body ? "  Body = " : "  Behavior = ") + cls.name + " ModuleTag_T4\n  End\nEnd\n";
		}
	}
	S4World saved(ini);
	// a synthetic game has no AIWorld: the AI update family's classes are made as their unported shells there (AIUpdate.cpp's registration), which
	// save; their own runtime is SAVE-4c's (test_save4b_retail.cpp sets them aside on a real game). Every object is saved as made (a frame would let the
	// short-lived ones, DeletionUpdate's and LifetimeUpdate's with their default lifetimes, die before the save)
	std::vector<std::string> failed;
	for (const std::string &c : classes)
	{
		try
		{
			saved.make("T4_" + c, saved.teamOf("Alice"));
		}
		catch (const std::exception &e)
		{
			failed.push_back(c + ": " + e.what());
		}
	}
	MESSAGE(classes.size() << " ported behaviour classes");
	CHECK(failed.empty());
	for (const std::string &f : failed)
	{
		MESSAGE("not made: " << f);
	}
	const std::vector<ObjectRecord> before = objectRecords(*saved.logic);
	std::set<std::string> savedClasses;
	for (const Object *o = saved.logic->getFirstObject(); o; o = o->getNextObject())
	{
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			savedClasses.insert(m->getModuleClassName());
		}
	}
	for (const std::string &c : classes)
	{
		CHECK_MESSAGE(savedClasses.count(c) == 1, c);
	}
	std::vector<std::uint8_t> stream;
	saved.logic->gameState().saveBlocks(coreAndSave4b(), stream);
	S4World loaded(ini);
	CHECK(loaded.logic->getObjectCount() == 0);
	loaded.activateUpgrades();
	loaded.logic->gameState().loadBlocks(coreAndSave4b(), stream);
	CHECK(objectRecords(*loaded.logic) == before);
	std::vector<std::uint8_t> again;
	loaded.logic->gameState().saveBlocks(coreAndSave4b(), again);
	CHECK(again == stream);
	// review r2: the same with the modules' state changed from their constructors' (every object hit, a status set, 30 frames run: the update modules run,
	// the bodies change their health and damage state, the short-lived objects go). The changed game saves other bytes, and loads, hashes and saves as it is
	saved.activateUpgrades();
	for (Object *o = saved.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (BodyModuleInterface *b = o->getBodyModule())
		{
			DamageInfo hit;
			hit.m_input.m_damageType = DAMAGE_SLASH;
			hit.m_input.m_amount = 30.0f;
			b->attemptDamage(hit);
		}
		o->setStatus(7, true);
	}
	runFrames(*saved.logic, 30);
	const std::vector<ObjectRecord> changed = objectRecords(*saved.logic);
	std::vector<std::uint8_t> changedStream;
	saved.logic->gameState().saveBlocks(coreAndSave4b(), changedStream);
	CHECK(changedStream != stream);
	S4World loadedChanged(ini);
	loadedChanged.activateUpgrades();
	loadedChanged.logic->gameState().loadBlocks(coreAndSave4b(), changedStream);
	CHECK(objectRecords(*loadedChanged.logic) == changed);
	std::vector<std::uint8_t> changedAgain;
	loadedChanged.logic->gameState().saveBlocks(coreAndSave4b(), changedAgain);
	CHECK(changedAgain == changedStream);
	saved.activateUpgrades();
}

namespace
{
const std::vector<std::string> kObjectSections = { "frame, object ids, RNG", "players and teams", "objects", "scheduler" };

// the saved game: objects made, changed, destroyed and made again after the START
void playObjects(S4World &f)
{
	GameLogic &logic = *f.logic;
	Object *keep = f.make("Keep", f.teamOf("Alice"));
	Object *s1 = f.make("Soldier", f.teamOf("Bob"));
	Object *s2 = f.make("Soldier", f.teamOf("Bob"));
	f.make("Rock");
	runFrames(logic, 3);
	Coord3D at{ 120.5f, -33.25f, 4.0f };
	s1->setPosition(&at);
	s1->setOrientation(1.25f);
	static_cast<ActiveBody *>(s1->getBodyModule())->friend_setHealthRaw(55.5f);
	s1->setStatus(7, true);
	s1->setModelConditionState(40, true);
	s1->setCapturerID(keep->getID());
	keep->setName("TheKeep");
	logic.destroyObject(s2); // gone at the next phase 5
	runFrames(logic, 2);
	Object *late = f.make("Rock", f.teamOf("Alice"));
	Coord3D lateAt{ -5.0f, 7.5f, 0.0f };
	late->setPosition(&lateAt);
	runFrames(logic, 4);
}
} // namespace

TEST_CASE("save4b objects T1 / T2: a game's objects (made, changed, destroyed and made after the START) load into the same START's game holding other "
		  "objects; each object, the scheduler and the frame state hash as saved, the objects are the saved ones by name and state, both games go on equal, "
		  "and the bytes save again identically")
{
	S4World saved(kObjects);
	playObjects(saved);
	S4World fresh(kObjects);
	fresh.make("Soldier", fresh.teamOf("Alice")); // an object the saved game does not have (its id is the saved Keep's)
	fresh.make("Keep", fresh.teamOf("Bob"));
	runFrames(*fresh.logic, 1);
	const std::vector<ObjectRecord> before = objectRecords(*saved.logic);
	const std::map<std::string, std::uint32_t> savedSections = sectionValues(*saved.logic);
	REQUIRE_FALSE(objectRecords(*fresh.logic) == before);

	saved.activateUpgrades();
	std::vector<std::uint8_t> stream;
	saved.logic->gameState().saveBlocks(coreAndSave4b(), stream);
	fresh.activateUpgrades();
	fresh.logic->gameState().loadBlocks(coreAndSave4b(), stream);
	CHECK(objectRecords(*fresh.logic) == before);
	const std::map<std::string, std::uint32_t> loadedSections = sectionValues(*fresh.logic);
	for (const std::string &name : kObjectSections)
	{
		CHECK_MESSAGE(loadedSections.at(name) == savedSections.at(name), name);
	}
	// the effects in the world, by name
	GameLogic &logic = *fresh.logic;
	CHECK(logic.getFrame() == 9);
	REQUIRE(logic.getObjectCount() == 4);
	std::vector<std::string> names;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		names.push_back(std::to_string(o->getID()) + " " + o->getTemplate()->getName() + " " + (o->getTeam() ? o->getTeam()->getName() : std::string()));
	}
	CHECK(names == std::vector<std::string>{ "1 Keep " + saved.teamOf("Alice")->getName(), "2 Soldier " + saved.teamOf("Bob")->getName(),
					   "4 Rock " + logic.players().getNeutralPlayer()->getDefaultTeam()->getName(), "5 Rock " + saved.teamOf("Alice")->getName() });
	const Object *s1 = logic.findObjectByID(2);
	REQUIRE(s1);
	CHECK(s1->getPosition()->x == 120.5f);
	CHECK(s1->getPosition()->y == -33.25f);
	CHECK(s1->getOrientation() == saved.logic->findObjectByID(2)->getOrientation());
	CHECK(s1->getBodyModule()->getHealth() == 55.5f);
	CHECK(s1->testStatus(7));
	CHECK(s1->testModelCondition(40));
	CHECK(s1->getCapturerID() == 1);
	CHECK(logic.findObjectByID(1)->getName() == "TheKeep");
	CHECK(logic.findObjectByID(3) == nullptr);
	CHECK(logic.findObjectByID(5)->getPosition()->x == -5.0f);
	// T2
	std::vector<std::uint8_t> again;
	logic.gameState().saveBlocks(coreAndSave4b(), again);
	CHECK(again == stream);
	// T1 as far as these blocks allow: both games go on and their objects and SAVE-4b's sections stay equal (the deletion and lifetime timers run)
	saved.activateUpgrades();
	runFrames(*saved.logic, 30);
	fresh.activateUpgrades();
	runFrames(*fresh.logic, 30);
	CHECK(objectRecords(*fresh.logic) == objectRecords(*saved.logic));
	const std::map<std::string, std::uint32_t> a = sectionValues(*saved.logic), b = sectionValues(*fresh.logic);
	for (const std::string &name : kObjectSections)
	{
		CHECK_MESSAGE(a.at(name) == b.at(name), name);
	}
}

TEST_CASE("save4b subsystem blocks round-trip with non-default values: combat, the sell list, the delayed experience grants, invisibility, damage over "
		  "time each load into the same START's game with their section's hash and their state, and save the same bytes")
{
	S4World saved(kObjects);
	Object *keep = saved.make("Keep", saved.teamOf("Alice"));
	Object *soldier = saved.make("Soldier", saved.teamOf("Alice"));
	runFrames(*saved.logic, 2);
	S4World fresh(kObjects);
	fresh.make("Keep", fresh.teamOf("Alice"));
	fresh.make("Soldier", fresh.teamOf("Alice"));
	runFrames(*fresh.logic, 2);
	const auto roundTrip = [&](const std::vector<std::string> &tokens, const char *section) {
		saved.activateUpgrades();
		std::vector<std::uint8_t> stream;
		saved.logic->gameState().saveBlocks(tokens, stream);
		const std::map<std::string, std::uint32_t> want = sectionValues(*saved.logic);
		fresh.activateUpgrades();
		const std::map<std::string, std::uint32_t> prior = sectionValues(*fresh.logic);
		REQUIRE((prior.count(section) == 0 || prior.at(section) != want.at(section))); // the loaded game differs in the section before the load
		fresh.logic->gameState().loadBlocks(tokens, stream);
		const std::map<std::string, std::uint32_t> got = sectionValues(*fresh.logic);
		REQUIRE(got.count(section) == 1);
		CHECK_MESSAGE(got.at(section) == want.at(section), section);
		std::vector<std::uint8_t> again;
		fresh.logic->gameState().saveBlocks(tokens, again);
		CHECK_MESSAGE(again == stream, section);
	};
	SUBCASE("combat")
	{
		CombatState &c = saved.logic->combat();
		c.counters().kills = 7;
		c.counters().crushes = 3;
		c.counters().shockwaveRampageKills = 2;
		c.recordCastleBreach(12, 3, 1);
		c.setAutoAcquireEnabled(false);
		roundTrip({ "obfme:Combat" }, "combat");
		const CombatState &f = fresh.logic->combat();
		CHECK(f.counters().kills == 7);
		CHECK(f.counters().crushes == 3);
		CHECK_FALSE(f.autoAcquireEnabled());
		REQUIRE(f.castleBreaches().size() == 1);
		CHECK(f.castleBreaches()[0].objectId == 3);
	}
	SUBCASE("the sell list (its entries are made by a sale; here written in the block's layout: version, count, then id and frame, newest first)")
	{
		XferSave w;
		std::uint8_t version = 1;
		std::uint32_t count = 2, id1 = keep->getID(), frame1 = 2, id2 = soldier->getID(), frame2 = 1;
		w.xferUnsignedByte(&version);
		w.xferUnsignedInt(&count);
		w.xferUnsignedInt(&id1);
		w.xferUnsignedInt(&frame1);
		w.xferUnsignedInt(&id2);
		w.xferUnsignedInt(&frame2);
		XferLoad r(w.data());
		saved.logic->sellList().xfer(r);
		REQUIRE(r.atEnd());
		REQUIRE(saved.logic->sellList().list().size() == 2);
		roundTrip({ "obfme:SellList" }, "sell list");
		const std::deque<BuildAssistantSell::Entry> &l = fresh.logic->sellList().list();
		REQUIRE(l.size() == 2);
		CHECK(l[0].id == keep->getID());
		CHECK(l[0].frame == 2);
		CHECK(l[1].id == soldier->getID());
		XferSave back;
		fresh.logic->sellList().xfer(back);
		CHECK(back.data() == w.data());
	}
	SUBCASE("the delayed experience level grants (the level by name)")
	{
		ExperienceLevelSystem levels;
		levels.registerBlocks(saved.w.fx.env.blocks);
		ExperienceLevelSystem *before = TheExperienceLevelSystem;
		TheExperienceLevelSystem = &levels;
		const std::string err = saved.w.load("ExperienceLevel SoldierLevel2\n  TargetNames = Soldier\n  RequiredExperience = 50\n  Rank = 2\nEnd\n", INI_LOAD_OVERWRITE, "xp.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		saved.logic->experience().queueLevelGrant(*levels.findLevel("SoldierLevel2"), *soldier, true);
		saved.logic->experience().setSuspended(true);
		roundTrip({ "obfme:Experience" }, "experience");
		CHECK(fresh.logic->experience().pendingGrants() == 1);
		TheExperienceLevelSystem = before;
	}
	SUBCASE("invisibility (the applied nugget's copy travels with the entry)")
	{
		static InvisibilityNugget nugget;
		nugget.invisibilityType = InvisibilityNugget::STEALTH;
		nugget.detectionRange = 75.0f;
		nugget.forbiddenConditions = InvisibilityNugget::MOVING | InvisibilityNugget::FIRING_ANY;
		nugget.options = InvisibilityNugget::DETECTED_BY_FRIENDLIES;
		nugget.becomeStealthedFX = "FX_Stealth";
		saved.logic->invisibility().applyNugget(soldier, 150, nugget);
		REQUIRE(saved.logic->invisibility().appliedCount(soldier->getID()) == 1);
		roundTrip({ "obfme:Invisibility" }, "invisibility");
		CHECK(fresh.logic->invisibility().appliedCount(soldier->getID()) == 1);
	}
	SUBCASE("damage over time")
	{
		DOTManager::Record r;
		r.info.m_input.m_sourceID = keep->getID();
		r.info.m_input.m_damageType = 3;
		r.info.m_input.m_amount = 4.5f;
		r.interval = 5;
		r.endFrame = 90;
		r.nextFrame = 7;
		saved.logic->dot().add(soldier->getID(), r);
		roundTrip({ "obfme:DamageOverTime" }, "damage over time");
		REQUIRE(fresh.logic->dot().entries().size() == 1);
		const DOTManager::Record &got = fresh.logic->dot().entries().at(soldier->getID());
		CHECK(got.info.m_input.m_amount == 4.5f);
		CHECK(got.info.m_input.m_sourceID == keep->getID());
		CHECK(got.endFrame == 90);
		CHECK(got.nextFrame == 7);
	}
}

namespace
{
// an ActiveBody subclass that declares no xfer of its own
class InheritingBody : public ActiveBody
{
public:
	using ActiveBody::ActiveBody;
};
} // namespace

TEST_CASE("save4b T5: every load failure of SAVE-4b's blocks is a named error (the version window, another game's data, another module list, ids, a class "
		  "without its own xfer, a tunnel network nemesis)")
{
	S4World saved(kObjects);
	playObjects(saved);
	saved.activateUpgrades();
	std::vector<std::uint8_t> stream;
	saved.logic->gameState().saveBlocks(coreAndSave4b(), stream);
	SUBCASE("the version window of the Objects block (RW 0xA1F5B9's two messages)")
	{
		std::vector<std::uint8_t> objectsOnly;
		saved.logic->gameState().saveBlocks({ "obfme:Objects" }, objectsOnly);
		const size_t versionAt = 16 + 1 + std::string("obfme:Objects").size() + 8; // the header, the token, "BLOK" and the end offset
		REQUIRE(objectsOnly[versionAt] == 1);
		S4World other(kObjects);
		objectsOnly[versionAt] = 2;
		CHECK(loadError(other, { "obfme:Objects" }, objectsOnly) == "block 'obfme:Objects': Don't know how to load version 2 (most recent is 1)");
		objectsOnly[versionAt] = 0;
		CHECK(loadError(other, { "obfme:Objects" }, objectsOnly) == "block 'obfme:Objects': Found version 0 but the earliest supported is 1");
	}
	SUBCASE("object ids at or past the game's next object id (the Objects block without the core before it)")
	{
		std::vector<std::uint8_t> objectsOnly;
		saved.logic->gameState().saveBlocks({ "obfme:Objects" }, objectsOnly);
		S4World other(kObjects);
		CHECK(loadError(other, { "obfme:Objects" }, objectsOnly) == "block 'obfme:Objects': the snapshot's object id 1 is invalid, repeated or not below the next object id 1");
	}
	SUBCASE("a template this game's data lacks")
	{
		S4World other(std::string(kObjects).substr(0, std::string(kObjects).find("Object Rock")));
		CHECK(loadError(other, coreAndSave4b(), stream) == "block 'obfme:Objects': the snapshot's object 4 is of the template 'Rock', which this game's data lacks");
	}
	SUBCASE("another module list: another tag, another count")
	{
		const std::string base = std::string(kObjects).substr(0, std::string(kObjects).find("Object Rock"));
		S4World tag(base + "Object Rock\n  Body = InactiveBody ModuleTag_Other\n  End\nEnd\n");
		CHECK(loadError(tag, coreAndSave4b(), stream) == "block 'obfme:Objects': the snapshot's object 4 'Rock' has the module 'ModuleTag_Body' where this one has 'ModuleTag_Other'");
		S4World count(base + "Object Rock\n  Body = InactiveBody ModuleTag_Body\n  End\n  Behavior = DeletionUpdate ModuleTag_Delete\n  End\nEnd\n");
		const std::string err = loadError(count, coreAndSave4b(), stream);
		CHECK(err.find("block 'obfme:Objects': the snapshot's object 4 'Rock' has ") == 0);
		CHECK(err.find(" modules, this one ") != std::string::npos);
	}
	SUBCASE("a module class that would inherit its xfer is refused at the save, named")
	{
		S4World w(kObjects);
		w.w.modules.bindModuleProc("ActiveBody", MODULETYPE_BEHAVIOR, [](Thing *t, const ModuleData *d, const ModuleFactory::ModuleTemplate &) {
			return std::unique_ptr<Module>(new InheritingBody(t, dynamic_cast<const ActiveBodyModuleData *>(d)));
		});
		w.make("Soldier", w.teamOf("Alice"));
		std::vector<std::uint8_t> out;
		std::string err;
		try
		{
			w.logic->gameState().saveBlocks({ "obfme:Objects" }, out);
		}
		catch (const XferException &e)
		{
			err = e.what();
		}
		CHECK(err == "block 'obfme:Objects': the module class ActiveBody (tag 'ModuleTag_Body') of 'Soldier' has no xfer of its own (it would inherit its base "
					 "class's)");
	}
	SUBCASE("a tunnel network with a nemesis (RotWK's fields after the tunnel count, not ported: S-1107)")
	{
		S4World w(kObjects);
		Object *keep = w.make("Keep", w.teamOf("Bob"));
		w.players.findPlayerWithName("Bob")->tunnelTracker().onTunnelCreated(*keep); // Bob is the last player with a network: its record ends the block
		std::vector<std::uint8_t> tunnels;
		w.logic->gameState().saveBlocks({ "obfme:TunnelTracker" }, tunnels);
		const size_t nemesisAt = tunnels.size() - 7 - 8; // before the end token ("SG_EOF" and its length byte): the nemesis id, then its frame
		REQUIRE(tunnels[nemesisAt] == 0);
		tunnels[nemesisAt] = 5;
		S4World other(kObjects);
		CHECK(loadError(other, { "obfme:TunnelTracker" }, tunnels)
			== "block 'obfme:TunnelTracker': the snapshot's tunnel network has a nemesis (object 5), which OpenBFME does not port (S-1107)");
	}
}

TEST_CASE("save4b T8 as far as SAVE-4b allows: its blocks' bytes are the same under Uber at 1, 4 and 16 logic threads")
{
	const int before = JobSystem::logic().threadCount();
	std::vector<std::uint8_t> first;
	for (int threads : { 1, 4, 16 })
	{
		JobSystem::logic().setThreadCount(threads);
		S4World w(kObjects);
		w.logic->setSimulationProfile(SimulationProfile::Uber);
		playObjects(w);
		std::vector<std::uint8_t> stream;
		w.logic->gameState().saveBlocks(coreAndSave4b(), stream);
		if (first.empty())
		{
			first = stream;
		}
		CHECK(stream == first);
	}
	JobSystem::logic().setThreadCount(before);
}

TEST_CASE("save4b hostile inputs: an oversized count, a dangling or repeated reference and an out-of-range enum value are each refused by name, with nothing "
		  "allocated, removed or linked")
{
	S4World saved(kObjects);
	playObjects(saved);
	saved.activateUpgrades();
	SUBCASE("an oversized object table: refused by its count before the game's objects are touched")
	{
		std::vector<std::uint8_t> stream;
		saved.logic->gameState().saveBlocks(coreAndSave4b(), stream);
		std::vector<std::uint8_t> objectsOnly;
		saved.logic->gameState().saveBlocks({ "obfme:Objects" }, objectsOnly);
		const size_t countAt = 16 + 1 + std::string("obfme:Objects").size() + 8 + 1; // the header, the token, "BLOK", its end, the version byte
		REQUIRE(objectsOnly[countAt] == 4);
		objectsOnly[countAt] = 0xFF;
		objectsOnly[countAt + 1] = 0xFF;
		objectsOnly[countAt + 2] = 0xFF;
		objectsOnly[countAt + 3] = 0x7F;
		S4World other(kObjects);
		Object *mine = other.make("Soldier", other.teamOf("Alice"));
		const std::string err = loadError(other, { "obfme:Objects" }, objectsOnly);
		CHECK(err.find("block 'obfme:Objects': a list declares 2147483647 entries of at least 29 bytes, more than the ") == 0);
		CHECK(other.logic->getObjectCount() == 1);
		CHECK(other.logic->getFirstObject() == mine);
	}
	SUBCASE("a tunnel network's dangling rider and a rider listed twice: refused, the network's riders unchanged")
	{
		Object *keep = saved.logic->findObjectByID(1);
		Object *soldier = saved.logic->findObjectByID(2);
		REQUIRE(keep);
		REQUIRE(soldier);
		TunnelTracker &t = saved.players.findPlayerWithName("Alice")->tunnelTracker();
		t.addToContainList(soldier);
		const auto network = [](std::int32_t riders, std::vector<ObjectID> ids) {
			XferSave w;
			std::uint8_t one = 1;
			std::uint16_t none = 0;
			std::uint32_t zero = 0;
			w.xferUnsignedByte(&one);     // the tracker's version
			w.xferUnsignedByte(&one);     // the tunnel id list's version
			w.xferUnsignedShort(&none);   // no tunnels
			w.xferInt(&riders);
			for (ObjectID &id : ids)
			{
				w.xferObjectID(&id);
			}
			w.xferUnsignedInt(&zero); // the tunnel count
			w.xferUnsignedInt(&zero); // the nemesis and its frame
			w.xferUnsignedInt(&zero);
			return w.data();
		};
		const auto loadInto = [&](const std::vector<std::uint8_t> &bytes) {
			XferLoad r(bytes);
			try
			{
				t.xfer(r, *saved.logic);
				return std::string("(loaded)");
			}
			catch (const XferException &e)
			{
				return std::string(e.what());
			}
		};
		CHECK(loadInto(network(1, { 999 })) == "a tunnel network's rider: object id 999 is not an object of the snapshot");
		CHECK(loadInto(network(2, { keep->getID(), keep->getID() })) == "the snapshot's tunnel network lists rider 1 twice");
		REQUIRE(t.riders().size() == 1);
		CHECK(t.riders().front() == soldier);
		CHECK(t.tunnelCount() == 0);
	}
	SUBCASE("a contain's rider list (SnapshotXfer::objectPointers): a dangling id, a repeated id, an id of no object and an oversized count")
	{
		Object *keep = saved.logic->findObjectByID(1);
		std::list<Object *> riders{ keep };
		const auto list = [](std::uint32_t n, std::vector<ObjectID> ids) {
			XferSave w;
			w.xferUnsignedInt(&n);
			for (ObjectID &id : ids)
			{
				w.xferObjectID(&id);
			}
			return w.data();
		};
		const auto loadInto = [&](const std::vector<std::uint8_t> &bytes) {
			XferLoad r(bytes);
			try
			{
				SnapshotXfer::objectPointers(r, *saved.logic, riders, "a contain's rider", 1);
				return std::string("(loaded)");
			}
			catch (const XferException &e)
			{
				return std::string(e.what());
			}
		};
		CHECK(loadInto(list(1, { 999 })) == "a contain's rider: object id 999 is not an object of the snapshot");
		CHECK(loadInto(list(2, { 2, 2 })) == "a contain's rider: object id 2 is listed twice");
		CHECK(loadInto(list(1, { 0 })) == "a contain's rider: an entry names no object");
		CHECK(loadInto(list(1000000, { 2 })) == "a list declares 1000000 entries of at least 4 bytes, more than the 4 bytes left");
		REQUIRE(riders.size() == 1);
		CHECK(riders.front() == keep);
	}
	SUBCASE("a body's damage state outside its closed set (BODY_PRISTINE .. BODY_RUBBLE): refused, the body's state unchanged")
	{
		Object *soldier = saved.logic->findObjectByID(2);
		ActiveBody *body = static_cast<ActiveBody *>(soldier->getBodyModule());
		XferSave w;
		body->xfer(w);
		std::vector<std::uint8_t> bytes = w.data();
		// the version, the base chain's three versions, three reals, the indestructible flag, five reals, two frames / ids, the mask, the attacker flag
		const size_t stateAt = 1 + 3 + 12 + 1 + 20 + 12 + 1;
		REQUIRE(bytes[stateAt] == (std::uint8_t)body->appliedDamageState());
		bytes[stateAt] = 7;
		XferLoad r(bytes);
		std::string err;
		try
		{
			body->xfer(r);
		}
		catch (const XferException &e)
		{
			err = e.what();
		}
		CHECK(err == "a body's damage state 7 is not one of 0 .. 3");
		CHECK(body->appliedDamageState() == BODY_PRISTINE);
	}
	SUBCASE("a map listing a key twice (the damage over time records): refused, the records unchanged")
	{
		XferSave w;
		std::uint8_t one = 1;
		std::uint32_t two = 2;
		w.xferUnsignedByte(&one);
		w.xferUnsignedInt(&two);
		for (int k = 0; k < 2; ++k)
		{
			ObjectID id = 2;
			w.xferObjectID(&id);
			DamageInfo info;
			SnapshotXfer::damageInfo(w, info);
			std::uint32_t frames[3] = { 5, 50, 6 };
			for (std::uint32_t &f : frames)
			{
				w.xferUnsignedInt(&f);
			}
		}
		XferLoad r(w.data());
		std::string err;
		try
		{
			saved.logic->dot().xfer(r);
		}
		catch (const XferException &e)
		{
			err = e.what();
		}
		CHECK(err == "the damage over time records: the key 2 is listed twice");
		CHECK(saved.logic->dot().entries().empty());
	}
	SUBCASE("the encoded sizes the counts rely on: a damage record and an invisibility nugget encode to the minimum their containers declare")
	{
		XferSave d;
		DamageInfo info;
		SnapshotXfer::damageInfo(d, info);
		CHECK(d.data().size() == SnapshotXfer::kDamageInfoBytes);
		XferSave n;
		InvisibilityNugget nugget;
		nugget.xfer(n);
		CHECK(n.data().size() == InvisibilityNugget::kXferBytes);
	}
}

// ---- review r2 (Sol): staged links, closed sets, section 8 framing, retail-width counts, every large container's count -------------------------------------
namespace
{
// the link world: a horde with its payload (its re-creation in a load makes members of its own, which the load removes), a transport, a tunnel network
const std::string kLinkObjects = std::string(kObjects)
	+ "Object Fighter\n"
	  "  KindOf = INFANTRY\n"
	  "  Body = ActiveBody ModuleTag_Body\n"
	  "    MaxHealth = 100\n"
	  "  End\n"
	  "End\n"
	  "Object FighterHorde\n"
	  "  Behavior = HordeContain ModuleTag_Horde\n"
	  "    RankInfo = RankNumber:1 UnitType:Fighter Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	  "    InitialPayload = Fighter 3\n"
	  "  End\n"
	  "End\n"
	  "Object Wagon\n"
	  "  Body = ActiveBody ModuleTag_Body\n"
	  "    MaxHealth = 100\n"
	  "  End\n"
	  "  Behavior = TransportContain ModuleTag_Contain\n"
	  "  End\n"
	  "End\n"
	  "Object Tunnel\n"
	  "  KindOf = STRUCTURE\n"
	  "  Body = ActiveBody ModuleTag_Body\n"
	  "    MaxHealth = 100\n"
	  "  End\n"
	  "  Behavior = TunnelContain ModuleTag_Tunnel\n"
	  "  End\n"
	  "End\n";

struct LinkGame
{
	Object *horde = nullptr, *wagon = nullptr, *wagon2 = nullptr, *rider = nullptr, *rider2 = nullptr, *rider3 = nullptr, *tunnel = nullptr, *tunneller = nullptr, *keep = nullptr;
	std::vector<Object *> members;
};

LinkGame playLinks(S4World &f)
{
	LinkGame g;
	g.keep = f.make("Keep", f.teamOf("Alice"));
	g.horde = f.make("FighterHorde", f.teamOf("Alice"));
	const ContainModuleInterface::ContainedItemsList *m = g.horde->getContain()->getContainedItemsList();
	g.members.assign(m->begin(), m->end());
	REQUIRE(g.members.size() == 3);
	g.wagon = f.make("Wagon", f.teamOf("Alice"));
	g.wagon2 = f.make("Wagon", f.teamOf("Alice"));
	g.rider = f.make("Soldier", f.teamOf("Alice"));
	g.rider2 = f.make("Soldier", f.teamOf("Alice"));
	REQUIRE(g.wagon->getContain()->addToContain(g.rider));
	REQUIRE(g.wagon2->getContain()->addToContain(g.rider2));
	g.rider3 = f.make("Soldier", f.teamOf("Alice"));
	REQUIRE(g.wagon2->getContain()->addToContain(g.rider3));
	g.tunnel = f.make("Tunnel", f.teamOf("Alice"));
	TunnelContain *tc = dynamic_cast<TunnelContain *>(g.tunnel->findModule("TunnelContain"));
	REQUIRE(tc);
	tc->onBuildComplete(); // the tunnel joins its player's network (RW 0x8810C9)
	REQUIRE(tc->isRegistered());
	g.tunneller = f.make("Soldier", f.teamOf("Alice"));
	REQUIRE(g.tunnel->getContain()->addToContain(g.tunneller));
	REQUIRE(g.tunneller->getContainedBy() == g.tunnel);
	return g;
}

std::vector<std::uint8_t> saveLinks(S4World &f)
{
	f.activateUpgrades();
	std::vector<std::uint8_t> stream;
	f.logic->gameState().saveBlocks(coreAndSave4b(), stream);
	return stream;
}

std::vector<std::string> linkTokens()
{
	return coreAndSave4b();
}

// the bytes `from` replaced by `to` at their first occurrence in `bytes`, which holds them exactly `occurrences` times
void patchOnce(std::vector<std::uint8_t> &bytes, const std::vector<std::uint32_t> &from, const std::vector<std::uint32_t> &to, size_t occurrences = 1)
{
	std::vector<std::uint8_t> a, b;
	for (std::uint32_t v : from)
	{
		for (int k = 0; k < 4; ++k)
		{
			a.push_back((std::uint8_t)(v >> (8 * k)));
		}
	}
	for (std::uint32_t v : to)
	{
		for (int k = 0; k < 4; ++k)
		{
			b.push_back((std::uint8_t)(v >> (8 * k)));
		}
	}
	REQUIRE(a.size() == b.size());
	size_t found = bytes.size(), count = 0;
	for (size_t i = 0; i + a.size() <= bytes.size(); ++i)
	{
		if (std::equal(a.begin(), a.end(), bytes.begin() + (std::ptrdiff_t)i))
		{
			found = count == 0 ? i : found;
			++count;
		}
	}
	REQUIRE(count == occurrences);
	std::copy(b.begin(), b.end(), bytes.begin() + (std::ptrdiff_t)found);
}

// the riders the (re-created) container of id `container` lists in the loaded game
std::set<ObjectID> listedBy(GameLogic &logic, ObjectID container)
{
	std::set<ObjectID> out;
	Object *c = logic.findObjectByID(container);
	if (c && c->getContain() && c->getContain()->getContainedItemsList())
	{
		for (const Object *r : *c->getContain()->getContainedItemsList())
		{
			out.insert(r->getID());
		}
	}
	return out;
}

int playerNumber(S4World &f, const char *name)
{
	for (int i = 0; i < f.players.getPlayerCount(); ++i)
	{
		if (f.players.getNthPlayer(i) == f.players.findPlayerWithName(name))
		{
			return i;
		}
	}
	return -1;
}
} // namespace

TEST_CASE("save4b links: a horde's members, a transport's riders and a tunnel network load into another game of the same data, linked from both sides as "
		  "saved, and save the same bytes")
{
	S4World saved(kLinkObjects);
	const LinkGame g = playLinks(saved);
	runFrames(*saved.logic, 2);
	const std::vector<ObjectRecord> before = objectRecords(*saved.logic);
	const std::vector<std::uint8_t> stream = saveLinks(saved);
	S4World loaded(kLinkObjects);
	loaded.make("Wagon", loaded.teamOf("Bob"));
	loaded.activateUpgrades();
	loaded.logic->gameState().loadBlocks(linkTokens(), stream);
	CHECK(objectRecords(*loaded.logic) == before);
	GameLogic &l = *loaded.logic;
	std::set<ObjectID> members;
	for (const Object *m : g.members)
	{
		members.insert(m->getID());
		CHECK(l.findObjectByID(m->getID())->getContainedBy() == l.findObjectByID(g.horde->getID()));
	}
	CHECK(listedBy(l, g.horde->getID()) == members);
	CHECK(listedBy(l, g.wagon->getID()) == std::set<ObjectID>{ g.rider->getID() });
	CHECK(l.findObjectByID(g.rider->getID())->getContainedBy() == l.findObjectByID(g.wagon->getID()));
	const TunnelTracker *t = loaded.players.findPlayerWithName("Alice")->tunnelTrackerIfAny();
	REQUIRE(t);
	CHECK(t->tunnelIds() == std::list<ObjectID>{ g.tunnel->getID() });
	REQUIRE(t->riders().size() == 1);
	CHECK(t->riders().front() == l.findObjectByID(g.tunneller->getID()));
	CHECK(l.findObjectByID(g.tunneller->getID())->getContainedBy() == l.findObjectByID(g.tunnel->getID()));
	std::vector<std::uint8_t> again;
	loaded.logic->gameState().saveBlocks(linkTokens(), again);
	CHECK(again == stream);
}

TEST_CASE("save4b links refused (review r2): a contradictory link, a rider in two contains, a creation-only object and every tunnel network contradiction "
		  "are refused by name before any link is installed")
{
	S4World saved(kLinkObjects);
	LinkGame g = playLinks(saved);
	const int alice = playerNumber(saved, "Alice");
	std::vector<std::uint8_t> stream;
	S4World other(kLinkObjects);
	const auto load = [&]() { return loadError(other, linkTokens(), stream); };
	const auto noSavedMembersListed = [&]() {
		std::set<ObjectID> listed = listedBy(*other.logic, g.horde->getID());
		for (const Object *m : g.members)
		{
			CHECK(listed.count(m->getID()) == 0);
		}
	};
	SUBCASE("a contain lists a rider whose own container is none")
	{
		g.members[0]->friend_setContainedBy(nullptr);
		stream = saveLinks(saved);
		CHECK(load() == "block 'obfme:Objects': the snapshot's object " + std::to_string(g.horde->getID()) + " lists rider " + std::to_string(g.members[0]->getID())
			+ ", whose container is none");
		noSavedMembersListed();
		g.members[0]->friend_setContainedBy(g.horde);
	}
	SUBCASE("an object names a container that does not list it")
	{
		g.keep->friend_setContainedBy(g.wagon);
		stream = saveLinks(saved);
		CHECK(load() == "block 'obfme:Objects': the snapshot's object " + std::to_string(g.keep->getID()) + " names container " + std::to_string(g.wagon->getID())
			+ ", which does not list it");
		CHECK(other.logic->findObjectByID(g.keep->getID())->getContainedBy() == nullptr);
		CHECK(listedBy(*other.logic, g.wagon->getID()).count(g.rider->getID()) == 0);
		g.keep->friend_setContainedBy(nullptr);
	}
	SUBCASE("a rider listed by two contains")
	{
		stream = saveLinks(saved);
		patchOnce(stream, { 2, g.rider2->getID(), g.rider3->getID() }, { 2, g.rider->getID(), g.rider3->getID() }); // the second wagon's list now names the first wagon's rider
		CHECK(load() == "block 'obfme:Objects': the snapshot's object " + std::to_string(g.rider->getID()) + " is listed as a rider by objects "
			+ std::to_string(g.wagon->getID()) + " and " + std::to_string(g.wagon2->getID()));
		CHECK(listedBy(*other.logic, g.wagon->getID()).count(g.rider->getID()) == 0);
	}
	SUBCASE("a member that only the load's creations made (an object the load removes afterwards)")
	{
		stream = saveLinks(saved);
		const ObjectID stray = saved.logic->getNextObjectID(); // the first id the load's creations hand out (the horde's own payload)
		patchOnce(stream, { 3, g.members[0]->getID(), g.members[1]->getID(), g.members[2]->getID() },
			{ 3, stray, g.members[1]->getID(), g.members[2]->getID() }, 2); // the horde's member list, before its core's registered set (the same ids)
		CHECK(load() == "block 'obfme:Objects': a horde's member: object id " + std::to_string(stray) + " is not an object of the snapshot");
		noSavedMembersListed();
	}
	SUBCASE("a tunnel network lists a rider whose container is not one of its tunnels")
	{
		TunnelTracker &t = saved.players.findPlayerWithName("Alice")->tunnelTracker();
		t.addToContainList(g.rider2);
		stream = saveLinks(saved);
		CHECK(load() == "block 'obfme:TunnelTracker': player " + std::to_string(alice) + "'s tunnel network lists rider " + std::to_string(g.rider2->getID())
			+ ", whose container " + std::to_string(g.wagon2->getID()) + " is not one of its tunnels");
		const TunnelTracker *ot = other.players.findPlayerWithName("Alice")->tunnelTrackerIfAny();
		CHECK((ot == nullptr || std::count(ot->riders().begin(), ot->riders().end(), other.logic->findObjectByID(g.rider2->getID())) == 0));
		t.removeFromContain(g.rider2);
	}
	SUBCASE("an object inside a tunnel its network does not list")
	{
		TunnelTracker &t = saved.players.findPlayerWithName("Alice")->tunnelTracker();
		t.removeFromContain(g.tunneller);
		stream = saveLinks(saved);
		CHECK(load() == "block 'obfme:TunnelTracker': object " + std::to_string(g.tunneller->getID()) + " is inside tunnel " + std::to_string(g.tunnel->getID())
			+ ", whose network does not list it");
	}
	SUBCASE("a tunnel network lists an object that is not a tunnel")
	{
		saved.players.findPlayerWithName("Alice")->tunnelTracker().onTunnelCreated(*g.keep);
		stream = saveLinks(saved);
		CHECK(load() == "block 'obfme:TunnelTracker': player " + std::to_string(alice) + "'s tunnel network lists tunnel " + std::to_string(g.keep->getID())
			+ ", which is not a tunnel of the snapshot");
	}
}

TEST_CASE("save4b links refused: a load refused at its links leaves every contain listing live objects only (the creation-only horde members a load "
		  "removes are not freed while listed; ASan pins the use-after-free), and the game then loads the good snapshot as saved")
{
	S4World saved(kLinkObjects);
	LinkGame g = playLinks(saved);
	runFrames(*saved.logic, 2);
	const std::vector<std::uint8_t> good = saveLinks(saved);
	const std::vector<ObjectRecord> before = objectRecords(*saved.logic);
	g.members[0]->friend_setContainedBy(nullptr);
	const std::vector<std::uint8_t> bad = saveLinks(saved);
	g.members[0]->friend_setContainedBy(g.horde);
	S4World other(kLinkObjects);
	CHECK(loadError(other, linkTokens(), bad) == "block 'obfme:Objects': the snapshot's object " + std::to_string(g.horde->getID()) + " lists rider "
		+ std::to_string(g.members[0]->getID()) + ", whose container is none");
	size_t contains = 0;
	for (Object *o = other.logic->getFirstObject(); o; o = o->getNextObject())
	{
		const ContainModuleInterface *c = o->getContain();
		if (!c || !c->getContainedItemsList())
		{
			continue;
		}
		++contains;
		for (Object *r : *c->getContainedItemsList())
		{
			const ObjectID id = r->getID();
			CHECK(other.logic->findObjectByID(id) == r);
			CHECK(r->getContainedBy() == o);
		}
	}
	CHECK(contains == 4); // the horde, the two wagons and the tunnel
	CHECK(loadError(other, linkTokens(), good) == "(loaded)");
	CHECK(objectRecords(*other.logic) == before);
	CHECK(saveLinks(other) == good);
}

TEST_CASE("save4b closed sets (review r2): a damage record's damage and death types and a horde's free slot outside their sets are refused by name, the "
		  "records and the slots unchanged")
{
	S4World saved(kLinkObjects);
	const LinkGame g = playLinks(saved);
	const auto dotRecord = [](std::int32_t damageType, std::int32_t deathType) {
		XferSave w;
		std::uint8_t one = 1;
		std::uint32_t count = 1;
		ObjectID id = 2;
		w.xferUnsignedByte(&one);
		w.xferUnsignedInt(&count);
		w.xferObjectID(&id);
		DamageInfo info;
		SnapshotXfer::damageInfo(w, info); // a valid record, then its two types patched (a save refuses a value outside its set too)
		std::uint32_t frames[3] = { 5, 50, 6 };
		for (std::uint32_t &f : frames)
		{
			w.xferUnsignedInt(&f);
		}
		std::vector<std::uint8_t> bytes = w.data();
		const size_t record = 1 + 4 + 4;                          // the block's version, the count, the victim id
		std::memcpy(&bytes[record + 1 + 8], &damageType, 4);      // the record's version, the source and its player mask, then the damage type
		std::memcpy(&bytes[record + 1 + 8 + 12], &deathType, 4);  // after the FX override and the sub type: the death type
		return bytes;
	};
	const auto loadDot = [&](const std::vector<std::uint8_t> &bytes) {
		XferLoad r(bytes);
		try
		{
			saved.logic->dot().xfer(r);
			return std::string("(loaded)");
		}
		catch (const XferException &e)
		{
			return std::string(e.what());
		}
	};
	CHECK(loadDot(dotRecord(2147483647, DEATH_NORMAL)) == "a damage record's damage type 2147483647 is not one of 0 .. 27");
	CHECK(loadDot(dotRecord(DAMAGE_SLASH, -99)) == "a damage record's death type -99 is not one of 0 .. 23");
	CHECK(saved.logic->dot().entries().empty());
	// a horde core with no slots and a free slot 2147483647 (the free list indexes the slots unchecked)
	HordeContainCore &core = const_cast<HordeContainCore &>(dynamic_cast<HordeContain *>(g.horde->findModule("HordeContain"))->core());
	const std::vector<HordeContainCore::Slot> slots = core.slots();
	const std::list<int> freeList = core.freeList();
	XferSave w;
	std::uint8_t one = 1;
	std::uint32_t zero = 0, oneEntry = 1;
	std::int32_t huge = 2147483647;
	w.xferUnsignedByte(&one);
	w.xferUnsignedInt(&zero);     // no slots
	w.xferUnsignedInt(&zero);     // no member slots
	w.xferUnsignedInt(&oneEntry); // one free slot
	w.xferInt(&huge);
	XferLoad r(w.data());
	std::string err;
	try
	{
		core.xfer(r);
	}
	catch (const XferException &e)
	{
		err = e.what();
	}
	CHECK(err == "a horde's free slot 2147483647 is not one of 0 .. -1");
	CHECK(core.slots().size() == slots.size());
	CHECK(core.freeList() == freeList);
}

TEST_CASE("save4b section 8 (review r2): every class frames its own version (ImmortalBody), a container link never reads its destination, retail-width "
		  "counts are checked and charged, and every large container's count is refused when its block cannot hold it")
{
	S4World saved(std::string(kLinkObjects) + "Object Immortal\n  Body = ImmortalBody ModuleTag_Body\n    MaxHealth = 100\n  End\nEnd\n");
	playObjects(saved);
	SUBCASE("ImmortalBody: its own version byte, then ActiveBody's")
	{
		Object *immortal = saved.make("Immortal", saved.teamOf("Alice"));
		Object *soldier = saved.logic->findObjectByID(2);
		XferSave a, b;
		static_cast<ActiveBody *>(soldier->getBodyModule())->xfer(a);
		ActiveBody *ib = static_cast<ActiveBody *>(immortal->getBodyModule());
		ib->xfer(b);
		CHECK(b.data().size() == a.data().size() + 1);
		std::vector<std::uint8_t> bytes = b.data();
		bytes[0] = 2;
		XferLoad r(bytes);
		std::string err;
		try
		{
			ib->xfer(r);
		}
		catch (const XferException &e)
		{
			err = e.what();
		}
		CHECK(err == "Don't know how to load version 2 (most recent is 1)");
	}
	SUBCASE("a container link's load never reads the destination it replaces")
	{
		XferSave w;
		ObjectID id = 1;
		w.xferObjectID(&id);
		XferLoad r(w.data());
		Object *dest = reinterpret_cast<Object *>(std::uintptr_t(16)); // not an object: reading it would fault
		SnapshotXfer::containerPointer(r, *saved.logic, dest, 2);
		CHECK(dest == saved.logic->findObjectByID(1));
	}
	SUBCASE("TunnelTracker's u16 tunnel count and i32 rider count are checked and charged like any count")
	{
		const auto network = [](std::uint16_t tunnels, std::int32_t riders) {
			XferSave w;
			std::uint8_t one = 1;
			w.xferUnsignedByte(&one);
			w.xferUnsignedByte(&one);
			w.xferUnsignedShort(&tunnels);
			if (tunnels == 0)
			{
				w.xferInt(&riders);
			}
			return w.data();
		};
		TunnelTracker t;
		const auto loadInto = [&](const std::vector<std::uint8_t> &bytes) {
			XferLoad r(bytes);
			try
			{
				t.xfer(r, *saved.logic);
				return std::string("(loaded)");
			}
			catch (const XferException &e)
			{
				return std::string(e.what());
			}
		};
		CHECK(loadInto(network(65535, 0)) == "a list declares 65535 entries of at least 4 bytes, more than the 0 bytes left");
		CHECK(loadInto(network(0, 2147483647)) == "a list declares 2147483647 entries of at least 4 bytes, more than the 0 bytes left");
		CHECK(t.tunnelIds().empty());
		CHECK(t.riders().empty());
	}
	SUBCASE("every large container's count, refused by its block before anything is allocated")
	{
		const std::uint32_t huge = 1000000;
		// the bytes before each container's count (zeros of the right layout), then the count
		const auto refusal = [&](const std::function<void(XferSave &)> &prefix, const std::function<void(Xfer &)> &load) {
			XferSave w;
			prefix(w);
			std::uint32_t n = huge;
			w.xferUnsignedInt(&n);
			XferLoad r(w.data());
			try
			{
				load(r);
				return std::string("(loaded)");
			}
			catch (const XferException &e)
			{
				return std::string(e.what());
			}
		};
		const auto version = [](XferSave &w) {
			std::uint8_t one = 1;
			w.xferUnsignedByte(&one);
		};
		const std::string declares = "a list declares 1000000 entries of at least ";
		CHECK(refusal(version, [&](Xfer &r) { saved.logic->dot().xfer(r); }) == declares + "121 bytes, more than the 0 bytes left");
		CHECK(refusal(version, [&](Xfer &r) { saved.logic->sellList().xfer(r); }) == declares + "8 bytes, more than the 0 bytes left");
		CHECK(refusal(version, [&](Xfer &r) { saved.logic->experience().xfer(r); }) == declares + "6 bytes, more than the 0 bytes left");
		CHECK(refusal(version, [&](Xfer &r) { saved.logic->xferScheduler(r); }) == declares + "12 bytes, more than the 0 bytes left");
		CHECK(refusal(
				  [&](XferSave &w) {
					  version(w);
					  std::uint32_t u = 0;
					  float f = 1.0f;
					  w.xferUnsignedInt(&u);
					  w.xferReal(&f);
				  },
				  [&](Xfer &r) { saved.logic->invisibility().xfer(r); })
			== declares + "20 bytes, more than the 0 bytes left");
		CHECK(refusal(
				  [&](XferSave &w) {
					  version(w);
					  std::uint32_t u = 0, one = 1;
					  float f = 1.0f;
					  ObjectID id = 2;
					  w.xferUnsignedInt(&u);
					  w.xferReal(&f);
					  w.xferUnsignedInt(&one); // one entry
					  w.xferObjectID(&id);
					  for (int k = 0; k < 3; ++k)
					  {
						  w.xferUnsignedInt(&u);
					  }
				  },
				  [&](Xfer &r) { saved.logic->invisibility().xfer(r); })
			== declares + "216 bytes, more than the 0 bytes left");
		CHECK(refusal(
				  [&](XferSave &w) {
					  version(w);
					  bool on = true;
					  w.xferBool(&on);
					  for (int k = 0; k < 39; ++k)
					  {
						  std::uint64_t z = 0;
						  w.xferUnsignedInt64(&z);
					  }
				  },
				  [&](Xfer &r) { saved.logic->combat().xfer(r); })
			== declares + "12 bytes, more than the 0 bytes left");
		HordeContain *horde = dynamic_cast<HordeContain *>(saved.make("FighterHorde")->findModule("HordeContain"));
		REQUIRE(horde);
		HordeContainCore &core = const_cast<HordeContainCore &>(horde->core());
		CHECK(refusal(version, [&](Xfer &r) { core.xfer(r); }) == declares + "28 bytes, more than the 0 bytes left");
		CHECK(saved.logic->dot().entries().empty());
	}
}


TEST_CASE("save4b native load preparation validates framing first and clears staged hero dependencies on every exit")
{
	GameState state;
	std::vector<std::string> events;
	bool prepared = false;
	int failure = 0;
	SUBCASE("normal completion") {}
	SUBCASE("preparation throws") { failure = 1; }
	SUBCASE("ordinary xfer throws") { failure = 2; }
	SUBCASE("postprocess throws") { failure = 3; }
	SUBCASE("bad framing never prepares or loads") { failure = 4; }
	struct Block : Snapshot
	{
		GameState &state;
		std::vector<std::string> &events;
		bool &prepared;
		int &failure;
		std::int32_t value = 42;
		Block(GameState &s, std::vector<std::string> &e, bool &p, int &f) : state(s), events(e), prepared(p), failure(f) {}
		void crc(Xfer *x) override { xfer(x); }
		void xfer(Xfer *x) override
		{
			x->xferInt(&value);
			if (!x->isLoading()) return;
			CHECK(prepared);
			events.push_back("load");
			if (failure == 2) throw XferException("test load failure");
			state.addPostProcessSnapshot(this);
		}
		void loadPostProcess() override
		{
			CHECK(prepared);
			events.push_back("post");
			if (failure == 3) throw XferException("test postprocess failure");
		}
	} objects(state, events, prepared, failure), heroes(state, events, prepared, failure);
	state.addSnapshotBlock("objects", &objects, 1, "test", {});
	state.addSnapshotBlock("heroes", &heroes, 1, "test", {});
	state.setLoadPreparation("heroes", [&](Xfer &x) {
		std::int32_t staged = 0;
		x.xferInt(&staged);
		CHECK(staged == 42);
		prepared = true;
		events.push_back("prepare");
		if (failure == 1) throw XferException("test preparation failure");
	}, [&]() {
		prepared = false;
		events.push_back("cleanup");
	});
	const std::vector<std::string> tokens{ "objects", "heroes" };
	std::vector<std::uint8_t> bytes;
	state.saveBlocks(tokens, bytes);
	if (failure == 4) bytes.pop_back();
	if (failure) CHECK_THROWS_AS(state.loadBlocks(tokens, bytes), XferException);
	else state.loadBlocks(tokens, bytes);
	CHECK_FALSE(prepared);
	if (failure == 4) CHECK(events.empty());
	else
	{
		REQUIRE_FALSE(events.empty());
		CHECK(events.front() == "prepare");
		CHECK(events.back() == "cleanup");
		CHECK(std::count(events.begin(), events.end(), "cleanup") == 1);
		if (!failure) CHECK(events == std::vector<std::string>{ "prepare", "load", "load", "post", "post", "cleanup" });
	}
}

TEST_CASE("save4b create-a-hero: transferred heroes load from saved object bindings instead of their new owners")
{
	// Both editions, native saves. Resetting the slot records after saving models
	// a fresh START where the saved object-to-record bindings do not yet exist.
	ExperienceLevelSystem levels, rebuiltLevels;
	CreateAHeroSystem cah;
	CommandStore commands;
	struct RestoreGlobals
	{
		ExperienceLevelSystem *levels = TheExperienceLevelSystem;
		CreateAHeroSystem *cah = TheCreateAHeroSystem;
		CommandStore *commands = TheCommandStore;
		~RestoreGlobals() { TheExperienceLevelSystem = levels; TheCreateAHeroSystem = cah; TheCommandStore = commands; }
	} restore;
	TheExperienceLevelSystem = &levels;
	TheCreateAHeroSystem = &cah;
	TheCommandStore = &commands;
	struct ColorHooks : ObjectClientHooks
	{
		std::map<std::uint32_t, std::vector<std::uint32_t>> colors;
		void objectCreated(Object &) override {}
		void objectDestroyed(Object &) override {}
		void setCustomColors(Object &obj, int kind, std::uint32_t a, std::uint32_t b, std::uint32_t c) override
		{
			colors[obj.getID()] = {std::uint32_t(kind), a, b, c};
		}
	} hooks; // outlives the world even if a failed assertion exits this test
	logictest::LogicWorld world;
	levels.registerBlocks(world.w.fx.env.blocks);
	cah.canBuildUpgradeName = "Upgrade_TransferBuild";
	cah.mapModeUpgradeName = "Upgrade_TransferMap";
	cah.gameModeUpgradeName = "Upgrade_TransferGame";
	const std::string levelIni = R"ini(
ExperienceLevel CreateAHeroLevel1
 TargetNames = CreateAHero
 RequiredExperience = 1
 Rank = 1
End
ExperienceLevel CreateAHeroLevel2
 TargetNames = CreateAHero
 RequiredExperience = 100
 Rank = 2
End
)ini";
	REQUIRE(world.w.load(levelIni + R"ini(
Upgrade Upgrade_TransferBuild
 Type = PLAYER
End
Upgrade Upgrade_TransferMap
 Type = OBJECT
End
Upgrade Upgrade_TransferGame
 Type = OBJECT
End
Object CreateAHero
 KindOf = CREATE_A_HERO HERO
End
)ini").empty());
	CreateAHeroClass cls;
	cls.upgradeName = "Upgrade_TransferGame";
	CreateAHeroSubClass sub;
	sub.upgradeName = cls.upgradeName;
	cls.subClasses.push_back(sub);
	cah.addClass(cls, nullptr);
	bool recipientHasHero = false;
	bool foreignTarget = false;
	bool duplicateBinding = false;
	SUBCASE("recipient has no hero record") {}
	SUBCASE("recipient has a different hero and live object") { recipientHasHero = true; }
	SUBCASE("another saved hero target is still refused") { recipientHasHero = true; foreignTarget = true; }
	SUBCASE("duplicate nonzero saved object bindings are refused") { recipientHasHero = true; duplicateBinding = true; }
	Player *alice = world.players.findPlayerWithName("Alice");
	Player *bob = world.players.findPlayerWithName("Bob");
	REQUIRE(alice);
	REQUIRE(bob);
	CreateAHeroHero original, recipient;
	original.uniqueID = "TransferredHero";
	original.primaryColor = 0xff123456u;
	original.secondaryColor = 0xff56789au;
	original.tertiaryColor = 0xff9abcdeu;
	recipient.primaryColor = 0xffabcdefu;
	recipient.secondaryColor = 0xff345678u;
	recipient.tertiaryColor = 0xff789abcu;
	recipient.uniqueID = "RecipientHero";
	world.logic->createAHeroes().assign(*alice, original);
	if (recipientHasHero) world.logic->createAHeroes().assign(*bob, recipient);
	world.logic->createAHeroes().startGame();
	Object *hero = world.make("CreateAHero", alice->getDefaultTeam());
	const ObjectID id = hero->getID();
	hero->getExperienceTracker()->gainLevel(false);
	REQUIRE(hero->getExperienceTracker()->getRank() == 2);
	ObjectID recipientID = INVALID_ID;
	if (recipientHasHero) recipientID = world.make("CreateAHero", bob->getDefaultTeam())->getID();
	hero->setTeam(bob->getDefaultTeam());
	REQUIRE(hero->getControllingPlayer() == bob);
	REQUIRE(world.logic->createAHeroes().heroOfObject(id)->uniqueID == original.uniqueID);
	if (foreignTarget) hero->getExperienceTracker()->setLevelTargetName("CreateAHero_RecipientHero");
	if (duplicateBinding) world.logic->createAHeroes().heroOf(*bob)->objectID = id;
	std::vector<std::string> blocks = coreAndSave4b();
	blocks.push_back("obfme:CreateAHero");
	std::vector<std::uint8_t> bytes;
	world.logic->gameState().saveBlocks(blocks, bytes);
	const auto before = sectionValues(*world.logic);
	world.logic->createAHeroes().assign(*alice, original);
	if (recipientHasHero) world.logic->createAHeroes().assign(*bob, recipient);
	REQUIRE_FALSE(world.logic->createAHeroes().heroOfObject(id));
	TheExperienceLevelSystem = &rebuiltLevels;
	REQUIRE(world.w.load(levelIni).empty());
	REQUIRE_FALSE(rebuiltLevels.levelsFor("CreateAHero_TransferredHero"));
	world.logic->setClientHooks(&hooks);
	const std::string result = loadError(world, blocks, bytes);
	world.logic->setClientHooks(&world.hooks);
	if (foreignTarget || duplicateBinding)
	{
		if (duplicateBinding) CHECK(result.find("object " + std::to_string(id) + " is bound to more than one saved hero") != std::string::npos);
		else CHECK(result.find("level target 'CreateAHero_RecipientHero' is not a template") != std::string::npos);
		// Failure also releases the temporary binding cache: an ordinary creation
		// after the failed load must use its player's record again.
		Object *next = world.make("CreateAHero", alice->getDefaultTeam());
		CHECK(next->getExperienceTracker()->levelTargetName() == "CreateAHero_TransferredHero");
		return;
	}
	REQUIRE(result == "(loaded)");
	hero = world.logic->findObjectByID(id);
	REQUIRE(hero);
	CHECK(hero->getControllingPlayer() == bob);
	REQUIRE(hooks.colors.count(id) == 1);
	CHECK(hooks.colors.at(id) == std::vector<std::uint32_t>{3u, original.primaryColor, original.secondaryColor, original.tertiaryColor});
	CHECK(hero->getExperienceTracker()->levelTargetName() == "CreateAHero_TransferredHero");
	CHECK(hero->getExperienceTracker()->getLevelName() == "CreateAHeroLevel2_TransferredHero");
	CHECK(hero->getExperienceTracker()->getRank() == 2);
	REQUIRE(world.logic->createAHeroes().heroOfObject(id));
	CHECK((world.logic->createAHeroes().heroOfObject(id)->flags & 8u) == 0u); // the color dirty bit was already consumed before saving
	CHECK(world.logic->createAHeroes().heroOfObject(id)->uniqueID == original.uniqueID);
	if (recipientHasHero)
	{
		REQUIRE(world.logic->createAHeroes().heroOfObject(recipientID));
		CHECK(world.logic->createAHeroes().heroOfObject(recipientID)->uniqueID == recipient.uniqueID);
		REQUIRE(hooks.colors.count(recipientID) == 1);
		CHECK(hooks.colors.at(recipientID) == std::vector<std::uint32_t>{3u, recipient.primaryColor, recipient.secondaryColor, recipient.tertiaryColor});
	}
	const auto after = sectionValues(*world.logic);
	for (const char *section : { "objects", "create-a-hero", "experience", "players and teams", "frame, object ids, RNG" })
	{
		CAPTURE(section);
		CHECK(after.at(section) == before.at(section));
	}
	std::vector<std::uint8_t> again;
	world.logic->gameState().saveBlocks(blocks, again);
	CHECK(again == bytes);
	Object *next = world.make("CreateAHero", alice->getDefaultTeam());
	CHECK(next->getExperienceTracker()->levelTargetName() == "CreateAHero_TransferredHero"); // cleanup after success
}

TEST_CASE("save4b create-a-hero: reconstructed generated experience targets roundtrip and foreign identities are refused")
{
	// Both editions, native snapshot validation only. Actual creation rebuilds the synthetic chain (RW 0x80AA38), without retail assets.
	ExperienceLevelSystem levels, rebuiltLevels, emptyLevels;
	CreateAHeroSystem cah;
	CommandStore commands;
	struct RestoreGlobals
	{
		ExperienceLevelSystem *levels = TheExperienceLevelSystem;
		CreateAHeroSystem *cah = TheCreateAHeroSystem;
		CommandStore *commands = TheCommandStore;
		~RestoreGlobals()
		{
			TheExperienceLevelSystem = levels;
			TheCreateAHeroSystem = cah;
			TheCommandStore = commands;
		}
	} restore;
	TheExperienceLevelSystem = &levels;
	TheCreateAHeroSystem = &cah;
	TheCommandStore = &commands;
	logictest::LogicWorld world;
	cah.canBuildUpgradeName = "Upgrade_AllowBuildCreateAHero";
	cah.mapModeUpgradeName = "Upgrade_CreateAHeroMapMode";
	cah.gameModeUpgradeName = "Upgrade_CreateAHeroGameMode";
	levels.registerBlocks(world.w.fx.env.blocks);
	const std::string levelIni = R"ini(
ExperienceLevel CreateAHeroLevel1
 TargetNames = CreateAHero
 RequiredExperience = 1
 Rank = 1
End
ExperienceLevel CreateAHeroLevel2
 TargetNames = CreateAHero
 RequiredExperience = 100
 Rank = 2
End
ExperienceLevel OrdinaryLevel1
 TargetNames = Ordinary
 RequiredExperience = 1
 Rank = 1
End
)ini";
	REQUIRE(world.w.load(levelIni + R"ini(
Upgrade Upgrade_AllowBuildCreateAHero
 Type = PLAYER
End
Upgrade Upgrade_CreateAHeroMapMode
 Type = OBJECT
End
Upgrade Upgrade_CreateAHeroGameMode
 Type = OBJECT
End
Object CreateAHero
 KindOf = CREATE_A_HERO HERO
End
Object Ordinary
 KindOf = INFANTRY
End
)ini").empty());
	CreateAHeroHero record;
	record.uniqueID = "SnapshotHero";
	Player *owner = world.players.findPlayerWithName("Alice");
	REQUIRE(owner);
	world.logic->createAHeroes().assign(*owner, record);
	world.logic->createAHeroes().startGame();
	Object *hero = world.make("CreateAHero", owner->getDefaultTeam());
	REQUIRE(hero->getExperienceTracker());
	ExperienceTracker *tracker = hero->getExperienceTracker();
	const std::string target = "CreateAHero_SnapshotHero";
	REQUIRE(tracker->levelTargetName() == target);
	REQUIRE_FALSE(world.w.things.findTemplate(target));
	tracker->gainLevel(false);
	REQUIRE(tracker->getRank() == 2);
	REQUIRE(tracker->getLevelName() == "CreateAHeroLevel2_SnapshotHero");
	tracker->setExperienceScalar(1.25f);
	tracker->setLevelCap(7);
	world.make("Ordinary", owner->getDefaultTeam()); // ordinary template-backed trackers keep roundtripping too
	const ObjectID heroID = hero->getID();
	const auto records = objectRecords(*world.logic);
	std::vector<std::uint8_t> bytes;
	world.logic->gameState().saveBlocks(coreAndSave4b(), bytes);
	TheExperienceLevelSystem = &rebuiltLevels; // a fresh data store has no generated levels
	REQUIRE(world.w.load(levelIni).empty());
	REQUIRE_FALSE(rebuiltLevels.levelsFor(target));
	REQUIRE(loadError(world, coreAndSave4b(), bytes) == "(loaded)");
	CHECK(objectRecords(*world.logic) == records);
	std::vector<std::uint8_t> again;
	world.logic->gameState().saveBlocks(coreAndSave4b(), again);
	CHECK(again == bytes);
	hero = world.logic->findObjectByID(heroID);
	REQUIRE(hero);
	tracker = hero->getExperienceTracker();
	REQUIRE(tracker);
	CHECK(tracker->getExperienceScalar() == 1.25f);
	CHECK(tracker->getLevelCap() == 7);
	CHECK(tracker->getRank() == 2);
	const auto refuseIdentity = [&](const std::string &savedTarget, const std::string &savedLevel) {
		// Only the identity prefix is needed: the named validation must reject before reading scalar fields.
		tracker->setLevelTargetName(target);
		XferSave out;
		std::uint8_t version = 1;
		std::string t = savedTarget, l = savedLevel;
		out.xferUnsignedByte(&version);
		out.xferAsciiString(&t);
		out.xferAsciiString(&l);
		XferLoad in(out.data());
		try { tracker->xfer(in); }
		catch (const XferException &e) { return std::string(e.what()); }
		return std::string("(loaded)");
	};
	CHECK(refuseIdentity("CreateAHero_Unknown", "CreateAHeroLevel2_SnapshotHero") ==
		"an experience tracker's level target 'CreateAHero_Unknown' is not a template of this game's data");
	REQUIRE(rebuiltLevels.cloneLevelForTarget("CreateAHeroLevel1", "OtherHeroLevel1", "CreateAHero_Other", "", ""));
	CHECK(refuseIdentity("CreateAHero_Other", "OtherHeroLevel1") ==
		"an experience tracker's level target 'CreateAHero_Other' is not a template of this game's data");
	CHECK(refuseIdentity(target, "OtherHeroLevel1") ==
		"an experience tracker's level 'OtherHeroLevel1' is not in its generated target 'CreateAHero_SnapshotHero'");
	CHECK(refuseIdentity(target, "MissingLevel") ==
		"an experience tracker's level 'MissingLevel' is not an experience level of this game's data");
	TheExperienceLevelSystem = &emptyLevels;
	CHECK(refuseIdentity(target, "") ==
		"an experience tracker's level target 'CreateAHero_SnapshotHero' is not a template of this game's data");
}

TEST_CASE("save4b create-a-hero: saved power buttons rebuild in a fresh command store without changing snapshot state")
{
	// Both editions: native snapshot reconstruction, not a change to rank/power rules.
	ExperienceLevelSystem levels;
	CreateAHeroSystem cah;
	CommandStore commands, freshCommands;
	struct RestoreGlobals
	{
		ExperienceLevelSystem *levels = TheExperienceLevelSystem;
		CreateAHeroSystem *cah = TheCreateAHeroSystem;
		CommandStore *commands = TheCommandStore;
		~RestoreGlobals() { TheExperienceLevelSystem = levels; TheCreateAHeroSystem = cah; TheCommandStore = commands; }
	} restore;
	TheExperienceLevelSystem = &levels;
	TheCreateAHeroSystem = &cah;
	TheCommandStore = &commands;
	logictest::LogicWorld world;
	levels.registerBlocks(world.w.fx.env.blocks);
	world.w.fx.env.blocks.registerBlock("CommandButton", CommandStore::parseCommandButtonDefinitionGlobal);
	world.w.fx.env.blocks.registerBlock("CommandSet", CommandStore::parseCommandSetDefinitionGlobal);
	cah.canBuildUpgradeName = "Upgrade_AllowCAH";
	cah.mapModeUpgradeName = "Upgrade_MapCAH";
	cah.gameModeUpgradeName = "Upgrade_GameCAH";
	cah.commandSetTemplate = "CahTemplate";
	REQUIRE(world.w.load(R"ini(
Upgrade Upgrade_AllowCAH
 Type = PLAYER
End
Upgrade Upgrade_MapCAH
End
Upgrade Upgrade_GameCAH
End
Upgrade Upgrade_ClassCAH
End
ExperienceLevel CreateAHeroLevel1
 TargetNames = CreateAHero
 RequiredExperience = 1
 Rank = 1
End
ExperienceLevel CreateAHeroLevel2
 TargetNames = CreateAHero
 RequiredExperience = 100
 Rank = 2
End
Object CreateAHero
 KindOf = CREATE_A_HERO HERO
End
)ini").empty());
	CreateAHeroClass cls;
	cls.upgradeName = "Upgrade_ClassCAH";
	CreateAHeroSubClass sub;
	sub.upgradeName = "Upgrade_ClassCAH";
	cls.subClasses.push_back(sub);
	cah.addClass(cls, nullptr);
	const std::string commandIni = R"ini(
CommandButton Command_CahLow
End
CommandButton Command_CahHigh
End
CommandButton Command_CahOther
End
CommandButton Command_AttackMove
End
CommandSet CahTemplate
 2 = Command_CahOther
End
)ini";
	REQUIRE(world.w.load(commandIni).empty());
	CreateAHeroHero record;
	record.uniqueID = "PowerSnapshotHero";
	record.powers[0].commandButton = "Command_CahLow";
	record.powers[0].buttonIndex = 0;
	record.powers[1].commandButton = "Command_CahHigh";
	record.powers[1].buttonIndex = 0;
	record.powers[1].expLevel = 1;
	Player *owner = world.players.findPlayerWithName("Alice");
	REQUIRE(owner);
	world.logic->createAHeroes().assign(*owner, record);
	world.logic->createAHeroes().startGame();
	Object *hero = world.make("CreateAHero", owner->getDefaultTeam());
	hero->setName("NamedHero");
	hero->getExperienceTracker()->gainLevel(false);
	REQUIRE(hero->getExperienceTracker()->getRank() == 2);
	int commandRank = 2;
	bool staticOverride = false;
	bool renamedAfterPowers = false;
	bool matchingStaticName = false;
	bool staleDynamic = false;
	SUBCASE("saved rank's power buttons") {}
	SUBCASE("pending level grant retains the previous command rank") { commandRank = 1; }
	SUBCASE("static or script replacement stays authoritative") { staticOverride = true; }
	SUBCASE("renaming the object preserves the earlier generated command-set name") { renamedAfterPowers = true; }
	SUBCASE("static definition with the exact generated name stays authoritative") { staticOverride = true; matchingStaticName = true; }
	SUBCASE("existing generated definition is rebuilt from the saved powers") { staleDynamic = true; }
	world.logic->createAHeroes().buildCommandSet(*hero, commandRank);
	if (renamedAfterPowers) { hero->setName("RenamedAfterPowers"); }
	if (staticOverride && !matchingStaticName) { hero->setCommandSetOverride("CahTemplate"); }
	const ObjectID id = hero->getID();
	const std::string savedSet = hero->getCommandSetOverride();
	const std::string savedObjectName = hero->getName();
	const std::string staticDefinition = matchingStaticName ? "CommandSet " + savedSet + "\n 2 = Command_CahOther\nEnd\n" : std::string();
	const std::string expectedPower = commandRank == 1 ? "Command_CahLow" : "Command_CahHigh";
	REQUIRE(commands.findCommandSet(savedSet));
	if (staticOverride && !matchingStaticName) { REQUIRE_FALSE(commands.findCommandSet(savedSet)->m_dynamic); }
	else { REQUIRE(commands.findCommandSet(savedSet)->getCommandButton(0)->m_name == expectedPower); }
	std::vector<std::string> blocks = coreAndSave4b();
	blocks.push_back("obfme:CreateAHero");
	std::vector<std::uint8_t> bytes;
	world.logic->gameState().saveBlocks(blocks, bytes);
	const auto before = sectionValues(*world.logic);
	TheCommandStore = &freshCommands;
	// Only the override name is saved. A fresh store may already define that
	// exact generated-looking name statically; loading must preserve it.
	REQUIRE(world.w.load(commandIni + staticDefinition).empty());
	if (matchingStaticName) { REQUIRE_FALSE(freshCommands.findCommandSet(savedSet)->m_dynamic); }
	if (!staticOverride) { REQUIRE_FALSE(freshCommands.findCommandSet(savedSet)); }
	if (staleDynamic)
	{
		CommandSet *stale = freshCommands.newDynamicCommandSet(savedSet);
		stale->setDynamicButton(0, freshCommands.findCommandButton("Command_CahOther"));
		REQUIRE(stale->getCommandButton(0)->m_name != expectedPower);
	}
	REQUIRE(loadError(world, blocks, bytes) == "(loaded)");
	hero = world.logic->findObjectByID(id);
	REQUIRE(hero);
	CHECK(hero->getCommandSetOverride() == savedSet);
	CHECK(hero->getName() == savedObjectName);
	const auto after = sectionValues(*world.logic);
	// Only these blocks were saved by this synthetic fixture (unrelated map subsystems were not).
	for (const char *section : { "objects", "create-a-hero", "experience", "players and teams", "frame, object ids, RNG" })
	{
		CAPTURE(section);
		CHECK(after.at(section) == before.at(section));
	}
	std::vector<std::uint8_t> again;
	world.logic->gameState().saveBlocks(blocks, again);
	CHECK(again == bytes);
	const CommandSet *rebuilt = freshCommands.findCommandSet(savedSet);
	REQUIRE(rebuilt);
	CHECK(rebuilt->m_dynamic == !staticOverride);
	if (staticOverride) { CHECK_FALSE(rebuilt->getCommandButton(0)); }
	else
	{
		REQUIRE(rebuilt->getCommandButton(0));
		CHECK(rebuilt->getCommandButton(0)->m_name == expectedPower);
	}
	REQUIRE(rebuilt->getCommandButton(1));
	CHECK(rebuilt->getCommandButton(1)->m_name == "Command_CahOther");
	if (staticOverride) { CHECK_FALSE(rebuilt->getCommandButton(16)); }
	else
	{
		REQUIRE(rebuilt->getCommandButton(16));
		CHECK(rebuilt->getCommandButton(16)->m_name == "Command_AttackMove");
	}
	// The reconstruction itself is idempotent and cannot change any authoritative hash section.
	world.logic->createAHeroes().finishLoad();
	CHECK(sectionValues(*world.logic) == after);
}
