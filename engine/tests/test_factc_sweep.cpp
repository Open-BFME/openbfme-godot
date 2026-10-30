// OpenBFME retail tests (lane FACT-C): the Goblins (FactionWild) and Angmar content works through the HUD as a player uses it. The inventory walks every
// command set the faction's objects show (FactcSweepUtil.h); the sweeps then drive each item through the control bar: a click selects the object, the button is
// pressed, a GUI command mode gets its target click, and the logic must answer (the power fires and recharges, ...). They SKIP when ROTWK_INSTALL /
// BFME2_INSTALL are unset. GPL-3.0.

#include "FactcSweepUtil.h"
#include "FactbRig.h"
#include "ProductionRateTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/PlayerHeroList.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Damage.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/HudObjects.h"
#include "GameClient/MapCache.h"
#include "GameLogic/Module/HordeDispatchSpecialPower.h"
#include "GameLogic/Module/ObjectCreationUpgrade.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "CreepTestUtil.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/System/ShroudManager.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <typeinfo>

using namespace hudtest;
using namespace factc;

namespace
{
const char *const kFactions[] = { "FactionWild", "FactionAngmar" };

struct AbilityResult
{
	std::string object, button, power;
	std::string outcome; // "ok" or what went wrong
	int level = -1;      // the levels gained before the button was usable
};

// the inventory from the PlayerTemplate and from every object slot 0 holds in a skirmish started as the lobby starts it (FACT-B's withStartedSkirmish:
// NewGame::prepareNewGame, the starting base placed and its castle unpacked: the keep, the expansion pads, the walls, the starting builders)
Inventory startedInventory(SharedWorld &s, const std::string &faction)
{
	std::vector<std::string> seeds;
	factb::withStartedSkirmish(faction, [&](LiveGame &game, Player &p) {
		for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == &p && std::find(seeds.begin(), seeds.end(), o->getTemplate()->getName()) == seeds.end())
			{
				seeds.push_back(o->getTemplate()->getName());
			}
		}
	});
	const auto ctx = s.world->enterContext();
	Inventory inv = crawl(*s.world, faction, seeds);
	std::string list;
	for (const std::string &x : seeds)
	{
		list += " " + x;
	}
	MESSAGE(faction << ": the started skirmish's base holds" << list);
	return inv;
}

void grant(HudRig &h, Object *o, const std::string &upgrade)
{
	if (const UpgradeTemplate *ut = Player::resolveUpgrade(upgrade, false))
	{
		if (ut->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			if (!h.rig.local->hasUpgradeComplete(ut))
			{
				h.rig.local->addUpgrade(ut, Player::UPGRADE_STATUS_COMPLETE);
				h.granted.push_back(ut);
			}
		}
		else
		{
			o->giveUpgrade(ut);
		}
	}
}

// the object shows `set` only after a CommandSetUpgrade switched to it: grant that module's triggers (a building level, a hero's mount)
void grantSetTriggers(HudRig &h, Object *o, const std::string &set)
{
	if (o->getCommandSetName() == set)
	{
		return;
	}
	for (const ThingTemplate::Nugget &n : o->getTemplate()->getFinalOverride()->behaviorModules().nuggets())
	{
		const CommandSetUpgradeModuleData *d = dynamic_cast<const CommandSetUpgradeModuleData *>(n.data.get());
		if (d && d->m_commandSet == set)
		{
			for (const std::string &t : d->m_triggeredBy)
			{
				grant(h, o, t);
			}
		}
	}
}

// one of each structure the faction builds, finished, for the player (upgrades name a structure the player must have: RequiredObjectFilter); the test and demo
// buildings of the retail sets (MordorBase*) are left out
void buildStructures(HudRig &h, const Inventory &inv)
{
	float x = 1200.0f;
	for (const std::string &name : inv.structures)
	{
		if (name.find("MordorBase") != std::string::npos)
		{
			continue;
		}
		const Coord3D c = h.rig.freeSpot(x, 2600.0f, 260.0f);
		h.makeBuilt(name, c.x, c.y);
		x += 300.0f;
	}
	h.frames(10);
}

// the ability buttons whose gap is registered (object + button -> stop)
const std::map<std::string, std::string> &knownAbilityGaps()
{
	static const std::map<std::string, std::string> gaps = {
		{ "GoblinCaveTroll Command_SpecialAbilityTrollGrabTree", "S-140: GrabPassengerSpecialPower is an unported module (the trolls' tree / goblin grab)|no module of the power" },
		// (lane FLY-1: Drogoth flies, his WingBlast and Incinerate reach their point: the S-084 pins went)
	};
	return gaps;
}

// the upgrade buttons whose gap is registered
const std::map<std::string, std::string> &knownUpgradeGaps()
{
	// the spike moat works now (ObjectCreationUpgrade, S-2007); the buy-new-catapult / giant buttons show only after the free one dies, which the unported
	// SlaveWatcherBehavior reports (S-1995): "S-xxxx|text": the outcome must say the text
	static const std::map<std::string, std::string> gaps = {
		{ "AngmarCatapultExpansion Command_AngmarPurchaseNewWallCatapult", "S-1995 (SlaveWatcherBehavior: the free catapult is made, its death does not show the buy button)|button not shown" },
		{ "WildGiantSentryExpansion Command_GoblinPurchaseNewWallGiantSentry", "S-1995 (SlaveWatcherBehavior: the free giant is made, its death does not show the buy button)|button not shown" },
	};
	return gaps;
}

// buttons retail buys by itself before the player can: the Hall of Twilight's DoCommandUpgrade modules press its technologies as it levels
// (retail angmarhalloftwilight.ini, RW 0x8B8E2E -> RW 0x696FD2's upgrade case): the bar shows them purchased (lane MODS-3)
const std::set<std::string> &retailAutoPurchased()
{
	static const std::set<std::string> s = { "AngmarHallofTwilight Command_PurchaseTechnologyAngmarSoulFreeze",
		"AngmarHallofTwilight Command_PurchaseTechnologyAngmarWellOfSouls", "AngmarHallofTwilight Command_PurchaseTechnologyAngmarCorpseRain" };
	return s;
}

bool isStructure(const ThingTemplate &tt)
{
	const ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(*tt.getFinalOverride());
	return MaskTest(info.kindOf, (unsigned)ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE"));
}

// by id: an object the test made may have died (and been freed) since
void destroyAll(GameLogic &logic, const std::vector<ObjectID> &ids)
{
	for (ObjectID id : ids)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			for (Object *x : withMembers(o))
			{
				logic.destroyObject(x);
			}
		}
	}
}

