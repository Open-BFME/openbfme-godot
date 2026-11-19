// OpenBFME unit tests (lane BUILDCLEAR-1): BuildAssistant::isLocationClearOfObjects as RotWK's RW 0x796D37 (tier A = BFME2 0x794879, BFME2 decomp
// BuildAssistant.cpp:2634) and the RotWK option bits and codes of isLocationLegalToBuild RW 0x796810 (BFME2 0x794352), on a retail skirmish (Evendim, Men
// against Mordor). The retail cases SKIP loudly when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ConstructionSite.h"
#include "GameLogic/SimMath.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{
const float kQuarterTurn = 1.57079637f;

Object *spawn(GameLogic &logic, const char *name, Player &owner, const Coord3D &at, float angle = 0.0f)
{
	const ThingTemplate *t = logic.things().findTemplate(name);
	REQUIRE_MESSAGE(t != nullptr, "retail template " << name);
	Object *o = logic.newObject(t, owner.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE_MESSAGE(o != nullptr, name);
	Coord3D p = at;
	o->setPosition(&p);
	o->setOrientation(angle);
	testutil::completeHorde(o);
	return o;
}

Coord3D onGround(GameLogic &logic, float x, float y)
{
	return { x, y, logic.getGroundHeight(x, y) };
}

struct Scene
{
	buildtest::Game g;
	GameLogic *logic = nullptr;
	Player *men = nullptr, *mordor = nullptr;
	Object *porter = nullptr;
	const ThingTemplate *barracks = nullptr;
	Coord3D site{};
	explicit Scene(starttest::Shared &s)
	{
		std::string error;
		REQUIRE_MESSAGE(buildtest::startGame(s, "FactionMen", "FactionMordor", 4242, g, &error), error);
		logic = &g.live->logic();
		men = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
		mordor = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[1]);
		REQUIRE(men);
		REQUIRE(mordor);
		REQUIRE(men->getRelationship(mordor->getDefaultTeam()) == ENEMIES);
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			porter = !porter && o->getControllingPlayer() == men && o->getTemplate()->getName() == "MenPorter" ? o : porter;
		}
		REQUIRE(porter);
		barracks = logic->things().findTemplate("GondorBarracks")->getFinalOverride();
		// the first open site on rings around the porter where the barracks is legal facing either way (terrain + objects: 0x05)
		bool found = false;
		for (int r = 2; r < 14 && !found; ++r)
		{
			for (int k = 0; k < 16 && !found; ++k)
			{
				const float a = 6.2831853f * (float)k / 16.0f;
				const Coord3D p = onGround(*logic, porter->getPosition()->x + 50.0f * (float)r * std::cos(a), porter->getPosition()->y + 50.0f * (float)r * std::sin(a));
				found = code(p, 0.0f, 0x05) == LBC_OK && code(p, kQuarterTurn, 0x05) == LBC_OK;
				site = p;
			}
		}
		REQUIRE(found);
	}
	int code(const Coord3D &p, float angle, unsigned options, Object *builder = nullptr, const ThingTemplate *what = nullptr)
	{
		return (int)BuildPlacement::isLocationLegalToBuild(*logic, p, what ? *what : *barracks, angle, options, builder ? builder : porter, nullptr);
	}
	// the barracks' exit point at `angle` as RW 0x797040 .. 0x797242 computes it: QueueProductionExitUpdate's NaturalRallyPoint + 20 along its own direction,
	// turned by the angle (independent model: plain float maths)
	Coord3D exitPoint(float angle) const
	{
		const QueueProductionExitUpdateModuleData *exit = nullptr;
		for (const ThingTemplate::Nugget &n : barracks->behaviorModules().nuggets())
		{
			exit = exit ? exit : dynamic_cast<const QueueProductionExitUpdateModuleData *>(n.data.get());
		}
		REQUIRE(exit);
		const Coord3D r = exit->m_naturalRallyPoint;
		const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
		const float px = r.x + r.x / len * 20.0f, py = r.y + r.y / len * 20.0f;
		return { site.x + std::cos(angle) * px - std::sin(angle) * py, site.y + std::sin(angle) * px + std::cos(angle) * py, site.z };
	}
	bool touchesBarracks(const Object &o, float angle) const
	{
		return NotifyCrushModules::geometriesOverlap(ObjectGeometry::shapesOf(*barracks), site, angle, o.geometryShapes(), *o.getPosition(), o.getOrientation());
	}
};
} // namespace

