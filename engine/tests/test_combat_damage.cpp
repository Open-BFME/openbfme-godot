// OpenBFME. COMBAT-1 tests: damage application (Object::attemptDamage, ActiveBody, the armour), pending damage, death (SlowDeathBehavior / DestroyDie), kill credit and the
// bounty, command points and the pathfinder after a death. Synthetic data (combattest::CombatWorld), no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"

using namespace combattest;

namespace
{
DamageInfo hit(const Object *source, float amount, int type, float delay = 0.0f)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	d.m_input.m_delay = delay;
	return d;
}
} // namespace

TEST_CASE("combat damage: the armour of the victim scales a hit by the percent of its damage type, UNRESISTABLE ignores it")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *d = w.unit("Dummy", 'B', 400, 300);
	REQUIRE(w.health(d) == 100.0f);
	DamageInfo slash = hit(a, 40.0f, DAMAGE_SLASH); // PlainArmor: SLASH 50%
	d->attemptDamage(slash);
	CHECK(w.health(d) == 80.0f);
	CHECK(slash.m_output.m_actualDamageDealt == 20.0f);
	DamageInfo pierce = hit(a, 40.0f, DAMAGE_PIERCE); // PIERCE 80%
	d->attemptDamage(pierce);
	CHECK(w.health(d) == doctest::Approx(48.0f));
	DamageInfo bare = hit(a, 10.0f, DAMAGE_UNRESISTABLE);
	d->attemptDamage(bare);
	CHECK(w.health(d) == doctest::Approx(38.0f));
	CHECK(w.combat().counters().damageApplications == 3);
	CHECK(w.combat().counters().kills == 0);
}

TEST_CASE("combat damage: the killing hit is clipped to the health left, the dead take no more damage")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *d = w.unit("Dummy", 'B', 400, 300);
	DamageInfo big = hit(a, 1000.0f, DAMAGE_SLASH); // 500 after the armour
	d->attemptDamage(big);
	CHECK(w.health(d) == 0.0f);
	CHECK(big.m_output.m_actualDamageDealt == 500.0f);          // the amount applied
	CHECK(big.m_output.m_actualDamageClipped == 100.0f);        // what the body really lost
	CHECK(d->isEffectivelyDead());
	CHECK(w.combat().counters().kills == 1);
	DamageInfo more = hit(a, 10.0f, DAMAGE_SLASH);
	d->attemptDamage(more);
	CHECK(w.combat().counters().damageApplications == 1); // the corpse is not damaged again
	CHECK(w.combat().counters().kills == 1);
}

TEST_CASE("combat damage: a delayed hit waits its frames in the pending list, a hit made while the object's AI runs waits one frame")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *d = w.unit("Dummy", 'B', 400, 300);
	DamageInfo delayed = hit(a, 40.0f, DAMAGE_SLASH, 3.0f);
	d->attemptDamage(delayed);
	CHECK(d->pendingDamageCount() == 1);
	CHECK(w.health(d) == 100.0f);
	int frames = 0;
	while (d->pendingDamageCount() != 0 && frames < 10)
	{
		w.frames(1);
		++frames;
	}
	CHECK(frames >= 3);
	CHECK(frames <= 4);
	CHECK(w.health(d) == 80.0f);
	// the AI status: a hit during the object's own update is queued for the next frame
	d->setStatus((unsigned)CombatNames::statuses().updatingAI, true);
	DamageInfo during = hit(a, 40.0f, DAMAGE_SLASH);
	d->attemptDamage(during);
	CHECK(d->pendingDamageCount() == 1);
	CHECK(w.health(d) == 80.0f);
	d->setStatus((unsigned)CombatNames::statuses().updatingAI, false);
	w.frames(2);
	CHECK(d->pendingDamageCount() == 0);
	CHECK(w.health(d) == 60.0f);
}

TEST_CASE("combat damage: healing adds health up to the maximum, never revives")
{
	CombatWorld w;
	Object *d = w.unit("Dummy", 'B', 400, 300);
	DamageInfo d1 = hit(nullptr, 60.0f, DAMAGE_UNRESISTABLE);
	d->attemptDamage(d1);
	CHECK(w.health(d) == 40.0f);
	d->attemptHealing(25.0f, d);
	CHECK(w.health(d) == 65.0f);
	d->attemptHealing(500.0f, d);
	CHECK(w.health(d) == 100.0f);
}

