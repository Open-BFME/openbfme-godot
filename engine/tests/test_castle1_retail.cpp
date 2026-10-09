// OpenBFME retail tests (lane CASTLE-1): the starting fortress after its unpack in a retail skirmish (Men against an Easy Mordor AI on Evendim). MenFortress has
// KeepDeathKillsEverything = Yes: once the base check is active (frame ftol(5 * SecondsBeforeBaseCheckActive)) the death of its keep (MenFortressCitadel) kills every member
// and the occupant of every pad, destroys the pads and the castle (RW 0x799ACB -> 0x7999E2 with RW 0x797F16 / 0x797F49): the castle stops counting for VictoryConditions
// (rule 2, RW 0x809404: no object of the player matches VictoryConditionStructureObjectFilter), and a player with no builder left is defeated.  SKIP loudly when ROTWK_INSTALL /
// BFME2_INSTALL are unset.  GPL-3.0.

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/Player.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/VictoryConditions.h"

#include <algorithm>
#include <map>

#include <set>
#include <vector>

namespace
{
struct Fortress
{
	Player *men = nullptr;
	Player *mordor = nullptr;
	Object *centre = nullptr;
	CastleBehavior *castle = nullptr;
};

bool findFortress(LiveGame &live, Fortress &out)
{
	out.men = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	out.mordor = live.players().findPlayerWithName(live.report().startSlotPlayers[1]);
	for (Object *o = live.logic().getFirstObject(); o; o = o->getNextObject())
	{
		CastleBehavior *cb = dynamic_cast<CastleBehavior *>(o->findModule("CastleBehavior"));
		if (cb && o->getControllingPlayer() == out.men)
		{
			out.centre = o;
			out.castle = cb;
		}
	}
	return out.men && out.mordor && out.centre && out.castle;
}

void killWithBody(Object *o)
{
	if (o && !o->isDestroyed() && !o->isEffectivelyDead() && o->getBodyModule())
	{
		o->kill(DEATH_NORMAL);
	}
}

int cacheSlotOf(const VictoryConditions &v, const Player *p)
{
	for (int i = 0; i < VictoryConditions::MAX_PLAYER_COUNT; ++i)
	{
		if (v.cachedPlayerIndex(i) == p->getPlayerIndex())
		{
			return i;
		}
	}
	return -1;
}
} // namespace

TEST_CASE("castle1 retail: Men's fortress keeps Men alive until its keep dies; the keep's death takes the whole fortress out of the game and Men is DEFEATED, Mordor wins")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 77, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	for (int i = 0; i < 3; ++i)
	{
		logic.runLogicFrame();
	}
	Fortress f;
	REQUIRE(findFortress(live, f));
	CHECK(f.centre->getTemplate()->getName() == "MenFortress");
	REQUIRE(f.castle->state() == CastleBehavior::STATE_UNPACKED);
	CHECK(f.castle->data()->m_keepDeathKillsEverything);
	Object *keep = logic.findObjectByID(f.castle->keepId());
	REQUIRE(keep != nullptr);
	CHECK(keep->getTemplate()->getName() == "MenFortressCitadel");
	CHECK_FALSE(f.castle->foundations().empty()); // the expansion pads
	CHECK(f.castle->members().empty());           // Fortress_Men is the keep and its pads (BUILD-1's survey: entries - 1 pads); walls come later from the hubs
	CHECK(f.castle->foundations().size() + 1 == f.castle->ownedObjects().size());
	VictoryConditions &v = logic.victory();
	const int menSlot = cacheSlotOf(v, f.men), mordorSlot = cacheSlotOf(v, f.mordor);
	REQUIRE(menSlot >= 0);
	REQUIRE(mordorSlot >= 0);
	// Men loses every unit (its builders included): the fortress alone keeps it in the game
	std::set<ObjectID> fortress(f.castle->ownedObjects().begin(), f.castle->ownedObjects().end());
	fortress.insert(f.centre->getID());
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == f.men && !fortress.count(o->getID()) && !o->isKindOfName("STRUCTURE"))
		{
			killWithBody(o);
		}
	}
	for (int i = 0; i < 60; ++i)
	{
		logic.runLogicFrame();
	}
	CHECK_FALSE(v.isDefeated(menSlot));
	CHECK_FALSE(v.singleAllianceRemaining());
	CHECK(f.castle->state() == CastleBehavior::STATE_UNPACKED);
	// the keep dies
	const ObjectID centreId = f.centre->getID();
	const std::vector<ObjectID> pads = f.castle->foundations(), members = f.castle->members();
	killWithBody(keep);
	logic.runLogicFrame();
	CHECK(logic.findObjectByID(centreId) == nullptr); // destroyed by its own update, removed at the end of the frame
	for (ObjectID id : pads)
	{
		INFO("pad " << id);
		CHECK(logic.findObjectByID(id) == nullptr);
	}
	for (ObjectID id : members)
	{
		const Object *o = logic.findObjectByID(id);
		INFO("member " << id);
		CHECK((o == nullptr || o->isEffectivelyDead()));
	}
	int frames = 0;
	while (!v.isDefeated(menSlot) && frames < 300)
	{
		logic.runLogicFrame();
		++frames;
	}
	CHECK(v.isDefeated(menSlot));
	CHECK_FALSE(v.isDefeated(mordorSlot));
	CHECK(v.singleAllianceRemaining());
	CHECK(v.hasAchievedVictory(f.mordor));
	CHECK(v.hasBeenDefeated(f.men));
	bool defeatEvent = false, victoryEvent = false;
	for (const VictoryConditions::Event &e : v.events())
	{
		defeatEvent = defeatEvent || (e.kind == VictoryConditions::Event::PLAYER_DEFEATED && e.playerIndex == f.men->getPlayerIndex());
		victoryEvent = victoryEvent || (e.kind == VictoryConditions::Event::ALLIANCE_VICTORY && e.playerIndex == f.mordor->getPlayerIndex());
	}
	CHECK(defeatEvent);
	CHECK(victoryEvent);
	MESSAGE("Men defeated " << frames << " frames after the fortress fell");
}

