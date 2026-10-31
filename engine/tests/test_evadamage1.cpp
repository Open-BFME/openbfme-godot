// OpenBFME unit tests (lane EVADAMAGE-1): the Eva half of RotWK's drawable damage notice, RW 0x67B4B7 (GameLogic/Object/EvaDamageNotice.h,
// GameClient/EvaDamage.h), with the retail objects and Eva.ini: one test per row (Ambushed, ByFire, FromShroudedSource, DamagedOwner and the
// SecondDamageFarFromFirst replacement) and the gates that keep Eva quiet.

#include "doctest.h"

#include "CreepTestUtil.h"
#include "HudTestUtil.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/Eva.h"
#include "GameClient/EvaDamage.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LiveGameAudio.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/EvaDamageNotice.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/System/ShroudManager.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
// the retail audio data with TheEva installed on this thread (AudioApi::reportEvaDamage runs here)
struct EvaAudio
{
	INIEnvironment env;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> manager;
	std::unique_ptr<Eva> eva;
	EvaAudio(ArchiveFileSystem &fs, const Player &local)
	{
		env.fileSystem = &fs;
		ini.registerBlocks(env.blocks);
		INI parser(env);
		ini.loadAll(parser);
		ini.loadEva(parser);
		cache = std::make_unique<AudioAssetCache>(&fs, 16u * 1024u * 1024u);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		manager = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::RotWK_GameDat_LCG, 1u);
		manager->enableEventLog(true);
		AudioApi::install(manager.get());
		eva = std::make_unique<Eva>(ini, *manager);
		eva->setLocalSide(local.getSide());
		eva->setLocalPlayerIndex(local.getPlayerIndex());
		AudioApi::installEva(eva.get());
	}
	~EvaAudio()
	{
		AudioApi::installEva(nullptr);
		AudioApi::install(nullptr);
	}
	// the Eva clock and queue moved on by `ms` (the event that waits plays: "about to play" ends)
	void advance(double ms)
	{
		for (double t = 0.0; t < ms; t += 50.0)
		{
			now += 50.0;
			device->setTime(now);
			manager->update(now);
			eva->update(now);
		}
	}
	double now = 0.0;
};

// a world with the retail Eva, the creeps gone, a local GondorFighter near the local base and the enemy's MordorFighter next to it
struct Scene
{
	hudtest::Rig rig;
	EvaAudio audio;
	Object *victim = nullptr;
	Object *attacker = nullptr;
	Coord3D base;
	Coord3D towardCentre; ///< unit vector from the base to the map's centre
	explicit Scene(hudtest::SharedWorld &s)
		: rig(s)
		, audio(*s.mount->fs, *rig.local)
	{
		creeptest::removeCreeps(rig.logic());
		double bx = 0.0, by = 0.0;
		int n = 0;
		for (Object *o = rig.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == rig.local)
			{
				bx += o->getPosition()->x;
				by += o->getPosition()->y;
				++n;
			}
		}
		REQUIRE(n > 0); // tolerance: the local player owns its start objects; their count is not the claim
		base = Coord3D{ (float)(bx / n), (float)(by / n), 0.0f };
		const ShroudManager *sm = rig.logic().shroud();
		REQUIRE(sm != nullptr);
		const float cx = sm->originX() + sm->cellSize() * (float)sm->cellCountX() * 0.5f, cy = sm->originY() + sm->cellSize() * (float)sm->cellCountY() * 0.5f;
		const float dx = cx - base.x, dy = cy - base.y, d = std::sqrt(dx * dx + dy * dy);
		towardCentre = Coord3D{ dx / d, dy / d, 0.0f };
		victim = make("GondorFighter", rig.index(), along(300.0f));
		attacker = make("MordorFighter", enemy(), along(360.0f));
		rig.frame(2);
	}
	int enemy() { return rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex(); }
	Coord3D along(float d) const { return Coord3D{ base.x + towardCentre.x * d, base.y + towardCentre.y * d, 0.0f }; }
	Object *make(const char *tmpl, int player, const Coord3D &at)
	{
		std::string err;
		Object *o = rig.game->createObject(tmpl, player, at, 0.0f, &err);
		REQUIRE_MESSAGE(o != nullptr, err);
		return o;
	}
	// a hit of `type` from `source` on `on`, then one logic frame (the event reaches the client and TheEva)
	void hit(Object *on, Object *source, DamageType type = DAMAGE_UNRESISTABLE, float amount = 1.0f)
	{
		DamageInfo d;
		d.m_input.m_amount = amount;
		d.m_input.m_damageType = type;
		d.m_input.m_sourceID = source ? source->getID() : INVALID_ID;
		d.m_input.m_sourcePlayerMask = source && source->getControllingPlayer() ? 1u << source->getControllingPlayer()->getPlayerIndex() : 0u;
		on->attemptDamage(d);
		rig.frame(1);
	}
	const EvaDamage::Stats &stats() { return rig.game->drawables().evaDamageStats(); }
};