TEST_CASE("BUILDCLEAR-1: RotWK's KindOf, status, option and code numbers that RW 0x796810 / 0x796D37 test")
{
	// RW 0xDA0E68 (BitFlagNames): the template bytes RW 0x796D37 and its filters test
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("IMMOBILE") == 2);         // + 0x108 & 4
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE") == 7);        // + 0x108 & 0x80
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("AIRCRAFT") == 12);        // + 0x109 & 0x10
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("DOZER") == 14);           // + 0x109 & 0x40
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("INERT") == 89);           // + 0x113 & 2
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("BASE_FOUNDATION") == 104); // + 0x115 & 1
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("BASE_SITE") == 120);      // + 0x117 & 1
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("BLOCKING_GATE") == 137);  // + 0x119 & 2 (RW 0x661AE1)
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("TAINT") == 152);          // + 0x11B & 1
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("WALL_HUB") == 156);       // + 0x11B & 0x10
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("WALL_SEGMENT") == 189);   // + 0x11F & 0x20
	CHECK(std::strcmp(TheObjectStatusNames[98], "ATTACHED") == 0);          // RW 0x796F0F: testStatus(0x62)
	// RW 0x796810 (BFME2 decomp BuildAssistant.cpp:1990) and InGameUI::displayCantBuildMessage (BFME2 0x69C291)
	CHECK(LLF_TERRAIN_RESTRICTIONS == 0x01u);
	CHECK(LLF_CLEAR_PATH == 0x02u);
	CHECK(LLF_NO_OBJECT_OVERLAP == 0x04u);
	CHECK(LLF_USE_QUICK_PATHFIND == 0x08u);
	CHECK(LLF_NO_ENEMY_OBJECT_OVERLAP == 0x20u);
	CHECK(LLF_NO_EXIT_TEST == 0x40u);
	CHECK(LLF_IGNORE_FOGGED == 0x80u);
	CHECK(LLF_CASTLE_MEMBERS == 0x100u);
	CHECK(LLF_ENEMY_STRUCTURES_ONLY == 0x200u);
	CHECK(LLF_PENDING_STRUCTURES == 0x400u);
	CHECK(LBC_NO_CLEAR_PATH == 2);
	CHECK(LBC_TOO_CLOSE_TO_SUPPLIES == 3);
	CHECK(LBC_SHROUD == 4);
	CHECK(LBC_NOT_FLAT_ENOUGH == 5);
	CHECK(LBC_RESTRICTED_TERRAIN == 6);
	CHECK(LBC_OBJECTS_IN_THE_WAY == 8);
	CHECK(LBC_BEYOND_BUILDOUT == 9);
}

TEST_CASE("BUILDCLEAR-1: GameData MaxCastleRadius (GlobalData + 0x1220) is loaded; retail reads 130")
{
	OPENBFME_REQUIRE_START(s);
	Scene sc(*s);
	CHECK(sc.logic->settings().buildRulesLoaded);
	CHECK(sc.logic->settings().maxCastleRadiusLoaded);
	CHECK(sc.logic->settings().maxCastleRadius == 130.0f);
}

