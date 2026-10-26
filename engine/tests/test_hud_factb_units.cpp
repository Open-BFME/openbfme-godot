// OpenBFME retail tests (lane FACT-B): every unit, horde and hero of Isengard and Mordor, through its own command buttons as a player presses them.
//   * the special power buttons (SPECIAL_POWER, SPECIAL_POWER_TOGGLE, ...): the unit is raised to its top level (a hero's abilities unlock by level), the
//     button pressed, a target clicked when the button needs one (an enemy, an ally, a building or the ground), and the power's module must have fired:
//     its recharge started (SpecialPowerModuleInterface::getReadyFrame, RW 0x89724B) and, for the SpecialPowerModule family, no effect that is not ported;
//   * the OBJECT_UPGRADE buttons (a horde's banner carrier, forged blades, heavy armour, ...): the player technology they need is granted (the
//     structure tests buy it through the armoury's button), the button pressed, the upgrade reaches the horde and its members;
//   * the other buttons (weapon toggles, formation, stances): the press is sent and no press is counted as unported (ControlBar::unportedPresses).
// Failures are collected per item, printed as one table, then reported one CHECK per item. They share the HUD tests' retail world and SKIP when
// ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "FactbRig.h"

#include "Common/SpecialPower.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameClient/Drawable.h"
#include "GameClient/HudObjects.h"
#include "GameClient/GUI/AptScreens/AptSpellStore.h"
#include "GameClient/SpellBookUI.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Armor.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/HordeDispatchSpecialPower.h"
#include "GameLogic/Module/SpellBookPowers.h"
#include "GameLogic/Module/StealthAbilityModules.h"
#include "GameLogic/Module/WeaponChangeSpecialPower.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/WallSpan.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponState.h"
#include "GameLogic/WeaponStores.h"
#include "Common/PlayerHeroList.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

using namespace hudtest;

namespace
{
struct Results
{
	std::map<std::string, std::vector<std::string>> problems;
	std::map<std::string, std::string> passed;
	void fail(const std::string &item, const std::string &what) { problems[item].push_back(what); }
	void pass(const std::string &item, const std::string &what)
	{
		if (!problems.count(item))
		{
			passed[item] = what;
		}
	}
	void print(const char *title)
	{
		std::printf("factb %s: %zu passed, %zu with problems\n", title, passed.size(), problems.size());
		for (const auto &kv : passed)
		{
			std::printf("  PASS %s: %s\n", kv.first.c_str(), kv.second.c_str());
		}
		for (const auto &kv : problems)
		{
			for (const std::string &p : kv.second)
			{
				std::printf("  FAIL %s: %s\n", kv.first.c_str(), p.c_str());
			}
		}
	}
	// `known`: the items that fail for a registered stop (item -> stop id; a key "*suffix" stands for every item ending so). They are reported as
	// KNOWN, not passed and not failed; a known item that no longer fails is a failure (the stop's pin must move)
	void check(const std::map<std::string, std::string> &known = {})
	{
		auto knownOf = [&](const std::string &item) -> const std::string * {
			for (const auto &kv : known)
			{
				const bool wild = !kv.first.empty() && kv.first[0] == '*';
				const std::string suffix = wild ? kv.first.substr(1) : std::string();
				if (wild ? (item.size() >= suffix.size() && item.compare(item.size() - suffix.size(), suffix.size(), suffix) == 0) : item == kv.first)
				{
					return &kv.second;
				}
			}
			return nullptr;
		};
		std::map<std::string, int> matched;
		for (const auto &kv : problems)
		{
			std::string all;
			for (const std::string &p : kv.second)
			{
				all += p + "; ";
			}
			const std::string *stop = knownOf(kv.first);
			// lane FACT-C: a pin "S-xxxx|text" holds only failures that say `text` (each of them): another failure of the item is not the pinned gap
			if (stop && stop->find('|') != std::string::npos)
			{
				const std::string want = stop->substr(stop->find('|') + 1);
				for (const std::string &p : kv.second)
				{
					stop = stop && p.find(want) != std::string::npos ? stop : nullptr;
				}
			}
			if (stop)
			{
				std::printf("  KNOWN %s (%s)\n", kv.first.c_str(), stop->c_str());
				for (const auto &k : known)
				{
					matched[k.first] += &k.second == stop ? 1 : 0;
				}
				continue;
			}
			CHECK_MESSAGE(false, kv.first << ": " << all);
		}
		for (const auto &kv : known)
		{
			CHECK_MESSAGE(matched[kv.first] >= 1, kv.first << " passes now: move the pin of " << kv.second);
		}
	}
};

std::string str(const char *fmt, ...)
{
	char buf[400];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}

void trace(const std::string &item)
{
	static const bool on = std::getenv("FACTB_TRACE") != nullptr;
	if (on)
	{
		std::fprintf(stderr, "factb trace: %s\n", item.c_str());
		std::fflush(stderr);
	}
}

// the faction's own soldier horde, the friendly target of a power on own units (lane FACT-C: the Goblins' and Angmar's too: their powers' filters take
// their own soldiers, Azog's Crack the Whip goblins)
const char *ownHordeOf(const std::string &faction)
{
	return faction == "FactionMordor" ? "MordorFighterHorde" : faction == "FactionWild" ? "GoblinFighterHorde" : faction == "FactionAngmar" ? "AngmarDarkDunedainHorde" : "IsengardFighterHorde";
}

float dist2d(const Coord3D &a, const Coord3D &b)
{
	return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

bool isPowerCommand(int c)
{
	return c == GUI_COMMAND_SPECIAL_POWER || c == GUI_COMMAND_SPECIAL_POWER_TOGGLE || c == GUI_COMMAND_SPECIAL_POWER_FROM_COMMAND_CENTER;
}

// the modules of `o` and of its members that carry the power
std::vector<SpecialPowerModuleInterface *> powerModules(GameLogic &logic, Object *o, const SpecialPowerTemplate *t)
{
	std::vector<SpecialPowerModuleInterface *> out;
	if (SpecialPowerModuleInterface *m = SpecialPowerModules::findModule(*o, t))
	{
		out.push_back(m);
	}
	if (o->getContain())
	{
		for (Object *m : *o->getContain()->getContainedItemsList())
		{
			if (SpecialPowerModuleInterface *pm = SpecialPowerModules::findModule(*m, t))
			{
				out.push_back(pm);
			}
		}
	}
	(void)logic;
	return out;
}

struct Targets
{
	Object *enemyUnit = nullptr, *enemyBuilding = nullptr, *allyUnit = nullptr, *neutralBuilding = nullptr;
};

std::vector<float> expectedHits(const WeaponTemplate &w, const Object &victim, const Player *attackerPlayer, std::string &text, const Object *attacker = nullptr);

// The hits the caster (the unit and its horde's members) lands, frame by frame: a victim's health drop in a frame whose body records one of the
// casters as its last damager in that frame (ActiveBody's last damage record, RW + 0xAC)
struct HitTracker
{
	struct Hit
	{
		UnsignedInt frame;
		ObjectID victim;
		float amount;
		float left; ///< the victim's health after it
	};
	std::set<ObjectID> casters;
	std::map<ObjectID, float> health;
	std::vector<Hit> hits;
	std::map<ObjectID, UnsignedInt> terrified; ///< the first frame an object other than the casters had a TERROR request or emotion
	struct Made
	{
		UnsignedInt frame;
		const Player *owner;
		const ThingTemplate *tmpl;
	};
	std::map<ObjectID, Made> made; ///< the objects that appeared after the first frame tracked (not projectiles), with their owner then
	ObjectID highest = INVALID_ID;
	void track(GameLogic &logic)
	{
		const bool first = highest == INVALID_ID;
		ObjectID top = first ? 0 : highest;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (!first && o->getID() > highest && !made.count(o->getID()) && !o->isKindOfName("PROJECTILE"))
			{
				made[o->getID()] = { logic.getFrame(), o->getControllingPlayer(), o->getTemplate() };
			}
			top = std::max(top, o->getID());
		}
		highest = top;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (!casters.count(o->getID()) && !terrified.count(o->getID()))
			{
				const EmotionTrackerUpdate *tr = EmotionTrackerUpdate::of(*o);
				if (tr && (tr->requested(EMOTION_TERROR) || (tr->current() && tr->current()->type() == EMOTION_TERROR)))
				{
					terrified[o->getID()] = logic.getFrame();
				}
			}
			const ActiveBody *body = dynamic_cast<const ActiveBody *>(o->getBodyModule());
			if (!body || casters.count(o->getID()))
			{
				continue;
			}
			const float h = body->getHealth();
			auto it = health.find(o->getID());
			if (it != health.end() && h < it->second && body->lastDamageFrame() == logic.getFrame() && casters.count(body->lastDamager()))
			{
				hits.push_back({ logic.getFrame(), o->getID(), it->second - h, h });
			}
			health[o->getID()] = h;
		}
	}
	// the most victims the casters hit in one frame of [from, to)
	int maxVictims(UnsignedInt from, UnsignedInt to) const
	{
		std::map<UnsignedInt, int> n;
		int m = 0;
		for (const Hit &x : hits)
		{
			m = x.frame >= from && x.frame < to ? std::max(m, ++n[x.frame]) : m;
		}
		return m;
	}
	float maxHit(UnsignedInt from, UnsignedInt to) const
	{
		float m = 0.0f;
		for (const Hit &x : hits)
		{
			m = x.frame >= from && x.frame < to ? std::max(m, x.amount) : m;
		}
		return m;
	}
};

// What a power did, read before and after a cast, as the world shows it (not a module's own counter): per module class of the power (on the unit and
// its members, or the spell book) the result it must have:
//   DominateEnemySpecialPower, a capture (SpecialAbilityUpdate of type 0x1D / 0x1A)  -> an object's owner became the caster's player
//   LevelGrantSpecialPower                                                            -> a rank of the caster's player rose
//   WeaponFireSpecialAbilityUpdate / ArrowStormUpdate                                 -> a hit of the special weapon's size (Weapon.ini x Armor.ini) by
//                                                                                        the caster, above every hit it landed before the cast
//   CurseSpecialPower                                                                 -> an enemy's special power recharge restarted (ready later)
//   a SpecialPowerModule's AttributeModifier, TriggerAttributeModifier, HeroMode's    -> that ModifierList in an object's pool that lacked it
//   OCLSpecialPower, SpecialObject                                                    -> an object of the caster's player made
//   PlayerHealSpecialPower                                                            -> an own object's health rose
//   WeaponModeSpecialPowerUpdate / WeaponChangeSpecialPowerModule                     -> the mode on / the weapon set flags changed
//   ToggleHiddenSpecialAbilityUpdate                                                  -> the caster's status changed
//   ActivateModuleSpecialPower                                                        -> what the modules it activates (by tag) must do
// A class with none of these is named and fails (no effect asserted)
struct EffectProbe
{
	enum Kind
	{
		OWNER,
		LEVEL,
		DAMAGE,
		CURSE,
		MODIFIER,
		SPAWN,
		HEAL,
		WEAPON_MODE,
		WEAPON_FLAGS,
		STATUS,
		GRAB,
		TERROR,
		MOUNT,
		TELEPORT, ///< lane FACT-C: TeleportSpecialAbilityUpdate -> the caster stands elsewhere
		BOUNTY,   ///< lane FACT-C: ScavengerSpecialPower -> the player's bounty percent changed (RW 0x6AA847)
	};
	struct Expect
	{
		Kind kind;
		std::string what;       ///< the module class and its INI value, for the report
		std::string modifier;   ///< MODIFIER: the list name
		const WeaponTemplate *weapon = nullptr; ///< DAMAGE
		float amount = 0.0f;                    ///< LEVEL: LevelGrantSpecialPower's Experience
		std::set<std::string> templates;        ///< SPAWN: the templates the module makes (its OCLs' CreateObject ObjectNames, a SpecialObject)
	};
	// the CreateObject templates of an ObjectCreationList (its ObjectNames)
	static void oclTemplates(const std::string &name, std::set<std::string> &out)
	{
		const ObjectCreationList *ocl = TheObjectCreationListStore && !name.empty() ? TheObjectCreationListStore->findObjectCreationList(name) : nullptr;
		for (size_t i = 0; ocl && i < ocl->nuggets().size(); ++i)
		{
			if (ocl->nuggets()[i].kind == OCLNugget::CREATE_OBJECT)
			{
				out.insert(ocl->nuggets()[i].objectNames.begin(), ocl->nuggets()[i].objectNames.end());
			}
		}
	}
	static Expect spawnOf(const std::string &what, const std::set<std::string> &templates)
	{
		Expect e{ SPAWN, what };
		e.templates = templates;
		return e;
	}
	struct Seen
	{
		const Player *owner = nullptr;
		float health = 0.0f;
		int rank = 0;
		float experience = 0.0f;
		std::set<std::string> modifiers;
		unsigned long long readySum = 0;
		ObjectStatusMaskType status{};
		WeaponConditionFlags weaponFlags{};
		ObjectID containedBy = INVALID_ID;
		bool terror = false;
		const ThingTemplate *tmpl = nullptr;
		Coord3D pos{}; ///< lane FACT-C
	};
	UnsignedInt frame = 0;
	ObjectID highest = 0;
	const Player *player = nullptr;
	std::set<ObjectID> holders;
	std::map<ObjectID, Seen> seen;
	std::vector<Expect> expects;
	std::vector<std::string> unasserted;
	unsigned weaponModeActive = 0;
	float bounty = 0.0f; ///< lane FACT-C: the caster's player's bounty percent

	static void expectFrom(BehaviorModule *m, Object &holder, const SpecialPowerTemplate *t, EffectProbe &p, int depth)
	{
		const std::string cls = m->getModuleClassName();
		if (const WeaponModeSpecialPowerUpdate *wm = dynamic_cast<const WeaponModeSpecialPowerUpdate *>(m))
		{
			p.weaponModeActive += wm->active() ? 1 : 0;
			p.expects.push_back({ WEAPON_MODE, cls });
			return;
		}
		if (const SpecialAbilityUpdate *a = dynamic_cast<const SpecialAbilityUpdate *>(m))
		{
			const SpecialAbilityUpdateModuleData *d = a->abilityData();
			if (!d->m_triggerAttributeModifier.empty())
			{
				p.expects.push_back({ MODIFIER, cls + " TriggerAttributeModifier " + d->m_triggerAttributeModifier, d->m_triggerAttributeModifier });
			}
			if (dynamic_cast<const DominateEnemySpecialPower *>(m))
			{
				p.expects.push_back({ OWNER, cls });
			}
			else if (const LevelGrantSpecialPowerModuleData *ld = dynamic_cast<const LevelGrantSpecialPowerModuleData *>(d))
			{
				p.expects.push_back({ LEVEL, cls + str(" Experience %d", ld->m_experience), "", nullptr, (float)ld->m_experience });
			}
			else if (dynamic_cast<const CurseSpecialPower *>(m))
			{
				p.expects.push_back({ CURSE, cls });
			}
			else if (const WeaponFireSpecialAbilityUpdateModuleData *wd = dynamic_cast<const WeaponFireSpecialAbilityUpdateModuleData *>(d))
			{
				// a weapon that deals damage (its own DamageNugget or its projectile's warhead's) hits; one that only grabs (GrabNugget,
				// ContainTargetOnEffect) holds its victim
				const WeaponTemplate *w = shared().world->weaponStores().weapons().findWeaponTemplate(wd->m_specialWeapon);
				bool damage = false, grab = false, ocl = false, hordeAttack = false;
				std::set<std::string> made;
				std::vector<const WeaponTemplate *> ws{ w };
				for (size_t i = 0; i < ws.size() && ws[i]; ++i)
				{
					for (const std::shared_ptr<WeaponNugget> &n : ws[i]->m_nuggets)
					{
						damage = damage || dynamic_cast<const DamageNugget *>(n.get());
						grab = grab || dynamic_cast<const GrabNugget *>(n.get());
						if (const WeaponOCLNugget *on = dynamic_cast<const WeaponOCLNugget *>(n.get()))
						{
							ocl = true;
							oclTemplates(on->m_weaponOCLName, made);
						}
						hordeAttack = hordeAttack || dynamic_cast<const HordeAttackNugget *>(n.get());
						if (const ProjectileNugget *pn = dynamic_cast<const ProjectileNugget *>(n.get()))
						{
							ws.push_back(shared().world->weaponStores().weapons().findWeaponTemplate(pn->m_warheadTemplateName));
						}
					}
				}
				if (damage)
				{
					p.expects.push_back({ DAMAGE, cls + " SpecialWeapon " + wd->m_specialWeapon, "", w });
				}
				if (grab)
				{
					p.expects.push_back({ GRAB, cls + " SpecialWeapon " + wd->m_specialWeapon + " (GrabNugget)" });
				}
				if (ocl)
				{
					p.expects.push_back(spawnOf(cls + " SpecialWeapon " + wd->m_specialWeapon + " (WeaponOCLNugget)", made));
				}
				if (!damage && !grab && !ocl)
				{
					p.unasserted.push_back(cls + " SpecialWeapon " + wd->m_specialWeapon + (hordeAttack ? " (HordeAttackNugget: the members' own attack)" : " (no Damage, Grab or OCL nugget)"));
				}
			}
			else if (const ArrowStormUpdateModuleData *ad = dynamic_cast<const ArrowStormUpdateModuleData *>(d))
			{
				const WeaponTemplate *w = shared().world->weaponStores().weapons().findWeaponTemplate(ad->m_weaponTemplate);
				p.expects.push_back({ DAMAGE, cls + " WeaponTemplate " + ad->m_weaponTemplate, "", w });
			}
			else if (const HeroModeSpecialAbilityUpdateModuleData *hd = dynamic_cast<const HeroModeSpecialAbilityUpdateModuleData *>(d))
			{
				if (!hd->m_heroAttributeModifier.empty())
				{
					p.expects.push_back({ MODIFIER, cls + " HeroAttributeModifier " + hd->m_heroAttributeModifier, hd->m_heroAttributeModifier });
				}
			}
			else if (dynamic_cast<const ToggleHiddenSpecialAbilityUpdate *>(m))
			{
				p.expects.push_back({ STATUS, cls });
			}
			else if (dynamic_cast<const TeleportSpecialAbilityUpdate *>(m))
			{
				p.expects.push_back({ TELEPORT, cls });
			}
			else if (const ToggleMountedSpecialAbilityUpdateModuleData *td = dynamic_cast<const ToggleMountedSpecialAbilityUpdateModuleData *>(d))
			{
				// a MountedTemplate replaces the unit (seen by the caller, nothing else counts); without one the unit itself mounts: its status or weapon set
				p.expects.push_back({ MOUNT, cls + (td->m_mountedTemplate.empty() ? "" : " MountedTemplate " + td->m_mountedTemplate), td->m_mountedTemplate });
			}
			else if (const SummonReplacementSpecialAbilityUpdateModuleData *sd = dynamic_cast<const SummonReplacementSpecialAbilityUpdateModuleData *>(d))
			{
				// lane SUMMON-1: the Thrall Master's summons, ToggleMounted's replacement over its own data (RW 0x8B228C)
				p.expects.push_back({ MOUNT, cls + (sd->m_mountedTemplate.empty() ? "" : " MountedTemplate " + sd->m_mountedTemplate), sd->m_mountedTemplate });
			}
			else if (const ActivateModuleSpecialPowerModuleData *amd = dynamic_cast<const ActivateModuleSpecialPowerModuleData *>(d))
			{
				for (const ActivateModuleSpecialPowerModuleData::Entry &e : amd->m_triggers)
				{
					const NameKeyType key = holder.logic().things().nameKeys().findKey(e.tag);
					BehaviorModule *activated = key == NAMEKEY_INVALID ? nullptr : holder.findModuleByTag(key);
					if (activated && activated != m && depth < 2)
					{
						expectFrom(activated, holder, nullptr, p, depth + 1);
					}
				}
			}
			else if (cls == "SpecialAbilityUpdate")
			{
				const int type = d->m_specialPowerTemplate ? d->m_specialPowerTemplate->m_type : 0;
				if (type == 0x1D || type == 0x1A)
				{
					p.expects.push_back({ OWNER, cls + " (a capture)" });
				}
				if (type == 0x89)
				{
					p.expects.push_back({ TERROR, cls + " (SPECIAL_SCREECH: BeScary -> RadiateTerrorEx -> BeTerrified)" });
				}
				if (!d->m_specialObject.empty())
				{
					p.expects.push_back(spawnOf(cls + " SpecialObject " + d->m_specialObject, { d->m_specialObject }));
				}
			}
			else
			{
				p.unasserted.push_back(cls);
			}
			return;
		}
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		if (!sp || (t && sp->getSpecialPowerTemplate() != t))
		{
			return;
		}
		const SpecialPowerModule *spm = dynamic_cast<const SpecialPowerModule *>(sp);
		if (dynamic_cast<const WeaponChangeSpecialPowerModule *>(m))
		{
			p.expects.push_back({ WEAPON_FLAGS, cls });
		}
		else if (const OCLSpecialPower *op = dynamic_cast<const OCLSpecialPower *>(m))
		{
			std::set<std::string> made;
			oclTemplates(op->data()->m_ocl, made);
			for (const auto &u : op->data()->m_upgradeOCL)
			{
				oclTemplates(u.second, made);
			}
			p.expects.push_back(spawnOf(cls + " OCL " + op->data()->m_ocl, made));
		}
		else if (dynamic_cast<const PlayerHealSpecialPower *>(m))
		{
			p.expects.push_back({ HEAL, cls });
		}
		else if (dynamic_cast<const ScavengerSpecialPower *>(m))
		{
			p.expects.push_back({ BOUNTY, cls });
		}
		else if (dynamic_cast<const HordeDispatchSpecialPower *>(m))
		{
			// lane FACT-C: the horde passes the power on to its members (S-2001): their modules carry the results
		}
		else if (cls != "SpecialPowerModule" && !(spm && !spm->spData()->m_attributeModifier.empty()))
		{
			p.unasserted.push_back(cls);
		}
		if (spm && !spm->spData()->m_attributeModifier.empty())
		{
			p.expects.push_back({ MODIFIER, cls + " AttributeModifier " + spm->spData()->m_attributeModifier, spm->spData()->m_attributeModifier });
		}
	}

