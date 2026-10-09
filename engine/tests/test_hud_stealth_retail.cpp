// OpenBFME retail tests for STEALTH-1 (RotWK invisibility in a live arena): which retail templates use the invisibility modules and the GameData rows; a camouflaged
// Gondor Ranger horde in a forest is not acquired, nor attackable by order, by Mordor archers that see it until a detector (a skull totem) reveals it, then the
// archers attack; away from trees the rangers stay visible; an enemy within the camouflage detection range reveals them; two runs give the same state hash.
// They share the HUD tests' retail world (the file name sorts with the test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"
#include "StealthArena.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/Radar.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/System/InvisibilityManager.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;
using stealthtest::Arena;
using stealthtest::membersOf;

bool haveWorld()
{
	return hudtest::haveWorld("stealth retail");
}

float healthOf(Object &o)
{
	float h = 0.0f;
	for (Object *m : membersOf(o))
	{
		h += (m->getBodyModule() && !m->isEffectivelyDead()) ? m->getBodyModule()->getHealth() : 0.0f;
	}
	return h;
}

// a member or the horde of `attackers` has a victim among `targets` (the horde object or its members)
bool targeting(Object &attackers, Object &targets)
{
	std::set<ObjectID> ids{ targets.getID() };
	for (Object *m : membersOf(targets))
	{
		ids.insert(m->getID());
	}
	std::vector<Object *> all = membersOf(attackers);
	all.push_back(&attackers);
	for (Object *a : all)
	{
		if (AIUpdateInterface *ai = a->getAIUpdateInterface())
		{
			if (ids.count(ai->currentVictimId()))
			{
				return true;
			}
		}
	}
	return false;
}

float floatField(const ThingTemplate *t, const char *name)
{
	if (const FieldValue *v = t->getFinalOverride()->findField(name))
	{
		if (const float *f = std::get_if<float>(v))
		{
			return *f;
		}
	}
	return -1.0f;
}

const float kRangersX = 600.0f, kRangersY = 600.0f;

struct Scenario
{
	Object *rangers = nullptr;
	Object *archers = nullptr;
};

// the rangers with a terrain tree beside them, in the HoldGround stance (a CAMOUFLAGE unit whose stance class is 3 does not acquire, RW 0x6685F7); they settle in
// first (the members' clips load: RELOADING_CLIP counts as firing, RW 0x68C89B), then the archers arrive `gap` to the east (they would otherwise be shot at:
// the rangers outrange them)
Scenario setUp(Arena &a, float gap, bool tree, int *settleFrames = nullptr)
{
	Scenario s;
	if (tree)
	{
		a.logic.invisibility().addTree(Coord3D{ kRangersX + 15.0f, kRangersY, 0.0f });
	}
	s.rangers = a.place("GondorRangerHorde", 0, kRangersX, kRangersY);
	if (StancesBehavior *sb = dynamic_cast<StancesBehavior *>(s.rangers->findModule("StancesBehavior")))
	{
		sb->setStance(STANCE_HOLD_GROUND);
	}
	int frames = 0;
	for (; frames < 40 && InvisibilityManager::invisibilityType(*s.rangers) != InvisibilityNugget::CAMOUFLAGE; ++frames)
	{
		a.run(1);
	}
	if (!tree)
	{
		a.run(40 - frames);
	}
	if (settleFrames)
	{
		*settleFrames = frames;
	}
	s.archers = a.place("MordorArcherHorde", 1, kRangersX + gap, kRangersY);
	return s;
}
} // namespace

