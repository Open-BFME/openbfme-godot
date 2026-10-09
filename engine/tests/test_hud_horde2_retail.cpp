// OpenBFME retail tests for HORDE-2 (horde combat as in RotWK 2.01): a Rohirrim charge into orcs (the crush), pikemen against cavalry, a flank attack against a frontal one,
// hordes re-forming after a melee, two-run determinism and hash mutation. They share the HUD tests' retail world (the file name sorts with the test_hud_* files, see
// test_hud_combat_retail.cpp) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("horde2 retail");
}

// two enemy sides on a flat arena with the retail economy and AI data (the COMBAT-1 arena)
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	Arena(SharedWorld &s, const char *factionA, const char *factionB, unsigned seed = 7)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", factionA, true, 0, 0, 1 });
		setup.players.push_back({ "B", factionB, false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(seed);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y, float angle)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(angle);
		return o;
	}
	std::vector<ObjectID> memberIds(Object *horde)
	{
		std::vector<ObjectID> out;
		for (const Object *m : *horde->getContain()->getContainedItemsList())
		{
			out.push_back(m->getID());
		}
		return out;
	}
	size_t alive(const std::vector<ObjectID> &ids)
	{
		size_t n = 0;
		for (ObjectID id : ids)
		{
			const Object *o = logic.findObjectByID(id);
			n += (o && !o->isEffectivelyDead()) ? 1u : 0u;
		}
		return n;
	}
	float health(const std::vector<ObjectID> &ids)
	{
		float h = 0.0f;
		for (ObjectID id : ids)
		{
			const Object *o = logic.findObjectByID(id);
			h += (o && !o->isEffectivelyDead()) ? o->getBodyModule()->getHealth() : 0.0f;
		}
		return h;
	}
	CombatState::Counters &counters() { return logic.combat().counters(); }
};

HordeContain *hordeContainOf(Object *h)
{
	return dynamic_cast<HordeContain *>(h->findModule("HordeContain")) ? dynamic_cast<HordeContain *>(h->findModule("HordeContain"))
	                                                                    : dynamic_cast<HordeContain *>(h->findModule("HorseHordeContain"));
}

// the outcome of a charge: the per-frame crush counts and deaths of the charged horde
struct Charge
{
	std::vector<std::uint32_t> hashes;
	std::vector<int> crushesPerFrame;
	std::vector<int> deathsPerFrame;
	unsigned long long crushes = 0, crushShots = 0, decelerations = 0, flanks = 0;
	size_t victimsStart = 0, victimsEnd = 0, chargersStart = 0, chargersEnd = 0;
	float chargerHealthStart = 0.0f, chargerHealthEnd = 0.0f;
	int firstCrushFrame = -1;
};

// `charger` (side 0, facing east at x = 400) is ordered onto `victim` (side 1 at x = 600). `victimAngle` is the victim horde's facing (pi: it faces the charge; 0: it
// shows its back). The idle scan of the victim side acquires the charger on its own.
Charge charge(const char *factionA, const char *charger, const char *factionB, const char *victim, float victimAngle, int frames, unsigned seed = 7)
{
	Charge out;
	Arena a(shared(), factionA, factionB, seed);
	Object *h0 = a.place(charger, 0, 400.0f, 600.0f, 0.0f);
	Object *h1 = a.place(victim, 1, 600.0f, 600.0f, victimAngle);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	const std::vector<ObjectID> m0 = a.memberIds(h0), m1 = a.memberIds(h1);
	out.chargersStart = a.alive(m0);
	out.victimsStart = a.alive(m1);
	out.chargerHealthStart = a.health(m0);
	REQUIRE(h0->getAIUpdateInterface()->aiAttackObject(h1, CMD_FROM_PLAYER));
	size_t lastAlive = out.victimsStart;
	unsigned long long lastCrushes = a.counters().crushes;
	for (int f = 0; f < frames; ++f)
	{
		a.logic.runLogicFrame();
		const unsigned long long c = a.counters().crushes;
		out.crushesPerFrame.push_back((int)(c - lastCrushes));
		if (c > lastCrushes && out.firstCrushFrame < 0)
		{
			out.firstCrushFrame = f;
		}
		lastCrushes = c;
		const size_t alive = a.alive(m1);
		out.deathsPerFrame.push_back((int)(lastAlive - alive));
		lastAlive = alive;
		out.hashes.push_back(a.logic.computeStateHash());
	}
	out.crushes = a.counters().crushes;
	out.crushShots = a.counters().crushWeaponShots;
	out.decelerations = a.counters().crushDecelerations;
	out.flanks = a.counters().flanks;
	out.victimsEnd = a.alive(m1);
	out.chargersEnd = a.alive(m0);
	out.chargerHealthEnd = a.health(m0);
	return out;
}
} // namespace

