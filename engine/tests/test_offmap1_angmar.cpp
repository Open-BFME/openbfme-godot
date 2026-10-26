// OpenBFME unit tests: lane OFFMAP-1. Units a map places outside its playable area stay there, OFF_MAP, as in RotWK, until a script sends them in.
// MAP ANG Angmar places the Black Numenorean counterattack ('BNEast / BNWest WR 1..3' AngmarWolfRiders, 'Counterattack Wave 1' with their dark
// rangers, Dunedain and troll slings) below the map's edge (y -78 .. -375). Retail keeps them there: nothing moves a placed unit
// (AIIdleState::doInitIdleState RW 0x745A64 only updates the pathfinder's goal; the horde member destination RW 0x6F0889 leaves the slot alone when the
// horde has no pathfind cell, RW 0x6E8CE6 does not clamp), so they are OFF_MAP (Object + 0x458 bit 3, RW 0x69355D), neither scan (RW 0x66845E) nor are
// found (RW 0x6FF7A4), and idle until 'BNWest Move In' / 'BNEast Move In Part 1 / 2' (TIMER_EXPIRED PlyrAngmar/BN Counterattack Go) march them in.
// GPL-3.0.
#include "doctest.h"
#include "CampaignTestUtil.h"
#include "StartTestUtil.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/ScriptEngine/MissionObjectiveTracker.h"

#include <string>
#include <vector>

namespace
{
// the map's three counterattack teams (the teams 'BN Arrival Message' tests), every object of them (hordes and their members)
const char *const kWaves[] = { "PlyrBNEast/Counterattack Wave 1", "PlyrBNWest/Counterattack Wave 1", "PlyrBNOther/South Counterattack Wave 1" };
const char *const kWolfRiders[] = { "BNEast WR 1", "BNEast WR 2", "BNEast WR 3", "BNWest WR 1", "BNWest WR 2", "BNWest WR 3" };

struct Placed
{
	ObjectID id;
	Coord3D pos;
};

std::vector<Placed> waveObjects(campaigntest::Mission &m)
{
	std::vector<Placed> out;
	for (const char *team : kWaves)
	{
		Team *t = ScriptConditions::team(m.engine(), team);
		REQUIRE_MESSAGE(t != nullptr, team);
		for (Object *o : ScriptConditions::members(*t))
		{
			out.push_back(Placed{ o->getID(), *o->getPosition() });
		}
	}
	return out;
}

// the map's own values (MAP ANG Angmar's objects list, read by tools/maps/oracle/fullparse.py): every wolf rider horde stands below y = 0
void requireWolfRidersPlacedOffMap(campaigntest::Mission &m)
{
	for (const char *n : kWolfRiders)
	{
		Object *h = m.unit(n);
		REQUIRE_MESSAGE(h != nullptr, n);
		CHECK_MESSAGE(h->getPosition()->y < -70.0f, n << " y " << h->getPosition()->y); // tolerance: the map places them at y -78 .. -173 (the map data), below the edge with margin
		CHECK_MESSAGE(h->isOffMap(), n);
	}
}

// the wave objects standing in 'AT - Reveal Rogash Battle' (the trigger 'BN Arrival Message' tests), by the polygon test of the trigger
size_t wavesInsideArrivalArea(campaigntest::Mission &m)
{
	const TriggerArea *area = m.engine().findTrigger("AT - Reveal Rogash Battle");
	REQUIRE(area != nullptr);
	size_t inside = 0;
	for (const char *team : kWaves)
	{
		Team *t = ScriptConditions::team(m.engine(), team);
		REQUIRE_MESSAGE(t != nullptr, team);
		for (Object *o : ScriptConditions::members(*t))
		{
			inside += ScriptConditions::pointInTrigger(*area, o->getPosition()->x, o->getPosition()->y) ? 1u : 0u;
		}
	}
	return inside;
}

// a one-shot script that ran is no longer active (the script engine clears + 0x40 after its actions); the script must exist (a typo is no "fired")
bool fired(campaigntest::Mission &m, const std::string &qualified)
{
	const ScriptEngine::RScript *r = m.engine().findScript(qualified);
	REQUIRE_MESSAGE(r != nullptr, qualified);
	return !r->active;
}
bool arrivalFired(campaigntest::Mission &m, const std::string &me)
{
	return fired(m, me + "/BN Arrival Message");
}
} // namespace

