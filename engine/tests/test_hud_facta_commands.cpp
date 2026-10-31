// OpenBFME retail tests for lane FACT-A: the commands of Men, Elves and Dwarves the production and ability sweeps do not press, used as a player uses them on
// the control bar: the fortress's expansion pads (FOUNDATION_CONSTRUCT), the wall hubs' spans (the line build) and the wall segments' gates, the citadel's
// castle upgrades and what they reach (the castle, its members, the walls), the builders' constructions (DOZER_CONSTRUCT and the placement click), the
// heroes' recruitment and revival (the fortress's REVIVE slots, the ring hero), the unit upgrades of the hordes (OBJECT_UPGRADE on a battalion: the weapon,
// the armour), the spell book (the store's purchase and the book's cast) and the context commands (a capture, the porters' fire fighting).
// SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "FactATestUtil.h"
#include "FactAEffects.h"

#include "Common/PlayerHeroList.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/ModelState.h"
#include "GameLogic/Construction.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpellEffectModules.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Armor.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Module/ExperienceUpgradeModules.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AreaScanModules.h"
#include "GameLogic/Module/CastleUpgrade.h"
#include "GameLogic/Module/FlammableUpdate.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/ReplaceSelfUpgrade.h"
#include "GameLogic/WallSpan.h"
#include "GameClient/GUI/AptScreens/AptSpellStore.h"
#include "GameClient/SpellBookUI.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponState.h"
#include "GameLogic/WeaponStores.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <string>
#include <vector>

using namespace facta;

namespace
{
// up to `n` frames until `done`; true when it held
bool waitFor(FactRig &r, int n, const std::function<bool()> &done)
{
	for (int f = 0; f < n; ++f)
	{
		if (done())
		{
			return true;
		}
		r.frames(1);
	}
	return done();
}

// the shown buttons with the command
std::vector<ControlBarButton> shownWith(FactRig &r, int command)
{
	std::vector<ControlBarButton> out;
	for (const ControlBarButton &b : r.shown())
	{
		if (b.button && b.button->m_command == command)
		{
			out.push_back(b);
		}
	}
	return out;
}

// a click on the pad: the rig's points first, then a grid over its footprint (a flat floor model: the camera ray meets it away from its centre)
bool selectPad(FactRig &r, Object *pad)
{
	if (r.select(pad))
	{
		return true;
	}
	for (int gy = -3; gy <= 3; ++gy)
	{
		for (int gx = -3; gx <= 3; ++gx)
		{
			r.hud->input().ui().deselectAll();
			Coord3D at = *pad->getPosition();
			at.x += 15.0f * (float)gx;
			at.y += 15.0f * (float)gy;
			r.rig.lookAt(at);
			r.frames(1);
			const ICoord2D px = r.rig.screenOf(at);
			r.hud->mouseMove(px.x, px.y, 0);
			r.rig.timeMs += 300;
			r.hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, r.rig.timeMs);
			r.rig.timeMs += 40;
			r.hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, r.rig.timeMs);
			r.rig.timeMs += 300;
			r.frames(2);
			const std::vector<ObjectID> &ui = r.hud->input().ui().selected();
			if (ui.size() == 1 && ui.front() == pad->getID() && r.local()->selection().size() == 1 && r.local()->selection().front() == pad->getID())
			{
				r.resetRange();
				return true;
			}
		}
	}
	return false;
}

// the object's model condition names
std::string conditions(const Object &o)
{
	std::string out;
	for (int bit = 0; bit < 19 * 32; ++bit)
	{
		if (o.testModelCondition(bit))
		{
			const char *n = ModelCondition::nameOf(bit);
			out += std::string(" ") + (n ? n : std::to_string(bit).c_str());
		}
	}
	return out;
}

// the objects within `d` of the object (template#id)
std::string nearby(FactRig &r, const Object &at, float d)
{
	std::string out;
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		const float dx = o->getPosition()->x - at.getPosition()->x, dy = o->getPosition()->y - at.getPosition()->y;
		if (o != &at && dx * dx + dy * dy < d * d)
		{
			out += " " + o->getTemplate()->getName() + "#" + std::to_string((unsigned)o->getID());
		}
	}
	return out;
}

bool kindOf(const Object &o, const char *k)
{
	return o.isKindOfName(k);
}

// the faction's castle unpacked as the start unpacks it (CastleBehavior::unpack, RW 0x79BE6A) on a free spot; null on failure
CastleBehavior *unpackCastle(FactRig &r, Object *&shell)
{
	const std::string &start = r.local()->getPlayerTemplate()->m_startingBuilding;
	shell = r.make(start, r.spot(2400.0f, 1300.0f, 400.0f));
	CastleBehavior *castle = shell ? dynamic_cast<CastleBehavior *>(shell->findModule("CastleBehavior")) : nullptr;
	REQUIRE_MESSAGE(castle, start);
	REQUIRE_MESSAGE(castle->unpack(), castle->lastError());
	r.frames(30);
	return castle;
}

// the object of the template (or one of its build variations) the player got after `before` (the newest)
Object *newOwn(FactRig &r, const ThingTemplate *tt, ObjectID before)
{
	Object *found = nullptr;
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		// lane FBSIM-1: a builder's site is RotWK's phantom (effectively dead, RW 0x88D0D8) until the builder places it
		static const int phantom = ObjectTemplateInfoBuilder::objectStatusIndex("PHANTOM_STRUCTURE");
		if (o->getID() > before && o->getControllingPlayer() == r.local() && (!o->isEffectivelyDead() || o->testStatus((unsigned)phantom))
			&& BuildAssistant::isEquivalentTo(o->getTemplate()->getFinalOverride(), tt->getFinalOverride()))
		{
			found = o;
		}
	}
	return found;
}

ObjectID lastId(FactRig &r)
{
	ObjectID id = INVALID_ID;
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		id = std::max(id == INVALID_ID ? (ObjectID)0 : id, o->getID());
	}
	return id;
}

struct Problems
{
	std::vector<std::string> list;
	void add(const std::string &p) { list.push_back(p); }
	void print(const char *what)
	{
		for (const std::string &p : list)
		{
			std::printf("facta %s problem: %s\n", what, p.c_str());
		}
	}
};

// a plot build from a pad's button (FOUNDATION_CONSTRUCT, RW 0x858701): the expansion rises on the pad and completes; null when it did not
Object *buildOnPad(FactRig &r, Object *pad, const CommandButton &b, Problems &p, const std::string &tag)
{
	const ThingTemplate *what = b.getThingTemplate();
	const std::vector<ObjectID> &already = r.hud->input().ui().selected();
	const bool selected = already.size() == 1 && already.front() == pad->getID() && r.local()->selection().size() == 1 && r.local()->selection().front() == pad->getID();
	if (!selected && !selectPad(r, pad))
	{
		const std::vector<ObjectID> &sel = r.hud->input().ui().selected();
		const Object *got = sel.empty() ? nullptr : r.logic().findObjectByID(sel.front());
		const CastleMemberBehavior *plot = dynamic_cast<const CastleMemberBehavior *>(pad->findModule("CastleMemberBehavior"));
		p.add(tag + ": a click does not select the pad (selected " + (got ? got->getTemplate()->getName() : std::string("nothing")) + ", plot taken "
			+ std::to_string(plot ? (int)plot->plotIsTaken() : -1) + ")");
		return nullptr;
	}
	const ControlBarButton *cb = r.find(b.m_name);
	if (!cb || cb->state != ButtonState::Enabled)
	{
		p.add(tag + ": the button is " + (cb ? "in state " + std::to_string((int)cb->state) : std::string("not shown")));
		return nullptr;
	}
	const ObjectID before = lastId(r);
	const unsigned money = r.money();
	const int cost = BuildAssistant::calcCostToBuild(*what, r.local(), pad, -1);
	if (!r.bar().pressButton(cb->slot, cb->inPalantir))
	{
		p.add(tag + ": the press was refused");
		return nullptr;
	}
	r.frames(1);
	if (r.hud->input().ui().isPlacing())
	{
		r.clickWorld(*pad->getPosition()); // a placement on the pad itself
	}
	Object *made = nullptr;
	waitFor(r, 20, [&]() { return (made = newOwn(r, what, before)) != nullptr; });
	if (!made)
	{
		p.add(tag + ": nothing was made on the pad");
		return nullptr;
	}
	// tolerance: the keep's AutoDepositUpdate (RW 0x89DB5D) pays GENERIC_KEEP_MONEY_AMOUNT 25 every GENERIC_KEEP_MONEY_TIME 6000 ms (180 frames); the
	// window between the two money reads (the press, the click, up to 20 frames until the structure shows) is under 25 frames, so exactly one deposit can
	// land in it (lane PLAY-4 r3: the Men's wall hub read 175 of 200); the +-5 is the existing allowance
	if ((int)(money - r.money()) < cost - 30 || (int)(money - r.money()) > cost + 5)
	{
		p.add(tag + ": charged " + std::to_string((int)(money - r.money())) + ", the cost is " + std::to_string(cost));
	}
	const int guard = 5 * (int)BuildAssistant::buildTime(*what) + 300;
	// lane PLAY-4 r3: risen means out of RotWK's under-construction context too (RW 0x71EBDA: GettingBuiltBehavior's + 0x36, which its completion check clears
	// within a second of the last heal, RW 0x857BDA); until then the bar shows only the cancel button (S-3801)
	if (!waitFor(r, guard, [&]() { return r.logic().findObjectByID(made->getID()) && !made->isUnderConstruction() && !ControlBar::isUnderConstructionContext(*made); }))
	{
		p.add(tag + ": still under construction after " + std::to_string(guard) + " frames (" + std::to_string((int)made->getConstructionPercent()) + " %)");
		return nullptr;
	}
	const float dx = made->getPosition()->x - pad->getPosition()->x, dy = made->getPosition()->y - pad->getPosition()->y;
	if (dx * dx + dy * dy > 30.0f * 30.0f)
	{
		p.add(tag + ": built away from its pad");
	}
	return made;
}

// a wall span from the hub (its line build button, RW 0x793E33), the pointer 320 away from the keep turned by `turn` radians, the click; the pieces the
// hub made, risen. Empty when no span was made
std::vector<ObjectID> buildSpan(FactRig &r, Object *hub, Object *keep, float turn, Problems &p, const std::string &f)
{
	std::vector<ObjectID> pieces;
	if (!r.select(hub))
	{
		p.add(f + " " + hub->getTemplate()->getName() + ": a click does not select the hub");
		return pieces;
	}
	const ControlBarButton *line = nullptr;
	for (const ControlBarButton &b : r.shown())
	{
		if (!line && b.button && b.button->m_command == GUI_COMMAND_DOZER_CONSTRUCT && BuildAssistant::isLineBuildTemplate(r.logic(), b.button->getThingTemplate(), hub))
		{
			line = &b;
		}
	}
	if (!line)
	{
		p.add(f + " " + hub->getTemplate()->getName() + ": no line build button on the hub");
		return pieces;
	}
	const float ox = hub->getPosition()->x - keep->getPosition()->x, oy = hub->getPosition()->y - keep->getPosition()->y;
	const float a = std::atan2(oy, ox) + turn;
	Coord3D end{ hub->getPosition()->x + 320.0f * std::cos(a), hub->getPosition()->y + 320.0f * std::sin(a), 0.0f };
	end.z = r.logic().getGroundHeight(end.x, end.y);
	const ObjectID before = lastId(r);
	REQUIRE(r.bar().pressButton(line->slot, line->inPalantir));
	r.frames(1);
	r.rig.lookAt(end);
	r.frames(1);
	const ICoord2D px = r.rig.screenOf(end);
	r.hud->mouseMove(px.x, px.y, 0);
	r.clickWorld(end);
	r.frames(3);
	r.hud->input().ui().setGUICommand(nullptr);
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getID() > before && o->getControllingPlayer() == r.local() && o->getProducerID() == hub->getID() && kindOf(*o, "STRUCTURE"))
		{
			pieces.push_back(o->getID());
		}
	}
	const bool up = waitFor(r, 1500, [&]() {
		for (ObjectID id : pieces)
		{
			const Object *o = r.logic().findObjectByID(id);
			if (o && o->isUnderConstruction())
			{
				return false;
			}
		}
		return true;
	});
	if (!pieces.empty() && !up)
	{
		p.add(f + " " + hub->getTemplate()->getName() + ": the span's pieces did not finish");
	}
	return pieces;
}
} // namespace