// RotWK 2.01 data: RohanRohirrimHorde (HorseHordeContain, 5 RohanRohirrim) charges MordorFighterHorde (20 orcs).
//   member RohanRohirrim: CrusherLevel 1, CrushWeapon RohirrimCrush (DamageNugget ROHIRRIM_CRUSH_DAMAGE CRUSH / CRUSHED, FlankingBonus 200%), SquishCollide;
//   member MordorFighter: CrushableLevel 0, CrushRevengeWeapon BasicInfantryCrushRevenge, SquishCollide;
//   the horde RohanRohirrimHorde: CrusherLevel 1 (so its attack starts with the Squish drive-through), MinCrushVelocityPercent 50%, CrushDecelerationPercent 30%.
TEST_CASE("horde2 retail: a Rohirrim charge into an orc horde tramples orcs: crushes, crush weapon hits and deaths per frame, the horde slows down")
{
	if (!haveWorld())
	{
		return;
	}
	const Charge c = charge("FactionMen", "RohanRohirrimHorde", "FactionMordor", "MordorFighterHorde", 3.14159265f, 300);
	REQUIRE(c.chargersStart == 10); // InitialPayload RohanRohirrim GOOD_RIDER_HORDE_SIZE
	REQUIRE(c.victimsStart == 20);
	CHECK(c.crushes > 0);
	CHECK(c.firstCrushFrame >= 0);
	CHECK(c.crushShots >= c.crushes); // every crush fires the rider's CrushWeapon (and the orc's revenge weapon when the orc was not flanked)
	CHECK(c.decelerations > 0);       // CrushDecelerationPercent 30%: the horde's locomotor slowed at each crush
	int dead = 0;
	std::string perFrame;
	for (size_t f = 0; f < c.deathsPerFrame.size(); ++f)
	{
		dead += c.deathsPerFrame[f];
		if (c.crushesPerFrame[f] || c.deathsPerFrame[f])
		{
			perFrame += " f" + std::to_string(f) + ":" + std::to_string(c.crushesPerFrame[f]) + "c/" + std::to_string(c.deathsPerFrame[f]) + "d";
		}
	}
	CHECK(dead > 0);
	std::printf("  info: Rohirrim charge: first crush at frame %d, %llu crushes, %llu crush weapon shots, %llu decelerations, orcs %zu -> %zu, riders %zu -> %zu (health %.0f -> %.0f);"
		" per frame (crushes / deaths):%s\n",
		c.firstCrushFrame, c.crushes, c.crushShots, c.decelerations, c.victimsStart, c.victimsEnd, c.chargersStart, c.chargersEnd, c.chargerHealthStart, c.chargerHealthEnd,
		perFrame.substr(0, 600).c_str());
}

// GondorTowerShieldGuardHorde (pikemen, 15 men): member GondorTowerShieldGuard CrushableLevel 0, CrushRevengeWeapon SuperInfantryCrushRevenge; FrontAngle 180. A Rohirrim
// charge into their front is answered by the revenge weapon of every pikeman crushed (they are not flanked), so the riders lose health they do not lose against orcs from behind.
TEST_CASE("horde2 retail: pikemen facing a cavalry charge hurt the riders with their crush revenge weapon")
{
	if (!haveWorld())
	{
		return;
	}
	const Charge front = charge("FactionMen", "RohanRohirrimHorde", "FactionMen", "GondorTowerShieldGuardHorde", 3.14159265f, 200);
	REQUIRE(front.chargersStart == 10);
	CHECK(front.crushes > 0);
	CHECK(front.chargerHealthEnd < front.chargerHealthStart);
	std::printf("  info: Rohirrim into pikemen (front): %llu crushes, %llu crush / revenge shots, riders' health %.0f -> %.0f, pikemen %zu -> %zu\n", front.crushes, front.crushShots,
		front.chargerHealthStart, front.chargerHealthEnd, front.victimsStart, front.victimsEnd);
}