TEST_CASE("stealth retail: the templates that use the invisibility modules, and the GameData rows")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &sh = shared();
	auto scope = sh.world->enterContext();
	std::map<std::string, std::vector<std::string>> users;
	size_t camouflage = 0, stealth = 0;
	for (const ThingTemplate *t : sh.world->things().templates())
	{
		const ThingTemplate *f = t->getFinalOverride();
		if (f != t)
		{
			continue;
		}
		for (const ThingTemplate::Nugget &n : f->behaviorModules().nuggets())
		{
			if (n.name == "InvisibilityUpdate" || n.name == "StealthDetectorUpdate" || n.name == "StealthUpdate")
			{
				users[n.name].push_back(t->getName());
			}
			if (const auto *d = dynamic_cast<const InvisibilityUpdateModuleData *>(n.data.get()))
			{
				++(d->m_nugget.invisibilityType == InvisibilityNugget::CAMOUFLAGE ? camouflage : stealth);
			}
		}
	}
	for (const auto &kv : users)
	{
		std::string names;
		for (const std::string &n : kv.second)
		{
			names += (names.empty() ? "" : " ") + n;
		}
		MESSAGE(kv.first << ": " << kv.second.size() << " templates: " << names);
	}
	MESSAGE("InvisibilityUpdate nuggets: " << camouflage << " CAMOUFLAGE, " << stealth << " STEALTH");
	auto has = [&](const char *cls, const char *name) {
		const std::vector<std::string> &v = users[cls];
		return std::find(v.begin(), v.end(), name) != v.end();
	};
	CHECK(has("InvisibilityUpdate", "GondorRangerHorde"));
	CHECK(has("InvisibilityUpdate", "ArnorRangerHorde"));
	CHECK(has("InvisibilityUpdate", "ElvenLorienArcherHorde"));
	CHECK(has("InvisibilityUpdate", "ElvenMirkwoodArcherHorde"));
	CHECK(has("InvisibilityUpdate", "RohanFrodo"));
	CHECK(has("InvisibilityUpdate", "ElvenCitadel"));
	CHECK(has("InvisibilityUpdate", "EnshroudingMistPing"));
	CHECK(has("StealthDetectorUpdate", "WildSkullTotem"));
	CHECK(has("StealthDetectorUpdate", "EyeOfSauron"));
	CHECK(has("StealthUpdate", "NoldorWarriorHorde"));
	CHECK(users["InvisibilityUpdate"].size() >= 30u);
	CHECK(users["StealthDetectorUpdate"].size() >= 14u);

	Arena a(sh);
	const GameLogicSettings &s = a.logic.settings();
	CHECK(s.invisibilityRulesLoaded);
	CHECK(s.reinvisibilityDelay == 10u); // 2000 ms
	CHECK(s.invisibilityOpacityMin == doctest::Approx(0.4f));
	CHECK(s.invisibilityOpacityMax == doctest::Approx(3.0f));
	CHECK(s.invisibilityOpacityCycleFrames == 10u);
	CHECK(s.camouflageDetectorFilter.get() != nullptr);
	// the rangers' nugget (data\ini\object\includes\defaultinvisibilityupdate.inc): CAMOUFLAGE, CAMOUFLAGE_RADIUS 100, AWAY_FROM_TREES MOVING FIRING_ANY
	const InvisibilityUpdateModuleData *d = nullptr;
	for (const ThingTemplate::Nugget &n : sh.world->things().findTemplate("GondorRangerHorde")->getFinalOverride()->behaviorModules().nuggets())
	{
		if (const auto *x = dynamic_cast<const InvisibilityUpdateModuleData *>(n.data.get()))
		{
			d = x;
		}
	}
	REQUIRE(d != nullptr);
	CHECK(d->m_nugget.invisibilityType == InvisibilityNugget::CAMOUFLAGE);
	CHECK(d->m_nugget.detectionRange == 100.0f);
	const unsigned forbidden = InvisibilityNugget::AWAY_FROM_TREES | InvisibilityNugget::MOVING | InvisibilityNugget::FIRING_ANY;
	CHECK(d->m_nugget.forbiddenConditions == forbidden);
	CHECK(d->m_startsActive);
	CHECK(d->m_updatePeriod == 10u);
}

