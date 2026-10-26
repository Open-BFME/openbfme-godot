// OpenBFME. GPL-3.0. Lane BANNER-1: a rank-two horde's banner carrier refills the horde after a real fight (owner, nightly 2026-10-22: "banner
// carriers don't auto replace units if they are lvl 2"). Retail: BannerCarrierUpdate::update RW 0x89ACE4, the horde's combat test RW 0x86EE52,
// the member spawn RW 0x89A392 / 0x873AE3.
#include "doctest.h"
#include "CampaignTestUtil.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BannerCarrierUpdate.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "Common/ScoreKeeper.h"

#include <algorithm>
#include <functional>
#include <sstream>

namespace
{
struct BannerGame : campaigntest::Mission
{
	Player *owner = nullptr;
	Player *enemy = nullptr;
	Coord3D position{ 0.0f, 0.0f, 0.0f };

	void start(starttest::Shared &s)
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
		const char *factions[] = { "FactionMen", "FactionMordor" };
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
		REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, maps, RandomAlgorithm::ZH_CarryChain, start, &error), error);
		LiveGame::Options options;
		options.start = &start;
		REQUIRE_MESSAGE(game->load(options, &error), error);
		owner = logic().players().findPlayerWithName(game->report().startSlotPlayers[0]);
		enemy = logic().players().findPlayerWithName(game->report().startSlotPlayers[1]);
		REQUIRE(owner);
		REQUIRE(enemy);
		position = waypoint("Player_1_Start");
		position.x += 400.0f;
	}

	Object *make(const std::string &name, Player *player, Coord3D at)
	{
		const ThingTemplate *tt = logic().things().findTemplate(name);
		REQUIRE_MESSAGE(tt, name);
		Object *o = logic().newObject(tt, player->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		place(*o, at);
		return o;
	}

	static HordeContain &contain(Object &horde)
	{
		auto *c = dynamic_cast<HordeContain *>(horde.getContain());
		REQUIRE(c);
		return *c;
	}

	std::vector<Object *> payload(Object &horde)
	{
		std::vector<Object *> out;
		auto &c = contain(horde);
		for (Object *o : *c.getContainedItemsList())
		{
			if (o->getID() != c.bannerCarrier() && !o->isEffectivelyDead())
			{
				out.push_back(o);
			}
		}
		return out;
	}

	// rank two through kills credited to a member, as a player's horde earns it (the victims stand far away)
	void veteran(Object &horde)
	{
		step(2);
		Object *killer = payload(horde).front();
		for (int batch = 0; batch < 100 && horde.getExperienceTracker()->getRank() < 2; ++batch)
		{
			std::vector<Object *> victims;
			Coord3D away = position;
			away.y += 1500.0f;
			for (int i = 0; i < 10; ++i)
			{
				victims.push_back(make("MordorFighter", enemy, away));
			}
			step();
			for (Object *victim : victims)
			{
				DamageInfo d;
				d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
				d.m_input.m_amount = 100000.0f;
				d.m_input.m_sourceID = killer->getID();
				victim->attemptDamage(d);
			}
			step();
		}
		REQUIRE(horde.getExperienceTracker()->getRank() == 2);
		step(2);
	}

	std::string probe(Object &horde)
	{
		auto &c = contain(horde);
		std::ostringstream os;
		os << "f" << frame() << " members " << payload(horde).size() << " carrier " << c.bannerCarrier();
		if (Object *b = logic().findObjectByID(c.bannerCarrier()))
		{
			AIUpdateInterface *ai = b->getAIUpdateInterface();
			os << " moving " << (ai && ai->isMoving()) << " waitPath " << (ai && ai->mover().isWaitingForPath()) << " lastShot "
			   << (b->getWeapons() ? b->getWeapons()->firingTracker().lastShotFrame() : 0u) << " mcATTACKING " << b->testModelCondition(0x25) << " mcMOVING " << b->testModelCondition(0x3D) << " pos " << b->getPosition()->x << "," << b->getPosition()->y;
		}
		int attacking = 0;
		unsigned newest = 0;
		for (Object *m : payload(horde))
		{
			attacking += m->testStatus((unsigned)CombatNames::statuses().isAttacking) ? 1 : 0;
			if (auto *body = dynamic_cast<const ActiveBody *>(m->getBodyModule()))
			{
				if (body->lastDamageFrame() != 0xFFFFFFFFu && body->lastDamageFrame() > newest)
				{
					newest = body->lastDamageFrame();
				}
			}
		}
		const EmotionTrackerUpdate *et = EmotionTrackerUpdate::of(horde);
		os << " emotion " << (et && et->current() ? et->current()->type() : -1) << " payloadCount " << c.payloadCount() << "/" << c.getSlotCapacity()
		   << " countdown " << c.bannerCountdown();
		os << " attacking " << attacking << " newestDamage " << newest << " attacked100 " << [&] { ObjectID a = INVALID_ID; return c.attackedWithin(100, a); }();
		return os.str();
	}

	struct Spawn
	{
		unsigned frame = 0;
		ObjectID id = INVALID_ID;
		int rank = -1;
		bool fx = false;
		Coord3D at{ 0.0f, 0.0f, 0.0f };
		Coord3D carrierAt{ 0.0f, 0.0f, 0.0f };
		float facing = 0.0f, carrierFacing = 0.0f;
	};

	// steps one frame at a time until the horde has `full` members or `maxFrames` passed; every member that appears is recorded with its frame's
	// UnitSpawnFX event (an OBJECT_FX of the carrier's UnitSpawnFX list played on that member, RW 0x89A43F)
	std::vector<Spawn> refill(Object &horde, size_t full, int maxFrames, const std::function<void()> &eachFrame = {})
	{
		std::vector<Spawn> out;
		auto &c = contain(horde);
		std::vector<ObjectID> known;
		for (Object *m : payload(horde))
		{
			known.push_back(m->getID());
		}
		for (int i = 0; i < maxFrames && payload(horde).size() < full; ++i)
		{
			if (eachFrame)
			{
				eachFrame();
			}
			Object *carrier = logic().findObjectByID(c.bannerCarrier());
			REQUIRE(carrier);
			const Coord3D carrierAt = *carrier->getPosition();
			const float carrierFacing = carrier->getOrientation();
			const std::string fxName = dynamic_cast<BannerCarrierUpdate *>(carrier->findModule("BannerCarrierUpdate"))->data().m_unitSpawnFX;
			step();
			for (Object *m : payload(horde))
			{
				if (std::find(known.begin(), known.end(), m->getID()) != known.end())
				{
					continue;
				}
				known.push_back(m->getID());
				Spawn sp;
				sp.frame = frame() - 1;
				sp.id = m->getID();
				sp.rank = m->getExperienceTracker()->getRank();
				for (const FXEvent &e : logic().fxEvents().frameEvents())
				{
					sp.fx = sp.fx || (e.kind == FXEvent::OBJECT_FX && e.primary == m->getID() && *e.fxList == fxName);
				}
				sp.carrierAt = carrierAt;
				sp.carrierFacing = carrierFacing;
				sp.at = *m->getPosition();
				sp.facing = m->getOrientation();
				out.push_back(sp);
			}
		}
		return out;
	}

	void hit(Object &target, float amount)
	{
		DamageInfo d;
		d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		d.m_input.m_amount = amount;
		target.attemptDamage(d);
	}
};
} // namespace

