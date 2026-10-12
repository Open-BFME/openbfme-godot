// OpenBFME retail tests for lane BUGS-1: a unit produced at a building leaves its door and walks where it is ordered (FACT-B: the catapult made at
// Mordor's Siege Works moved 19 of a 300-unit order in 120 frames). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP).
#include "doctest.h"
#include "StructureArena.h"

#include "Common/Money.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "Common/MiniJson.h"
#include "GameClient/ControlBarCommands.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <vector>
#include <cstdio>
#include <string>

using namespace structtest;

namespace
{
const ThingTemplate::Nugget *nuggetOf(const ThingTemplate &tt, const char *cls)
{
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (n.name == cls)
		{
			return &n;
		}
	}
	return nullptr;
}

// the CommandSet names a building can have: its own and its CommandSetUpgrade modules' (test_prod_retail.cpp's walk, typed data only)
std::vector<std::string> commandSetsOf(const ThingTemplate &tt)
{
	std::vector<std::string> out;
	if (const FieldValue *v = tt.getFinalOverride()->findField("CommandSet"))
	{
		if (const std::string *own = std::get_if<std::string>(v))
		{
			out.push_back(*own);
		}
	}
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (const CommandSetUpgradeModuleData *typed = dynamic_cast<const CommandSetUpgradeModuleData *>(n.data.get()))
		{
			if (std::find(out.begin(), out.end(), typed->m_commandSet) == out.end())
			{
				out.push_back(typed->m_commandSet);
			}
		}
	}
	return out;
}

struct ExitRun
{
	bool queued = false, made = false, immobile = false, wasHeld = false;
	float heldDrift = 0.0f; // how far a held unit went in the 10 frames after a move order
	int exitFrames = 0;
	Coord3D exitAt{}, from{}, to{};
	float walked = 0.0f;
};

bool isHeld(const Object &o)
{
	return (o.getDisabledMask() & (1u << 3)) != 0; // DISABLED_HELD
}

bool traceOn(const std::string &tag)
{
	const char *e = std::getenv("OPENBFME_BUGS1_TRACE");
	return e && tag.find(e) != std::string::npos;
}

// the building (with the upgrades the button needs) produces the unit through its queue; 60 frames after the unit appears, it (or the horde holding it) is
// ordered 360 away, east-south-east of where it stands, and walks for 120 frames
ExitRun produceAndWalk(const char *faction, const char *building, const CommandButton &button, const std::string &setName, bool trace)
{
	SharedWorld &s = shared();
	ExitRun out;
	Arena ar(s, faction, "FactionMordor");
	Object *b = ar.place(building, 0, 400.0f, 600.0f);
	ar.player(0)->getMoney()->deposit(1000000, false);
	ar.logic.productionSettings() = s.world->productionSettings();
	ar.logic.setUpgradeTypes(&s.world->upgradeTypes());
	ar.player(0)->commandPoints().setFromScript(100000, 100000);
	for (const std::string &u : button.m_neededUpgrade)
	{
		const int type = s.world->upgradeTypes().typeOf(u);
		if (type == UpgradeTypeTable::UPGRADE_TYPE_OBJECT)
		{
			b->giveUpgrade(u);
		}
		else if (type == UpgradeTypeTable::UPGRADE_TYPE_PLAYER)
		{
			ar.player(0)->addCompletedUpgrade(u);
		}
	}
	b->setCommandSetOverride(setName);
	ar.logic.runLogicFrame();
	ProductionUpdateInterface *pu = b->getProductionUpdate();
	REQUIRE(pu != nullptr);
	const ThingTemplate *t = button.getThingTemplate();
	if (!pu->queueCreateUnit(t, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false))
	{
		return out;
	}
	out.queued = true;
	std::set<const ThingTemplate *> variants = { t->getFinalOverride() };
	if (const FieldValue *bv = t->getFinalOverride()->findField("BuildVariations"))
	{
		if (const std::vector<std::string> *names = std::get_if<std::vector<std::string>>(bv))
		{
			for (const std::string &n : *names)
			{
				if (const ThingTemplate *v = s.world->things().findTemplate(n))
				{
					variants.insert(v->getFinalOverride());
				}
			}
		}
	}
	Object *made = nullptr;
	for (int f = 0; f < 3000 && !made; ++f)
	{
		ar.logic.runLogicFrame();
		for (Object *o = ar.logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (variants.count(o->getTemplate()->getFinalOverride()) && !o->isDestroyed())
			{
				made = o;
			}
		}
	}
	if (!made)
	{
		return out;
	}
	out.made = true;
	out.exitAt = *made->getPosition();
	auto line = [&](const char *what, int f, Object *o) {
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		std::printf("  trace %s f%d (%.1f, %.1f) state %u moving %d blocked %d held %d\n", what, f, o->getPosition()->x, o->getPosition()->y, ai ? ai->currentStateId() : 0u,
			ai ? (int)ai->isMoving() : -1, ai ? (int)ai->isBlocked() : -1, (int)isHeld(*o));
	};
	// a unit that fades in (BuildFadeInOnCreateTime) is DISABLED_HELD until its exit (RW 0x8A2AB5): a move order then is refused (RW 0x66B3FD -> 0x690E97)
	if (isHeld(*made) && made->getAIUpdateInterface())
	{
		out.wasHeld = true;
		made->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ out.exitAt.x + 300.0f, out.exitAt.y - 200.0f, 0.0f }, CMD_FROM_PLAYER);
		for (int f = 0; f < 10; ++f)
		{
			ar.logic.runLogicFrame();
		}
		out.heldDrift = std::hypot(made->getPosition()->x - out.exitAt.x, made->getPosition()->y - out.exitAt.y);
	}
	// the exit: held until the fade in ends, then the exit path (AI_FOLLOW_EXITPRODUCTION_PATH) to the natural rally point
	int f = 0;
	for (; f < 600; ++f)
	{
		Object *m = made->getContainedBy() ? made->getContainedBy() : made;
		AIUpdateInterface *ai = m->getAIUpdateInterface();
		if (f >= 20 && !isHeld(*made) && (!ai || ai->currentStateId() != AI_FOLLOW_EXITPRODUCTION_PATH))
		{
			break;
		}
		ar.logic.runLogicFrame();
		if (trace && f % 5 == 0)
		{
			line("exit", f, made);
		}
	}
	out.exitFrames = f;
	Object *mover = made->getContainedBy() ? made->getContainedBy() : made;
	if (mover->isKindOfName("IMMOBILE") || !mover->getAIUpdateInterface())
	{
		out.immobile = true;
		return out;
	}
	out.from = *mover->getPosition();
	out.to = Coord3D{ out.from.x + 300.0f, out.from.y - 200.0f, 0.0f };
	mover->getAIUpdateInterface()->aiMoveToPosition(out.to, CMD_FROM_PLAYER);
	for (int f = 0; f < 120; ++f)
	{
		ar.logic.runLogicFrame();
		if (trace && f % 5 == 0)
		{
			line("move", f, mover);
		}
	}
	out.walked = std::hypot(mover->getPosition()->x - out.from.x, mover->getPosition()->y - out.from.y);
	return out;
}
} // namespace