TEST_CASE("BUILDCLEAR-1: a barracks whose exit strip runs into the player's own structure is refused (code 8); turned away it is accepted (RW 0x797040 .. 0x7973F7)")
{
	OPENBFME_REQUIRE_START(s);
	Scene sc(*s);
	GameLogic &logic = *sc.logic;
	// a Gondor well (a STRUCTURE with no exit) on the strip from the site to the exit point, clear of the barracks' own footprint and within twice its circle
	const Coord3D e = sc.exitPoint(0.0f);
	const float range = SimMath::mulf32(ConstructionSite::geometryBounds(ObjectGeometry::shapesOf(*sc.barracks)).circle, 2.0f);
	Object *well = nullptr;
	for (int i = 20; i >= 5 && !well; --i)
	{
		const float t = (float)i / 20.0f;
		const Coord3D at = onGround(logic, sc.site.x + (e.x - sc.site.x) * t, sc.site.y + (e.y - sc.site.y) * t);
		Object *w = spawn(logic, "GondorWell", *sc.men, at);
		const float dx = at.x - sc.site.x, dy = at.y - sc.site.y;
		if (sc.touchesBarracks(*w, 0.0f) || sc.touchesBarracks(*w, kQuarterTurn) || std::sqrt(dx * dx + dy * dy) > range)
		{
			logic.destroyObject(w);
			logic.runLogicFrame();
			continue;
		}
		well = w;
	}
	REQUIRE(well);
	logic.runLogicFrame();
	MESSAGE("site " << sc.site.x << " " << sc.site.y << ", exit point " << e.x << " " << e.y << ", well " << well->getPosition()->x << " " << well->getPosition()->y);
	// the exit faces the well: objects in the way, with the placement click's 0x49F as with the bare overlap test
	CHECK(sc.code(sc.site, 0.0f, 0x05) == LBC_OBJECTS_IN_THE_WAY);
	CHECK(sc.code(sc.site, 0.0f, 0x49F) == LBC_OBJECTS_IN_THE_WAY);
	// the strip is what refuses: bit 0x40 skips the exit test (RW 0x797026)
	CHECK(sc.code(sc.site, 0.0f, 0x45) == LBC_OK);
	// turned a quarter, the exit faces away: accepted
	CHECK(sc.code(sc.site, kQuarterTurn, 0x05) == LBC_OK);
	CHECK(sc.code(sc.site, kQuarterTurn, 0x49F) == LBC_OK);
}

TEST_CASE("BUILDCLEAR-1: an enemy flyer over the site does not block (the 3D overlap, RW 0xAD2CE0); AIRCRAFT never blocks (RW 0x796E80)")
{
	OPENBFME_REQUIRE_START(s);
	Scene sc(*s);
	GameLogic &logic = *sc.logic;
	REQUIRE(sc.code(sc.site, 0.0f, 0x45) == LBC_OK);
	// a Mordor fell beast over the site (retail's fell beasts are not KindOf AIRCRAFT: their height keeps them out of the geometry filter)
	Object *beast = spawn(logic, "MordorFellBeast", *sc.mordor, { sc.site.x, sc.site.y, sc.site.z + 200.0f });
	CHECK_FALSE(beast->isKindOfName("AIRCRAFT"));
	logic.runLogicFrame(); // ThePartitionManager takes the new object's place at the frame's update
	MESSAGE("the fell beast flies at " << beast->getPosition()->z - sc.site.z << " above the site");
	CHECK(sc.code(sc.site, 0.0f, 0x45) == LBC_OK);
	// the same shapes on the ground would touch the barracks: ZH's 2D footprint test (the port before this lane) refused the site
	CHECK(NotifyCrushModules::geometriesOverlap(ObjectGeometry::shapesOf(*sc.barracks), sc.site, 0.0f, beast->geometryShapes(), sc.site, beast->getOrientation()));
	CHECK_FALSE(sc.touchesBarracks(*beast, 0.0f));
	// a KindOf AIRCRAFT object of the enemy on the ground of the site is skipped
	Object *tornado = spawn(logic, "GaladrielTornado", *sc.mordor, sc.site);
	REQUIRE(tornado->isKindOfName("AIRCRAFT"));
	logic.runLogicFrame();
	if (!tornado->isDestroyed())
	{
		CHECK(sc.code(sc.site, 0.0f, 0x45) == LBC_OK);
	}
}