TEST_CASE("combat damage: ImmortalBody never drops below 1 health, HighlanderBody survives a normal hit but dies to an unresistable one")
{
	CombatWorld w(
		"Object Immortal\n"
		"  KindOf = INFANTRY SELECTABLE\n"
		"  Body = ImmortalBody ModuleTag_Body\n"
		"    MaxHealth = 50\n"
		"  End\n"
		"End\n"
		"Object Highlander\n"
		"  KindOf = INFANTRY SELECTABLE\n"
		"  Body = HighlanderBody ModuleTag_Body\n"
		"    MaxHealth = 50\n"
		"  End\n"
		"End\n");
	Object *i = w.unit("Immortal", 'B', 400, 300);
	Object *h = w.unit("Highlander", 'B', 450, 300);
	DamageInfo big = hit(nullptr, 500.0f, DAMAGE_SLASH);
	i->attemptDamage(big);
	CHECK(w.health(i) == 1.0f);
	CHECK_FALSE(i->isEffectivelyDead());
	DamageInfo big2 = hit(nullptr, 500.0f, DAMAGE_SLASH);
	h->attemptDamage(big2);
	CHECK(w.health(h) == 1.0f);
	CHECK_FALSE(h->isEffectivelyDead());
	DamageInfo kill = hit(nullptr, 500.0f, DAMAGE_UNRESISTABLE);
	h->attemptDamage(kill);
	CHECK(h->isEffectivelyDead());
}

TEST_CASE("combat damage: Object::kill takes the whole health with an unresistable hit")
{
	CombatWorld w;
	Object *d = w.unit("Dummy", 'B', 400, 300);
	d->kill(DEATH_NORMAL);
	CHECK(d->isEffectivelyDead());
	CHECK(w.combat().counters().kills == 1);
}

TEST_CASE("combat damage: a dead unit with DestroyDie leaves the world at the next destroy phase, the command points of the dead are free, the bounty goes to the killer's player")
{
	CombatWorld w;
	w.playerOf('A')->setBountyPercent(1.0f);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *d = w.unit("Dummy", 'B', 400, 300);
	const ObjectID id = d->getID();
	CHECK(w.playerOf('B')->commandPoints().getUsage() == 3); // Dummy CommandPoints = 3
	const int cashBefore = w.cash('A');
	DamageInfo big = hit(a, 1000.0f, DAMAGE_SLASH);
	d->attemptDamage(big);
	CHECK(w.cash('A') == cashBefore + 7); // BountyValue 7 at percent 1.0 (Economy::awardBounty)
	CHECK(w.combat().counters().bountyPaid == 7);
	CHECK(w.playerOf('B')->commandPoints().getUsage() == 0);
	w.frames(2);
	CHECK(w.byId(id) == nullptr); // DestroyDie destroys the object; the destroy list runs in the same frame
}

TEST_CASE("combat damage: no bounty at the default percent, none for a kill of the own side or of an ally")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *own = w.unit("Dummy", 'A', 400, 300);
	Object *enemy = w.unit("Dummy", 'B', 450, 300);
	const int cash = w.cash('A');
	DamageInfo k1 = hit(a, 1000.0f, DAMAGE_SLASH);
	enemy->attemptDamage(k1);
	CHECK(w.cash('A') == cash); // the percent is 0.0: a skirmish pays no bounty
	CHECK(w.combat().counters().kills == 1);
	w.playerOf('A')->setBountyPercent(1.0f);
	DamageInfo k2 = hit(a, 1000.0f, DAMAGE_SLASH);
	own->attemptDamage(k2);
	CHECK(w.cash('A') == cash); // a unit of the killer's own side pays nothing
	CHECK(w.combat().counters().bountyPaid == 0);
}

TEST_CASE("combat damage: SlowDeathBehavior sinks the corpse after SinkDelay and destroys it after DestructionDelay; the corpse is no longer a pathfinder obstacle")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *s = w.unit("Swordsman", 'B', 330, 300);
	w.frames(3);
	const ObjectID id = s->getID();
	const float z0 = s->getPosition()->z;
	DamageInfo big = hit(a, 1000.0f, DAMAGE_SLASH);
	s->attemptDamage(big);
	CHECK(s->isEffectivelyDead());
	// SinkDelay 1000 ms = 5 frames, SinkRate 1.0 per second, DestructionDelay 2000 ms = 10 frames
	w.frames(3);
	REQUIRE(w.byId(id) != nullptr);
	CHECK(w.byId(id)->getPosition()->z == doctest::Approx(z0)); // not sinking yet
	w.frames(4);
	REQUIRE(w.byId(id) != nullptr);
	CHECK(w.byId(id)->getPosition()->z < z0); // sinking
	w.frames(10);
	CHECK(w.byId(id) == nullptr); // destroyed
}