TEST_CASE("bugs1 retail: every faction's queue-exit production buildings send what they make out of the door and it walks (FACT-B's Siege Works catapult)")
{
	if (!hudtest::haveWorld("bugs1 retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	std::vector<unsigned char> bytes;
	std::string err;
	JsonValue census;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/production/faction_trainable.json", bytes, &err), err);
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), census, &err), err);
	int runs = 0;
	std::set<std::string> held; // the units that were DISABLED_HELD when they appeared (BuildFadeInOnCreateTime)
	std::vector<std::string> problems;
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		const JsonValue *fj = census.get(faction);
		REQUIRE(fj != nullptr);
		const JsonValue *structures = fj->get("structures");
		REQUIRE(structures != nullptr);
		for (const JsonValue &sv : structures->array)
		{
			const ThingTemplate *building = s.world->things().findTemplate(sv.string);
			if (!building || !nuggetOf(*building, "ProductionUpdate") || !nuggetOf(*building, "QueueProductionExitUpdate"))
			{
				continue;
			}
			std::set<std::string> done;
			for (const std::string &setName : commandSetsOf(*building))
			{
				const CommandSet *set = s.world->commands().findCommandSet(setName);
				REQUIRE_MESSAGE(set != nullptr, setName);
				for (int slot = 0; slot < CommandSet::MAX_BUTTONS; ++slot)
				{
					const CommandButton *button = set->getCommandButton(slot);
					if (!button || button->m_command != GUI_COMMAND_UNIT_BUILD || !button->getThingTemplate() || !done.insert(button->getThingTemplate()->getName()).second)
					{
						continue;
					}
					const ThingTemplate *ut = button->getThingTemplate()->getFinalOverride();
					const std::string tag = sv.string + "/" + ut->getName();
					const ExitRun r = produceAndWalk(faction, sv.string.c_str(), *button, setName, traceOn(tag));
					++runs;
					if (r.wasHeld)
					{
						held.insert(ut->getName());
					}
					std::printf("  info: %s: %s left at (%.1f, %.1f)%s after %d frames, walked %.1f of %.1f in 120 frames\n", faction, tag.c_str(), r.exitAt.x, r.exitAt.y,
						r.wasHeld ? " (held, a move order refused)" : "", r.exitFrames, r.walked, std::hypot(r.to.x - r.from.x, r.to.y - r.from.y));
					if (!r.queued || !r.made)
					{
						problems.push_back(tag + (r.queued ? ": nothing was made" : ": the queue refused it"));
					}
					else if (r.exitFrames >= 600)
					{
						problems.push_back(tag + ": still held or on its exit path after 600 frames (an exit stall)");
					}
					else if (r.heldDrift > 0.01f)
					{
						problems.push_back(tag + ": moved " + std::to_string(r.heldDrift) + " while DISABLED_HELD");
					}
					else if (!r.immobile && r.walked < 100.0f)
					{
						problems.push_back(tag + ": walked only " + std::to_string((int)r.walked));
					}
				}
			}
		}
	}
	std::printf("  info: %d production runs, %zu problems\n", runs, problems.size());
	for (const std::string &p : problems)
	{
		std::printf("  problem: %s\n", p.c_str());
	}
	CHECK(runs > 50);
	CHECK(held.count("MordorCatapult") == 1); // BuildFadeInOnCreateTime 16 s: 80 frames held
	CHECK(problems.empty());
}