	static EffectProbe take(GameLogic &logic, Object *u, const SpecialPowerTemplate *t)
	{
		EffectProbe p;
		p.frame = logic.getFrame();
		p.player = u ? u->getControllingPlayer() : nullptr;
		p.bounty = p.player ? p.player->getBountyPercent() : 0.0f;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			p.highest = std::max(p.highest, o->getID());
			Seen s;
			s.owner = o->getControllingPlayer();
			s.health = o->getBodyModule() ? o->getBodyModule()->getHealth() : 0.0f;
			s.rank = o->getExperienceTracker() ? o->getExperienceTracker()->getRank() : 0;
			s.experience = o->getExperienceTracker() ? o->getExperienceTracker()->getExperience() : 0.0f;
			if (const AttributeModifierPool *pool = static_cast<const AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate")))
			{
				for (const std::string &n : pool->listNames())
				{
					s.modifiers.insert(n);
				}
			}
			for (const std::unique_ptr<BehaviorModule> &m : o->modules())
			{
				if (SpecialPowerModuleInterface *sp = m->getSpecialPower())
				{
					s.readySum += sp->getReadyFrame();
				}
			}
			s.tmpl = o->getTemplate();
			s.pos = *o->getPosition();
			s.status = o->getStatusBits();
			s.weaponFlags = o->getWeaponSetFlags();
			s.containedBy = o->getContainedBy() ? o->getContainedBy()->getID() : INVALID_ID;
			const EmotionTrackerUpdate *tr = EmotionTrackerUpdate::of(*o);
			s.terror = tr && (tr->requested(EMOTION_TERROR) || (tr->current() && tr->current()->type() == EMOTION_TERROR));
			p.seen[o->getID()] = s;
		}
		if (!u || !t)
		{
			return p;
		}
		std::vector<Object *> hs{ u };
		if (u->getContain())
		{
			for (Object *m : *u->getContain()->getContainedItemsList())
			{
				hs.push_back(m);
			}
		}
		for (Object *h : hs)
		{
			p.holders.insert(h->getID());
		}
		// the classes per holder: a horde's members carry the same modules, one expectation each class is enough
		std::set<std::string> done;
		for (Object *h : hs)
		{
			EffectProbe one;
			for (const std::unique_ptr<BehaviorModule> &m : h->modules())
			{
				const SpecialAbilityUpdate *a = dynamic_cast<const SpecialAbilityUpdate *>(m.get());
				const WeaponModeSpecialPowerUpdate *wm = dynamic_cast<const WeaponModeSpecialPowerUpdate *>(m.get());
				SpecialPowerModuleInterface *sp = m->getSpecialPower();
				const bool mine = a ? a->abilityData()->m_specialPowerTemplateName == t->getName()
									: wm ? wm->getSpecialPowerTemplate() == t : sp && sp->getSpecialPowerTemplate() == t;
				if (mine)
				{
					expectFrom(m.get(), *h, t, one, 0);
				}
			}
			p.weaponModeActive += one.weaponModeActive;
			for (const Expect &e : one.expects)
			{
				if (done.insert(e.what).second)
				{
					p.expects.push_back(e);
				}
			}
			for (const std::string &c : one.unasserted)
			{
				if (done.insert("unasserted " + c).second)
				{
					p.unasserted.push_back(c);
				}
			}
		}
		return p;
	}

	// what the cast did ("" when nothing); `missing` gets each expected result that did not show. `hits`: the caster's hits (DAMAGE), tracked from
	// before the cast (its frames before this probe's frame are the baseline)
	std::string compare(const EffectProbe &after, std::string &missing, const HitTracker *hits = nullptr) const
	{
		std::vector<std::string> out;
		auto was = [&](ObjectID id) -> const Seen * {
			auto it = seen.find(id);
			return it == seen.end() ? nullptr : &it->second;
		};
		for (const Expect &e : expects)
		{
			std::string got;
			switch (e.kind)
			{
			case OWNER:
				for (const auto &kv : after.seen)
				{
					const Seen *b = was(kv.first);
					if (b && !holders.count(kv.first) && b->owner != player && kv.second.owner == player && player)
					{
						got = str("object %u's owner became the caster's player", (unsigned)kv.first);
						break;
					}
				}
				break;
			case LEVEL:
			{
				// a rank, or the Experience (a horde's share is split among its members: summed over the own objects; the own horde stands away from
				// the enemy, no kill gives it any)
				float gained = 0.0f;
				int objects = 0;
				for (const auto &kv : after.seen)
				{
					const Seen *b = was(kv.first);
					if (b && kv.second.owner == player && !holders.count(kv.first))
					{
						if (kv.second.rank > b->rank)
						{
							got = str("object %u: rank %d -> %d", (unsigned)kv.first, b->rank, kv.second.rank);
							break;
						}
						if (kv.second.experience > b->experience)
						{
							gained += kv.second.experience - b->experience;
							++objects;
						}
					}
				}
				if (got.empty() && e.amount > 0.0f && gained >= e.amount * 0.9f)
				{
					got = str("%.0f experience on %d own objects", (double)gained, objects);
				}
				break;
			}
			case DAMAGE:
				if (!e.weapon)
				{
					break;
				}
				if (hits)
				{
					// the caster's hits are its ability's alone: the sweeps turn the idle mood scan off (nothing attacks unless ordered) and the
					// victims stand with twenty times their health (no hit is cut short by a death). So the caster has landed no hit before the cast,
					// and the first frame it lands any after it holds a hit of the weapon's size against that victim (Weapon.ini x Armor.ini; the
					// level's damage bonus allowed: up to 2.5 x)
					const int before = hits->maxVictims(0, frame);
					UnsignedInt first = 0;
					for (const HitTracker::Hit &x : hits->hits)
					{
						first = x.frame >= frame && (first == 0 || x.frame < first) ? x.frame : first;
					}
					if (before > 0)
					{
						break; // reported below: the caster fought before the cast
					}
					for (const HitTracker::Hit &x : hits->hits)
					{
						auto it = hitNominal.find(x.victim);
						const float nominal = it == hitNominal.end() ? 0.0f : it->second, sum = it == hitNominal.end() ? 0.0f : hitSum.at(x.victim);
						// at least its biggest nugget, at most all its nuggets at once with the level's bonus
						if (x.frame == first && nominal > 0.0f && x.amount >= nominal * 0.9f && x.amount <= sum * 2.5f)
						{
							got = str("its first hit after the cast (frame %u) %.0f, the weapon's %.0f (all nuggets %.0f) against the victim's armour; no hit before the cast",
									  first - frame, (double)x.amount, (double)nominal, (double)sum);
							break;
						}
					}
				}
				break;
			case CURSE:
				for (const auto &kv : after.seen)
				{
					const Seen *b = was(kv.first);
					if (b && !holders.count(kv.first) && kv.second.owner != player && kv.second.readySum > b->readySum)
					{
						got = str("object %u's power recharge restarted", (unsigned)kv.first);
						break;
					}
				}
				break;
			case MODIFIER:
				for (const auto &kv : after.seen)
				{
					const Seen *b = was(kv.first);
					if (kv.second.modifiers.count(e.modifier) && (!b || !b->modifiers.count(e.modifier)))
					{
						got = str("%s on object %u", e.modifier.c_str(), (unsigned)kv.first);
						break;
					}
				}
				break;
			case SPAWN:
			{
				// an object of one of the templates the module makes, new since the cast (seen now, or by the tracker frame by frame)
				auto expected = [&](const ThingTemplate *t) { return t && e.templates.count(t->getName()) > 0; };
				for (const auto &kv : after.seen)
				{
					if (kv.first > highest && expected(kv.second.tmpl))
					{
						got = str("%s %u made", kv.second.tmpl->getName().c_str(), (unsigned)kv.first);
						break;
					}
				}
				for (auto it = hits ? hits->made.begin() : std::map<ObjectID, HitTracker::Made>::const_iterator{}; hits && it != hits->made.end() && got.empty(); ++it)
				{
					if (it->first > highest && expected(it->second.tmpl))
					{
						got = str("%s %u made (frame %u)", it->second.tmpl->getName().c_str(), (unsigned)it->first, it->second.frame);
					}
				}
				break;
			}
			case HEAL:
				for (const auto &kv : after.seen)
				{
					const Seen *b = was(kv.first);
					if (b && kv.second.owner == player && kv.second.health > b->health + 1.0f)
					{
						got = str("object %u healed %.0f", (unsigned)kv.first, (double)(kv.second.health - b->health));
						break;
					}
				}
				break;
			case GRAB:
				for (const auto &kv : after.seen)
				{
					if (!holders.count(kv.first) && kv.second.owner != player && holders.count(kv.second.containedBy))
					{
						got = str("object %u held by the caster", (unsigned)kv.first);
						break;
					}
				}
				break;
			case TERROR:
				for (const auto &kv : after.seen)
				{
					const Seen *b = was(kv.first);
					const bool now = kv.second.terror || (hits && hits->terrified.count(kv.first) && hits->terrified.at(kv.first) >= frame);
					if (kv.second.owner != player && now && !(b && b->terror))
					{
						got = str("object %u terrified (EMOTION_TERROR)", (unsigned)kv.first);
						break;
					}
				}
				break;
			case TELEPORT:
				for (ObjectID id : holders)
				{
					const Seen *b = was(id);
					auto it = after.seen.find(id);
					if (b && it != after.seen.end() && dist2d(b->pos, it->second.pos) > 100.0f)
					{
						got = str("the caster moved %.0f in an instant", (double)dist2d(b->pos, it->second.pos));
						break;
					}
				}
				break;
			case BOUNTY:
				if (after.bounty != bounty)
				{
					got = str("the player's bounty went from %g to %g", (double)bounty, (double)after.bounty);
				}
				break;
			case WEAPON_MODE:
				if (after.weaponModeActive > weaponModeActive)
				{
					got = str("%u weapon modes on", after.weaponModeActive - weaponModeActive);
				}
				break;
			case WEAPON_FLAGS:
			case STATUS:
			case MOUNT:
				for (ObjectID id : holders)
				{
					if (e.kind == MOUNT && !e.modifier.empty())
					{
						break; // the replacement by the MountedTemplate (the caller's check) is the only result
					}
					const Seen *b = was(id);
					auto it = after.seen.find(id);
					const bool status = it != after.seen.end() && b && it->second.status != b->status, flags = it != after.seen.end() && b && it->second.weaponFlags != b->weaponFlags;
					if (e.kind == STATUS ? status : e.kind == WEAPON_FLAGS ? flags : status || flags)
					{
						got = e.kind == WEAPON_FLAGS || !status ? "the weapon set flags changed" : "the caster's status changed";
						break;
					}
				}
				break;
			}
			if (got.empty())
			{
				std::string detail;
				if (e.kind == DAMAGE && hits)
				{
					float best = 0.0f, nominal = 0.0f;
					for (const HitTracker::Hit &x : hits->hits)
					{
						if (x.frame >= frame && x.amount > best)
						{
							best = x.amount;
							auto it = hitNominal.find(x.victim);
							nominal = it == hitNominal.end() ? 0.0f : it->second;
						}
					}
					detail = str(" (the caster's biggest hit after the cast %.0f, the weapon's against that victim %.0f; %d victims hit in a frame before the cast)", (double)best,
								 (double)nominal, hits->maxVictims(0, frame));
				}
				if (e.kind == SPAWN)
				{
					std::string names;
					for (const std::string &n : e.templates)
					{
						names += (names.empty() ? "" : ", ") + n;
					}
					detail = names.empty() ? " (its OCL names no CreateObject template)" : " (none of " + names + " made)";
				}
				if (e.kind == WEAPON_FLAGS || e.kind == STATUS)
				{
					for (ObjectID id : holders)
					{
						const Seen *b = was(id);
						auto it = after.seen.find(id);
						detail += it == after.seen.end() ? str(" (holder %u gone)", (unsigned)id)
														 : b ? str(" (holder %u flags %08x -> %08x)", (unsigned)id, b->weaponFlags[0], it->second.weaponFlags[0]) : "";
					}
				}
				missing += e.what + ": no result" + detail + "; ";
			}
			else
			{
				out.push_back(e.what + ": " + got);
			}
		}
		for (const std::string &c : unasserted)
		{
			missing += "no effect of " + c + " is asserted; ";
		}
		if (expects.empty() && unasserted.empty())
		{
			missing += "no effect of it is asserted (module SpecialPowerModule); ";
		}
		std::string all;
		for (const std::string &x : out)
		{
			all += (all.empty() ? "" : ", ") + x;
		}
		return all;
	}
	// DAMAGE: the special weapon's biggest nugget against each object present before the cast (filled by nominals())
	std::map<ObjectID, float> hitNominal;
	std::map<ObjectID, float> hitSum;
	void nominals(GameLogic &logic)
	{
		for (const Expect &e : expects)
		{
			if (e.kind != DAMAGE || !e.weapon)
			{
				continue;
			}
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getControllingPlayer() == player || !o->getBodyModule())
				{
					continue;
				}
				std::string text;
				const std::vector<float> h = expectedHits(*e.weapon, *o, player, text);
				// the biggest single nugget (not the sum expectedHits appends): a frame's drop holds at least it when the weapon's hit lands
				float m = 0.0f;
				for (size_t k = 0; k < (h.size() > 1 ? h.size() - 1 : h.size()); ++k)
				{
					m = std::max(m, h[k]);
				}
				if (m > 0.0f)
				{
					hitNominal[o->getID()] = std::max(hitNominal[o->getID()], m);
					hitSum[o->getID()] = std::max(hitSum[o->getID()], h.back()); // every nugget at once (expectedHits' last entry with several)
				}
			}
		}
	}
};

// a victim that outlives the hits measured on it: twenty times its health (ActiveBody::setMaxHealth change type 2 adds the difference), its horde's
// members too
void toughen(GameLogic &logic, ObjectID id)
{
	Object *o = logic.findObjectByID(id);
	std::vector<Object *> all{ o };
	if (o && o->getContain() && o->getContain()->getContainedItemsList())
	{
		all.insert(all.end(), o->getContain()->getContainedItemsList()->begin(), o->getContain()->getContainedItemsList()->end());
	}
	for (Object *x : all)
	{
		if (ActiveBody *body = x ? dynamic_cast<ActiveBody *>(x->getBodyModule()) : nullptr)
		{
			body->setMaxHealth(body->getMaxHealth() * 20.0f, 2);
		}
	}
}