TEST_CASE("offmap1 retail: MAP ANG Angmar's counterattack placed below the map stays where the map put it, OFF_MAP, through the intro: no fight, the "
		  "Witch-king and Morgomir keep their experience, 'BN Arrival Message' does not fire")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.load(*s, "map ang angmar");
	const std::string me = m.logic().players().getLocalPlayer()->getPlayerName();
	REQUIRE(m.engine().findScript(me + "/BN Arrival Message") != nullptr);
	REQUIRE(m.engine().scriptActive(me + "/BN Arrival Message"));
	requireWolfRidersPlacedOffMap(m);
	const std::vector<Placed> placed = waveObjects(m);
	REQUIRE(placed.size() == 249); // 23 hordes and their members (the port's count of the three teams at load; the map places 23 hordes there)
	size_t offMapAtLoad = 0;
	for (const Placed &p : placed)
	{
		offMapAtLoad += m.logic().findObjectByID(p.id)->isOffMap() ? 1u : 0u;
	}
	CHECK(offMapAtLoad == placed.size()); // every one of them stands at y < 0
	Object *wk = m.unit("Witch King");
	Object *morgomir = m.unit("Morgomir");
	REQUIRE(wk != nullptr);
	REQUIRE(morgomir != nullptr);
	REQUIRE(wk->getExperienceTracker() != nullptr);
	REQUIRE(morgomir->getExperienceTracker() != nullptr);
	const float wkXp = wk->getExperienceTracker()->getExperience();
	const float morgomirXp = morgomir->getExperienceTracker()->getExperience();
	const int wkRank = wk->getExperienceTracker()->getRank();

	// the intro (its end: 'Intro Cine Part 5' shows objective 3) and 150 frames after it: before OFFMAP-1 the wolf riders were pulled onto the grid's row 0
	// at frame 1, 'BN Arrival Message' fired at frame 26 and the Witch-king reached level 3 at about frame 145
	const MissionObjectiveTracker &t = m.logic().missionObjectives();
	REQUIRE(m.until([&] { return t.objectives().size() > 2 && t.objectives()[2].shown; }, 400)); // tolerance: a frame budget for the intro (it ends at frame 181)
	const unsigned introEnd = m.frame();
	MESSAGE("intro over at frame " << introEnd);
	m.step(150);
	for (const Placed &p : placed)
	{
		const Object *o = m.logic().findObjectByID(p.id);
		REQUIRE(o != nullptr);
		CHECK_FALSE(o->isEffectivelyDead());
		CHECK(o->isOffMap());
		// a horde member turns to its slot's facing and takes its slot, nothing more: no one walks toward the map. The wolf riders' banner carrier
		// (AngmarThrallMasterWolfBanner, created at the horde's position) walks to its banner slot beside the horde, still below the edge: 35 before merge
		// queue q26, 55 since the slot table (banner slot included) is centred on the horde (community #73, RW 0x877751 -> RW 0x86C4EA) and BANNER-1's
		// carrier takes that centred slot (measured: 54.2 .. 55.1)
		CHECK(o->getPosition()->y < 0.0f); // tolerance: below the map edge is the claim (OFF_MAP), not a position
		const float dx = o->getPosition()->x - p.pos.x, dy = o->getPosition()->y - p.pos.y;
		const float limit = o->getTemplate()->getName() == "AngmarThrallMasterWolfBanner" ? 56.0f : 10.0f;
		CHECK_MESSAGE(dx * dx + dy * dy < limit * limit, o->getTemplate()->getName() << " " << o->getID() << " moved (" << dx << ", " << dy << ")"); // tolerance: a member takes its formation slot (turn + step; the banner its slot 55 away), no walk
		CHECK(o->getBodyModule()->getHealth() == o->getBodyModule()->getMaxHealth());
	}
	CHECK(wk->getExperienceTracker()->getExperience() == wkXp);
	CHECK(wk->getExperienceTracker()->getRank() == wkRank);
	CHECK(morgomir->getExperienceTracker()->getExperience() == morgomirXp);
	CHECK_FALSE(t.objectives()[9].completed);  // 'WK Reaches Level 3'
	CHECK_FALSE(t.objectives()[10].completed); // 'Morgomir Reaches Level 3'
	CHECK_FALSE(arrivalFired(m, me));
	CHECK(wavesInsideArrivalArea(m) == 0u);
	CHECK_FALSE(m.requested("DISPLAY_NOTIFICATION_BOX", "BonusObjectiveCompleted"));
}

