// OpenBFME. PLAY-4: the Discord tester's "Aragorn doesn't attack anyone" (v0.3.0-preview.6), with the retail data: every multiplayer hero of every faction is
// recruited from its fortress (MSG_QUEUE_UNIT_CREATE with the hero record's index, the ring heroes after the spell book's Upgrade_RingHero), walks to a spot
// between the bases (a player move order) and then, in two separate games:
//   * on its own: the Battle stance's guard (AIGuard.h) attacks an enemy horde made 100 away, no order;
//   * on an order: with the auto-acquire switched off (no unit picks a target by itself, the horde does not fight back), the hero is ordered onto the horde.
// Each mode counts the hero's strikes and the health the horde's members lost.
// The cause of the report (lane PLAY-4): a lone melee unit whose engagement search (RW 0x6F37B3) finds no free spot next to a horde member falls back to the
// contact back-off (RW 0x7463E8), whose CanApproachToTarget (RW 0x6FB754) the port had replaced by a line test that other units' goal reservations close: next
// to a horde no line was ever open, so the hero waited ten seconds at a time and never struck. Needs the retail install (SKIPs loudly without it).

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/Armor.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>

namespace
{
// a horde goes with its last member: looked up by id (0 health when gone)
float membersHealth(GameLogic &logic, ObjectID id)
{
	float h = 0.0f;
	Object *horde = logic.findObjectByID(id);
	if (horde && !(horde->getContain() && horde->getContain()->getHordeContainInterface()))
	{
		return horde->isEffectivelyDead() ? 0.0f : horde->getBodyModule()->getHealth(); // lane ATTACK-1: a lone foe (the duel)
	}
	if (horde && horde->getContain() && horde->getContain()->getContainedItemsList())
	{
		for (Object *m : *horde->getContain()->getContainedItemsList())
		{
			h += m->isEffectivelyDead() ? 0.0f : m->getBodyModule()->getHealth();
		}
	}
	return h;
}

std::vector<std::string> heroesOf(const PlayerTemplate &pt)
{
	std::vector<std::string> out;
	std::set<std::string> seen;
	for (const auto *list : { &pt.m_buildableHeroesMP, &pt.m_buildableRingHeroesMP })
	{
		for (const std::string &h : *list)
		{
			if (seen.insert(h).second)
			{
				out.push_back(h);
			}
		}
	}
	return out;
}

struct Fight
{
	bool recruited = false;
	int recruitFrames = -1;
	bool giantBird = false; // GIANT_BIRD heroes (the fell beasts, Drogoth) run GiantBirdAIUpdate
	unsigned long long shots = 0; // ObjectWeapons' counter: the weapon's shots AND the CrushWeapon's deliveries (lane ATTACK-1 r2: weaponShots below)
	float lost = 0.0f;
	unsigned hordeMembers = 0;
	unsigned membersKilled = 0;
	bool heroAlive = false;
	float walked = 0.0f;     // how far the move order to the spot took it
	bool noted3804 = false;  // the runtime stops the game reported
	bool noted3807 = false;
	// lane ATTACK-1: every health loss of a member during the fight (its health before the frame, what it lost), what the members lost in the frames the
	// hero's melee machine ran its crush (0xE9, before or after the frame), and the hero at the order: MOUNTED, the hit sizes of its weapon and its
	// CrushWeapon against a member (the INI's Damage x the member's armour x the DamageScalars that take it)
	std::vector<std::pair<float, float>> drops;
	float lostInCrush = 0.0f;
	bool mounted = false;
	std::vector<float> hitSizes;
	float memberMaxHealth = 0.0f;
	// lane ATTACK-1 r2: `shots` (ObjectWeapons' counter) also counts the CrushWeapon's deliveries. `weaponShots`: the frames the hero's own weapon (the
	// current one at the order) fired (its lastFireFrame moved); `lostOnWeaponShot`: what the foe lost in the frames the weapon fired, outside the crush run;
	// `firstWeaponLoss`: the loss in the frame of the first weapon shot
	unsigned weaponShots = 0;
	float lostOnWeaponShot = 0.0f;
	float firstWeaponLoss = -1.0f;
	// the blocking case: the hero's live crusher level at the order (RW 0x695070), the frames its mover's blocked counter (AI + 0x16C) was above 0 and the
	// first of them, and how near the hero came to the horde object's centre
	int crusherLevel = -1;
	int blockedFrames = 0;
	int firstBlocked = -1;
	float nearestToHorde = 1.0e9f;
};

// lane ATTACK-1: the hits a weapon's DamageNuggets deal `victim` (Damage x its armour's coefficient for the DamageType x the DamageScalars whose filter takes it)
void addHitSizes(const WeaponTemplate *w, const Object &victim, const Player *attacker, std::vector<float> &out)
{
	REQUIRE(w);
	const ActiveBody *body = dynamic_cast<const ActiveBody *>(victim.getBodyModule());
	const ArmorTemplate *armor = body ? body->currentArmor() : nullptr;
	for (const std::shared_ptr<WeaponNugget> &n : w->m_nuggets)
	{
		if (const DamageNugget *d = dynamic_cast<const DamageNugget *>(n.get()))
		{
			float scalar = 1.0f;
			for (const DamageScalarEntry &e : d->m_damageScalar)
			{
				scalar *= ObjectFilterMatch::allows(victim.logic(), e.filter, victim, attacker) ? e.scalar : 1.0f;
			}
			out.push_back(d->m_damage * (armor ? armor->getCoefficient(d->m_damageType) : 1.0f) * scalar);
		}
	}
}

unsigned meleeSubState(Object *h)
{
	AIAttackState *as = h && h->getAIUpdateInterface() ? dynamic_cast<AIAttackState *>(h->getAIUpdateInterface()->stateMachine().currentState()) : nullptr;
	return as && as->attackMachine() ? as->attackMachine()->currentStateId() : 0u;
}

bool noted(GameLogic &logic, const char *id)
{
	for (const auto &kv : logic.notedStopHits())
	{
		if (kv.first.rfind(id, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

GameMessage selectMsg(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}

GameMessage queueHeroMsg(int player, int index)
{
	// MSG_QUEUE_UNIT_CREATE with the build-index flag (RW 0x77A764): the index of the player's hero record
	GameMessage m(MSG_QUEUE_UNIT_CREATE, player);
	m.appendBooleanArgument(true);
	m.appendIntegerArgument(index);
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(false);
	m.appendBooleanArgument(false);
	return m;
}

// a fresh game; the hero recruited from the fortress and walked to the spot between the bases; then the fight of the mode
Fight fight(starttest::Shared &s, const char *faction, const std::string &hero, bool ordered, const char *foe = "MordorFighterHorde", const char *modifier = nullptr)
{
	Fight out;
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(s, faction, "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	Player *enemy = live.players().findPlayerWithName(live.report().startSlotPlayers[1]);
	REQUIRE(player);
	REQUIRE(enemy);
	player->getMoney()->deposit(1000000, false);
	player->commandPoints().setFromScript(100000, 100000);
	const ThingTemplate *tt = logic.things().findTemplate(hero);
	REQUIRE(tt);
	tt = tt->getFinalOverride();
	int index = player->heroes().findIndex(*tt, 0xFFFFFFFFu, 0);
	if (index < 0)
	{
		// a ring hero: the spell book's BuildableHeroListUpgrade (Upgrade_RingHero, RW 0x8BC4C6) lists it
		Object *book = logic.findObjectByID(player->science().spellBookId());
		REQUIRE(book);
		book->giveUpgrade("Upgrade_RingHero");
		index = player->heroes().findIndex(*tt, 0xFFFFFFFFu, 0);
	}
	REQUIRE_MESSAGE(index >= 0, hero << " has no hero record");
	Object *fortress = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		fortress = (o && o->getControllingPlayer() == player) ? o : fortress;
	}
	REQUIRE(fortress);
	// the producer: the player's structure whose queue takes the hero (the fortress's citadel)
	const int pi = player->getPlayerIndex();
	Object *producer = nullptr;
	for (Object *o = logic.getFirstObject(); o && !producer; o = o->getNextObject())
	{
		ProductionUpdateInterface *pu = o->getControllingPlayer() == player && o->isKindOfName("STRUCTURE") ? o->getProductionUpdate() : nullptr;
		if (!pu)
		{
			continue;
		}
		live.commands().append(selectMsg(pi, o->getID()));
		live.commands().append(queueHeroMsg(pi, index));
		logic.runLogicFrame();
		producer = pu->firstProduction() != nullptr ? o : nullptr;
	}
	REQUIRE_MESSAGE(producer, hero << ": no structure of " << faction << " recruits it");
	const ObjectID producerId = producer->getID();
	Object *h = nullptr;
	for (int f = 1; f < 6000 && !h; ++f)
	{
		logic.runLogicFrame();
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getFinalOverride() == tt && o->getControllingPlayer() == player && !o->isEffectivelyDead())
			{
				h = o;
				out.recruitFrames = f;
			}
		}
	}
	REQUIRE_MESSAGE(h, hero << " was never made");
	out.recruited = h->getProducerID() == producerId;
	out.giantBird = h->isKindOfName("GIANT_BIRD");
	const ObjectID heroId = h->getID();
	const Coord3D made = *h->getPosition();
	// the spot between the bases, out of both fortresses' reach (Evendim: start 0 at the west, start 4 at the east)
	Coord3D spot = *fortress->getPosition();
	spot.x += 900.0f;
	spot.z = logic.getGroundHeight(spot.x, spot.y);
	h->getAIUpdateInterface()->aiMoveToPosition(spot, CMD_FROM_PLAYER);
	for (int f = 0; f < 1500; ++f)
	{
		logic.runLogicFrame();
		h = logic.findObjectByID(heroId);
		REQUIRE(h);
		const float dx = h->getPosition()->x - spot.x, dy = h->getPosition()->y - spot.y;
		if (f > 10 && std::sqrt(dx * dx + dy * dy) < 60.0f && !h->getAIUpdateInterface()->isMoving())
		{
			break;
		}
	}
	for (int f = 0; f < 15; ++f)
	{
		logic.runLogicFrame(); // the move's end (StancesBehavior onMoveEnd -> the guard, RW 0x86228E)
	}
	h = logic.findObjectByID(heroId);
	REQUIRE(h);
	out.walked = std::sqrt((h->getPosition()->x - made.x) * (h->getPosition()->x - made.x) + (h->getPosition()->y - made.y) * (h->getPosition()->y - made.y));
	if (ordered)
	{
		logic.combat().setAutoAcquireEnabled(false);
	}
	const Coord3D at = *h->getPosition();
	Object *horde = live.createObject(foe, enemy->getPlayerIndex(), Coord3D{ at.x + 100.0f, at.y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(horde, error);
	const ObjectID hordeId = horde->getID();
	logic.runLogicFrame();
	horde = logic.findObjectByID(hordeId);
	REQUIRE(horde);
	std::vector<ObjectID> members;
	if (horde->getContain() && horde->getContain()->getHordeContainInterface())
	{
		REQUIRE(horde->getContain()->getContainedItemsList());
		REQUIRE_FALSE(horde->getContain()->getContainedItemsList()->empty());
		for (Object *m : *horde->getContain()->getContainedItemsList())
		{
			members.push_back(m->getID());
		}
	}
	else
	{
		members.push_back(hordeId); // lane ATTACK-1 r2: a lone foe
	}
	out.hordeMembers = (unsigned)members.size();
	const float before = membersHealth(logic, hordeId);
	h = logic.findObjectByID(heroId);
	const unsigned long long shots0 = h && h->getWeapons() ? h->getWeapons()->stats().shotsFired : 0;
	if (modifier && h)
	{
		REQUIRE(h->addAttributeModifier(modifier, 0)); // lane ATTACK-1 r2: for good (duration 0)
	}
	if (ordered && h)
	{
		out.crusherLevel = ObjectCrush::crusherLevel(*h);
		// the player's order: the cursor's pick of a horde member passes to the horde (HudObjects.cpp, ZH addDrawableToList), whose nearest member the AI attacks
		// (aiAttackObject's horde victim, then the next member while the horde lives)
		h->getAIUpdateInterface()->aiAttackObject(logic.findObjectByID(hordeId), CMD_FROM_PLAYER);
		out.mounted = h->testModelCondition(CombatNames::modelCondition("MOUNTED"));
		Object *member = logic.findObjectByID(members.front());
		REQUIRE(member);
		out.memberMaxHealth = member->getBodyModule()->getMaxHealth();
		addHitSizes(h->getWeapons()->currentWeapon()->getTemplate(), *member, player, out.hitSizes);
		const std::string crush = CrushTemplateInfo::cached(*h).crushWeapon;
		if (!crush.empty())
		{
			addHitSizes(TheWeaponStore->findWeaponTemplate(crush), *member, player, out.hitSizes);
		}
	}
	for (int f = 0; f < 150; ++f)
	{
		std::vector<float> health;
		for (ObjectID id : members)
		{
			Object *m = logic.findObjectByID(id);
			health.push_back(m && !m->isEffectivelyDead() ? m->getBodyModule()->getHealth() : 0.0f);
		}
		const bool crushBefore = meleeSubState(logic.findObjectByID(heroId)) == MELEE_SQUISH;
		Object *hb = logic.findObjectByID(heroId);
		const Weapon *wb = ordered && hb && hb->getWeapons() ? hb->getWeapons()->currentWeapon() : nullptr;
		const std::uint32_t firedBefore = wb ? wb->lastFireFrame() : 0u;
		logic.runLogicFrame();
		const bool crushing = crushBefore || meleeSubState(logic.findObjectByID(heroId)) == MELEE_SQUISH;
		Object *ha = logic.findObjectByID(heroId);
		const Weapon *wa = ordered && ha && ha->getWeapons() ? ha->getWeapons()->currentWeapon() : nullptr;
		const bool weaponFired = wb && wa == wb && wa->lastFireFrame() != firedBefore;
		if (ha && ha->getAIUpdateInterface()->mover().blockedFrames() > 0)
		{
			++out.blockedFrames;
			out.firstBlocked = out.firstBlocked < 0 ? f : out.firstBlocked;
		}
		if (Object *hd = ha ? logic.findObjectByID(hordeId) : nullptr)
		{
			const float dx = ha->getPosition()->x - hd->getPosition()->x, dy = ha->getPosition()->y - hd->getPosition()->y;
			out.nearestToHorde = std::min(out.nearestToHorde, std::sqrt(dx * dx + dy * dy));
		}
		float frameLoss = 0.0f;
		for (size_t i = 0; i < members.size(); ++i)
		{
			Object *m = logic.findObjectByID(members[i]);
			const float now = m && !m->isEffectivelyDead() ? m->getBodyModule()->getHealth() : 0.0f;
			if (now < health[i])
			{
				out.drops.emplace_back(health[i], health[i] - now);
				out.lostInCrush += crushing ? health[i] - now : 0.0f;
				frameLoss += health[i] - now;
			}
		}
		if (weaponFired)
		{
			out.firstWeaponLoss = out.weaponShots == 0 ? frameLoss : out.firstWeaponLoss;
			++out.weaponShots;
			out.lostOnWeaponShot += crushing ? 0.0f : frameLoss;
		}
	}
	h = logic.findObjectByID(heroId);
	out.heroAlive = h && !h->isEffectivelyDead();
	out.shots = (h && h->getWeapons() ? h->getWeapons()->stats().shotsFired : shots0) - shots0;
	out.lost = before - membersHealth(logic, hordeId);
	out.noted3804 = noted(logic, "[S-3804]");
	out.noted3807 = noted(logic, "[S-3807]");
	for (ObjectID id : members)
	{
		Object *m = logic.findObjectByID(id);
		out.membersKilled += (!m || m->isEffectivelyDead()) ? 1u : 0u;
	}
	return out;
}
} // namespace

TEST_CASE("play4 heroes retail: every faction's heroes, recruited from the fortress, fight an enemy horde on their own and, auto-acquire off, on an order")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		const PlayerTemplate *pt = s->world->playerTemplates().findPlayerTemplate(faction);
		REQUIRE(pt);
		const std::vector<std::string> heroes = heroesOf(*pt);
		CHECK_FALSE(heroes.empty());
		for (const std::string &hero : heroes)
		{
			if (hero == "CreateAHero")
			{
				continue; // the Create-a-Hero placeholder: a player's own hero is built from its saved definition, not this template (S-852)
			}
			INFO(std::string(faction) << " " << hero);
			const Fight own = fight(*s, faction, hero, false);
			const Fight ord = fight(*s, faction, hero, true);
			MESSAGE(std::string(faction) << " " << hero << std::string(own.giantBird ? " (GIANT_BIRD)" : "") << ": recruited in " << own.recruitFrames << " frames; on its own " << own.shots
							<< " strikes, " << own.lost << " health taken, " << own.membersKilled << "/" << own.hordeMembers << " killed, walked " << own.walked << "; ordered " << ord.shots
							<< " strikes, " << ord.lost << " health taken, " << ord.membersKilled << "/" << ord.hordeMembers << " killed");
			CHECK(own.recruited);
			CHECK(ord.recruited);
			if (own.giantBird)
			{
				// lane FLY-1 closed S-3804 (the GIANT_BIRD mover and GiantBirdAIUpdate are ported, S-4040): the birds fly to the horde. Measured on the merged tree
				// (merge queue q7): on their own they strike nothing and take nothing; ordered, Khamul,
				// Morgomir and the Witch-king on his fell beast kill 5 members (450 health taken) and Drogoth 3 strikes, 1500 health, all 20
				CHECK_FALSE(own.noted3804);
				CHECK_FALSE(ord.noted3804);
				const bool drogoth = hero == "Drogoth";
				// merge queue q28 (measured once on the merged tree and at each merge point): on his own Drogoth now strikes the horde once (975 health, 13 of 20
				// killed); the fell beasts still strike nothing
				// merge queue q29 (measured once on the merged tree and at each merge point): on his own Drogoth strikes nothing again, like the fell beasts
				// (DEVPORT-SIM: one strike, 825 health, 11 killed; none from community #113 on, whose capture squads move the scene's computer opponent)
				CHECK(own.shots == 0u);
				CHECK(own.lost == 0.0f);
				CHECK(own.membersKilled == 0u);
				// lane BACKLOG4: 3 strikes (4 before): the hero's creation uses the teleport form (RW 0x7814CF: RW 0x696E63(pos, 0)), which idles the new
				// hero's AI and wakes it; the fight's damage and kills are unchanged (measured with and without that call)
				// lane STEALTHFLY1: 2 strikes (3 before): recruited from the fortress, Drogoth leaves along its production exit path in the bird's own follow
				// state 0x3F6 (privateFollowPath RW 0x76B491) instead of the ground path state, so the ordered fight starts from another place and time;
				// the damage and the kills are the same
				// merge queue q22 (SUMMON-1 + FX-4 + AI5 + NUGGET-1, measured on the merged tree on srv1): 4 strikes (2 before): the scene's computer opponent draws
				// AI5's upgrade-registration random values and SUMMON-1's OCL draws, which shift the ordered fight's timing; the damage and the kills are the same
				// lane TARGET-1: 4 strikes (3 before): the idle bird wakes at its next mood check (RW 0x7553AB) and the map's creeps' attack-moves push their
				// mood checks back (RW 0x662DD9), so the scene's world plays differently; the damage and kills are unchanged
				// lane OFFMAP-1: 3 strikes (2 before): RotWK's idle state (RW 0x745A64) no longer snaps the scene's units onto cell centres at frame <= 1
				// (ZH's cheat move), so the fight starts from the positions they were made at; the damage and the kills are the same
				CHECK(ord.shots == (drogoth ? 4u : 0u));
				CHECK(ord.lost == (drogoth ? 1500.0f : 450.0f));
				CHECK(ord.membersKilled == (drogoth ? 20u : 5u));
				continue;
			}
			CHECK(own.walked > 600.0f);
			// on its own: the Battle stance's guard attacks the horde (with the reach of RW 0x6ED843 every walking hero kills several members)
			CHECK(own.shots > 0u);
			// lane TARGET-1: Haldir's guard (RotWK's search, RW 0x949CA7 -> 0x70146A) took a creep first: from the spot, before the horde is made, a
			// MordorGoblinSwordsman_Slaved of the Moria goblin lair about 550 to the south-west, then the lair itself, so his strikes in the window never
			// reached the horde (measured on a CI host). Community #74 (the destination adjustment spiralling around the cell, RW 0x6FA2A4, with #73's centred
			// formations) on q23's tree: he fought the horde (14 of 20 killed). Merge queue q26 (with OFFMAP-1's start positions and the rest, measured once on
			// the merged tree): he takes the creep first again; every other hero fights the horde. Merge queue q27 (LOS-1's terrain line of sight in the
			// target search, measured at each merge point): the creep behind the hill is out of his sight, and he fights the horde (8 of 20 killed) like
			// every other hero
			// Merge queue q28 (measured once on the merged tree and at each merge point): Haldir's guard takes the creep first again (1 strike, nothing taken from
			// the horde); every other hero fights the horde
			// Merge queue q29 (measured once on the merged tree and at each merge point): Haldir fights the horde again from DEVPORT-SIM on (a hero leaving the
			// fortress drops IS_LEAVING_FACTORY, RW 0x74B77D, and takes its Battle stance's guard), all 20 killed (1500 health) on the merged tree
			if (hero == "ElvenHaldir")
			{
				CHECK(own.lost == 1500.0f);
			}
			else
			{
				CHECK(own.lost > 0.0f); // tolerance: the pre-existing "the guard attacks the horde" bound
			}
			CHECK_FALSE(own.noted3804);
			// on an order (auto-acquire off): the order alone strikes and kills. Lane ATTACK-1 closed S-3807 (its two named exceptions, the mounted Witch-king and
			// Mouth of Sauron, were the port's static crush test in blockedBy: the next test); its runtime note is gone
			CHECK_FALSE(ord.noted3807);
			CHECK(ord.shots > 0u);
			CHECK(ord.lost > 0.0f);
		}
	}
}

// lane PLAY-4 r1: RW 0x6CC07C's melee branch for a moving victim (an AI with a current locomotor, RW 0x6CC0CE) is RW 0x6ED843's footprint test. Aragorn (PathfindDiameter
// 40: 4 cells, not centred) against a lone MordorFighter (radius 8: diameter 16 -> 20 -> 2 cells), STANDARD_MELEE_ATTACK_RANGE 11.5 -> floor(11.5 * 0.1 + 0.1) = 1
// cell: the unit box [c - 2 - 1, c - 2 + 4 + 1] and the target box [t - 1, t - 1 + 2] overlap while |t - c| <= 4 cells. Both on cell centres of one row
// (x = 10 * cell: not centred, RW 0x6E8CE6 adds 0.5 before the floor): 40 apart is in reach, 50 is not (the S-320 edge distance it replaced reached 11.5 + 8 + 8 = 27.5).
// The reach is tested from the source position RW passes (the engagement search's candidate cell), not from where the unit stands.
TEST_CASE("play4 heroes retail: a melee weapon reaches a moving unit by the footprints (RW 0x6ED843), from the position it is asked about")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	Player *enemy = live.players().findPlayerWithName(live.report().startSlotPlayers[1]);
	REQUIRE(player);
	REQUIRE(enemy);
	const float y = 4500.0f;
	Object *hero = live.createObject("GondorAragornMP", player->getPlayerIndex(), Coord3D{ 1500.0f, y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	Object *orc = live.createObject("MordorFighter", enemy->getPlayerIndex(), Coord3D{ 1540.0f, y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(orc, error);
	auto put = [&](Object *o, float x) {
		Coord3D p{ x, y, logic.getGroundHeight(x, y) };
		o->setPosition(&p);
	};
	ObjectWeapons *w = hero->getWeapons();
	REQUIRE(w);
	REQUIRE(w->currentWeapon());
	REQUIRE(w->currentWeapon()->getTemplate()->m_meleeWeapon);
	REQUIRE(orc->getAIUpdateInterface());
	REQUIRE(orc->getAIUpdateInterface()->curLocomotor());
	put(hero, 1500.0f);
	put(orc, 1540.0f);
	CHECK(w->isWithinAttackRange(*orc, 0.0f, false)); // 4 cells: the boxes touch
	put(orc, 1550.0f);
	CHECK_FALSE(w->isWithinAttackRange(*orc, 0.0f, false)); // 5 cells
	put(orc, 1460.0f);
	CHECK(w->isWithinAttackRange(*orc, 0.0f, false)); // 4 cells on the other side
	// asked from another position (the engagement search's candidate): the reach is the candidate's
	put(orc, 1540.0f);
	put(hero, 2000.0f);
	CHECK_FALSE(w->isWithinAttackRange(*orc, 0.0f, false));
	CHECK(w->isWithinAttackRangeFrom(Coord3D{ 1500.0f, y, logic.getGroundHeight(1500.0f, y) }, orc, *orc->getPosition(), 0.0f, false));
	// a SIEGE_TOWER outside the boxes takes RW's ramp branch (RW 0x6EDB6F), not ported: false and stop S-3806 reported
	Object *tower = live.createObject("MordorSiegeTower", enemy->getPlayerIndex(), Coord3D{ 1700.0f, y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(tower, error);
	put(hero, 1500.0f);
	put(tower, 1700.0f);
	CHECK_FALSE(w->isWithinAttackRange(*tower, 0.0f, false));
	bool noted = false;
	for (const auto &kv : logic.notedStopHits())
	{
		noted = noted || kv.first == meleeReachStopLine();
	}
	CHECK(noted);
}

// lane ATTACK-1 (S-3807 closed): the Angmar Witch-king and the Mouth of Sauron are recruited MOUNTED; their crusher level is then MountedCrusherLevel 1 (RW 0x695070)
// above the MordorFighterHorde's CrushableLevel 0, while their templates' CrusherLevel is 0. RW's blockedBy (RW 0x66D16E) lets a unit pass an enemy it may crush
// (RW 0x69519A(other, 2) with the unit as the crusher, RW 0x66D1A2 .. 0x66D1B0); the port asked the templates' static CrusherLevel, so the horde object (it
// collides: its members do not, RW 0x66E233) blocked the hero's crush run (0xE9) in front of it: the run went round the members, no crush landed, and
// 0xE1's computePath refused the blocked hero (RW 0x746DB6, AI + 0x16C > 0), which ended the order before a strike. Now, ordered onto the horde with the
// auto-acquire off (only the order acts, the horde does not fight back): the run kills members with the CrushWeapon, then the weapon strikes; every
// member's loss is a whole hit of the hero's weapon or CrushWeapon (Weapon.ini x Armor.ini; each is more than a member's health, so the hit member dies)
TEST_CASE("attack1 heroes retail: the mounted Witch-king and Mouth of Sauron, ordered onto a horde, crush through it and strike: S-3807 closed")
{
	OPENBFME_REQUIRE_START(s);
	struct Case
	{
		const char *faction, *hero;
		unsigned shots, killed;
		float lostInCrush;
		std::vector<float> hits; // the weapon's hit, then the CrushWeapon's, against a MordorFighter (Weapon.ini x Armor.ini)
		// r2: the hero's own weapon's swings among `shots` (the rest are CrushWeapon deliveries), what the first swing took, what all swings took outside the run
		unsigned weaponShots;
		float firstWeaponLoss, lostOnWeaponShot;
		// r2, the duel with a lone MordorMountainTroll (1500 health, CrushableLevel 2: no crush): its health before and its loss at each of the hero's swings
		std::vector<std::pair<float, float>> duel;
		// r2, the same order with GlorfindelWindRider (CRUSHER_LEVEL -1) on the hero: frames blocked and the first, swings, health taken, the nearest it came
		int loweredBlocked, loweredFirstBlocked;
		unsigned loweredShots;
		float loweredLost, loweredNearest, controlNearest;
	};
	// The strike, kill, blocked-frame and distance totals are MEASURED ENGINE PINS of this scene (lane ATTACK-1, the archive ad9bec05 merged), not retail
	// values; the hit sizes are the INI's: the Witch-king's mace 400 x HERO 200% = 800 and KnightCrush 80 x CRUSH 140% = 112, the Mouth of Sauron's sword
	// 320 x 200% = 640 and HeroCrush 160 x 200% = 320 against a MordorFighter (75 health each, 20 members); against the troll's armour (HERO 200%) 800 and 640
	// Merge queue q12 (the q11 ATTACK-1 / BACKLOG-1 pins against this batch, measured on the merged tree on a CI host; the lane that moved them was not separated): the swing, kill, blocked and distance
	// pins moved. The CRUSHER_LEVEL -1 case is no longer blocked (blocked 0 frames; the Witch-king makes no swing at all and stays 69.9 away, the Mouth of Sauron swings once
	// from 41.3): a change of behaviour to review, not only of numbers.
	// Lane AI5: the scene's computer opponent (Mordor) now registers its units' upgrade buttons, one GetGameLogicRandomValue each (AIUnitUpgrader.cpp
	// 0x5D), which shifts the random sequence of the Witch-king's fight: 7 shots, 18 killed (300 in the crush run), 3 swings (the first took 525, 1050
	// outside the run), the control's nearest 0.381908, the lowered case's 70.444. With only that registration switched off the old pins (5 / 12 / 225, 2 /
	// 450 / 675, 0.789662 / 69.9072) come back exactly; the Mouth of Sauron's case does not move. Engine pins, not retail values.
	// Lane ATTACKPATH-1: the pins moved again, all through one retail rule: the towards-goal line of the path search now adds the hierarchical corridor's remaining
	// length to its cells' estimate as RotWK does (RW 0x6F6D57 with its record + 0x34), so the crush run's path takes another line; with that rule alone switched
	// off (an experiment build of the lane) every value below was the q12 pin again. The CRUSHER_LEVEL -1 case is blocked again (10 frames from frame 37 / 30) and
	// both heroes swing once from about 40 away.
	// lane FLANK-1 (the crush run's computePath RW 0x742271: refused while blocked, a forced re-path without a path, the goal's z kept; onEnter's destroyed path
	// RW 0x66276B; the crush contact RW 0xAD2CE0 with the heights; the flank test at fire time): measured engine pins of the same scene: the Witch-king 7 shots
	// (5 before), 17 of 20 killed (12), 3 swings of his mace (2), the first taking 375 (450), 975 outside the run (675), nearest 5.97199 (0.789662); the Mouth of
	// Sauron 8 shots (11), 8 killed (11), 4 swings (7), 300 outside the run (525), nearest 9.96728 (20.1465). The CRUSHER_LEVEL -1 runs and the duels are unchanged
	// Merge queue q23 (TARGET-1's mood scan cadence, ATTACKPATH-1's towards-goal corridor term and FLANK-1's crush run computePath / contact, measured once on
	// the merged tree; each lane alone measured its values above): the Witch-king 5 shots, 14 killed (225 in the crush run), 2 swings (the first took 525, 825
	// outside the run), nearest 1.26859, with CRUSHER_LEVEL -1 never blocked, one swing taking 450 from 40.8519; the Mouth of Sauron 7 shots, 7 killed (150), 5
	// swings (75 / 375), nearest 10.1093, with CRUSHER_LEVEL -1 one swing taking 75 from 41.3165. The duels are unchanged. Engine pins, not retail values.
	// Lane OFFMAP-1 (RotWK's idle state RW 0x745A64 has no ZH "frame <= 1: snap onto the goal cell centre", so the scene's hero and horde start where they
	// were made): re-measured. Witch-king: 6 shots, 20 killed, 3 swings (first 525, 1275 outside the run), nearest 4.91667; Mouth of Sauron: 8 shots,
	// 8 killed, 4 swings (300 outside the run), nearest 6.20071, and the CRUSHER_LEVEL -1 case is blocked again (10 frames, first at 27, nearest 41.8102):
	// the behaviour the r2 description above names. The crush runs' losses, the hit sizes and the duels are unchanged
	// Merge queue q24 (OFFMAP-1: RotWK's idle state RW 0x745A64 has no ZH frame <= 1 snap, so the scene's hero and horde start where they were made; measured
	// once on the merged tree; without OFFMAP-1 the merged tree keeps q23's values): the Witch-king 6 shots, 16 killed (225 in the crush run), 3 swings (the
	// first took 375, 975 outside the run), nearest 0.565623, with CRUSHER_LEVEL -1 no swing (nearest 70.444, as OFFMAP-1's lane measured 69.9); the Mouth of
	// Sauron 15 shots, 15 killed (150), 12 swings (75 / 900), nearest 10.3192, with CRUSHER_LEVEL -1 blocked again (10 frames from frame 27, nearest 51.8487,
	// one swing taking 75). The duels are unchanged. Engine pins, not retail values.
	// Merge queue q26 (community #73's slot tables centred on their hordes and #74's spiral on the archive's OFFMAP-1 start positions, with BANNER-1, SIEGE-1 and
	// the rest; measured once on the merged tree): no CrushWeapon delivery lands in either run and the CRUSHER_LEVEL -1 heroes are blocked again, as #73 /
	// #74's own port measured: the Witch-king 4 swings (the first took 525, 1125 outside the run), 15 killed, nearest 35.1055, with CRUSHER_LEVEL -1 blocked
	// 10 frames from frame 30, one swing taking 525 from 50.9059; the Mouth of Sauron 8 swings (75 / 600), 8 killed, nearest 24.9008, with CRUSHER_LEVEL -1
	// blocked 18 frames from frame 21, 5 swings taking 375 from 60.1409. The duels are unchanged. Engine pins, not retail values.
	// Merge queue q28 (#96 / #97 / #98, #117's fortress module in the update schedule, #124 and the archive's plain batch; measured once on the merged tree
	// and at each merge point): the Witch-king 6 shots, 18 killed (225 in the crush run), 3 swings (the first took 450, 1125 outside the run), nearest 5.53305,
	// with CRUSHER_LEVEL -1 blocked 10 frames from frame 30, one swing taking 600 from 50.9059; the Mouth of Sauron 10 swings (75 / 750), 10 killed, nearest
	// 40.9154, with CRUSHER_LEVEL -1 blocked 18 frames from frame 7, 5 swings taking 375 from 69.7602. The duels are unchanged. Engine pins, not retail values.
	// Merge queue q29 (measured once on the merged tree and at each merge point): PATHPRIO-1's onCollide attack-target exits (RW 0x66E2D1 .. 0x66E309 with
	// RW 0x68B36F) end the CRUSHER_LEVEL -1 blocking (0 frames from its merge point on, as its lane measured); the numbers then move with FBSIM-1 (the scene's
	// computer opponent builds phantom sites): the Witch-king 4 shots, all swings of his mace (the first took 450, 1200 outside the run, nothing in a crush run),
	// 16 killed, nearest 29.9244, with CRUSHER_LEVEL -1 4 swings taking 1200 from 29.5534; the Mouth of Sauron 6 shots, 6 killed (300 in the crush run), 2
	// swings (75 / 150), nearest 1.00478, with CRUSHER_LEVEL -1 5 swings taking 375 from 59.6801. The duels are unchanged. Engine pins, not retail values.
	for (const Case &c : { Case{ "FactionAngmar", "AngmarWitchking", 4u, 16u, 0.0f, { 800.0f, 112.0f }, 4u, 450.0f, 1200.0f, { { 1500.0f, 800.0f }, { 700.0f, 700.0f } }, 0, -1, 4u, 1200.0f, 29.5534f, 29.9244f },
			 Case{ "FactionMordor", "MordorMouthOfSauron", 6u, 6u, 300.0f, { 640.0f, 320.0f }, 2u, 75.0f, 150.0f, { { 1500.0f, 640.0f }, { 860.0f, 640.0f }, { 220.0f, 220.0f } }, 0, -1, 5u, 375.0f, 59.6801f, 1.00478f } })
	{
		INFO(std::string(c.hero));
		const ThingTemplate *tt = s->world->things().findTemplate(c.hero);
		REQUIRE(tt);
		const CrushTemplateInfo crush = CrushTemplateInfo::of(*tt);
		CHECK(crush.crusherLevel == 0);
		CHECK(crush.mountedCrusherLevel == 1);
		const Fight ord = fight(*s, c.faction, c.hero, true);
		std::string drops = "; hit sizes";
		for (float h : ord.hitSizes)
		{
			drops += " " + std::to_string(h);
		}
		drops += "; losses";
		for (const auto &d : ord.drops)
		{
			drops += " " + std::to_string(d.first) + "->" + std::to_string(d.second);
		}
		MESSAGE(std::string(c.hero) << ": mounted " << ord.mounted << ", ordered " << ord.shots << " shots, " << ord.lost << " health taken (" << ord.lostInCrush << " in the crush run), "
						<< ord.membersKilled << "/" << ord.hordeMembers << " killed; " << ord.weaponShots << " swings of the weapon (the first took " << ord.firstWeaponLoss << ", all "
						<< ord.lostOnWeaponShot << " outside the run), the other shots are CrushWeapon deliveries" << drops);
		// r2, a normal weapon hit and its damage. `shots` counts the CrushWeapon's deliveries too: the mace / sword swung `weaponShots` times, and the frame of
		// the first swing took the members within its reach (the mace's Radius 30: three of them; the sword: one)
		CHECK(ord.weaponShots == c.weaponShots);
		CHECK(ord.firstWeaponLoss == c.firstWeaponLoss);
		CHECK(ord.lostOnWeaponShot == c.lostOnWeaponShot);
		// r2, the unsaturated hit: the same order onto a lone MordorMountainTroll the hero cannot crush (CrushableLevel 2) and whose 1500 health outlasts a
		// hit: no CrushWeapon delivery, every loss falls in the frame of a swing and is the weapon's hit against the troll's armour (the last: what was left)
		const Fight duel = fight(*s, c.faction, c.hero, true, "MordorMountainTroll");
		CHECK(duel.memberMaxHealth == 1500.0f);
		REQUIRE_FALSE(duel.hitSizes.empty());
		CHECK(duel.hitSizes[0] == c.hits[0]);
		CHECK(duel.shots == (unsigned long long)c.duel.size());
		CHECK(duel.weaponShots == (unsigned)c.duel.size());
		CHECK(duel.lostInCrush == 0.0f);
		CHECK(duel.drops == c.duel);
		CHECK(duel.firstWeaponLoss == duel.hitSizes[0]);
		CHECK(duel.lostOnWeaponShot == 1500.0f);
		// r2, the negative case through blockedBy (RW 0x66D16E): the same order with the attribute modifier GlorfindelWindRider (CRUSHER_LEVEL -1) on the hero.
		// Its live crusher level (RW 0x695070) is 0, so RW 0x69519A refuses the crush: no crush run (nothing is lost in one, the hero stays at the horde's
		// edge instead of riding through its centre), the horde blocks the Mouth of Sauron (the Witch-king's counter is never above 0 at a frame's end), and
		// the order ends after one swing. The control is `ord` above: crusher level 1, never blocked
		const Fight low = fight(*s, c.faction, c.hero, true, "MordorFighterHorde", "GlorfindelWindRider");
		MESSAGE(std::string(c.hero) << " with CRUSHER_LEVEL -1: crusher level " << low.crusherLevel << ", blocked in " << low.blockedFrames << " frames (first " << low.firstBlocked << "), nearest "
									<< low.nearestToHorde << " (control " << ord.nearestToHorde << "), " << low.weaponShots << " swings of " << low.shots << " shots, " << low.lost << " health taken");
		CHECK(ord.crusherLevel == 1);
		CHECK(ord.blockedFrames == 0);
		CHECK(ord.nearestToHorde == doctest::Approx(c.controlNearest).epsilon(0.0001)); // tolerance: the pin is the printed 6 digits of a float
		CHECK(low.crusherLevel == 0);
		CHECK(low.blockedFrames == c.loweredBlocked);
		CHECK(low.firstBlocked == c.loweredFirstBlocked);
		CHECK(low.nearestToHorde == doctest::Approx(c.loweredNearest).epsilon(0.0001)); // tolerance: the pin is the printed 6 digits of a float
		CHECK(low.lostInCrush == 0.0f);
		CHECK(low.shots == (unsigned long long)c.loweredShots);
		CHECK(low.weaponShots == c.loweredShots);
		CHECK(low.lost == c.loweredLost);
		CHECK(ord.mounted);
		CHECK(ord.memberMaxHealth == 75.0f);
		// each hit is bigger than a member's 75 health, so every loss is a whole member: its full health
		CHECK(ord.hitSizes == c.hits);
		CHECK(ord.drops.size() == c.killed);
		for (const auto &d : ord.drops)
		{
			CHECK(d.second == d.first);
			CHECK(d.first == ord.memberMaxHealth);
		}
		CHECK(ord.shots == c.shots);
		CHECK(ord.membersKilled == c.killed);
		CHECK(ord.lost == 75.0f * (float)c.killed);
		CHECK(ord.lostInCrush == c.lostInCrush);
		CHECK(ord.heroAlive);
	}
}