void unitsOf(const std::string &faction, Results &res)
{
	const factb::Inventory &inv = factb::inventory(faction);
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str());
	g.rig.local->depositMoney(5000000);
	Player *enemy = g.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	// the idle mood scan off (CombatState's scenario switch): nothing attacks unless ordered, so a hit the caster lands after its ability's press is
	// the ability's (EffectProbe DAMAGE)
	g.logic().combat().setAutoAcquireEnabled(false);
	// the structures that sell the player technologies (their presence is the RequiredObjectFilter of the units' upgrades, an armoury), one each by the base
	{
		int k = 0;
		for (const factb::Entry *e : inv.of(factb::Kind::Structure))
		{
			const ThingTemplate *btt = s.world->things().findTemplate(e->name);
			const CommandSet *cs = s.world->commands().findCommandSet(factb::stringFieldOf(*btt, "CommandSet"));
			bool sells = false;
			for (int i = 0; cs && i < CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *b = cs->getCommandButton(i);
				sells = sells || (b && b->getFinalOverride()->m_command == GUI_COMMAND_PLAYER_UPGRADE);
			}
			if (sells)
			{
				const Coord3D home = g.home();
				const Coord3D at = g.rig.freeSpot(home.x - 400.0f + 200.0f * (float)(k % 4), home.y - 400.0f - 200.0f * (float)(k / 4), 150.0f);
				g.rig.make(e->name, at.x, at.y);
				++k;
			}
		}
	}
	int n = 0;
	std::vector<const factb::Entry *> items;
	for (factb::Kind k : { factb::Kind::Horde, factb::Kind::Unit, factb::Kind::Hero })
	{
		for (const factb::Entry *e : inv.of(k))
		{
			items.push_back(e);
		}
	}
	for (const factb::Entry *e : items)
	{
		const ThingTemplate *tt = s.world->things().findTemplate(e->name);
		if (!factb::hasKindOf(*tt, "SELECTABLE") || e->via.size() == 1 && e->via.begin()->find(":InitialPayload") != std::string::npos)
		{
			continue; // a horde member is tested inside its horde; the spell book is not a unit
		}
		if (const char *only = std::getenv("FACTB_ONLY")) // a debugging run of some units: comma-separated parts of their names
		{
			bool any = false;
			for (size_t a = 0, b; a <= std::strlen(only); a = b + 1)
			{
				b = std::string(only).find(',', a);
				b = b == std::string::npos ? std::strlen(only) : b;
				any = any || (b > a && e->name.find(std::string(only).substr(a, b - a)) != std::string::npos);
			}
			if (!any)
			{
				continue;
			}
		}
		// each button gets a fresh unit at its top level with fresh targets at a new site (a press before it must not change it: a weapon toggle rejects
		// another ability, RejectedConditions)
		Coord3D c{};
		Object *u = nullptr;
		Targets tg;
		std::string enemyName = "GondorFighterHorde";
		ObjectID uid = INVALID_ID, enemyUnitId = INVALID_ID, enemyBuildingId = INVALID_ID, allyId = INVALID_ID, ownHordeId = INVALID_ID;
		auto clear = [&]() {
			// Targeted commands (including Bloodthirsty, RW 0x940435) keep taking clicks until a target or a right-click cancellation.
			// Destroying this fixture is not a player cancellation; do that through the HUD before trying to select the next unit.
			if (const CommandButton *pending = g.hud->input().ui().getGUICommand())
			{
				INFO("cancel pending fixture command " << pending->m_name);
				g.clickAt(c, HudInput::Button::Right);
				REQUIRE(g.hud->input().ui().getGUICommand() == nullptr);
			}
			for (ObjectID id : { uid, enemyUnitId, enemyBuildingId, allyId, ownHordeId })
			{
				if (Object *o = id != INVALID_ID ? g.logic().findObjectByID(id) : nullptr)
				{
					g.logic().destroyObject(o);
				}
			}
			uid = enemyUnitId = enemyBuildingId = allyId = ownHordeId = INVALID_ID;
			g.step(2);
		};
		auto refreshTargets = [&]() {
			auto again = [&](ObjectID &id, Object *&p, const char *name, float dx, float dy, Player *owner) {
				p = id != INVALID_ID ? g.logic().findObjectByID(id) : nullptr;
				if (!p || p->isEffectivelyDead())
				{
					p = g.rig.make(name, c.x + dx, c.y + dy, owner);
					id = p->getID();
				}
			};
			again(enemyUnitId, tg.enemyUnit, enemyName.c_str(), 160.0f, 40.0f, enemy);
			again(enemyBuildingId, tg.enemyBuilding, "GondorBarracks", -30.0f, 220.0f, enemy);
			again(allyId, tg.allyUnit, e->name.c_str(), -120.0f, -60.0f, nullptr);
		};
		auto spawn = [&](int levelsToGain = 10) -> bool {
			clear();
			const Coord3D home = g.home();
			const float r = 600.0f + 250.0f * (float)((n / 7) % 2); // the old site's objects are gone: the sites repeat, close to the base
			// lane FACT-C: a site past the map's low edge (a base in a corner) is mirrored onto the map: a unit there cannot walk to its target
			const float dx = r * std::cos(0.9f * (float)n), dy = r * std::sin(0.9f * (float)n);
			c = g.rig.freeSpot(home.x + (home.x + dx < 200.0f ? std::fabs(dx) : dx), home.y + (home.y + dy < 200.0f ? std::fabs(dy) : dy), 300.0f, false,
								 { { 90.0f, 20.0f }, { -100.0f, 100.0f }, { -120.0f, -60.0f } }); // the targets (refreshTargets)
			++n;
			u = g.rig.make(e->name, c.x, c.y);
			uid = u->getID();
			refreshTargets();
			g.step(20); // a horde's members are made, the objects settle
			toughen(g.logic(), enemyUnitId);
			u = g.logic().findObjectByID(uid);
			if (!u)
			{
				return false;
			}
			// the top level: a hero's abilities and a horde's level powers unlock with the level
			if (ExperienceTracker *xp = u->getExperienceTracker())
			{
				xp->gainExpForLevel(levelsToGain, false);
			}
			g.step(5);
			return true;
		};
		if (!spawn())
		{
			res.fail(e->name, "left the world in its first 20 frames");
			continue;
		}
		std::set<const CommandButton *> seen;
		for (int round = 0; round < 1; ++round)
		{
			u = g.logic().findObjectByID(uid);
			const CommandSet *cs = u ? s.world->commands().findCommandSet(u->getCommandSetName()) : nullptr;
			if (!cs)
			{
				break;
			}
			for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *b0 = cs->getCommandButton(i);
				const CommandButton *cb = b0 ? b0->getFinalOverride() : nullptr;
				if (!cb || !seen.insert(cb).second)
				{
					continue;
				}
				const int cmd = cb->m_command;
				const bool power = isPowerCommand(cmd);
				const bool upgrade = cmd == GUI_COMMAND_OBJECT_UPGRADE;
				const bool other = cmd == GUI_COMMAND_TOGGLE_WEAPONSET || cmd == GUI_COMMAND_HORDE_TOGGLE_FORMATION || cmd == GUI_COMMAND_SET_STANCE
								   || cmd == GUI_COMMAND_TOGGLE_STANCE || cmd == GUI_COMMAND_FIRE_WEAPON || cmd == GUI_COMMAND_BLOODTHIRSTY
								   || cmd == GUI_COMMAND_TOGGLE_NO_AUTO_ACQUIRE || cmd == GUI_COMMAND_SWITCH_WEAPON;
				if (!power && !upgrade && !other)
				{
					continue;
				}
				const std::string item = e->name + " " + cb->m_name;
				trace(item);
				if (cb->m_options & 0x10000000u)
				{
					res.pass(item, "NONPRESSABLE (option bit 28): a passive ability's indicator, not pressed");
					continue;
				}
				if (cmd == GUI_COMMAND_TOGGLE_STANCE)
				{
					res.fail(item, "S-289: the stance button opens the Palantir's radial (RW 0x96FAC6, tested by test_campui1_stance.cpp), not a command press; the STANCE_* hotkeys change the stance");
					continue;
				}
				// an enemy-object power tries the first of a horde's soldier, an enemy hero and an enemy troll that its SpecialPower's ObjectFilter takes
				// (Dominate Troll: ATTACK_TROLL_DOMINATE_OBJECT_FILTER), then, when it did not fire or showed no result, an enemy hero (Corrode Allegiance
				// and the curse take heroes only, a grab takes no hero)
				const SpecialPowerTemplate *spt = power && TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(cb->m_specialPowerName) : nullptr;
				std::string firstEnemy = "GondorFighterHorde";
				{
					// the filters the target must pass: the SpecialPower's ObjectFilter (an object target) and a dominate's AttributeModifierAffects
					// (Sharku's Tame the Beast: wargs)
					std::vector<const ObjectFilter *> filters;
					if (spt && spt->m_objectFilter && cb->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT | COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT))
					{
						filters.push_back(spt->m_objectFilter.get());
					}
					Object *unit = spt ? g.logic().findObjectByID(uid) : nullptr;
					for (size_t k = 0; unit && k < unit->modules().size(); ++k)
					{
						const DominateEnemySpecialPower *de = dynamic_cast<const DominateEnemySpecialPower *>(unit->modules()[k].get());
						const DominateEnemySpecialPowerModuleData *dd = de ? dynamic_cast<const DominateEnemySpecialPowerModuleData *>(de->abilityData()) : nullptr;
						if (dd && dd->m_specialPowerTemplateName == spt->getName() && dd->m_affects)
						{
							filters.push_back(dd->m_affects.get());
						}
					}
					for (const char *cand : { "GondorFighterHorde", "GondorBoromir", "GoblinCaveTroll", "MordorDrummerTroll", "MordorMountainTroll", "IsengardWargRiderHorde" })
					{
						bool ok = !filters.empty();
						for (const ObjectFilter *f : filters)
						{
							ok = ok && ObjectFilterMatch::allows(g.logic(), *f, s.world->things().findTemplate(cand), enemy, g.rig.local);
						}
						if (ok)
						{
							firstEnemy = cand;
							break;
						}
					}
				}
				bool retryHero = false;
				std::string firstTry;
				for (int attempt = 0; attempt < 2; ++attempt)
				{
				if (attempt == 1 && !retryHero)
				{
					break;
				}
				enemyName = attempt == 1 ? "GondorBoromir" : firstEnemy;
				if (!spawn(upgrade ? 0 : 10))
				{
					res.fail(item, "the unit left the world in its first 20 frames");
					continue;
				}
				if (upgrade)
				{
					// the player technology the button needs (bought at the armoury in the structure tests)
					for (const std::string &nu : cb->m_neededUpgrade)
					{
						const UpgradeTemplate *need = s.world->upgrades().findUpgrade(nu);
						if (need && need->getUpgradeType() == UPGRADE_TYPE_PLAYER)
						{
							g.rig.local->addCompletedUpgrade(nu);
						}
					}
					g.step(2);
				}
				if (!g.select(u))
				{
					res.fail(item, "the unit could not be selected with a click (selection " + g.selectionText() + ")");
					continue;
				}
				ControlBarButton b;
				if (!g.reveal(u, [&](const CommandButton &x) { return &x == cb; }, b))
				{
					// a button neither the arc nor the side bar shows (RW 0x92FF5C / 0x92F082: InPalantir past window 5, or neither flag) is off the bar in retail
					// too: the context command or a hotkey reaches it (hud4 placement test)
					const ControlBar::Placement place = ControlBar::placementOf(*cb, i, 0);
					if (cb->m_showButton && (place.arc || place.side))
					{
						res.fail(item, "not on the bar");
					}
					continue;
				}
				// lane DEVPORT-3C: a button with an EnableOnModelCondition mask (ROTWK-C's port of RW 0x942733's model condition gate: Lurtz's Carnage needs
				// WEAPONSET_TOGGLE_1, the Goblin King's stinger MOUNTED or HERO) is restricted until the player's weapon toggle / mount below; it is checked after them
				bool modelGated = false;
				for (std::uint32_t w : cb->m_enableOnModelCondition)
				{
					modelGated = modelGated || w != 0;
				}
				// lane SUMMON-1: likewise a button the unit's current model condition disables (DisableOnModelCondition: the Mountain Giant's Stomp while he throws
				// rocks) is restricted until the weapon toggle below
				for (size_t w = 0; w < cb->m_disableOnModelCondition.size(); ++w)
				{
					modelGated = modelGated || (u->getModelConditionBits()[w] & cb->m_disableOnModelCondition[w]) != 0;
				}
				modelGated = modelGated && !upgrade && b.state == ButtonState::Restricted;
				// lane MODS-3: a unit its DoCommandUpgrade put in the toggled weapon set at creation (the mountain giant's rock throwing, Upgrade_SwitchToRockThrowing
				// -> Command_ToggleMountainGiantRockThrow) has the buttons whose DisableOnModelCondition holds WEAPONSET_TOGGLE_1 (its Stomp) disabled: the player
				// toggles back first, so does the test
				bool toggleDisabled = false;
				for (size_t w = 0; w < cb->m_disableOnModelCondition.size(); ++w)
				{
					toggleDisabled = toggleDisabled || (cb->m_disableOnModelCondition[w] & u->getModelConditionBits()[w]) != 0; // tolerance: any shared bit
				}
				if (!upgrade && toggleDisabled && ((u->getWeaponSetFlags()[0] >> 24) & 1u) != 0) // tolerance: the WEAPONSET_TOGGLE_1 bit
				{
					ControlBarButton toggle;
					if (g.select(u) && g.reveal(u, [](const CommandButton &x) { return x.m_command == GUI_COMMAND_TOGGLE_WEAPONSET; }, toggle))
					{
						g.press(toggle);
						g.step(15);
					}
					g.select(u);
					if (!g.reveal(u, [&](const CommandButton &x) { return &x == cb; }, b))
					{
						res.fail(item, "the button left the bar after the weapon toggle");
						continue;
					}
				}
				if (b.state != ButtonState::Enabled && b.state != ButtonState::Active && !modelGated)
				{
					res.fail(item, str("state %d at level %d (not enabled)", (int)b.state, u->getExperienceTracker() ? u->getExperienceTracker()->getRank() : 0));
					continue;
				}
				const auto unportedBefore = g.hud->controlBar().unportedPresses();
				const unsigned pressFrame = g.frame();
				if (upgrade)
				{
					const UpgradeTemplate *up = s.world->upgrades().findUpgrade(cb->m_upgradeName);
					const unsigned spentBefore = g.spent();
					if (!up || !g.press(b))
					{
						res.fail(item, "the press sent nothing");
						continue;
					}
					const int expect = (int)std::ceil(up->getBuildTimeSeconds() * 5.0f);
					bool got = false;
					for (int f = 0; f < expect + 60 && !got; ++f)
					{
						g.step(1);
						got = u->hasUpgrade(up);
					}
					const int cost = up->calcCostToBuild(g.rig.local, u);
					if (!got)
					{
						res.fail(item, str("%s not on the unit after %d frames", cb->m_upgradeName.c_str(), expect + 60));
						continue;
					}
					std::string members;
					size_t without = 0, count = 0, carriers = 0;
					if (u->getContain() && u->isKindOfName("HORDE")) // a horde passes its upgrades on (a transport's riders are not its members)
					{
						g.step(3);
						for (Object *m : *u->getContain()->getContainedItemsList())
						{
							// the members the horde passes it to: those affected by it (RW 0x87566B -> RW 0x694914); a level of the horde
							// (LevelUpUpgrade) stays on the horde
							// lane FACT-C: a member that has it counts too (once it has it, affectedByUpgrade answers no: the count was 0 for every horde)
							if (m->hasUpgrade(up) || m->affectedByUpgrade(up))
							{
								++count;
								without += m->hasUpgrade(up) ? 0 : 1;
							}
							for (const std::unique_ptr<BehaviorModule> &bm : m->modules())
							{
								const UpgradeMux *mux = bm->getUpgrade();
								const std::vector<std::string> *tb = mux ? &mux->muxData()->m_triggeredBy : nullptr;
								carriers += tb && std::find(tb->begin(), tb->end(), cb->m_upgradeName) != tb->end() ? 1 : 0;
							}
						}
					}
					if (count == 0 && carriers > 0)
					{
						res.fail(item, str("no member has %s, though %zu member modules are triggered by it", cb->m_upgradeName.c_str(), carriers));
					}
					if ((int)(g.spent() - spentBefore) != cost)
					{
						res.fail(item, str("paid %u, calcCostToBuild %d", g.spent() - spentBefore, cost));
					}
					if (without > 0)
					{
						// RW 0x87566B (HordeContain slot 0x174): the horde passes its upgrades to every member that is affected by them
						res.fail(item, str("%zu of the %zu members lack %s", without, count, cb->m_upgradeName.c_str()));
					}
					res.pass(item, str("%s in %u frames, cost %d, on all %zu members it affects", cb->m_upgradeName.c_str(), g.frame() - pressFrame, cost, count));
					continue;
				}
				if (other)
				{
					// lane FACT-C: the state the press must change, read before and after: the weapon set flags (TOGGLE_WEAPONSET), the horde's contain
					// (HORDE_TOGGLE_FORMATION swaps it for the AlternateFormation's, RW 0x86C7F6), the stance (SET_STANCE: the button's stance)
					auto stateOf = [&]() {
						std::string out;
						Object *x = g.logic().findObjectByID(uid);
						if (!x)
						{
							return std::string("gone");
						}
						std::vector<Object *> all{ x };
						if (x->getContain() && x->getContain()->getContainedItemsList())
						{
							all.insert(all.end(), x->getContain()->getContainedItemsList()->begin(), x->getContain()->getContainedItemsList()->end());
						}
						for (Object *o : all)
						{
							if (cmd == GUI_COMMAND_TOGGLE_WEAPONSET)
							{
								out += str("%u ", (unsigned)((o->getWeaponSetFlags()[0] >> 24) & 0xFu));
							}
							else if (cmd == GUI_COMMAND_SET_STANCE)
							{
								for (const std::unique_ptr<BehaviorModule> &m : o->modules())
								{
									if (const StancesBehavior *sb = dynamic_cast<const StancesBehavior *>(m.get()))
									{
										out += str("%d ", (int)sb->getStance());
									}
								}
							}
						}
						if (cmd == GUI_COMMAND_HORDE_TOGGLE_FORMATION && x->getContain())
						{
							const Module *cm = dynamic_cast<const Module *>(x->getContain());
							out += str("%p", cm ? (const void *)cm->getModuleData() : nullptr);
						}
						return out;
					};
					const std::string stateBefore = stateOf();
					const bool sent = g.press(b);
					g.step(5);
					const std::string stateAfter = stateOf();
					std::string stance;
					for (size_t k = 0; cmd == GUI_COMMAND_SET_STANCE && k < cb->m_stances.size(); ++k)
					{
						stance += str("%d ", cb->m_stances[k]);
					}
					const bool stateCase = cmd == GUI_COMMAND_TOGGLE_WEAPONSET || cmd == GUI_COMMAND_HORDE_TOGGLE_FORMATION || cmd == GUI_COMMAND_SET_STANCE;
					if (sent && stateCase && g.hud->controlBar().unportedPresses() == unportedBefore
						&& (cmd == GUI_COMMAND_SET_STANCE ? (stance.empty() || stateAfter.find(stance.substr(0, stance.find(' '))) == std::string::npos) : stateAfter == stateBefore))
					{
						res.fail(item, str("the press of %s left the state as it was (%s -> %s)", GUICommandName(cmd), stateBefore.c_str(), stateAfter.c_str()));
					}
					else if (!sent)
					{
						res.fail(item, str("the press of %s sent nothing", GUICommandName(cmd)));
					}
					else if (g.hud->controlBar().unportedPresses() != unportedBefore)
					{
						res.fail(item, str("the press of %s is counted as unported", GUICommandName(cmd)));
					}
					else
					{
						res.pass(item, stateCase ? str("%s: %s -> %s", GUICommandName(cmd), stateBefore.c_str(), stateAfter.c_str()) : std::string(GUICommandName(cmd)));
					}
					continue;
				}
				// a power
				const SpecialPowerTemplate *t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(cb->m_specialPowerName) : nullptr;
				// the modules (of the unit and its members: looked up again after frames pass, a member may die), their recharge and unported counts
				std::vector<SpecialPowerModuleInterface *> mods;
				std::vector<unsigned> readyBefore;
				std::vector<unsigned long long> unportedFx;
				auto collect = [&]() {
					mods = t ? powerModules(g.logic(), u, t) : std::vector<SpecialPowerModuleInterface *>{};
					readyBefore.clear();
					unportedFx.clear();
					for (SpecialPowerModuleInterface *m : mods)
					{
						readyBefore.push_back(m->getReadyFrame());
						const SpecialPowerModule *spm = dynamic_cast<const SpecialPowerModule *>(m);
						unportedFx.push_back(spm ? spm->unportedEffects() : 0);
					}
				};
				collect();
				if (mods.empty())
				{
					res.fail(item, "no module of the unit or its members carries " + cb->m_specialPowerName);
					continue;
				}
				// an ability whose update requires the toggled weapon set (RequiredConditions WEAPON_TOGGLE: Lurtz's Carnage) is used the way a player
				// does: the unit's weapon toggle first
				bool needsToggle = false;
				for (const std::unique_ptr<BehaviorModule> &m : u->modules())
				{
					const SpecialAbilityUpdate *a = dynamic_cast<const SpecialAbilityUpdate *>(m.get());
					needsToggle = needsToggle || (a && a->abilityData()->m_specialPowerTemplateName == t->getName() && (a->abilityData()->m_requiredConditions & 2u) != 0);
				}
				if (needsToggle && ((u->getWeaponSetFlags()[0] >> 24) & 1u) == 0)
				{
					ControlBarButton toggle;
					if (g.select(u) && g.reveal(u, [](const CommandButton &x) { return x.m_command == GUI_COMMAND_TOGGLE_WEAPONSET; }, toggle))
					{
						g.press(toggle);
						g.step(15);
					}
					g.select(u);
					if (!g.reveal(u, [&](const CommandButton &x) { return &x == cb; }, b))
					{
						res.fail(item, "the button left the bar after the weapon toggle");
						continue;
					}
				}
				// lane SUMMON-1: a button the unit's current model condition disables (DisableOnModelCondition WEAPONSET_TOGGLE_1: the Mountain Giant's Stomp;
				// the giant starts throwing rocks, scripts.lua's OnMountainGiantCreated grants Upgrade_SwitchToRockThrowing and DoCommandUpgrade toggles the set)
				// is used the way a player does: the unit's weapon toggle first
				bool disabledNow = false;
				for (size_t w = 0; w < cb->m_disableOnModelCondition.size(); ++w)
				{
					disabledNow = disabledNow || (u->getModelConditionBits()[w] & cb->m_disableOnModelCondition[w]) != 0;
				}
				if (disabledNow)
				{
					ControlBarButton toggle;
					if (g.select(u) && g.reveal(u, [](const CommandButton &x) { return x.m_command == GUI_COMMAND_TOGGLE_WEAPONSET; }, toggle))
					{
						g.press(toggle);
						g.step(15);
					}
					g.select(u);
					if (!g.reveal(u, [&](const CommandButton &x) { return &x == cb; }, b))
					{
						res.fail(item, "the button left the bar after the weapon toggle");
						continue;
					}
				}
				// lane FACT-C: an ability whose update requires its user MOUNTED (RequiredConditions bit 0, RW 0x851551: the Goblin King's stinger on the
				// scorpion) is used the way a player does: the unit's ToggleMounted button first, and the mount must show (the MOUNTED condition)
				bool needsMount = false;
				for (const std::unique_ptr<BehaviorModule> &m : u->modules())
				{
					const SpecialAbilityUpdate *a = dynamic_cast<const SpecialAbilityUpdate *>(m.get());
					needsMount = needsMount || (a && a->abilityData()->m_specialPowerTemplateName == t->getName() && (a->abilityData()->m_requiredConditions & 1u) != 0);
				}
				if (needsMount)
				{
					ControlBarButton mount;
					if (!g.select(u) || !g.reveal(u, [](const CommandButton &x) { return x.m_specialPowerName == "SpecialAbilityToggleMounted"; }, mount) || !g.press(mount))
					{
						res.fail(item, "needs MOUNTED: the ToggleMounted button could not be pressed");
						continue;
					}
					g.step(40);
					u = g.logic().findObjectByID(uid);
					if (!u || !g.select(u) || !g.reveal(u, [&](const CommandButton &x) { return &x == cb; }, b))
					{
						res.fail(item, "the button left the bar after the mount");
						continue;
					}
				}
				if (b.state != ButtonState::Enabled && b.state != ButtonState::Active)
				{
					res.fail(item, str("state %d at level 10%s (not enabled)", (int)b.state, modelGated ? " after the weapon toggle / mount" : ""));
					continue;
				}
				// a position power whose result is on own units (a level grant, a heal) is cast at an own horde made there; one whose result is on
				// enemies (a dominate, a hit, a curse, a terror, a grab) at the enemy unit; the others at the ground by the unit
				bool atOwn = false, atEnemy = false, terror = false;
				for (const EffectProbe::Expect &x : EffectProbe::take(g.logic(), u, t).expects)
				{
					terror = terror || x.kind == EffectProbe::TERROR;
					atOwn = atOwn || x.kind == EffectProbe::LEVEL || x.kind == EffectProbe::HEAL;
					atEnemy = atEnemy || x.kind == EffectProbe::OWNER || x.kind == EffectProbe::DAMAGE || x.kind == EffectProbe::CURSE || x.kind == EffectProbe::TERROR
							  || x.kind == EffectProbe::GRAB;
				}
				// a screech terrifies within its EffectRange (180 for the Nazgul) around the caster: the enemy soldiers come closer than the site's
				if (terror)
				{
					if (Object *old = g.logic().findObjectByID(enemyUnitId))
					{
						g.logic().destroyObject(old);
					}
					enemyUnitId = g.rig.make(enemyName, c.x + 90.0f, c.y + 20.0f, enemy)->getID();
					g.step(5);
					toughen(g.logic(), enemyUnitId);
				}
				if (atOwn && ownHordeId == INVALID_ID)
				{
					ownHordeId = g.rig.make(ownHordeOf(faction), c.x - 100.0f, c.y + 100.0f)->getID();
				}
				// the caster's hits before the cast (the DAMAGE baseline), then the probe
				HitTracker hits;
				hits.casters.insert(uid);
				if (u->getContain())
				{
					for (Object *m : *u->getContain()->getContainedItemsList())
					{
						hits.casters.insert(m->getID());
					}
				}
				for (int f = 0; f < 30; ++f)
				{
					hits.track(g.logic());
					g.step(1);
				}
				hits.track(g.logic());
				u = g.logic().findObjectByID(uid);
				if (!u)
				{
					res.fail(item, "the unit left the world before the press");
					continue;
				}
				collect();
				EffectProbe probeBefore = EffectProbe::take(g.logic(), u, t);
				probeBefore.nominals(g.logic());
				if (!g.select(u) || !g.reveal(u, [&](const CommandButton &x) { return &x == cb; }, b))
				{
					res.fail(item, "the button left the bar during the baseline frames");
					continue;
				}
				if (!g.press(b))
				{
					res.fail(item, "the press sent nothing");
					continue;
				}
				std::string target = "none";
				if (cb->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT | COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT | COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT | COMMAND_OPTION_NEED_TARGET_POS))
				{
					Object *on = nullptr;
					if (cb->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT))
					{
						// (looked up by id: the baseline frames may have killed one)
						on = cb->m_affectsKindOf != KindOfMaskType{} ? g.logic().findObjectByID(enemyBuildingId) : g.logic().findObjectByID(enemyUnitId);
					}
					else if (cb->hasOption(COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT))
					{
						on = g.logic().findObjectByID(allyId);
					}
					else if (cb->hasOption(COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT))
					{
						on = g.logic().findObjectByID(enemyBuildingId);
					}
					else if (atOwn)
					{
						on = g.logic().findObjectByID(ownHordeId);
					}
					else if (atEnemy)
					{
						on = g.logic().findObjectByID(enemyUnitId);
					}
					// an object target: a horde is clicked on one of its members (the horde object has no body to pick); a position power is cast at the
					// horde's own position (its centre: an area dominate skips HORDE_MEMBERs and takes the horde within DominateRadius)
					const bool objectTarget = cb->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT | COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT | COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT);
					if (on && objectTarget && on->getContain() && on->getContain()->getContainCount() > 0 && !on->getContain()->getContainedItemsList()->empty())
					{
						on = on->getContain()->getContainedItemsList()->front();
					}
					// an object is clicked a few heights over its foot, from the south and from the north, until the click reaches it (the pick volume
					// of a tall model may not cover its foot; a big caster in front takes the click), pressing the button again before each further try;
					// its position is read again each time (it fights and moves)
					const ObjectID onId = on ? on->getID() : INVALID_ID;
					Coord3D ground{ c.x + 120.0f, c.y + 120.0f, 0.0f };
					// lane SUMMONLOC-1: the hero power click issues nothing where the location test refuses (RW 0x81E9C5 -> RW 0x6A4260 -> RW 0x82DFB7):
					// a LIMIT_DISTANCE 150 power (Hwaldar's Hillmen) is clicked nearer, as a player would
					if (Object *caster = g.logic().findObjectByID(uid); caster && !on && cb->m_command == GUI_COMMAND_SPECIAL_POWER && TheSpecialPowerStore)
					{
						// (the spots around the caster at 100: an earlier summon's units may stand on the first)
						const SpecialPowerTemplate *pw = TheSpecialPowerStore->findSpecialPowerTemplate(cb->m_specialPowerName);
						const Coord3D onGround{ ground.x, ground.y, g.logic().getGroundHeight(ground.x, ground.y) };
						if (pw && !ActionManager::canDoSpecialPowerAtLocation(*caster, onGround, pw, cb->m_options))
						{
							for (const Coord3D d : { Coord3D{ 70.0f, 70.0f, 0.0f }, Coord3D{ -70.0f, 70.0f, 0.0f }, Coord3D{ 70.0f, -70.0f, 0.0f }, Coord3D{ -70.0f, -70.0f, 0.0f } })
							{
								const Coord3D nearer{ c.x + d.x, c.y + d.y, g.logic().getGroundHeight(c.x + d.x, c.y + d.y) };
								if (ActionManager::canDoSpecialPowerAtLocation(*caster, nearer, pw, cb->m_options))
								{
									ground = nearer; // clicked at its terrain height: a click at z 0 under a raised terrain lands elsewhere
									break;
								}
							}
						}
					}
					const size_t sentBefore = g.hud->input().messageLog().size();
					bool reached = false;
					Coord3D at = on ? *on->getPosition() : ground;
					int tries = 0;
					for (float back : { 420.0f, -420.0f })
					{
						for (float dz : { 5.0f, 25.0f, 45.0f, 70.0f })
						{
							if (reached || (tries > 0 && !on))
							{
								break;
							}
							on = onId != INVALID_ID ? g.logic().findObjectByID(onId) : nullptr;
							if (onId != INVALID_ID && !on)
							{
								break; // the target died
							}
							if (tries++ > 0)
							{
								Object *again = g.logic().findObjectByID(uid);
								if (!again || !g.select(again) || !g.reveal(again, [&](const CommandButton &x) { return &x == cb; }, b) || !g.press(b))
								{
									break;
								}
							}
							at = on ? *on->getPosition() : ground;
							g.cameraBack = back;
							g.clickAt({ at.x, at.y, at.z + dz });
							const std::vector<std::string> &log = g.hud->input().messageLog();
							for (size_t k = sentBefore; k < log.size(); ++k)
							{
								reached = reached || log[k].compare(0, 20, "MSG_DO_SPECIAL_POWER") == 0;
							}
						}
					}
					target = on ? on->getTemplate()->getName() : "the ground";
					if (on && !reached)
					{
						// nothing sent: what the pointer hits there (for the report)
						const ICoord2D px = g.pointAt({ at.x, at.y, at.z + 5.0f });
						target += " (nothing sent; the pick there hits";
						for (const HudObjects::PickHit &h : HudObjects::pickHits(g.hud->input().context(), px, HudObjects::pickTypesForContext(g.hud->input().context()) | 0x100))
						{
							target += str(" %s/0x%X/%s", h.obj ? h.obj->getTemplate()->getName().c_str() : "?", h.type,
										  h.obj && h.obj->getControllingPlayer() ? h.obj->getControllingPlayer()->getPlayerName().c_str() : "-");
						}
						target += ")";
					}
				}
				if (cb->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT | COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT | COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT)
					&& !cb->hasOption(COMMAND_OPTION_NEED_TARGET_POS) && target.find("nothing sent") != std::string::npos)
				{
					g.hud->input().ui().setGUICommand(nullptr);
					if (attempt == 0 && cb->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT) && firstEnemy != "GondorBoromir")
					{
						retryHero = true;
						firstTry = "target " + target;
						continue;
					}
					res.fail(item, cb->m_specialPowerName + ": the click on its target sent nothing (target " + target + ")" + (firstTry.empty() ? "" : " (first try, " + firstTry + ")"));
					continue;
				}
				// the modules belong to the unit (and its members): a power that replaces the unit (a fell beast's rider dismounting,
				// ToggleMountedSpecialAbilityUpdate's MountedTemplate) destroys them, so the unit's presence is checked first, by id
				bool fired = false, replaced = false;
				std::vector<ObjectID> holders;
				holders.push_back(uid);
				if (u->getContain())
				{
					for (Object *m : *u->getContain()->getContainedItemsList())
					{
						holders.push_back(m->getID());
					}
				}
				auto holdersAlive = [&]() {
					for (ObjectID id : holders)
					{
						if (!g.logic().findObjectByID(id))
						{
							return false;
						}
					}
					return true;
				};
				for (int f = 0; f < 300 && !fired && !replaced; ++f)
				{
					g.step(1);
					hits.track(g.logic());
					if (!holdersAlive())
					{
						replaced = true;
						break;
					}
					for (size_t k = 0; k < mods.size(); ++k)
					{
						fired = fired || mods[k]->getReadyFrame() != readyBefore[k];
					}
				}
				if (replaced)
				{
					// the unit (or a member) left the world after the press: a replacement shows as a new object of the player at the spot
					Object *after = nullptr;
					for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
					{
						if (o->getID() > uid && o->getControllingPlayer() == g.rig.local && o->getTemplate() && o->getTemplate()->getName() != e->name
							&& dist2d(*o->getPosition(), c) < 600.0f && factb::hasKindOf(*o->getTemplate(), "SELECTABLE"))
						{
							after = o;
						}
					}
					if (after)
					{
						res.pass(item, str("%s replaced the unit with %s after %u frames", cb->m_specialPowerName.c_str(), after->getTemplate()->getName().c_str(),
										   g.frame() - pressFrame));
					}
					else
					{
						res.fail(item, str("%s: the unit left the world %u frames after the press, nothing replaced it", cb->m_specialPowerName.c_str(), g.frame() - pressFrame));
					}
					g.hud->input().ui().setGUICommand(nullptr);
					continue;
				}
				unsigned long long unported = 0;
				for (size_t k = 0; k < mods.size(); ++k)
				{
					const SpecialPowerModule *spm = dynamic_cast<const SpecialPowerModule *>(mods[k]);
					unported += spm ? spm->unportedEffects() - unportedFx[k] : 0;
				}
				if (!fired && attempt == 0 && cb->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT))
				{
					g.hud->input().ui().setGUICommand(nullptr);
					retryHero = true;
					firstTry = "target " + target + ": did not fire (sent: " + g.lastMessages() + ")";
					continue;
				}
				if (!fired)
				{
					res.fail(item, str("%s (target %s) did not fire in 300 frames (sent: %s)%s", cb->m_specialPowerName.c_str(), target.c_str(), g.lastMessages().c_str(),
									   firstTry.empty() ? "" : (" (first try, " + firstTry + ")").c_str()));
				}
				else if (unported > (unsigned long long)std::max<size_t>(1, mods.size())) // lane FACT-C: one S-920 note per module (a horde's members each carry one)
				{
					res.fail(item, str("%s fired with %llu unported effects", cb->m_specialPowerName.c_str(), unported));
				}
				if (g.hud->controlBar().unportedPresses() != unportedBefore)
				{
					res.fail(item, "the press is counted as unported");
				}
				// the effect, not only the recharge: what the power's modules did (see EffectProbe)
				std::string effect;
				if (fired)
				{
					// read every 10 frames for up to 200 (an ability unpacks, walks to its target, then runs its effect step)
					std::string missing;
					for (int k = 0; k < 21; ++k)
					{
						for (int f = 0; f < (k == 0 ? 0 : 10); ++f) // k 0: right after the fire (a toggle, a caster that dies in the fight)
						{
							g.step(1);
							hits.track(g.logic());
						}
						Object *uNow = g.logic().findObjectByID(uid);
						const EffectProbe probeAfter = EffectProbe::take(g.logic(), uNow, t);
						missing.clear();
						effect = probeBefore.compare(probeAfter, missing, &hits);
						if (missing.empty())
						{
							break;
						}
					}
					if (!missing.empty() && attempt == 0 && cb->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT) && firstEnemy != "GondorBoromir")
					{
						g.hud->input().ui().setGUICommand(nullptr);
						retryHero = true;
						firstTry = "target " + target + ": " + missing;
						continue;
					}
					if (!missing.empty())
					{
						res.fail(item, cb->m_specialPowerName + " (target " + target + ") fired but " + missing + (firstTry.empty() ? "" : " (first try, " + firstTry + ")"));
					}
				}
				// one unported effect is S-920's: the victims' "outside the playable area" flag (RW 0xC0F374) every SpecialPowerModule scan counts
				res.pass(item, str("%s fired (target %s) after %u frames: %s%s", cb->m_specialPowerName.c_str(), target.c_str(), g.frame() - pressFrame, effect.c_str(),
								   unported ? " (S-920 noted)" : ""));
				g.hud->input().ui().setGUICommand(nullptr);
				}
			}
		}
		clear();
		g.step(3);
	}
}
} // namespace