// presses the ability button `offer` of a fresh object of offer.object and watches its power module(s)
AbilityResult runAbility(HudRig &h, const Offer &offer, const Coord3D &spot)
{
	AbilityResult r{ offer.object, offer.button, offer.cb->m_specialPowerName, "" };
	GameLogic &logic = h.logic();
	h.reset();
	Object *o = h.rig.make(offer.object, spot.x, spot.y);
	std::vector<ObjectID> made{ o->getID() };
	h.frames(5); // a horde makes its members
	// the levels and the upgrades the button needs
	for (const std::string &u : offer.cb->m_neededUpgrade)
	{
		grant(h, o, u);
	}
	grantSetTriggers(h, o, offer.set);
	h.frames(3);
	std::vector<SpecialPowerModuleInterface *> mods = powerModules(o, r.power);
	if (mods.empty())
	{
		r.outcome = "no module of the power on the object or its members";
		destroyAll(logic, made);
		return r;
	}
	if (!h.select(o))
	{
		r.outcome = "a click does not select it";
		destroyAll(logic, made);
		return r;
	}
	// an ability that needs its user MOUNTED (RequiredConditions bit 0, RW 0x851551: the Goblin King's stinger on the scorpion): the player mounts first with the
	// object's ToggleMounted button
	bool needsMount = false;
	for (Object *x : withMembers(o))
	{
		for (const auto &m : x->modules())
		{
			const SpecialAbilityUpdate *u = dynamic_cast<const SpecialAbilityUpdate *>(m.get());
			needsMount = needsMount || (u && u->abilityData()->m_specialPowerTemplateName == r.power && (u->abilityData()->m_requiredConditions & 1u));
		}
	}
	if (needsMount)
	{
		for (Object *x : withMembers(o))
		{
			if (x->getExperienceTracker())
			{
				x->getExperienceTracker()->gainExpForLevel(10, false);
			}
		}
		h.frames(3);
		ControlBarButton mount;
		if (!h.findButtonIf([](const ControlBarButton &x) { return x.button->m_specialPowerName == "SpecialAbilityToggleMounted"; }, mount) ||
		    !h.hud->controlBar().pressButton(mount.slot, mount.inPalantir))
		{
			r.outcome = "needs MOUNTED: no usable ToggleMounted button";
			destroyAll(logic, made);
			return r;
		}
		h.frames(40);
		h.hud->input().ui().deselectAll();
		h.frames(1);
		if (!h.select(o))
		{
			r.outcome = "mounted: a click does not select it";
			destroyAll(logic, made);
			return r;
		}
	}
	// lane SUMMON-1: a button the unit's current model condition disables (DisableOnModelCondition: the Mountain Giant's Stomp in WEAPONSET_TOGGLE_1; the giant
	// starts throwing rocks, scripts.lua's OnMountainGiantCreated grants Upgrade_SwitchToRockThrowing and DoCommandUpgrade toggles the set) is used the way a
	// player does: the unit's TOGGLE_WEAPONSET button first
	bool disabledNow = false;
	for (size_t w = 0; w < offer.cb->m_disableOnModelCondition.size(); ++w)
	{
		disabledNow = disabledNow || (o->getModelConditionBits()[w] & offer.cb->m_disableOnModelCondition[w]) != 0;
	}
	if (disabledNow)
	{
		ControlBarButton toggle;
		if (!h.findButtonIf([](const ControlBarButton &x) { return x.button->m_command == GUI_COMMAND_TOGGLE_WEAPONSET; }, toggle) ||
		    !h.hud->controlBar().pressButton(toggle.slot, toggle.inPalantir))
		{
			r.outcome = "disabled by its model condition: no usable TOGGLE_WEAPONSET button";
			destroyAll(logic, made);
			return r;
		}
		h.frames(5);
	}
	// the level that unlocks it: one level at a time (a hero's later level may swap the button for another, as Hwaldar's summons)
	ControlBarButton b;
	bool shown = false;
	for (int level = 0; level <= 10; ++level)
	{
		if (level > 0)
		{
			for (Object *x : withMembers(o))
			{
				if (x->getExperienceTracker())
				{
					x->getExperienceTracker()->gainExpForLevel(1, false);
				}
			}
			h.frames(3);
		}
		shown = h.findButton(offer.button, b);
		if (shown && (b.state == ButtonState::Enabled || b.state == ButtonState::Active))
		{
			r.level = level;
			break;
		}
	}
	if (!shown)
	{
		r.outcome = "button not shown (command set " + h.hud->controlBar().commandSetName() + ")";
		destroyAll(logic, made);
		return r;
	}
	if (b.state != ButtonState::Enabled && b.state != ButtonState::Active)
	{
		r.outcome = std::string("button ") + stateName(b.state);
		destroyAll(logic, made);
		return r;
	}
	for (const ControlBarButton &x : h.hud->controlBar().offBarButtons())
	{
		if (x.slot == b.slot && x.button == b.button)
		{
			r.outcome = "ok (off the bar: neither InPalantir in windows 0 .. 5 nor Radial, retail reaches it by the context click; the capture click: factc neutral)";
			destroyAll(logic, made);
			return r;
		}
	}
	std::vector<unsigned> ready0;
	for (SpecialPowerModuleInterface *m : mods)
	{
		ready0.push_back(m->getReadyFrame());
	}
	const unsigned now0 = logic.getFrame();
	// a target for a GUI command mode
	// in front of the caster as the camera (south of it, looking north) sees them: a click on them hits them, not the caster
	Object *enemy = h.rig.make("MordorMountainTroll", spot.x + 260.0f, spot.y - 200.0f, h.rig.game->players().findPlayerWithName("Player_2")); // sturdy: alive until the ability lands
	made.push_back(enemy->getID());
	if (enemy->getBodyModule())
	{
		enemy->getBodyModule()->setIndestructible(true); // the target lives until the ability lands (a hero's own blows would kill it first)
	}
	Object *ally = nullptr; // only for a button that wants an ally target (a second copy of a hero would cast its own auto abilities)
	if (offer.cb->m_options & COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT)
	{
		ally = h.rig.make(offer.object, spot.x - 160.0f, spot.y - 80.0f);
		made.push_back(ally->getID());
	}
	h.frames(2);
	const size_t log0 = h.hud->input().messageLog().size();
	if (!h.hud->controlBar().pressButton(b.slot, b.inPalantir))
	{
		r.outcome = "press refused";
		destroyAll(logic, made);
		return r;
	}
	h.frames(1);
	if (const CommandButton *mode = h.hud->input().ui().getGUICommand())
	{
		const std::uint32_t opt = mode->m_options;
		Coord3D at{ spot.x + 100.0f, spot.y - 140.0f, logic.getGroundHeight(spot.x + 100.0f, spot.y - 140.0f) };
		if (opt & COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT)
		{
			at = *enemy->getPosition();
		}
		else if ((opt & COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT) && ally)
		{
			at = *withMembers(ally).back()->getPosition();
		}
		else if ((opt & COMMAND_OPTION_NEED_TARGET_POS) && TheSpecialPowerStore)
		{
			// lane SUMMONLOC-1: the hero power click issues nothing where the location test refuses (RW 0x81E9C5 -> RW 0x6A4260 -> RW 0x82DFB7), as
			// retail's red cursor: a player clicks where it is valid. The spot 108 from the caster serves a LIMIT_DISTANCE 150 power (Hwaldar's
			// Hillmen); a NEEDS_OBJECT_FILTER power (Azog's Crack the Whip: allied infantry within its radius) gets a soldier of the caster's there
			const SpecialPowerTemplate *pw = TheSpecialPowerStore->findSpecialPowerTemplate(mode->m_specialPowerName);
			const Coord3D near{ spot.x + 60.0f, spot.y - 90.0f, logic.getGroundHeight(spot.x + 60.0f, spot.y - 90.0f) };
			if (pw && !ActionManager::canDoSpecialPowerAtLocation(*o, at, pw, opt))
			{
				if ((pw->getFinalOverride()->getFlags() & SPF_NEEDS_OBJECT_FILTER) && !ActionManager::canDoSpecialPowerAtLocation(*o, near, pw, opt))
				{
					Object *soldier = h.rig.make("GondorFighter", near.x, near.y);
					made.push_back(soldier->getID());
					h.frames(2);
				}
				at = near;
			}
		}
		at.z += 8.0f;
		h.rig.lookAt(*o->getPosition());
		h.frames(1);
		h.clickAt(at);
		h.frames(1);
	}
	std::string sent;
	for (size_t i = log0; i < h.hud->input().messageLog().size(); ++i)
	{
		const std::string &l = h.hud->input().messageLog()[i];
		if (l.find("SPECIAL_POWER") != std::string::npos)
		{
			sent = l.substr(0, l.find(' '));
		}
	}
	const bool modeLeft = h.hud->input().ui().getGUICommand() != nullptr;
	h.hud->input().ui().setGUICommand(nullptr); // a player's Escape
	bool fired = false;
	for (int i = 0; i < 300 && !fired; ++i)
	{
		h.frames(1);
		for (size_t k = 0; k < mods.size(); ++k)
		{
			fired = fired || mods[k]->getReadyFrame() > std::max(ready0[k], now0);
		}
	}
	std::ostringstream why;
	for (Object *x : withMembers(o))
	{
		for (const auto &m : x->modules())
		{
			const SpecialAbilityUpdate *u = dynamic_cast<const SpecialAbilityUpdate *>(m.get());
			if (u && u->abilityData()->m_specialPowerTemplateName == r.power)
			{
				why << "; the update: active " << u->isActive() << ", packing " << u->packingState() << ", triggers " << u->triggerCount();
			}
		}
	}
	const Object *liveEnemy = logic.findObjectByID(made[1]);
	why << "; the enemy " << (liveEnemy && !liveEnemy->isEffectivelyDead() ? "alive" : "gone");
	r.outcome = fired ? "ok" : "pressed, the power never fired (no recharge started in 300 frames; message " + (sent.empty() ? std::string("none") : sent) + why.str() + ")";
	if (modeLeft)
	{
		r.outcome += " [the GUI command mode stayed after the target click]";
	}
	destroyAll(logic, made);
	h.frames(2);
	return r;
}
} // namespace

namespace
{
std::string fieldText(const ThingTemplate &tt, const char *name)
{
	const FieldValue *v = tt.getFinalOverride()->findField(name);
	if (!v)
	{
		return "-";
	}
	std::ostringstream o;
	if (const long long *i = std::get_if<long long>(v))
	{
		o << *i;
	}
	else if (const float *f = std::get_if<float>(v))
	{
		o << *f;
	}
	else if (const std::string *t = std::get_if<std::string>(v))
	{
		o << *t;
	}
	else
	{
		o << "?";
	}
	return o.str();
}

// the module classes of the template (behaviors), the ones a check reads
std::string moduleClasses(const ThingTemplate &tt, const std::string &onlyPower = std::string())
{
	std::set<std::string> names;
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (onlyPower.empty())
		{
			names.insert(n.name);
			continue;
		}
		if (const SpecialPowerModuleData *d = dynamic_cast<const SpecialPowerModuleData *>(n.data.get()))
		{
			if (d->m_specialPowerTemplate && d->m_specialPowerTemplate->getName() == onlyPower)
			{
				names.insert(n.name);
			}
		}
		else if (const SpecialAbilityUpdateModuleData *a = dynamic_cast<const SpecialAbilityUpdateModuleData *>(n.data.get()))
		{
			if (a->m_specialPowerTemplateName == onlyPower)
			{
				names.insert(n.name);
			}
		}
	}
	std::string out;
	for (const std::string &n : names)
	{
		out += (out.empty() ? "" : ", ") + n;
	}
	return out;
}

