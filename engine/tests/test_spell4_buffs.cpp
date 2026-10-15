// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-4: the BuffNugget buffs (GameClient/BuffDraw.h). Heal and Rebuild put HealingBuff (EXHealbuff) on the units they heal, Rallying Call's
// modifier puts LeadershipBuff*Lvl1 on the units it lifts; the port noted the nugget and drew nothing (the owner: "it's just a circle"). The retail
// cases SKIP without ROTWK_INSTALL / BFME2_INSTALL.

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameClient/BuffDraw.h"
#include "GameClient/FXPlayback.h"
#include "GameClient/LiveFX.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Object.h"

#include <map>
#include <memory>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

TEST_CASE("SPELL-4: ClientBuffs follows BuffManager::TurnOnBuff / MakeNormalBuff / TurnOff and TBuff::UpdateFade (RW 0x764483, 0x7641A2, 0x4D1417)")
{
	ClientBuffs b;
	// types 1 .. 8 only; a template and a model needed
	CHECK_FALSE(b.turnOn(7, 0, "HealingBuff", "EXHealbuff", 72, 100));
	CHECK_FALSE(b.turnOn(7, 9, "HealingBuff", "EXHealbuff", 72, 100));
	CHECK_FALSE(b.turnOn(7, 2, "", "EXHealbuff", 72, 100));
	CHECK(b.count() == 0u);
	REQUIRE(b.turnOn(7, 2, "HealingBuff", "EXHealbuff", 72, 100));
	REQUIRE(b.count() == 1u);
	const ClientBuffs::Buff &h = b.buffs().begin()->second;
	CHECK(h.object == 7u);
	CHECK(h.active);
	CHECK(h.expireFrame == 172u);
	CHECK(h.state == ClientBuffs::FADE_IN);
	CHECK(h.opacity == 0.0f);
	const std::uint64_t first = h.serial;
	// the fade: 1/12 a drawn frame, ON at the 12th (0.083333336 * 12 rounds to 1 or above in float steps)
	int frames = 0;
	while (b.buffs().begin()->second.state == ClientBuffs::FADE_IN)
	{
		b.update(101);
		++frames;
		REQUIRE(frames < 20); // tolerance: a loop guard only (the exact count is checked after the loop)
	}
	CHECK(frames == 13); // 12 float additions of 0.083333336 stay below 1.0: the 13th step reaches it (clamped to 1)
	CHECK(b.buffs().begin()->second.opacity == 1.0f);
	// the same template again: kept (the same TBuff), the expiry moved
	REQUIRE(b.turnOn(7, 2, "HealingBuff", "EXHealbuff", 72, 150));
	CHECK(b.buffs().begin()->second.serial == first);
	CHECK(b.buffs().begin()->second.expireFrame == 222u);
	// the expiry: fade out, then released
	b.update(222);
	CHECK(b.buffs().begin()->second.state == ClientBuffs::FADE_OUT);
	CHECK_FALSE(b.buffs().begin()->second.active);
	// a refresh while fading out: back to ON at opacity 1, the same TBuff
	REQUIRE(b.turnOn(7, 2, "HealingBuff", "EXHealbuff", 10, 222));
	CHECK(b.buffs().begin()->second.state == ClientBuffs::ON);
	CHECK(b.buffs().begin()->second.opacity == 1.0f);
	CHECK(b.buffs().begin()->second.serial == first);
	// another template of the type replaces it (a new TBuff)
	REQUIRE(b.turnOn(7, 2, "LeadershipBuffInfantryLvl1", "EXLeadership", 10, 222));
	CHECK(b.count() == 1u);
	CHECK(b.buffs().begin()->second.thing == "LeadershipBuffInfantryLvl1");
	CHECK(b.buffs().begin()->second.serial != first);
	// another type is another instance
	REQUIRE(b.turnOn(7, 3, "HealingBuff", "EXHealbuff", 10, 222));
	CHECK(b.count() == 2u);
	// a turn-off fades out (not immediate) and the faded buff is released after 12 frames
	b.turnOff(7, 3, false);
	int out = 0;
	while (b.count() == 2u)
	{
		b.update(223);
		++out;
		REQUIRE(out < 20); // tolerance: a loop guard only (the exact count is checked after the loop)
	}
	CHECK(out == 1); // it was still fading in at 1/12: one step down to 0, then released
	b.turnOff(7, 2, true); // immediate
	CHECK(b.count() == 0u);
	// the object's death releases all its buffs at once
	REQUIRE(b.turnOn(8, 1, "HealingBuff", "EXHealbuff", 10, 0));
	REQUIRE(b.turnOn(8, 4, "HealingBuff", "EXHealbuff", 10, 0));
	REQUIRE(b.turnOn(9, 4, "HealingBuff", "EXHealbuff", 10, 0));
	b.removeObject(8);
	CHECK(b.count() == 1u);
	CHECK(b.buffs().begin()->second.object == 9u);
	// W3DBuffBuffer::addBuff: 1000 TBuffs of a template at most
	ClientBuffs many;
	for (ObjectID id = 1; id <= 1000; ++id)
	{
		REQUIRE(many.turnOn(id, 2, "HealingBuff", "EXHealbuff", 10, 0));
	}
	CHECK_FALSE(many.turnOn(1001, 2, "HealingBuff", "EXHealbuff", 10, 0));
	CHECK(many.turnOn(1001, 2, "OtherBuff", "EXOther", 10, 0));
	many.removeObject(1);
	CHECK(many.turnOn(1002, 2, "HealingBuff", "EXHealbuff", 10, 0));
}