// flanking: the same Gondor fighter horde attacks the same orc horde once from the orcs' front and once from behind. GondorSword's DamageNugget has FlankingBonus 50%,
// MordorFighterHorde FrontAngle 270 (a 135 degree half angle): from behind the orc horde is flanked, its orcs take 1.5x sword damage.
TEST_CASE("horde2 retail: a flank attack does more damage than a frontal one (FlankingBonus) and the flanked horde can no longer flank")
{
	if (!haveWorld())
	{
		return;
	}
	// the first health step of every orc: a frontal sword hit takes 40 (GondorSword 40 SLASH, MordorOrcArmor SLASH 100%), a flanking one 40 * (1 + 50%) = 60
	// the orc horde object stands so that its nearest rank is about 20 in front of the Gondor front rank either way (its ranks lie at X 50 .. -10 of the horde object)
	auto fight = [&](float orcAngle, float orcX, unsigned long long &flanks, std::vector<float> &firstSteps, int frames) {
		Arena a(shared(), "FactionMen", "FactionMordor");
		a.logic.combat().setAutoAcquireEnabled(false); // the orcs stand still: only the attacker's side matters
		Object *g = a.place("GondorFighterHorde", 0, 400.0f, 600.0f, 0.0f);
		Object *o = a.place("MordorFighterHorde", 1, orcX, 600.0f, orcAngle);
		const ObjectID orcHordeId = o->getID();
		a.logic.runLogicFrame();
		a.logic.runLogicFrame();
		const std::vector<ObjectID> orcs = a.memberIds(o);
		std::vector<float> last;
		for (ObjectID id : orcs)
		{
			last.push_back(a.logic.findObjectByID(id)->getBodyModule()->getHealth());
		}
		std::vector<bool> seen(orcs.size(), false);
		REQUIRE(g->getAIUpdateInterface()->aiAttackObject(o, CMD_FROM_PLAYER));
		for (int f = 0; f < frames; ++f)
		{
			a.logic.runLogicFrame();
			for (size_t i = 0; i < orcs.size(); ++i)
			{
				const Object *m = a.logic.findObjectByID(orcs[i]);
				const float h = (m && !m->isEffectivelyDead()) ? m->getBodyModule()->getHealth() : 0.0f;
				if (!seen[i] && h < last[i])
				{
					seen[i] = true;
					firstSteps.push_back(last[i] - h);
				}
				last[i] = h;
			}
		}
		flanks = a.counters().flanks;
		// lane SMOOTH-3: look the orc horde up by id (a horde whose members all died is gone: the pointer would dangle)
		Object *orcHorde = a.logic.findObjectByID(orcHordeId);
		REQUIRE_MESSAGE(orcHorde != nullptr, "the orc horde was destroyed within " << frames << " frames");
		HordeContain *hc = hordeContainOf(orcHorde);
		REQUIRE(hc != nullptr);
		if (flanks > 0)
		{
			CHECK_FALSE(hc->canFlank()); // RW 0x876FC4 step 5: a flanked horde's H+0x2E8 is cleared
		}
		CHECK(hc->flankHistory().size() == 10u); // FlankedDelay 2000 ms = 10 frames of orientation history
	};
	unsigned long long frontFlanks = 0, backFlanks = 0;
	std::vector<float> front, back;
	// 70 frames (lane SMOOTH-3: with RotWK's member hub the Gondor horde destroys the passive orc horde from behind before frame 120; the first hits come well before 70)
	// lane AUDIO-4: 60 frames (LargeGroupAudioUpdate's logic random draws, RW 0x8AEEC3, move the fight: at 70 the orc horde is gone from behind; with the module
	// unregistered 70 holds again; at 60 both fights have their first hits and the orc horde stands)
	fight(3.14159265f, 520.0f, frontFlanks, front, 60);
	fight(0.0f, 480.0f, backFlanks, back, 60);
	CHECK(frontFlanks == 0);
	CHECK(backFlanks > 0);
	REQUIRE_FALSE(front.empty());
	REQUIRE_FALSE(back.empty());
	size_t front60 = 0, back60 = 0, front40 = 0;
	for (float s : front)
	{
		front60 += s == 60.0f ? 1u : 0u;
		front40 += s == 40.0f ? 1u : 0u;
	}
	for (float s : back)
	{
		back60 += s == 60.0f ? 1u : 0u;
	}
	CHECK(front60 == 0);
	CHECK(front40 > 0);
	CHECK(back60 > 0);
	std::printf("  info: flank: Gondor fighters on orcs, first hits: front %zu orcs (%zu of 40, %zu of 60, %llu flank answers), behind %zu orcs (%zu of 60, %llu flank answers)\n",
		front.size(), front40, front60, frontFlanks, back.size(), back60, backFlanks);
}

