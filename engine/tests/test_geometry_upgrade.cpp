// OpenBFME unit tests. GPL-3.0.
// Lane MODCOMPAT-MOD1: GeometryUpgrade (RW 0x8BADC0 / 0x8BAC96) and the object's own GeometryInfo (Object + 0xA8) the simulation's readers now take
// (Object::geometryShapes / ownPathfindGeometry). Binary facts read from the RotWK binary (RW_GAME_DAT); behaviour on the retail data (ROTWK_INSTALL) in a
// started skirmish. The expectations are retail's rules (each check names its address), not this engine's frame-exact output.

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatLineOfSight.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/GeometryUpgrade.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
// the lobby's result: slot 0 a human (`faction0`), slot 1 a computer (`faction1`) on Evendim, start positions 0 and 4 (test_mod4_modules.cpp's game)
bool startGeoGame(starttest::Shared &s, const std::string &faction0, const std::string &faction1, std::uint32_t seed, buildtest::Game &out, std::string *error)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = buildtest::templateIndex(s, faction0);
	b.tmpl = buildtest::templateIndex(s, faction1);
	a.color = 0;
	b.color = 1;
	a.start = 0;
	b.start = 4;
	NewGameMessage message;
	message.game = startgolden::makeInfo({ a, b });
	message.game.slots[1].state = SLOT_EASY_AI;
	message.game.mapName = "maps/map mp evendim/map mp evendim.map";
	message.game.seed = seed;
	message.game.startingCash = 5000;
	if (!NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, error))
	{
		return false;
	}
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.live = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	return out.live->load(o, error);
}

const Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

Object *firstOf(GameLogic &logic, const Player *owner, const char *kindOfName)
{
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == owner && x->isKindOfName(kindOfName) && !x->isEffectivelyDead())
		{
			return x;
		}
	}
	return nullptr;
}

GeometryUpgrade *geometryUpgrade(Object &o, const std::string &tag)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		GeometryUpgrade *g = dynamic_cast<GeometryUpgrade *>(m.get());
		if (g && g->getModuleData() && g->getModuleData()->getModuleTagName() == tag)
		{
			return g;
		}
	}
	return nullptr;
}

// the active flags of the shapes named `name` in the object's own geometry (every one must agree)
int namedActive(const Object &o, const std::string &name)
{
	int on = 0, off = 0;
	for (const ObjectGeometry::Shape &s : o.geometryShapes())
	{
		if (s.name == name)
		{
			(s.active ? on : off) += 1;
		}
	}
	REQUIRE_MESSAGE(on + off > 0, name); // tolerance: any count of shapes carrying the name will do, none is the error
	return off == 0 ? 1 : (on == 0 ? 0 : -1);
}

std::string lastActiveName(const Object &o)
{
	std::string out;
	for (const ObjectGeometry::Shape &s : o.geometryShapes())
	{
		if (s.active)
		{
			out = s.name;
		}
	}
	return out;
}

} // namespace

// ---- binary facts ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("GeometryUpgrade: the binary facts: the field table RW 0xC6F578's parsers, the mux slots 8 / 10 and the xfer slot")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("GeometryUpgrade binary facts (set RW_GAME_DAT)");
		return;
	}
	// RW 0xC6F578: ShowGeometry / HideGeometry parseAsciiStringVectorAppend RW 0x42E59E, the three meshes parseAsciiString RW 0x42EE5E
	const std::uint32_t parsers[] = { 0x42E59Eu, 0x42E59Eu, 0x42EE5Eu, 0x42EE5Eu, 0x42EE5Eu };
	for (std::uint32_t i = 0; i < 5; ++i)
	{
		CHECK(pe->u32At(0xC6F578u + 16u * i + 4u) == parsers[i]);
	}
	// the constructor RW 0x8BABCB writes the mux vtable RW 0xC6F5D8 at + 0x10: slot 0 isAlreadyUpgraded, slot 8 the removal, slot 9 setUpgradeExecuted,
	// slot 10 the implementation
	CHECK(pe->u32At(0x8BABFBu + 3u) == 0xC6F5D8u); // mov [esi + 0x10], 0xC6F5D8
	CHECK(pe->u32At(0xC6F5D8u + 4u * 0u) == 0x4986C4u);
	CHECK(pe->u32At(0xC6F5D8u + 4u * 8u) == 0x8BAC96u);
	CHECK(pe->u32At(0xC6F5D8u + 4u * 9u) == 0x5B462Du);
	CHECK(pe->u32At(0xC6F5D8u + 4u * 10u) == 0x8BADC0u);
	// the module vtable RW 0xC6F620: slot 3 the xfer RW 0x8BAB72
	CHECK(pe->u32At(0x8BABEEu + 2u) == 0xC6F620u); // mov [esi], 0xC6F620
	CHECK(pe->u32At(0xC6F620u + 4u * 3u) == 0x8BAB72u);
}