// RW 0x6955BC scoreTheKill: IGNORED_IN_GUI (0x695661..0x69566B) excludes the victim; enemy / other owner (0x6956E8..0x6956F7); the bounty call 0x695743 -> 0x6AC06F comes BEFORE
// the victim's playable-side test at 0x69574F (which only gates the later skill-point handling). Review probes of Sol (combat1-review-r1).
TEST_CASE("combat damage: the bounty of an enemy whose side is not playable is paid (RW 0x695743 precedes the playable-side test 0x69574F)")
{
	CombatWorld w;
	w.playerOf('A')->setBountyPercent(1.0f);
	const_cast<PlayerTemplate *>(w.playerOf('B')->getPlayerTemplate())->m_playableSide = false;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *b = w.unit("Dummy", 'B', 400, 300);
	const int before = w.cash('A');
	DamageInfo d = hit(a, 1000.0f, DAMAGE_SLASH);
	b->attemptDamage(d);
	CHECK(w.cash('A') == before + 7); // BountyValue 7 * percent 1.0
	CHECK(w.combat().counters().bountyPaid == 7);
}

TEST_CASE("combat damage: a victim with KindOf IGNORED_IN_GUI pays no bounty (RW 0x695661)")
{
	CombatWorld w(
		"Object IgnoredDummy\n"
		"  KindOf = PRELOAD INFANTRY IGNORED_IN_GUI\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 5\n"
		"  GeometryHeight = 10\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 100\n"
		"  End\n"
		"  BountyValue = 7\n"
		"End\n");
	w.playerOf('A')->setBountyPercent(1.0f);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *b = w.unit("IgnoredDummy", 'B', 400, 300);
	const int before = w.cash('A');
	DamageInfo d = hit(a, 1000.0f, DAMAGE_SLASH);
	b->attemptDamage(d);
	CHECK(b->isEffectivelyDead());
	CHECK(w.cash('A') == before);
	CHECK(w.combat().counters().bountyPaid == 0);
}

// S-320..S-328 reach the run report of the game (GameLogic::report().stops), once each, and the counters line is there.
TEST_CASE("combat stops: S-320 .. S-328 are in GameLogic::report().stops exactly once, with their counters line")
{
	CombatWorld w;
	const GameLogic::Report report = w.logic->report();
	const char *expect[] = { "weapon sets", "delivery", "ActiveBody", "kill credit", "death", "attack machine", "acquisition", "horde combat", "attack commands" };
	for (int id = 320; id <= 328; ++id)
	{
		const std::string prefix = "[S-" + std::to_string(id) + "] ";
		int matches = 0;
		for (const std::string &line : report.stops)
		{
			if (line.rfind(prefix, 0) == 0)
			{
				++matches;
				CHECK_MESSAGE(line.find(expect[id - 320]) != std::string::npos, line.substr(0, 60));
			}
		}
		CHECK_MESSAGE(matches == 1, prefix << "appears " << matches << " times");
	}
	int counters = 0;
	for (const std::string &line : report.stops)
	{
		counters += line.rfind("[S-320..S-328 counters]", 0) == 0 ? 1 : 0;
	}
	CHECK(counters == 1);
	// S-322 says the five structure / respawn / delayed / symbiotic / inactive bodies run since COMBAT-2 (S-340 .. S-343)
	for (const std::string &line : report.stops)
	{
		if (line.rfind("[S-322] ", 0) == 0)
		{
			CHECK(line.find("COMBAT-2") != std::string::npos);
			CHECK(line.find("have NO runtime") == std::string::npos);
		}
	}
	// the counters line follows what happened
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *d = w.unit("Dummy", 'B', 400, 300);
	DamageInfo big = hit(a, 1000.0f, DAMAGE_SLASH);
	d->attemptDamage(big);
	std::string counterLine;
	for (const std::string &line : w.logic->report().stops)
	{
		if (line.rfind("[S-320..S-328 counters]", 0) == 0)
		{
			counterLine = line;
		}
	}
	CHECK(counterLine.find("damage applications 1") != std::string::npos);
	CHECK(counterLine.find("kills 1") != std::string::npos);
}