TEST_CASE("offmap1 retail: MAP ANG Angmar's counterattack timer sends the off-map waves in: they cross the map's edge (OFF_MAP clears), reach their "
		  "march on and 'BN Arrival Message' fires then, not before")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.load(*s, "map ang angmar");
	const std::string me = m.logic().players().getLocalPlayer()->getPlayerName();
	requireWolfRidersPlacedOffMap(m);
	m.step(100);
	CHECK_FALSE(arrivalFired(m, me));
	// the map's own 'DEBUG - Test BN Counterattack' (disabled in the map: SET_MILLISECOND_TIMER(PlyrAngmar/BN Counterattack Go, 10)) stands in for the
	// three fortresses + 5 s + 120 s the real flow takes (test_script2_angmar plays that flow to the counterattack's damage and the victory)
	ScriptEngine::RScript *debug = nullptr;
	std::string debugSide;
	for (int i = 0; i < m.logic().players().getPlayerCount() && !debug; ++i)
	{
		debugSide = m.logic().players().getNthPlayer(i)->getPlayerName();
		debug = m.engine().findScript(debugSide + "/DEBUG - Test BN Counterattack");
	}
	REQUIRE(debug != nullptr);
	MESSAGE("'DEBUG - Test BN Counterattack' is a script of side " << debugSide);
	CHECK_FALSE(debug->active);
	debug->active = true;
	CHECK_FALSE(fired(m, "PlyrBNWest/BNWest Move In"));
	CHECK_FALSE(fired(m, "PlyrBNEast/BNEast Move In Part 1"));
	const unsigned go = m.frame();
	// the timer runs 10 s = 50 frames (SET_MILLISECOND_TIMER's real is seconds, RW 0x60911C); the Move In scripts fire when it expires
	REQUIRE(m.until([&] { return fired(m, "PlyrBNWest/BNWest Move In"); }, 80));
	MESSAGE("BNWest Move In at frame " << m.frame() << " (debug timer set at " << go << ")");
	CHECK_FALSE(arrivalFired(m, me));
	// every wolf rider horde crosses y = 0 (OFF_MAP clears in RW 0x69355D's position change) and the waves start their attack
	REQUIRE(m.until(
		[&] {
			for (const char *n : kWolfRiders)
			{
				Object *h = m.unit(n);
				if (!h || h->isOffMap())
				{
					return false;
				}
			}
			return true;
		},
		600));
	MESSAGE("every wolf rider horde on the map at frame " << m.frame());
	REQUIRE(m.until([&] { return arrivalFired(m, me); }, 900));
	MESSAGE("'BN Arrival Message' at frame " << m.frame() << " (" << (m.frame() - go) << " frames after the timer)");
	CHECK(m.frame() > go); // tolerance: after the timer is the claim; the frame is printed
	// retail's condition (the map's script): TEAM_INSIDE_AREA_PARTIALLY of one of the three waves in 'AT - Reveal Rogash Battle'
	CHECK(wavesInsideArrivalArea(m) > 0u); // tolerance: "partially inside" is any member
	// each wolf rider horde marches on toward its path's end (the armed attack move); the march to the fortresses and their damage is
	// test_script2_angmar's real flow ("Uber survives the counterattack": the counterattack damages a completed fortress)
	for (const char *n : kWolfRiders)
	{
		Object *h = m.unit(n);
		REQUIRE_MESSAGE(h != nullptr, n);
		CHECK_MESSAGE(h->getAIUpdateInterface()->attackMoveActive(), n);
	}
}