// the inventory as a markdown section (OPENBFME_FACTC_INVENTORY=<file> writes it: docs/FACTC.md is made from it)
std::string inventoryMarkdown(RetailObjectWorld &w, const Inventory &inv)
{
	std::ostringstream md;
	md << "## " << inv.faction << "\n\n";
	md << "### Structures (" << inv.structures.size() << ")\n\n| Structure | BuildCost | BuildTime (s) | Command sets | Buttons |\n|---|---|---|---|---|\n";
	auto buttonsOf = [&](const std::string &obj) {
		std::set<std::string> b;
		for (const Offer &o : inv.offers)
		{
			if (o.object == obj && o.cb->m_command != GUI_COMMAND_SELL && o.cb->m_command != GUI_COMMAND_STOP && o.cb->m_command != GUI_COMMAND_ATTACK_MOVE &&
			    o.cb->m_command != GUI_COMMAND_SET_STANCE && o.cb->m_command != GUI_COMMAND_TOGGLE_STANCE && o.cb->m_command != GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE &&
			    o.cb->m_command != GUI_COMMAND_POP_VISIBLE_COMMAND_RANGE)
			{
				b.insert(std::string(GUICommandName(o.cb->m_command)) + " " + (o.cb->m_command == GUI_COMMAND_SPECIAL_POWER ? o.cb->m_specialPowerName
				                                                              : !o.cb->m_upgradeName.empty() ? o.cb->m_upgradeName
				                                                              : o.cb->getThingTemplate() ? o.cb->getThingTemplate()->getName() : o.cb->m_name));
			}
		}
		std::string out;
		for (const std::string &x : b)
		{
			out += (out.empty() ? "" : "; ") + x;
		}
		return out;
	};
	auto setsText = [&](const std::string &obj) {
		std::string out;
		auto it = inv.setsOf.find(obj);
		if (it != inv.setsOf.end())
		{
			for (const std::string &x : it->second)
			{
				out += (out.empty() ? "" : ", ") + x;
			}
		}
		return out;
	};
	std::set<std::string> structures = inv.structures;
	for (const auto &kv : inv.setsOf)
	{
		const ThingTemplate *tt = w.things().findTemplate(kv.first);
		if (tt && !inv.units.count(kv.first) && !inv.heroes.count(kv.first) && MaskTest(ObjectTemplateInfoBuilder::build(*tt->getFinalOverride()).kindOf, (unsigned)ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE")))
		{
			structures.insert(kv.first);
		}
	}
	for (const std::string &n : structures)
	{
		const ThingTemplate *tt = w.things().findTemplate(n);
		md << "| " << n << " | " << (tt ? fieldText(*tt, "BuildCost") : "?") << " | " << (tt ? fieldText(*tt, "BuildTime") : "?") << " | " << setsText(n) << " | " << buttonsOf(n) << " |\n";
	}
	auto units = [&](const char *title, const std::set<std::string> &list) {
		md << "\n### " << title << " (" << list.size() << ")\n\n| Unit | BuildCost | BuildTime (s) | CommandPoints | Abilities and upgrades (buttons) |\n|---|---|---|---|---|\n";
		for (const std::string &n : list)
		{
			const ThingTemplate *tt = w.things().findTemplate(n);
			md << "| " << n << " | " << (tt ? fieldText(*tt, "BuildCost") : "?") << " | " << (tt ? fieldText(*tt, "BuildTime") : "?") << " | " << (tt ? fieldText(*tt, "CommandPoints") : "?")
			   << " | " << buttonsOf(n) << " |\n";
		}
	};
	units("Units and hordes", inv.units);
	units("Heroes", inv.heroes);
	md << "\n### Upgrades (" << inv.objectUpgrades.size() + inv.playerUpgrades.size() << ")\n\n| Upgrade | Type | BuildCost | BuildTime (s) |\n|---|---|---|---|\n";
	for (const std::set<std::string> *list : { &inv.playerUpgrades, &inv.objectUpgrades })
	{
		for (const std::string &n : *list)
		{
			const UpgradeTemplate *u = w.upgrades().findUpgrade(n);
			md << "| " << n << " | " << (u ? (u->getUpgradeType() == UPGRADE_TYPE_PLAYER ? "PLAYER" : "OBJECT") : "?") << " | " << (u ? u->getBuildCost() : -1) << " | "
			   << (u ? u->getBuildTimeSeconds() : -1.0f) << " |\n";
		}
	}
	md << "\n### Special powers (" << inv.powers.size() << ")\n\n| Power | ReloadTime (frames) | Objects (modules) |\n|---|---|---|\n";
	for (const std::string &p : inv.powers)
	{
		const SpecialPowerTemplate *t = w.spellStores().specialPowers().findSpecialPowerTemplate(p);
		std::string users;
		std::set<std::string> seen;
		for (const Offer &o : inv.offers)
		{
			if (o.cb->m_specialPowerName == p && seen.insert(o.object).second)
			{
				const ThingTemplate *tt = w.things().findTemplate(o.object);
				users += (users.empty() ? "" : "; ") + o.object + " (" + (tt ? moduleClasses(*tt, p) : std::string("?")) + ")";
			}
		}
		md << "| " << p << " | " << (t ? (long long)t->getReloadTime() : -1) << " | " << users << " |\n";
	}
	md << "\n### Spell book sciences (" << inv.sciences.size() << ")\n\n";
	for (const std::string &sc : inv.sciences)
	{
		md << "- " << sc << "\n";
	}
	md << "\n";
	return md.str();
}
} // namespace

TEST_CASE("factc inventory: the Goblins' and Angmar's command sets resolve (every object, upgrade and special power a button names exists)")
{
	if (!haveWorld("factc inventory"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (const std::string faction : kFactions)
	{
		const Inventory inv = startedInventory(s, faction);
		if (const char *path = std::getenv("OPENBFME_FACTC_INVENTORY"))
		{
			const auto ctx = s.world->enterContext();
			std::ofstream(path, faction == kFactions[0] ? std::ios::trunc : std::ios::app) << inventoryMarkdown(*s.world, inv);
		}
		MESSAGE(faction << " inventory: " << inv.setsOf.size() << " objects, " << inv.offers.size() << " buttons\n" << describe(inv));
		for (const std::string &p : inv.problems)
		{
			CHECK_MESSAGE(false, faction << ": " << p);
		}
		CHECK(inv.structures.size() >= 8);
		CHECK(inv.units.size() >= 6);
		CHECK(inv.heroes.size() >= 4);
	}
}

TEST_CASE("factc abilities: every SPECIAL_POWER button of the Goblins' and Angmar's units and heroes fires its power through the HUD")
{
	if (!haveWorld("factc abilities"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (const std::string faction : kFactions)
	{
		const Inventory inv = startedInventory(s, faction);
		HudRig h(s, faction.c_str());
		const auto ctx = h.hud->enterContext();
		h.rig.local->depositMoney(100000);
		const Coord3D spot = h.rig.freeSpot(2800, 1400, 400.0f);
		std::vector<AbilityResult> results;
		std::set<std::pair<std::string, std::string>> done;
		for (const Offer &of : inv.offers)
		{
			// NONPRESSABLE (option bit 28, RW 0xDA4C88): a passive ability's display button (leadership), never pressed
			if (of.cb->m_command != GUI_COMMAND_SPECIAL_POWER || (of.cb->m_options & 0x10000000u) || !done.insert({ of.object, of.button }).second)
			{
				continue;
			}
			const ThingTemplate *tt = s.world->things().findTemplate(of.object);
			if (!tt || isStructure(*tt))
			{
				continue;
			}
			std::fprintf(stderr, "factc ability start %s %s\n", of.object.c_str(), of.button.c_str());
			results.push_back(runAbility(h, of, spot));
		}
		int ok = 0;
		for (const AbilityResult &r : results)
		{
			ok += r.outcome.compare(0, 2, "ok") == 0 ? 1 : 0;
			std::printf("factc ability %s %s %s %s (level +%d): %s\n", faction.c_str(), r.object.c_str(), r.button.c_str(), r.power.c_str(), r.level, r.outcome.c_str());
		}
		MESSAGE(faction << ": " << ok << " of " << results.size() << " ability buttons fire");
		for (const AbilityResult &r : results)
		{
			const std::string key = r.object + " " + r.button;
			const auto known = knownAbilityGaps().find(key);
			if (known != knownAbilityGaps().end())
			{
				// a registered gap: it must still fail (a fix updates the list), for its own reason (the text after '|'), and it is reported with its stop
				CHECK_MESSAGE(r.outcome.compare(0, 2, "ok") != 0, key << " works now: take it off knownAbilityGaps (" << known->second << ")");
				CHECK_MESSAGE((known->second.find('|') == std::string::npos || r.outcome.find(known->second.substr(known->second.find('|') + 1)) != std::string::npos),
							  key << " fails for another reason than its pin: " << r.outcome);
				MESSAGE(faction << " " << key << ": " << known->second);
				continue;
			}
			CHECK_MESSAGE(r.outcome.compare(0, 2, "ok") == 0, faction << " " << r.object << " " << r.button << " (" << r.power << "): " << r.outcome);
		}
	}
}

namespace
{
struct UpgradeResult
{
	std::string object, button, upgrade;
	std::string outcome;
	int modulesRun = 0;
};

bool triggers(const UpgradeModuleData &d, const std::string &upgrade)
{
	return std::find(d.m_triggeredBy.begin(), d.m_triggeredBy.end(), upgrade) != d.m_triggeredBy.end() && (!d.m_requiresAllTriggers || d.m_triggeredBy.size() == 1) &&
	       d.m_conflictsWith.empty();
}

// presses the upgrade button `offer` on a fresh offer.object: the price leaves the bank, after the build time the object (OBJECT) or the player (PLAYER) has the
// upgrade, and every upgrade module of the object (or its members) that the upgrade alone triggers has run
UpgradeResult runUpgrade(HudRig &h, const Offer &offer, const Coord3D &spot)
{
	UpgradeResult r{ offer.object, offer.button, offer.cb->m_upgradeName, "" };
	GameLogic &logic = h.logic();
	h.reset();
	const UpgradeTemplate *ut = Player::resolveUpgrade(r.upgrade, false);
	if (!ut)
	{
		r.outcome = "unknown upgrade";
		return r;
	}
	Object *o = h.makeBuilt(offer.object, spot.x, spot.y);
	std::vector<ObjectID> made{ o->getID() };
	h.frames(5);
	for (const std::string &u : offer.cb->m_neededUpgrade)
	{
		grant(h, o, u);
	}
	grantSetTriggers(h, o, offer.set);
	h.frames(2);
	if (!h.select(o))
	{
		r.outcome = "a click does not select it";
		destroyAll(logic, made);
		return r;
	}
	ControlBarButton b;
	if (!h.findButton(offer.button, b))
	{
		r.outcome = "button not shown (command set " + h.hud->controlBar().commandSetName() + ")";
		destroyAll(logic, made);
		return r;
	}
	if (b.state != ButtonState::Enabled)
	{
		r.outcome = std::string("button ") + stateName(b.state);
		destroyAll(logic, made);
		return r;
	}
	const HudRig::Purse money0 = h.purse();
	const int expectedCost = ut->calcCostToBuild(h.rig.local, o); // RW 0x66F2C8 (a discount template, the sub upgrades)
	if (!h.hud->controlBar().pressButton(b.slot, b.inPalantir))
	{
		r.outcome = "press refused";
		destroyAll(logic, made);
		return r;
	}
	h.frames(2);
	const int paid = h.paidSince(money0);
	auto has = [&]() {
		if (ut->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			return h.rig.local->hasUpgradeComplete(ut);
		}
		bool any = false;
		for (Object *x : withMembers(o))
		{
			any = any || x->hasUpgrade(ut);
		}
		return any;
	};
	const int limit = (int)(ut->getBuildTimeSeconds() * 5.0f) + 30;
	int frames = 0;
	for (; frames < limit && !has(); ++frames)
	{
		h.frames(1);
	}
	std::ostringstream out;
	if (!has())
	{
		out << "never completed in " << limit << " frames (paid " << paid << ", the producer's queue " << (o->getProductionUpdate() ? (int)o->getProductionUpdate()->getProductionCount() : -1) << ")";
	}
	else
	{
		h.frames(3);
		if ((int)paid != expectedCost && !(expectedCost == 0 && paid <= 0)) // a free research: only the income moved the bank
		{
			out << "paid " << paid << " (upgrade cost " << ut->getBuildCost() << ", expected " << expectedCost << ") ";
		}
		for (Object *x : withMembers(o))
		{
			for (const auto &m : x->modules())
			{
				UpgradeMux *mux = m->getUpgrade();
				if (mux && triggers(*mux->muxData(), r.upgrade))
				{
					++r.modulesRun;
					if (!mux->isAlreadyUpgraded())
					{
						out << "an upgrade module (" << typeid(*m).name() << ") of " << x->getTemplate()->getName() << " did not run; ";
					}
				}
			}
		}
	}
	r.outcome = out.str().empty() ? "ok" : out.str();
	if (ut->getUpgradeType() == UPGRADE_TYPE_PLAYER && h.rig.local->hasUpgradeComplete(ut))
	{
		h.granted.push_back(ut); // researched here: taken back for the next item
	}
	destroyAll(logic, made);
	h.frames(2);
	return r;
}
} // namespace

TEST_CASE("factc upgrades: every OBJECT / PLAYER upgrade button of the Goblins' and Angmar's structures and units is bought through the HUD and runs its modules")
{
	if (!haveWorld("factc upgrades"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (const std::string faction : kFactions)
	{
		const Inventory inv = startedInventory(s, faction);
		HudRig h(s, faction.c_str());
		const auto ctx = h.hud->enterContext();
		h.rig.local->depositMoney(200000);
		buildStructures(h, inv);
		const Coord3D spot = h.rig.freeSpot(2800, 1400, 400.0f);
		std::vector<UpgradeResult> results;
		std::set<std::pair<std::string, std::string>> done;
		for (const Offer &of : inv.offers)
		{
			if ((of.cb->m_command != GUI_COMMAND_OBJECT_UPGRADE && of.cb->m_command != GUI_COMMAND_PLAYER_UPGRADE) || !done.insert({ of.object, of.button }).second)
			{
				continue;
			}
			if (of.object.find("MordorBase") != std::string::npos)
			{
				continue; // the retail data's test building (Command_ConstructMordorBaseTest on the Mordor worker's set): its test upgrades are no game content
			}
			std::fprintf(stderr, "factc upgrade start %s %s\n", of.object.c_str(), of.button.c_str());
			results.push_back(runUpgrade(h, of, spot));
		}
		int ok = 0;
		for (const UpgradeResult &r : results)
		{
			ok += r.outcome == "ok" ? 1 : 0;
			std::printf("factc upgrade %s %s %s %s (%d modules): %s\n", faction.c_str(), r.object.c_str(), r.button.c_str(), r.upgrade.c_str(), r.modulesRun, r.outcome.c_str());
		}
		MESSAGE(faction << ": " << ok << " of " << results.size() << " upgrade buttons work");
		for (const UpgradeResult &r : results)
		{
			if (retailAutoPurchased().count(r.object + " " + r.button))
			{
				CHECK_MESSAGE(r.outcome == "button purchased", r.object << " " << r.button << ": DoCommandUpgrade should have bought it: " << r.outcome);
				continue;
			}
			const auto known = knownUpgradeGaps().find(r.object + " " + r.button);
			if (known != knownUpgradeGaps().end())
			{
				CHECK_MESSAGE(r.outcome != "ok", r.object << " " << r.button << " works now: take it off knownUpgradeGaps (" << known->second << ")");
				CHECK_MESSAGE((known->second.find('|') == std::string::npos || r.outcome.find(known->second.substr(known->second.find('|') + 1)) != std::string::npos),
							  r.object << " " << r.button << " fails for another reason than its pin: " << r.outcome);
				MESSAGE(faction << " " << r.object << " " << r.button << ": " << known->second);
				continue;
			}
			CHECK_MESSAGE(r.outcome == "ok", faction << " " << r.object << " " << r.button << " (" << r.upgrade << "): " << r.outcome);
		}
	}
}

namespace
{
struct ProductionResult
{
	std::string producer, button, unit;
	std::string outcome;
	int cost = 0, frames = 0, commandPoints = 0;
};

Object *findStructure(GameLogic &logic, Player *owner, const std::string &name)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == owner && o->getTemplate()->getName() == name && !o->isEffectivelyDead())
		{
			return o;
		}
	}
	return nullptr;
}

// presses the UNIT_BUILD button `offer` on the player's offer.object: the button's cost leaves the bank (calcCostToBuild), the unit appears for the player after
// calcTimeToBuild frames (a few frames of exit allowed) and stands near the producer
ProductionResult runProduction(HudRig &h, const Offer &offer)
{
	ProductionResult r{ offer.object, offer.button, offer.cb->getThingTemplate() ? offer.cb->getThingTemplate()->getName() : std::string(), "" };
	GameLogic &logic = h.logic();
	h.reset();
	Object *b = findStructure(logic, h.rig.local, offer.object);
	if (!b)
	{
		r.outcome = "no " + offer.object + " of the player";
		return r;
	}
	grantSetTriggers(h, b, offer.set);
	for (const std::string &u : offer.cb->m_neededUpgrade)
	{
		grant(h, b, u); // the building level the button needs (a player buys it first)
	}
	h.frames(2);
	if (!h.select(b))
	{
		r.outcome = "a click does not select it";
		return r;
	}
	ControlBarButton cb;
	if (!h.findButton(offer.button, cb))
	{
		r.outcome = "button not shown (command set " + h.hud->controlBar().commandSetName() + ")";
		return r;
	}
	const ThingTemplate *tt = offer.cb->getThingTemplate();
	if (cb.state != ButtonState::Enabled)
	{
		std::ostringstream o;
		o << "button " << stateName(cb.state) << " (canMakeUnit " << (int)BuildAssistant::canMakeUnit(*b, tt, -1) << ", playerCanBuild " << BuildAssistant::playerCanBuild(*h.rig.local, tt, logic)
		  << ", command points " << BuildAssistant::commandPointsAvailable(*h.rig.local, *tt, logic) << ": usage " << h.rig.local->commandPoints().getUsage() << ", base "
		  << h.rig.local->commandPoints().getBase() << ", bonus " << h.rig.local->commandPoints().getBonus() << ", cap " << h.rig.local->commandPoints().getCap() << ", the unit's "
		  << fieldText(*tt, "CommandPoints") << ")";
		r.outcome = o.str();
		return r;
	}
	std::set<ObjectID> before;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		before.insert(o->getID());
	}
	const int expectCost = BuildAssistant::calcCostToBuild(*tt, h.rig.local, b, -1);
	const int expectFrames = BuildAssistant::calcTimeToBuild(*tt, h.rig.local, b, -1, logic.productionSettings(), logic);
	const HudRig::Purse money0 = h.purse();
	if (!h.hud->controlBar().pressButton(cb.slot, cb.inPalantir))
	{
		r.outcome = "press refused";
		return r;
	}
	const ObjectID producerId = b->getID();
	std::vector<float> rates{ prodrate::rateNow(*b) }; // lane PRODUCTION1: the producer's PRODUCTION rate of every frame (RW 0x8A1EBC)
	h.frames(1);
	r.cost = h.paidSince(money0);
	std::ostringstream out;
	if (r.cost != expectCost || cb.cost != expectCost)
	{
		out << "paid " << r.cost << ", the button shows " << cb.cost << ", calcCostToBuild " << expectCost << "; ";
	}
	Object *made = nullptr;
	for (; r.frames < expectFrames + 60 && !made; ++r.frames)
	{
		if (Object *p = logic.findObjectByID(producerId))
		{
			rates.push_back(prodrate::rateNow(*p));
		}
		h.frames(1);
		for (Object *o = logic.getFirstObject(); o && !made; o = o->getNextObject())
		{
			if (!before.count(o->getID()) && o->getTemplate() == tt->getFinalOverride() && o->getControllingPlayer() == h.rig.local)
			{
				made = o;
			}
			if (!before.count(o->getID()) && o->getTemplate()->getName() == tt->getName() && o->getControllingPlayer() == h.rig.local)
			{
				made = o;
			}
		}
	}
	if (!made)
	{
		out << "no " << r.unit << " after " << r.frames << " frames (calcTimeToBuild " << expectFrames << ")";
	}
	else
	{
		const int rated = prodrate::expectedFrames(rates, expectFrames); // a level 2 / 3 building is faster (its BuildSpeedModLvl<n>)
		if (r.frames < rated - 1)
		{
			out << "came after " << r.frames << " frames, calcTimeToBuild " << expectFrames << " (" << rated << " at the producer's PRODUCTION rate); ";
		}
		const float dx = made->getPosition()->x - b->getPosition()->x, dy = made->getPosition()->y - b->getPosition()->y;
		if (dx * dx + dy * dy > 600.0f * 600.0f)
		{
			out << "appeared " << std::sqrt(dx * dx + dy * dy) << " from the producer; ";
		}
		h.frames(30); // a horde's members come out one by one
		std::vector<ObjectID> gone{ made->getID() };
		const int usage = h.rig.local->commandPoints().getUsage();
		destroyAll(logic, gone);
		h.frames(2);
		r.commandPoints = usage - h.rig.local->commandPoints().getUsage();
		if (r.commandPoints <= 0 && fieldText(*tt, "CommandPoints") != "0" && fieldText(*tt, "CommandPoints") != "-")
		{
			out << "the unit used no command points (usage " << usage << " -> " << h.rig.local->commandPoints().getUsage() << ", CommandPoints " << fieldText(*tt, "CommandPoints") << "); ";
		}
	}
	r.outcome = out.str().empty() ? "ok" : out.str();
	// an empty queue for the next button
	for (int i = 0; i < 400 && b->getProductionUpdate() && b->getProductionUpdate()->getProductionCount() > 0; ++i)
	{
		h.frames(1);
	}
	h.frames(2);
	return r;
}
} // namespace

TEST_CASE("factc production: every UNIT_BUILD button of the Goblins' and Angmar's structures trains its unit through the HUD (cost, time, the unit)")
{
	if (!haveWorld("factc production"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (const std::string faction : kFactions)
	{
		const Inventory inv = startedInventory(s, faction);
		HudRig h(s, faction.c_str());
		const auto ctx = h.hud->enterContext();
		h.rig.local->depositMoney(500000);
		buildStructures(h, inv);
		std::vector<ProductionResult> results;
		std::set<std::pair<std::string, std::string>> done;
		for (const Offer &of : inv.offers)
		{
			if (of.cb->m_command != GUI_COMMAND_UNIT_BUILD || !of.cb->getThingTemplate() || !done.insert({ of.object, of.button }).second)
			{
				continue;
			}
			const ThingTemplate *pt = s.world->things().findTemplate(of.object);
			if (!pt || !isStructure(*pt) || of.object.find("MordorBase") != std::string::npos)
			{
				continue; // a unit's own build buttons (a summoner) are the ability sweep's
			}
			std::fprintf(stderr, "factc production start %s %s\n", of.object.c_str(), of.button.c_str());
			results.push_back(runProduction(h, of));
		}
		int ok = 0;
		for (const ProductionResult &r : results)
		{
			ok += r.outcome == "ok" ? 1 : 0;
			std::printf("factc production %s %s %s %s (cost %d, %d frames, %d command points): %s\n", faction.c_str(), r.producer.c_str(), r.button.c_str(), r.unit.c_str(), r.cost, r.frames, r.commandPoints, r.outcome.c_str());
		}
		MESSAGE(faction << ": " << ok << " of " << results.size() << " build buttons work");
		for (const ProductionResult &r : results)
		{
			CHECK_MESSAGE(r.outcome == "ok", faction << " " << r.producer << " " << r.button << ": " << r.outcome);
		}
	}
}

namespace
{
struct HeroResult
{
	std::string hero, outcome;
	int cost = 0, reviveCost = 0;
};

// the player's keep (the fortress citadel the castle unpacked: a structure with a hero list)
Object *keepOf(HudRig &h, const std::string &fortress)
{
	for (Object *o = h.logic().getFirstObject(); o; o = o->getNextObject())
	{
		const std::string &n = o->getTemplate()->getName();
		if (o->getControllingPlayer() == h.rig.local && n.find(fortress) == 0 && n.find("Citadel") != std::string::npos)
		{
			return o;
		}
	}
	return nullptr;
}

Object *liveHero(GameLogic &logic, Player *owner, const std::string &name)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == owner && o->getTemplate()->getName() == name && !o->isEffectivelyDead())
		{
			return o;
		}
	}
	return nullptr;
}

// the keep's REVIVE button of the hero record `index`: press it, the record's cost leaves the bank and the hero comes; it dies; the button offers the revive at
// its cost (RW 0x780AD3) and a press brings the hero back
HeroResult runHero(HudRig &h, Object *keep, int index)
{
	GameLogic &logic = h.logic();
	PlayerHeroList &list = h.rig.local->heroes();
	HeroResult r{ list.at(index)->templateName, "" };
	auto pressRevive = [&](int &paid) -> std::string {
		h.reset();
		if (!h.select(keep))
		{
			return "a click does not select the keep";
		}
		// the hero's record by its template (a death may move it: the index of the first press is not the revive's)
		ControlBarButton b;
		if (!h.findButtonIf(
				[&](const ControlBarButton &x) {
					const HeroRecord *rec = x.button->m_command == GUI_COMMAND_REVIVE ? list.at(x.reviveIndex) : nullptr;
					return rec && rec->templateName == r.hero;
				},
				b))
		{
			return "no REVIVE button for the record";
		}
		index = b.reviveIndex;
		if (b.state != ButtonState::Enabled)
		{
			return std::string("REVIVE button ") + stateName(b.state);
		}
		const int expect = list.costAt(logic, *h.rig.local, index, keep);
		const HudRig::Purse money0 = h.purse();
		if (!h.hud->controlBar().pressButton(b.slot, b.inPalantir))
		{
			return "press refused";
		}
		h.frames(2);
		paid = h.paidSince(money0);
		if (paid != expect || b.cost != expect)
		{
			std::ostringstream o;
			o << "paid " << paid << ", the button shows " << b.cost << ", the record's cost " << expect;
			return o.str();
		}
		return "";
	};
	auto waitHero = [&]() -> Object * {
		for (int i = 0; i < 1500; ++i)
		{
			if (Object *o = liveHero(logic, h.rig.local, r.hero))
			{
				return o;
			}
			h.frames(1);
		}
		return nullptr;
	};
	std::string e = pressRevive(r.cost);
	if (!e.empty())
	{
		r.outcome = "recruit: " + e;
		return r;
	}
	Object *hero = waitHero();
	if (!hero)
	{
		r.outcome = "recruit: the hero never came";
		return r;
	}
	h.frames(20);
	hero->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	h.frames(60);
	if (liveHero(logic, h.rig.local, r.hero))
	{
		r.outcome = "the hero did not die";
		return r;
	}
	std::set<ObjectID> before;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		before.insert(o->getID());
	}
	e = pressRevive(r.reviveCost);
	if (!e.empty())
	{
		r.outcome = "revive: " + e;
		return r;
	}
	hero = waitHero();
	r.outcome = hero ? "ok" : "revive: the hero never came back";
	if (!hero)
	{
		const HeroRecord *rec = list.at(index);
		std::ostringstream o;
		o << " (record dead " << (rec ? rec->dead : false) << ", startFrame " << (rec ? rec->startFrame : -2) << ", seconds " << (rec ? rec->seconds : -1) << ", now " << logic.getFrame()
		  << "; new objects of the player:";
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			if (!before.count(x->getID()) && x->getControllingPlayer() == h.rig.local)
			{
				o << " " << x->getTemplate()->getName() << (x->isEffectivelyDead() ? "(dead)" : "");
			}
		}
		r.outcome += o.str() + ")";
	}
	if (hero)
	{
		std::vector<ObjectID> gone{ hero->getID() };
		destroyAll(logic, gone);
		h.frames(2);
	}
	return r;
}
} // namespace

