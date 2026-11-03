// OpenBFME. GPL-3.0. Retail veterancy recovery through combat credit and scheduled updates.
#include "HordeCompleteTestUtil.h"
#include "doctest.h"
#include "CampaignTestUtil.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"

#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BannerCarrierUpdate.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"

namespace
{
struct RecoveryGame : campaigntest::Mission
{
	Player *owner = nullptr;
	Player *enemy = nullptr;
	Coord3D position{ 1000.0f, 1000.0f, 0.0f };

	void start(starttest::Shared &s, bool campaign)
	{
		if (campaign)
		{
			load(s);
			REQUIRE(game->mapScriptsRunning());
			step(200); // Foundation's opening camera sequence ends before player control.
			REQUIRE(requested("CAMERA_LETTERBOX_END"));
			owner = logic().players().getLocalPlayer();
			enemy = logic().players().findPlayerWithName("PlyrBNEast");
			REQUIRE(unit("Witch King"));
			position = *unit("Witch King")->getPosition();
			position.x += 200.0f;
			// lane DEVPORT-7: Foundation's opening battle is fought around the Witch-king for thousands of frames (PlyrBNEast's rangers and Dunedain
			// hit the staged hordes and pull them into the fight). As auto-acquire is off, the mission's other players and the local player are made
			// neutral to each other for the scene: nobody fights it; the well's enemy horde is then a neutral one (still not an ally: not refilled)
			for (int i = 0; i < logic().players().getPlayerCount(); ++i)
			{
				Player *p = logic().players().getNthPlayer(i);
				if (p && p != owner)
				{
					owner->setPlayerRelationship(p, NEUTRAL);
					p->setPlayerRelationship(owner, NEUTRAL);
				}
			}
		}
		else
		{
			source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
			assets = std::make_unique<WW3DAssetManager>(*source);
			game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *assets, s.options);
			std::string error;
			std::vector<MapCacheEntry> maps;
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, maps, &error), error);
			NewGameMessage message;
			message.game.mapName = "maps/map mp evendim/map mp evendim.map";
			message.game.seed = 2;
			const char *factions[] = { "FactionAngmar", "FactionMordor" };
			for (int i = 0; i < 2; ++i)
			{
				auto &slot = message.game.slots[i];
				slot.state = SLOT_PLAYER;
				slot.name = i == 0 ? u"A" : u"B";
				slot.startPos = i;
				slot.color = i;
				slot.teamNumber = i;
				for (int t = 0; t < s.world->playerTemplates().getPlayerTemplateCount(); ++t)
				{
					if (s.world->playerTemplates().getNthPlayerTemplate(t)->getName() == factions[i])
					{
						slot.playerTemplate = t;
					}
				}
			}
			NewGameStart start(RandomAlgorithm::ZH_CarryChain);
			REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, maps,
				RandomAlgorithm::ZH_CarryChain, start, &error), error);
			LiveGame::Options options;
			options.start = &start;
			REQUIRE_MESSAGE(game->load(options, &error), error);
			owner = logic().players().findPlayerWithName(game->report().startSlotPlayers[0]);
			enemy = logic().players().findPlayerWithName(game->report().startSlotPlayers[1]);
			position = waypoint("Player_1_Start");
			position.x += 200.0f;
		}
		REQUIRE(owner);
		REQUIRE(enemy);
		logic().combat().setAutoAcquireEnabled(false); // Isolate recovery from incidental map fights; scripts remain enabled.
	}

	Object *make(const std::string &name, Player *player = nullptr)
	{
		const ThingTemplate *tt = logic().things().findTemplate(name);
		REQUIRE_MESSAGE(tt, name);
		Object *o = logic().newObject(tt, (player ? player : owner)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		place(*o, position);
		return testutil::completeHorde(o);
	}

	void hit(Object &target, float amount, Object *attacker = nullptr)
	{
		DamageInfo damage;
		damage.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		damage.m_input.m_amount = amount;
		damage.m_input.m_sourceID = attacker ? attacker->getID() : 0;
		target.attemptDamage(damage);
	}

	HordeContain &contain(Object &horde)
	{
		auto *c = dynamic_cast<HordeContain *>(horde.getContain());
		REQUIRE(c);
		return *c;
	}

	Object &member(Object &horde, ObjectID exclude = 0)
	{
		auto &c = contain(horde);
		for (Object *o : *c.getContainedItemsList())
		{
			if (o->getID() != c.bannerCarrier() && o->getID() != exclude && !o->isEffectivelyDead())
			{
				return *o;
			}
		}
		FAIL("horde has no living payload member");
		return horde;
	}

	Object &living(ObjectID id)
	{
		Object *o = logic().findObjectByID(id);
		REQUIRE_MESSAGE(o, "recovery subject was destroyed: " << id);
		REQUIRE_FALSE(o->isEffectivelyDead());
		return *o;
	}

	void veteran(Object &horde, int rank = 2, const char *upgrade = "Upgrade_ObjectLevel2")
	{
		step(2);
		Object &killer = member(horde);
		REQUIRE(horde.getExperienceTracker());
		CHECK(horde.getExperienceTracker()->getRank() == 1);
		// lane DEVPORT-7: the kills earn experience only from an enemy (the campaign scene keeps the players neutral otherwise)
		const Relationship was = owner->getRelationship(enemy->getDefaultTeam());
		owner->setPlayerRelationship(enemy, ENEMIES);
		enemy->setPlayerRelationship(owner, ENEMIES);
		for (int batch = 0; batch < 100 && horde.getExperienceTracker()->getRank() < rank; ++batch)
		{
			std::vector<Object *> victims;
			for (int i = 0; i < 10; ++i)
			{
				Object *victim = make("MordorFighter", enemy);
				Coord3D away = position;
				away.x += 1000.0f;
				place(*victim, away);
				victims.push_back(victim);
			}
			step(); // Initialize the victims' level-one ExperienceValue before they die.
			for (Object *victim : victims)
			{
				// tolerance: This fixture needs a positive XP award to prove its victims are initialized; it does not pin their payout.
				REQUIRE(victim->getExperienceTracker()->getExperienceValue(killer, false) > 0);
				hit(*victim, 100000.0f, &killer);
			}
			step();
		}
		owner->setPlayerRelationship(enemy, was);
		enemy->setPlayerRelationship(owner, was);
		REQUIRE(horde.getExperienceTracker()->getRank() == rank);
		step(2); // ExperienceWorld's queued level grants.
		CHECK(horde.hasUpgrade(upgrade));
		CHECK(killer.hasUpgrade(upgrade));
	}
};

