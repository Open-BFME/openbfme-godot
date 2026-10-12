// OpenBFME unit tests. GPL-3.0.
// Lane FACT-A: what a command did to the world, read from the logic after it (the effect letters of the ability, spell book and command tests).

#pragma once

#include "FactATestUtil.h"

#include "Common/PlayerList.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/System/EmotionSystem.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace facta
{
// the health of an object, a horde's members summed
inline float groupHealth(Object &o)
{
	float h = o.getBodyModule() ? o.getBodyModule()->getHealth() : 0.0f;
	if (ContainModuleInterface *c = o.getContain())
	{
		if (c->getHordeContainInterface() && c->getContainedItemsList())
		{
			h = 0.0f;
			for (const Object *m : *c->getContainedItemsList())
			{
				h += m->getBodyModule() ? m->getBodyModule()->getHealth() : 0.0f;
			}
		}
	}
	return h;
}

// the object and, for a horde, its members
inline std::vector<Object *> bodies(Object &o)
{
	std::vector<Object *> out{ &o };
	if (ContainModuleInterface *c = o.getContain())
	{
		if (c->getHordeContainInterface() && c->getContainedItemsList())
		{
			out.insert(out.end(), c->getContainedItemsList()->begin(), c->getContainedItemsList()->end());
		}
	}
	return out;
}

// the attribute modifier lists on the object and its members ("<id>:<list>")
inline std::set<std::string> modifiers(Object &o)
{
	std::set<std::string> out;
	for (Object *b : bodies(o))
	{
		if (const AttributeModifierPool *pool = dynamic_cast<const AttributeModifierPool *>(b->findModule("AttributeModifierPoolUpdate")))
		{
			for (const std::string &n : pool->listNames())
			{
				out.insert(n);
			}
		}
	}
	return out;
}

// a wound for the heals to show: the object and its members lose 40 % of their health (unresistable damage)
inline void wound(Object &o)
{
	for (Object *b : bodies(o))
	{
		BodyModuleInterface *body = b->getBodyModule();
		if (body && body->getMaxHealth() > 0.0f)
		{
			DamageInfo hurt;
			hurt.m_input.m_damageType = DAMAGE_UNRESISTABLE;
			hurt.m_input.m_amount = body->getMaxHealth() * 0.4f;
			b->attemptDamage(hurt);
		}
	}
}

inline void holdFire(Object &o)
{
	static const int noAuto = ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE");
	for (Object *b : bodies(o))
	{
		b->setStatus((unsigned)noAuto, true);
	}
}

// what an ability did to the world, accumulated over the frames after the press: D damage to the enemy (its health fell or it died), H a heal (the wounded
// object's or ally's health rose), B a buff (a new attribute modifier list on the object or the ally), X a debuff (a new list on the enemy), S a summon (a new
// object of the player that is not a projectile), M the object moved (more than 40), T the object's status or weapon set changed, C a capture (the target
// flag's building changed owner), F an enemy member became afraid (FEAR .. DOOM), K an enemy member was moved (thrown back, fled), L an
// ally gained experience (levels granted), R an allied hero's recharging power became ready (a timer refresh)
struct Effects
{
	std::set<char> seen;
	std::string str() const { return seen.empty() ? std::string("none") : std::string(seen.begin(), seen.end()); }
	bool has(char c) const { return seen.count(c) != 0; }
};

struct Watch
{
	ObjectID self = INVALID_ID, ally = INVALID_ID, enemy = INVALID_ID, captured = INVALID_ID;
	float selfHp = 0, allyHp = 0, enemyHp = 0;
	std::set<std::string> selfMods, allyMods, enemyMods;
	std::set<ObjectID> existing;
	Coord3D selfPos{};
	ObjectStatusMaskType selfStatus{};
	std::uint32_t selfWeaponSet = 0;
	Player *capturedOwner = nullptr;
	std::map<ObjectID, float> allyExperience; // member -> experience
	ObjectID helper = INVALID_ID;             // an allied hero whose power recharges (a timer refresh's target)
	std::vector<const SpecialPowerModuleInterface *> recharging; // the helper's powers that recharge
	std::map<ObjectID, int> enemyEmotion; // member -> the current emotion's type (-1 none)
	std::map<ObjectID, Coord3D> enemyPos;

	static int emotionOf(Object &m)
	{
		const EmotionTrackerUpdate *t = EmotionTrackerUpdate::of(m);
		return t && t->current() ? t->current()->type() : -1;
	}

	void begin(GameLogic &logic, Object &o, Object &a, Object &e, Object *capture)
	{
		self = o.getID();
		ally = a.getID();
		enemy = e.getID();
		selfHp = groupHealth(o);
		allyHp = groupHealth(a);
		enemyHp = groupHealth(e);
		selfMods = modifiers(o);
		allyMods = modifiers(a);
		enemyMods = modifiers(e);
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			existing.insert(x->getID());
		}
		selfPos = *o.getPosition();
		selfStatus = o.getStatusBits();
		selfWeaponSet = o.getWeaponSetFlags()[0];
		for (Object *m : bodies(a))
		{
			allyExperience[m->getID()] = m->getExperienceTracker() ? m->getExperienceTracker()->getExperience() : 0.0f;
		}
		for (Object *m : bodies(e))
		{
			enemyEmotion[m->getID()] = emotionOf(*m);
			enemyPos[m->getID()] = *m->getPosition();
		}
		captured = capture ? capture->getID() : INVALID_ID;
		capturedOwner = capture ? capture->getControllingPlayer() : nullptr;
	}
	void sample(GameLogic &logic, Player *me, Effects &fx) const
	{
		static const int projectile = ObjectTemplateInfoBuilder::kindOfIndex("PROJECTILE");
		Object *o = logic.findObjectByID(self), *a = logic.findObjectByID(ally), *e = logic.findObjectByID(enemy);
		if (!e || e->isEffectivelyDead() || groupHealth(*e) < enemyHp - 0.5f)
		{
			fx.seen.insert('D');
		}
		if ((o && groupHealth(*o) > selfHp + 0.5f) || (a && groupHealth(*a) > allyHp + 0.5f))
		{
			fx.seen.insert('H');
		}
		auto grew = [](const std::set<std::string> &before, const std::set<std::string> &now) {
			for (const std::string &n : now)
			{
				if (!before.count(n))
				{
					return true;
				}
			}
			return false;
		};
		if ((o && grew(selfMods, modifiers(*o))) || (a && grew(allyMods, modifiers(*a))))
		{
			fx.seen.insert('B');
		}
		if (e && grew(enemyMods, modifiers(*e)))
		{
			fx.seen.insert('X');
		}
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			if (!existing.count(x->getID()) && x->getControllingPlayer() == me && !(projectile >= 0 && x->isKindOf((unsigned)projectile)) && !x->getContainedBy())
			{
				fx.seen.insert('S');
			}
		}
		if (o)
		{
			const float dx = o->getPosition()->x - selfPos.x, dy = o->getPosition()->y - selfPos.y;
			if (dx * dx + dy * dy > 40.0f * 40.0f)
			{
				fx.seen.insert('M');
			}
			if (o->getStatusBits() != selfStatus || o->getWeaponSetFlags()[0] != selfWeaponSet)
			{
				fx.seen.insert('T');
			}
		}
		for (const auto &kv : enemyEmotion)
		{
			Object *m = logic.findObjectByID(kv.first);
			if (!m)
			{
				continue;
			}
			const int now = emotionOf(*m);
			if (now >= EMOTION_FEAR && now <= EMOTION_DOOM && now != kv.second)
			{
				fx.seen.insert('F'); // the enemy is afraid (FEAR, UNCONTROLLABLE_FEAR, TERROR, DOOM)
			}
			const Coord3D &p0 = enemyPos.at(kv.first);
			const float dx = m->getPosition()->x - p0.x, dy = m->getPosition()->y - p0.y;
			if (dx * dx + dy * dy > 40.0f * 40.0f)
			{
				fx.seen.insert('K'); // the enemy was moved (thrown, fled)
			}
		}
		for (const auto &kv : allyExperience)
		{
			Object *m = logic.findObjectByID(kv.first);
			if (m && m->getExperienceTracker() && m->getExperienceTracker()->getExperience() > kv.second)
			{
				fx.seen.insert('L'); // the ally gained experience (levels granted)
			}
		}
		if (helper != INVALID_ID && logic.findObjectByID(helper))
		{
			for (const SpecialPowerModuleInterface *sp : recharging)
			{
				if (sp->getPercentReady() >= 1.0f)
				{
					fx.seen.insert('R'); // the helper's power is ready again
				}
			}
		}
		if (captured != INVALID_ID)
		{
			const Object *c = logic.findObjectByID(captured);
			if (c && c->getControllingPlayer() != capturedOwner)
			{
				fx.seen.insert('C');
			}
		}
	}
};

} // namespace facta
