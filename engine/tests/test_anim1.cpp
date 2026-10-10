// OpenBFME retail tests for lane ANIM-1: units animate like the original in combat (FEEDBACK-2 G1, G3, G5, G8). They run only when ROTWK_INSTALL and
// BFME2_INSTALL are set (otherwise SKIP): pure RotWK 2.01 + BFME2 1.06.
//   G1  a melee horde that reaches a building or a horde attacks without MOVING (the 0xE1 / 0xE2 / approach onExit, RW 0x74933F / 0x74B716 -> 0x748D8A)
//   G3  the drawable sees one model condition change per logic frame (RW 0x679512 / 0x6759C4) plus the pre-fire flush (RW 0x69213E -> 0x67449C)
//   G5  the troll keeps the weapon whose wind-up it began (RW 0x6C8C72: PRE_ATTACK is ready), never bashes infantry (OnlyAgainst, RW 0x6C8D21), strikes
//       when the pre-attack ends and plays its swing over the weapon's cycle (UseWeaponTiming, RW 0x4BEE31)
//   G8  the Rohirrim's bow mode sets the model condition WEAPONSET_TOGGLE_1 (RW 0x691059 / 0xC16958): the bow model, its shooting states standing and
//       riding, the arrow leaving the bow's FIREAROWTIP bone
#include "doctest.h"
#include "HudTestUtil.h"
#include "StructureArena.h"

#include "GameClient/ClientEvents.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/WeaponState.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
bool has(const Object &o, const char *condition)
{
	return o.testModelCondition(CombatNames::modelCondition(condition));
}

std::vector<Object *> membersOf(Object &horde)
{
	std::vector<Object *> out;
	if (ContainModuleInterface *c = horde.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			out.assign(items->begin(), items->end());
		}
	}
	return out;
}