void recovery(starttest::Shared &s, bool campaign)
{
	RecoveryGame g;
	g.start(s, campaign);
	Object *horde = g.make("AngmarDarkDunedainHorde");
	g.veteran(*horde);
	auto &c = g.contain(*horde);
	// tolerance: ObjectIDs are allocated per run; this checks for a carrier, not a fixed ID.
	REQUIRE(c.bannerCarrier() != 0);
	Object *banner = g.logic().findObjectByID(c.bannerCarrier());
	REQUIRE(banner);
	auto *replenish = dynamic_cast<BannerCarrierUpdate *>(banner->findModule("BannerCarrierUpdate"));
	REQUIRE(replenish);
	CHECK(replenish->data().m_idleSpawnRate == 50);
	CHECK(replenish->data().m_meleeFreeUnitSpawnTime == 100);
	const int full = c.getContainCount();
	g.hit(g.member(*horde), 100000.0f);
	g.step(2);
	REQUIRE(c.getContainCount() == full - 1);
	Object &wounded = g.member(*horde);
	const ObjectID woundedID = wounded.getID();
	const float maximum = wounded.getBodyModule()->getMaxHealth();
	g.hit(wounded, maximum / 2.0f);
	const float damaged = wounded.getBodyModule()->getHealth();
	g.step(300); // 60 seconds exceeds the retail 20-second combat gate plus 10-second spawn pulse.
	g.living(woundedID);
	MESSAGE((campaign ? "campaign" : "skirmish") << ": rank " << horde->getExperienceTracker()->getRank()
		<< ", banner spawned " << replenish->membersSpawned() << ", members " << c.getContainCount()
		<< "/" << full << ", health " << damaged << " -> " << wounded.getBodyModule()->getHealth());
	CHECK(c.getContainCount() == full);
	CHECK(replenish->membersSpawned() == 1);
	// This ordinary Angmar troop has no AutoHealBehavior: rank two adds bonuses and a banner, not a self-heal.
	CHECK(wounded.findModule("AutoHealBehavior") == nullptr);
	CHECK(wounded.getBodyModule()->getHealth() == damaged);

	Object *hero = g.make("AngmarWitchking");
	g.step(100); // Let the full-health self healer go to sleep before damage wakes it.
	REQUIRE(hero->findModule("AutoHealBehavior"));
	const float heroMax = hero->getBodyModule()->getMaxHealth();
	g.hit(*hero, 100.0f);
	g.step(74); // StartHealingDelay = 15000 ms = 75 logic frames.
	CHECK(hero->getBodyModule()->getHealth() == heroMax - 100.0f);
	g.step(30);
	CHECK(hero->getBodyModule()->getHealth() == heroMax);
}