namespace
{
// the render side as the device runs it: six drawn frames (30 a second) to a logic frame (5 a second); a drawn frame plays the queued logic effects and steps the
// buffs on the logic frame it sees (GodotGameWorld::syncBuffs)
std::unique_ptr<LiveFX> makeFx(FXPlayback &playback, LiveGame &game)
{
	const std::vector<std::string> errors = playback.loadRetailData();
	if (!errors.empty())
	{
		throw std::runtime_error("FX data: " + errors.front());
	}
	return std::make_unique<LiveFX>(game, playback);
}

struct BuffWorld
{
	hudtest::Rig rig;
	FXPlayback playback;
	std::unique_ptr<LiveFX> fxOwner;
	LiveFX &fx;
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	Object *book = nullptr;
	Object *anchor = nullptr;

	BuffWorld(hudtest::SharedWorld &s, const char *faction, const char *citadel, std::initializer_list<const char *> sciences)
		: rig(s, "map mp fall back 4p", faction)
		, playback(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG)
		, fxOwner(makeFx(playback, *rig.game))
		, fx(*fxOwner)
		, context(s.world->enterContext())
	{
		std::string err;
		anchor = rig.game->createObject(citadel, rig.index(), Coord3D{ 600.0f, 600.0f, 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(anchor, err);
		anchor->friend_onBuildComplete();
		for (const char *sci : sciences)
		{
			rig.local->science().grantScience(TheScienceStore->getScienceFromInternalName(sci));
		}
	}
	unsigned frame() { return rig.logic().getFrame(); }
	void drawn()
	{
		rig.game->advance(0.2 / 6.0);
		fx.flushPending();
		fx.buffs().update(frame());
		playback.step();
	}
	void logicFrames(int n)
	{
		const unsigned target = frame() + (unsigned)n;
		int guard = 0;
		while (frame() < target)
		{
			drawn();
			REQUIRE(++guard < 100 * n + 100); // tolerance: a loop guard only
		}
	}
	void findBook()
	{
		book = SpecialPowerModules::findSpellBookObject(rig.logic(), *rig.local);
		REQUIRE(book);
	}
	void cast(const char *power, const Coord3D &at)
	{
		const SpecialPowerTemplate *p = TheSpecialPowerStore->findSpecialPowerTemplate(power);
		REQUIRE(p);
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_LOCATION, rig.index());
		m.appendIntegerArgument((int)p->getID());
		m.appendLocationArgument(at);
		m.appendObjectIDArgument(INVALID_ID);
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(book->getID());
		rig.game->commands().append(m);
	}
	size_t particles(const std::string &name) const
	{
		size_t n = 0;
		playback.particles().forEachSystem([&](const FXParticleSystem::ParticleSystem &sys) { n += sys.getTemplate().getName() == name ? 1u : 0u; });
		return n;
	}
};

// (template / model) -> how many of `ids` carry that buff
std::map<std::string, int> buffsOn(const ClientBuffs &b, const std::set<ObjectID> &ids)
{
	std::map<std::string, int> out;
	for (const auto &kv : b.buffs())
	{
		if (ids.count(kv.second.object))
		{
			++out[kv.second.thing + "/" + kv.second.model];
		}
	}
	return out;
}

std::set<ObjectID> membersOf(GameLogic &logic, const Object &horde, const std::string &memberTemplate)
{
	std::set<ObjectID> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o != &horde && o->getTemplate()->getName() == memberTemplate && o->getControllingPlayer() == horde.getControllingPlayer())
		{
			out.insert(o->getID());
		}
	}
	return out;
}