// re-forming: after a melee the survivors walk back to their slots (RW hub 0x87468B has no B1 "stay on your own goal cell" block): the worst slot error shrinks.
TEST_CASE("horde2 retail: after a melee the winner's survivors walk back toward their formation slots")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *g = a.place("GondorFighterHorde", 0, 400.0f, 600.0f, 0.0f);
	Object *o = a.place("MordorFighterHorde", 1, 540.0f, 600.0f, 3.14159265f);
	const ObjectID gid = g->getID();
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	REQUIRE(g->getAIUpdateInterface()->aiAttackObject(o, CMD_FROM_PLAYER));
	const ObjectID oid = o->getID();
	int f = 0;
	unsigned long long refreshes = 0;
	for (; f < 3000 && a.logic.findObjectByID(oid) && a.logic.findObjectByID(gid); ++f)
	{
		a.logic.runLogicFrame();
		if (HordeAIUpdate *hai = a.logic.findObjectByID(gid) ? dynamic_cast<HordeAIUpdate *>(a.logic.findObjectByID(gid)->getAIUpdateInterface()) : nullptr)
		{
			refreshes = hai->contactRefreshes();
		}
	}
	CHECK(refreshes > 0); // HordeMemberCollide (RW 0x8C0518): the members' contact with the orcs refreshed the melee readiness
	Object *winner = a.logic.findObjectByID(gid) ? a.logic.findObjectByID(gid) : a.logic.findObjectByID(oid);
	REQUIRE(winner != nullptr);
	HordeContain *hc = hordeContainOf(winner);
	REQUIRE(hc != nullptr);
	const float early = hc->worstMemberSlotError();
	for (int i = 0; i < 150; ++i)
	{
		a.logic.runLogicFrame();
	}
	const float late = hc->worstMemberSlotError();
	CHECK_FALSE(hc->meleeEngaged());
	// the members behind their slots walk back; a member AHEAD of its slot waits for the formation (UseSlowHordeMovement, the member order RW 0x877A7A), so the worst
	// error shrinks but need not reach 0 (stop S-584)
	CHECK(late <= early);
	std::printf("  info: re-form: the fight ended at frame %d, worst slot error %.1f at the end -> %.1f 180 frames later, %u survivors, %llu contact refreshes\n", f, early, late, hc->getContainCount(), refreshes);
}

TEST_CASE("horde2 retail: the same charge twice gives the same state hash in every frame; another seed changes it")
{
	if (!haveWorld())
	{
		return;
	}
	const Charge a = charge("FactionMen", "RohanRohirrimHorde", "FactionMordor", "MordorFighterHorde", 3.14159265f, 150);
	const Charge b = charge("FactionMen", "RohanRohirrimHorde", "FactionMordor", "MordorFighterHorde", 3.14159265f, 150);
	REQUIRE(a.hashes.size() == b.hashes.size());
	for (size_t i = 0; i < a.hashes.size(); ++i)
	{
		REQUIRE_MESSAGE(a.hashes[i] == b.hashes[i], "frame " << i);
	}
	const Charge c = charge("FactionMen", "RohanRohirrimHorde", "FactionMordor", "MordorFighterHorde", 3.14159265f, 150, 8);
	bool differs = false;
	for (size_t i = 0; i < a.hashes.size() && i < c.hashes.size(); ++i)
	{
		differs = differs || a.hashes[i] != c.hashes[i];
	}
	CHECK(differs);
}