// ---- the retail data ---------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("GeometryUpgrade: the Gondor barracks' level geometry: HideGeometry off, ShowGeometry on in the object's own GeometryInfo, read by combat, partition and pathfinder")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startGeoGame(*s, "FactionMen", "FactionMordor", 4403, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);

	// an object without a GeometryUpgrade (and no geometry change) answers its template's geometry itself (no copy: it hashes and reads as before)
	const Object *plain = nullptr;
	for (Object *x = logic.getFirstObject(); x && !plain; x = x->getNextObject())
	{
		bool has = false;
		for (const ThingTemplate::Nugget &n : static_cast<const ThingTemplate *>(x->getTemplate())->behaviorModules().nuggets())
		{
			has = has || n.name == "GeometryUpgrade";
		}
		if (!has && x->getControllingPlayer() == human && !ObjectGeometry::cachedShapesOf(*x->getTemplate()).empty())
		{
			plain = x;
		}
	}
	REQUIRE(plain);
	REQUIRE(plain->geometryActive().empty());
	CHECK(&plain->geometryShapes() == &ObjectGeometry::cachedShapesOf(*plain->getTemplate()));
	const PathfindGeometry &plainTemplate = logic.aiWorld()->movementInfo(*plain->getTemplate()).geometry;
	CHECK(&plain->ownPathfindGeometry(plainTemplate) == &plainTemplate);
	CHECK(&CombatQueries::geometry(*plain) == &plainTemplate);

	const Coord3D at{ fortress->getPosition()->x + 500.0f, fortress->getPosition()->y, fortress->getPosition()->z };
	Object *barracks = g.live->createObject("GondorBarracks", human->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(barracks, err);
	GeometryUpgrade *level1 = geometryUpgrade(*barracks, "Geom_ModuleTag_HideAll");
	GeometryUpgrade *level3 = geometryUpgrade(*barracks, "Geom_ModuleTag_ShowFlagsAndTower");
	REQUIRE(level1);
	REQUIRE(level3);
	// the data (RW 0xC6F578): Level1 shows Geom_Orig and hides Geom_V2, Level3 the other way
	REQUIRE(level1->data()->m_showGeometry == std::vector<std::string>{ "Geom_Orig" });
	REQUIRE(level1->data()->m_hideGeometry == std::vector<std::string>{ "Geom_V2" });
	REQUIRE(level3->data()->m_showGeometry == std::vector<std::string>{ "Geom_V2" });
	REQUIRE(level3->data()->m_hideGeometry == std::vector<std::string>{ "Geom_Orig" });
	CHECK(level1->data()->m_wallBoundsMesh.empty());

	if (!level1->isAlreadyUpgraded())
	{
		CHECK(barracks->geometryActive().empty()); // the template's: every shape active (no GeometryActive row)
		CHECK(namedActive(*barracks, "Geom_V2") == 1);
		CHECK(namedActive(*barracks, "Geom_Orig") == 1);
		barracks->giveUpgrade("Upgrade_StructureLevel1"); // OBJECT upgrade (upgrade.ini): the experience level's grant
	}
	REQUIRE(level1->isAlreadyUpgraded());
	g.live->advance(0.2);
	// RW 0x8BAF89 .. 0x8BB004 (the template's shapes all start active, so the last active shape was a Geom_V2 box)
	CHECK(level1->upgradeName() == "Geom_V2");
	CHECK(namedActive(*barracks, "Geom_V2") == 0);
	CHECK(namedActive(*barracks, "Geom_Orig") == 1);
	CHECK_FALSE(level3->isAlreadyUpgraded());
	// RW 0xAD1920 over the object's own shapes: the Geom_Orig tower (height 75) is the highest active shape now, not the Geom_V2 one (105)
	CHECK(CombatLineOfSight::maxHeightAbovePosition(*barracks) == 75.0f);
	CHECK(ObjectGeometry::maxHeightAbovePosition(ObjectGeometry::cachedShapesOf(*barracks->getTemplate())) == 105.0f);
	// combat (Object + 0xB8 / + 0xBC), the partition (the same values) and the pathfinder's adapter read the object's copy
	const PathfindGeometry &own = CombatQueries::geometry(*barracks);
	const PathfindGeometry &tmpl = logic.aiWorld()->movementInfo(*barracks->getTemplate()).geometry;
	CHECK(&own != &tmpl);
	REQUIRE(own.shapes.size() == tmpl.shapes.size());
	const std::vector<ObjectGeometry::Shape> &shapes = barracks->geometryShapes();
	for (size_t i = 0; i < own.shapes.size(); ++i)
	{
		CHECK(own.shapes[i].active == shapes[i].active);
		CHECK(tmpl.shapes[i].active); // the template's stays as written
	}
	const float levelOneSphere = CombatQueries::boundingSphereRadius(*barracks);
	// RW 0xAD2860 over the template's shapes with the Geom_V2 ones off (the shape list as the template wrote it, the flags as Level 1 left them)
	PathfindGeometry levelOne = tmpl;
	const std::vector<ObjectGeometry::Shape> &templateShapes = ObjectGeometry::cachedShapesOf(*barracks->getTemplate());
	for (size_t i = 0; i < levelOne.shapes.size(); ++i)
	{
		levelOne.shapes[i].active = templateShapes[i].name != "Geom_V2";
	}
	CHECK(levelOneSphere == levelOne.computeBoundingSphereRadius());
	CHECK(levelOneSphere != tmpl.boundingSphereRadius()); // tolerance: the Level 1 shapes differ from the template's (the exact value is checked above)
	CHECK(logic.partition().geometryOf(*barracks).sphere == levelOneSphere);
	CHECK(logic.partition().geometryOf(*barracks).circle == CombatQueries::boundingCircleRadius(*barracks));

	// Level 3: Geom_Orig off, Geom_V2 on; the name kept is the last active shape before (a Geom_Orig box)
	const std::uint32_t version = barracks->geometryVersion();
	barracks->giveUpgrade("Upgrade_GondorBarracksLevel3");
	REQUIRE(level3->isAlreadyUpgraded());
	CHECK(barracks->geometryVersion() != version);
	CHECK(level3->upgradeName() == "Geom_Orig");
	CHECK(namedActive(*barracks, "Geom_Orig") == 0);
	CHECK(namedActive(*barracks, "Geom_V2") == 1);
	CHECK(lastActiveName(*barracks) == "Geom_V2");
	CHECK(CombatLineOfSight::maxHeightAbovePosition(*barracks) == 105.0f);
	CHECK(CombatQueries::boundingSphereRadius(*barracks) == tmpl.boundingSphereRadius()); // Geom_V2 holds the template's tallest boxes over Geom_Orig's footprint
	CHECK(logic.partition().geometryOf(*barracks).sphere == CombatQueries::boundingSphereRadius(*barracks));

	// the snapshot (RW 0x8BAB72): the version, the upgrade name, UpgradeModule's; the name survives a save / load round trip
	StateHasher before;
	level3->crc(before);
	XferSave saved;
	level3->xfer(saved);
	const std::string expected = lastActiveName(*barracks);
	level3->forceRefreshUpgrade(); // slot 5: the implementation again: the kept name becomes the last active shape's
	CHECK(level3->upgradeName() == expected);
	CHECK(expected == "Geom_V2");
	StateHasher changed;
	level3->crc(changed);
	CHECK(changed.value() != before.value()); // the name is module state
	{
		XferLoad load(saved.data());
		level3->xfer(load);
	}
	StateHasher after;
	level3->crc(after);
	CHECK(after.value() == before.value());
	CHECK(level3->upgradeName() == "Geom_Orig");
	CHECK(level3->isAlreadyUpgraded());
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(level3->xfer(lt), XferException);
	// the game runs on with the object's own geometry (its later level grants may switch the shapes again: Geom_ModuleTag_ShowWalls on Level2)
	g.live->advance(0.4);
	MESSAGE("after the frames: Geom_Orig " << namedActive(*barracks, "Geom_Orig") << ", Geom_V2 " << namedActive(*barracks, "Geom_V2"));
	CHECK(logic.partition().geometryOf(*barracks).sphere == CombatQueries::boundingSphereRadius(*barracks));
}