TEST_CASE("factb units: every Isengard unit, horde and hero ability and upgrade button")
{
	if (!haveWorld("factb units"))
	{
		return;
	}
	Results r;
	unitsOf("FactionIsengard", r);
	r.print("units FactionIsengard");
	r.check({ { "* Command_ToggleStance", "S-289" }, { "MordorWorker Command_Harvest", "S-1993 closed: the sweep asserts no SPECIAL_HARVEST effect; test_lumber1.cpp does" }, { "IsengardPorter Command_PorterExtinguishFire", "S-1997" },
		{ "IsengardBallista Command_BombardEvil", "S-1990" }, { "IsengardSaruman Command_SarumanThunderBolt", "S-180 (the bolt's damage lands; its WeaponOCLNugget makes nothing)" },
		{ "IsengardSharku Command_SpecialAbilitySharkuManEater", "S-180 (GrabNugget delivery)" } });
}

TEST_CASE("factb units: every Mordor unit, horde and hero ability and upgrade button")
{
	if (!haveWorld("factb units"))
	{
		return;
	}
	Results r;
	unitsOf("FactionMordor", r);
	r.print("units FactionMordor");
	// lane FLY-1: the fell beasts' dismounts land and fire (GiantBirdAIUpdate, MOVING_TO_DISMOUNT): S-1994's pins went
	r.check({ { "* Command_ToggleStance", "S-289" }, { "MordorWorker Command_Harvest", "S-1993 closed: the sweep asserts no SPECIAL_HARVEST effect; test_lumber1.cpp does" }, { "MordorPorter Command_PorterExtinguishFire", "S-1997" },
		{ "MordorHaradrimArcherHorde Command_HaradrimArcherBarbedArrowAbility", "S-1997" } });
}

// The pick of a click on a siege engine (the train sweep could not select Isengard's battering ram or Mordor's catapult with a click): the hits at
// the pixel of the object's position, for the log; the ballista (selectable) as the reference
TEST_CASE("factb pick: the hits of a click on Isengard's and Mordor's siege engines (diagnostic)")
{
	if (!haveWorld("factb pick"))
	{
		return;
	}
	factb::HudGame g(shared(), "FactionIsengard");
	const Coord3D home = g.home();
	int k = 0;
	for (const char *name : { "IsengardBallista", "IsengardBatteringRam", "MordorCatapult", "MordorBatteringRam", "MordorWorker", "MordorHaradrimRiderHorde", "MordorMountainTroll" })
	{
		const Coord3D at = g.rig.freeSpot(home.x + 300.0f * (float)k++, home.y, 200.0f);
		Object *o = g.rig.make(name, at.x, at.y);
		g.step(10);
		const ICoord2D px = g.pointAt(*o->getPosition());
		const std::vector<HudObjects::PickHit> hits = HudObjects::pickHits(g.hud->input().context(), px, HudObjects::pickTypesForContext(g.hud->input().context()) | 0x100);
		std::printf("factb pick %s (collision type 0x%X, contains %zu): %zu hits", name, HudObjects::collisionTypeOf(g.hud->input().context(), *o),
					o->getContain() ? (size_t)o->getContain()->getContainCount() : (size_t)0, hits.size());
		for (const HudObjects::PickHit &h : hits)
		{
			std::printf(" [%s type 0x%X]", h.obj ? h.obj->getTemplate()->getName().c_str() : "?", h.type);
		}
		Object *probe = o->getContain() && !o->getContain()->getContainedItemsList()->empty() ? o->getContain()->getContainedItemsList()->front() : o;
		std::printf("; probe %s selectable %d unselectable-status %d; selected after a click: %s (%s)\n", probe->getTemplate()->getName().c_str(),
					(int)HudObjects::isSelectable(*probe), (int)probe->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE")),
					g.select(o) ? "yes" : "no", g.selectionText().c_str());
		g.logic().destroyObject(o);
		g.step(2);
	}
}

// The Berserker's Light Torch (WeaponChangeSpecialPowerModule, RW 0x8C8420) through its button: the first press sets WEAPONSET_TOGGLE_1 (the torch
// weapon set), holds the berserker (disabled type 4) for ToggleOnSleepFrames (25) and shows the special model condition; the second press clears the
// flag again and holds it for ToggleOffSleepFrames (0: not at all)
TEST_CASE("factb units: the Berserker's Light Torch button toggles its weapon set flag (WeaponChangeSpecialPowerModule, RW 0x8C8420)")
{
	if (!haveWorld("factb torch"))
	{
		return;
	}
	factb::HudGame g(shared(), "FactionIsengard");
	const Coord3D home = g.home();
	const Coord3D at = g.rig.freeSpot(home.x, home.y, 200.0f);
	Object *b = g.rig.make("IsengardBeserker", at.x, at.y);
	g.step(10);
	const int toggle1 = 24; // WEAPONSET_TOGGLE_1 in RW 0xDA1328 (WeaponSetToggle.h)
	auto flagOn = [&]() { return ((b->getWeaponSetFlags()[0] >> toggle1) & 1u) != 0; };
	REQUIRE_FALSE(flagOn());
	for (int press = 0; press < 2; ++press)
	{
		REQUIRE(g.select(b));
		ControlBarButton btn;
		REQUIRE(g.reveal(b, [](const CommandButton &c) { return c.m_name == "Command_SpecialAbilityLightTorch"; }, btn));
		REQUIRE(btn.state == ButtonState::Enabled);
		const unsigned frame = g.frame();
		REQUIRE(g.press(btn));
		g.step(2);
		CHECK(flagOn() == (press == 0));
		if (press == 0)
		{
			CHECK(b->getDisabledMask() != 0); // held while the torch is lit (ToggleOnSleepFrames = 25)
			for (int f = 0; f < 30; ++f)
			{
				g.step(1);
			}
			CHECK(b->getDisabledMask() == 0);
			CHECK(g.frame() - frame >= 25u);
		}
		else
		{
			CHECK(b->getDisabledMask() == 0); // ToggleOffSleepFrames = 0
		}
		// the recharge of the power passes before the next press
		for (int f = 0; f < 60 && press == 0; ++f)
		{
			g.step(1);
		}
	}
}