TEST_CASE("BUILDCLEAR-1: an enemy infantry unit on the site blocks (code 8), an allied one does not (RW 0x796F4B .. 0x796F61)")
{
	OPENBFME_REQUIRE_START(s);
	Scene sc(*s);
	GameLogic &logic = *sc.logic;
	REQUIRE(sc.code(sc.site, 0.0f, 0x45) == LBC_OK);
	Object *orc = spawn(logic, "MordorFighter", *sc.mordor, sc.site);
	REQUIRE(orc->isKindOfName("INFANTRY"));
	logic.runLogicFrame();
	REQUIRE(sc.touchesBarracks(*orc, 0.0f));
	CHECK(sc.code(sc.site, 0.0f, 0x45) == LBC_OBJECTS_IN_THE_WAY);
	// bit 0x200: only an enemy STRUCTURE blocks
	CHECK(sc.code(sc.site, 0.0f, 0x245) == LBC_OK);
	logic.destroyObject(orc);
	logic.runLogicFrame();
	Object *ally = spawn(logic, "GondorFighter", *sc.men, sc.site);
	REQUIRE(ally->isKindOfName("INFANTRY"));
	logic.runLogicFrame();
	REQUIRE(sc.touchesBarracks(*ally, 0.0f));
	CHECK(sc.code(sc.site, 0.0f, 0x45) == LBC_OK);
}

TEST_CASE("BUILDCLEAR-1: a castle's own members do not block its layout with bit 0x100 (RW 0x796E9A .. 0x796EE3); without it they do")
{
	OPENBFME_REQUIRE_START(s);
	Scene sc(*s);
	GameLogic &logic = *sc.logic;
	Object *fortress = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		fortress = !fortress && o->getControllingPlayer() == sc.men && o->getTemplate()->getName() == "MenFortress" ? o : fortress;
	}
	REQUIRE(fortress);
	REQUIRE(fortress->isKindOfName("BASE_SITE"));
	CastleBehavior *castle = dynamic_cast<CastleBehavior *>(fortress->findModule("CastleBehavior"));
	REQUIRE(castle);
	// every structure member of the fortress, on its own spot, with its own template and angle
	int members = 0;
	for (ObjectID id : castle->ownedObjects())
	{
		Object *m = logic.findObjectByID(id);
		if (!m || m->isDestroyed() || !m->isKindOfName("STRUCTURE") || m->isKindOfName("INERT"))
		{
			continue;
		}
		const ThingTemplate *tt = static_cast<const ThingTemplate *>(m->getTemplate());
		const int alone = sc.code(*m->getPosition(), m->getOrientation(), 0x104, fortress, tt);
		const int other = sc.code(*m->getPosition(), m->getOrientation(), 0x004, fortress, tt);
		INFO(tt->getName());
		CHECK(alone == LBC_OK);
		CHECK(other == LBC_OBJECTS_IN_THE_WAY);
		++members;
	}
	CHECK(members == 7); // the MenFortress layout's structure members on Evendim
	MESSAGE("fortress members checked: " << members);
}

TEST_CASE("BUILDCLEAR-1: bit 0x400 refuses a site over the player's unplaced dozer structure (RW 0x796F9E .. 0x797017)")
{
	OPENBFME_REQUIRE_START(s);
	Scene sc(*s);
	GameLogic &logic = *sc.logic;
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(sc.porter->getAIUpdateInterface());
	REQUIRE(dozer);
	Object *phantom = dozer->construct(*sc.barracks, sc.site, 0.0f, *sc.men);
	REQUIRE(phantom);
	REQUIRE(dozer->unplacedStructure() == phantom->getID());
	Object *other = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		other = !other && o != sc.porter && o->getControllingPlayer() == sc.men && o->getTemplate()->getName() == "MenPorter" ? o : other;
	}
	REQUIRE(other);
	// the porter carries its phantom out of the world (no partition entry): only the player's list (bit 0x400) sees it
	CHECK_FALSE(phantom->isInWorld());
	CHECK(sc.code(sc.site, 0.0f, 0x044, other) == LBC_OK);
	CHECK(sc.code(sc.site, 0.0f, 0x404, other) == LBC_OBJECTS_IN_THE_WAY);
	CHECK(sc.code(sc.site, 0.0f, 0x444, other) == LBC_OBJECTS_IN_THE_WAY);
}