// The owner's report, on the real game: a rank-two horde (BannerCarrierMinLevel 1: the carrier comes at rank two) fights an enemy horde with auto-acquire on,
// loses members, and then stands idle. Retail (RW 0x89ACE4) refills it one member every IdleSpawnRate (10000 ms = 50 frames) once no member was removed for
// MeleeFreeUnitSpawnTime (20000 ms = 100 frames) and none was damaged for 4 seconds; each new member is made at the carrier's matrix (RW 0x89A415), plays
// UnitSpawnFX (FX_HordeResurrection, RW 0x89A43F), takes the horde's experience pool for its template (RW 0x873BD3 .. 0x873C35: rank two, like the others) and
// is not counted as a unit built (the score keeper is off, RW 0x89A40E)
TEST_CASE("banner1 retail: a rank-two Gondor horde refills after a real fight, members at the horde's rank with UnitSpawnFX")
{
	OPENBFME_REQUIRE_START(s);
	BannerGame g;
	g.start(*s);
	Object *horde = g.make("GondorFighterHorde", g.owner, g.position);
	g.veteran(*horde);
	auto &c = g.contain(*horde);
	REQUIRE(c.bannerCarrier() != 0); // tolerance: object ids are allocated per run; the claim is that a carrier exists
	const size_t full = g.payload(*horde).size();
	REQUIRE(full == (size_t)c.getSlotCapacity());
	for (Object *m : g.payload(*horde))
	{
		REQUIRE(m->getExperienceTracker()->getRank() == 2);
	}
	MESSAGE("full " << full << " " << g.probe(*horde));
	// three orc hordes: one alone dies without costing a rank-two Gondor horde a man
	std::vector<ObjectID> foes;
	for (int i = 0; i < 3; ++i)
	{
		Coord3D foeAt = g.position;
		foeAt.x += 120.0f;
		foeAt.y += -80.0f + 80.0f * (float)i;
		foes.push_back(g.make("MordorFighterHorde", g.enemy, foeAt)->getID());
	}
	REQUIRE(g.until([&] {
		for (ObjectID id : foes)
		{
			Object *f = g.logic().findObjectByID(id);
			if (f && !f->isEffectivelyDead())
			{
				return false;
			}
		}
		return true;
	}, 3000));
	const size_t left = g.payload(*horde).size();
	MESSAGE("after the fight: " << g.probe(*horde));
	REQUIRE(left < full); // tolerance: the fight must cost members (how many is the fight's); the claim needs losses to refill
	REQUIRE(c.bannerCarrier() != 0); // tolerance: object ids are allocated per run; the claim is that a carrier exists
	// the survivors' rank: the horde may have gained a rank in the fight; its experience pool is what a new member takes (RW 0x873BD3 .. 0x873C35)
	const int survivorRank = g.payload(*horde).front()->getExperienceTracker()->getRank();
	for (Object *m : g.payload(*horde))
	{
		REQUIRE(m->getExperienceTracker()->getRank() == survivorRank);
	}
	REQUIRE(survivorRank >= 2); // tolerance: rank two or more (the fight may add a rank); the claim is the new members match the survivors
	const int built = g.owner->getScoreKeeper().unitsBuilt();
	const std::vector<BannerGame::Spawn> spawns = g.refill(*horde, full, 2000);
	MESSAGE("after the refill window: " << g.probe(*horde) << ", " << spawns.size() << " new members");
	CHECK(g.payload(*horde).size() == full);
	REQUIRE(spawns.size() == full - left);
	for (size_t i = 0; i < spawns.size(); ++i)
	{
		const BannerGame::Spawn &sp = spawns[i];
		CAPTURE(i);
		CHECK(g.logic().findObjectByID(sp.id)->getTemplate()->getName() == "GondorFighter");
		CHECK(sp.rank == survivorRank);
		CHECK(sp.fx);
		CHECK(sp.at.x == sp.carrierAt.x);
		CHECK(sp.at.y == sp.carrierAt.y);
		CHECK(sp.facing == sp.carrierFacing);
		if (i > 0)
		{
			CHECK(sp.frame - spawns[i - 1].frame == 50u); // IdleSpawnRate: one member per 10000 ms
		}
	}
	CHECK(g.owner->getScoreKeeper().unitsBuilt() == built);
}