namespace
{
// every buildable hero of the faction through the starting building's REVIVE buttons: the press recruits it for the record's cost, it comes out, is
// killed, its record turns into a revive record, the REVIVE button shows it again and a second press revives it
void heroesOf(const std::string &faction, Results &res)
{
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str());
	g.rig.local->depositMoney(5000000);
	const Coord3D home = g.home();
	const Coord3D at = g.rig.freeSpot(home.x, home.y, 450.0f);
	// the keep that carries the REVIVE buttons: in a game the starting castle unpacks it (CastleBehavior; the bare map load of these tests makes no
	// castle, and the StartingBuilding template itself has no command set), so the faction's structure template whose CommandSet holds REVIVE
	// buttons is made directly
	const std::string &side = g.rig.local->getPlayerTemplate()->m_side;
	const ThingTemplate *keepTemplate = nullptr;
	for (const ThingTemplate *t : s.world->things().templates())
	{
		const FieldValue *bonus = t->getFinalOverride()->findField("CommandPointBonus");
		const long long *points = bonus ? std::get_if<long long>(bonus) : nullptr;
		if (keepTemplate || factb::stringFieldOf(*t, "Side") != side || !factb::hasKindOf(*t, "STRUCTURE") || !points || *points <= 0)
		{
			continue; // the faction's own keep (a CommandPointBonus), not a campaign camp's
		}
		const CommandSet *cs = s.world->commands().findCommandSet(factb::stringFieldOf(*t, "CommandSet"));
		for (int i = 0; cs && i < CommandSet::MAX_BUTTONS && !keepTemplate; ++i)
		{
			const CommandButton *b = cs->getCommandButton(i);
			keepTemplate = b && b->getFinalOverride()->m_command == GUI_COMMAND_REVIVE ? t : nullptr;
		}
	}
	if (!keepTemplate)
	{
		res.fail(faction, "no structure of the side has REVIVE buttons");
		return;
	}
	Object *keep = g.rig.make(keepTemplate->getName(), at.x, at.y);
	g.step(10);
	MESSAGE(faction << ": the REVIVE buttons are on " << keep->getTemplate()->getName());
	PlayerHeroList &list = g.rig.local->heroes();
	std::vector<std::string> heroes;
	for (const HeroRecord &r : list.records())
	{
		heroes.push_back(r.templateName);
	}
	// the ring hero: its REVIVE slot (Command_RingHeroReviveSlot, NEED_UPGRADE HIDE_WHILE_DISABLED) needs Upgrade_RingHero (the player's) and
	// Upgrade_FortressRingHero (the keep's), which a ring bearer entering the keep grants (CastleBehavior UpgradeForRingEntry); granted here
	for (const std::string &ring : g.rig.local->getPlayerTemplate()->m_buildableRingHeroesMP)
	{
		if (std::find(heroes.begin(), heroes.end(), ring) == heroes.end())
		{
			heroes.push_back(ring);
		}
	}
	g.rig.local->addCompletedUpgrade("Upgrade_RingHero");
	keep->giveUpgrade("Upgrade_FortressRingHero");
	g.step(3);
	// the record of a hero by its template (a death may move or replace it)
	auto indexOf = [&](const std::string &name) -> int {
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (list.records()[i].templateName == name)
			{
				return (int)i;
			}
		}
		return -1;
	};
	for (const std::string &name : heroes)
	{
		HeroRecord rec;
		rec.templateName = name;
		if (rec.templateName == "CreateAHero")
		{
			continue; // the Create-A-Hero record (HERO-2's tests)
		}
		const std::string item = "hero " + rec.templateName;
		trace(item);
		for (int round = 0; round < 2; ++round)
		{
			const char *what = round == 0 ? "recruit" : "revive";
			if (!g.select(keep))
			{
				res.fail(item, std::string(what) + ": the starting building could not be selected with a click");
				break;
			}
			ControlBarButton b;
			const int idx = indexOf(name);
			if (idx < 0 || !g.revealBar(keep, [&](const ControlBarButton &x) { return x.button->m_command == GUI_COMMAND_REVIVE && x.reviveIndex == idx; }, b))
			{
				res.fail(item, std::string(what) + ": no REVIVE button shows the record");
				break;
			}
			if (b.state != ButtonState::Enabled)
			{
				res.fail(item, str("%s: the button's state is %d", what, (int)b.state));
				break;
			}
			const int cost = list.costAt(g.logic(), *g.rig.local, idx, keep);
			const unsigned spentBefore = g.spent();
			const ObjectID before = g.highestID();
			if (!g.press(b))
			{
				res.fail(item, std::string(what) + ": the press sent nothing");
				break;
			}
			Object *hero = nullptr;
			for (int f = 0; f < 3000 && !hero; ++f)
			{
				g.step(1);
				hero = g.newestOf(rec.templateName, before);
			}
			if (!hero)
			{
				res.fail(item, std::string(what) + ": no hero came in 3000 frames");
				break;
			}
			if ((int)(g.spent() - spentBefore) != cost)
			{
				res.fail(item, str("%s paid %u, the record's cost %d", what, g.spent() - spentBefore, cost));
			}
			if (round == 1)
			{
				res.pass(item, str("recruited and revived through the REVIVE buttons (revive cost %d)", cost));
				g.logic().destroyObject(hero);
				g.step(2);
				break;
			}
			const std::vector<std::string> &rings = g.rig.local->getPlayerTemplate()->m_buildableRingHeroesMP;
			if (std::find(rings.begin(), rings.end(), name) != rings.end())
			{
				// the ring hero (RemoveUpgradeUpgrade: building it takes Upgrade_RingHero / Upgrade_FortressRingHero from the player and its objects;
				// ExperienceLevelCreate LevelToGrant 10; CreateObjectDie OCL_TheOneRing): no revive record, the ring drops where it dies, and with the ring
				// brought back (the two upgrades again) the slot recruits it once more
				g.step(3);
				if (g.rig.local->hasUpgradeComplete("Upgrade_RingHero") || keep->hasUpgrade("Upgrade_FortressRingHero"))
				{
					res.fail(item, "the ring upgrades stay after the ring hero was made");
				}
				const ObjectID heroId = hero->getID();
				const int level = hero->getExperienceTracker() ? hero->getExperienceTracker()->getRank() : -1;
				const ObjectID beforeDeath = g.highestID();
				hero->kill(0);
				for (int f = 0; f < 400 && g.logic().findObjectByID(heroId); ++f)
				{
					g.step(1);
				}
				g.step(5);
				int dropped = 0;
				for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
				{
					dropped += o->getID() > beforeDeath && o->getTemplate()->getName().find("Ring") != std::string::npos ? 1 : 0;
				}
				if (dropped == 0)
				{
					res.fail(item, "its death dropped no ring (CreateObjectDie OCL_TheOneRing)");
				}
				g.rig.local->addCompletedUpgrade("Upgrade_RingHero");
				keep->giveUpgrade("Upgrade_FortressRingHero");
				g.step(3);
				if (round == 0)
				{
					res.pass(item, str("recruited through the ring slot (cost %d, level %d), the ring upgrades taken, the ring dropped at its death", cost, level));
				}
				continue; // round 1: recruited again with the ring back
			}
			// the death: a killing hit, then the record becomes a revive record
			const ObjectID heroId = hero->getID();
			hero->kill(0);
			for (int f = 0; f < 400 && g.logic().findObjectByID(heroId); ++f)
			{
				g.step(1);
			}
			g.step(5);
			const int deadIdx = indexOf(name);
			if (deadIdx < 0 || !list.at(deadIdx)->dead)
			{
				res.fail(item, "the dead hero's record is not a revive record");
				break;
			}
		}
	}
}
} // namespace

TEST_CASE("factb heroes: every Isengard hero is recruited and revived through the starting building's REVIVE buttons")
{
	if (!haveWorld("factb heroes"))
	{
		return;
	}
	Results r;
	heroesOf("FactionIsengard", r);
	r.print("heroes FactionIsengard");
	r.check();
}

TEST_CASE("factb heroes: every Mordor hero is recruited and revived through the starting building's REVIVE buttons")
{
	if (!haveWorld("factb heroes"))
	{
		return;
	}
	Results r;
	heroesOf("FactionMordor", r);
	r.print("heroes FactionMordor");
	r.check();
}

namespace
{
float healthOf(GameLogic &logic, ObjectID id)
{
	Object *o = logic.findObjectByID(id);
	if (!o)
	{
		return 0.0f;
	}
	float h = o->getBodyModule() ? o->getBodyModule()->getHealth() : 0.0f;
	if (o->getContain() && o->isKindOfName("HORDE"))
	{
		for (Object *m : *o->getContain()->getContainedItemsList())
		{
			h += m->getBodyModule() ? m->getBodyModule()->getHealth() : 0.0f;
		}
	}
	return h;
}

// every unit, horde and hero that can attack, ordered by a click on an enemy (a horde of soldiers; a structure for what attacks only walls and
// buildings): the enemy loses health within 200 frames. What a unit deals per second is printed (the INI's weapon against the target's armour)
void fightsOf(const std::string &faction, Results &res)
{
	const factb::Inventory &inv = factb::inventory(faction);
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str());
	Player *enemy = g.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	int n = 0;
	for (factb::Kind k : { factb::Kind::Horde, factb::Kind::Unit, factb::Kind::Hero })
	{
		for (const factb::Entry *e : inv.of(k))
		{
			const ThingTemplate *tt = s.world->things().findTemplate(e->name);
			const bool member = e->via.size() == 1 && e->via.begin()->find(":InitialPayload") != std::string::npos;
			const bool armed = ObjectTemplateInfoBuilder::build(*tt->getFinalOverride()).canPossiblyHaveAnyWeapon || factb::nugget(*tt, "HordeContain");
			if (member || !factb::hasKindOf(*tt, "SELECTABLE") || !armed || factb::hasKindOf(*tt, "STRUCTURE") || factb::hasKindOf(*tt, "DOZER")
				|| factb::hasKindOf(*tt, "MINE"))
			{
				continue;
			}
			const std::string item = "fight " + e->name;
			trace(item);
			const bool wallsOnly = factb::hasKindOf(*tt, "CAN_ATTACK_WALLS") && factb::hasKindOf(*tt, "SIEGEENGINE");
			std::string outcome, firstOutcome;
			// the enemy at 170, then (a siege engine with a minimum range: a catapult) at 420
			for (float off : { 170.0f, 170.0f, 420.0f })
			{
				const Coord3D home = g.home();
				const float r = 600.0f + 250.0f * (float)((n / 7) % 2);
				const Coord3D c = g.rig.freeSpot(home.x + r * std::cos(0.9f * (float)n), home.y + r * std::sin(0.9f * (float)n), 320.0f, false, { { off, 30.0f } }, nullptr, 160.0f);
				++n;
				Object *u = g.rig.make(e->name, c.x, c.y);
				Object *target = g.rig.make(wallsOnly ? "GondorBarracks" : "GondorFighterHorde", c.x + off, c.y + 30.0f, enemy);
				// Stage a fight, not an automatic charge before the HUD's order.
				const unsigned noAuto = (unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE");
				for (Object *actor : { u, target })
				{
					actor->setStatus(noAuto, true);
					if (const auto *contain = actor->getContain())
					{
						if (const auto *members = contain->getContainedItemsList())
						{
							for (Object *member : *members)
							{
								member->setStatus(noAuto, true);
							}
						}
					}
				}
				const ObjectID uid = u->getID(), tid = target->getID();
				g.step(15);
				const float before = healthOf(g.logic(), tid);
				u = g.logic().findObjectByID(uid);
				target = g.logic().findObjectByID(tid);
				if (!u || !target || !g.select(u))
				{
					outcome = "the unit could not be selected with a click";
				}
				else if (!(target = g.logic().findObjectByID(tid)))
				{
					// lane OFFMAP-1: select() runs frames; the test read the target through the pointer it had before them (a crash when the enemy died then)
					outcome = "the enemy died before the order";
				}
				else
				{
					// RotWK's default mouse: a left click on an enemy attacks. A horde is clicked through a member; lane OFFMAP-1: as select() does, up to four
					// members are tried until the HUD sends the attack (a click that met the ground sent a move: the giant birds' scenes, once the start
					// units kept the positions they were made at, RW 0x745A64)
					std::vector<Object *> ats{ target };
					if (target->getContain() && !target->getContain()->getContainedItemsList()->empty())
					{
						ats.assign(target->getContain()->getContainedItemsList()->begin(), target->getContain()->getContainedItemsList()->end());
					}
					int clicks = 0;
					for (size_t k = 0; k < ats.size() && k < 4; ++k)
					{
						Object *at = ats[k];
						const size_t logged = g.hud->input().messageLog().size();
						// community #74: the click goes where the member is drawn (the last completed frame's pose): a member still walking to its slot
						// stands a step ahead of its drawing
						const Drawable *drawn = g.rig.game->drawables().findByObject(at->getID());
						const Coord3D seen = drawn ? *drawn->getPosition() : *at->getPosition();
						g.clickAt({ seen.x, seen.y, seen.z + 10.0f });
						++clicks;
						g.step(1);
						const std::vector<std::string> &log = g.hud->input().messageLog();
						bool attacked = false;
						for (size_t i = logged; i < log.size(); ++i)
						{
							attacked = attacked || log[i].rfind("MSG_DO_ATTACK_OBJECT", 0) == 0;
						}
						if (attacked)
						{
							break;
						}
					}
					if (clicks > 1)
					{
						std::printf("  info: fight %s: %d clicks on the enemy horde's members before the HUD's attack was seen\n", e->name.c_str(), clicks);
					}
					const unsigned start = g.frame();
					float now = before;
					for (int f = 0; f < 200 && now >= before; ++f)
					{
						g.step(1);
						now = healthOf(g.logic(), tid);
					}
					const unsigned first = g.frame() - start;
					g.step(25);
					const float lost = before - healthOf(g.logic(), tid);
					// lane PATHPERF-1: where the two ended (a melee unit stopping short of its target, S-2005)
					const Object *ue = g.logic().findObjectByID(uid), *te = g.logic().findObjectByID(tid);
					const float apart = ue && te ? dist2d(*ue->getPosition(), *te->getPosition()) : -1.0f;
					outcome = lost > 0.0f ? str("first damage after %u frames at %.0f, %.0f health in the 25 frames after", first, off, lost)
										  : str("the enemy %s lost no health in 200 frames after the click, then %.0f apart (sent: %s)", wallsOnly ? "structure" : "horde", apart,
												g.lastMessages().c_str());
				}
				for (ObjectID id : { uid, tid })
				{
					if (Object *o = g.logic().findObjectByID(id))
					{
						g.logic().destroyObject(o);
					}
				}
				g.step(2);
				if (outcome.rfind("first damage", 0) == 0)
				{
					break;
				}
				firstOutcome = firstOutcome.empty() ? outcome : firstOutcome;
			}
			if (outcome.rfind("first damage", 0) == 0)
			{
				res.pass(item, outcome);
			}
			else
			{
				res.fail(item, "first: " + firstOutcome + "; last (at 420): " + outcome);
			}
		}
	}
}
} // namespace

TEST_CASE("factb fights: every Isengard unit, horde and hero attacks an enemy it is ordered to with a click")
{
	if (!haveWorld("factb fights"))
	{
		return;
	}
	Results r;
	fightsOf("FactionIsengard", r);
	r.print("fights FactionIsengard");
	r.check();
}

TEST_CASE("factb fights: every Mordor unit, horde and hero attacks an enemy it is ordered to with a click")
{
	if (!haveWorld("factb fights"))
	{
		return;
	}
	Results r;
	fightsOf("FactionMordor", r);
	r.print("fights FactionMordor");
	r.check({}); // lane FLY-1: the fell beasts fight (S-1994's pins went)
}

namespace
{
// the damage a weapon's hit can do to `victim`: per DamageNugget (its own, or its projectile's warhead's) Damage x the victim's armour coefficient for
// the nugget's DamageType (the INI's numbers: Weapon.ini, Armor.ini); the sum of them as one more candidate (a hit of every nugget at once)
std::vector<float> expectedHits(const WeaponTemplate &w, const Object &victim, const Player *attackerPlayer, std::string &text, const Object *attacker)
{
	std::vector<float> out;
	const ActiveBody *body = dynamic_cast<const ActiveBody *>(victim.getBodyModule());
	const ArmorTemplate *armor = body ? body->currentArmor() : nullptr;
	float sum = 0.0f;
	std::vector<const WeaponTemplate *> weapons{ &w };
	// lane FACT-C: with the attacker known, a nugget its upgrades switch off (RequiredUpgradeNames / ForbiddenUpgradeNames: the spider riders'
	// forged-blade swords, the fire arrows' warhead) is not one of its hits
	auto off = [&](const WeaponNugget &n) {
		bool o = false;
		for (const std::string &u : n.m_requiredUpgradeNames)
		{
			o = o || (attacker && !attacker->hasUpgrade(u));
		}
		for (const std::string &u : n.m_forbiddenUpgradeNames)
		{
			o = o || (attacker && attacker->hasUpgrade(u));
		}
		return o;
	};
	for (const std::shared_ptr<WeaponNugget> &n : w.m_nuggets)
	{
		if (off(*n))
		{
			continue;
		}
		if (const ProjectileNugget *p = dynamic_cast<const ProjectileNugget *>(n.get()))
		{
			if (const WeaponTemplate *warhead = shared().world->weaponStores().weapons().findWeaponTemplate(p->m_warheadTemplateName))
			{
				weapons.push_back(warhead);
			}
		}
	}
	for (const WeaponTemplate *wt : weapons)
	{
		for (const std::shared_ptr<WeaponNugget> &n : wt->m_nuggets)
		{
			if (const DamageNugget *d = dynamic_cast<const DamageNugget *>(n.get()))
			{
				if (off(*d))
				{
					continue;
				}
				if (n->kind() == NUGGET_FIRE_LOGIC)
				{
					continue; // lane FLY-1: a FireLogicNugget feeds fires, it hits no victim (RW 0x8F8014 answers false: Drogoth's WingBlast's INCREASE_FUEL_ON_EXISTING_FIRE)
				}
				const float coef = armor ? armor->getCoefficient(d->m_damageType) : 1.0f;
				// the nugget's DamageScalar entries whose filter takes the victim (Weapon.ini `DamageScalar = 80% ANY +HERO`)
				float scalar = 1.0f;
				for (const DamageScalarEntry &e : d->m_damageScalar)
				{
					if (ObjectFilterMatch::allows(victim.logic(), e.filter, victim, attackerPlayer))
					{
						scalar *= e.scalar;
					}
				}
				const float hit = d->m_damage * coef * scalar;
				out.push_back(hit);
				sum += hit;
				text += str(" %g x %g%s (type %d)", (double)d->m_damage, (double)coef, scalar != 1.0f ? str(" x %g", (double)scalar).c_str() : "", d->m_damageType);
			}
		}
	}
	if (out.size() > 1)
	{
		out.push_back(sum);
	}
	return out;
}

// one attacker (a horde's member alone, a unit, a hero) at level 1 against one enemy soldier (GondorFighter, outside a horde: no horde bonus on either
// side), ordered by a click: the soldier's first health loss must be one weapon hit of the INI's numbers against its armour
// `siege` (lane FACT-C): the siege engines instead, against an enemy structure (a barracks 380 away: outside their MinimumAttackRange), its first health
// loss one of the weapon's hits against the structure's armour
void damageOf(const std::string &faction, Results &res, bool siege = false)
{
	const factb::Inventory &inv = factb::inventory(faction);
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str());
	Player *enemy = g.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	std::set<std::string> attackers;
	for (factb::Kind k : { factb::Kind::Horde, factb::Kind::Unit, factb::Kind::Hero })
	{
		for (const factb::Entry *e : inv.of(k))
		{
			const ThingTemplate *tt = s.world->things().findTemplate(e->name);
			const bool member = e->via.size() == 1 && e->via.begin()->find(":InitialPayload") != std::string::npos;
			const bool armed = ObjectTemplateInfoBuilder::build(*tt->getFinalOverride()).canPossiblyHaveAnyWeapon || factb::nugget(*tt, "HordeContain");
			if (!member && armed && factb::hasKindOf(*tt, "SELECTABLE") && !factb::hasKindOf(*tt, "DOZER") && !factb::hasKindOf(*tt, "MINE")
				&& !factb::hasKindOf(*tt, "STRUCTURE") && factb::hasKindOf(*tt, "SIEGEENGINE") == siege)
			{
				attackers.insert(e->name); // a horde attacks as a horde: its members' hits land together, so a health loss is k hits
			}
		}
	}
	int n = 0;
	for (const std::string &name : attackers)
	{
		const std::string item = "damage " + name;
		trace(item);
		const Coord3D home = g.home();
		const float r = 600.0f + 250.0f * (float)((n / 7) % 2);
		const Coord3D c = g.rig.freeSpot(home.x + r * std::cos(0.9f * (float)n), home.y + r * std::sin(0.9f * (float)n), 320.0f, false, { { siege ? 380.0f : 150.0f, 0.0f } }, nullptr, 160.0f);
		++n;
		Object *a = g.rig.make(name, c.x, c.y);
		// the enemy: a hero (GondorBoromir), whose health outlasts any one hit (a soldier's 200 would clip the first hit of most heroes)
		Object *v = siege ? g.rig.make("GondorBarracks", c.x + 380.0f, c.y, enemy) : g.rig.make("GondorBoromir", c.x + 150.0f, c.y, enemy);
		const ObjectID aid = a->getID(), vid = v->getID();
		g.step(10);
		a = g.logic().findObjectByID(aid);
		v = g.logic().findObjectByID(vid);
		std::string outcome;
		bool ok = false;
		if (!a || !v || !g.select(a))
		{
			outcome = "the attacker could not be selected with a click";
		}
		else
		{
			// the click on the enemy; when it did not become an attack order (the pick met another model first), once more with the camera from the north
			for (float back : { 420.0f, -420.0f })
			{
				g.cameraBack = back;
				g.clickAt({ v->getPosition()->x, v->getPosition()->y, v->getPosition()->z + 15.0f });
				g.step(1);
				if (g.lastMessages().find("MSG_DO_ATTACK_OBJECT") != std::string::npos || g.lastMessages().find("MSG_DO_FORCE_ATTACK_OBJECT") != std::string::npos)
				{
					break;
				}
				if (!g.select(a))
				{
					break;
				}
			}
			g.cameraBack = 420.0f;
			float before = v->getBodyModule()->getHealth(), lost = 0.0f;
			int maxHits = 1;
			std::string expectedText;
			std::vector<float> expected;
			for (int f = 0; f < 400 && lost <= 0.0f; ++f)
			{
				// the weapon of the hit is the one the attacker holds when it lands (a melee swing lands at once, an arrow later: the same weapon)
				a = g.logic().findObjectByID(aid);
				Object *armed = a;
				int hitters = 1;
				if (a && a->getContain() && !a->getContain()->getContainedItemsList()->empty())
				{
					armed = a->getContain()->getContainedItemsList()->front(); // the members carry the weapons
					// lane FLY-1: a contained horde (the Mumakil's Haradrim archers in its HordeTransportContain) hits with each of its members: their arrows can land
					// in one frame (seen once the fell beasts before it in the sweep fight and the game's timing moved)
					hitters = 0;
					for (const Object *x : *a->getContain()->getContainedItemsList())
					{
						ContainModuleInterface *xc = const_cast<Object *>(x)->getContain();
						hitters += xc && xc->getHordeContainInterface() && xc->getContainedItemsList() && !xc->getContainedItemsList()->empty()
							? (int)xc->getContainedItemsList()->size() : 1;
					}
				}
				maxHits = hitters;
				// the weapons of the object and of what it holds (a horde's members, the Mumakil's riders): a hit is one of theirs
				std::vector<Object *> shooters;
				if (a)
				{
					shooters.push_back(a);
					for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
					{
						// a horde's members, a transport's riders (the Mumakil's archers): what the player owns at the site (only the attacker's are there)
						if (o != a && o->getControllingPlayer() == g.rig.local && dist2d(*o->getPosition(), c) < 400.0f)
						{
							shooters.push_back(o);
						}
					}
				}
				(void)armed;
				if (v && !shooters.empty())
				{
					std::vector<float> now;
					std::string nowText;
					std::set<const WeaponTemplate *> seen;
					for (Object *sh : shooters)
					{
						ObjectWeapons *w = sh->getWeapons();
						for (int slot = 0; w && slot < 3; ++slot) // the primary, secondary and tertiary weapon of its current set
						{
							Weapon *cur = w->weaponInSlot(slot);
							if (cur && cur->getTemplate() && seen.insert(cur->getTemplate()).second)
							{
								const std::vector<float> e = expectedHits(*cur->getTemplate(), *v, g.rig.local, nowText, sh);
								now.insert(now.end(), e.begin(), e.end());
							}
						}
					}
					if (!now.empty())
					{
						expected = now;
						expectedText = nowText;
					}
				}
				g.step(1);
				v = g.logic().findObjectByID(vid);
				const float now = v && v->getBodyModule() ? v->getBodyModule()->getHealth() : 0.0f;
				lost = before - now;
			}
			if (lost <= 0.0f)
			{
				a = g.logic().findObjectByID(aid);
				v = g.logic().findObjectByID(vid);
				outcome = "the enemy hero lost no health in 400 frames (sent: " + g.lastMessages() + ")" +
					(a && v ? str("; then %.0f apart, the attacker's weapon %s", dist2d(*a->getPosition(), *v->getPosition()),
								  a->getWeapons() && a->getWeapons()->currentWeapon() && a->getWeapons()->currentWeapon()->getTemplate()
									  ? a->getWeapons()->currentWeapon()->getTemplate()->getName().c_str() : "none")
							: std::string());
			}
			else
			{
				for (float e : expected)
				{
					for (int k = 1; k <= maxHits; ++k) // k members' hits in the same frame
					{
						ok = ok || std::fabs(e * (float)k - lost) < 0.01f;
					}
					ok = ok || (lost >= before - 0.01f && e * (float)maxHits >= before); // a killing hit is clipped to the health left
				}
				if (expected.empty())
				{
					expectedText = " none: the weapon has no DamageNugget";
				}
				outcome = str("the first hit took %.2f; the weapon's hits against its armour:%s", lost, expectedText.c_str());
			}
		}
		if (ok)
		{
			res.pass(item, outcome);
		}
		else
		{
			res.fail(item, outcome);
		}
		for (ObjectID id : { aid, vid })
		{
			if (Object *o = g.logic().findObjectByID(id))
			{
				g.logic().destroyObject(o);
			}
		}
		g.step(2);
	}
}
} // namespace