// RW 0x8BADDB .. 0x8BAE2C: a conflicting upgrade the object (+ 0x28C) holds stops the implementation; the mux still records it executed (RW 0x855388)
TEST_CASE("GeometryUpgrade: a conflicting object upgrade stops the implementation (RW 0x8BAE07); no geometry change, the mux is still executed")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startGeoGame(*s, "FactionMen", "FactionMordor", 4404, g, &err), err);
	GameLogic &logic = g.live->logic();
	// a template whose GeometryUpgrade names shapes and conflicts with an OBJECT upgrade
	const ThingTemplate *found = nullptr;
	std::string tag;
	const UpgradeTemplate *conflict = nullptr;
	for (const ThingTemplate *t : s->world->things().templates())
	{
		const ThingTemplate *f = t->getFinalOverride();
		if (f->getName().rfind("CINE_", 0) == 0 || f->getName().rfind("Default", 0) == 0)
		{
			continue;
		}
		for (const ThingTemplate::Nugget &n : f->behaviorModules().nuggets())
		{
			const GeometryUpgradeModuleData *d = dynamic_cast<const GeometryUpgradeModuleData *>(n.data.get());
			if (!d || d->m_conflictsWith.empty() || (d->m_showGeometry.empty() && d->m_hideGeometry.empty()))
			{
				continue;
			}
			for (const std::string &c : d->m_conflictsWith)
			{
				const UpgradeTemplate *u = Player::resolveUpgrade(c, false);
				if (u && u->getUpgradeType() == UPGRADE_TYPE_OBJECT)
				{
					found = f;
					tag = n.tag;
					conflict = u;
					break;
				}
			}
			if (found)
			{
				break;
			}
		}
		if (found)
		{
			break;
		}
	}
	REQUIRE(found);
	MESSAGE("GeometryUpgrade with ConflictsWith: " << found->getName() << " " << tag << ", conflict " << conflict->getUpgradeName());
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);
	const Coord3D at{ fortress->getPosition()->x + 600.0f, fortress->getPosition()->y, fortress->getPosition()->z };
	Object *o = g.live->createObject(found->getName(), human->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(o, err);
	GeometryUpgrade *gu = geometryUpgrade(*o, tag);
	REQUIRE(gu);
	if (gu->isAlreadyUpgraded())
	{
		return; // its trigger is already granted at creation: nothing to probe
	}
	o->giveUpgrade(conflict);
	const std::vector<std::uint8_t> flags = o->geometryActive();
	const std::uint32_t version = o->geometryVersion();
	gu->giveSelfUpgrade(); // RW 0x855388: the implementation, then executed
	CHECK(gu->isAlreadyUpgraded());
	CHECK(o->geometryActive() == flags);
	CHECK(o->geometryVersion() == version);
	CHECK(gu->upgradeName().empty());
}