ActiveBody &bodyOf(GameLogic &logic, ObjectID id)
{
	Object *o = logic.findObjectByID(id);
	REQUIRE(o);
	ActiveBody *body = dynamic_cast<ActiveBody *>(o->getBodyModule());
	REQUIRE(body);
	return *body;
}

const AttributeModifierPool *poolOf(GameLogic &logic, ObjectID id)
{
	Object *o = logic.findObjectByID(id);
	REQUIRE(o);
	return static_cast<const AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate"));
}

// the product of an attribute over a unit's pool at the current frame: false when no live entry answers it
bool poolProduct(GameLogic &logic, ObjectID id, int type, float &out)
{
	const AttributeModifierPool *pool = poolOf(logic, id);
	return pool && pool->product(type, nullptr, true, out);
}

// every drawn frame until the first buff exists; the logic frame it appeared in
unsigned runUntilBuffs(BuffWorld &w)
{
	for (int i = 0; i < 400 && w.fx.buffs().count() == 0u; ++i) // tolerance: a loop guard (the check below fails when it ran out)
	{
		w.drawn();
	}
	REQUIRE(w.fx.buffs().count() > 0u); // tolerance: the caller pins the exact count right after; this only fails the wait loop that ran out
	return w.frame();
}

// the buffs' expiry as the drawn frames see it: all `expected` buffs active while the logic frame is below `expire`; at the first drawn frame on or past it
// every one is fading out (inactive); then they are released after the 13-step fade (opacity 1 -> 0 at 1/12 a step: 12 steps to reach 0.0, state GONE on the
// 13th? the loop counts them)
void checkExpiry(BuffWorld &w, const std::set<ObjectID> &ids, unsigned expire, size_t expected, int &fadeSteps)
{
	bool crossed = false;
	for (int guard = 0; guard < 4000; ++guard) // tolerance: a loop guard only
	{
		w.drawn();
		size_t active = 0, fading = 0;
		for (const auto &kv : w.fx.buffs().buffs())
		{
			if (ids.count(kv.second.object))
			{
				active += kv.second.active ? 1u : 0u;
				fading += kv.second.state == ClientBuffs::FADE_OUT ? 1u : 0u;
			}
		}
		if (!crossed && w.frame() < expire)
		{
			REQUIRE(active == expected);
			REQUIRE(fading == 0u);
		}
		else if (!crossed)
		{
			crossed = true;
			REQUIRE(w.frame() == expire); // the first drawn frame at the expiry sees exactly the expiry frame (six drawn frames a logic frame)
			REQUIRE(active == 0u);
			REQUIRE(fading == expected);
			fadeSteps = 0;
		}
		if (crossed)
		{
			++fadeSteps;
			if (buffsOn(w.fx.buffs(), ids).empty())
			{
				return;
			}
		}
	}
	FAIL("the buffs never went");
}
} // namespace