std::string field(const char *tmpl, const char *name)
{
	const ThingTemplate *tt = hudtest::shared().world->things().findTemplate(tmpl);
	REQUIRE(tt != nullptr);
	return EvaDamage::templateEvent(*tt, name);
}
} // namespace

TEST_CASE("evadamage1 retail: a unit's Eva damage rows (standardunitevaevents.inc) and RW 0x67B4B7's constants")
{
	if (!hudtest::haveWorld("evadamage1 rows"))
	{
		return;
	}
	CHECK(field("GondorFighter", "EvaEventDamagedOwner") == "UnitUnderAttack");
	CHECK(field("GondorFighter", "EvaEventAmbushed") == "UnitAmbushed");
	CHECK(field("GondorFighter", "EvaEventDamagedByFireOwner") == "GenericUnitBeingBurned");
	CHECK(field("GondorFighter", "EvaEventDamagedFromShroudedSourceOwner") == "UnitUnderAttackFromShroudedUnit");
	CHECK(field("GondorFighter", "EvaEventSecondDamageFarFromFirstOwner") == "UnitUnderAttackAgain");
	const ThingTemplate *tt = hudtest::shared().world->things().findTemplate("GondorFighter");
	REQUIRE(tt != nullptr);
	CHECK(EvaDamage::scanRange(*tt) == 600.0f);
	CHECK(EvaDamage::timeoutFrames(*tt) == 155u); // 31000 ms: ceil(31000 x 0.005f) (parseDurationUnsignedInt RW 0x73A429)
	// a template without the rows keeps the ctor's (RW 0x73FB7A): no events, 200.0, 150 frames
	const ThingTemplate *keep = hudtest::shared().world->things().findTemplate("GondorKeep");
	REQUIRE(keep != nullptr);
	CHECK(EvaDamage::templateEvent(*keep, "EvaEventAmbushed").empty());
	CHECK(EvaDamage::templateEvent(*keep, "EvaEventSecondDamageFarFromFirstOwner").empty());
	CHECK(EvaDamage::scanRange(*keep) == 200.0f);
	CHECK(EvaDamage::timeoutFrames(*keep) == 150u);
	// the model conditions RW 0x67B5A7 / 0x67B5B8 / 0x67B5C6 push, and the fire type RW 0x67B5E9 compares
	CHECK(Construction::modelConditionIndex("ENGAGED") == 0x9D);
	CHECK(Construction::modelConditionIndex("EMOTION_ALERT") == 0x3F);
	CHECK(Construction::modelConditionIndex("ATTACKING") == 0x25);
	CHECK((int)DAMAGE_LOGICAL_FIRE == 0x17);
	// the port's inferences are reported (stop S-7090)
	const std::vector<std::string> stops = EvaDamage::acceptanceStops();
	REQUIRE(stops.size() == 1);
	CHECK(stops[0].rfind("[S-7090]", 0) == 0);
}