TEST_CASE("horde2 retail: the horde modules not ported yet are reported as unported")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	a.place("RohanRohirrimHorde", 0, 400.0f, 600.0f, 0.0f);
	a.place("GondorFighterHorde", 0, 400.0f, 800.0f, 0.0f);
	a.place("ArnorInfantryBanner", 0, 600.0f, 800.0f, 0.0f); // a banner carrier template (BannerCarrierUpdate)
	a.logic.runLogicFrame();
	const std::map<std::string, size_t> &u = shared().world->modules().unportedCreated();
	for (const char *name : { "HordeNotifyTargetsOfImminentProbableCrushingUpdate" })
	{
		CHECK_MESSAGE(u.count(name) == 0, name << " is ported (lane MODULES-3, S-1029)");
	}
	CHECK(u.count("StancesBehavior") == 0); // ported by INTEG-1 (S-585)
	CHECK(u.count("SquishCollide") == 0); // ported by HORDE-2
	CHECK(u.count("HordeMemberCollide") == 0);
	CHECK(u.count("BannerCarrierUpdate") == 0);
}

// GondorTowerShieldGuardHorde has AlternateFormation = GondorTowerShieldGuardHordePorcupine (FrontAngle 360, IsPorcupineFormation, ThisFormationIsTheMainFormation No):
// MSG_HORDE_TOGGLE_FORMATION swaps the horde object's HordeContain (RW 0x77BE83 / 0x875FB2) and back
TEST_CASE("horde2 retail: the formation swap gives the horde object the alternate HordeContain, the members walk to the new slots, and it swaps back")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *h = a.place("GondorTowerShieldGuardHorde", 0, 600.0f, 600.0f, 0.0f);
	for (int i = 0; i < 20; ++i)
	{
		a.logic.runLogicFrame();
	}
	const ObjectID id = h->getID();
	HordeContain *hc = dynamic_cast<HordeContain *>(h->getContain());
	REQUIRE(hc != nullptr);
	CHECK_FALSE(hc->hordeData().m_isPorcupineFormation);
	const unsigned members = hc->getContainCount();
	REQUIRE(members == 15);
	REQUIRE(hc->canToggleFormation());
	GameLogicDispatch dispatch(a.logic);
	HordeCommands::registerHandlers(dispatch);
	GameMessage msg(MSG_HORDE_TOGGLE_FORMATION, 0);
	msg.appendObjectIDArgument(id);
	dispatch.dispatch(msg);
	REQUIRE(a.logic.findObjectByID(id) == h); // the same object
	HordeContain *alt = dynamic_cast<HordeContain *>(h->getContain());
	REQUIRE(alt != nullptr);
	CHECK(alt != hc);
	CHECK(alt->hordeData().m_isPorcupineFormation);
	CHECK(alt->hordeData().m_frontAngle == 360.0f);
	CHECK(alt->getContainCount() == members);
	for (const Object *m : *alt->getContainedItemsList())
	{
		CHECK(m->getContainedBy() == h);
		CHECK(m->testModelCondition(CombatNames::modelCondition("ALTERNATE_FORMATION")));
	}
	for (int i = 0; i < 120; ++i)
	{
		a.logic.runLogicFrame();
	}
	CHECK(alt->worstMemberSlotError() < 5.0f);
	REQUIRE(alt->canToggleFormation());
	HordeContain *back = alt->toggleFormation();
	REQUIRE(back != nullptr);
	CHECK_FALSE(back->hordeData().m_isPorcupineFormation);
	CHECK(back->getContainCount() == members);
	for (int i = 0; i < 120; ++i)
	{
		a.logic.runLogicFrame();
	}
	CHECK(back->worstMemberSlotError() < 5.0f);
	for (const Object *m : *back->getContainedItemsList())
	{
		CHECK(m->testModelCondition(CombatNames::modelCondition("PRIMARY_FORMATION")));
	}
}