// G1: frames in which a member is ATTACKING with MOVING set while it stands where it stood a frame before (the run cycle on the spot)
bool hasStopLine(const std::vector<std::string> &stops, const char *prefix)
{
	for (const std::string &l : stops)
	{
		if (l.rfind(prefix, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

struct TreadmillCount
{
	int attackingFrames = 0;
	int treadmillFrames = 0;
	bool meleeExitStop = false; // [S-1581]
};

TreadmillCount meleeHordeAttack(const char *target, float targetX)
{
	using namespace structtest;
	SharedWorld &s = shared();
	Arena a(s, "FactionMen", "FactionMordor");
	Object *horde = a.place("GondorFighterHorde", 0, 500.0f, 500.0f);
	Object *b = a.place(target, 1, targetX, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	if (b->getAIUpdateInterface())
	{
		b->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	}
	REQUIRE(horde->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER));
	std::vector<ObjectID> ids;
	for (Object *m : membersOf(*horde))
	{
		ids.push_back(m->getID());
	}
	std::map<ObjectID, Coord3D> last;
	TreadmillCount c;
	for (int f = 0; f < 100; ++f)
	{
		a.logic.runLogicFrame();
		for (ObjectID id : ids)
		{
			Object *m = a.logic.findObjectByID(id);
			if (!m || m->isEffectivelyDead())
			{
				continue;
			}
			const Coord3D p = *m->getPosition();
			const auto it = last.find(id);
			if (has(*m, "ATTACKING"))
			{
				++c.attackingFrames;
				if (has(*m, "MOVING") && it != last.end() && it->second.x == p.x && it->second.y == p.y)
				{
					++c.treadmillFrames;
				}
			}
			last[id] = p;
		}
	}
	c.meleeExitStop = hasStopLine(a.logic.report().stops, "[S-1581]");
	return c;
}
} // namespace

TEST_CASE("anim1 retail G1: a Gondor horde that reaches a barracks attacks standing, without MOVING (no run cycle on the spot)")
{
	if (!hudtest::haveWorld("anim1 retail"))
	{
		return;
	}
	const TreadmillCount c = meleeHordeAttack("MordorBarracks", 700.0f);
	std::printf("  info: G1 building: %d attacking member frames, %d with MOVING standing still\n", c.attackingFrames, c.treadmillFrames);
	CHECK(c.attackingFrames > 100);
	CHECK(c.treadmillFrames == 0);
}

TEST_CASE("anim1 retail G1: a Gondor horde that reaches an orc horde fights standing, without MOVING")
{
	if (!hudtest::haveWorld("anim1 retail"))
	{
		return;
	}
	const TreadmillCount c = meleeHordeAttack("MordorFighterHorde", 700.0f);
	std::printf("  info: G1 horde: %d attacking member frames, %d with MOVING standing still\n", c.attackingFrames, c.treadmillFrames);
	CHECK(c.attackingFrames > 50);
	CHECK(c.treadmillFrames == 0);
	CHECK_FALSE(c.meleeExitStop); // S-1581 is closed (lane DECOMP-1): RW 0x748650 is ported as RW 0x694569
}

TEST_CASE("anim1 retail G5: the mountain troll strikes when its punch's wind-up ends, never switches to the bash against infantry and holds its follow-through")
{
	if (!hudtest::haveWorld("anim1 retail"))
	{
		return;
	}
	using namespace structtest;
	SharedWorld &s = shared();
	Arena a(s, "FactionMordor", "FactionMen");
	Object *troll = a.place("MordorMountainTroll", 0, 500.0f, 500.0f);
	Object *b = a.place("GondorFighterHorde", 1, 600.0f, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	b->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	REQUIRE(troll->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER));
	ObjectWeapons *w = troll->getWeapons();
	REQUIRE(w != nullptr);
	Weapon *punch = w->weaponInSlot(0);
	Weapon *bash = w->weaponInSlot(1);
	REQUIRE(punch != nullptr);
	REQUIRE(bash != nullptr);
	CHECK(punch->getTemplate()->getName() == "MordorCaveTrollPunch");
	CHECK(bash->getTemplate()->getName() == "MordorCaveTrollBash");
	int strikes = 0, earlyStrikes = 0, movedWhileSwinging = 0, slotChangesInSwing = 0;
	std::uint32_t lastFire = punch->lastFireFrame();
	Coord3D lastPos = *troll->getPosition();
	int lastSlot = w->curSlot();
	bool lastSwinging = false;
	for (int f = 0; f < 80; ++f)
	{
		a.logic.runLogicFrame();
		const std::uint32_t now = a.logic.getFrame();
		const Weapon *cur = w->currentWeapon();
		const int status = w->currentStatus();
		const bool swinging = status == WEAPON_PRE_ATTACK || status == WEAPON_FIRING;
		if (punch->lastFireFrame() != lastFire)
		{
			lastFire = punch->lastFireFrame();
			++strikes;
			earlyStrikes += now < punch->whenPreAttackFinished() ? 1 : 0; // the punch lands when its wind-up ends (RW 0x6CD142 PRE_ATTACK until +0x1C)
		}
		const Coord3D p = *troll->getPosition();
		if (swinging && lastSwinging && (p.x != lastPos.x || p.y != lastPos.y))
		{
			++movedWhileSwinging;
		}
		if (lastSwinging && w->curSlot() != lastSlot)
		{
			++slotChangesInSwing;
		}
		(void)cur;
		lastPos = p;
		lastSlot = w->curSlot();
		lastSwinging = swinging;
	}
	std::printf("  info: G5: %d punches (%d before the wind-up ended), bash fired at frame %u, %d frames moved in a swing, %d slot changes in a swing\n", strikes,
		earlyStrikes, bash->lastFireFrame(), movedWhileSwinging, slotChangesInSwing);
	CHECK(strikes >= 2);
	CHECK(earlyStrikes == 0);
	CHECK(bash->lastFireFrame() == 0u); // OnlyAgainst = SECONDARY STRUCTURE BLOCKING_GATE
	CHECK(movedWhileSwinging == 0);
	CHECK(slotChangesInSwing == 0);
	CHECK(hasStopLine(a.logic.report().stops, "[S-1582]")); // what stays unported of RW 0x6C8A4E is reported (lane DECOMP-1)
}

namespace
{
const W3DScriptedModelDraw *modelDrawOf(const Drawable *d)
{
	if (!d)
	{
		return nullptr;
	}
	for (const DrawEntry &e : d->entries())
	{
		if (e.draw && e.draw->currentModelState() && e.draw->currentModel())
		{
			return e.draw.get();
		}
	}
	return nullptr;
}

const W3DScriptedModelDraw *modelDrawOf(hudtest::Rig &r, const Object &o)
{
	return modelDrawOf(r.game->drawables().findByObject(o.getID()));
}

size_t countEntries(const W3DScriptedModelDraw &d, size_t from)
{
	size_t n = 0;
	for (size_t i = from; i < d.log().size(); ++i)
	{
		n += d.log()[i].rfind("enter ", 0) == 0 ? 1u : 0u;
	}
	return n;
}

Coord3D centre(hudtest::Rig &r, float clear)
{
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	return r.freeSpot(mx * 0.5f, my * 0.5f, clear);
}

// the arena with a client: the logic's events reach a DrawableManager after every logic frame (LiveGame::publish / present without the threads), and the
// animations advance by one logic frame of render time (200 ms)
struct DrawArena
{
	structtest::Arena a;
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	DrawableManager drawables;
	ClientEventRecorder recorder;

	DrawArena(const char *factionA, const char *factionB)
		: a(structtest::shared(), factionA, factionB)
		, source(*structtest::shared().mount->fs)
		, assets(source)
		, drawables(assets, a.logic)
		, recorder(a.logic)
	{
		a.logic.setClientHooks(&recorder);
	}
	~DrawArena() { a.logic.setClientHooks(nullptr); }
	void frame()
	{
		a.logic.runLogicFrame();
		drawables.applyEvents(recorder.take());
		drawables.advance(200.0);
	}
	const Drawable *drawableOf(const Object &o) const { return drawables.findByObject(o.getID()); }
};
} // namespace

TEST_CASE("anim1 retail G3: an archer's drawable selects its animation state once per logic frame (plus the pre-fire flush), never an in-between state")
{
	if (!hudtest::haveWorld("anim1 retail"))
	{
		return;
	}
	DrawArena w("FactionMen", "FactionMordor");
	Object *archer = w.a.place("GondorArcher", 0, 500.0f, 500.0f);
	Object *target = w.a.place("MordorFighter", 1, 700.0f, 500.0f);
	w.frame();
	w.frame();
	target->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(target, CMD_FROM_PLAYER));
	const Drawable *dr = w.drawableOf(*archer);
	const W3DScriptedModelDraw *d = modelDrawOf(dr);
	REQUIRE(d != nullptr);
	const ObjectID targetId = target->getID();
	size_t maxPerFrame = 0, frames = 0, firingFrames = 0, dirty = 0, entries = 0;
	for (int f = 0; f < 60 && w.a.logic.findObjectByID(targetId); ++f)
	{
		const size_t before = d->log().size();
		w.frame();
		const size_t n = countEntries(*d, before);
		entries += n;
		maxPerFrame = n > maxPerFrame ? n : maxPerFrame;
		++frames;
		firingFrames += has(*archer, "FIRING_OR_RELOADING_A") || has(*archer, "PREATTACK_A") ? 1u : 0u;
		dirty += dr->modelConditionsDirty() ? 1u : 0u; // every logic frame ends with the drawable's flags flushed (RW 0x6759C4)
	}
	std::printf("  info: G3: %zu frames (%zu firing), %zu animation state selections, at most %zu in one frame\n", frames, firingFrames, entries, maxPerFrame);
	CHECK(firingFrames > 10);
	CHECK(maxPerFrame <= 2u);
	CHECK(dirty == 0u);
	CHECK(hasStopLine(w.drawables.report().stops, "[S-1583]")); // the flush points not reproduced
}

TEST_CASE("anim1 retail G5: a melee swing plays over its weapon cycle (UseWeaponTiming: the pre-attack plus FiringDuration, RW 0x4BEE31)")
{
	if (!hudtest::haveWorld("anim1 retail"))
	{
		return;
	}
	DrawArena w("FactionMen", "FactionMordor");
	// GondorFighter's FIRING_OR_PREATTACK_A animation GUManMocap_ATKA has UseWeaponTiming = Yes
	Object *fighter = w.a.place("GondorFighter", 0, 400.0f, 500.0f);
	Object *target = w.a.place("MordorFighter", 1, 700.0f, 500.0f);
	w.frame();
	w.frame();
	target->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	REQUIRE(fighter->getAIUpdateInterface()->aiAttackObject(target, CMD_FROM_PLAYER));
	const Drawable *dr = w.drawableOf(*fighter);
	const W3DScriptedModelDraw *d = modelDrawOf(dr);
	REQUIRE(d != nullptr);
	int swingFrames = 0, timedFrames = 0;
	float measured = -1.0f, expected = -1.0f;
	std::string prevClip;
	float prevFrame = 0.0f;
	const ObjectID targetId = target->getID();
	for (int f = 0; f < 120 && w.a.logic.findObjectByID(targetId); ++f)
	{
		w.frame();
		int cycle = -1;
		const bool logicTiming = fighter->getWeapons()->drawWeaponTimingFrames(cycle);
		const W3DDrawFrame fr = d->frame();
		const W3DDrawTrack &t = fr.tracks[fr.trackCount > 1 ? 1 : 0];
		const bool swing = t.anim && t.clipName.find("_ATKA") != std::string::npos && has(*fighter, "FIRING_OR_PREATTACK_A");
		if (swing)
		{
			++swingFrames;
			timedFrames += logicTiming && cycle > 0 && dr->weaponTimingFrames() == cycle ? 1 : 0;
			if (t.clipName == prevClip && measured < 0.0f && t.frame > prevFrame && cycle > 0)
			{
				const float natural = (float)t.anim->Get_Num_Frames() * 1000.0f / t.anim->Get_Frame_Rate();
				expected = (float)(int)(natural * 0.005f) / (float)cycle;
				measured = (t.frame - prevFrame) / (t.anim->Get_Frame_Rate() * 0.2f); // one logic frame of render time (200 ms)
			}
			prevClip = t.clipName;
			prevFrame = t.frame;
		}
		else
		{
			prevClip.clear();
		}
	}
	bool unsourced = false;
	for (const W3DStopHit &h : d->stops())
	{
		unsourced = unsourced || h.Message.find("no weapon timing source") != std::string::npos;
	}
	std::printf("  info: G5 draw: %d swing frames, %d with the logic's weapon cycle, speed factor %.3f (expected %.3f)\n", swingFrames, timedFrames, measured, expected);
	CHECK(swingFrames > 0);
	CHECK(timedFrames == swingFrames);
	CHECK_FALSE(unsourced);
	REQUIRE(measured > 0.0f);
	CHECK(measured == doctest::Approx(expected).epsilon(0.02));
}

TEST_CASE("anim1 retail G8: the Rohirrim's bow mode shows the bow: WEAPONSET_TOGGLE_1 and the swap condition, the shooting animations standing and riding, the arrow on the bow's tip")
{
	if (!hudtest::haveWorld("anim1 retail"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig r(s);
	const Coord3D c0 = centre(r, 900.0f);
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	Object *horde = r.make("RohanRohirrimHorde", c0.x, c0.y, enemy);
	Object *target = r.make("IsengardFighterHorde", c0.x + 250.0f, c0.y);
	r.frame(3);
	const int bit = CombatNames::weaponSetBit("WEAPONSET_TOGGLE_1");
	horde->getWeapons()->setWeaponSetFlag(bit, true);
	std::vector<Object *> members = membersOf(*horde);
	REQUIRE(!members.empty());
	for (Object *m : members)
	{
		m->getWeapons()->setWeaponSetFlag(bit, true);
	}
	Object *m = members.front();
	// RW 0x691059: the model condition of the flag (RW 0xC16958: WEAPONSET_TOGGLE_1 -> 301) and SWAPPING_TO_WEAPONSET_1 for 5 frames
	CHECK(has(*m, "WEAPONSET_TOGGLE_1"));
	CHECK(has(*m, "SWAPPING_TO_WEAPONSET_1"));
	int swapFrames = 0;
	for (int f = 0; f < 8; ++f)
	{
		r.frame(1);
		swapFrames += has(*m, "SWAPPING_TO_WEAPONSET_1") ? 1 : 0;
	}
	CHECK(swapFrames >= 4);
	CHECK(swapFrames <= 5);
	REQUIRE(horde->getAIUpdateInterface()->aiAttackObject(target, CMD_FROM_PLAYER));
	const W3DScriptedModelDraw *d = modelDrawOf(r, *m);
	REQUIRE(d != nullptr);
	std::set<std::string> standing, riding;
	int launches = 0, launchesOnBone = 0;
	for (int f = 0; f < 80; ++f)
	{
		if (f == 40)
		{
			horde->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ c0.x, c0.y + 600.0f, 0.0f }, CMD_FROM_AI);
		}
		r.frame(1);
		if (!r.logic().findObjectByID(m->getID()) || m->isEffectivelyDead())
		{
			break;
		}
		const W3DDrawFrame fr = d->frame();
		for (int t = 0; t < fr.trackCount; ++t)
		{
			if (fr.tracks[t].anim && fr.tracks[t].clipName.find("RURhrmArch_ATK") != std::string::npos)
			{
				(f < 40 ? standing : riding).insert(fr.tracks[t].clipName);
			}
		}
		if (has(*m, "FIRING_B"))
		{
			// the launch offset the logic's arrow takes (RW 0x6756A1): the bow model's FIREAROWTIP, well above the saddle
			float launch[12] = {};
			++launches;
			ProjectileLaunchOffsets *offsets = r.logic().combat().launchOffsets();
			REQUIRE(offsets != nullptr);
			if (offsets->launchOffset(*m, 1, 0, launch) && launch[11] > 10.0f)
			{
				++launchesOnBone;
			}
		}
	}
	CHECK(d->currentModelState() != nullptr);
	if (d->currentModelState())
	{
		CHECK(d->currentModelState()->modelName() == "RURhrmArch_SKN");
	}
	std::string st, rd;
	for (const std::string &c : standing)
	{
		st += c + " ";
	}
	for (const std::string &c : riding)
	{
		rd += c + " ";
	}
	std::printf("  info: G8: standing shots play %s; riding shots play %s; %d shots, %d from the bow's tip\n", st.c_str(), rd.c_str(), launches, launchesOnBone);
	CHECK(!standing.empty());
	bool ridingClip = false;
	for (const std::string &c : riding)
	{
		// the move-and-fire clips: ATKD (ahead), ATKF / ATKH / ATKJ (TURRET_ANGLE_90 / 180 / 270)
		ridingClip = ridingClip || c.find("_ATKD") != std::string::npos || c.find("_ATKF") != std::string::npos || c.find("_ATKH") != std::string::npos ||
		             c.find("_ATKJ") != std::string::npos;
	}
	CHECK(ridingClip);
	CHECK(launches > 0);
	CHECK(launchesOnBone == launches);
	// the flag off: the model condition goes (RW 0x691106)
	m->getWeapons()->setWeaponSetFlag(bit, false);
	CHECK_FALSE(has(*m, "WEAPONSET_TOGGLE_1"));
}