TEST_CASE("castle1 retail: two games of the same seed stay hash-identical frame by frame through the fall of Men's fortress")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game a, b;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 1234, a, &error), error);
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 1234, b, &error), error);
	Fortress fa, fb;
	ObjectID centreA = INVALID_ID;
	for (int frame = 0; frame < 120; ++frame)
	{
		if (frame == 3)
		{
			REQUIRE(findFortress(*a.live, fa));
			REQUIRE(findFortress(*b.live, fb));
			centreA = fa.centre->getID();
		}
		if (frame == 40)
		{
			killWithBody(a.live->logic().findObjectByID(fa.castle->keepId()));
			killWithBody(b.live->logic().findObjectByID(fb.castle->keepId()));
		}
		a.live->logic().runLogicFrame();
		b.live->logic().runLogicFrame();
		REQUIRE_MESSAGE(a.live->logic().computeStateHash() == b.live->logic().computeStateHash(), "frame " << frame);
	}
	CHECK(centreA != INVALID_ID);
	CHECK(a.live->logic().findObjectByID(centreA) == nullptr);
}

namespace
{
Object *placeBetween(GameLogic &logic, const Fortress &f, const char *name, float along)
{
	Object *mordorCentre = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == f.mordor && o->findModule("CastleBehavior"))
		{
			mordorCentre = o;
		}
	}
	REQUIRE(mordorCentre);
	const ThingTemplate *tt = logic.things().findTemplate(name);
	REQUIRE_MESSAGE(tt, name);
	Object *camp = logic.newObject(tt, f.men->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(camp);
	Coord3D at;
	at.x = f.centre->getPosition()->x + (mordorCentre->getPosition()->x - f.centre->getPosition()->x) * along;
	at.y = f.centre->getPosition()->y + (mordorCentre->getPosition()->y - f.centre->getPosition()->y) * along;
	at.z = f.centre->getPosition()->z;
	camp->setPosition(&at);
	camp->friend_onBuildComplete();
	return camp;
}
} // namespace