// lane INTEG-1: HORDE-2's banner carrier on XP-1's real horde rank. RW 0x8719E4 needs the horde's AI, then compares BannerCarrierMinLevel (a byte, default 1;
// GondorFighterHorde does not set it) with the horde object's ExperienceTracker rank (object + 0x26C, + 0x24): at rank 1 no carrier. The 49th orc kill fills the
// soldier pool to 50 (GoodLevel2, see test_xp_retail.cpp), the horde's tracker follows the pool (RW 0x873AB9) and its rank gain forces the check (RW 0x873ACF:
// RW 0x8719E4(1)), which makes BannerCarriersAllowed[0] (GondorInfantryBanner) on the horde's team and adds it to the horde.
TEST_CASE("horde2 retail: a Gondor soldier horde that reaches rank 2 gets its banner carrier (BannerCarrierMinLevel 1 < rank 2, RW 0x873ACF -> 0x8719E4(1))")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *horde = a.place("GondorFighterHorde", 0, 300.0f, 300.0f, 0.0f);
	std::vector<Object *> orcs;
	for (int i = 0; i < 49; ++i)
	{
		orcs.push_back(a.place("MordorFighter", 1, 1000.0f + 10.0f * (float)(i % 7), 1000.0f + 10.0f * (float)(i / 7), 0.0f));
	}
	a.logic.runLogicFrame();
	HordeContain *hc = hordeContainOf(horde);
	REQUIRE(hc);
	REQUIRE(horde->getAIUpdateInterface() != nullptr);
	Object *member = horde->getContain()->getContainedItemsList()->front();
	const size_t membersBefore = hc->getContainCount();
	auto kill = [&](Object &victim) { // a killing UNRESISTABLE hit from the member (the ActiveBody path: kill credit, experience)
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_sourceID = member->getID();
		info.m_input.m_amount = 100000.0f;
		info.m_input.m_kill = true;
		victim.attemptDamage(info);
	};
	for (int i = 0; i < 48; ++i)
	{
		kill(*orcs[(size_t)i]);
	}
	for (int f = 0; f < 30; ++f) // more than 4 * LOGICFRAMES_PER_SECOND frames: the unforced check of every update runs too
	{
		a.logic.runLogicFrame();
	}
	CHECK(horde->getExperienceTracker()->getRank() == 1);
	CHECK(hc->bannerCarrier() == 0); // BannerCarrierMinLevel 1 is not below rank 1
	kill(*orcs[48]);
	CHECK(horde->getExperienceTracker()->getRank() == 2); // the horde's tracker follows the pool at once (RW 0x873AB9)
	REQUIRE(hc->bannerCarrier() != 0);                    // the forced check in the same kill (RW 0x873ACF)
	Object *banner = a.logic.findObjectByID(hc->bannerCarrier());
	REQUIRE(banner);
	CHECK(banner->getTemplate()->getName() == "GondorInfantryBanner");
	CHECK(banner->getContainedBy() == horde);
	CHECK(banner->getControllingPlayer() == a.player(0));
	CHECK(hc->getContainCount() == membersBefore + 1);
	for (int f = 0; f < 10; ++f)
	{
		a.logic.runLogicFrame();
	}
	CHECK(hc->bannerCarrier() == banner->getID()); // one carrier: the check needs none
	std::printf("  info: GondorFighterHorde rank 2: banner carrier %s (%u members -> %u)\n", banner->getTemplate()->getName().c_str(), (unsigned)membersBefore,
		(unsigned)hc->getContainCount());
}