TEST_CASE("evadamage1 retail: a hit from a visible enemy says UnitUnderAttack; a second hit far from the first, while it waits, says UnitUnderAttackAgain")
{
	if (!hudtest::haveWorld("evadamage1 damaged"))
	{
		return;
	}
	Scene s(hudtest::shared());
	s.hit(s.victim, s.attacker);
	CHECK(s.stats().notices == 1);
	CHECK(s.stats().lastEvent == "UnitUnderAttack");
	CHECK(s.stats().damaged == 1);
	CHECK(s.audio.eva->isEventAboutToPlay(s.audio.eva->findEvent("UnitUnderAttack")));
	// the victim's far frame: the hit's frame + 155 (RW 0x671DFD over the scan: the victim is within its own 600)
	const Drawable *d = s.rig.game->drawables().findByObject(s.victim->getID());
	REQUIRE(d != nullptr);
	const UnsignedInt farFrame = d->evaDamageFarFrame();
	CHECK(farFrame == s.rig.logic().getFrame() - 1 + 155u);
	// the scan's owner filter (RW 0x660AFE): the enemy next to it (the same UnitUnderAttack row, another owner) keeps 0
	const Drawable *ad = s.rig.game->drawables().findByObject(s.attacker->getID());
	REQUIRE(ad != nullptr);
	CHECK(ad->evaDamageFarFrame() == 0u);
	// a second hit on the same unit: its far frame is ahead, so the same event again (dropped by Eva: already waiting)
	s.hit(s.victim, s.attacker);
	CHECK(s.stats().lastEvent == "UnitUnderAttack");
	CHECK(s.stats().secondFar == 0);
	// a unit 800 away (outside the 600 scan): its far frame is 0, the event waits -> the second event
	Object *far = s.make("GondorFighter", s.rig.index(), s.along(1100.0f));
	s.rig.frame(1);
	s.hit(far, s.attacker);
	CHECK(s.stats().lastEvent == "UnitUnderAttackAgain");
	CHECK(s.stats().secondFar == 1);
	// once the first played and its TimeBetweenEventsMS ran out, a far hit is an ordinary one again
	s.audio.advance(120000.0);
	Object *farther = s.make("GondorFighter", s.rig.index(), s.along(1900.0f));
	s.rig.frame(1);
	s.hit(farther, s.attacker);
	CHECK(s.stats().lastEvent == "UnitUnderAttack");
}

TEST_CASE("evadamage1 retail: a hit from a stealthed enemy is an ambush (UnitAmbushed); not while the victim is ATTACKING")
{
	if (!hudtest::haveWorld("evadamage1 ambush"))
	{
		return;
	}
	Scene s(hudtest::shared());
	const int stealthed = ObjectTemplateInfoBuilder::objectStatusIndex("STEALTHED");
	REQUIRE(stealthed == 0xF);
	s.attacker->setStatus((unsigned)stealthed, true);
	CHECK_FALSE(EvaDamage::isVisibleTo(s.rig.logic(), *s.attacker, s.rig.local, true));
	CHECK_FALSE(EvaDamage::isVisibleTo(s.rig.logic(), *s.attacker, s.rig.local, false));
	s.hit(s.victim, s.attacker);
	CHECK(s.stats().lastEvent == "UnitAmbushed");
	CHECK(s.stats().ambushed == 1);
	// the victim fighting back (ATTACKING, RW 0x694154(0x25)): an enemy it cannot see is no ambush; the shroud row needs the attacker in the shroud, so
	// the plain row
	Object *busy = s.make("GondorFighter", s.rig.index(), s.along(1500.0f));
	busy->setModelConditionState(Construction::modelConditionIndex("ATTACKING"), true);
	s.rig.frame(1);
	s.hit(busy, s.attacker);
	CHECK(s.stats().ambushed == 1);
	CHECK(s.stats().lastEvent == "UnitUnderAttack");
	// seen (DETECTED): an enemy in sight, no ambush (RW 0x694CCC mode 1 true)
	s.attacker->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("DETECTED"), true);
	CHECK(EvaDamage::isVisibleTo(s.rig.logic(), *s.attacker, s.rig.local, true));
	// an ally is never "visible as an enemy" (RW 0x694CCC mode 1: ALLIES first)
	CHECK_FALSE(EvaDamage::isVisibleTo(s.rig.logic(), *s.victim, s.rig.local, true));
	CHECK(EvaDamage::isVisibleTo(s.rig.logic(), *s.victim, s.rig.local, false));
}

TEST_CASE("evadamage1 retail: LOGICAL_FIRE damage says GenericUnitBeingBurned and has no second event; FLAME is an ordinary hit")
{
	if (!hudtest::haveWorld("evadamage1 fire"))
	{
		return;
	}
	Scene s(hudtest::shared());
	s.hit(s.victim, s.attacker, DAMAGE_LOGICAL_FIRE);
	CHECK(s.stats().lastEvent == "GenericUnitBeingBurned");
	CHECK(s.stats().byFire == 1);
	// far away while it waits: still the fire event (ByFire has no second, RW 0x67B5F9)
	Object *far = s.make("GondorFighter", s.rig.index(), s.along(1100.0f));
	s.rig.frame(1);
	s.hit(far, s.attacker, DAMAGE_LOGICAL_FIRE);
	CHECK(s.stats().lastEvent == "GenericUnitBeingBurned");
	CHECK(s.stats().secondFar == 0);
	Object *other = s.make("GondorFighter", s.rig.index(), s.along(1900.0f));
	s.rig.frame(1);
	s.hit(other, s.attacker, DAMAGE_FLAME);
	CHECK(s.stats().lastEvent == "UnitUnderAttack");
}