TEST_CASE("SPELL-4 retail: Heal heals exactly 15 soldiers to full and puts HealingBuff (EXHealbuff, 12 logic frames) on each; the no-cast control shows neither")
{
	if (!hudtest::haveWorld("spell4 heal buffs"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	BuffWorld w(s, "FactionMen", "MenFortressCitadel", { "SCIENCE_Heal" });
	std::string err;
	const Coord3D at{ 900.0f, 700.0f, w.rig.logic().getGroundHeight(900.0f, 700.0f) };
	Object *horde = w.rig.game->createObject("GondorFighterHorde", w.rig.index(), at, 0.0f, &err);
	REQUIRE_MESSAGE(horde, err);
	w.logicFrames(10);
	w.findBook();
	const std::set<ObjectID> soldiers = membersOf(w.rig.logic(), *horde, "GondorFighter");
	REQUIRE(soldiers.size() == 15u);
	float max = 0.0f;
	for (ObjectID id : soldiers)
	{
		ActiveBody &b = bodyOf(w.rig.logic(), id);
		max = b.getMaxHealth();
		b.internalChangeHealth(-40.0f); // Heal gives 60% of the maximum: 120 of 200
	}
	REQUIRE(max == 200.0f);
	// the no-fix control: the same scene for as long as the cast takes, no cast: no buff, nobody healed, no effect played
	const unsigned long long fxBefore = w.rig.logic().fxEvents().total();
	w.logicFrames(8);
	CHECK(w.fx.buffs().count() == 0u);
	CHECK(w.rig.logic().fxEvents().total() == fxBefore);
	for (ObjectID id : soldiers)
	{
		CHECK(bodyOf(w.rig.logic(), id).getHealth() == 160.0f);
	}
	// the cast
	w.cast("SpellBookHeal", at);
	const unsigned appeared = runUntilBuffs(w);
	CHECK(w.fx.buffErrors().empty());
	CHECK(buffsOn(w.fx.buffs(), soldiers) == std::map<std::string, int>{ { "HealingBuff/EXHealbuff", 15 } });
	CHECK(w.fx.buffs().count() == 15u); // nobody else carries one
	for (const auto &kv : w.fx.buffs().buffs())
	{
		CHECK(kv.second.type == 1); // BuffType = Healing
		CHECK(kv.second.expireFrame == appeared + 12u); // BuffLifeTime 2400 ms = 12 logic frames
	}
	w.logicFrames(1);
	for (ObjectID id : soldiers)
	{
		CHECK(bodyOf(w.rig.logic(), id).getHealth() == 200.0f); // 160 + 120 = 280, capped at the maximum
	}
	{
		const auto &sites = w.rig.logic().fxEvents().perSite();
		auto it = sites.find("PlayerHealSpecialPower HealFX");
		REQUIRE(it != sites.end());
		CHECK(it->second == 16u); // one HealFX per healed object: the 15 soldiers and their horde (BuffNugget gives a HORDE no buff: 15 buffs)
	}
	int fadeSteps = 0;
	checkExpiry(w, soldiers, appeared + 12u, 15u, fadeSteps);
	CHECK(fadeSteps == 13); // the fade out: 12 steps of 1/12 from the full opacity; released on the 13th (float sum 0.083333336 * 12 stays above 0)
	CHECK(w.fx.buffs().count() == 0u);
}

TEST_CASE("SPELL-4 retail: Rallying Call gives exactly 15 soldiers its modifier (damage x1.5, armour x0.5, 300 logic frames) and LeadershipBuffInfantryLvl1 (305 frames); neither without the cast")
{
	if (!hudtest::haveWorld("spell4 rally buffs"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	BuffWorld w(s, "FactionMen", "MenFortressCitadel", { "SCIENCE_RallyingCallMP" });
	std::string err;
	const Coord3D at{ 900.0f, 700.0f, w.rig.logic().getGroundHeight(900.0f, 700.0f) };
	Object *horde = w.rig.game->createObject("GondorFighterHorde", w.rig.index(), at, 0.0f, &err);
	REQUIRE_MESSAGE(horde, err);
	w.logicFrames(10);
	w.findBook();
	const std::set<ObjectID> soldiers = membersOf(w.rig.logic(), *horde, "GondorFighter");
	REQUIRE(soldiers.size() == 15u);
	const ModifierListTemplate *list = TheAttributeModifierStore->get(TheAttributeModifierStore->findIndex("SpellBookRallyingCallModifier"));
	REQUIRE(list);
	float wantDamage = 0.0f, wantArmor = 0.0f;
	REQUIRE(list->value(ATTRIBUTE_DAMAGE_MULT, nullptr, wantDamage));
	REQUIRE(list->value(ATTRIBUTE_ARMOR, nullptr, wantArmor));
	CHECK(wantDamage == 1.5f);
	CHECK(wantArmor == 0.5f);
	// the no-fix control: no cast, no modifier and no buff
	w.logicFrames(8);
	CHECK(w.fx.buffs().count() == 0u);
	for (ObjectID id : soldiers)
	{
		float v = 0.0f;
		CHECK_FALSE(poolProduct(w.rig.logic(), id, ATTRIBUTE_DAMAGE_MULT, v));
	}
	// the cast: the logic frame the modifier first answers is the frame it was added in; it ends 300 frames later (Duration 60000 ms)
	w.cast("SpellBookRallyingCall", at);
	unsigned added = 0;
	for (int i = 0; i < 20 && added == 0u; ++i)
	{
		w.drawn();
		float v = 0.0f;
		if (poolProduct(w.rig.logic(), *soldiers.begin(), ATTRIBUTE_DAMAGE_MULT, v))
		{
			added = w.frame();
		}
	}
	REQUIRE(added != 0u);
	for (ObjectID id : soldiers)
	{
		float d = 0.0f, a = 0.0f;
		CHECK(poolProduct(w.rig.logic(), id, ATTRIBUTE_DAMAGE_MULT, d));
		CHECK(poolProduct(w.rig.logic(), id, ATTRIBUTE_ARMOR, a));
		CHECK(d == wantDamage);
		CHECK(a == wantArmor);
	}
	const unsigned appeared = runUntilBuffs(w);
	CHECK(appeared == added);
	CHECK(w.fx.buffErrors().empty());
	CHECK(buffsOn(w.fx.buffs(), soldiers) == std::map<std::string, int>{ { "LeadershipBuffInfantryLvl1/EXLeaderbuff", 15 } });
	CHECK(w.fx.buffs().count() == 15u);
	for (const auto &kv : w.fx.buffs().buffs())
	{
		CHECK(kv.second.type == 2); // BuffType = LeaderShip
		CHECK(kv.second.expireFrame == appeared + 305u); // BuffLifeTime 61000 ms
	}
	CHECK(w.particles("SpellRallyingCallCircles") == 1u); // the power's TriggerFX at the location
	// the modifier's boundary: live at +299, gone at +300 (the pool's own frame < expiry test)
	while (w.frame() < added + 299u)
	{
		w.drawn();
	}
	for (ObjectID id : soldiers)
	{
		float d = 0.0f;
		CHECK(poolProduct(w.rig.logic(), id, ATTRIBUTE_DAMAGE_MULT, d));
	}
	while (w.frame() < added + 300u)
	{
		w.drawn();
	}
	for (ObjectID id : soldiers)
	{
		float d = 0.0f;
		CHECK_FALSE(poolProduct(w.rig.logic(), id, ATTRIBUTE_DAMAGE_MULT, d));
	}
	// the glow outlives it by the 5 frames BuffLifeTime 61000 adds to Duration 60000
	for (const auto &kv : w.fx.buffs().buffs())
	{
		CHECK(kv.second.active);
	}
	int fadeSteps = 0;
	checkExpiry(w, soldiers, appeared + 305u, 15u, fadeSteps);
	CHECK(fadeSteps == 13);
}

TEST_CASE("SPELL-4 retail: Rebuild plays SpellRebuild and SpellRebuildDots on each structure it rebuilds (and heals it to full); no cast, no particles")
{
	if (!hudtest::haveWorld("spell4 rebuild particles"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	BuffWorld w(s, "FactionMen", "MenFortressCitadel", { "SCIENCE_Rebuild" });
	std::string err;
	const Coord3D at{ 1700.0f, 1700.0f, w.rig.logic().getGroundHeight(1700.0f, 1700.0f) };
	std::vector<ObjectID> farms;
	for (int i = 0; i < 2; ++i)
	{
		Object *f = w.rig.game->createObject("GondorFarm", w.rig.index(), Coord3D{ at.x + 60.0f * (float)i, at.y, at.z }, 0.0f, &err);
		REQUIRE_MESSAGE(f, err);
		f->friend_onBuildComplete();
		farms.push_back(f->getID());
	}
	w.logicFrames(10);
	w.findBook();
	float max = 0.0f;
	for (ObjectID id : farms)
	{
		ActiveBody &b = bodyOf(w.rig.logic(), id);
		max = b.getMaxHealth();
		b.internalChangeHealth(-1000.0f);
	}
	REQUIRE(max == 2000.0f); // GondorFarm
	// the control
	const unsigned long long fxBefore = w.rig.logic().fxEvents().total();
	w.logicFrames(8);
	CHECK(w.particles("SpellRebuild") == 0u);
	CHECK(w.particles("SpellRebuildDots") == 0u);
	for (ObjectID id : farms)
	{
		CHECK(bodyOf(w.rig.logic(), id).getHealth() == max - 1000.0f);
	}
	CHECK(w.rig.logic().fxEvents().total() == fxBefore);
	w.cast("SpellBookRebuild", at);
	size_t rebuild = 0, dots = 0;
	for (int i = 0; i < 40 && rebuild == 0u; ++i)
	{
		w.drawn();
		rebuild = w.particles("SpellRebuild");
		dots = w.particles("SpellRebuildDots");
	}
	const unsigned long long healedFx = w.rig.logic().fxEvents().total() - fxBefore;
	MESSAGE("rebuild: " << healedFx << " HealFX events, " << rebuild << " SpellRebuild, " << dots << " SpellRebuildDots systems");
	CHECK(healedFx == 2u); // the two farms: the citadel is out of the radius
	CHECK(rebuild == 2u);
	CHECK(dots == 2u);
	for (ObjectID id : farms)
	{
		CHECK(bodyOf(w.rig.logic(), id).getHealth() == max); // + 4000, capped
	}
	CHECK(w.fx.buffErrors().empty());
}

TEST_CASE("SPELL-4 retail: Isengard's War Chant gives its modifier and the PositiveAtkBuff glow (EXPosBuff, 300 frames) to the same soldiers, and plays SpellWarChant; no cast, none")
{
	if (!hudtest::haveWorld("spell4 war chant"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	BuffWorld w(s, "FactionIsengard", "IsengardFortressCitadel", { "SCIENCE_WarChant" });
	std::string err;
	const Coord3D at{ 900.0f, 700.0f, w.rig.logic().getGroundHeight(900.0f, 700.0f) };
	Object *horde = w.rig.game->createObject("IsengardFighterHorde", w.rig.index(), at, 0.0f, &err);
	REQUIRE_MESSAGE(horde, err);
	w.logicFrames(10);
	w.findBook();
	const std::set<ObjectID> soldiers = membersOf(w.rig.logic(), *horde, "IsengardFighter");
	MESSAGE("war chant soldiers " << soldiers.size());
	REQUIRE(soldiers.size() == 15u);
	w.logicFrames(8);
	CHECK(w.fx.buffs().count() == 0u);
	CHECK(w.particles("SpellWarChant") == 0u);
	w.cast("SpellBookWarChant", at);
	unsigned added = 0;
	for (int i = 0; i < 20 && added == 0u; ++i)
	{
		w.drawn();
		float v = 0.0f;
		if (poolProduct(w.rig.logic(), *soldiers.begin(), ATTRIBUTE_DAMAGE_MULT, v))
		{
			added = w.frame();
		}
	}
	REQUIRE(added != 0u);
	const unsigned appeared = runUntilBuffs(w);
	CHECK(appeared == added);
	std::set<ObjectID> modified;
	for (ObjectID id : soldiers)
	{
		float d = 0.0f, a = 0.0f;
		if (poolProduct(w.rig.logic(), id, ATTRIBUTE_DAMAGE_MULT, d))
		{
			CHECK(d == 1.5f);
			CHECK(poolProduct(w.rig.logic(), id, ATTRIBUTE_ARMOR, a));
			CHECK(a == 0.5f);
			modified.insert(id);
		}
	}
	CHECK(modified.size() == 15u);
	CHECK(buffsOn(w.fx.buffs(), soldiers) == std::map<std::string, int>{ { "PositiveAtkBuff/EXPosBuff", 15 } });
	CHECK(w.fx.buffs().count() == 15u);
	for (const auto &kv : w.fx.buffs().buffs())
	{
		CHECK(kv.second.expireFrame == appeared + 300u); // BuffLifeTime 60000 ms
	}
	CHECK(w.particles("SpellWarChant") == 1u);
	CHECK(w.fx.buffErrors().empty());
	int fadeSteps = 0;
	checkExpiry(w, soldiers, appeared + 300u, 15u, fadeSteps);
	CHECK(fadeSteps == 13);
}