TEST_CASE("FACT-A fortress: every expansion of every pad built from its control bar; a wall span from a hub, the segment's gates; the citadel's castle upgrades reach the castle, the pads, the members and the walls")
{
	if (!hudtest::haveWorld("facta fortress"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	size_t problems = 0;
	for (const std::string &f : factions())
	{
		FactRig r(s, f.c_str());
		r.local()->depositMoney(2000000);
		Problems p;
		Object *shell = nullptr;
		CastleBehavior *castle = unpackCastle(r, shell);
		Object *keep = r.logic().findObjectByID(castle->keepId());
		REQUIRE(keep);
		std::vector<Object *> pads;
		for (const std::vector<ObjectID> *list : { &castle->foundations(), &castle->defenseFoundations() })
		{
			for (ObjectID id : *list)
			{
				if (Object *o = r.logic().findObjectByID(id))
				{
					pads.push_back(o);
				}
			}
		}
		REQUIRE(!pads.empty());
		// 1. the expansions: each FOUNDATION_CONSTRUCT button of each pad kind, on a free pad of that kind (one is freed again when all are taken)
		std::map<std::string, std::vector<Object *>> byKind;
		for (Object *pad : pads)
		{
			byKind[pad->getTemplate()->getName()].push_back(pad);
		}
		size_t built = 0, soldAndFreed = 0;
		std::vector<Object *> hubs;
		std::vector<std::pair<std::string, const CommandButton *>> deferred; // pad kind, button: no clickable free pad of the kind
		for (const auto &kv : byKind)
		{
			const CommandSet *set = TheCommandStore->findCommandSet(kv.second.front()->getCommandSetName());
			size_t next = 0;
			std::vector<std::pair<Object *, Object *>> taken; // pad, expansion
			for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (!b || b->m_command != GUI_COMMAND_FOUNDATION_CONSTRUCT || !b->getThingTemplate())
				{
					continue;
				}
				Object *pad = nullptr;
				// the next free pad of the kind a click selects (a pad behind the citadel, seen from the camera, is picked as the citadel: the next one)
				while (!pad && next < kv.second.size())
				{
					Object *cand = kv.second[next++];
					if (selectPad(r, cand))
					{
						pad = cand;
					}
				}
				if (!pad)
				{
					// every pad of the kind holds an expansion: the first one that is not a wall hub is sold through its bar (the pad comes back)
					for (auto &t : taken)
					{
						if (pad || !t.second || kindOf(*t.second, "WALL_HUB"))
						{
							continue;
						}
						Object *exp = t.second;
						const CommandButton *sell = setButton(exp->getCommandSetName(), GUI_COMMAND_SELL);
						if (!sell || !r.select(exp) || !r.press(sell->m_name))
						{
							p.add(f + " " + exp->getTemplate()->getName() + ": the expansion cannot be sold from its bar");
							continue;
						}
						const ObjectID eid = exp->getID();
						if (!waitFor(r, 400, [&]() { return r.logic().findObjectByID(eid) == nullptr; }))
						{
							p.add(f + " " + exp->getTemplate()->getName() + ": the sold expansion stays");
							continue;
						}
						t.second = nullptr;
						// the plot frees (CastleMemberBehavior, its occupant gone: RW 0x8584DB) and a click selects the pad again (when the camera sees it)
						const CastleMemberBehavior *plot = dynamic_cast<const CastleMemberBehavior *>(t.first->findModule("CastleMemberBehavior"));
						if (!waitFor(r, 60, [&]() { return plot && !plot->plotIsTaken(); }))
						{
							p.add(f + " " + t.first->getTemplate()->getName() + ": the plot stays taken after its expansion is sold");
							continue;
						}
						++soldAndFreed;
						if (!selectPad(r, t.first))
						{
							continue; // seen behind another model from the camera: the button waits for a fresh castle
						}
						pad = t.first;
					}
				}
				if (!pad)
				{
					deferred.push_back({ kv.first, b });
					continue;
				}
				const std::string tag = f + " " + kv.first + " " + b->m_name;
				Object *made = buildOnPad(r, pad, *b, p, tag);
				if (made)
				{
					++built;
					taken.push_back({ pad, made });
					if (kindOf(*made, "WALL_HUB"))
					{
						hubs.push_back(made);
					}
				}
			}
		}
		std::printf("facta fortress %s: %zu pads, %zu expansions built, %zu wall hubs\n", f.c_str(), pads.size(), built, hubs.size());
		// 2. a wall span from a hub: its line build button, the pointer 320 away from the castle, the click (RotWK's PlaceEventTranslator, RW 0x83E911)
		Object *hub = hubs.empty() ? nullptr : hubs.front();
		std::vector<ObjectID> segments = hub ? buildSpan(r, hub, keep, 0.0f, p, f) : std::vector<ObjectID>();
		if (segments.empty())
		{
			p.add(f + ": no wall span was built");
		}
		else
		{
			std::printf("facta fortress %s: a span of %zu pieces from %s\n", f.c_str(), segments.size(), hub->getTemplate()->getName().c_str());
		}
		// 3. the segment's upgrades: each OBJECT_UPGRADE button of a WALL_SEGMENT piece whose ReplaceSelfUpgrade turns it into another object (a gate, a postern,
		// a tower, a hub, an emplacement). Each on the middle segment of a span of its own (a replacement is refused where it would overlap a hub or a wall
		// upgrade, RW 0x8BB4D9, or stand on uneven ground, RW 0x8BB578): spans in other directions are tried until one is accepted
		size_t gates = 0;
		const Object *firstSeg = nullptr;
		for (ObjectID id : segments)
		{
			const Object *o = r.logic().findObjectByID(id);
			firstSeg = !firstSeg && o && kindOf(*o, "WALL_SEGMENT") ? o : firstSeg;
		}
		const CommandSet *segSet = firstSeg ? TheCommandStore->findCommandSet(firstSeg->getCommandSetName()) : nullptr;
		const ThingTemplate *segTemplate = firstSeg ? firstSeg->getTemplate() : nullptr;
		for (int slot = 0; segSet && slot < CommandSet::MAX_BUTTONS; ++slot)
		{
			const CommandButton *b = segSet->getCommandButton(slot);
			if (!b || b->m_command != GUI_COMMAND_OBJECT_UPGRADE)
			{
				continue;
			}
			const std::string tag = f + " " + segTemplate->getName() + " " + b->m_name;
			const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(b->m_upgradeName);
			bool replaced = false;
			std::string why;
			static const float turns[] = { 0.0f, 0.7f, -0.7f, 1.4f, -1.4f, 2.1f, -2.1f };
			for (float turn : turns)
			{
				if (replaced)
				{
					break;
				}
				const std::vector<ObjectID> span = turn == 0.0f ? segments : buildSpan(r, hub, keep, turn, p, f);
				// the middle segment: the farthest from every hub and wall upgrade
				Object *seg = nullptr;
				float best = -1.0f;
				for (ObjectID id : span)
				{
					Object *o = r.logic().findObjectByID(id);
					if (!o || !kindOf(*o, "WALL_SEGMENT") || o->getTemplate() != segTemplate)
					{
						continue;
					}
					float nearest = 1e9f;
					for (Object *h = r.logic().getFirstObject(); h; h = h->getNextObject())
					{
						if (kindOf(*h, "WALL_HUB") || kindOf(*h, "WALL_UPGRADE"))
						{
							const float dx = h->getPosition()->x - o->getPosition()->x, dy = h->getPosition()->y - o->getPosition()->y;
							nearest = std::min(nearest, dx * dx + dy * dy);
						}
					}
					if (nearest > best)
					{
						best = nearest;
						seg = o;
					}
				}
				if (!seg)
				{
					why += " (turn " + std::to_string(turn) + ": no segment)";
					continue;
				}
				std::string replaceWith;
				for (const auto &m : seg->modules())
				{
					const ReplaceSelfUpgradeModuleData *rd = dynamic_cast<const ReplaceSelfUpgradeModuleData *>(m->getModuleData());
					if (rd && !rd->m_triggeredBy.empty() && rd->m_triggeredBy.front() == b->m_upgradeName && !rd->m_pieces.empty())
					{
						replaceWith = rd->m_pieces.front();
					}
				}
				if (!r.select(seg))
				{
					why += " (turn " + std::to_string(turn) + ": a click does not select the segment)";
					continue;
				}
				const ControlBarButton *cb = r.find(b->m_name);
				if (!cb || cb->state != ButtonState::Enabled)
				{
					why += " (turn " + std::to_string(turn) + ": the button is " + (cb ? "in state " + std::to_string((int)cb->state) : std::string("not shown")) + ")";
					continue;
				}
				const ObjectID before = lastId(r);
				const Coord3D where = *seg->getPosition();
				const unsigned money = r.money();
				REQUIRE(r.bar().pressButton(cb->slot, cb->inPalantir));
				const int guard = u ? (int)(5.0f * u->getBuildTimeSeconds()) + 60 : 200;
				const ObjectID sid = seg->getID();
				waitFor(r, guard, [&]() { return !r.logic().findObjectByID(sid) || seg->hasUpgrade(u); });
				r.frames(5);
				if (r.logic().findObjectByID(sid) && !seg->hasUpgrade(u))
				{
					// refused by the placement test (wouldUpgrade): nothing was charged, the next direction is tried
					why += " (turn " + std::to_string(turn) + ": refused, affected " + std::to_string((int)seg->affectedByUpgrade(u)) + ", charged "
						+ std::to_string((int)(money - r.money())) + ")";
					continue;
				}
				if (replaceWith.empty())
				{
					replaced = true; // an upgrade that only marks the segment
					continue;
				}
				const ThingTemplate *rt = r.logic().things().findTemplate(replaceWith);
				Object *gate = rt ? newOwn(r, rt, before) : nullptr;
				if (!gate)
				{
					p.add(tag + ": the segment did not become " + replaceWith);
					replaced = true;
					continue;
				}
				const float dx = gate->getPosition()->x - where.x, dy = gate->getPosition()->y - where.y;
				if (dx * dx + dy * dy > 20.0f * 20.0f)
				{
					p.add(tag + ": " + replaceWith + " stands away from the segment");
				}
				if (u && (int)gate->getBuildCostPaid() != u->getBuildCost())
				{
					p.add(tag + ": " + replaceWith + "'s paid cost " + std::to_string((int)gate->getBuildCostPaid()) + ", the upgrade's BuildCost " + std::to_string(u->getBuildCost()));
				}
				++gates;
				replaced = true;
				std::printf("facta fortress %s: %s -> %s (turn %.1f)\n", f.c_str(), b->m_name.c_str(), replaceWith.c_str(), (double)turn);
			}
			if (!replaced)
			{
				p.add(tag + ": no span gave a segment the upgrade could replace" + why);
			}
		}
		std::printf("facta fortress %s: %zu wall pieces replaced by the segment's upgrades\n", f.c_str(), gates);
		if (gates > 0)
		{
			bool noted = false;
			for (const std::string &line : r.logic().report().stops)
			{
				noted = noted || line.rfind("[S-1986]", 0) == 0;
			}
			CHECK(noted); // the wall links are the stop
		}
		// 4. the citadel's castle upgrades (CastleUpgrade, FACT-B's port): each is bought on the citadel's bar and its granted upgrade must reach the castle, the
		// keep, every pad, the pads' expansions it affects and the player's walls within WallUpgradeRadius (RW 0x8BAAC0, 0x79986B, 0x8BA9EC)
		size_t propagated = 0;
		for (const auto &m : keep->modules())
		{
			const CastleUpgradeModuleData *cd = dynamic_cast<const CastleUpgradeModuleData *>(m->getModuleData());
			if (!cd)
			{
				continue;
			}
			const UpgradeTemplate *grant = TheUpgradeCenter->findUpgrade(cd->m_upgradeName);
			if (!grant || cd->m_triggeredBy.empty())
			{
				continue;
			}
			const std::string trigger = cd->m_triggeredBy.front();
			const std::string tag = f + " " + keep->getTemplate()->getName() + " CastleUpgrade " + trigger + " -> " + cd->m_upgradeName;
			if (!r.select(keep))
			{
				p.add(tag + ": a click does not select the citadel");
				continue;
			}
			const ControlBarButton *cb = nullptr;
			for (const ControlBarButton &b : r.shown())
			{
				cb = !cb && b.button && b.button->m_command == GUI_COMMAND_OBJECT_UPGRADE && b.button->m_upgradeName == trigger ? &b : cb;
			}
			std::string name = cb ? cb->button->m_name : std::string();
			const CommandSet *set = TheCommandStore->findCommandSet(keep->getCommandSetName());
			for (int slot = 0; !cb && set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (b && b->m_command == GUI_COMMAND_OBJECT_UPGRADE && b->m_upgradeName == trigger)
				{
					name = b->m_name;
					cb = r.find(name);
				}
			}
			if (!cb)
			{
				continue; // bought elsewhere (a later level's set): the production test buys every citadel button
			}
			if (cb->state != ButtonState::Enabled)
			{
				// a NEED_UPGRADE one (Ivory Tower after Stonework): its needs first
				for (const std::string &n : cb->button->m_neededUpgrade)
				{
					keep->giveUpgrade(n);
				}
				r.frames(3);
				cb = r.find(name);
			}
			if (!cb || cb->state != ButtonState::Enabled || !r.bar().pressButton(cb->slot, cb->inPalantir))
			{
				p.add(tag + ": the citadel's button cannot be pressed");
				continue;
			}
			const UpgradeTemplate *tu = TheUpgradeCenter->findUpgrade(trigger);
			if (!waitFor(r, tu ? (int)(5.0f * tu->getBuildTimeSeconds()) + 60 : 300, [&]() { return keep->hasUpgrade(tu); }))
			{
				p.add(tag + ": the purchase did not complete");
				continue;
			}
			r.frames(3);
			std::string missing;
			if (!shell->hasUpgrade(grant))
			{
				missing += " the castle";
			}
			if (!keep->hasUpgrade(grant))
			{
				missing += " the keep";
			}
			for (Object *pad : pads)
			{
				if (r.logic().findObjectByID(pad->getID()) && !pad->hasUpgrade(grant))
				{
					missing += " pad#" + std::to_string((unsigned)pad->getID());
				}
				const CastleMemberBehavior *plot = dynamic_cast<const CastleMemberBehavior *>(pad->findModule("CastleMemberBehavior"));
				Object *occupant = plot && plot->plotIsTaken() ? r.logic().findObjectByID(plot->occupantId()) : nullptr;
				if (occupant && occupant->affectedByUpgrade(grant) && !occupant->hasUpgrade(grant))
				{
					missing += " " + occupant->getTemplate()->getName();
				}
			}
			size_t walls = 0;
			for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
			{
				const bool wall = kindOf(*o, "WALL_SEGMENT") || kindOf(*o, "WALL_HUB") || kindOf(*o, "WALL_UPGRADE") || kindOf(*o, "DEFENSIVE_WALL");
				const float dx = o->getPosition()->x - keep->getPosition()->x, dy = o->getPosition()->y - keep->getPosition()->y, dz = o->getPosition()->z - keep->getPosition()->z;
				if (wall && o->getControllingPlayer() == r.local() && dx * dx + dy * dy + dz * dz <= cd->m_wallUpgradeRadius * cd->m_wallUpgradeRadius)
				{
					++walls;
					if (!o->hasUpgrade(grant))
					{
						missing += " wall " + o->getTemplate()->getName();
					}
				}
			}
			if (!missing.empty())
			{
				p.add(tag + ": the granted upgrade is missing on" + missing);
			}
			else
			{
				++propagated;
			}
			std::printf("facta fortress %s: %s reached the castle, the keep, %zu pads and %zu walls within %.0f\n", f.c_str(), tag.c_str(), pads.size(), walls,
				(double)cd->m_wallUpgradeRadius);
		}
		CHECK(propagated > 0);
		// 4b. the citadel's CASTLE_UPGRADE buttons (the House of Healing): availability RW 0x942733 case 8 (RW 0x943123), the press RW 0x940435 case 8
		// (MSG_QUEUE_UPGRADE on the citadel, a member of its castle); the cost is charged, the purchase completes, the button then shows owned (the castle has
		// it, state 8), and a PassiveAreaEffectBehavior the upgrade enables heals a wounded battalion within its radius that did not heal before
		size_t castleBought = 0;
		{
			std::vector<std::string> names;
			const CommandSet *set = TheCommandStore->findCommandSet(keep->getCommandSetName());
			for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (b && b->m_command == GUI_COMMAND_CASTLE_UPGRADE)
				{
					names.push_back(b->m_name);
				}
			}
			std::string patientName;
			for (const std::string &n : reachableTemplates(r))
			{
				if (patientName.empty() && n.find("Horde") != std::string::npos && n.find("Hero") == std::string::npos && n.find("Summoned") == std::string::npos)
				{
					patientName = n;
				}
			}
			for (const std::string &name : names)
			{
				const std::string tag = f + " " + keep->getTemplate()->getName() + " CASTLE_UPGRADE " + name;
				const CommandButton *button = TheCommandStore->findCommandButton(name);
				const UpgradeTemplate *u = button ? TheUpgradeCenter->findUpgrade(button->m_upgradeName) : nullptr;
				if (!u)
				{
					p.add(tag + ": no upgrade template");
					continue;
				}
				// the heal module the upgrade enables, on the citadel or a castle piece
				const PassiveAreaEffectBehaviorModuleData *heal = nullptr;
				Object *healer = nullptr;
				for (Object *o : { keep, shell })
				{
					for (const auto &m : o->modules())
					{
						const PassiveAreaEffectBehaviorModuleData *d = dynamic_cast<const PassiveAreaEffectBehaviorModuleData *>(m->getModuleData());
						if (!heal && d && d->m_upgradeRequired == u->getUpgradeName() && d->m_healPercentPerSecond > 0.0f)
						{
							heal = d;
							healer = o;
						}
					}
				}
				Object *patient = nullptr;
				float before = 0.0f;
				if (heal && !patientName.empty())
				{
					const Coord3D at{ healer->getPosition()->x + heal->m_effectRadius * 0.5f, healer->getPosition()->y, 0.0f };
					patient = r.make(patientName, at);
					r.frames(5);
					holdFire(*patient);
					wound(*patient);
					r.frames(2);
					before = groupHealth(*patient);
					r.frames(90);
					if (groupHealth(*patient) > before + 1.0f)
					{
						p.add(tag + ": the battalion healed before the purchase");
					}
					before = groupHealth(*patient);
				}
				if (!r.select(keep))
				{
					p.add(tag + ": a click does not select the citadel");
					continue;
				}
				const ControlBarButton *cb = r.find(name);
				if (!cb || cb->state != ButtonState::Enabled)
				{
					p.add(tag + ": not enabled on the citadel's bar (state " + (cb ? std::to_string((int)cb->state) : std::string("absent")) + ")");
					continue;
				}
				const unsigned money = r.money();
				const unsigned cost = (unsigned)u->calcCostToBuild(r.local(), keep);
				if (!r.bar().pressButton(cb->slot, cb->inPalantir))
				{
					p.add(tag + ": the press was refused");
					continue;
				}
				r.frames(3);
				if (money - r.money() != cost)
				{
					p.add(tag + ": charged " + std::to_string(money - r.money()) + ", the cost is " + std::to_string(cost));
				}
				if (!waitFor(r, (int)(5.0f * u->getBuildTimeSeconds()) + 60, [&]() { return keep->hasUpgrade(u); }))
				{
					p.add(tag + ": the purchase did not complete");
					continue;
				}
				r.frames(3);
				cb = r.select(keep) ? r.find(name) : nullptr;
				// lane HUD-7: an owned upgrade is RotWK's availability 8 (RW 0x9431A6), ButtonState::Purchased (the _cantAfford clip), not availability 0
				if (shell->hasUpgrade(u) && (!cb || cb->state != ButtonState::Purchased))
				{
					p.add(tag + ": the castle has it and the button does not show owned");
				}
				if (heal)
				{
					if (!patient)
					{
						p.add(tag + ": no battalion of the faction to heal");
						continue;
					}
					r.frames(90);
					const float after = groupHealth(*patient);
					std::printf("facta fortress %s: %s heals %s %.1f -> %.1f in 90 frames (%.0f %%/s within %.0f)\n", f.c_str(), tag.c_str(), patientName.c_str(),
						(double)before, (double)after, (double)(heal->m_healPercentPerSecond * 100.0f), (double)heal->m_effectRadius);
					if (after <= before + 1.0f)
					{
						p.add(tag + ": the wounded battalion within the radius did not heal");
						continue;
					}
				}
				if (patient)
				{
					r.logic().destroyObject(patient);
					r.frames(2);
				}
				++castleBought;
			}
			std::printf("facta fortress %s: %zu of %zu CASTLE_UPGRADE buttons bought and effective\n", f.c_str(), castleBought, names.size());
			CHECK(castleBought == names.size());
		}
		// 5. the buttons that found no clickable free pad: a fresh castle (the old one torn down) for each, its first clickable pad of the kind
		auto tearDown = [&]() {
			for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getControllingPlayer() == r.local() && (kindOf(*o, "STRUCTURE") || o == shell))
				{
					r.logic().destroyObject(o);
				}
			}
			r.frames(10);
		};
		for (const auto &d : deferred)
		{
			tearDown();
			CastleBehavior *again = unpackCastle(r, shell);
			Object *pad = nullptr;
			for (const std::vector<ObjectID> *list : { &again->foundations(), &again->defenseFoundations() })
			{
				for (ObjectID id : *list)
				{
					Object *o = r.logic().findObjectByID(id);
					if (!pad && o && o->getTemplate()->getName() == d.first && selectPad(r, o))
					{
						pad = o;
					}
				}
			}
			if (!pad)
			{
				p.add(f + " " + d.first + " " + d.second->m_name + ": no free pad of the kind could be clicked in a fresh castle");
				continue;
			}
			if (buildOnPad(r, pad, *d.second, p, f + " " + d.first + " " + d.second->m_name))
			{
				++built;
			}
		}
		std::printf("facta fortress %s: %zu expansions built in all, %zu sold expansions freed their plots\n", f.c_str(), built, soldAndFreed);
		p.print("fortress");
		problems += p.list.size();
	}
	CHECK(problems == 0);
}