TEST_CASE("stealth retail: a camouflaged Ranger horde in a forest is not attacked by archers in sight until a skull totem reveals it; then they attack")
{
	if (!haveWorld())
	{
		return;
	}
	const float gap = 250.0f;
	Arena a(shared());
	int frames = 0;
	Scenario s = setUp(a, gap, true, &frames);
	const float archerVision = floatField(shared().world->things().findTemplate("MordorArcherHorde"), "VisionRange");
	MESSAGE("MordorArcherHorde VisionRange " << archerVision << ", the archers " << gap << " east of the rangers");
	CHECK(archerVision > gap + 60.0f);
	MESSAGE("camouflaged after " << frames << " frames");
	REQUIRE(InvisibilityManager::invisibilityType(*s.rangers) == InvisibilityNugget::CAMOUFLAGE);
	for (Object *m : membersOf(*s.rangers))
	{
		CHECK(InvisibilityManager::invisibilityType(*m) == InvisibilityNugget::CAMOUFLAGE); // RW 0x81AB0A: the members too
	}
	CHECK(InvisibilityManager::isStealthedAndUndetected(*s.rangers, a.player(1)));
	CHECK(InvisibilityManager::clientLook(*s.rangers, a.player(0)) == 1);
	CHECK(InvisibilityManager::clientLook(*s.rangers, a.player(1)) == 5);
	// the archers see them but neither acquire them nor may be ordered at them (RW 0x6C9147)
	REQUIRE(s.archers->getWeapons() != nullptr);
	CHECK_FALSE(s.archers->getWeapons()->canAttackObject(*s.rangers, CMD_FROM_PLAYER, false));
	const std::vector<Object *> archerMembers = membersOf(*s.archers);
	REQUIRE_FALSE(archerMembers.empty());
	const std::vector<Object *> rangerMembers = membersOf(*s.rangers);
	REQUIRE_FALSE(rangerMembers.empty());
	REQUIRE(archerMembers[0]->getWeapons() != nullptr);
	CHECK_FALSE(archerMembers[0]->getWeapons()->canAttackObject(*rangerMembers[0], CMD_FROM_PLAYER, false));
	const float before = healthOf(*s.rangers);
	bool targeted = false;
	for (int i = 0; i < 60 && !targeted; ++i)
	{
		a.run(1);
		targeted = targeting(*s.archers, *s.rangers);
	}
	CHECK_FALSE(targeted);
	CHECK(healthOf(*s.rangers) == before);
	CHECK(InvisibilityManager::invisibilityType(*s.rangers) == InvisibilityNugget::CAMOUFLAGE);

	// a detector of the enemy within its 400 range (WildSkullTotem, SKULL_TOTEM_STEALTH_DETECT_RADIUS)
	a.place("WildSkullTotem", 1, kRangersX + gap, kRangersY + 100.0f);
	bool revealed = false;
	for (frames = 0; frames < 40 && !revealed; ++frames)
	{
		a.run(1);
		revealed = InvisibilityManager::invisibilityType(*s.rangers) == InvisibilityNugget::NONE;
	}
	MESSAGE("revealed after " << frames << " frames");
	REQUIRE(revealed);
	CHECK(s.rangers->testStatus((unsigned)CombatNames::status("INVISIBLE_DETECTED")));
	CHECK_FALSE(InvisibilityManager::isStealthedAndUndetected(*s.rangers, a.player(1)));
	CHECK(InvisibilityManager::clientLook(*s.rangers, a.player(1)) == 3);
	for (frames = 0; frames < 80 && !targeted; ++frames)
	{
		a.run(1);
		targeted = targeting(*s.archers, *s.rangers);
	}
	MESSAGE("the archers acquired the rangers " << frames << " frames after the reveal");
	CHECK(targeted);
	a.run(75);
	MESSAGE("the rangers' health " << before << " -> " << healthOf(*s.rangers));
	CHECK(healthOf(*s.rangers) < before);
	CHECK(a.logic.invisibility().reveals() >= 1u);
	const GameLogic::Report report = a.logic.report();
	CHECK(report.errors.empty());
	for (const char *stop : { "[S-1040] invisibility (lane STEALTH-1)", "[S-1041] invisibility modules", "[S-1042] stealth look", "[S-1043] stealth abilities" })
	{
		CHECK_MESSAGE(std::any_of(report.stops.begin(), report.stops.end(), [&](const std::string &l) { return l.rfind(stop, 0) == 0; }), stop);
	}
}

TEST_CASE("stealth retail: away from trees the rangers stay visible; an enemy within the camouflage detection range reveals them; the state is hashed and deterministic")
{
	if (!haveWorld())
	{
		return;
	}
	{
		Arena a(shared());
		Scenario s = setUp(a, 250.0f, false);
		CHECK(InvisibilityManager::invisibilityType(*s.rangers) == InvisibilityNugget::NONE); // AWAY_FROM_TREES
		CHECK_FALSE(InvisibilityManager::isStealthedAndUndetected(*s.rangers, a.player(1)));
	}
	{
		Arena a(shared());
		Scenario s = setUp(a, 480.0f, true);
		REQUIRE(InvisibilityManager::invisibilityType(*s.rangers) == InvisibilityNugget::CAMOUFLAGE);
		// a Mordor horde walks within 100 * CamouflageDetectionMultiplier: the next evaluation (at most a second) reveals the rangers
		a.place("MordorArcherHorde", 1, kRangersX - 60.0f, kRangersY);
		a.run(6);
		CHECK(InvisibilityManager::invisibilityType(*s.rangers) == InvisibilityNugget::NONE);
		CHECK(s.rangers->testStatus((unsigned)CombatNames::status("INVISIBLE_DETECTED")));
	}
	std::uint32_t h[2] = { 0, 0 };
	for (int k = 0; k < 2; ++k)
	{
		Arena a(shared());
		Scenario s = setUp(a, 250.0f, true);
		a.run(10);
		const std::uint32_t invisible = a.logic.computeStateHash();
		s.rangers->setStatus((unsigned)CombatNames::status("INVISIBLE_DETECTED"), true);
		CHECK(a.logic.computeStateHash() != invisible);
		s.rangers->setStatus((unsigned)CombatNames::status("INVISIBLE_DETECTED"), false);
		a.place("WildSkullTotem", 1, kRangersX + 250.0f, kRangersY + 100.0f);
		a.run(60);
		h[k] = a.logic.computeStateHash();
	}
	CHECK(h[0] == h[1]);
}