TEST_CASE("GeometryUpgrade: S-1423 is reported with the world's stops and the class is bound (no longer an UnportedModule)")
{
	OPENBFME_REQUIRE_START(s);
	const std::vector<std::string> unported = s->world->modules().unportedModuleClassNames();
	CHECK(std::find(unported.begin(), unported.end(), "GeometryUpgrade") == unported.end());
	int found = 0;
	for (const std::string &l : s->world->acceptanceStops())
	{
		found += l.rfind("[S-1423] GeometryUpgrade (lane MODCOMPAT-MOD1;", 0) == 0 ? 1 : 0;
	}
	CHECK(found == 1);
	// the runtime texts carry the stop id
	CHECK(std::string(GeometryUpgrade::kStopDrawMeshes).rfind("[S-1423]", 0) == 0);
	CHECK(std::string(GeometryUpgrade::kStopWallUpgrade).rfind("[S-1423]", 0) == 0);
}

// Sol review of MODCOMPAT-MOD1 (blocking): retail's order (RW 0x8BADC0 sets the flags at RW 0x8BAFD4 / 0x8BAFE6, then RW 0x68B244(1) at RW 0x8BAFF0 removes
// and adds the footprint) is kept: the removal (RW 0x6E85FB -> 0x936B7D) walks only the ACTIVE shapes and RW 0x93491C clears only the visited cells the object
// owns, so the cells only the hidden shapes covered keep the structure's id, and they keep it after it dies (its removal walks the same active shapes).
// Such a cell stays an obstacle (setType RW 0x934387 forces OBSTACLE while an id is set; nothing reclassifies it). A whole-game save holding those cells
// loads (an issued id, not a live one, AIPathfindSnapshot.cpp) and the loaded game goes on as the uninterrupted one.
TEST_CASE("GeometryUpgrade (Sol review): a level shape change leaves cells with the structure's id after it dies (RW 0x936B7D / 0x93491C); a save of "
		  "them loads and goes on as the uninterrupted game")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startGeoGame(*s, "FactionMen", "FactionMordor", 4405, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);
	const Coord3D at{ fortress->getPosition()->x + 500.0f, fortress->getPosition()->y, fortress->getPosition()->z };
	Pathfinder &pf = logic.aiWorld()->pathfinder();
	ICoord2D centre;
	REQUIRE_FALSE(pf.worldToCell(&at, &centre)); // true = off the grid (clamped)
	// the cells around the spot (a structure's shapes are well inside 40 cells = 400 units) whose obstacle is `who`
	auto cellsOf = [&](ObjectID who) {
		std::vector<std::pair<int, int>> out;
		for (int x = centre.x - 40; x <= centre.x + 40; ++x)
		{
			for (int y = centre.y - 40; y <= centre.y + 40; ++y)
			{
				const PathfindCell *c = pf.cellAt(x, y);
				if (c && (ObjectID)c->getObstacleID() == who)
				{
					out.emplace_back(x, y);
				}
			}
		}
		return out;
	};
	// the retail structures whose GeometryUpgrade hides shapes (no meshes, no conflicts), in the template order: each is built at the spot, gets its
	// upgrade's first trigger (an OBJECT upgrade), and dies; the first that leaves cells is the case. (The Gondor barracks' Level 1 leaves none: its hidden
	// Geom_V2 boxes stand over Geom_Orig's footprint.) The removal walks the shapes active after the flag change, so the cells only the hidden shapes covered
	// keep the id; the structure's own removal at its death walks the same active shapes.
	ObjectID id = INVALID_ID;
	std::string name;
	std::vector<std::pair<int, int>> orphaned;
	size_t registered = 0;
	int tried = 0;
	for (const ThingTemplate *t : s->world->things().templates())
	{
		const ThingTemplate *f = t->getFinalOverride();
		if (!orphaned.empty() || f != t || f->getName().rfind("CINE_", 0) == 0 || f->getName().rfind("Default", 0) == 0)
		{
			continue;
		}
		std::string tag, trigger;
		for (const ThingTemplate::Nugget &n : f->behaviorModules().nuggets())
		{
			const GeometryUpgradeModuleData *d = dynamic_cast<const GeometryUpgradeModuleData *>(n.data.get());
			if (tag.empty() && d && !d->m_hideGeometry.empty() && d->m_conflictsWith.empty() && d->m_wallBoundsMesh.empty() && d->m_rampMesh1.empty() &&
				d->m_rampMesh2.empty() && !d->m_triggeredBy.empty())
			{
				const UpgradeTemplate *u = Player::resolveUpgrade(d->m_triggeredBy.front(), false);
				if (u && u->getUpgradeType() == UPGRADE_TYPE_OBJECT)
				{
					tag = n.tag;
					trigger = d->m_triggeredBy.front();
				}
			}
		}
		if (tag.empty())
		{
			continue;
		}
		Object *o = g.live->createObject(f->getName(), human->getPlayerIndex(), at, 0.0f, &err);
		if (!o)
		{
			MESSAGE(f->getName() << " not placed: " << err);
			continue;
		}
		++tried;
		GeometryUpgrade *upg = geometryUpgrade(*o, tag);
		REQUIRE(upg);
		const ObjectID oid = o->getID();
		const bool before = upg->isAlreadyUpgraded();
		if (!before)
		{
			o->giveUpgrade(trigger);
		}
		g.live->advance(0.1);
		const size_t cells = cellsOf(oid).size();
		logic.destroyObject(o); // processDestroyList: the footprint removal over the active shapes
		g.live->advance(0.1);
		REQUIRE(logic.findObjectByID(oid) == nullptr);
		std::vector<std::pair<int, int>> left = cellsOf(oid);
		if (!left.empty())
		{
			id = oid;
			name = f->getName() + " " + tag + " (" + trigger + (before ? ", given at the creation)" : ")");
			orphaned = left;
			registered = cells;
		}
	}
	MESSAGE(tried << " structures tried; " << name << ": " << registered << " cells before its death, " << orphaned.size() << " keep its id " << id);
	REQUIRE_FALSE(orphaned.empty()); // retail's quirk: the cells only the hidden shapes covered
	CHECK(orphaned.size() < registered); // tolerance: the active shapes' cells were cleared, any count of them will do
	for (const std::pair<int, int> &xy : orphaned)
	{
		const PathfindCell *c = pf.cellAt(xy.first, xy.second);
		CHECK(c->getType() == PathfindCell::CELL_OBSTACLE); // still blocked (RW 0x934387 keeps an id'd cell OBSTACLE)
	}

	// a whole-game save at this frame; the uninterrupted game runs on 90 frames; the save loads back and runs the same 90 frames
	std::vector<std::uint8_t> saved;
	REQUIRE_MESSAGE(logic.saveSnapshot(saved, &err), err);
	const UnsignedInt frame = logic.getFrame();
	const std::uint32_t hashAtSave = logic.computeStateHash();
	std::vector<std::uint32_t> uninterrupted;
	for (int i = 0; i < 90; ++i)
	{
		g.live->stepFrame();
		uninterrupted.push_back(logic.computeStateHash());
	}
	REQUIRE_MESSAGE(g.live->loadSnapshot(saved, &err), err); // refused before the fix: "the obstacle of a cell <id> is not an object of the loaded game"
	REQUIRE(logic.getFrame() == frame);
	REQUIRE(logic.computeStateHash() == hashAtSave);
	CHECK(cellsOf(id) == orphaned);
	for (int i = 0; i < 90; ++i)
	{
		g.live->stepFrame();
		REQUIRE_MESSAGE(logic.computeStateHash() == uninterrupted[(size_t)i], "frame " << logic.getFrame());
	}
	for (const std::pair<int, int> &xy : orphaned)
	{
		const PathfindCell *c = pf.cellAt(xy.first, xy.second);
		CHECK((ObjectID)c->getObstacleID() == id);
		CHECK(c->getType() == PathfindCell::CELL_OBSTACLE);
	}
}