TEST_CASE("FACT-A builders: every construction of the builders (DOZER_CONSTRUCT and the placement click; the walls' two-click span) is placed, paid, built and completed")
{
	if (!hudtest::haveWorld("facta builders"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	size_t problems = 0;
	for (const std::string &f : factions())
	{
		FactRig r(s, f.c_str());
		r.local()->depositMoney(2000000);
		Problems p;
		// the builders: the start's non-structure objects with a DOZER_CONSTRUCT button
		std::vector<std::string> builders;
		for (const std::string &n : factaStartTemplates(f))
		{
			const ThingTemplate *tt = r.logic().things().findTemplate(n);
			const CommandButton *b = tt ? setButton(templateCommandSet(*tt), GUI_COMMAND_DOZER_CONSTRUCT) : nullptr;
			if (b && std::find(builders.begin(), builders.end(), n) == builders.end() && !r.logic().things().findTemplate(n)->getName().empty())
			{
				Object *probe = r.make(n, r.spot(1500.0f, 1000.0f, 100.0f));
				const bool structure = kindOf(*probe, "STRUCTURE");
				r.logic().destroyObject(probe);
				r.frames(1);
				if (!structure)
				{
					builders.push_back(n);
				}
			}
		}
		REQUIRE_MESSAGE(!builders.empty(), f);
		size_t built = 0;
		float x = 1200.0f;
		for (const std::string &bn : builders)
		{
			const CommandSet *set = TheCommandStore->findCommandSet(templateCommandSet(*r.logic().things().findTemplate(bn)));
			for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (!b || b->m_command != GUI_COMMAND_DOZER_CONSTRUCT || !b->getThingTemplate())
				{
					continue;
				}
				const ThingTemplate *what = b->getThingTemplate();
				const std::string tag = f + " " + bn + " " + b->m_name;
				PathfindGeometry geom;
				ObjectGeometry::fillPathfindGeometry(*what->getFinalOverride(), geom);
				const float clear = std::max(260.0f, geom.boundingCircleRadius() * 1.5f + 80.0f);
				Coord3D site = r.spot(x, 1200.0f, clear);
				x = x > 3600.0f ? 1200.0f : x + 350.0f;
				Object *builder = r.make(bn, Coord3D{ site.x - 150.0f, site.y, 0.0f });
				r.frames(3);
				if (!r.select(builder) && !(r.frames(20), r.select(builder)))
				{
					p.add(tag + ": a click does not select the builder");
					r.logic().destroyObject(builder);
					continue;
				}
				const ControlBarButton *cb = r.find(b->m_name);
				if (!cb || cb->state != ButtonState::Enabled)
				{
					p.add(tag + ": the button is " + (cb ? "in state " + std::to_string((int)cb->state) : std::string("not shown")));
					r.logic().destroyObject(builder);
					continue;
				}
				const ObjectID before = lastId(r);
				const unsigned money = r.money();
				const int cost = BuildAssistant::calcCostToBuild(*what, r.local(), builder, -1);
				REQUIRE(r.bar().pressButton(cb->slot, cb->inPalantir));
				r.frames(1);
				if (!r.hud->input().ui().isPlacing())
				{
					p.add(tag + ": the press does not start the placement");
					r.logic().destroyObject(builder);
					continue;
				}
				const bool line = BuildAssistant::isLineBuildTemplate(r.logic(), what, builder);
				r.clickWorld(site);
				// an illegal site (CantBuildThere, uneven or blocked ground) keeps the placement: the player clicks elsewhere
				for (int k = 1; k < 8 && !line && r.hud->input().ui().isPlacing(); ++k)
				{
					site = r.spot(1200.0f + 330.0f * (float)k, 1200.0f + (k % 2 ? 900.0f : 0.0f), clear);
					r.clickWorld(site);
				}
				if (line)
				{
					// the wall: the first click anchors it, the pointer goes 300 along, the second click lays the span (RW 0x83E911)
					Coord3D end{ site.x, site.y + 300.0f, 0.0f };
					end.z = r.logic().getGroundHeight(end.x, end.y);
					r.rig.lookAt(end);
					r.frames(1);
					const ICoord2D px = r.rig.screenOf(end);
					r.hud->mouseMove(px.x, px.y, 0);
					r.frames(1);
					r.clickWorld(end);
				}
				r.frames(3);
				Object *made = nullptr;
				waitFor(r, 20, [&]() { return (made = newOwn(r, what, before)) != nullptr; });
				if (!made)
				{
					std::string sent;
					for (const std::string &l : r.hud->input().messageLog())
					{
						sent = l.find("CONSTRUCT") != std::string::npos ? l : sent;
					}
					std::string why;
					for (const std::string &w : r.rig.game->buildCommands().refusals())
					{
						why = w;
					}
					std::string made2;
					for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
					{
						made2 += o->getID() > before ? " " + o->getTemplate()->getName() : std::string();
					}
					p.add(tag + ": nothing was placed (last construct message: " + sent + "; the logic's last refusal: " + why + "; new objects:" + made2 + "; the dozer stats "
						+ std::to_string(r.rig.game->buildCommands().stats().dozerBuilds) + " refused " + std::to_string(r.rig.game->buildCommands().stats().refused) + " rejected " + std::to_string(r.rig.game->buildCommands().stats().rejected) + ", unhandled " + std::to_string(r.unhandled()[MSG_DOZER_CONSTRUCT]) + ", sent "
						+ std::to_string(r.hud->input().sentMessages()) + ")");
					r.hud->input().ui().setGUICommand(nullptr);
					r.logic().destroyObject(builder);
					continue;
				}
				if (!line && ((int)(money - r.money()) < cost - 5 || (int)(money - r.money()) > cost + 5))
				{
					p.add(tag + ": charged " + std::to_string((int)(money - r.money())) + ", the cost is " + std::to_string(cost));
				}
				const int guard = 5 * (int)BuildAssistant::buildTime(*what) + 900;
				const ObjectID mid = made->getID();
				static const int phantom = ObjectTemplateInfoBuilder::objectStatusIndex("PHANTOM_STRUCTURE");
				if (!waitFor(r, guard, [&]() { return !r.logic().findObjectByID(mid) || (!made->isUnderConstruction() && !made->testStatus((unsigned)phantom)); }) || !r.logic().findObjectByID(mid))
				{
					p.add(tag + ": not completed after " + std::to_string(guard) + " frames");
				}
				else
				{
					++built;
				}
				for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
				{
					if (o->getID() > before && o->getControllingPlayer() == r.local())
					{
						r.logic().destroyObject(o);
					}
				}
				r.logic().destroyObject(builder);
				r.frames(3);
			}
		}
		std::printf("facta builders %s: %zu builders, %zu constructions completed\n", f.c_str(), builders.size(), built);
		CHECK(built > 5);
		p.print("builders");
		problems += p.list.size();
	}
	CHECK(problems == 0);
}

namespace
{
// the shown REVIVE button of the citadel's hero pages whose record is the template (dead: a revive record), the pages pushed in turn; null none
const ControlBarButton *findRevive(FactRig &r, const std::string &tmpl, bool dead)
{
	auto look = [&]() -> const ControlBarButton * {
		for (const auto *list : { &r.bar().palantirButtons(), &r.bar().sideButtons(), &r.bar().offBarButtons() })
		{
			for (const ControlBarButton &b : *list)
			{
				const HeroRecord *rec = b.button && b.button->m_command == GUI_COMMAND_REVIVE && b.reviveIndex >= 0 ? r.local()->heroes().at(b.reviveIndex) : nullptr;
				if (rec && rec->templateName == tmpl && rec->dead == dead)
				{
					return &b;
				}
			}
		}
		return nullptr;
	};
	r.resetRange();
	if (const ControlBarButton *b = look())
	{
		return b;
	}
	std::vector<std::pair<int, bool>> pushes;
	for (const ControlBarButton &b : shownWith(r, GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE))
	{
		pushes.push_back({ b.slot, b.inPalantir });
	}
	for (const auto &p : pushes)
	{
		r.resetRange();
		r.bar().pressButton(p.first, p.second);
		r.frames(2);
		if (const ControlBarButton *b = look())
		{
			return b;
		}
	}
	r.resetRange();
	return nullptr;
}

Object *liveHero(FactRig &r, const std::string &tmpl)
{
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == tmpl && o->getControllingPlayer() == r.local() && !o->isEffectivelyDead())
		{
			return o;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("FACT-A heroes: every hero and the ring hero recruited from the fortress's REVIVE slots (paid, made), killed, and revived from its slot")
{
	if (!hudtest::haveWorld("facta heroes"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	size_t problems = 0;
	for (const std::string &f : factions())
	{
		FactRig r(s, f.c_str());
		r.local()->depositMoney(2000000);
		Problems p;
		Object *shell = nullptr;
		CastleBehavior *castle = unpackCastle(r, shell);
		Object *keep = r.logic().findObjectByID(castle->keepId());
		REQUIRE(keep);
		const PlayerTemplate *pt = r.local()->getPlayerTemplate();
		// RW 0x9428CB: the ring slot requires both the player's Ring upgrade and the receiving fortress's OBJECT upgrade, as when the Ring arrives.
		if (const CommandButton *ring = TheCommandStore->findCommandButton("Command_RingHeroReviveSlot"))
		{
			for (const std::string &n : ring->m_neededUpgrade)
			{
				const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(n);
				if (u && u->getUpgradeType() == UPGRADE_TYPE_PLAYER && !r.local()->hasUpgradeComplete(u))
				{
					r.local()->addCompletedUpgrade(n);
				}
				else if (u && u->getUpgradeType() == UPGRADE_TYPE_OBJECT && !keep->hasUpgrade(u))
				{
					keep->giveUpgrade(u);
				}
			}
		}
		r.frames(5);
		// the Create-a-Hero slot needs a record of the Create-a-Hero system (lanes CAH-1 / CAH-2): not this lane's
		std::vector<std::string> heroes;
		for (const std::string &h : pt->m_buildableHeroesMP)
		{
			if (h != "CreateAHero")
			{
				heroes.push_back(h);
			}
		}
		const size_t regular = heroes.size();
		heroes.insert(heroes.end(), pt->m_buildableRingHeroesMP.begin(), pt->m_buildableRingHeroesMP.end());
		size_t recruited = 0, revived = 0, ringsDropped = 0;
		for (size_t hi = 0; hi < heroes.size(); ++hi)
		{
			const std::string &h = heroes[hi];
			const bool ringHero = hi >= regular;
			const std::string tag = f + " " + h;
			for (int round = 0; round < 2; ++round)
			{
				const bool revive = round == 1;
				if (!r.select(keep))
				{
					p.add(tag + ": a click does not select the citadel");
					break;
				}
				const ControlBarButton *cb = findRevive(r, h, revive);
				if (!cb)
				{
					p.add(tag + ": no " + std::string(revive ? "revive" : "recruit") + " slot shows the hero");
					break;
				}
				if (cb->state != ButtonState::Enabled)
				{
					p.add(tag + ": the " + std::string(revive ? "revive" : "recruit") + " slot is in state " + std::to_string((int)cb->state));
					break;
				}
				const int cost = cb->cost;
				// the money also moves by the player's income: two idle frames measure it first
				const unsigned m0 = r.money();
				r.frames(2);
				const int income = (int)r.money() - (int)m0;
				cb = findRevive(r, h, revive);
				REQUIRE(cb);
				const unsigned money = r.money();
				REQUIRE(r.bar().pressButton(cb->slot, cb->inPalantir));
				r.frames(2);
				const int charged = (int)money - (int)r.money() + income;
				if (charged < cost - 30 || charged > cost + 30) // one income tick (25) may land between the measures
				{
					p.add(tag + ": the " + std::string(revive ? "revival" : "recruitment") + " charged " + std::to_string(charged) + ", the slot shows " + std::to_string(cost));
				}
				Object *hero = nullptr;
				if (!waitFor(r, 3000, [&]() { return (hero = liveHero(r, h)) != nullptr; }))
				{
					p.add(tag + ": the hero did not come out of the fortress");
					break;
				}
				revive ? ++revived : ++recruited;
				r.frames(30);
				if (ringHero)
				{
					// the ring hero takes the Ring: the player's Upgrade_RingHero goes (RemoveUpgradeUpgrade on creation), and at its death the Ring drops
					// (CreateObjectDie OCL_TheOneRing): no revive slot, the Ring must be brought back
					const UpgradeTemplate *ring = TheUpgradeCenter->findUpgrade("Upgrade_RingHero");
					if (ring && r.local()->hasUpgradeComplete(ring))
					{
						p.add(tag + ": the player keeps Upgrade_RingHero once the ring hero is made");
					}
					const ObjectID before = lastId(r);
					hero->kill(DAMAGE_UNRESISTABLE, 0);
					const ThingTemplate *dropped = r.logic().things().findTemplate("TheDroppedRing");
					if (!dropped || !waitFor(r, 200, [&]() { return newOwn(r, dropped, before) != nullptr || [&]() {
							for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
							{
								if (o->getID() > before && o->getTemplate() == dropped)
								{
									return true;
								}
							}
							return false;
						}(); }))
					{
						p.add(tag + ": the Ring does not drop at the ring hero's death");
					}
					else
					{
						++ringsDropped;
					}
					r.frames(30);
					break;
				}
				if (!revive)
				{
					// killed: its revive record comes (RW 0x780DD4)
					hero->kill(DAMAGE_UNRESISTABLE, 0);
					if (!waitFor(r, 200, [&]() { return liveHero(r, h) == nullptr; }))
					{
						p.add(tag + ": the hero does not die");
						break;
					}
					r.frames(30);
				}
				else
				{
					hero->kill(DAMAGE_UNRESISTABLE, 0); // out of the way of the next one
					r.frames(30);
				}
			}
		}
		std::printf("facta heroes %s: %zu heroes, %zu recruited, %zu revived, %zu Rings dropped\n", f.c_str(), heroes.size(), recruited, revived, ringsDropped);
		CHECK(recruited == heroes.size());
		CHECK(revived == regular);
		CHECK(ringsDropped == heroes.size() - regular);
		p.print("heroes");
		problems += p.list.size();
	}
	CHECK(problems == 0);
}

namespace
{
// a victim that outlives the hits measured on it (FACT-B e2d261aa): twenty times its health (ActiveBody::setMaxHealth change type 2 adds the
// difference), its horde's members too
void toughen(Object &o)
{
	for (Object *b : bodies(o))
	{
		if (ActiveBody *body = dynamic_cast<ActiveBody *>(b->getBodyModule()))
		{
			body->setMaxHealth(body->getMaxHealth() * 20.0f, 2);
		}
	}
}

// the hits an attacker lands on a victim, as the world shows them: ordered onto the victim (AIUpdateInterface::aiAttackObject, a player's command),
// ActiveBody's damage observer measures each source/victim/frame, combining one weapon's damage nuggets but not different soldiers' hits. A body's
// net health drop conflates simultaneous arrows and healing. Nothing else fights: both sides are toughened and hold their fire. Sample 150 frames,
// from 60 frames after the first hit (flanks and splash make several sizes).
struct Landed
{
	static constexpr int kWindow = 150;
	static constexpr int kSettle = 60; // the window opens 60 frames after the first hit: a cavalry charge's trample (its crush weapon) is not the sword
	std::vector<float> hits;
	float total = 0.0f; // within the window
	std::string why;    // few hits: where the two stood and what the attacker held
	// the health lost a hit: the rate of the shots (a volley's timing, the members' approach) is not the weapon's
	float perHit() const { return hits.empty() ? 0.0f : total / (float)hits.size(); }
	std::string str() const
	{
		char b[96];
		std::snprintf(b, sizeof b, "%.0f in %d frames, %.1f a hit (%zu hits)", (double)total, kWindow, (double)perHit(), hits.size());
		return b + (hits.size() < 5 ? " " + why : std::string());
	}
};
Landed landedHits(FactRig &r, ObjectID attackerId, ObjectID victimId, int guard = 500)
{
	Landed out;
	Object *a = r.logic().findObjectByID(attackerId);
	Object *v = r.logic().findObjectByID(victimId);
	if (!a || !v || !a->getAIUpdateInterface() || !a->getAIUpdateInterface()->aiAttackObject(v, CMD_FROM_PLAYER))
	{
		return out;
	}
	std::map<std::tuple<unsigned, ObjectID, ObjectID>, float> damage;
	std::set<ObjectID> attackers, victims;
	int first = -1;
	int sample = 0;
	struct RestoreTrace
	{
		CombatState &combat;
		CombatState::DamageTrace previous;
		~RestoreTrace() { combat.setDamageTrace(previous); }
	} restore{ r.logic().combat(), r.logic().combat().damageTrace() };
	r.logic().combat().setDamageTrace([&](const Object &victim, std::uint32_t source, float dealt, float taken) {
		if (taken > 0.0f && attackers.count(source) && victims.count(victim.getID()))
		{
			first = first < 0 ? sample : first;
			if (sample >= first + Landed::kSettle)
			{
				damage[{ r.logic().getFrame(), source, victim.getID() }] += taken;
			}
		}
		if (restore.previous)
		{
			restore.previous(victim, source, dealt, taken);
		}
	});
	for (int k = 0; k < guard && (first < 0 || k < first + Landed::kSettle + Landed::kWindow); ++k)
	{
		a = r.logic().findObjectByID(attackerId);
		v = r.logic().findObjectByID(victimId);
		if (!a || !v)
		{
			break;
		}
		if (k > 0 && ((k % 100 == 0 && first < 0) || (k % 20 == 0 && a->getAIUpdateInterface()->stateId() == 0)))
		{
			a->getAIUpdateInterface()->aiAttackObject(v, CMD_FROM_PLAYER); // a player clicks again when the order did not start a fight or lapsed
		}
		attackers.clear();
		victims.clear();
		for (Object *b : bodies(*a))
		{
			attackers.insert(b->getID());
		}
		for (Object *b : bodies(*v))
		{
			victims.insert(b->getID());
		}
		sample = k;
		r.frames(1);
	}
	for (const auto &hit : damage)
	{
		out.hits.push_back(hit.second);
		out.total += hit.second;
	}
	a = r.logic().findObjectByID(attackerId);
	v = r.logic().findObjectByID(victimId);
	if (out.hits.size() < 5 && a && v)
	{
		const Object *am = bodies(*a).back();
		const Weapon *w = am->getWeapons() ? am->getWeapons()->currentWeapon() : nullptr;
		std::string vs;
		for (const char *st : { "STEALTHED", "DETECTED", "MASKED", "UNSELECTABLE", "NO_ATTACK_FROM_AI", "SOLD", "DISGUISED" })
		{
			for (Object *b : bodies(*v))
			{
				if (b->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex(st)))
				{
					vs += std::string(" ") + st;
					break;
				}
			}
		}
		const ContainModuleInterface *ac = a->getContain();
		char cnt[160];
		std::snprintf(cnt, sizeof cnt, "first hit at %d; the attacker's bodies %zu, the victim's %zu; ", first, bodies(*a).size(), bodies(*v).size());
		(void)ac;
		out.why = std::string(cnt) + "victim status:" + vs + " ";
		char b[200];
		std::snprintf(b, sizeof b, "(%.0f apart, the attacker's weapon %s, its AI state %d)", std::hypot((double)(a->getPosition()->x - v->getPosition()->x), (double)(a->getPosition()->y - v->getPosition()->y)),
			w && w->getTemplate() ? w->getTemplate()->getName().c_str() : "none", a->getAIUpdateInterface() ? a->getAIUpdateInterface()->stateId() : -1);
		out.why += b;
	}
	return out;
}

// the hits a unit deals to an enemy battalion (MordorFighterHorde) beside it, and the hits it takes from enemy fighters (SLASH) and archers (PIERCE)
Object *scenePartner(FactRig &r, Object &unit, const char *name, float dx)
{
	// a free place beside the unit (after a fight its soldiers stand spread where the last victim stood)
	Object *e = r.make(name, r.spot(unit.getPosition()->x + dx, unit.getPosition()->y, 70.0f), r.enemy());
	r.frames(5);
	// facing the unit, its soldiers too (the hits come from the front)
	const float face = (float)std::atan2((double)(unit.getPosition()->y - e->getPosition()->y), (double)(unit.getPosition()->x - e->getPosition()->x));
	for (Object *b : bodies(*e))
	{
		b->setOrientation(face);
	}
	toughen(*e);
	holdFire(*e);
	return e;
}
Landed dealt(FactRig &r, Object &unit)
{
	Object *v = scenePartner(r, unit, "MordorFighterHorde", 120.0f);
	const ObjectID vid = v->getID();
	Landed l = landedHits(r, unit.getID(), vid);
	// the victim dies as in a game (its soldiers killed, the horde with them), then is removed
	if (Object *x = r.logic().findObjectByID(vid))
	{
		for (Object *b : bodies(*x))
		{
			b->kill(DAMAGE_UNRESISTABLE, 0);
		}
	}
	r.frames(60);
	if (Object *x = r.logic().findObjectByID(vid))
	{
		r.logic().destroyObject(x);
	}
	r.frames(2);
	return l;
}
std::vector<Landed> taken(FactRig &r, Object &unit)
{
	std::vector<Landed> out;
	for (const auto &a : { std::make_pair("MordorFighterHorde", 120.0f), std::make_pair("MordorArcherHorde", 260.0f) })
	{
		Object *e = scenePartner(r, unit, a.first, a.second);
		const ObjectID eid = e->getID();
		// a camouflaged battalion (the Lorien ones' InvisibilityUpdate, ALLOW_NEAR_TREES) that stopped near trees cannot be ordered upon (RW 0x6C9147): it is
		// revealed for the measure as a detector would (RW 0x81C32C), so the measure is the armour's and not where the battalion stopped (lane FLY-1: the Elves'
		// game moved once the ElvenFortressEagle the template walk makes for a frame ran GiantBirdAIUpdate, and a Lorien battalion stopped among trees)
		r.logic().invisibility().markDetected(&unit, e, 1000, 0);
		out.push_back(landedHits(r, eid, unit.getID()));
		if (Object *x = r.logic().findObjectByID(eid))
		{
			r.logic().destroyObject(x);
		}
		r.frames(2);
	}
	return out;
}

// the module classes of the object and its members whose TriggeredBy holds the upgrade
std::set<std::string> respondingModules(Object &o, const UpgradeTemplate &u)
{
	std::set<std::string> out;
	for (Object *b : bodies(o))
	{
		for (const auto &m : b->modules())
		{
			if (m->isUnported())
			{
				// an unported upgrade class: its raw TriggeredBy is not read here; named with a mark
				continue;
			}
			UpgradeMux *mux = m->getUpgrade();
			if (mux && mux->muxData() && mux->muxData()->m_activationMask.test((unsigned)u.getMaskBit()))
			{
				out.insert(m->getModuleClassName());
			}
		}
	}
	return out;
}
} // namespace

TEST_CASE("FACT-A unit upgrades: every upgrade on a battalion's or unit's bar bought after its forge research; measured in real hits, the weapon hits harder, the armour takes smaller hits, Basic Training raises the rank")
{
	if (!hudtest::haveWorld("facta unit upgrades"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	size_t problems = 0;
	for (const std::string &f : factions())
	{
		FactRig r(s, f.c_str());
		r.local()->depositMoney(2000000);
		Problems p;
		size_t bought = 0, weapons = 0, armours = 0, levels = 0;
		r.logic().combat().setAutoAcquireEnabled(false); // nothing attacks unless ordered: the hits measured are the ordered ones (FACT-B e2d261aa)
		// a player who buys a battalion's upgrade owns the building its RequiredObjectFilter names (the forge): one of each of the faction's buildings
		// (not the castle) stands on the map's far side
		std::vector<ObjectID> placed;
		std::vector<std::string> placedNames;
		{
			float bx = 600.0f;
			for (const std::string &name : reachableTemplates(r))
			{
				Object *probe = r.make(name, r.spot(bx, 2100.0f, 150.0f));
				r.frames(1);
				const bool keepIt = kindOf(*probe, "STRUCTURE") && !probe->findModule("CastleBehavior") && !kindOf(*probe, "WALL_SEGMENT");
				if (!keepIt)
				{
					r.logic().destroyObject(probe);
				}
				else
				{
					bx = bx > 4800.0f ? 600.0f : bx + 260.0f;
					placed.push_back(probe->getID());
					placedNames.push_back(name);
					if (BodyModuleInterface *body = probe->getBodyModule())
					{
						body->setIndestructible(true); // the map's creeps (cave trolls, drakes) would wreck them during the long waits
					}
				}
			}
			r.frames(10);
			std::string list;
			for (const std::string &n : placedNames)
			{
				list += " " + n;
			}
			std::printf("facta unit upgrades %s: placed%s\n", f.c_str(), list.c_str());
			for (size_t i = 0; i < placed.size(); ++i)
			{
				if (!r.logic().findObjectByID(placed[i]))
				{
					std::printf("facta unit upgrades %s: the building %s placed for the upgrades is gone\n", f.c_str(), placedNames[i].c_str());
				}
			}
		}
		for (const std::string &name : reachableTemplates(r))
		{
			const ThingTemplate *tt = r.logic().things().findTemplate(name);
			const CommandSet *set = TheCommandStore->findCommandSet(templateCommandSet(*tt));
			std::vector<const CommandButton *> ups;
			for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (b && b->m_command == GUI_COMMAND_OBJECT_UPGRADE)
				{
					ups.push_back(b);
				}
			}
			if (ups.empty())
			{
				continue;
			}
			Object *probe = r.make(name, r.spot(1500.0f, 1000.0f, 120.0f));
			const bool structure = kindOf(*probe, "STRUCTURE");
			r.logic().destroyObject(probe);
			r.frames(2);
			if (structure)
			{
				continue; // the buildings' upgrades are the production test's
			}
			for (const CommandButton *b : ups)
			{
				for (size_t i = 0; i < placed.size(); ++i)
				{
					if (placed[i] != INVALID_ID && !r.logic().findObjectByID(placed[i]))
					{
						std::printf("facta unit upgrades %s: %s is gone before %s %s\n", f.c_str(), placedNames[i].c_str(), name.c_str(), b->m_name.c_str());
						placed[i] = INVALID_ID;
					}
				}
				const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(b->m_upgradeName);
				const std::string tag = f + " " + name + " " + b->m_name;
				if (!u)
				{
					p.add(tag + ": no upgrade " + b->m_upgradeName);
					continue;
				}
				// the research a player did at the forge (the production test buys it): the button's NeededUpgrade
				for (const std::string &n : b->m_neededUpgrade)
				{
					const UpgradeTemplate *nu = TheUpgradeCenter->findUpgrade(n);
					if (nu && nu->getUpgradeType() == UPGRADE_TYPE_PLAYER && !r.local()->hasUpgradeComplete(nu))
					{
						r.local()->addCompletedUpgrade(n);
					}
				}
				Object *o = r.make(name, r.spot(2000.0f, 1300.0f, 300.0f));
				r.frames(20);
				const std::set<std::string> responders = respondingModules(*o, *u);
				// the unit's real hits before the purchase: what it deals to an enemy battalion and takes from enemy fighters and archers
				toughen(*o);
				holdFire(*o);
				const bool weaponUp = responders.count("WeaponSetUpgrade") > 0, armourUp = responders.count("ArmorUpgrade") > 0;
				// the hits before the purchase are a twin's (the same template, fresh): the unit that buys fights once. Ordered onto a second victim right
				// after a fight, GondorKnightHorde and DwarvenGuardianHorde stay in AI_ATTACK_OBJECT and land nothing (the next order after that works)
				Landed dealsBefore;
				if (weaponUp)
				{
					Object *twin = r.make(name, r.spot(o->getPosition()->x, o->getPosition()->y + 400.0f, 150.0f));
					r.frames(20);
					toughen(*twin);
					holdFire(*twin);
					dealsBefore = dealt(r, *twin);
					if (Object *x = r.logic().findObjectByID(twin->getID()))
					{
						r.logic().destroyObject(x);
					}
					r.frames(2);
				}
				const std::vector<Landed> takesBefore = armourUp ? taken(r, *o) : std::vector<Landed>{};
				const LevelUpUpgradeModuleData *levelUp = nullptr;
				for (const auto &m : o->modules())
				{
					const LevelUpUpgradeModuleData *d = dynamic_cast<const LevelUpUpgradeModuleData *>(m->getModuleData());
					UpgradeMux *mux = m->getUpgrade();
					if (d && mux && mux->muxData() && mux->muxData()->m_activationMask.test((unsigned)u->getMaskBit()))
					{
						levelUp = d;
					}
				}
				const int rankBefore = o->getExperienceTracker() ? o->getExperienceTracker()->getRank() : -1;
				if (!r.select(o))
				{
					p.add(tag + ": a click does not select the unit");
					r.logic().destroyObject(o);
					continue;
				}
				const ControlBarButton *cb = r.find(b->m_name);
				if (!cb || cb->state != ButtonState::Enabled)
				{
					const ProductionUpdateInterface *pu = o->getProductionUpdate();
					p.add(tag + ": the button is " + (cb ? "in state " + std::to_string((int)cb->state) : std::string("not shown")) + " after the research (affected "
						+ std::to_string((int)o->affectedByUpgrade(u)) + ", has " + std::to_string((int)o->hasUpgrade(u)) + ", queued "
						+ std::to_string(pu ? (int)pu->isUpgradeInQueue(u) : -1) + ", queue " + std::to_string(pu ? pu->getProductionCount() : -1) + ", own structures "
						+ [&]() { int n = 0; std::string fs; for (Object *x = r.logic().getFirstObject(); x; x = x->getNextObject()) { if (x->getControllingPlayer() == r.local() && kindOf(*x, "STRUCTURE")) { ++n; fs += " " + x->getTemplate()->getName() + (x->isUnderConstruction() ? "(uc)" : "") + (x->isEffectivelyDead() ? "(dead)" : ""); } } return std::to_string(n) + fs; }() + ")");
					r.logic().destroyObject(o);
					r.frames(2);
					continue;
				}
				const unsigned money = r.money();
				REQUIRE(r.bar().pressButton(cb->slot, cb->inPalantir));
				r.frames(1);
				const int charged = (int)(money - r.money());
				const int guard = (int)(5.0f * u->getBuildTimeSeconds()) + 300;
				if (!waitFor(r, guard, [&]() { return o->hasUpgrade(u); }))
				{
					p.add(tag + ": not complete after " + std::to_string(guard) + " frames (charged " + std::to_string(charged) + ", modules answering it:"
						+ [&]() { std::string x; for (const std::string &m : responders) x += " " + m; return x; }() + ")");
					r.logic().destroyObject(o);
					r.frames(2);
					continue;
				}
				r.frames(5);
				++bought;
				if (charged < u->getBuildCost() - 30 || charged > u->getBuildCost() + 30)
				{
					p.add(tag + ": charged " + std::to_string(charged) + ", BuildCost " + std::to_string(u->getBuildCost()));
				}
				std::string what;
				for (const std::string &m : responders)
				{
					what += " " + m;
				}
				std::string measured;
				if (weaponUp)
				{
					// a WeaponSetUpgrade: the unit's hits on the same enemy battalion are bigger (a weapon that changes its name and not its hit fails)
					++weapons;
					const Landed after = dealt(r, *o);
					measured += " deals " + dealsBefore.str() + " -> " + after.str() + ";";
					if (dealsBefore.hits.size() < 5 || after.hits.size() < 5 || !(after.perHit() > dealsBefore.perHit() * 1.03f))
					{
						p.add(tag + ": the weapon upgrade takes no more health a hit from an enemy battalion (" + dealsBefore.str() + " -> " + after.str() + ")");
					}
				}
				if (armourUp)
				{
					// an ArmorUpgrade: the hits of enemy fighters or archers on the unit are smaller
					++armours;
					const std::vector<Landed> after = taken(r, *o);
					bool less = false, measuredAll = true;
					for (size_t k = 0; k < takesBefore.size() && k < after.size(); ++k)
					{
						measuredAll = measuredAll && takesBefore[k].hits.size() >= 5 && after[k].hits.size() >= 5;
						less = less || (takesBefore[k].hits.size() >= 5 && after[k].hits.size() >= 5 && after[k].perHit() < takesBefore[k].perHit() * 0.97f);
						measured += std::string(k == 0 ? " takes from fighters " : " from archers ") + takesBefore[k].str() + " -> " + after[k].str() + ";";
					}
					if (!less || !measuredAll)
					{
						p.add(tag + ": the armour upgrade loses no less health a hit to enemy fighters or archers (" + measured + ")");
					}
				}
				if (levelUp)
				{
					++levels;
					// Basic Training (LevelUpUpgrade, RW 0x8B7F90): the battalion gains min(LevelsToGain, LevelCap - rank) levels
					const int rankAfter = o->getExperienceTracker() ? o->getExperienceTracker()->getRank() : -1;
					const int expect = std::max(rankBefore, std::min(rankBefore + levelUp->m_levelsToGain, levelUp->m_levelCap));
					measured += " rank " + std::to_string(rankBefore) + " -> " + std::to_string(rankAfter) + " (LevelsToGain " + std::to_string(levelUp->m_levelsToGain)
						+ ", LevelCap " + std::to_string(levelUp->m_levelCap) + ");";
					if (rankAfter != expect || rankAfter <= rankBefore)
					{
						p.add(tag + ": Basic Training: the battalion's rank " + std::to_string(rankBefore) + " -> " + std::to_string(rankAfter) + ", expected "
							+ std::to_string(expect));
					}
				}
				if (responders.empty())
				{
					p.add(tag + ": no ported module of the unit answers the upgrade (unported:" + [&]() { std::string x; for (const std::string &m : unportedModules(*o)) x += " " + m; return x; }() + ")");
				}
				std::printf("facta unit upgrade %s: %s ->%s modules%s\n", tag.c_str(), b->m_upgradeName.c_str(), measured.c_str(), what.c_str());
				r.logic().destroyObject(o);
				r.frames(3);
			}
		}
		std::printf("facta unit upgrades %s: %zu bought, %zu weapon, %zu armour, %zu level\n", f.c_str(), bought, weapons, armours, levels);
		CHECK(bought > 3);
		p.print("unit upgrade");
		problems += p.list.size();
	}
	CHECK(problems == 0);
}

TEST_CASE("FACT-A context commands: a battalion captures a neutral flag's building, a porter puts out a burning building (the click path is stop S-286; the logic through the special power message)")
{
	if (!hudtest::haveWorld("facta context"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	static const int aflame = ObjectTemplateInfoBuilder::objectStatusIndex("AFLAME");
	// RotWK's context click (CommandTranslator::evaluateContextCommand RW 0x81E60B: ActionManager type 0xC capture RW 0x82D0A1, 0x11 fire fighting RW 0x82DDE1, 0x12 the
	// porters' GiveUpgrade RW 0x82D780; the matching SPECIAL_POWER button of the first selected object, issued by RW 0x81E147) is not ported: stop S-286
	bool s286 = false;
	for (const std::string &line : HudInput::acceptanceStops())
	{
		s286 = s286 || line.rfind("[S-286]", 0) == 0;
	}
	REQUIRE(s286);
	// what RW 0x81E147 sends for the button: MSG_DO_SPECIAL_POWER_AT_OBJECT { power id, target, options, source }
	auto issue = [](FactRig &r, Object &source, Object &target, const char *power) {
		const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(power);
		REQUIRE(t);
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_OBJECT, r.local()->getPlayerIndex());
		m.appendIntegerArgument((int)t->getID());
		m.appendObjectIDArgument(target.getID());
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(source.getID());
		r.rig.game->commands().append(m);
	};
	for (const std::string &f : factions())
	{
		FactRig r(s, f.c_str());
		r.local()->depositMoney(2000000);
		// 1. the capture: the first battalion with the CaptureBuilding button
		std::string capturer;
		for (const std::string &n : reachableTemplates(r))
		{
			const ThingTemplate *tt = r.logic().things().findTemplate(n);
			const CommandSet *set = TheCommandStore->findCommandSet(templateCommandSet(*tt));
			for (int slot = 0; capturer.empty() && set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (b && b->m_specialPowerName == "SpecialAbilityCaptureBuilding" && n.find("Horde") != std::string::npos)
				{
					capturer = n;
				}
			}
		}
		REQUIRE_MESSAGE(!capturer.empty(), f);
		const Coord3D at = r.spot(2000.0f, 1300.0f, 400.0f);
		Player *neutral = r.rig.game->players().getNeutralPlayer();
		Object *outpost = r.make("Outpost", Coord3D{ at.x + 300.0f, at.y, 0.0f }, neutral);
		Object *flag = r.make("CaptureFlag", Coord3D{ at.x + 180.0f, at.y, 0.0f }, neutral);
		Object *horde = r.make(capturer, at);
		r.frames(20);
		REQUIRE(r.select(horde));
		r.clickWorld(FactRig::clickPoint(*flag));
		r.frames(30);
		CHECK_MESSAGE(outpost->getControllingPlayer() != r.local(), "the click captured: S-286 can shrink");
		// lane HEROCAST-1: the message only when the click started no capture. A second cast while the ability runs is refused (not ready), but the group
		// first gives the horde's AI the player's command source (AI + 0x48, RW 0x770B3D), and a player command ends a running ability (RW 0x854F0B)
		SpecialPowerUpdateInterface *capture = SpecialAbilityModules::findUpdate(*horde, TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityCaptureBuilding"));
		if (!capture || !capture->isActive())
		{
			issue(r, *horde, *flag, "SpecialAbilityCaptureBuilding");
		}
		const bool captured = waitFor(r, 900, [&]() { return outpost->getControllingPlayer() == r.local(); });
		CHECK_MESSAGE(captured, f << " " << capturer << ": the outpost is still " << (outpost->getControllingPlayer() ? outpost->getControllingPlayer()->getPlayerName() : std::string("nobody's")));
		std::printf("facta context %s: %s captured the Outpost through the special power message: %d\n", f.c_str(), capturer.c_str(), (int)captured);
		for (Object *o : { outpost, flag, horde })
		{
			if (r.logic().findObjectByID(o->getID()))
			{
				r.logic().destroyObject(o);
			}
		}
		r.frames(5);
		// 2. the fire fighting: the first own building with a FlammableUpdate set aflame (FlammableUpdate::tryToIgnite, RW 0x8901AB), the porter's power on it
		std::string porter;
		for (const std::string &n : reachableTemplates(r))
		{
			const ThingTemplate *tt = r.logic().things().findTemplate(n);
			const CommandSet *set = TheCommandStore->findCommandSet(templateCommandSet(*tt));
			for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (b && b->m_specialPowerName == "SpecialAbilityPorterExtinguishFire" && porter.empty())
				{
					porter = n;
				}
			}
		}
		REQUIRE_MESSAGE(!porter.empty(), f);
		Object *building = nullptr;
		FlammableUpdate *fl = nullptr;
		const Coord3D site = r.spot(2600.0f, 1300.0f, 350.0f);
		for (const std::string &n : reachableTemplates(r))
		{
			if (fl)
			{
				break;
			}
			Object *o = r.make(n, site);
			r.frames(1);
			FlammableUpdate *candidate = kindOf(*o, "STRUCTURE") ? dynamic_cast<FlammableUpdate *>(o->findModule("FlammableUpdate")) : nullptr;
			if (candidate && candidate->wouldIgnite())
			{
				building = o;
				fl = candidate;
			}
			else
			{
				r.logic().destroyObject(o);
				r.frames(1);
			}
		}
		if (!fl)
		{
			std::printf("facta context %s: no building of the faction has a FlammableUpdate (nothing to put out)\n", f.c_str());
			continue;
		}
		Object *pt = r.make(porter, Coord3D{ site.x - 160.0f, site.y, 0.0f });
		r.frames(5);
		fl->tryToIgnite();
		r.frames(2);
		REQUIRE(building->testStatus((unsigned)aflame));
		REQUIRE(r.select(pt));
		issue(r, *pt, *building, "SpecialAbilityPorterExtinguishFire");
		const bool out = waitFor(r, 900, [&]() { return !building->testStatus((unsigned)aflame); });
		// the extinguisher (PorterFireExtinguisher) is a FireLogicNugget (DECREASE_BURN_RATE): RotWK's fire system is stop S-321, the fire burns on
		bool s321 = false;
		for (const std::string &line : r.logic().report().stops)
		{
			s321 = s321 || (line.rfind("[S-321]", 0) == 0 && line.find("FireLogic") != std::string::npos);
		}
		CHECK_MESSAGE((out || s321), f << " " << porter << ": " << building->getTemplate()->getName() << " still burns and S-321 does not name FireLogic");
		CHECK_MESSAGE(!out, f << ": the porter put the fire out: S-321's FireLogic can shrink");
		std::printf("facta context %s: %s's extinguisher on %s: fire out %d (FireLogicNugget, stop S-321)\n", f.c_str(), porter.c_str(), building->getTemplate()->getName().c_str(), (int)out);
	}
}

namespace
{
// what each spell book power must do on the scene (Effects letters; a summon is required by its templates' names, not by S): the heal H, Rallying Call's buff B, the quakes' and the
// sun's damage D, the mist's debuff X; and the spell book's own letters (spellSample):
//   R  Rebuild (PlayerHealSpecialPower, HealAffects STRUCTURE, 4000 within 300): the scene's wounded economy building of the player regained health;
//   W  Cloud Break (CloudBreakSpecialPower RW 0x8C8CE4): the map's weather became SUNNY (ChangeWeather) and a SunbeamObject (CloudBreakSunbeam) stands;
//   E  Dwarven Riches (SpecialPowerModule AttributeModifier): the scene's economy building carries the SpellBookDwarvenRiches list (its PRODUCTION 300 %
//      income is stop S-253: no PRODUCTION modifier reaches the economy);
//   P  Arrow Volley, Bombard: a hit on the enemy battalion (toughened, nothing auto-acquiring) by an object the power made, of the size of that object's
//      weapons against the victim's armour (FireWeaponUpdate's and its slots' weapons; FACT-B e2d261aa's window: at least 0.9 x the biggest DamageNugget,
//      at most 2.5 x all of them);
//   Flood: its SpellBookFlood object is made and its wave damages (FloodUpdate is SPELL-3's, merged; stop S-1987 closed)
// the hit window of a weapon against a victim: its biggest DamageNugget and all of them at once (a projectile's warhead included), x the victim's armour
// coefficient for the nugget's damage type x the DamageScalar entries whose filter takes the victim (FACT-B e2d261aa's expectedHits)
void hitWindow(GameLogic &logic, const WeaponTemplate *w, const Object &victim, const Player *attacker, float &biggest, float &sum, int depth = 0)
{
	if (!w || depth > 3)
	{
		return;
	}
	const ActiveBody *body = dynamic_cast<const ActiveBody *>(victim.getBodyModule());
	const ArmorTemplate *armor = body ? body->currentArmor() : nullptr;
	for (const std::shared_ptr<WeaponNugget> &n : w->m_nuggets)
	{
		if (const DamageNugget *d = dynamic_cast<const DamageNugget *>(n.get()))
		{
			float scalar = 1.0f;
			for (const DamageScalarEntry &e : d->m_damageScalar)
			{
				scalar *= ObjectFilterMatch::allows(logic, e.filter, victim, attacker) ? e.scalar : 1.0f;
			}
			const float hit = d->m_damage * (armor ? armor->getCoefficient(d->m_damageType) : 1.0f) * scalar;
			biggest = std::max(biggest, hit);
			sum += hit;
		}
		else if (const ProjectileNugget *pn = dynamic_cast<const ProjectileNugget *>(n.get()))
		{
			hitWindow(logic, TheWeaponStore ? TheWeaponStore->findWeaponTemplate(pn->m_warheadTemplateName) : nullptr, victim, attacker, biggest, sum, depth + 1);
		}
	}
}

// what the spell book's own letters need, frame by frame after the cast (see spellExpected)
struct SpellWatch
{
	ObjectID highest = INVALID_ID;                               // the newest object before the cast
	std::map<ObjectID, std::vector<const WeaponTemplate *>> made; // the player's objects made after it, with their weapons
	std::map<ObjectID, float> health;                            // the enemy's bodies
	ObjectID econ = INVALID_ID;
	float econHealth = 0.0f;
	std::string hit;   // the first power hit in the window
	std::string near;  // the closest miss (a hit outside the window)
	void begin(GameLogic &logic, ObjectID econId)
	{
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			highest = std::max(highest == INVALID_ID ? (ObjectID)0 : highest, o->getID());
		}
		econ = econId;
		Object *e = logic.findObjectByID(econ);
		econHealth = e && e->getBodyModule() ? e->getBodyModule()->getHealth() : 0.0f;
	}
	void sample(GameLogic &logic, Player *me, Object *enemy, Effects &fx)
	{
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getID() > highest && o->getControllingPlayer() == me && !made.count(o->getID()))
			{
				std::vector<const WeaponTemplate *> ws;
				for (const auto &m : o->modules())
				{
					if (const FireWeaponUpdateModuleData *d = dynamic_cast<const FireWeaponUpdateModuleData *>(m->getModuleData()))
					{
						for (const FireWeaponNuggetData &n : d->m_nuggets)
						{
							ws.push_back(TheWeaponStore ? TheWeaponStore->findWeaponTemplate(n.weaponName) : nullptr);
						}
					}
				}
				for (int slot = 0; o->getWeapons() && slot < 3; ++slot)
				{
					if (const Weapon *w = o->getWeapons()->weaponInSlot(slot))
					{
						ws.push_back(w->getTemplate());
					}
				}
				made[o->getID()] = ws;
			}
		}
		// the enemy's bodies, and those seen before that still exist: a battalion whose members all fall in one frame (boulders landing together) is
		// gone the next, its dying members are not (lane PATHPERF-1: the killing hit was not measured)
		std::vector<Object *> watched;
		if (enemy)
		{
			watched = bodies(*enemy);
		}
		for (const auto &kv : health)
		{
			Object *x = logic.findObjectByID(kv.first);
			if (x && std::find(watched.begin(), watched.end(), x) == watched.end())
			{
				watched.push_back(x);
			}
		}
		for (Object *b : watched)
		{
			const ActiveBody *body = dynamic_cast<const ActiveBody *>(b->getBodyModule());
			if (!body)
			{
				continue;
			}
			auto it = health.find(b->getID());
			auto by = made.find(body->lastDamager());
			if (it != health.end() && body->getHealth() < it->second && body->lastDamageFrame() == logic.getFrame() && by != made.end())
			{
				const float amount = it->second - body->getHealth();
				std::string windows;
				bool in = false;
				for (const WeaponTemplate *w : by->second)
				{
					float biggest = 0.0f, sum = 0.0f;
					hitWindow(logic, w, *b, me, biggest, sum);
					char t[160];
					std::snprintf(t, sizeof t, " %s %.0f .. %.0f", w ? w->getName().c_str() : "?", (double)(biggest * 0.9f), (double)(sum * 2.5f));
					windows += t;
					// a killing hit is the health that was left (a lower bound of the damage: boulders landing together): no upper bound
					const bool lethal = body->getHealth() <= 0.0f;
					if (biggest > 0.0f && amount >= biggest * 0.9f && (amount <= sum * 2.5f || lethal) && hit.empty())
					{
						in = true;
						std::snprintf(t, sizeof t, "%.0f by #%u (%s: %.0f .. %.0f)", (double)amount, (unsigned)body->lastDamager(), w ? w->getName().c_str() : "?",
							(double)(biggest * 0.9f), (double)(sum * 2.5f));
						hit = t;
					}
					in = in || (biggest > 0.0f && amount >= biggest * 0.9f && (amount <= sum * 2.5f || lethal));
				}
				if (in)
				{
					fx.seen.insert('P');
				}
				else if (near.size() < 300)
				{
					char t[64];
					std::snprintf(t, sizeof t, "%.0f by #%u", (double)amount, (unsigned)body->lastDamager());
					near += std::string(near.empty() ? "" : "; ") + t + " (windows" + windows + ")";
				}
			}
			health[b->getID()] = body->getHealth();
		}
		if (Object *e = logic.findObjectByID(econ))
		{
			if (e->getBodyModule() && e->getBodyModule()->getHealth() > econHealth + 1.0f)
			{
				fx.seen.insert('R');
			}
			if (modifiers(*e).count("SpellBookDwarvenRiches"))
			{
				fx.seen.insert('E');
			}
		}
		bool sunbeam = false;
		for (Object *o = logic.getFirstObject(); o && !sunbeam; o = o->getNextObject())
		{
			sunbeam = o->getID() > highest && o->getTemplate()->getName() == "CloudBreakSunbeam";
		}
		if (sunbeam && logic.weather().weather() == 4) // SUNNY (RW 0xDB0074: NONE, CLOUDY, RAINY, CLOUDYRAINY, SUNNY)
		{
			fx.seen.insert('W');
		}
	}
};

// each power: its Effects / spell-book letters and the templates it must make (all of them, each seen as a new object after the cast; named from the
// power's module in system.ini: its OCL's CreateObject ObjectNames, and for an egg the OCL the egg hatches, so a ping or an egg alone is no summon).
// "A|B": one CreateObject with several ObjectNames, which picks one of them at random for each object it makes (RW 0x5F11CD,
// ObjectCreationList.cpp): any of them is the summon (lane PATHPERF-1: OCL_SpawnEnts' three ents can all be birches)
struct SpellExpect
{
	std::string letters;
	std::vector<std::string> made;
	bool stopped = false; // a registered stop: cast and asserted unported, not effect-verified
};
const std::map<std::string, SpellExpect> &spellExpected()
{
	static const std::map<std::string, SpellExpect> e = {
		{ "SpellBookHeal", { "H", {} } },
		{ "SpellBookRallyingCall", { "B", {} } },
		{ "SpellBookRebuild", { "R", {} } },
		{ "SpellBookTomBombadil", { "", { "TomBombadil_Summoned" } } }, // TomBombadilSummonEgg -> OCL_SpawnTomBombadil
		{ "SpellBookHobbitAllies", { "", { "RohanHobbitFriendsHorde_Summoned", "RohanSam_Summoned", "RohanFrodo_Summoned", "RohanMerry_Summoned", "RohanPippen_Summoned" } } },
		{ "SpellBookSpawnLoneTower", { "", { "GondorSentryTower_Independant" } } },
		{ "SpellBookSpawnLoneTowerDwarf", { "", { "DwarvenSentryTower_Independent" } } },
		{ "SpellBookArrowVolleyGood", { "P", { "SpellBookArrowVolley" } } },
		{ "SpellBookCloudBreak", { "W", { "CloudBreakSunbeam" } } },
		{ "SpellBookRohanAllies", { "", { "RohanRohirrimHorde_Summoned" } } },        // SummonRohanRohirrimEgg -> OCL_SpawnRohanRohirrim
		{ "SpellBookDunedainAllies", { "", { "GondorRangerHorde_Summoned" } } },      // DunedainAlliesSummonEgg -> OCL_SpawnDunedainRangers
		{ "SpellBookArmyoftheDead", { "", { "RohanOathbreakerHordeSmall" } } },       // GondorArmyofTheDeadSmallEggs -> SUPERWEAPON_SpawnOathbreakers
		{ "SpellBookEarthquake", { "D", { "SpellBookEarthquake" } } },
		{ "SpellBookFarsight", { "", { "FarSeeingPing" } } },                         // OCL_SpecialPowerFarSeeing: the revealing object
		{ "SpellBookEnshroudingMist", { "X", { "EnshroudingMistPing" } } },
		{ "SpellBookElvenWood", { "", { "ElvenGrove" } } },                           // ElvenWoodSpecialPower ElvenGroveObject
		{ "SpellBookEntAllies", { "", { "RohanEntFir_Summoned|RohanEntBirch_Summoned" } } }, // OCL_SpawnEnts: Count 3 of the two names
		{ "SpellBookEagleAllies", { "", { "GondorGwaihir_Summoned" } } },
		{ "SpellBookFlood", { "D", { "SpellBookFlood" } } },                          // FloodUpdate is SPELL-3's: the wave's damage is seen (S-1987 closed on the merge)
		{ "SpellBookSunflare", { "D", { "SunflareSunbeam" } } },
		{ "SpellBookDwarvenRiches", { "E", {} } },
		{ "SpellBookMenOfDaleAllies", { "", { "DwarvenMenOfDaleHorde_Summoned" } } }, // DwarvenMenOfDaleAlliesSummonEgg -> OCL_SpawnMenOfDale
		{ "SpellBookUndermine", { "", { "DwarvenMineShaftForUndermine" } } },
		{ "SpellBookBombard", { "P", { "SpellBookBombardSeed" } } },
		{ "SpellBookCitadel", { "", { "DwarvenSummonedCitadel" } } },               // DwarvenSummonedCitadelEgg -> OCL_SpellBookCitadelFinish
	};
	return e;
}
} // namespace

TEST_CASE("FACT-A spell book: every power of the faction's book bought in the store and cast from the book on the ground of an enemy battalion; what it does")
{
	if (!hudtest::haveWorld("facta spell book"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	for (const std::string &f : factions())
	{
		FactRig r(s, f.c_str());
		r.local()->depositMoney(2000000);
		const PlayerTemplate *pt = r.local()->getPlayerTemplate();
		// the powers' RequirementsFilter wants the player's fortress: the castle unpacked as at the start
		Object *shell = nullptr;
		(void)unpackCastle(r, shell);
		// the player's spell book object (the start makes it: SpellBookMp)
		bool haveBook = false;
		for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
		{
			haveBook = haveBook || (o->getControllingPlayer() == r.local() && o->getTemplate()->getName() == pt->m_spellBookMp);
		}
		if (!haveBook)
		{
			r.make(pt->m_spellBookMp, r.spot(800.0f, 800.0f, 50.0f));
			r.frames(2);
		}
		// 1. the store: every power it offers, bought in rounds as the points and the tree allow (OnBttnSpell RW 0x8235A5, the close sends them RW 0x8232ED)
		r.local()->science().addSciencePurchasePoints(200);
		size_t bought = 0;
		for (int round = 0; round < 12; ++round)
		{
			std::string err;
			REQUIRE_MESSAGE(r.hud->openSpellStore(&err), err);
			r.frames(2);
			AptSpellStore *st = r.hud->spellStore();
			REQUIRE(st);
			size_t clicked = 0;
			for (const SpellStoreModel::Button &b : st->model().buttons())
			{
				if (b.state == SpellStoreModel::STATE_AVAILABLE && st->click(b.index))
				{
					++clicked;
				}
			}
			r.hud->closeSpellStore();
			r.frames(3);
			bought += clicked;
			if (clicked == 0)
			{
				break;
			}
		}
		// the inventoried powers: the SPELL_BOOK buttons of the book's command set (the FACT-A inventory lists them), by name
		std::set<std::string> inventory;
		{
			const ThingTemplate *bookTt = r.logic().things().findTemplate(pt->m_spellBookMp);
			REQUIRE(bookTt);
			const CommandSet *set = TheCommandStore->findCommandSet(templateCommandSet(*bookTt));
			REQUIRE(set);
			for (int slot = 0; slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (b && b->m_command == GUI_COMMAND_SPELL_BOOK && !b->m_specialPowerName.empty())
				{
					inventory.insert(b->m_specialPowerName);
				}
			}
		}
		std::printf("facta spell book %s: %zu powers bought, %zu in the book's command set\n", f.c_str(), bought, inventory.size());
		REQUIRE(!inventory.empty());
		// 2. the book: each owned power, its target the ground of an enemy battalion beside a wounded allied one
		size_t cast = 0, withEffect = 0;
		std::set<std::string> ownedNames, verified, stopped;
		r.frames(5);
		// the quiet place of the map: the grid point farthest from every lair and from the castle
		Coord3D quiet{ 2000.0f, 1300.0f, 0.0f };
		{
			float best = -1.0f;
			for (float gx = 600.0f; gx <= 4900.0f; gx += 250.0f)
			{
				for (float gy = 500.0f; gy <= 2000.0f; gy += 250.0f)
				{
					float nearest = 1e12f;
					for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
					{
						if (o->getTemplate()->getName().find("Lair") != std::string::npos || o == shell)
						{
							const float dx = o->getPosition()->x - gx, dy = o->getPosition()->y - gy;
							nearest = std::min(nearest, dx * dx + dy * dy);
						}
					}
					if (nearest > best)
					{
						best = nearest;
						quiet = Coord3D{ gx, gy, 0.0f };
					}
				}
			}
		}
		// nothing attacks unless ordered (FACT-B e2d261aa): a hit on the enemy after the cast is the power's
		r.logic().combat().setAutoAcquireEnabled(false);
		// the economy building of the faction (Rebuild's and the Riches' target): INDUSTRY_TYPE_SPELL_OBJECT_FILTER's own good kinds
		std::string econName;
		{
			const std::vector<std::string> reach = reachableTemplates(r);
			for (const char *e : { "GondorFarm", "ArnorFarm", "ElvenMallornTree", "DwarvenMineShaft" })
			{
				econName = econName.empty() && std::find(reach.begin(), reach.end(), e) != reach.end() ? e : econName;
			}
		}
		REQUIRE(!econName.empty());
		const size_t n = r.hud->spellBar().buttons().size();
		size_t owned = 0;
		for (size_t i = 0; i < n; ++i)
		{
			r.frames(2);
			const InGameSpellBookModel::Button bb = r.hud->spellBar().buttons()[i];
			if (!bb.power)
			{
				continue;
			}
			if (!bb.owned)
			{
				FAIL_CHECK(f << " " << bb.power->getName() << ": the book's power is not owned after the store");
				continue;
			}
			++owned;
			ownedNames.insert(bb.power->getName());
			const std::string tag = f + " " + bb.power->getName();
			const Coord3D at = r.spot(quiet.x, quiet.y, 300.0f); // far from the castle (its defenders would shoot the enemy) and the creeps' lairs
			// the enemy and the ally 300 apart (closer, they fight on their own), the power aimed between them
			Object *enemy = r.make("MordorFighterHorde", Coord3D{ at.x + 150.0f, at.y, 0.0f }, r.enemy());
			Object *ally = r.make("GondorFighterHorde", Coord3D{ at.x - 150.0f, at.y, 0.0f });
			Object *self = r.make("GondorFighterHorde", Coord3D{ at.x - 150.0f, at.y + 260.0f, 0.0f });
			const ObjectID enemyId = enemy->getID(), allyId = ally->getID(), selfId = self->getID();
			// Rebuild's and the Riches' target: the player's wounded economy building beside the scene
			const std::string pname = bb.power->getName();
			ObjectID econId = INVALID_ID;
			Coord3D aim = at;
			const auto wantIt = spellExpected().find(pname);
			if (wantIt != spellExpected().end() && wantIt->second.letters.find('P') != std::string::npos)
			{
				aim = FactRig::clickPoint(*enemy); // the volley's and the boulders' radius (100) is around the click: aimed at the battalion
				aim.z = 0.0f;
			}
			if (wantIt != spellExpected().end() && wantIt->second.letters.find('B') != std::string::npos)
			{
				// Rallying Call's AttributeModifierRange (100) is around the click: aimed at the ally battalion, whose members stand around their horde (RW
				// 0x86C4EA), 150 from the scene's middle
				aim = FactRig::clickPoint(*ally);
				aim.z = 0.0f;
			}
			if (pname == "SpellBookRebuild" || pname == "SpellBookDwarvenRiches")
			{
				Object *econ = r.make(econName, Coord3D{ at.x, at.y - 260.0f, 0.0f });
				econId = econ->getID();
				aim = *econ->getPosition();
			}
			// lane PLAY-4 r3: held at once after the stances' first update put them on guard (RotWK's guard scan reaches past 300 and ignores NO_AUTO_ACQUIRE,
			// RW 0x949CA7: the enemy walked off the aim to fight the ally, and the Bombard's boulders fell beside it)
			r.frames(1);
			for (Object *x : { enemy, ally, self })
			{
				holdFire(*x);
			}
			r.frames(19);
			for (Object *x : { enemy, ally, self })
			{
				holdFire(*x);
			}
			toughen(*enemy);
			wound(*ally);
			wound(*self);
			if (Object *econ = r.logic().findObjectByID(econId))
			{
				wound(*econ);
			}
			r.frames(2);
			// a power recharging at the start of the game is made ready (the point of the test is its effect)
			for (int k = 0; k < 600 && !r.hud->spellBar().buttons()[i].ready; ++k)
			{
				r.frames(1);
			}
			if (!r.hud->spellBar().buttons()[i].ready || !r.hud->spellBar().buttons()[i].usable)
			{
				FAIL_CHECK(tag << ": not ready or not usable after 600 frames");
				for (ObjectID id : { enemyId, allyId, selfId, econId })
				{
					if (Object *x = r.logic().findObjectByID(id))
					{
						r.logic().destroyObject(x);
					}
				}
				r.frames(2);
				continue;
			}
			// the control: what the scene does without the power (the map's creeps, the castle's defenders) for 100 frames is not the power's
			Effects control;
			{
				Watch c;
				c.begin(r.logic(), *self, *ally, *enemy, nullptr);
				for (int k = 0; k < 100; ++k)
				{
					r.frames(1);
					c.sample(r.logic(), r.local(), control);
				}
			}
			if (!r.logic().findObjectByID(enemyId) || !r.logic().findObjectByID(allyId) || !r.logic().findObjectByID(selfId))
			{
				FAIL_CHECK(tag << ": a battalion of the scene died without the power");
				for (ObjectID id : { enemyId, allyId, selfId })
				{
					if (Object *x = r.logic().findObjectByID(id))
					{
						r.logic().destroyObject(x);
					}
				}
				r.frames(5);
				continue;
			}
			Watch w;
			w.begin(r.logic(), *self, *ally, *enemy, nullptr);
			SpellWatch sw;
			sw.begin(r.logic(), econId);
			Effects fx;
			std::printf("facta spell book %s: casting\n", tag.c_str());
			std::fflush(stdout);
			const unsigned long long castsBefore = r.rig.game->spellCommands().casts();
			REQUIRE(r.hud->pressSpell((int)i));
			r.frames(1);
			if (r.hud->spellBar().targeting())
			{
				Coord3D ground = aim;
				ground.z = r.logic().getGroundHeight(ground.x, ground.y);
				r.clickWorld(ground);
			}
			// 300 frames: the Bombard's seeds live up to 6 s before their boulders fall
			const auto expectIt = spellExpected().find(pname);
			std::set<std::string> madeSeen;
			auto allMade = [&]() {
				if (expectIt == spellExpected().end())
				{
					return true;
				}
				for (const std::string &t : expectIt->second.made)
				{
					if (!madeSeen.count(t))
					{
						return false;
					}
				}
				return true;
			};
			// 300 frames at least, up to 900 while a named object is missing (an egg's hatch)
			for (int k = 0; k < 900 && (k < 300 || !allMade()); ++k)
			{
				r.frames(1);
				for (Object *x = r.logic().getFirstObject(); x; x = x->getNextObject())
				{
					if (x->getID() > sw.highest && x->getControllingPlayer() != r.enemy())
					{
						madeSeen.insert(x->getTemplate()->getName());
					}
				}
				w.sample(r.logic(), r.local(), fx);
				sw.sample(r.logic(), r.local(), r.logic().findObjectByID(enemyId), fx);
			}
			if (!sw.hit.empty() || !sw.near.empty())
			{
				std::printf("facta spell hit: %s: %s%s\n", tag.c_str(), sw.hit.empty() ? "none in the window" : sw.hit.c_str(),
					sw.near.empty() ? "" : (" (outside: " + sw.near + ")").c_str());
			}
			for (char c : control.seen)
			{
				fx.seen.erase(c);
			}
			const bool accepted = r.rig.game->spellCommands().casts() > castsBefore;
			cast += accepted ? 1 : 0;
			withEffect += fx.seen.empty() ? 0 : 1;
			std::printf("facta spell effect: %s -> %s (the scene alone: %s)%s\n", tag.c_str(), fx.str().c_str(), control.str().c_str(), accepted ? "" : " (the cast was not accepted)");
			CHECK_MESSAGE(accepted, tag << ": the book's press was not cast by the logic");
			const auto want = spellExpected().find(bb.power->getName());
			if (want == spellExpected().end())
			{
				FAIL_CHECK(tag << ": no expected effect is listed (seen " << fx.str() << ")");
			}
			else
			{
				bool ok = accepted;
				for (char c : want->second.letters)
				{
					CHECK_MESSAGE(fx.has(c), tag << ": the effect " << c << " is missing (seen " << fx.str() << ")");
					ok = ok && fx.has(c);
				}
				std::string made;
				for (const std::string &t : want->second.made)
				{
					std::string seenOne;
					for (size_t b = 0, e = 0; e != std::string::npos; b = e + 1)
					{
						e = t.find('|', b);
						const std::string one = t.substr(b, e == std::string::npos ? std::string::npos : e - b);
						seenOne = seenOne.empty() && madeSeen.count(one) ? one : seenOne;
					}
					CHECK_MESSAGE(!seenOne.empty(), tag << ": the power made no " << t);
					ok = ok && !seenOne.empty();
					made += " " + (seenOne.empty() ? t + " (missing)" : seenOne);
				}
				std::printf("facta spell made: %s:%s\n", tag.c_str(), made.empty() ? " (no object expected)" : made.c_str());
				if (want->second.stopped)
				{
					if (ok)
					{
						stopped.insert(pname);
					}
				}
				else if (ok)
				{
					verified.insert(pname);
				}
			}
			// by id: a horde the power killed is gone (its pointer dangles)
			const ObjectID last = selfId;
			std::set<ObjectID> members; // the hordes' soldiers go with their hordes
			for (ObjectID id : { enemyId, allyId, selfId, econId })
			{
				if (Object *x = r.logic().findObjectByID(id))
				{
					for (Object *m : bodies(*x))
					{
						members.insert(m->getID());
					}
					r.logic().destroyObject(x);
				}
			}
			for (Object *x = r.logic().getFirstObject(); x; x = x->getNextObject())
			{
				if (x->getID() > last && !x->getContainedBy() && !members.count(x->getID()))
				{
					r.logic().destroyObject(x);
				}
			}
			r.frames(5);
		}
		// coverage by name: every inventoried power owned, cast and either effect-verified or on its registered stop; nothing else
		std::string missingOwned, missingDone, extra;
		for (const std::string &pw : inventory)
		{
			missingOwned += ownedNames.count(pw) ? "" : " " + pw;
			missingDone += verified.count(pw) || stopped.count(pw) ? "" : " " + pw;
		}
		for (const std::string &pw : ownedNames)
		{
			extra += inventory.count(pw) ? "" : " " + pw;
		}
		std::string stoppedList;
		for (const std::string &pw : stopped)
		{
			stoppedList += " " + pw + " (S-1987)";
		}
		std::printf("facta spell book %s: %zu inventoried, %zu owned, %zu cast, %zu effect-verified, %zu stopped:%s\n", f.c_str(), inventory.size(),
			ownedNames.size(), cast, verified.size(), stopped.size(), stoppedList.empty() ? " none" : stoppedList.c_str());
		CHECK_MESSAGE(missingOwned.empty(), f << ": inventoried powers not owned after the store:" << missingOwned);
		CHECK_MESSAGE(missingDone.empty(), f << ": inventoried powers neither effect-verified nor stopped:" << missingDone);
		CHECK_MESSAGE(extra.empty(), f << ": the book shows powers outside the inventory:" << extra);
		CHECK(verified.size() + stopped.size() == inventory.size());
		(void)n;
		(void)withEffect;
	}
}