// RW 0x86EE52 (HordeContain::attackedWithin) is the combat test: a member removed within MeleeFreeUnitSpawnTime (100 frames) or a member damaged within 4 seconds
// (20 frames) holds the refill back. HORDE-2's stand-in also held it while any member had IS_ATTACKING and for 100 frames after any damage.
TEST_CASE("banner1 retail: the refill waits for RW 0x86EE52's windows, not for IS_ATTACKING")
{
	OPENBFME_REQUIRE_START(s);
	BannerGame g;
	g.start(*s);
	g.logic().combat().setAutoAcquireEnabled(false);
	Object *horde = g.make("GondorFighterHorde", g.owner, g.position);
	g.veteran(*horde);
	auto &c = g.contain(*horde);
	REQUIRE(c.bannerCarrier() != 0); // tolerance: object ids are allocated per run; the claim is that a carrier exists
	const size_t full = g.payload(*horde).size();
	g.step(100);
	const int isAttacking = CombatNames::statuses().isAttacking;
	auto markAttacking = [&] {
		for (Object *m : g.payload(*horde))
		{
			m->setStatus((unsigned)isAttacking, true);
		}
	};
	// a member dies at frame F: no member before F + 100 (the removal window, inclusive), then the first pulse makes one, every remaining member IS_ATTACKING
	g.hit(*g.payload(*horde).front(), 100000.0f);
	const unsigned died = g.frame() - 1;
	g.step(1);
	REQUIRE(g.payload(*horde).size() == full - 1);
	std::vector<BannerGame::Spawn> spawns = g.refill(*horde, full, 400, markAttacking);
	REQUIRE(spawns.size() == 1);
	MESSAGE("died " << died << " refilled " << spawns[0].frame);
	CHECK(spawns[0].frame > died + 100); // tolerance: nothing inside the removal window (MeleeFreeUnitSpawnTime, inclusive); the pulse phase picks the frame
	CHECK(spawns[0].frame <= died + 100 + 50); // tolerance: the pulse phase (IdleSpawnRate 50 frames) decides which frame after the window it falls on
	// a second death, then a living member hit every 15 frames (inside 4 seconds) until F + 300: nothing while the hits go on, a member within one pulse after the last
	// hit's 20-frame window
	g.hit(*g.payload(*horde).front(), 100000.0f);
	const unsigned died2 = g.frame() - 1;
	unsigned lastHit = 0;
	spawns = g.refill(*horde, full, 600, [&] {
		if (g.frame() < died2 + 300 && (g.frame() - died2) % 15 == 0)
		{
			g.hit(*g.payload(*horde).back(), 1.0f);
			lastHit = g.frame();
		}
	});
	REQUIRE(spawns.size() == 1);
	MESSAGE("died " << died2 << " last hit " << lastHit << " refilled " << spawns[0].frame);
	CHECK(spawns[0].frame > lastHit + 20); // tolerance: nothing inside the 4-second damage window (inclusive); the pulse phase picks the frame
	CHECK(spawns[0].frame <= lastHit + 20 + 50); // tolerance: the pulse phase (IdleSpawnRate 50 frames) decides which frame after the window it falls on
}