// lane INTEG-1 (S-585): the stances. A GondorFighterHorde (StancesBehavior, StanceTemplate = FighterHorde) takes Battle on its first update (RW 0x8622E9);
// MSG_CHANGE_STANCE (RW 0x77BC59 -> AIGroup::setStance RW 0x76FF27 -> RW 0x8620DD) swaps the stance's ModifierList on the horde and, through HordeContain
// (RW 0x870E50 / 0x870F75), on every member, and gives the horde the stance's MeleeBehavior (slot 0x260). stances.ini 2.01: FighterHorde Aggressive =
// FighterHordeStanceAggressive (DAMAGE_MULT 125%), HoldGround = FighterHordeStanceHoldGround (DAMAGE_MULT 85%), both with MeleeBehavior Amoeba; Battle has no entry.
TEST_CASE("horde2 retail: stances: MSG_CHANGE_STANCE swaps the stance's ModifierList on the horde and its members and sets its MeleeBehavior (RW 0x8620DD)")
{
	if (!haveWorld())
	{
		return;
	}
	const StanceTemplate *fighter = shared().world->stances().find("FighterHorde");
	REQUIRE(fighter);
	REQUIRE(fighter->entries[STANCE_AGGRESSIVE].attributeModifier == "FighterHordeStanceAggressive");
	REQUIRE(fighter->entries[STANCE_HOLD_GROUND].attributeModifier == "FighterHordeStanceHoldGround");
	CHECK(fighter->entries[STANCE_BATTLE].attributeModifier.empty());
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *h = a.place("GondorFighterHorde", 0, 600.0f, 600.0f, 0.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	StancesBehavior *st = StancesBehavior::of(*h);
	REQUIRE(st);
	CHECK(st->getStance() == STANCE_BATTLE); // RW 0x8622E9
	HordeContain *hc = hordeContainOf(h);
	REQUIRE(hc);
	CHECK(hc->meleeBehaviorData() == hc->hordeData().m_meleeBehavior.get()); // Battle: no stance MeleeBehavior, the contain's own
	GameLogicDispatch dispatch(a.logic);
	StancesBehavior::registerHandlers(dispatch);
	a.player(0)->selection() = { h->getID() };
	auto change = [&](StanceType s) {
		GameMessage m(MSG_CHANGE_STANCE, a.player(0)->getPlayerIndex());
		m.appendIntegerArgument((int)s);
		dispatch.dispatch(m);
	};
	auto damageMult = [](const Object &o) {
		float v = 1.0f;
		o.attributeModifierProduct(ATTRIBUTE_DAMAGE_MULT, nullptr, true, v);
		return v;
	};
	auto everyone = [&](const char *list) {
		int n = 0;
		const AttributeModifierPool *hp = static_cast<const AttributeModifierPool *>(h->findModule("AttributeModifierPoolUpdate"));
		n += hp && hp->hasList(list) ? 1 : 0;
		for (const Object *m : *hc->getContainedItemsList())
		{
			const AttributeModifierPool *p = static_cast<const AttributeModifierPool *>(m->findModule("AttributeModifierPoolUpdate"));
			n += p && p->hasList(list) ? 1 : 0;
		}
		return n;
	};
	const int objects = 1 + (int)hc->getContainCount();
	Object *member = hc->getContainedItemsList()->front();
	const std::uint32_t battleHash = a.logic.computeStateHash();
	change(STANCE_AGGRESSIVE);
	a.logic.runLogicFrame();
	CHECK(st->getStance() == STANCE_AGGRESSIVE);
	CHECK(everyone("FighterHordeStanceAggressive") == objects);
	CHECK(damageMult(*member) == doctest::Approx(1.25f));
	CHECK(hc->meleeBehaviorData() == fighter->entries[STANCE_AGGRESSIVE].meleeBehavior.get());
	CHECK(a.logic.computeStateHash() != battleHash);
	change(STANCE_HOLD_GROUND);
	a.logic.runLogicFrame();
	CHECK(st->getStance() == STANCE_HOLD_GROUND);
	CHECK(everyone("FighterHordeStanceAggressive") == 0); // RW 0x68F259 through the horde (RW 0x870F75)
	CHECK(everyone("FighterHordeStanceHoldGround") == objects);
	CHECK(damageMult(*member) == doctest::Approx(0.85f));
	CHECK(hc->meleeBehaviorData() == fighter->entries[STANCE_HOLD_GROUND].meleeBehavior.get());
	change(STANCE_BATTLE);
	a.logic.runLogicFrame();
	CHECK(st->getStance() == STANCE_BATTLE);
	CHECK(everyone("FighterHordeStanceHoldGround") == 0);
	CHECK(damageMult(*member) == doctest::Approx(1.0f));
	CHECK(hc->meleeBehaviorData() == hc->hordeData().m_meleeBehavior.get());
	// a stance of a horde the player does not have selected does not change (the group is the player's selection, RW 0x6AAB85)
	a.player(0)->selection().clear();
	change(STANCE_AGGRESSIVE);
	CHECK(st->getStance() == STANCE_BATTLE);
}

// lane INTEG-1 (S-585): the porcupine stance. GondorTowerShieldGuardHorde (StanceTemplate = PikeHorde): the formation swap into its porcupine formation sets the
// Porcupine stance (RW 0x876294: PikeHordeStancePorcupine, MeleeBehavior HoldGround); a stance change out of Porcupine toggles the porcupine formation back first
// (RW 0x86211D .. 0x862140: slots 0xF0 / 0x5C / 0x60; the swap back sets Battle, RW 0x87629F), then applies the new stance.
TEST_CASE("horde2 retail: stances: the porcupine formation sets the Porcupine stance and leaving the stance toggles the formation back (RW 0x876294, 0x86213C)")
{
	if (!haveWorld())
	{
		return;
	}
	const StanceTemplate *pike = shared().world->stances().find("PikeHorde");
	REQUIRE(pike);
	REQUIRE(pike->entries[STANCE_PORCUPINE].attributeModifier == "PikeHordeStancePorcupine");
	REQUIRE(static_cast<bool>(pike->entries[STANCE_PORCUPINE].meleeBehavior));
	CHECK(pike->entries[STANCE_PORCUPINE].meleeBehavior->m_kind == MeleeBehaviorModuleData::HOLD_GROUND);
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *h = a.place("GondorTowerShieldGuardHorde", 0, 600.0f, 600.0f, 0.0f);
	for (int i = 0; i < 20; ++i)
	{
		a.logic.runLogicFrame();
	}
	StancesBehavior *st = StancesBehavior::of(*h);
	REQUIRE(st);
	REQUIRE(st->getStance() == STANCE_BATTLE);
	GameLogicDispatch dispatch(a.logic);
	HordeCommands::registerHandlers(dispatch);
	StancesBehavior::registerHandlers(dispatch);
	GameMessage toggle(MSG_HORDE_TOGGLE_FORMATION, a.player(0)->getPlayerIndex());
	toggle.appendObjectIDArgument(h->getID());
	dispatch.dispatch(toggle);
	HordeContain *alt = hordeContainOf(h);
	REQUIRE(alt);
	REQUIRE(alt->hordeData().m_isPorcupineFormation);
	CHECK(st->getStance() == STANCE_PORCUPINE);
	const AttributeModifierPool *hp = static_cast<const AttributeModifierPool *>(h->findModule("AttributeModifierPoolUpdate"));
	REQUIRE(hp);
	CHECK(hp->hasList("PikeHordeStancePorcupine"));
	CHECK(alt->meleeBehaviorData() == pike->entries[STANCE_PORCUPINE].meleeBehavior.get());
	for (int i = 0; i < 30; ++i)
	{
		a.logic.runLogicFrame();
	}
	a.player(0)->selection() = { h->getID() };
	GameMessage m(MSG_CHANGE_STANCE, a.player(0)->getPlayerIndex());
	m.appendIntegerArgument((int)STANCE_AGGRESSIVE);
	dispatch.dispatch(m);
	HordeContain *back = hordeContainOf(h);
	REQUIRE(back);
	CHECK_FALSE(back->hordeData().m_isPorcupineFormation); // toggled back
	CHECK(st->getStance() == STANCE_AGGRESSIVE);
	CHECK_FALSE(hp->hasList("PikeHordeStancePorcupine"));
	CHECK(hp->hasList(pike->entries[STANCE_AGGRESSIVE].attributeModifier));
	CHECK(back->meleeBehaviorData() == pike->entries[STANCE_AGGRESSIVE].meleeBehavior.get());
}