TEST_CASE("castle1 retail: campaign fortresses placed for Men pre-build their PreBuiltList on the pads of those indices; those whose PreBuiltPlyr is absent unpack nothing (RW 0x79C265 -> 0x79A5EC / 0x79A518)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 77, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	for (int i = 0; i < 3; ++i)
	{
		logic.runLogicFrame();
	}
	Fortress f;
	REQUIRE(findFortress(live, f));
	// PreBuiltPlyr = PlyrRivendell / PlyrElves: no such player on Evendim
	Object *rivendell = placeBetween(logic, f, "MER_ElvenFortress_1", 0.2f);
	Object *greyHavens = placeBetween(logic, f, "EGH_ElvenFortress_1", 0.35f);
	Object *celduin = placeBetween(logic, f, "MordorFortress_Celduin_1", 0.5f);
	Object *blue = placeBetween(logic, f, "WildFortress_BlueMountains_1", 0.7f);
	logic.runLogicFrame();
	for (Object *absent : { rivendell, greyHavens })
	{
		CastleBehavior *rb = dynamic_cast<CastleBehavior *>(absent->findModule("CastleBehavior"));
		REQUIRE(rb);
		CHECK(rb->state() == CastleBehavior::STATE_UNPACKED);
		CHECK(rb->ownedObjects().empty());
		CHECK_FALSE(rb->data()->m_preBuiltList.empty());
	}
	for (Object *camp : { celduin, blue })
	{
		CastleBehavior *cb = dynamic_cast<CastleBehavior *>(camp->findModule("CastleBehavior"));
		REQUIRE(cb);
		INFO(camp->getTemplate()->getName());
		REQUIRE(cb->state() == CastleBehavior::STATE_UNPACKED);
		CHECK(camp->getControllingPlayer() == f.men);
		const auto &list = cb->data()->m_preBuiltList;
		REQUIRE_FALSE(list.empty());
		// an independent model of the pad choice: FS_BASE_DEFENSE templates go to the BASE_DEFENSE_FOUNDATION pads when there are any; -2 takes the first pad that holds nothing;
		// another index is clamped to the last pad; a pad whose foundation holds something takes nothing
		const int fsBaseDefense = ObjectTemplateInfoBuilder::kindOfIndex("FS_BASE_DEFENSE");
		REQUIRE(fsBaseDefense >= 0);
		std::map<ObjectID, std::string> want;
		for (const auto &entry : list)
		{
			const ThingTemplate *et = logic.things().findTemplate(entry.first);
			REQUIRE(et);
			const bool defence = MaskTest(logic.templateInfo(et->getFinalOverride()).kindOf, (unsigned)fsBaseDefense);
			const std::vector<ObjectID> &pads = defence && !cb->defenseFoundations().empty() ? cb->defenseFoundations() : cb->foundations();
			REQUIRE_FALSE(pads.empty());
			if (entry.second == -2)
			{
				for (ObjectID pad : pads)
				{
					if (!want.count(pad))
					{
						want[pad] = entry.first;
						break;
					}
				}
				continue;
			}
			const size_t at = entry.second < 0 ? pads.size() - 1 : std::min((size_t)entry.second, pads.size() - 1);
			if (!want.count(pads[at]))
			{
				want[pads[at]] = entry.first;
			}
		}
		std::map<ObjectID, std::string> got;
		for (const std::vector<ObjectID> *pads : { &cb->foundations(), &cb->defenseFoundations() })
		{
			for (ObjectID pad : *pads)
			{
				const CastleMemberBehavior *m = dynamic_cast<const CastleMemberBehavior *>(logic.findObjectByID(pad)->findModule("CastleMemberBehavior"));
				const Object *occupant = m && m->occupantId() != INVALID_ID ? logic.findObjectByID(m->occupantId()) : nullptr;
				if (occupant)
				{
					got[pad] = occupant->getTemplate()->getName();
					CHECK(occupant->getControllingPlayer() == f.men);
					CHECK_FALSE(occupant->isUnderConstruction());
				}
			}
		}
		CHECK(got == want);
		CHECK(got.size() == list.size());
		MESSAGE(camp->getTemplate()->getName() << ": " << cb->foundations().size() << " pads, " << cb->defenseFoundations().size() << " defence pads, " << got.size() << " pre-built structures");
	}
	for (const std::string &e : logic.report().errors)
	{
		MESSAGE("logic error: " << e);
	}
}

TEST_CASE("castle1 retail: every faction's starting fortress registers its keep and still stands after the base check delay")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		INFO(faction);
		std::string error;
		buildtest::Game g;
		REQUIRE_MESSAGE(buildtest::startGame(*s, faction, "FactionMen", 77, g, &error), error);
		GameLogic &logic = g.live->logic();
		for (int i = 0; i < 3; ++i)
		{
			logic.runLogicFrame();
		}
		Fortress f;
		REQUIRE(findFortress(*g.live, f));
		const ObjectID centreId = f.centre->getID();
		const Object *keep = logic.findObjectByID(f.castle->keepId());
		REQUIRE(keep != nullptr);
		CHECK(keep->isKindOfName("CASTLE_KEEP"));
		for (int i = 0; i < 60; ++i)
		{
			logic.runLogicFrame();
		}
		REQUIRE(logic.findObjectByID(centreId) != nullptr);
		CHECK(f.castle->state() == CastleBehavior::STATE_UNPACKED);
		CHECK_FALSE(f.castle->keepGone());
		CHECK(logic.report().errors.empty());
	}
}