// RW 0x89ADA3 .. 0x89ADC1: the horde object's emotion tracker running a POINT / FEAR / UNCONTROLLABLE_FEAR / TERROR / DOOM / BRACE_FOR_BEING_CRUSHED nugget
// (RW 0x8B50A5 -> 0x8B4F39) holds the refill back; it resumes once the nugget ends. The horde is made to point (the scripts' forced emotion), which keeps it
// in place: the carrier never has MOVING while it is held
TEST_CASE("banner1 retail: a horde running a POINT emotion gets no member until the emotion ends (RW 0x8B50A5)")
{
	OPENBFME_REQUIRE_START(s);
	BannerGame g;
	g.start(*s);
	g.logic().combat().setAutoAcquireEnabled(false);
	Object *horde = g.make("GondorFighterHorde", g.owner, g.position);
	g.veteran(*horde);
	auto &c = g.contain(*horde);
	REQUIRE(c.bannerCarrier() != 0); // tolerance: object ids are allocated per run; the claim is that a carrier exists
	const size_t full = g.payload(*horde).size();
	g.step(100);
	EmotionTrackerUpdate *tracker = EmotionTrackerUpdate::of(*horde);
	REQUIRE(tracker);
	Coord3D far = g.position;
	far.x += 400.0f;
	Object *pointedAt = g.make("MordorFighter", g.enemy, far); // the nugget needs its source (EmotionNugget::canApply, RW 0x8E12F5)
	EmotionTrackerUpdate::forceEmotion(*horde, EMOTION_POINT, 60.0f, pointedAt);
	g.step(2);
	REQUIRE(tracker->current());
	REQUIRE(tracker->current()->type() == EMOTION_POINT);
	g.hit(*g.payload(*horde).front(), 100000.0f);
	const unsigned died = g.frame() - 1;
	unsigned ended = 0;
	int stillRun = 0, longestStill = 0; // frames in a row, after the removal window and while pointing, with the carrier neither moving nor MOVING
	const std::vector<BannerGame::Spawn> spawns = g.refill(*horde, full, 800, [&] {
		const bool pointing = tracker->current() && tracker->current()->type() == EMOTION_POINT;
		if (!pointing && ended == 0)
		{
			ended = g.frame();
		}
		Object *carrier = g.logic().findObjectByID(c.bannerCarrier());
		const bool still = !carrier->testModelCondition(0x3D) && !carrier->getAIUpdateInterface()->isMoving();
		stillRun = pointing && still && g.frame() > died + 100 ? stillRun + 1 : 0;
		longestStill = std::max(longestStill, stillRun);
	});
	REQUIRE(spawns.size() == 1);
	MESSAGE("died " << died << " the emotion ended " << ended << " refilled " << spawns[0].frame);
	REQUIRE(ended > died + 100 + 50); // tolerance: the forced 60 s must outlast the removal window plus one IdleSpawnRate pulse for the hold to be seen
	MESSAGE("longest still stretch while pointing: " << longestStill << " frames");
	CHECK(longestStill >= 50); // tolerance: a full IdleSpawnRate period (50 frames) with every other gate open passed with no member: the emotion held it
	CHECK(spawns[0].frame >= ended); // tolerance: the first pulse at or after the end of the nugget makes the member
	CHECK(spawns[0].frame < ended + 50); // tolerance: the pulse phase (IdleSpawnRate 50 frames)
}