TEST_CASE("factc heroes: every Goblin and Angmar hero is recruited from the keep's REVIVE buttons, dies and is revived through the HUD")
{
	if (!haveWorld("factc heroes"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (const std::string faction : kFactions)
	{
		HudRig h(s, faction.c_str());
		const auto ctx = h.hud->enterContext();
		h.rig.local->depositMoney(500000);
		const PlayerTemplate *pt = h.rig.local->getPlayerTemplate();
		const Coord3D c = h.rig.freeSpot(2800, 1400, 500.0f);
		h.makeBuilt(pt->m_startingBuilding, c.x, c.y);
		h.frames(30);
		Object *keep = keepOf(h, pt->m_startingBuilding);
		REQUIRE_MESSAGE(keep != nullptr, faction << ": no keep");
		std::vector<HeroResult> results;
		const PlayerHeroList &list = h.rig.local->heroes();
		// by name: a death and a revive may reorder the records (the index of a record is looked up again for each hero)
		for (const std::string &name : pt->m_buildableHeroesMP)
		{
			int index = -1;
			for (int i = 0; i < (int)list.size() && index < 0; ++i)
			{
				index = list.at(i) && list.at(i)->templateName == name ? i : -1;
			}
			if (name == "CreateAHero" || index < 0)
			{
				CHECK_MESSAGE(name == "CreateAHero", faction << ": no hero record for " << name);
				continue;
			}
			std::fprintf(stderr, "factc hero start %s\n", name.c_str());
			results.push_back(runHero(h, keep, index));
		}
		int ok = 0;
		for (const HeroResult &r : results)
		{
			ok += r.outcome == "ok" ? 1 : 0;
			std::printf("factc hero %s %s (recruit %d, revive %d): %s\n", faction.c_str(), r.hero.c_str(), r.cost, r.reviveCost, r.outcome.c_str());
		}
		MESSAGE(faction << ": " << ok << " of " << results.size() << " heroes recruited and revived");
		CHECK(results.size() >= 4);
		for (const HeroResult &r : results)
		{
			CHECK_MESSAGE(r.outcome == "ok", faction << " " << r.hero << ": " << r.outcome);
		}
	}
}

namespace
{
struct FightResult
{
	std::string unit, outcome;
	float damage = 0.0f;
	int frames = 0;
};

// a fresh `unit` of the player is ordered by a right click to attack an enemy in front of it: the target must lose health
FightResult runFight(HudRig &h, const std::string &unit, const Coord3D &spot)
{
	FightResult r{ unit, "" };
	GameLogic &logic = h.logic();
	h.reset();
	Object *o = h.makeBuilt(unit, spot.x, spot.y);
	std::vector<ObjectID> made{ o->getID() };
	h.frames(8);
	// the target 226 away, or nearer for a unit that sees less (a fogged target is no order target: ThrallMaster VisionRange 175)
	// and inside what the player sees through it: the shroud clears ShroudClearingRange around a unit (the Angmar thrall master's is 25: SHROUD_CLEAR_STANDARD)
	float vision = 1000.0f;
	for (const char *field : { "VisionRange", "ShroudClearingRange" })
	{
		if (const FieldValue *v = o->getTemplate()->getFinalOverride()->findField(field))
		{
			if (const float *f = std::get_if<float>(v))
			{
				vision = std::min(vision, *f);
			}
		}
	}
	const float d = std::max(12.0f, std::min(160.0f, (vision - 30.0f) / 1.4142f));
	// the reference target: an orc warrior (a mountain troll flings infantry away before it can swing); a siege engine shoots at a troll, a target its
	// MinimumAttackRange and scatter can hit
	const bool siege = o->isKindOfName("SIEGEENGINE");
	Object *enemy = h.rig.make(siege ? "MordorMountainTroll" : "MordorFighter", spot.x + d, spot.y - d, h.rig.game->players().findPlayerWithName("Player_2"));
	made.push_back(enemy->getID());
	if (AIUpdateInterface *ai = enemy->getAIUpdateInterface())
	{
		ai->aiIdle(CMD_FROM_AI);
	}
	h.frames(6); // the shroud shows the target to the player before the order (a fogged object is no order target)
	bool armed = false;
	const std::vector<Object *> all = withMembers(o);
	for (size_t k = all.size() > 1 ? 1 : 0; k < all.size(); ++k) // a horde's own weapon is its rangefinder: the members decide
	{
		armed = armed || ObjectTemplateInfoBuilder::build(*all[k]->getTemplate()->getFinalOverride()).canPossiblyHaveAnyWeapon;
	}
	if (!armed)
	{
		r.outcome = "ok (no weapon in its data: a spell caster)";
		destroyAll(logic, made);
		return r;
	}
	BodyModuleInterface *body = enemy->getBodyModule();
	REQUIRE(body != nullptr);
	const float hp0 = body->getHealth();
	if (!h.select(o))
	{
		r.outcome = "a click does not select it";
		destroyAll(logic, made);
		return r;
	}
	h.rig.lookAt(*enemy->getPosition());
	// the order: a right click on the troll (it may walk under the pointer of another object: a click that became no attack is made again)
	std::string sent;
	for (int tries = 0; tries < 5; ++tries)
	{
		h.rig.lookAt(*enemy->getPosition());
		h.clickAt({ enemy->getPosition()->x, enemy->getPosition()->y, enemy->getPosition()->z + 10.0f }, true);
		h.frames(1);
		const std::vector<std::string> &log = h.hud->input().messageLog();
		sent += (log.empty() ? std::string("none") : log.back().substr(0, log.back().find(' '))) + " ";
		if (!log.empty() && log.back().find("ATTACK") != std::string::npos)
		{
			break;
		}
		h.frames(2);
	}
	// the order's target hidden from the player (S-565: a fogged object is no order target for a human player's unit, RW 0x82C167)
	const bool fogged = ShroudManager::isShroudedForAction(*o, *enemy);
	const ObjectID oid = o->getID();
	for (; r.frames < 600 && body->getHealth() >= hp0; ++r.frames)
	{
		h.frames(1);
	}
	r.damage = hp0 - body->getHealth();
	r.outcome = r.damage > 0.0f ? "ok" : "the target took no damage in 600 frames";
	if (r.damage <= 0.0f)
	{
		std::ostringstream w;
		if (const Object *a = logic.findObjectByID(oid)) // the unit may have died meanwhile (the target fights back)
		{
			const float dx = a->getPosition()->x - enemy->getPosition()->x, dy = a->getPosition()->y - enemy->getPosition()->y;
			w << " (distance " << std::sqrt(dx * dx + dy * dy) << ", moved " << a->getPosition()->x - spot.x << "," << a->getPosition()->y - spot.y;
		}
		else
		{
			w << " (the unit died";
		}
		w << "; the clicks sent " << sent << (fogged ? "; the target fogged for the player at the order: S-565" : "") << ")";
		r.outcome += w.str();
	}
	destroyAll(logic, made);
	h.frames(2);
	return r;
}
} // namespace

TEST_CASE("factc combat: every Goblin and Angmar unit and hero attacks an enemy on a click and damages it")
{
	if (!haveWorld("factc combat"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (const std::string faction : kFactions)
	{
		const Inventory inv = startedInventory(s, faction);
		HudRig h(s, faction.c_str());
		const auto ctx = h.hud->enterContext();
		const Coord3D spot = h.rig.freeSpot(2800, 1400, 400.0f);
		std::vector<FightResult> results;
		for (const std::set<std::string> *list : { &inv.units, &inv.heroes })
		{
			for (const std::string &u : *list)
			{
				const ThingTemplate *tt = s.world->things().findTemplate(u);
				const ObjectTemplateInfo info = tt ? ObjectTemplateInfoBuilder::build(*tt->getFinalOverride()) : ObjectTemplateInfo{};
				if (!tt || MaskTest(info.kindOf, (unsigned)ObjectTemplateInfoBuilder::kindOfIndex("DOZER")) || (!info.canPossiblyHaveAnyWeapon && !hasNugget(*tt, "HordeContain")))
				{
					continue; // the builders
				}
				std::fprintf(stderr, "factc fight start %s\n", u.c_str());
				results.push_back(runFight(h, u, spot));
			}
		}
		int ok = 0;
		for (const FightResult &r : results)
		{
			ok += r.outcome.compare(0, 2, "ok") == 0 ? 1 : 0;
			std::printf("factc fight %s %s (%.1f damage after %d frames): %s\n", faction.c_str(), r.unit.c_str(), r.damage, r.frames, r.outcome.c_str());
		}
		MESSAGE(faction << ": " << ok << " of " << results.size() << " units fight");
		for (const FightResult &r : results)
		{
			if (r.unit == "AngmarThrallMaster" && r.outcome.find("fogged for the player at the order") != std::string::npos)
			{
				// S-565: the target 17 away stays fogged for the player (the thrall master's ShroudClearingRange is 25), so the click's attack is refused
				MESSAGE(faction << " " << r.unit << ": KNOWN S-565 (" << r.outcome << ")");
				continue;
			}
			CHECK_MESSAGE(r.outcome.compare(0, 2, "ok") == 0, faction << " " << r.unit << ": " << r.outcome);
		}
	}
}

TEST_CASE("factc stops: S-2000 / S-2001 / S-2002 are reported")
{
	bool s2000 = false, s2002 = false;
	if (haveWorld("factc stops"))
	{
		SharedWorld &s = shared();
		HudRig h(s, "FactionWild");
		for (const std::string &l : h.hud->input().stops())
		{
			s2000 = s2000 || l.compare(0, 8, "[S-2000]") == 0;
			s2002 = s2002 || l.compare(0, 8, "[S-2002]") == 0;
		}
		CHECK(s2000);
		CHECK(s2002);
	}
	const std::vector<std::string> d = HordeDispatchSpecialPower::acceptanceStops();
	REQUIRE(d.size() == 1);
	CHECK(d[0].compare(0, 8, "[S-2001]") == 0);
	const std::vector<std::string> o = ObjectCreationUpgrade::acceptanceStops();
	REQUIRE(o.size() == 1);
	CHECK(o[0].compare(0, 8, "[S-2007]") == 0);
}

namespace
{
// the neutral pieces of a skirmish map: the structures no slot player owns that spawn (lairs), can be captured (flags), sit by a flag (inns, outposts,
// signal fires) or produce; and the creeps a spawner made
bool isNeutralPiece(Object &o)
{
	if (SpawnBehaviorInterface::of(o) || o.findModule("SlavedUpdate"))
	{
		return true;
	}
	return o.isKindOfName("STRUCTURE") && (o.isKindOfName("CAPTURABLE") || o.isKindOfName("LINKED_TO_FLAG") || o.getProductionUpdate());
}

std::vector<std::string> multiplayerMaps(SharedWorld &s)
{
	std::vector<MapCacheEntry> cache;
	std::string error;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	std::vector<std::string> out;
	for (const MapCacheEntry &e : cache)
	{
		if (!e.isMultiplayer || e.name.compare(0, 5, "maps/") != 0)
		{
			continue;
		}
		const std::string rest = e.name.substr(5);
		const std::string dir = rest.substr(0, rest.find('/'));
		if (std::find(out.begin(), out.end(), dir) == out.end())
		{
			out.push_back(dir);
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}
} // namespace

TEST_CASE("factc neutral inventory: the lairs, creeps, flags and neutral buildings of every multiplayer map load and resolve")
{
	if (!haveWorld("factc neutral inventory"))
	{
		return;
	}
	SharedWorld &s = shared();
	std::map<std::string, std::set<std::string>> where; // template -> maps
	const std::vector<std::string> maps = multiplayerMaps(s);
	REQUIRE(maps.size() >= 20);
	int loaded = 0;
	for (const std::string &m : maps)
	{
		hudtest::Rig rig(s, m.c_str(), "FactionWild");
		++loaded;
		rig.frame(2);
		for (Object *o = rig.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() != rig.local && isNeutralPiece(*o))
			{
				where[o->getTemplate()->getName()].insert(m);
			}
		}
	}
	std::ostringstream out;
	for (const auto &kv : where)
	{
		out << kv.first << " (" << kv.second.size() << " maps)\n";
	}
	MESSAGE(loaded << " multiplayer maps; neutral pieces:\n" << out.str());
	std::printf("factc neutral %d maps\n%s", loaded, out.str().c_str());
	if (const char *path = std::getenv("OPENBFME_FACTC_NEUTRAL"))
	{
		std::ofstream f(path);
		for (const auto &kv : where)
		{
			f << kv.first << "\t" << kv.second.size() << "\t" << *kv.second.begin() << "\n";
		}
	}
	CHECK(loaded == (int)maps.size());
	CHECK(where.size() >= 5);
}

namespace
{
// the first multiplayer map with a capture flag that has a LINKED_TO_FLAG `want` building within 150 of it
bool findInnMap(SharedWorld &s, const std::string &want, std::string &mapOut, std::string &innOut)
{
	for (const std::string &m : multiplayerMaps(s))
	{
		hudtest::Rig rig(s, m.c_str(), "FactionWild");
		for (Object *f = rig.logic().getFirstObject(); f; f = f->getNextObject())
		{
			if (!f->isKindOfName("CAPTURABLE") || f->getControllingPlayer() == rig.local)
			{
				continue;
			}
			for (Object *b = rig.logic().getFirstObject(); b; b = b->getNextObject())
			{
				const float dx = b->getPosition()->x - f->getPosition()->x, dy = b->getPosition()->y - f->getPosition()->y;
				if (b != f && b->isKindOfName("LINKED_TO_FLAG") && b->getTemplate()->getName() == want && dx * dx + dy * dy <= 150.0f * 150.0f)
				{
					mapOut = m;
					innOut = b->getTemplate()->getName();
					return true;
				}
			}
		}
	}
	return false;
}
} // namespace

TEST_CASE("factc neutral: Goblin and Angmar parties capture the flags of an inn, an outpost, a signal fire and a shipwright by a right click (S-2002); the building follows its flag and trains its units")
{
	if (!haveWorld("factc neutral capture"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (const char *want : { "Inn", "Outpost", "SignalFire", "ShipWright" })
	for (const std::string faction : kFactions)
	{
	std::string map, inn;
	REQUIRE_MESSAGE(findInnMap(s, want, map, inn), "no multiplayer map has a " << want << " by a capture flag");
	MESSAGE("map " << map << ", " << inn);
	{
		HudRig h(s, faction.c_str(), map.c_str());
		const auto ctx = h.hud->enterContext();
		GameLogic &logic = h.logic();
		creeptest::removeCreeps(logic); // the map's lairs would fight the capture party
		Object *flag = nullptr, *building = nullptr;
		for (Object *f = logic.getFirstObject(); f && !building; f = f->getNextObject())
		{
			if (!f->isKindOfName("CAPTURABLE") || f->getControllingPlayer() == h.rig.local)
			{
				continue;
			}
			for (Object *b = logic.getFirstObject(); b; b = b->getNextObject())
			{
				const float dx = b->getPosition()->x - f->getPosition()->x, dy = b->getPosition()->y - f->getPosition()->y;
				if (b->getTemplate()->getName() == inn && dx * dx + dy * dy <= 150.0f * 150.0f)
				{
					flag = f;
					building = b;
				}
			}
		}
		REQUIRE(flag != nullptr);
		const std::string party = faction == "FactionWild" ? "GoblinFighterHorde" : "AngmarThrallMaster";
		const Coord3D c = h.rig.freeSpot(flag->getPosition()->x, flag->getPosition()->y - 220.0f, 80.0f);
		Object *horde = h.rig.make(party, c.x, c.y);
		h.frames(10);
		REQUIRE(h.select(horde));
		const size_t log0 = h.hud->input().messageLog().size();
		h.rig.lookAt(*flag->getPosition());
		h.clickAt({ flag->getPosition()->x, flag->getPosition()->y, flag->getPosition()->z + 8.0f }, true); // the order: a right click
		h.frames(2);
		bool sent = false;
		for (size_t i = log0; i < h.hud->input().messageLog().size(); ++i)
		{
			sent = sent || h.hud->input().messageLog()[i].find("MSG_DO_SPECIAL_POWER_AT_OBJECT") != std::string::npos;
		}
		CHECK_MESSAGE(sent, faction << ": the click on the flag sent no capture");
		int frames = 0;
		for (; frames < 600 && flag->getControllingPlayer() != h.rig.local; ++frames)
		{
			h.frames(1);
		}
		CHECK_MESSAGE(flag->getControllingPlayer() == h.rig.local, faction << ": the flag was not captured in 600 frames");
		h.frames(20);
		CHECK_MESSAGE(building->getControllingPlayer() == h.rig.local, faction << ": the " << inn << " did not follow its flag");
		std::printf("factc neutral %s %s: flag captured after %d frames, %s owned by the player: %d\n", faction.c_str(), map.c_str(), frames, inn.c_str(),
		            building->getControllingPlayer() == h.rig.local ? 1 : 0);
		if (building->getControllingPlayer() != h.rig.local)
		{
			continue;
		}
		if (building->findModule("AutoDepositUpdate"))
		{
			// an outpost pays its new owner (AutoDepositUpdate: the capture bonus at once, then the deposits)
			const std::uint32_t m0 = h.rig.local->getMoney()->countMoney();
			h.frames(300);
			const std::uint32_t m1 = h.rig.local->getMoney()->countMoney();
			CHECK_MESSAGE(m1 > m0, faction << ": the captured " << inn << " paid nothing in 60 s");
			std::printf("factc neutral %s %s: %u paid in 60 s\n", faction.c_str(), inn.c_str(), m1 - m0);
		}
		// the inn's units for the faction (its CommandSetUpgrade by the faction upgrade): one is trained through the HUD
		h.rig.local->depositMoney(50000);
		h.reset();
		REQUIRE(h.select(building));
		ControlBarButton b;
		const bool found = h.findButtonIf([](const ControlBarButton &x) { return x.button->m_command == GUI_COMMAND_UNIT_BUILD && x.state == ButtonState::Enabled; }, b);
		std::printf("factc neutral %s %s: command set %s, a unit to train: %d\n", faction.c_str(), inn.c_str(), h.hud->controlBar().commandSetName().c_str(), found ? 1 : 0);
		if (inn == "Inn")
		{
			CHECK_MESSAGE(found, faction << ": the captured inn shows no unit to train (command set " << h.hud->controlBar().commandSetName() << ")");
		}
		if (!found)
		{
			continue;
		}
		const ThingTemplate *tt = b.button->getThingTemplate();
		REQUIRE(h.hud->controlBar().pressButton(b.slot, b.inPalantir));
		Object *made = nullptr;
		for (int i = 0; i < 1500 && !made; ++i)
		{
			h.frames(1);
			for (Object *o = logic.getFirstObject(); o && !made; o = o->getNextObject())
			{
				made = (o->getTemplate()->getName() == tt->getName() && o->getControllingPlayer() == h.rig.local && o->getProducerID() == building->getID()) ? o : nullptr;
			}
		}
		CHECK_MESSAGE(made != nullptr, faction << ": the inn's " << tt->getName() << " never came");
		std::printf("factc neutral %s inn unit %s (command set %s): %s\n", faction.c_str(), tt->getName().c_str(), h.hud->controlBar().commandSetName().c_str(), made ? "ok" : "never came");
	}
	}
}

TEST_CASE("factc recapture: an outpost flag an enemy holds shows the capture cursor, not the attack cursor, and a right click takes it back (UNATTACKABLE, RW 0x6C91FA)")
{
	if (!haveWorld("factc recapture"))
	{
		return;
	}
	SharedWorld &s = shared();
	std::string map, outpost;
	REQUIRE_MESSAGE(findInnMap(s, "Outpost", map, outpost), "no multiplayer map has an Outpost by a capture flag");
	MESSAGE("map " << map);
	HudRig h(s, "FactionWild", map.c_str());
	const auto ctx = h.hud->enterContext();
	GameLogic &logic = h.logic();
	creeptest::removeCreeps(logic);
	Object *flag = nullptr, *building = nullptr;
	for (Object *f = logic.getFirstObject(); f && !building; f = f->getNextObject())
	{
		if (!f->isKindOfName("CAPTURABLE") || f->getControllingPlayer() == h.rig.local)
		{
			continue;
		}
		for (Object *b = logic.getFirstObject(); b; b = b->getNextObject())
		{
			const float dx = b->getPosition()->x - f->getPosition()->x, dy = b->getPosition()->y - f->getPosition()->y;
			if (b->getTemplate()->getName() == outpost && dx * dx + dy * dy <= 150.0f * 150.0f)
			{
				flag = f;
				building = b;
			}
		}
	}
	REQUIRE(flag != nullptr);
	CHECK(flag->isKindOfName("UNATTACKABLE")); // retail data: CaptureFlag's KindOf
	// the enemy took them: the flag and its outpost on the enemy's default team, as SpecialAbilityUpdate's capture leaves them (RW 0x8546F6 / 0x854719)
	Player *enemy = h.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	flag->setCapturedTeam(enemy->getDefaultTeam());
	building->setCapturedTeam(enemy->getDefaultTeam());
	REQUIRE(h.rig.local->getRelationship(flag->getTeam()) == ENEMIES);
	const Coord3D c = h.rig.freeSpot(flag->getPosition()->x, flag->getPosition()->y - 220.0f, 80.0f);
	Object *horde = h.rig.make("GoblinFighterHorde", c.x, c.y);
	h.frames(10);
	REQUIRE(h.select(horde));
	const Coord3D aim{ flag->getPosition()->x, flag->getPosition()->y, flag->getPosition()->z + 8.0f };
	h.rig.lookAt(*flag->getPosition());
	const ICoord2D px = h.rig.screenOf(aim);
	REQUIRE(HudObjects::pickObject(h.hud->input().context(), px) == flag);
	// the hover: the attack test refuses the UNATTACKABLE flag, the capture test answers (RW 0x81E60B)
	h.hud->mouseMove(px.x, px.y, 0);
	h.hud->update(0.034);
	CHECK(h.hud->input().ui().cursor() == "CaptureBuilding");
	// the order: a right click sends the capture power at the flag, no attack
	const size_t log0 = h.hud->input().messageLog().size();
	h.clickAt(aim, true);
	h.frames(2);
	int captures = 0, attacks = 0;
	for (size_t i = log0; i < h.hud->input().messageLog().size(); ++i)
	{
		captures += h.hud->input().messageLog()[i].find("MSG_DO_SPECIAL_POWER_AT_OBJECT") != std::string::npos ? 1 : 0;
		attacks += h.hud->input().messageLog()[i].find("MSG_DO_ATTACK_OBJECT") != std::string::npos ? 1 : 0;
	}
	CHECK(captures == 1);
	CHECK(attacks == 0);
	int frames = 0;
	for (; frames < 600 && flag->getControllingPlayer() != h.rig.local; ++frames)
	{
		h.frames(1);
	}
	CHECK_MESSAGE(flag->getControllingPlayer() == h.rig.local, "the flag was not recaptured in 600 frames");
	h.frames(20);
	CHECK_MESSAGE(building->getControllingPlayer() == h.rig.local, "the " << outpost << " did not follow its flag");
	std::printf("factc recapture %s: the enemy's flag recaptured after %d frames, %s owned by the player: %d\n", map.c_str(), frames, outpost.c_str(),
	            building->getControllingPlayer() == h.rig.local ? 1 : 0);
}

TEST_CASE("factc neutral lairs: every creep lair of the skirmish maps spawns its creeps, and they fight a Goblin party that comes near")
{
	if (!haveWorld("factc neutral lairs"))
	{
		return;
	}
	SharedWorld &s = shared();
	const std::vector<std::string> maps = multiplayerMaps(s);
	for (const char *lairName : { "CaveTrollLair", "MoriarGoblinLair", "WargLair", "SpiderLair", "FireDrakeLair", "BarrowWightLair" })
	{
		std::string map;
		for (const std::string &m : maps)
		{
			hudtest::Rig rig(s, m.c_str(), "FactionWild");
			for (Object *o = rig.logic().getFirstObject(); o && map.empty(); o = o->getNextObject())
			{
				map = o->getTemplate()->getName() == lairName ? m : map;
			}
			if (!map.empty())
			{
				break;
			}
		}
		REQUIRE_MESSAGE(!map.empty(), "no multiplayer map has a " << lairName);
		std::fprintf(stderr, "factc lair start %s on %s\n", lairName, map.c_str());
		HudRig h(s, "FactionWild", map.c_str());
		const auto ctx = h.hud->enterContext();
		GameLogic &logic = h.logic();
		Object *lair = nullptr;
		for (Object *o = logic.getFirstObject(); o && !lair; o = o->getNextObject())
		{
			lair = o->getTemplate()->getName() == lairName ? o : nullptr;
		}
		REQUIRE(lair != nullptr);
		// a skirmish start makes the map's creeps every slot player's enemies (SkirmishSides: PlyrCreeps in each player's enemies, both ways); the HUD rig loads the
		// map without that start, so the test sets the same relationship on the two default teams
		Player *creeps = h.rig.game->players().findPlayerWithName("PlyrCreeps");
		REQUIRE(creeps != nullptr);
		creeps->getDefaultTeam()->setTeamRelationship(h.rig.local->getDefaultTeam(), ENEMIES);
		h.rig.local->getDefaultTeam()->setTeamRelationship(creeps->getDefaultTeam(), ENEMIES);
		// lane PLAY-4 r3: and at the player level, as SkirmishSides does (the creeps side's playerEnemies is every player, each player's has PlyrCreeps): the lair's
		// spawns are on other teams of PlyrCreeps, which the default teams' relationship does not reach (they were neutral to the party; the guard's "attacked me"
		// condition RW 0x949994 answers an ENEMY attacker only, RW 0x68D7AB)
		creeps->setPlayerRelationship(h.rig.local, ENEMIES);
		h.rig.local->setPlayerRelationship(creeps, ENEMIES);
		h.frames(150);
		auto spawnsNear = [&]() {
			int n = 0;
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				const float dx = o->getPosition()->x - lair->getPosition()->x, dy = o->getPosition()->y - lair->getPosition()->y;
				n += (o->findModule("SlavedUpdate") && !o->isEffectivelyDead() && dx * dx + dy * dy < 600.0f * 600.0f) ? 1 : 0;
			}
			return n;
		};
		const int spawned = spawnsNear();
		CHECK_MESSAGE(spawned >= 1, lairName << " on " << map << " spawned nothing in 30 s");
		// a Goblin party walks up to the lair: the creeps defend it
		const Coord3D c = h.rig.freeSpot(lair->getPosition()->x, lair->getPosition()->y - 160.0f, 60.0f, true); // the party goes to the creeps
		Object *party = h.rig.make("GoblinFighterHorde", c.x, c.y);
		h.frames(10);
		std::vector<ObjectID> members; // by id: the creeps may kill the party (and the dead leave the world)
		float hp0 = 0.0f;
		for (Object *m : withMembers(party))
		{
			members.push_back(m->getID());
			hp0 += m->getBodyModule() ? m->getBodyModule()->getHealth() : 0.0f;
		}
		const ObjectID partyId = party->getID();
		party = nullptr;
		// lane PLAY-4 r3: the creeps' hits are counted frame by frame (a member's health that drops): with RotWK's melee reach (RW 0x6ED843) the party can win
		// and its members gain ranks, whose level-up heals them (Barrow Wights on Evendim: 1001 -> 1421 at the end), so the end's health alone does not show the fight
		int hits = 0;
		std::vector<float> lastHp;
		for (ObjectID id : members)
		{
			const Object *m = logic.findObjectByID(id);
			lastHp.push_back(m && m->getBodyModule() ? m->getBodyModule()->getHealth() : 0.0f);
		}
		for (int f = 0; f < 250; ++f)
		{
			h.frames(1);
			for (size_t k = 0; k < members.size(); ++k)
			{
				const Object *m = logic.findObjectByID(members[k]);
				const float now = m && m->getBodyModule() && !m->isEffectivelyDead() ? m->getBodyModule()->getHealth() : 0.0f;
				hits += now < lastHp[k] ? 1 : 0;
				lastHp[k] = now;
			}
		}
		int ranked = 0;
		for (ObjectID id : members)
		{
			const Object *m = logic.findObjectByID(id);
			ranked += m && m->getExperienceTracker() && m->getExperienceTracker()->getRank() > m->getExperienceTracker()->getBaseRank() ? 1 : 0;
		}
		float hp1 = 0.0f;
		for (ObjectID id : members)
		{
			const Object *m = logic.findObjectByID(id);
			hp1 += m && m->getBodyModule() && !m->isEffectivelyDead() ? m->getBodyModule()->getHealth() : 0.0f;
		}
		std::string rel;
		for (Object *o = logic.getFirstObject(); o && rel.empty(); o = o->getNextObject())
		{
			if (o->findModule("SlavedUpdate") && !o->isEffectivelyDead())
			{
				const float dx = o->getPosition()->x - c.x, dy = o->getPosition()->y - c.y;
				std::ostringstream w;
				(void)partyId;
				w << o->getTemplate()->getName() << " relationship to the player " << (int)o->getControllingPlayer()->getRelationship(h.rig.local->getDefaultTeam()) << ", distance " << std::sqrt(dx * dx + dy * dy)
				  << ", player " << (o->getControllingPlayer() ? o->getControllingPlayer()->getPlayerName() : std::string("none"));
				rel = w.str();
			}
		}
		std::printf("factc lair %s on %s: %d creeps; the party's health %.0f -> %.0f, %d hits taken, %d members ranked up; %s\n", lairName, map.c_str(), spawned, hp0, hp1, hits,
			ranked, rel.c_str());
		// lane PLAY-4 r3 (S-787): the Barrow Wights engage (attack state, a member as victim) but land nothing: the engagement destination (RW 0x6F37B3, its
		// obstacle-zone mapping not ported, RW 0x6F3082 inferred) lies behind the party's members, the path ends where the wight stands and the engage state
		// waits and asks again; the stance's guard (RW 0x743882 -> RW 0x662A21, its idle state's random draw RW 0x949630) moved the slaves' wander spots onto it.
		// Lane PATHPERF-1 (the merge of PLAY-4 r3): with the party on clear ground beside the lair (freeSpot's placement rules) the wights reach it and hit; the
		// gap stays in S-787, this scene no longer shows it
		{
			// tolerance: how many hits land depends on the creeps and the party's approach; one shows the creeps fought
			CHECK_MESSAGE(hits > 0, "the creeps of " << lairName << " did not attack the party");
		}
	}
}