TEST_CASE("stealth retail: a Noldor warrior horde (StealthUpdate) stealths among trees; archers cannot target it until it ambushes; an enemy within DetectedByAnyoneRange detects it")
{
	if (!haveWorld())
	{
		return;
	}
	const int stealthed = CombatNames::status("STEALTHED");
	const int detected = CombatNames::status("DETECTED");
	{
		Arena a(shared());
		Object *warriors = a.place("NoldorWarriorHorde", 0, kRangersX, kRangersY); // no tree: AWAY_FROM_TREES forbids
		a.run(20);
		CHECK_FALSE(warriors->testStatus((unsigned)stealthed));
	}
	for (int part = 0; part < 2; ++part)
	{
		Arena a(shared());
		a.logic.invisibility().addTree(Coord3D{ kRangersX + 15.0f, kRangersY, 0.0f });
		Object *warriors = a.place("NoldorWarriorHorde", 0, kRangersX, kRangersY);
		REQUIRE(StealthUpdate::of(*warriors) != nullptr);
		int frames = 0;
		for (; frames < 40 && !warriors->testStatus((unsigned)stealthed); ++frames)
		{
			a.run(1);
		}
		REQUIRE(warriors->testStatus((unsigned)stealthed));
		if (part == 0)
		{
			MESSAGE("STEALTHED after " << frames << " frames (StealthDelay " << StealthUpdate::of(*warriors)->data()->m_stealthDelay << " frames)");
			for (Object *m : membersOf(*warriors))
			{
				CHECK(m->testStatus((unsigned)stealthed)); // RW 0x77670A: horde-wide
			}
			CHECK(InvisibilityManager::isStealthedAndUndetected(*warriors, a.player(1)));
			CHECK(InvisibilityManager::clientLook(*warriors, a.player(1)) == 5);
			CHECK(InvisibilityManager::clientLook(*warriors, a.player(0)) == 1);
			// archers in sight: they cannot target the stealthed horde; the horde ambushes them (AutoAcquireEnemiesWhenIdle STEALTHED) and attacking ends the
			// stealth (StealthForbiddenConditions ATTACKING), then the archers acquire it
			Object *archers = a.place("MordorArcherHorde", 1, kRangersX + 300.0f, kRangersY);
			REQUIRE(archers->getWeapons() != nullptr);
			CHECK_FALSE(archers->getWeapons()->canAttackObject(*warriors, CMD_FROM_PLAYER, false));
			int lost = -1, acquired = -1;
			for (int i = 0; i < 120 && acquired < 0; ++i)
			{
				const bool targetedNow = targeting(*archers, *warriors);
				if (targetedNow && lost < 0)
				{
					CHECK_MESSAGE(false, "the archers acquired a stealthed horde");
				}
				a.run(1);
				if (lost < 0 && !warriors->testStatus((unsigned)stealthed))
				{
					lost = i;
				}
				if (acquired < 0 && targeting(*archers, *warriors))
				{
					acquired = i;
				}
			}
			MESSAGE("the warriors left stealth at +" << lost << " frames (ambush), the archers acquired them at +" << acquired);
			CHECK(lost >= 0);
			CHECK(acquired >= lost);
		}
		else
		{
			// an enemy within DetectedByAnyoneRange (120): DETECTED at the next update
			a.place("MordorArcherHorde", 1, kRangersX - 90.0f, kRangersY);
			a.run(1);
			CHECK(warriors->testStatus((unsigned)detected));
			CHECK_FALSE(InvisibilityManager::isStealthedAndUndetected(*warriors, a.player(1)));
			CHECK(InvisibilityManager::clientLook(*warriors, a.player(1)) == 3);
		}
	}
}