// RW 0x89A308: when the carrier's outermost container is a GARRISON with an AllowBannerSpawnUpgrade module (retail: MenGarrisonTowerExpansion, TriggeredBy
// Upgrade_MenFortressHouseOfHealing), the carrier makes members only once that module is upgraded
TEST_CASE("banner1 retail: a horde garrisoned in the Gondor tower expansion is refilled only after the tower's AllowBannerSpawnUpgrade (RW 0x89A308)")
{
	OPENBFME_REQUIRE_START(s);
	BannerGame g;
	g.start(*s);
	g.logic().combat().setAutoAcquireEnabled(false);
	Object *horde = g.make("GondorArcherHorde", g.owner, g.position);
	g.veteran(*horde);
	auto &c = g.contain(*horde);
	REQUIRE(c.bannerCarrier() != 0); // tolerance: object ids are allocated per run; the claim is that a carrier exists
	const size_t full = g.payload(*horde).size();
	Coord3D towerAt = g.position;
	towerAt.x += 250.0f;
	Object *tower = g.make("MenGarrisonTowerExpansion", g.owner, towerAt);
	REQUIRE(tower->findModule("AllowBannerSpawnUpgrade"));
	g.step(5);
	REQUIRE(horde->getAIUpdateInterface()->aiEnter(tower, CMD_FROM_PLAYER));
	REQUIRE(g.until([&] { return horde->getContainedBy() == tower && c.allMembersEntered() && !g.payload(*horde).empty(); }, 600));
	g.step(100);
	REQUIRE(g.payload(*horde).size() == full);
	Object *carrier = g.logic().findObjectByID(c.bannerCarrier());
	REQUIRE(carrier);
	CHECK(carrier->getContainedBy() == horde);
	g.hit(*g.payload(*horde).front(), 100000.0f);
	g.step(2);
	REQUIRE(g.payload(*horde).size() == full - 1);
	// 400 frames: the removal window (100) and six IdleSpawnRate pulses (50) pass with the tower not upgraded
	CHECK(g.refill(*horde, full, 400).empty());
	const UpgradeModule *allow = dynamic_cast<const UpgradeModule *>(tower->findModule("AllowBannerSpawnUpgrade"));
	REQUIRE(allow);
	CHECK_FALSE(allow->isAlreadyUpgraded());
	tower->giveUpgrade("Upgrade_MenFortressHouseOfHealing");
	g.step(1);
	CHECK(allow->isAlreadyUpgraded());
	MESSAGE("upgraded: " << g.probe(*horde));
	{
		Object *b = g.logic().findObjectByID(c.bannerCarrier());
		MESSAGE("carrier gates: mc45 " << b->testModelCondition(0x45) << " st57 " << b->testStatus(0x57) << " mc220 " << b->testModelCondition(0x220)
			<< " st3E " << b->testStatus(0x3E) << " mc25 " << b->testModelCondition(0x25) << " mc3D " << b->testModelCondition(0x3D) << " container "
			<< (b->getContainedBy() ? b->getContainedBy()->getTemplate()->getName() : std::string("none")) << " hordeIn "
			<< (horde->getContainedBy() ? horde->getContainedBy()->getTemplate()->getName() : std::string("none")) << " freeSlots " << c.freeSlotIndices().size()
			<< " spawned " << dynamic_cast<BannerCarrierUpdate *>(b->findModule("BannerCarrierUpdate"))->membersSpawned());
	}
	const std::vector<BannerGame::Spawn> spawns = g.refill(*horde, full, 60); // one IdleSpawnRate pulse (50 frames)
	CHECK(spawns.size() == 1);
	MESSAGE("garrisoned: " << g.probe(*horde));
	// review r1: the tower saves (Object::xfer refuses a module class without its own xfer) and the upgraded state round-trips
	XferSave saved;
	CHECK_NOTHROW(tower->xfer(saved));
	XferSave moduleSaved;
	const_cast<UpgradeModule *>(allow)->xfer(moduleSaved);
	Object *other = g.make("MenGarrisonTowerExpansion", g.owner, g.position);
	UpgradeModule *otherAllow = dynamic_cast<UpgradeModule *>(other->findModule("AllowBannerSpawnUpgrade"));
	REQUIRE(otherAllow);
	REQUIRE_FALSE(otherAllow->isAlreadyUpgraded());
	XferLoad load(moduleSaved.data());
	otherAllow->xfer(load);
	CHECK(otherAllow->isAlreadyUpgraded());
}