TEST_CASE("evadamage1 retail: a hit from an enemy in the shroud says UnitUnderAttackFromShroudedUnit (the attacker's drawable +0x440)")
{
	if (!hudtest::haveWorld("evadamage1 shrouded"))
	{
		return;
	}
	Scene s(hudtest::shared());
	ShroudManager *sm = s.rig.logic().shroud();
	sm->setDisplayed(true); // as a game started from a game setup shows it (VIS-1: RW 0x62F91A)
	// an enemy archer far beyond the local player's sight (the map's far side)
	Object *archer = s.make("MordorFighter", s.enemy(), s.along(5000.0f));
	s.rig.frame(3);
	const Drawable *ad = s.rig.game->drawables().findByObject(archer->getID());
	REQUIRE(ad != nullptr);
	REQUIRE(ad->shroudLook().fullyObscured);
	s.hit(s.victim, archer);
	CHECK(s.stats().lastEvent == "UnitUnderAttackFromShroudedUnit");
	CHECK(s.stats().shrouded == 1);
	// the attacker in sight: the plain row
	s.audio.advance(120000.0);
	s.hit(s.victim, s.attacker);
	CHECK(s.stats().lastEvent == "UnitUnderAttack");
}

TEST_CASE("evadamage1 retail: heals, penalties, FADED deaths, sub type 2, zero damage and the player's own hits say nothing")
{
	if (!hudtest::haveWorld("evadamage1 gates"))
	{
		return;
	}
	Scene s(hudtest::shared());
	s.hit(s.victim, s.attacker, DAMAGE_HEALING);
	s.hit(s.victim, s.attacker, DAMAGE_PENALTY);
	s.hit(s.victim, s.attacker, DAMAGE_UNRESISTABLE, 0.0f);
	{
		DamageInfo d;
		d.m_input.m_amount = 1.0f;
		d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		d.m_input.m_sourceID = s.attacker->getID();
		d.m_input.m_deathType = (DeathType)0x16; // FADED
		s.victim->attemptDamage(d);
		d.m_input.m_deathType = DEATH_NORMAL;
		d.m_input.m_damageSubType = 2;
		s.victim->attemptDamage(d);
		d.m_input.m_damageSubType = 0;
		d.m_input.m_shouldPlayUnderAttackEva = false; // the hit's report byte (+0x25)
		s.victim->attemptDamage(d);
		d.m_input.m_shouldPlayUnderAttackEva = true;
		d.m_input.m_sourcePlayerMask = 1u << s.rig.index(); // the owner's own bit
		s.victim->attemptDamage(d);
		s.rig.frame(1);
	}
	// an enemy's object hit: not the local player's (RW 0x6AA7B2)
	s.hit(s.attacker, s.victim);
	CHECK(s.stats().notices == 0);
	CHECK(s.audio.eva->counters().reported == 0);
	// the same gates let an ordinary hit through
	s.hit(s.victim, s.attacker);
	CHECK(s.stats().notices == 1);
	CHECK(s.stats().reported == 1);
}

TEST_CASE("evadamage1 retail: the far frame is due on its own frame (RW 0x67B64F: only a far frame above the logic frame keeps the first event)")
{
	if (!hudtest::haveWorld("evadamage1 far frame"))
	{
		return;
	}
	Scene s(hudtest::shared());
	s.rig.logic().destroyObject(s.attacker); // no fight in the scene: every notice below is the test's own
	// a lone unit and a source far from each other (no other hit raises the lone unit's far frame)
	Object *lone = s.make("GondorFighter", s.rig.index(), s.along(1500.0f));
	Object *source = s.make("MordorFighter", s.enemy(), s.along(-1500.0f));
	s.rig.frame(1);
	const UnsignedInt first = s.rig.logic().getFrame();
	s.hit(lone, source);
	REQUIRE(s.stats().lastEvent == "UnitUnderAttack");
	const Drawable *d = s.rig.game->drawables().findByObject(lone->getID());
	REQUIRE(d != nullptr);
	REQUIRE(d->evaDamageFarFrame() == first + 155u);
	while (s.rig.logic().getFrame() < first + 155u)
	{
		s.rig.frame(1);
	}
	REQUIRE(s.rig.logic().getFrame() == first + 155u);
	s.hit(lone, source); // + 0x388 == frame: due; UnitUnderAttack still waits (the Eva clock did not move)
	CHECK(s.stats().notices == 2);
	CHECK(s.stats().lastEvent == "UnitUnderAttackAgain");
}