TEST_CASE("stealth retail (review r1): the radar leaves out an enemy's undetected invisible object and shows it once detected, both blip overloads")
{
	if (!haveWorld())
	{
		return;
	}
	hudtest::Rig rig(shared());
	rig.game->shroud().setDisplayed(false);
	const std::vector<Coord3D> &trees = rig.logic().invisibility().trees();
	REQUIRE_FALSE(trees.empty());
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	std::string err;
	const Coord3D at{ trees[trees.size() / 2].x + 20.0f, trees[trees.size() / 2].y, 0.0f };
	Object *rangers = rig.game->createObject("GondorRangerHorde", rig.index(), at, 0.0f, &err);
	REQUIRE_MESSAGE(rangers != nullptr, err);
	if (StancesBehavior *sb = dynamic_cast<StancesBehavior *>(rangers->findModule("StancesBehavior")))
	{
		sb->setStance(STANCE_HOLD_GROUND);
	}
	for (int i = 0; i < 40 && InvisibilityManager::invisibilityType(*rangers) != InvisibilityNugget::CAMOUFLAGE; ++i)
	{
		rig.frame();
	}
	REQUIRE(InvisibilityManager::invisibilityType(*rangers) == InvisibilityNugget::CAMOUFLAGE);
	Radar radar(rig.input->context());
	REQUIRE(radar.setupFromTerrain());
	auto has = [&](const std::vector<Radar::Blip> &blips) {
		return std::any_of(blips.begin(), blips.end(), [&](const Radar::Blip &b) { return b.object == rangers->getID(); });
	};
	CHECK(has(radar.blips(256))); // the owner's radar shows them
	rig.game->players().setLocalPlayer(enemy);
	CHECK(InvisibilityManager::clientLook(*rangers, enemy) == 5);
	CHECK_FALSE(has(radar.blips(256)));
	std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(rig.logic(), nullptr, false, 0);
	CHECK_FALSE(has(radar.blips(*snap, 256, enemy->getPlayerIndex())));
	// detected: on the enemy's radar again
	REQUIRE(rig.logic().invisibility().markDetected(rangers, nullptr, 100, 1));
	CHECK(InvisibilityManager::clientLook(*rangers, enemy) == 3);
	CHECK(has(radar.blips(256)));
	snap = LogicSnapshot::build(rig.logic(), nullptr, false, 0);
	CHECK(has(radar.blips(*snap, 256, enemy->getPlayerIndex())));
	rig.game->players().setLocalPlayer(rig.local);
}

TEST_CASE("stealth retail (review r1): a detector marks a container's occupant for DetectionRate + 2 frames (RW 0x8A680C / 0x8A6823), an ordinary candidate for + 1")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *totem = a.place("WildSkullTotem", 1, kRangersX, kRangersY);
	StealthDetectorUpdate *det = dynamic_cast<StealthDetectorUpdate *>(totem->findModule("StealthDetectorUpdate"));
	REQUIRE(det != nullptr);
	const auto *data = dynamic_cast<const StealthDetectorUpdateModuleData *>(det->getModuleData());
	REQUIRE(data != nullptr);
	const unsigned rate = data->m_detectionRate;
	Object *occupant = a.place("LindonElvenWarrior", 0, kRangersX + 1000.0f, kRangersY); // out of the totem's range: only the occupant path marks it
	Object *candidate = a.place("LindonElvenWarrior", 0, kRangersX + 100.0f, kRangersY);
	REQUIRE(StealthUpdate::of(*occupant) != nullptr);
	REQUIRE(StealthUpdate::of(*candidate) != nullptr);
	a.run(1);
	const int detected = CombatNames::status("DETECTED");
	REQUIRE_FALSE(occupant->testStatus((unsigned)detected));
	REQUIRE_FALSE(candidate->testStatus((unsigned)detected));
	const unsigned now = a.logic.getFrame();
	const ContainModuleInterface::ContainedItemsList occupants{ occupant };
	StealthDetectorUpdate::detectOccupants(*totem, occupants, rate);
	det->update(); // the candidate within the 400 range
	CHECK(occupant->testStatus((unsigned)detected));
	CHECK(candidate->testStatus((unsigned)detected));
	CHECK(StealthUpdate::of(*occupant)->detectionExpiresFrame() == now + rate + 2);
	CHECK(StealthUpdate::of(*candidate)->detectionExpiresFrame() == now + rate + 1);
}