// review r1 follow-up: RW 0x804F02 hands down the pool's entries with their remaining time; an entry already past its expiry (kept by the port, whose expiry
// removal is not ported, S-633) is not handed down, a live one is with what is left of it
TEST_CASE("banner1 retail: the attribute modifier hand-down skips expired entries and hands down live ones (RW 0x804F02)")
{
	OPENBFME_REQUIRE_START(s);
	BannerGame g;
	g.start(*s);
	g.logic().combat().setAutoAcquireEnabled(false);
	Object *horde = g.make("GondorFighterHorde", g.owner, g.position);
	g.step(2);
	auto *pool = dynamic_cast<AttributeModifierPool *>(horde->findModule("AttributeModifierPoolUpdate"));
	REQUIRE(pool);
	REQUIRE(pool->add("GenericBuff", 5));
	REQUIRE(pool->add("StandardDebuff", 100));
	g.step(10);
	Coord3D away = g.position;
	away.y += 600.0f;
	Object *target = g.make("GondorFighter", g.owner, away);
	g.step(1);
	auto *targetPool = dynamic_cast<AttributeModifierPool *>(target->findModule("AttributeModifierPoolUpdate"));
	REQUIRE(targetPool);
	REQUIRE_FALSE(targetPool->hasList("GenericBuff"));
	REQUIRE_FALSE(targetPool->hasList("StandardDebuff"));
	pool->handDownTo(*target);
	CHECK_FALSE(targetPool->hasList("GenericBuff"));
	CHECK(targetPool->hasList("StandardDebuff"));
}