TEST_CASE("factb damage: the first hit of every Isengard unit, horde and hero on an enemy hero is its weapon's damage against the hero's armour")
{
	if (!haveWorld("factb damage"))
	{
		return;
	}
	Results r;
	damageOf("FactionIsengard", r);
	r.print("damage FactionIsengard");
	// lane PLAY-4 r3: IsengardBeserker hits Boromir since RotWK's melee reach (RW 0x6ED843's footprint boxes) replaced the edge distance: S-1996 closed
	r.check({});
}

TEST_CASE("factb damage: the first hit of every Mordor unit, horde and hero on an enemy hero is its weapon's damage against the hero's armour")
{
	if (!haveWorld("factb damage"))
	{
		return;
	}
	Results r;
	damageOf("FactionMordor", r);
	r.print("damage FactionMordor");
	// lane PLAY-4 r3 / FLY-1: the fell beasts fly and strike (S-1994 closed)
	// lane ATTACKPATH-1: the mumak reaches Boromir on another frame (RotWK's horde approach goal, + 0x5D) and its first frame's loss is 140, not one listed hit
	// (S-4104: the first-hit window takes only copies of one hit)
	// merge queue q23 (with TARGET-1's target choice and FLANK-1's flank test at fire time): the mumak's first frame takes one listed hit again
	r.check({});
}

namespace
{
size_t completedUpgrades(const Player &p)
{
	size_t n = 0;
	for (const UpgradeTemplate *u : shared().world->upgrades().templates())
	{
		n += u && p.hasUpgradeComplete(u) ? 1 : 0;
	}
	return n;
}

float healthNear(GameLogic &logic, const Coord3D &at, float radius, const Player *owner, bool mine)
{
	float h = 0.0f;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		const bool own = o->getControllingPlayer() == owner;
		if (own == mine && o->getBodyModule() && !o->isKindOfName("STRUCTURE") && dist2d(*o->getPosition(), at) < radius)
		{
			h += o->getBodyModule()->getHealth();
		}
	}
	return h;
}

// every power of the faction's spell book: its sciences given (the purchase screen is PLAY-1 / SPELL-3's), the power cast through the HUD's spell bar
// (pressSpell, a click on the ground for a targeted one) over a mixed group (a wounded own horde, an enemy horde, an enemy structure), and an effect seen:
// the attribute modifier reaching objects, objects made (an OCL), enemies losing or friends gaining health near the point, the map's weather or the
// player's upgrades changing. A power none of these shows fails (its module class named)
// `viaStore` (lane FACT-C): the sciences are bought the way a player buys them: the Palantir's store (SpellStore.apt) opened, every _active button
// clicked and the store closed (MSG_PURCHASE_SCIENCE, RW 0x823360) round after round with the points of a full rank ladder, until each power's
// required sciences are the player's; a power whose science the store never sold fails
void spellBookOf(const std::string &faction, Results &res, bool viaStore = false)
{
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str());
	Player *enemy = g.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	g.hud->update(0.1);
	g.step(5);
	InGameSpellBookModel bar = g.hud->spellBar();
	REQUIRE_MESSAGE(!bar.buttons().empty(), "the spell bar shows no power");
	Object *book = g.logic().findObjectByID(bar.book());
	REQUIRE(book);
	// the powers' RequirementsFilterMPSkirmish want the player's keep standing (the citadel a started skirmish unpacks): the side's structure
	// with a CommandPointBonus, made by the base
	{
		const std::string &side = g.rig.local->getPlayerTemplate()->m_side;
		for (const ThingTemplate *t : s.world->things().templates())
		{
			const FieldValue *bonus = t->getFinalOverride()->findField("CommandPointBonus");
			const long long *points = bonus ? std::get_if<long long>(bonus) : nullptr;
			const CommandSet *cs = s.world->commands().findCommandSet(factb::stringFieldOf(*t, "CommandSet"));
			bool keep = false;
			for (int i = 0; cs && i < CommandSet::MAX_BUTTONS; ++i)
			{
				keep = keep || (cs->getCommandButton(i) && cs->getCommandButton(i)->getFinalOverride()->m_command == GUI_COMMAND_REVIVE);
			}
			if (keep && points && *points > 0 && factb::stringFieldOf(*t, "Side") == side && factb::hasKindOf(*t, "STRUCTURE"))
			{
				const Coord3D home = g.home();
				const Coord3D at = g.rig.freeSpot(home.x, home.y - 900.0f, 400.0f);
				g.rig.make(t->getName(), at.x, at.y);
				break;
			}
		}
		g.step(5);
	}
	std::map<const SpecialPowerTemplate *, EffectProbe> purchaseEffects;
	if (viaStore)
	{
		for (const InGameSpellBookModel::Button &btn : bar.buttons())
		{
			if (!btn.power)
			{
				continue;
			}
			EffectProbe before = EffectProbe::take(g.logic(), book, btn.power);
			for (const EffectProbe::Expect &effect : before.expects)
			{
				if (effect.kind == EffectProbe::BOUNTY)
				{
					purchaseEffects.emplace(btn.power, before);
					break;
				}
			}
		}
		// the points a player earns over the ten ranks (RW 0x782764 adds them; the store's prices are the sciences' SciencePurchasePointCost)
		g.rig.local->science().addSciencePurchasePoints(200);
		int bought = 0;
		for (int round = 0; round < 24; ++round)
		{
			std::string error;
			REQUIRE_MESSAGE(g.hud->openSpellStore(&error), error);
			g.step(2);
			AptSpellStore *st = g.hud->spellStore();
			REQUIRE(st);
			int clicked = 0;
			for (int i = 0; i < SpellStoreModel::MAX_BUTTONS; ++i)
			{
				if (std::string(AptSpellStore::stateName(st->buttonState(i))) == "_active" && st->click(i))
				{
					++clicked;
					g.hud->update(0.01);
				}
			}
			g.hud->closeSpellStore();
			g.step(3);
			bought += clicked;
			if (clicked == 0)
			{
				break;
			}
		}
		res.pass("spell store", str("%d sciences bought through the store", bought));
		bar = g.hud->spellBar();
	}
	int n = 0;
	for (const InGameSpellBookModel::Button &btn : bar.buttons())
	{
		if (!btn.power)
		{
			continue;
		}
		const std::string item = "spell " + btn.power->getName();
		trace(item);
		bool owned = true;
		for (ScienceType st : btn.power->getRequiredSciences())
		{
			if (viaStore)
			{
				owned = owned && g.rig.local->science().hasScience(st);
			}
			else
			{
				g.rig.local->science().grantScience(st);
			}
		}
		if (!owned)
		{
			res.fail(item, "the store did not sell its science");
			continue;
		}
		const auto purchased = purchaseEffects.find(btn.power);
		if (purchased != purchaseEffects.end())
		{
			std::string missing;
			const std::string effect = purchased->second.compare(EffectProbe::take(g.logic(), book, btn.power), missing);
			if (effect.empty() || !missing.empty())
			{
				res.fail(item, "purchase did not activate the passive power: " + missing);
			}
			else
			{
				res.pass(item, "activated on purchase: " + effect);
			}
			continue;
		}
		// the scene: an own horde (wounded: a heal shows), an enemy horde and an enemy structure around the point. It is played twice from the same
		// placement: once without the cast (the control: what the two hordes' own fight does in those frames), once with it; the power's effect is
		// what the cast run shows beyond the control
		const Coord3D home = g.home();
		const float r = 600.0f + 250.0f * (float)((n / 7) % 2);
		const Coord3D c = g.rig.freeSpot(home.x + r * std::cos(0.9f * (float)n), home.y + r * std::sin(0.9f * (float)n), 320.0f, false,
										 { { -60.0f, 0.0f }, { 60.0f, 0.0f }, { 0.0f, 200.0f }, { 0.0f, -150.0f } }, nullptr, 160.0f); // the scene's members
		++n;
		struct Outcome
		{
			float enemyLost = 0.0f, mineGained = 0.0f;
			int weather = 0;
			size_t upgrades = 0;
			EffectProbe before, after;
			HitTracker tracked; ///< frame by frame (a summon's egg lives a few frames)
			bool cast = false;
			std::string fail;
		};
		auto play = [&](bool cast) {
			Outcome out;
			std::vector<ObjectID> scene;
			Object *mine = g.rig.make(ownHordeOf(faction), c.x - 60.0f, c.y);
			scene.push_back(mine->getID());
			scene.push_back(g.rig.make("GondorFighterHorde", c.x + 60.0f, c.y, enemy)->getID());
			scene.push_back(g.rig.make("GondorBarracks", c.x, c.y + 200.0f, enemy)->getID());
			scene.push_back(g.rig.make("GondorFarm", c.x, c.y - 150.0f, enemy)->getID()); // lane FACT-C: an economy structure in range (Blight's filter lists the farms, mills, mines; Snowbind takes structures within 200)
			g.step(20);
			for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getContainedBy() == mine && o->getBodyModule())
				{
					if (ActiveBody *body = dynamic_cast<ActiveBody *>(o->getBodyModule()))
					{
						body->internalChangeHealth(-body->getMaxHealth() * 0.5f);
					}
				}
			}
			// the objects of the scene before the cast (what a summon or an OCL makes is not counted as a heal)
			std::set<ObjectID> present;
			for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
			{
				present.insert(o->getID());
			}
			auto healthOf = [&](bool own) {
				float h = 0.0f;
				for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
				{
					if (present.count(o->getID()) && (o->getControllingPlayer() == g.rig.local) == own && o->getBodyModule() && !o->isKindOfName("STRUCTURE")
						&& dist2d(*o->getPosition(), c) < 300.0f)
					{
						h += o->getBodyModule()->getHealth();
					}
				}
				return h;
			};
			const float enemyBefore = healthOf(false);
			// a heal: own objects whose health went UP (a fight only takes it down), summed per object
			std::map<ObjectID, float> ownHealth;
			for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getControllingPlayer() == g.rig.local && o->getBodyModule() && dist2d(*o->getPosition(), c) < 300.0f)
				{
					ownHealth[o->getID()] = o->getBodyModule()->getHealth();
				}
			}
			out.weather = g.logic().weather().weather();
			out.upgrades = completedUpgrades(*g.rig.local);
			out.before = EffectProbe::take(g.logic(), book, btn.power);
			out.tracked.casters.insert(book->getID());
			out.tracked.track(g.logic());
			if (cast)
			{
				g.hud->update(0.1);
				const InGameSpellBookModel::Button *cur = nullptr;
				for (const InGameSpellBookModel::Button &x : g.hud->spellBar().buttons())
				{
					cur = x.index == btn.index ? &x : cur;
				}
				if (!cur || !cur->owned || !cur->usable || !cur->ready)
				{
					out.fail = str("the spell bar's button is not castable (owned %d, usable %d, ready %d)", cur ? (int)cur->owned : -1, cur ? (int)cur->usable : -1,
								   cur ? (int)cur->ready : -1);
				}
				else if (!g.hud->pressSpell(btn.index))
				{
					out.fail = "the spell bar's press did nothing";
				}
				else
				{
					out.cast = true;
					if (g.hud->spellBar().targeting())
					{
						g.clickAt(c);
						// lane SPELL-4: RotWK refuses a spot its location test fails (RW 0x6A4260 -> 0x82DFB7: e.g. Barricade's ForbiddenObjectFilter
						// with the scene's barracks in range); the cursor is red and the click casts nothing (RW 0x81EBE9). The cast then goes to the
						// nearest spot of a ring search the HUD accepts; none: the item fails by name
						for (int ring = 1; ring <= 6 && g.hud->spellBar().targeting(); ++ring)
						{
							for (int k = 0; k < 8 && g.hud->spellBar().targeting(); ++k)
							{
								const float a = 0.785398163f * (float)k, d = 150.0f * (float)ring;
								Coord3D p{ c.x + d * std::cos(a), c.y + d * std::sin(a), 0.0f };
								p.z = g.logic().getGroundHeight(p.x, p.y);
								if (g.hud->spellBar().targetValid(g.logic(), p))
								{
									g.clickAt(p);
								}
							}
						}
						if (g.hud->spellBar().targeting())
						{
							out.fail = "the HUD refused the scene's spot and every spot of the ring search (RotWK's location test, RW 0x82DFB7)";
							g.clickAt(c, HudInput::Button::Right); // the targeting is cancelled
						}
					}
				}
			}
			for (int f = 0; f < 60; ++f)
			{
				g.step(1);
				out.tracked.track(g.logic());
			}
			out.after = EffectProbe::take(g.logic(), book, btn.power);
			out.enemyLost = enemyBefore - healthOf(false);
			for (const auto &kv : ownHealth)
			{
				Object *o = g.logic().findObjectByID(kv.first);
				if (o && o->getBodyModule() && o->getBodyModule()->getHealth() > kv.second)
				{
					out.mineGained += o->getBodyModule()->getHealth() - kv.second;
				}
			}
			// the scene and what the cast made around it go (the base and the book stay)
			for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if ((!present.count(o->getID()) || std::find(scene.begin(), scene.end(), o->getID()) != scene.end()) && o != book && dist2d(*o->getPosition(), c) < 900.0f)
				{
					g.logic().destroyObject(o);
				}
			}
			g.step(3);
			return out;
		};
		const Outcome control = play(false);
		const Outcome run = play(true);
		if (!run.fail.empty())
		{
			res.fail(item, run.fail);
			continue;
		}
		std::string missing;
		const std::string effect = run.before.compare(run.after, missing, &run.tracked);
		std::vector<std::string> seen;
		if (!effect.empty())
		{
			seen.push_back(effect);
		}
		if (run.mineGained > control.mineGained + 50.0f)
		{
			seen.push_back(str("own units healed %.0f more than without it", run.mineGained - control.mineGained));
		}
		if (run.enemyLost > control.enemyLost + 500.0f) // the two hordes' own fight varies by a few hundred between the runs
		{
			seen.push_back(str("enemies lost %.0f health more than without it", run.enemyLost - control.enemyLost));
		}
		if (g.logic().weather().weather() != run.weather)
		{
			seen.push_back("the weather changed");
		}
		if (completedUpgrades(*g.rig.local) != run.upgrades)
		{
			seen.push_back("the player gained upgrades");
		}
		std::string all;
		for (const std::string &x : seen)
		{
			all += (all.empty() ? "" : ", ") + x;
		}
		if (seen.empty())
		{
			res.fail(item, "cast, but no effect seen beyond the control run: " + missing +
							   str(" (enemies lost %.0f with the cast, %.0f without; own units healed %.0f / %.0f)", run.enemyLost, control.enemyLost, run.mineGained, control.mineGained));
		}
		else
		{
			res.pass(item, all);
		}
	}
}
} // namespace

TEST_CASE("factb spell book: every Isengard spell book power cast through the HUD's spell bar shows its effect")
{
	if (!haveWorld("factb spell book"))
	{
		return;
	}
	Results r;
	spellBookOf("FactionIsengard", r);
	r.print("spell book FactionIsengard");
	// lane SUMMON-1: Isengard's Taint shows no effect of its own (its terrain taint is S-921, as Mordor's Taint below); it passed before only because the
	// hordes' own fight in the control and cast runs differed by more than the 500 margin, which the OCLs' added retail random draws changed
	// lane TARGET-1: Isengard's Taint is S-1997's (its stop names it): it passed only while the scene's own fight lost 500 more health in the cast run than
	// in the control; with RotWK's target choice the two runs lose about the same (1008 with the cast, 1140 without, measured on a CI host)
	// lane ATTACKPATH-1: Isengard's Taint is S-1997's (no effect of TaintSpecialPower is asserted, as Mordor's and the Goblins'): it passed before only
	// because its scene's fight happened to cost the enemies more health than the control run
	// lane OFFMAP-1: Isengard's Taint has no effect assertion of its own (S-1997, as the Goblin / Angmar sweep's SpellBookTaint): it passed through the
	// "enemies lost 500 more than the control run" heuristic, which the scene no longer meets once the start units keep the positions they were made at
	// (RotWK's idle state RW 0x745A64 has no ZH frame <= 1 snap)
	// merge queue q24 (CAMPUI-1, OFFMAP-1, SCRIPT-4, MODS-3 and the amoeba reach fix on q23's tree, measured once): the scene meets the 500 heuristic again,
	// so the Taint line passes; it still asserts no effect of TaintSpecialPower of its own (S-1997 keeps the explanation)
	// merge queue q26 (community #73's slot table centred on the horde, as #73's own run found): the scene misses the 500 heuristic again (1536 with the cast,
	// 1732 without), so Taint is a KNOWN line once more
	r.check({ { "spell SpellBookDevastation", "S-921 (SPELL-3)" }, { "spell SpellBookIndustry", "S-1997" },
		{ "spell SpellBookFueltheFires", "S-1997" }, { "spell SpellBookDragonStrike", "S-1997" }, { "spell SpellBookIsengardTaint", "S-1997" } });
}