void elvenRecovery(starttest::Shared &s, bool campaign)
{
	RecoveryGame g;
	g.start(s, campaign);
	Object *horde = g.make("ElvenLorienWarriorHorde");
	g.veteran(*horde);
	auto &c = g.contain(*horde);
	Object *banner = g.logic().findObjectByID(c.bannerCarrier());
	REQUIRE(banner);
	REQUIRE(banner->findModule("AutoHealBehavior"));
	Object &wounded = g.member(*horde);
	const ObjectID id = wounded.getID();
	const float maximum = wounded.getBodyModule()->getMaxHealth();
	g.hit(wounded, 50.0f);
	REQUIRE(wounded.getBodyModule()->getHealth() == maximum - 50.0f);
	g.step(60); // ElvenBanner: 10 health every 2000 ms (10 frames), through AffectsContained.
	CHECK(g.living(id).getBodyModule()->getHealth() == maximum);
}

void miniHordeRecovery(starttest::Shared &s, bool campaign)
{
	RecoveryGame g;
	g.start(s, campaign);
	Object *horde = g.make("GondorKnightsofDolHorde");
	g.step(2);
	auto &c = g.contain(*horde);
	REQUIRE(c.getContainCount() == 5);
	REQUIRE(horde->findModule("AutoHealBehavior"));
	CHECK_FALSE(horde->hasUpgrade("Upgrade_MiniHordeLvl3"));
	g.hit(g.member(*horde), 100000.0f);
	g.hit(g.member(*horde), 100000.0f);
	g.step(2);
	REQUIRE(c.getContainCount() == 3);
	Object &wounded = g.member(*horde);
	const ObjectID id = wounded.getID();
	const float maximum = wounded.getBodyModule()->getMaxHealth();
	g.hit(wounded, 100.0f);
	g.step(100);
	CHECK(g.living(id).getBodyModule()->getHealth() == maximum - 100.0f);
	CHECK(c.getContainCount() == 3);
	g.veteran(*horde, 3, "Upgrade_MiniHordeLvl3");
	REQUIRE(c.getContainCount() != 5); // Two losses ensure the rank helper cannot consume the final replenishment pulse.
	for (int i = 0; i < 450 && c.getContainCount() < 5; ++i)
	{
		g.step();
	}
	REQUIRE(c.getContainCount() == 5);
	// A loss on this observed replenishment frame must wait another raw 200 frames, not 200 ms.
	g.hit(g.member(*horde, id), 100000.0f);
	g.step(199);
	CHECK(c.getContainCount() == 4);
	g.step(26); // First HealingDelay pulse at/after RespawnMinimumDelay.
	Object &healed = g.living(id);
	CHECK(healed.getBodyModule()->getHealth() == healed.getBodyModule()->getMaxHealth());
	CHECK(c.getContainCount() == 5);
	g.step(100);
	CHECK(c.getContainCount() == 5); // Never exceed the retail Slots capacity.
}