TEST_CASE("factb spell book: every Mordor spell book power cast through the HUD's spell bar shows its effect")
{
	if (!haveWorld("factb spell book"))
	{
		return;
	}
	Results r;
	spellBookOf("FactionMordor", r);
	r.print("spell book FactionMordor");
	r.check({ { "spell SpellBookIndustry", "S-1997" }, { "spell SpellBookTaint", "S-1997" }, { "spell SpellBookUntamedAllegiance", "S-1997" } });
}

namespace
{
// The fortress improvements bought for real in the map's starting castle (HudGame: the castle unpacked, its citadel the keep). Isengard first builds walls:
// a wall hub by the keep (made: its pad is the found test's) and a span from its Begin Wall Span button and a click (MSG_WALL_HUB_CONSTRUCT_SPAN). Then the
// keep's improvement buttons in order of need: the tower / spire button is not enabled before its precursor; Iron Plating's / Morgul Sorcery's
// "...Trigger" research hands the real upgrade to the keep through CastleUpgrade (RW 0x8BAAC0 -> RW 0x79986B) and, Iron Plating, to the walls in
// WallUpgradeRadius (RW 0x8BA9EC); that enables the Wizard's Tower / Gorgoroth Spire, bought the same way; its power's button is then cast at an enemy
// horde through the HUD and its weapon's hits are asserted (EffectProbe)
void fortressOf(const std::string &faction, Results &res)
{
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str(), true);
	g.step(5); // the castle unpacks
	g.rig.local->depositMoney(5000000);
	Player *enemy = g.enemy;
	REQUIRE(enemy);
	const bool isengard = faction == "FactionIsengard";
	// lane FACT-C: the faction's castle (Goblins and Angmar too): the wall hub and its span button (none for the Goblins), the improvements in order
	// of need, the "...Trigger" research whose CastleUpgrade hands `granted` over, the improvement that needs it, the fortress power's button
	struct Castle
	{
		std::string hub, span;
		std::vector<std::string> order;
		std::string trigger, granted, tower, cast;
	};
	Castle cfg;
	if (isengard)
	{
		cfg = { "IsengardCastleWallHubExpansion", "Command_IsengardCastleWallHub_BeginWallSpan",
				{ "Upgrade_IsengardFortressIronPlatingTrigger", "Upgrade_IsengardFortressWizardsTower" }, "Upgrade_IsengardFortressIronPlatingTrigger",
				"Upgrade_IsengardFortressIronPlating", "Upgrade_IsengardFortressWizardsTower", "Command_IsengardWizardsTowerLightningStrike" };
	}
	else if (faction == "FactionMordor")
	{
		cfg = { "", "", { "Upgrade_MordorFortressMorgulSorceryTrigger", "Upgrade_MordorFortressGorgorothSpire" }, "Upgrade_MordorFortressMorgulSorceryTrigger",
				"Upgrade_MordorFortressMorgulSorcery", "Upgrade_MordorFortressGorgorothSpire", "Command_FireWeaponMordorFortressSpireFireball" };
	}
	else if (faction == "FactionAngmar")
	{
		cfg = { "AngmarWallHubSmall", "Command_AngmarCastleWallHub_BeginWallSpan", { "Upgrade_AngmarFortressIceWallsTrigger", "Upgrade_AngmarFortressSanctum" },
				"Upgrade_AngmarFortressIceWallsTrigger", "Upgrade_AngmarFortressIceWalls", "Upgrade_AngmarFortressSanctum",
				"Command_FireWeaponAngmarFortressSanctumFrostball" };
	}
	else
	{
		REQUIRE(faction == "FactionWild");
		// the Goblins' fortress power is passive: Command_SpecialAbilityBatCloud is ShowButton = No; the Bat Cloud improvement gives the keep its
		// vision modifier (AttributeModifierUpgrade WildFortressBatCloudBonus) and its stealth detection
		cfg = { "", "", { "Upgrade_WildFortressWebCocoonTrigger", "Upgrade_WildFortressDragonNest", "Upgrade_WildFortressBatCloud" },
				"Upgrade_WildFortressWebCocoonTrigger", "Upgrade_WildFortressWebCocoon", "Upgrade_WildFortressDragonNest", "" };
	}
	const bool hasWalls = !cfg.hub.empty();
	Object *keep = nullptr;
	for (Object *o = g.logic().getFirstObject(); o && !keep; o = o->getNextObject())
	{
		const CommandSet *cs = o->getControllingPlayer() == g.rig.local ? s.world->commands().findCommandSet(o->getCommandSetName()) : nullptr;
		for (int i = 0; cs && i < CommandSet::MAX_BUTTONS && !keep; ++i)
		{
			keep = cs->getCommandButton(i) && cs->getCommandButton(i)->getFinalOverride()->m_command == GUI_COMMAND_REVIVE ? o : nullptr;
		}
	}
	REQUIRE(keep);
	const ObjectID keepId = keep->getID();
	const Coord3D kp = *keep->getPosition();
	CAPTURE(keep->getTemplate()->getName());

	// 1. the walls
	std::vector<ObjectID> walls;
	if (hasWalls)
	{
		const std::string item = "walls: " + cfg.hub + "'s " + cfg.span;
		Object *hub = nullptr;
		Coord3D end{};
		for (int k = 0; k < 32 && !hub; ++k)
		{
			const float a = 0.7f * (float)k, r = 450.0f + 100.0f * (float)(k % 4);
			const Coord3D spot = g.rig.freeSpot(kp.x + r * std::cos(a), kp.y + r * std::sin(a), 200.0f);
			Object *h = g.rig.make(cfg.hub, spot.x, spot.y);
			Coord3D e{ spot.x - 280.0f * std::sin(a), spot.y + 280.0f * std::cos(a), 0.0f }; // along the ring around the keep
			e.z = g.logic().getGroundHeight(e.x, e.y);
			WallSpan::Plan plan;
			if (WallHubBehavior *wh = WallHubBehavior::find(*h, 0); wh && dist2d(e, kp) < 1300.0f && WallSpan::plan(g.logic(), *h, *h->getPosition(), e, wh->data()->m_options, plan) && plan.worstCode == 0)
			{
				hub = h;
				end = e;
			}
			else
			{
				g.logic().destroyObject(h);
				g.step(1);
			}
		}
		REQUIRE(hub);
		const ObjectID hubId = hub->getID();
		g.step(3);
		ControlBarButton b;
		if (!g.select(hub) || !g.reveal(hub, [&](const CommandButton &x) { return x.m_name == cfg.span; }, b))
		{
			res.fail(item, "the hub's Begin Wall Span button could not be reached (selection " + g.selectionText() + ")");
		}
		else if (!g.press(b))
		{
			res.fail(item, "the press sent nothing");
		}
		else
		{
			g.pointAt(end); // the placement update starts the line at the hub (RW 0x6A2D26)
			g.clickAt(end);
			g.step(2);
			for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getID() != hubId && o->getProducerID() == hubId && o->getControllingPlayer() == g.rig.local && o->isKindOfName("STRUCTURE"))
				{
					walls.push_back(o->getID());
				}
			}
			bool up = false;
			for (int f = 0; f < 1500 && !up && !walls.empty(); ++f)
			{
				g.step(1);
				up = true;
				for (ObjectID id : walls)
				{
					const Object *w = g.logic().findObjectByID(id);
					up = up && w && w->getConstructionPercent() == -1.0f;
				}
			}
			if (walls.empty())
			{
				res.fail(item, "the click built no wall (sent: " + g.lastMessages() + ")");
			}
			else if (!up)
			{
				res.fail(item, str("%zu wall pieces, not all built after 1500 frames", walls.size()));
			}
			else
			{
				res.pass(item, str("%zu wall pieces built along the span", walls.size()));
			}
		}
	}

	// 2. the improvements, through the keep's buttons
	const std::vector<std::string> order = cfg.order;
	const std::string tower = cfg.tower;
	const std::string granted = cfg.granted;
	auto buttonFor = [&](const std::string &upgrade, ControlBarButton &out) {
		keep = g.logic().findObjectByID(keepId);
		return keep && g.select(keep) && g.reveal(keep, [&](const CommandButton &x) { return x.m_upgradeName == upgrade; }, out);
	};
	{
		ControlBarButton b;
		const std::string item = "research " + tower + " before " + granted;
		if (!buttonFor(tower, b))
		{
			res.fail(item, "the button is not on the keep's bar");
		}
		else if (b.state == ButtonState::Enabled)
		{
			res.fail(item, "enabled before its NeededUpgrade " + granted);
		}
		else
		{
			res.pass(item, str("not enabled (state %d) before %s", (int)b.state, granted.c_str()));
		}
	}
	int bought = 0;
	for (const std::string &name : order)
	{
		const std::string item = "research " + name + " at the starting castle's " + keep->getTemplate()->getName();
		const UpgradeTemplate *u = s.world->upgrades().findUpgrade(name);
		REQUIRE(u);
		ControlBarButton b;
		if (!buttonFor(name, b))
		{
			res.fail(item, "the button is not on the keep's bar");
			continue;
		}
		if (b.state != ButtonState::Enabled)
		{
			res.fail(item, str("not enabled (state %d)", (int)b.state));
			continue;
		}
		const unsigned spentBefore = g.spent();
		if (!g.press(b))
		{
			res.fail(item, "the press sent nothing");
			continue;
		}
		const int expect = (int)std::ceil(u->getBuildTimeSeconds() * 5.0f);
		bool got = false;
		for (int f = 0; f < expect + 100 && !got; ++f)
		{
			g.step(1);
			keep = g.logic().findObjectByID(keepId);
			got = keep && keep->hasUpgrade(u);
		}
		if (!got)
		{
			res.fail(item, str("not complete after %d frames", expect + 100));
			continue;
		}
		g.step(3);
		std::string what = str("paid %u, done in at most %d frames", g.spent() - spentBefore, expect + 100);
		if (name == cfg.trigger)
		{
			// CastleUpgrade: the real upgrade on the keep, and on every wall built above
			const UpgradeTemplate *real = s.world->upgrades().findUpgrade(granted);
			REQUIRE(real);
			if (!keep->hasUpgrade(real))
			{
				res.fail(item, granted + " did not reach the keep (CastleUpgrade)");
				continue;
			}
			what += ", " + granted + " on the keep";
			if (hasWalls)
			{
				int with = 0;
				for (ObjectID id : walls)
				{
					const Object *w = g.logic().findObjectByID(id);
					with += w && w->hasUpgrade(real) ? 1 : 0;
				}
				if (walls.empty() || with != (int)walls.size())
				{
					res.fail(item, str("%d of the %zu walls built have %s (WallUpgradeRadius)", with, walls.size(), granted.c_str()));
					continue;
				}
				what += str(" and on all %zu walls", walls.size());
			}
		}
		++bought;
		res.pass(item, what);
	}

	// 3. the tower's / spire's power, through its button, at an enemy horde
	const std::string castName = cfg.cast;
	const std::string item = "cast " + (castName.empty() ? std::string("(passive) Bat Cloud") : castName);
	if (bought != (int)order.size())
	{
		res.fail(item, "not every improvement was bought");
		return;
	}
	if (castName.empty())
	{
		keep = g.logic().findObjectByID(keepId);
		const AttributeModifierPool *pool = keep ? static_cast<const AttributeModifierPool *>(keep->findModule("AttributeModifierPoolUpdate")) : nullptr;
		bool has = false;
		for (const std::string &nm : pool ? pool->listNames() : std::vector<std::string>{})
		{
			has = has || nm == "WildFortressBatCloudBonus";
		}
		if (has)
		{
			res.pass(item, "WildFortressBatCloudBonus in the keep's modifier pool");
		}
		else
		{
			res.fail(item, "WildFortressBatCloudBonus is not in the keep's modifier pool after Upgrade_WildFortressBatCloud");
		}
		return;
	}
	// nothing attacks unless ordered (the castle's own archers neither): the keep's hits after the press are its power's; the horde outlives them
	g.logic().combat().setAutoAcquireEnabled(false);
	const Coord3D at = g.rig.freeSpot(kp.x + 450.0f, kp.y + 250.0f, 120.0f);
	Object *target = g.rig.make("GondorFighterHorde", at.x, at.y, enemy);
	g.step(10);
	toughen(g.logic(), target->getID());
	target = g.logic().findObjectByID(target->getID());
	REQUIRE(target);
	keep = g.logic().findObjectByID(keepId);
	REQUIRE(keep);
	ControlBarButton b;
	// the power's button shows once its module is unpaused (Angmar: the Sanctum's ObjectCreationUpgrade grants Upgrade_AngmarFortressSanctumReady after
	// its Delay of 8.5 s, which an UnpauseSpecialPowerUpgrade answers)
	for (int f = 0; f < 120; f += 10)
	{
		keep = g.logic().findObjectByID(keepId);
		ControlBarButton probe;
		if (keep && g.select(keep) && g.reveal(keep, [&](const CommandButton &x) { return x.m_name == castName; }, probe))
		{
			break;
		}
		g.step(10);
	}
	keep = g.logic().findObjectByID(keepId);
	REQUIRE(keep);
	if (!g.select(keep) || !g.reveal(keep, [&](const CommandButton &x) { return x.m_name == castName; }, b))
	{
		res.fail(item, "the button is not on the keep's bar after " + tower);
		return;
	}
	const CommandButton *cb = b.button;
	const SpecialPowerTemplate *t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(cb->m_specialPowerName) : nullptr;
	REQUIRE(t);
	const std::vector<SpecialPowerModuleInterface *> mods = powerModules(g.logic(), keep, t);
	REQUIRE(!mods.empty());
	std::vector<unsigned> readyBefore;
	for (SpecialPowerModuleInterface *m : mods)
	{
		readyBefore.push_back(m->getReadyFrame());
	}
	HitTracker hits;
	hits.casters.insert(keepId);
	for (int f = 0; f < 30; ++f) // the baseline: the keep lands no hit before the press
	{
		hits.track(g.logic());
		g.step(1);
	}
	hits.track(g.logic());
	keep = g.logic().findObjectByID(keepId);
	REQUIRE(keep);
	EffectProbe before = EffectProbe::take(g.logic(), keep, t);
	before.nominals(g.logic());
	if (b.state != ButtonState::Enabled || !g.press(b))
	{
		res.fail(item, str("the button (state %d) could not be pressed", (int)b.state));
		return;
	}
	const Object *member = target->getContain() && !target->getContain()->getContainedItemsList()->empty() ? target->getContain()->getContainedItemsList()->front() : target;
	g.clickAt(*member->getPosition());
	const unsigned pressFrame = g.frame();
	bool fired = false;
	for (int f = 0; f < 300 && !fired; ++f)
	{
		g.step(1);
		hits.track(g.logic());
		for (size_t k = 0; k < mods.size(); ++k)
		{
			fired = fired || mods[k]->getReadyFrame() != readyBefore[k];
		}
	}
	if (!fired)
	{
		res.fail(item, str("did not fire in 300 frames (sent: %s)", g.lastMessages().c_str()));
		return;
	}
	std::string missing, effect;
	for (int k = 0; k < 30; ++k)
	{
		for (int f = 0; f < 10; ++f)
		{
			g.step(1);
			hits.track(g.logic());
		}
		missing.clear();
		effect = before.compare(EffectProbe::take(g.logic(), g.logic().findObjectByID(keepId), t), missing, &hits);
		if (missing.empty())
		{
			break;
		}
	}
	if (!missing.empty())
	{
		res.fail(item, "fired but " + missing);
		return;
	}
	res.pass(item, str("fired %u frames after the press: %s", g.frame() - pressFrame, effect.c_str()));
}
} // namespace

namespace
{
// lane FACT-C: every health drop any of `victims` takes in one frame over `frames` frames (the toughened victims do not heal in the window); a drop that
// kills is left out (clipped to the health left)
std::vector<float> hitSizes(factb::HudGame &g, const std::vector<ObjectID> &victims, int frames)
{
	std::map<ObjectID, float> last;
	std::vector<float> out;
	for (int f = 0; f <= frames; ++f)
	{
		for (ObjectID id : victims)
		{
			const Object *v = g.logic().findObjectByID(id);
			const float h = v && v->getBodyModule() ? v->getBodyModule()->getHealth() : 0.0f;
			auto it = last.find(id);
			if (v && h > 0.0f && !v->isEffectivelyDead() && it != last.end() && it->second - h > 0.001f) // a killing hit is clipped to the health left: not a hit size
			{
				out.push_back(it->second - h);
			}
			last[id] = h;
		}
		g.step(1);
	}
	return out;
}

// the members of `o` (or `o`) that carry `up` or would take it (a horde's banner carrier, which the upgrade does not reach, left out)
std::vector<ObjectID> carriersOf(GameLogic &logic, Object *o, const UpgradeTemplate *up);

// the ids of an object and of what its contain holds (a horde's members)
std::vector<ObjectID> withMembersIds(Object *o)
{
	std::vector<ObjectID> out;
	if (!o)
	{
		return out;
	}
	if (o->getContain() && o->getContain()->getContainedItemsList() && !o->getContain()->getContainedItemsList()->empty())
	{
		for (Object *m : *o->getContain()->getContainedItemsList())
		{
			out.push_back(m->getID());
		}
	}
	else
	{
		out.push_back(o->getID());
	}
	return out;
}

std::vector<ObjectID> carriersOf(GameLogic &logic, Object *o, const UpgradeTemplate *up)
{
	std::vector<ObjectID> out;
	for (ObjectID id : withMembersIds(o))
	{
		Object *x = logic.findObjectByID(id);
		if (x && (x->hasUpgrade(up) || x->affectedByUpgrade(up)))
		{
			out.push_back(id);
		}
	}
	return out;
}

// Every unit / horde upgrade button whose upgrade changes the weapon (WeaponSetUpgrade / WeaponBonusUpgrade) or the armour (ArmorUpgrade) of the unit
// or its members, proved by real health loss: the same scene twice, a fresh unit without the upgrade and a fresh unit that bought it through its button
// (the player's technology given, it is the structure sweep's). Weapon: the hits the unit lands on a toughened enemy hero (GondorBoromir) it is ordered
// onto; armour: the hits an enemy hero lands on its members (the hero attacks it by itself). In each scene the INI hit is computed from the weapon the
// attacker holds then against the victim's armour then (Weapon.ini x Armor.ini, the nuggets its upgrades switch on); the upgrade must change that INI
// hit, and the health the victims lose with it must show the new hit (k simultaneous hits of a horde allowed)
void upgradeEffectsOf(const std::string &faction, Results &res)
{
	const factb::Inventory &inv = factb::inventory(faction);
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str());
	g.rig.local->depositMoney(5000000);
	Player *enemy = g.enemy;
	REQUIRE(enemy);
	// the structures that sell the player technologies (their presence is the RequiredObjectFilter of the units' upgrades), as the unit sweep puts them
	{
		int k = 0;
		for (const factb::Entry *e : inv.of(factb::Kind::Structure))
		{
			const ThingTemplate *btt = s.world->things().findTemplate(e->name);
			const CommandSet *bcs = s.world->commands().findCommandSet(factb::stringFieldOf(*btt, "CommandSet"));
			bool sells = false;
			for (int i = 0; bcs && i < CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *b = bcs->getCommandButton(i);
				sells = sells || (b && b->getFinalOverride()->m_command == GUI_COMMAND_PLAYER_UPGRADE);
			}
			if (sells)
			{
				const Coord3D home = g.home();
				const Coord3D at = g.rig.freeSpot(home.x - 400.0f + 200.0f * (float)(k % 4), home.y - 400.0f - 200.0f * (float)(k / 4), 150.0f);
				g.rig.make(e->name, at.x, at.y);
				++k;
			}
		}
		g.step(3);
	}
	int n = 0;
	for (factb::Kind k : { factb::Kind::Horde, factb::Kind::Unit })
	{
		for (const factb::Entry *e : inv.of(k))
		{
			const ThingTemplate *tt = s.world->things().findTemplate(e->name);
			const bool member = e->via.size() == 1 && e->via.begin()->find(":InitialPayload") != std::string::npos;
			const CommandSet *cs = s.world->commands().findCommandSet(factb::stringFieldOf(*tt, "CommandSet"));
			if (member || !cs || factb::hasKindOf(*tt, "STRUCTURE"))
			{
				continue;
			}
			// the templates whose modules the upgrade reaches: the unit and its InitialPayload members
			std::vector<const ThingTemplate *> holders{ tt->getFinalOverride() };
			for (const factb::Entry *m : inv.of(factb::Kind::Unit))
			{
				for (const std::string &v : m->via)
				{
					if (v == e->name + ":InitialPayload")
					{
						holders.push_back(s.world->things().findTemplate(m->name)->getFinalOverride());
					}
				}
			}
			for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *cb = cs->getCommandButton(i) ? cs->getCommandButton(i)->getFinalOverride() : nullptr;
				if (!cb || cb->m_command != GUI_COMMAND_OBJECT_UPGRADE || cb->m_upgradeName.empty())
				{
					continue;
				}
				const std::string up = cb->m_upgradeName.substr(0, cb->m_upgradeName.find_first_of(" \t"));
				bool weapon = false, armour = false;
				for (const ThingTemplate *h : holders)
				{
					for (const ThingTemplate::Nugget &nu : h->behaviorModules().nuggets())
					{
						const UpgradeModuleData *u = dynamic_cast<const UpgradeModuleData *>(nu.data.get());
						if (u && std::find(u->m_triggeredBy.begin(), u->m_triggeredBy.end(), up) != u->m_triggeredBy.end())
						{
							weapon = weapon || nu.name == "WeaponSetUpgrade" || nu.name == "WeaponBonusUpgrade";
							armour = armour || nu.name == "ArmorUpgrade";
						}
					}
				}
				if (!weapon && !armour)
				{
					continue;
				}
				const std::string item = "upgrade effect " + e->name + " " + up;
				trace(item);
				const UpgradeTemplate *ut = s.world->upgrades().findUpgrade(up);
				REQUIRE(ut);
				// one scene: the unit (bought: the upgrade through its button first) and the enemy hero; the smallest hit dealt (weapon) or taken (armour)
				struct Scene
				{
					std::vector<float> drops, expected;
					std::string text;
				};
				auto scene = [&](bool bought, std::string &why) -> Scene {
					const Coord3D home = g.home();
					const float r = 600.0f + 250.0f * (float)((n / 7) % 2);
					const float dx = r * std::cos(0.9f * (float)n), dy = r * std::sin(0.9f * (float)n);
					const Coord3D c = g.rig.freeSpot(home.x + (home.x + dx < 200.0f ? std::fabs(dx) : dx), home.y + (home.y + dy < 200.0f ? std::fabs(dy) : dy), 320.0f,
													 false, { { weapon ? 300.0f : 150.0f, 0.0f } }); // the enemy hero
					++n;
					g.logic().combat().setAutoAcquireEnabled(false); // nothing fights before the measured window
					Object *u = g.rig.make(e->name, c.x, c.y);
					const ObjectID uid = u->getID();
					g.step(10);
					if (ExperienceTracker *xp = (u = g.logic().findObjectByID(uid)) ? u->getExperienceTracker() : nullptr)
					{
						xp->gainExpForLevel(10, false); // as the unit sweep: the top level (both scenes alike)
					}
					g.step(5);
					Scene out;
					ObjectID vid = INVALID_ID;
					if (bought)
					{
						// the player technology the button needs, given now (as the unit sweep does: after the unit is made)
						for (const std::string &need : cb->m_neededUpgrade)
						{
							if (const UpgradeTemplate *nt = s.world->upgrades().findUpgrade(need); nt && nt->getUpgradeType() == UPGRADE_TYPE_PLAYER)
							{
								g.rig.local->addCompletedUpgrade(need); // the technology (the armoury's, bought in the structure sweep)
							}
						}
						g.step(2);
						ControlBarButton b;
						u = g.logic().findObjectByID(uid);
						const bool selected = u && g.select(u);
						const bool shown = selected && g.reveal(u, [&](const CommandButton &x) { return x.getFinalOverride() == cb; }, b);
						if (!shown || b.state != ButtonState::Enabled || !g.press(b))
						{
							why = str("the upgrade button could not be pressed (selected %d, on the bar %d, state %d; the unit has it %d, is affected %d, has a queue %d)", (int)selected,
									  (int)shown, shown ? (int)b.state : -1, u ? (int)u->hasUpgrade(ut) : -1, u ? (int)u->affectedByUpgrade(ut) : -1, u ? (int)(u->getProductionUpdate() != nullptr) : -1);
						}
						bool all = false;
						for (int f = 0; why.empty() && f < (int)std::ceil(ut->getBuildTimeSeconds() * 5.0f) + 60 && !all; ++f)
						{
							g.step(1);
							all = true;
							for (ObjectID id : withMembersIds(g.logic().findObjectByID(uid)))
							{
								const Object *x = g.logic().findObjectByID(id);
								all = all && x && (x->hasUpgrade(ut) || !x->affectedByUpgrade(ut));
							}
						}
						if (why.empty() && !all)
						{
							why = "the upgrade did not reach the unit and its members";
						}
					}
					if (why.empty())
					{
						// 300 away for a weapon (a unit with a bow and a sword shoots: the arrow upgrades are its bow's), 150 for the armour
						Object *v = g.rig.make("GondorBoromir", c.x + (weapon ? 300.0f : 150.0f), c.y, enemy);
						vid = v->getID();
						g.step(3);
						toughen(g.logic(), vid);
						toughen(g.logic(), uid);
						u = g.logic().findObjectByID(uid);
						if (weapon)
						{
							// one attacker: a horde keeps one member that carries the upgrade (the others leave), so each frame's drop on the hero is one
							// member's hit (its nuggets), never k members landing together
							if (u && u->getContain() && u->isKindOfName("HORDE"))
							{
								std::vector<Object *> leave;
								bool kept = false;
								for (ObjectID id : withMembersIds(u))
								{
									Object *x = g.logic().findObjectByID(id);
									if (x && !kept && (x->hasUpgrade(ut) || x->affectedByUpgrade(ut)))
									{
										kept = true;
										continue;
									}
									if (x)
									{
										leave.push_back(x);
									}
								}
								for (Object *x : leave)
								{
									g.logic().destroyObject(x);
								}
								g.step(3);
								u = g.logic().findObjectByID(uid);
							}
							v = g.logic().findObjectByID(vid);
							if (!u || !v || !g.select(u))
							{
								why = "the unit could not be selected";
							}
							else
							{
								g.clickAt({ v->getPosition()->x, v->getPosition()->y, v->getPosition()->z + 15.0f });
								out.drops = hitSizes(g, { vid }, 250);
							}
						}
						else
						{
							g.logic().combat().setAutoAcquireEnabled(true); // the hero attacks the unit by itself
							out.drops = hitSizes(g, carriersOf(g.logic(), u, ut), 300);
							Object *v2 = g.logic().findObjectByID(vid);
							u = g.logic().findObjectByID(uid);
							if (out.drops.empty() && v2 && u && v2->getAIUpdateInterface())
							{
								// it did not: ordered onto the unit (a test order: the enemy has no HUD)
								v2->getAIUpdateInterface()->aiAttackObject(u, CMD_FROM_AI);
								out.drops = hitSizes(g, carriersOf(g.logic(), u, ut), 300);
							}
						}
						// the INI hit now: the attacker's current weapons against the victim's current armour
						Object *hero = g.logic().findObjectByID(vid);
						// the member that carries the upgrade (not a banner carrier the upgrade does not reach)
						Object *member = nullptr;
						for (ObjectID id : withMembersIds(g.logic().findObjectByID(uid)))
						{
							Object *x = g.logic().findObjectByID(id);
							member = !member && x && (x->hasUpgrade(ut) || x->affectedByUpgrade(ut)) ? x : member;
						}
						Object *attacker = weapon ? member : hero, *victim = weapon ? hero : member;
						for (int slot = 0; attacker && victim && attacker->getWeapons() && slot < 3; ++slot)
						{
							if (const Weapon *w = attacker->getWeapons()->weaponInSlot(slot); w && w->getTemplate())
							{
								const std::vector<float> e = expectedHits(*w->getTemplate(), *victim, attacker->getControllingPlayer(), out.text, attacker);
								out.expected.insert(out.expected.end(), e.begin(), e.end());
							}
						}
					}
					for (ObjectID id : { uid, vid })
					{
						if (Object *o = id != INVALID_ID ? g.logic().findObjectByID(id) : nullptr)
						{
							g.logic().destroyObject(o);
						}
					}
					g.logic().combat().setAutoAcquireEnabled(true);
					g.step(2);
					return out;
				};
				// a frame's drop that is exactly one INI hit (multiples are not taken: two hits of 10 would read as one of 20, so a frame where several
				// members land together proves nothing; over 250 frames a horde lands single hits too)
				auto shows = [](const std::vector<float> &drops, float e) {
					for (float d : drops)
					{
						if (std::fabs(d - e) < 0.01f)
						{
							return true;
						}
					}
					return false;
				};
				std::string why;
				const Scene before = scene(false, why);
				const Scene after = why.empty() ? scene(true, why) : Scene{};
				// the INI hits the upgrade brought (in the second scene's list, not the first's)
				std::vector<float> fresh;
				for (float e : after.expected)
				{
					bool old = false;
					for (float b0 : before.expected)
					{
						old = old || std::fabs(e - b0) < 0.01f;
					}
					if (!old && e > 0.0f)
					{
						fresh.push_back(e);
					}
				}
				// the observed change, one attacker's hits (a horde's dealt scene keeps one member; a taken scene has the one hero): the first scene shows one
				// of its INI hits as a single drop, the second shows a new INI hit the first never showed
				bool seenFresh = false, seenBefore = false;
				for (float e : fresh)
				{
					seenFresh = seenFresh || (shows(after.drops, e) && !shows(before.drops, e));
				}
				for (float e : before.expected)
				{
					seenBefore = seenBefore || shows(before.drops, e);
				}
				const char *how = weapon ? "dealt" : "taken";
				if (!why.empty())
				{
					res.fail(item, why);
				}
				else if (before.drops.empty() || after.drops.empty())
				{
					res.fail(item, str("no hit %s (%zu without, %zu with)", how, before.drops.size(), after.drops.size()));
				}
				else if (fresh.empty() && !weapon)
				{
					// the armour's INI coefficient is unchanged (an ArmorUpgrade without its own armour set: the protection comes another way); the health
					// lost must still shrink
					const float b0 = *std::min_element(before.drops.begin(), before.drops.end()), a0 = *std::min_element(after.drops.begin(), after.drops.end());
					if (a0 < b0 * 0.999f)
					{
						res.pass(item, str("the smallest hit taken shrank %.2f -> %.2f (the armour template's coefficient is the same:%s)", b0, a0, after.text.c_str()));
					}
					else
					{
						std::string seen = "; without:";
						for (float d : before.drops)
						{
							seen += str(" %.2f", d);
						}
						seen += "; with:";
						for (float d : after.drops)
						{
							seen += str(" %.2f", d);
						}
						res.fail(item, str("the smallest hit taken did not shrink (%.2f -> %.2f) and the INI hit is the same (%s)", b0, a0, after.text.c_str()) + seen);
					}
				}
				else if (fresh.empty())
				{
					res.fail(item, "the upgrade did not change the INI hit (without:" + before.text + "; with:" + after.text + ")");
				}
				else if (!seenBefore || !seenFresh)
				{
					auto hist = [](const std::vector<float> &drops) {
						std::map<long, int> n;
						for (float d : drops)
						{
							++n[std::lround(d * 100.0f)];
						}
						std::string t;
						for (const auto &kv : n)
						{
							t += str(" %.2fx%d", kv.first / 100.0, kv.second);
						}
						return t;
					};
					res.fail(item, str("the health lost does not show the change (%s: an INI hit without %s, a new INI hit with %s; the hits without:%s; with:%s; drops without:%s; with:%s)",
									   how, seenBefore ? "seen" : "not seen", seenFresh ? "seen" : "not seen", before.text.c_str(), after.text.c_str(), hist(before.drops).c_str(),
									   hist(after.drops).c_str()));
				}
				else
				{
					res.pass(item, str("the hit %s changed as the INI says: a new INI hit in the health lost the first scene never showed (with:%s)", how, after.text.c_str()));
				}
			}
		}
	}
}
} // namespace

TEST_CASE("factb fortress: Isengard builds walls, buys Iron Plating and the Wizard's Tower at its starting castle and casts the Lightning Strike")
{
	if (!haveWorld("factb fortress"))
	{
		return;
	}
	Results r;
	fortressOf("FactionIsengard", r);
	r.print("fortress FactionIsengard");
	r.check();
}

TEST_CASE("factb fortress: Mordor buys Morgul Sorcery and the Gorgoroth Spire at its starting castle and casts the Spire's fireball")
{
	if (!haveWorld("factb fortress"))
	{
		return;
	}
	Results r;
	fortressOf("FactionMordor", r);
	r.print("fortress FactionMordor");
	r.check();
}

// ---- lane FACT-C: the Goblins (FactionWild) and Angmar through the same sweeps ----
namespace
{
void factcCase(const char *what, const char *faction, void (*sweep)(const std::string &, Results &), const std::map<std::string, std::string> &known)
{
	Results r;
	sweep(faction, r);
	r.print((std::string(what) + " " + faction).c_str());
	r.check(known);
}
void spellsViaStore(const std::string &f, Results &r)
{
	spellBookOf(f, r, true);
}
void siegeDamageOf(const std::string &f, Results &r)
{
	damageOf(f, r, true);
}
void heroDamageOf(const std::string &f, Results &r)
{
	damageOf(f, r, false);
}
} // namespace

TEST_CASE("factc units: every Goblin and Angmar unit, horde and hero ability and upgrade button")
{
	if (!haveWorld("factc units"))
	{
		return;
	}
	factcCase("units", "FactionWild", unitsOf,
			  { { "* Command_ToggleStance", "S-289" },
				{ "GoblinArcherHorde Command_GoblinArcherPoisonArrows", "S-1997|HordeAttackNugget" },
				{ "MordorWorker Command_Harvest", "S-1993 closed: the sweep asserts no SPECIAL_HARVEST effect; test_lumber1.cpp does|no effect of it is asserted" },
				{ "WildMountainGiant Command_SpecialAbilityWildMountainGiantStomp", "S-2006|WildMountainGiantStomp: no result" },
				{ "WildPorter Command_PorterExtinguishFire", "S-1997|PorterFireExtinguisher: no result" } });
	factcCase("units", "FactionAngmar", unitsOf,
			  { { "* Command_ToggleStance", "S-289" },
				{ "AngmarDarkRangerHorde Command_AngmarLongShotAbility", "S-1997|HordeAttackNugget" }, { "AngmarKarsh Command_WhisperofDeath", "S-2006|AngmarWhisperofDeath: no result" },
				{ "AngmarNecromancerHorde Command_NecromancerBlackIce", "S-2006|no effect of it is asserted" },
				{ "AngmarPorter Command_PorterExtinguishFire", "S-1997|PorterFireExtinguisher: no result" }, { "AngmarRogash Command_RogashLeap", "S-2006|RogashLeap: no result" } });
}

TEST_CASE("factc heroes: every Goblin and Angmar hero is recruited and revived through the started keep's REVIVE buttons")
{
	if (!haveWorld("factc heroes"))
	{
		return;
	}
	factb::StartedScope started; // the lobby's start (an idle opponent)
	factcCase("heroes", "FactionWild", heroesOf, {});
	factcCase("heroes", "FactionAngmar", heroesOf, {});
}

TEST_CASE("factc fights: every Goblin and Angmar unit, horde and hero attacks an enemy it is ordered to with a click")
{
	if (!haveWorld("factc fights"))
	{
		return;
	}
	factcCase("fights", "FactionWild", fightsOf, {});
	// lane PLAY-4 r3: the sites are kept on the map, where the enemy is in sight: the clicks are attack orders (S-565's fogged target, a move, no longer
	// happens here); the Necromancers stop short of the enemy as in the damage sweep (S-2005)
	// Lane TARGET-1: the Thrall Master's guard no longer charges the staged horde before the click: RotWK's search refuses a NO_AUTO_ACQUIRE searcher (the
	// guard's qualifier 0x40, RW 0x949E79 -> RW 0x7006D7), which the earlier stand-in did not; the clicked member stands 180 away, past its
	// VISION_STANDARD_MELEE, so the order on the fogged target is refused (RW 0x82C167, S-565) at 170 and 420
	factcCase("fights", "FactionAngmar", fightsOf, { { "fight AngmarNecromancerHorde", "S-2005|lost no health" }, { "fight AngmarThrallMaster", "S-565|lost no health" } });
}

TEST_CASE("factc damage: the first hit of every Goblin and Angmar unit, horde and hero on an enemy hero, and of every siege engine on a structure")
{
	if (!haveWorld("factc damage"))
	{
		return;
	}
	// merge q8 (PATHPERF-1's placement on FLY-1's tree): the Goblin archers' first frame of damage on the hero was two different hits of their weapon
	// summed (4.35 = 3.75 + 0.6), which the window's "k copies of one hit" does not accept (S-4104, closed: since the formations are centred on their
	// hordes, RW 0x86C4EA, the first frame holds one hit)
	// Lane TARGET-1: the archers' first frame of damage on the hero is one hit again (3.75): RotWK's target search and the horde member pick (RW 0x68D30E ->
	// 0x86FA87) moved the volley's timing
	factcCase("damage", "FactionWild", heroDamageOf, {});
	// lane PLAY-4 r3: RotWK's melee reach (RW 0x6ED843's footprint boxes) brings the Goblin King, the Marauder swords and the Dark Dunedain onto Boromir (S-2005
	// keeps the Necromancers); on the site kept on the map the Thrall Master's target is in sight (S-565 no longer applies)
	// and Karsh's first loss on the site kept on the map is his touch's two nuggets (63), not S-2004's 0.25.
	// Lane PATHPERF-1 (the merge): on the site on clear ground (freeSpot's placement rules) Karsh's first loss is the 0.25 again (S-2004)
	factcCase("damage", "FactionAngmar", heroDamageOf,
			  { { "damage AngmarNecromancerHorde", "S-2005|lost no health" }, { "damage AngmarKarsh", "S-2004|the first hit took 0.25" } });
	factcCase("siege damage", "FactionAngmar", siegeDamageOf, {});
}

TEST_CASE("factc upgrade effects: every Goblin and Angmar weapon and armour upgrade changes the hits dealt or taken")
{
	if (!haveWorld("factc upgrade effects"))
	{
		return;
	}
	factcCase("upgrade effects", "FactionWild", upgradeEffectsOf,
			  { { "upgrade effect GoblinSpiderRiderHorde Upgrade_WildFireArrows", "S-2008|did not change the INI hit" } });
	// lane PLAY-4 r3: the armour scenes show hits since Boromir reaches the members with RotWK's melee reach (RW 0x6ED843's footprint boxes)
	// lane ATTACKPATH-1: the Snow Trolls' Dark Iron Armor cannot show against Boromir: armor.ini gives SnowTrollArmor and SnowTrollHeavyArmor both HERO 100%,
	// so his hit is 300 with and without it; the scene passed before only because its first run's smallest frame drop was two hits (600) landing together
	// merge queue q23: on the merged tree the scene without the armour has a two-hit frame again (600), so the smallest hit "shrinks" and the line passes; the
	// armour still does not change Boromir's HERO hit (S-2008 keeps the explanation)
	// merge queue q24 (OFFMAP-1's exact start positions: without OFFMAP-1 the merged tree passes it): no two-hit frame without the armour again, the line fails
	// as ATTACKPATH-1 found
	factcCase("upgrade effects", "FactionAngmar", upgradeEffectsOf,
			  { { "upgrade effect AngmarSnowTrollHorde Upgrade_AngmarDarkIronArmor", "S-2008|did not shrink" } });
}

TEST_CASE("factc spell book: every Goblin and Angmar spell bought in the store and cast through the spell bar shows its effect")
{
	if (!haveWorld("factc spell book"))
	{
		return;
	}
	factcCase("spell book", "FactionWild", spellsViaStore,
			  { { "spell SpellBookTaint", "S-1997|no effect of TaintSpecialPower" }, { "spell SpellBookUntamedAllegiance", "S-1997|the HUD refused the scene's spot" } });
	factcCase("spell book", "FactionAngmar", spellsViaStore, { { "spell SpellBookUntamedAllegiance", "S-1997|the HUD refused the scene's spot" } });
}

TEST_CASE("factc fortress: the Goblins and Angmar build walls, buy their fortress improvements at the started castle and cast its power")
{
	if (!haveWorld("factc fortress"))
	{
		return;
	}
	factcCase("fortress", "FactionWild", fortressOf, {});
	factcCase("fortress", "FactionAngmar", fortressOf, {});
}