void wellRecovery(starttest::Shared &s, bool campaign)
{
	RecoveryGame g;
	g.start(s, campaign);
	const Coord3D centre = g.position;
	std::vector<Object *> hordes;
	for (int i = 0; i < 4; ++i)
	{
		g.position = centre;
		g.position.x += i == 2 ? 600.0f : 30.0f * i;
		hordes.push_back(g.make("GondorKnightsofDolHorde", i == 3 ? g.enemy : g.owner));
	}
	g.step(2);
	for (Object *horde : hordes)
	{
		REQUIRE(g.contain(*horde).getContainCount() == 5);
		CHECK_FALSE(horde->hasUpgrade("Upgrade_MiniHordeLvl3"));
		g.hit(g.member(*horde), 100000.0f);
	}
	g.step(2);
	for (Object *horde : hordes)
	{
		REQUIRE(g.contain(*horde).getContainCount() == 4);
	}
	g.position = centre;
	Object *well = g.make("GondorWell");
	auto *replenish = dynamic_cast<BannerCarrierUpdate *>(well->findModule("BannerCarrierUpdate"));
	REQUIRE(replenish);
	CHECK(replenish->data().m_replenishNearbyHorde);
	CHECK(replenish->data().m_replenishAllNearbyHordes);
	CHECK(replenish->data().m_idleSpawnRate == 50);
	g.step(100);
	CHECK(g.contain(*hordes[0]).getContainCount() == 5);
	CHECK(g.contain(*hordes[1]).getContainCount() == 5);
	CHECK(g.contain(*hordes[2]).getContainCount() == 4); // Beyond ScanHordeDistance.
	CHECK(g.contain(*hordes[3]).getContainCount() == 4); // Enemy in range.
	CHECK(replenish->membersSpawned() == 2);
}
} // namespace

TEST_CASE("veteran recovery retail: skirmish rank two banner and hero healing")
{
	OPENBFME_REQUIRE_START(s);
	recovery(*s, false);
}

TEST_CASE("veteran recovery retail: Foundation rank two banner and hero healing")
{
	OPENBFME_REQUIRE_START(s);
	recovery(*s, true);
}

TEST_CASE("veteran recovery retail: skirmish Elven banner heals its horde")
{
	OPENBFME_REQUIRE_START(s);
	elvenRecovery(*s, false);
}

TEST_CASE("veteran recovery retail: Foundation Elven banner heals its horde")
{
	OPENBFME_REQUIRE_START(s);
	elvenRecovery(*s, true);
}

TEST_CASE("veteran recovery retail: skirmish rank three mini-horde heals and replenishes")
{
	OPENBFME_REQUIRE_START(s);
	miniHordeRecovery(*s, false);
}

TEST_CASE("veteran recovery retail: Foundation rank three mini-horde heals and replenishes")
{
	OPENBFME_REQUIRE_START(s);
	miniHordeRecovery(*s, true);
}

TEST_CASE("veteran recovery retail: skirmish well replenishes nearby allies only")
{
	OPENBFME_REQUIRE_START(s);
	wellRecovery(*s, false);
}

TEST_CASE("veteran recovery retail: Foundation well replenishes nearby allies only")
{
	OPENBFME_REQUIRE_START(s);
	wellRecovery(*s, true);
}
