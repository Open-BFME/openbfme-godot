// OpenBFME retail tests, lane HUD-5 round 4 (Sol's r3 review): each pick caller's pick types driven through the HUD's real input events. GPL-3.0.
// RW 0x71083F (the base types, SHRUBBERY / ROCK from the GUI command or the selection, RW 0x71077B), the point selection | OWN (RW 0x485CB8), the hover forced
// (RW 0x83CC13), the order click (RW 0x81FBB3), the double click SELECTABLE only (RW 0x81F7C5), a GUI command's object target (RW 0x83D41A), the drag box unranked.
// The tests SKIP loudly without the installs.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <variant>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
bool logHasLine(const HudInput &in, const std::string &line)
{
	const std::vector<std::string> &l = in.messageLog();
	return std::find(l.begin(), l.end(), line) != l.end();
}

size_t logCountPrefix(const HudInput &in, const std::string &prefix)
{
	size_t n = 0;
	for (const std::string &l : in.messageLog())
	{
		n += l.compare(0, prefix.size(), prefix) == 0 ? 1 : 0;
	}
	return n;
}

bool templateHas(const ThingTemplate &tt, const char *kind)
{
	const ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(*tt.getFinalOverride());
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(kind);
	return bit >= 0 && MaskTest(info.kindOf, (unsigned)bit);
}

// the first template with `kind` (SELECTABLE or not: RW 0x4B5894 gives a SHRUBBERY / ROCK its own type in either case) that the game makes at `at` and whose pick the ray at its foot meets; the object, null for none
Object *placePickable(SharedWorld &s, Rig &r, const char *kind, const Coord3D &at, std::string &name)
{
	const HudContext &ctx = r.input->context();
	int tried = 0;
	for (const ThingTemplate *tt : s.world->things().templates())
	{
		if (!tt || tt->getFinalOverride() != tt || !templateHas(*tt, kind) || templateHas(*tt, "CLICK_THROUGH") || (std::string(kind) == "FORCEATTACKABLE" && templateHas(*tt, "SELECTABLE")))
		{
			continue;
		}
		if (++tried > 400)
		{
			break;
		}
		std::string err;
		Object *o = r.game->createObject(tt->getName(), r.local->getPlayerIndex(), Coord3D{ at.x, at.y, r.logic().getGroundHeight(at.x, at.y) }, 0.0f, &err);
		if (!o)
		{
			continue;
		}
		r.frame(1);
		const ICoord2D px = r.screenOf({ at.x, at.y, r.logic().getGroundHeight(at.x, at.y) + 4.0f });
		bool hit = false;
		for (const HudObjects::PickHit &h : HudObjects::pickHits(ctx, px, 0xFFFFu))
		{
			hit = hit || h.obj == o;
		}
		if (hit && HudObjects::collisionTypeOf(ctx, *o) != 0)
		{
			name = tt->getName();
			return o;
		}
		r.logic().destroyObject(o);
		r.frame(1);
	}
	return nullptr;
}

const CommandButton *buttonWith(SharedWorld &s, std::uint32_t option)
{
	for (const std::string &n : s.world->commands().buttonNames())
	{
		const CommandButton *b = s.world->commands().findCommandButton(n);
		if (b && b->hasOption(option))
		{
			return b;
		}
	}
	return nullptr;
}
} // namespace

// the hover is forced (RW 0x83CC13), the selection and the order click are not: a FORCEATTACKABLE object is under the cursor for the hover only. SHRUBBERY and
// ROCK join the pick types only through a GUI command's options or the selection (RW 0x71083F / 0x71077B), and a GUI command's object target takes RW 0x83D41A's
TEST_CASE("hud5 pick retail: hover is forced; shrubbery / rock come from the GUI command or the selection; a GUI target picks by its button (real mouse moves)")
{
	if (!haveWorld("hud5 pick hover"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	r.input->context().commands = &s.world->commands(); // as InGameHud installs it: the selection's command sets (RW 0x691F7F / 0x691FCE)
	const HudContext &ctx = r.input->context();
	const Coord3D c = r.freeSpot(2400, 1800, 300.0f);
	r.lookAt({ c.x, c.y, r.logic().getGroundHeight(c.x, c.y) }, 300.0f, 40.0f);
	r.frame(2);
	const ICoord2D px = r.screenOf({ c.x, c.y, r.logic().getGroundHeight(c.x, c.y) + 4.0f });
	auto hoverAt = [&](const ICoord2D &p) {
		r.move(p.x + 3, p.y);
		r.move(p.x, p.y);
		return r.input->ui().mouseoverObject();
	};

	// FORCEATTACKABLE (RW 0x4B583C: type 0x20)
	std::string name;
	if (Object *fa = placePickable(s, r, "FORCEATTACKABLE", c, name))
	{
		MESSAGE("FORCEATTACKABLE: " << name);
		CHECK(hoverAt(px) == fa->getID());                      // the hover always casts with 0x20
		CHECK(HudObjects::pickObject(ctx, px) != fa);            // the order click without the force attack mode does not
		CHECK(HudObjects::pickForSelection(ctx, px) != fa);
		r.input->ui().deselectAll(true);
		r.leftClick(px.x, px.y);
		CHECK_FALSE(r.input->ui().isSelected(fa->getID()));
		r.logic().destroyObject(fa);
		r.frame(2);
	}
	else
	{
		MESSAGE("no RotWK 2.01 template has KindOf FORCEATTACKABLE (a mod may): the force-only hover target is pinned by the mask");
		CHECK((HudObjects::pickTypesForContext(ctx, true) & HudObjects::PICK_TYPE_FORCEATTACKABLE) != 0);
	}

	// SHRUBBERY (type 8)
	Object *shrub = placePickable(s, r, "SHRUBBERY", c, name);
	REQUIRE_MESSAGE(shrub, "no pickable SHRUBBERY template");
	MESSAGE("SHRUBBERY: " << name);
	r.input->ui().deselectAll(true);
	r.input->ui().setGUICommand(nullptr);
	CHECK(hoverAt(px) != shrub->getID()); // nothing selected, no GUI command: 8 is not cast
	// a GUI command that allows a shrubbery target: the hover and the button's object target meet it
	const CommandButton *shrubButton = buttonWith(s, COMMAND_OPTION_ALLOW_SHRUBBERY_TARGET);
	REQUIRE(shrubButton);
	MESSAGE("shrubbery button: " << shrubButton->m_name);
	r.input->ui().setGUICommand(shrubButton);
	CHECK((HudObjects::pickTypesForContext(ctx) & HudObjects::PICK_TYPE_SHRUBBERY) != 0);
	CHECK(hoverAt(px) == shrub->getID());
	CHECK(HudObjects::pickForGuiCommand(ctx, px, *shrubButton) == shrub);
	r.input->ui().setGUICommand(nullptr);
	CHECK(hoverAt(px) != shrub->getID());
	// a button without the option: its object target does not meet it
	const CommandButton *plain = nullptr;
	for (const std::string &n : s.world->commands().buttonNames())
	{
		const CommandButton *b = s.world->commands().findCommandButton(n);
		if (b && b->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT) && !b->hasOption(COMMAND_OPTION_ALLOW_SHRUBBERY_TARGET) && !b->hasOption(COMMAND_OPTION_ALLOW_ROCK_TARGET))
		{
			plain = b;
			break;
		}
	}
	REQUIRE(plain);
	CHECK(HudObjects::pickForGuiCommand(ctx, px, *plain) != shrub);
	// the selection (RW 0x71077B): a controllable unit that deals FLAME damage or has such a button makes the hover meet the shrubbery
	Object *flamer = nullptr;
	// a unit template whose command set holds a button with ALLOW_SHRUBBERY_TARGET (RW 0x691F7F's second test)
	std::vector<std::string> sets;
		for (const std::string &n : s.world->commands().setNames())
		{
			const CommandSet *set = s.world->commands().findCommandSet(n);
			for (int i = 0; set && i < (int)CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *b = set->getCommandButton(i);
				if (b && b->hasOption(COMMAND_OPTION_ALLOW_SHRUBBERY_TARGET))
				{
					sets.push_back(n);
					break;
				}
			}
		}
		for (const ThingTemplate *tt : s.world->things().templates())
		{
			if (flamer || !tt || tt->getFinalOverride() != tt || !templateHas(*tt, "SELECTABLE"))
			{
				continue;
			}
			const FieldValue *v = tt->findField("CommandSet");
			const std::string *set = v ? std::get_if<std::string>(v) : nullptr;
			if (!set || std::find(sets.begin(), sets.end(), *set) == sets.end())
			{
				continue;
			}
			std::string err;
			Object *u = r.game->createObject(tt->getName(), r.local->getPlayerIndex(), Coord3D{ c.x + 200.0f, c.y + 200.0f, 0.0f }, 0.0f, &err);
			if (!u)
			{
				continue;
			}
			r.input->ui().deselectAll(true);
			r.input->ui().selectObject(u->getID());
			if (HudObjects::pickTypesForContext(ctx) & HudObjects::PICK_TYPE_SHRUBBERY)
			{
				flamer = u;
				MESSAGE("selection that adds SHRUBBERY: " << tt->getName() << " (command set " << *set << ")");
			}
			else
			{
				r.input->ui().deselectAll(true);
				r.logic().destroyObject(u);
			}
		}
	REQUIRE_MESSAGE(flamer, "no unit adds SHRUBBERY by its selection (RW 0x71077B)");
	r.frame(1);
	r.input->ui().deselectAll(true);
	r.input->ui().selectObject(flamer->getID());
	CHECK(hoverAt(px) == shrub->getID());
	// nothing selected: no SHRUBBERY again
	r.input->ui().deselectAll(true);
	CHECK(hoverAt(px) != shrub->getID());
	r.logic().destroyObject(shrub);
	r.frame(2);

	// ROCK / ROCK_VENDOR (type 0x200) through a GUI command that allows it
	const CommandButton *rockButton = buttonWith(s, COMMAND_OPTION_ALLOW_ROCK_TARGET);
	Object *rock = placePickable(s, r, "ROCK_VENDOR", c, name);
	if (!rock)
	{
		rock = placePickable(s, r, "ROCK", c, name);
	}
	if (rock && rockButton)
	{
		MESSAGE("ROCK: " << name << ", button " << rockButton->m_name);
		r.input->ui().deselectAll(true);
		CHECK(hoverAt(px) != rock->getID());
		r.input->ui().setGUICommand(rockButton);
		CHECK(hoverAt(px) == rock->getID());
		CHECK(HudObjects::pickForGuiCommand(ctx, px, *rockButton) == rock);
		r.input->ui().setGUICommand(nullptr);
		CHECK(HudObjects::pickForGuiCommand(ctx, px, *plain) != rock);
	}
	else
	{
		MESSAGE("rock target not testable: template " << (rock ? "found" : "missing") << ", button " << (rockButton ? "found" : "missing"));
	}
}

// the order click (RW 0x81FBB3: no OWN) attacks the enemy behind an own unit; the double click (RW 0x81F7C5: SELECTABLE) meets that enemy too, so it selects no
// matching units; a selection click takes the own unit; a drag box selects every own unit inside, unranked. All through the HUD's mouse events
TEST_CASE("hud5 pick retail: order click, double click, selection click and drag box through real mouse events")
{
	if (!haveWorld("hud5 pick input"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	const Coord3D c = r.freeSpot(2400, 1800, 300.0f);
	const std::string soldier = templates(s).infantry.front();
	// the camera looks north: the blocker stands south of the enemy on the same ray; the attacker and a second unit of the blocker's type stand aside
	Object *blocker = r.make("MenPorter", c.x, c.y);
	Object *foe = r.make("MenPorter", c.x, c.y + 60.0f, enemy);
	Object *twin = r.make("MenPorter", c.x + 90.0f, c.y);
	Object *attacker = r.make(soldier, c.x - 90.0f, c.y);
	r.lookAt({ c.x, c.y + 30.0f, r.logic().getGroundHeight(c.x, c.y) }, 300.0f, 40.0f);
	r.frame(2);
	const ICoord2D px = r.screenOf({ c.x, c.y, r.logic().getGroundHeight(c.x, c.y) + 8.0f });
	auto at = [&](Object *o) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, o->getPosition()->z + 6.0f }); };

	// the selection click on the blocker: the own unit, not the enemy behind it
	r.input->ui().deselectAll(true);
	r.leftClick(px.x, px.y);
	CHECK(r.input->ui().isSelected(blocker->getID()));
	CHECK_FALSE(r.input->ui().isSelected(foe->getID()));

	// the double click there: RotWK picks with SELECTABLE only, the enemy behind wins (0x14): no units of the blocker's type are added
	r.leftClick(px.x, px.y, true);
	CHECK_FALSE(r.input->ui().isSelected(twin->getID()));
	CHECK_FALSE(r.input->ui().isSelected(foe->getID()));
	// on the twin alone (nothing behind it) the double click does select the matching units on the screen
	const ICoord2D pt = at(twin);
	r.leftClick(pt.x, pt.y);
	r.leftClick(pt.x, pt.y, true);
	CHECK(r.input->ui().isSelected(twin->getID()));
	CHECK(r.input->ui().isSelected(blocker->getID()));

	// the order click (alternate setup: the right click orders) with the attacker selected: it attacks the enemy behind the own blocker
	const ICoord2D pa = at(attacker);
	r.input->ui().deselectAll(true);
	r.leftClick(pa.x, pa.y);
	r.frame(1);
	REQUIRE(r.input->ui().isSelected(attacker->getID()));
	REQUIRE(r.input->ui().getSelectCount() == 1);
	r.input->commandTranslator().setUseAlternateMouse(true);
	const size_t attacks = logCountPrefix(*r.input, "MSG_DO_ATTACK_OBJECT");
	r.rightClick(px.x, px.y);
	CHECK(logCountPrefix(*r.input, "MSG_DO_ATTACK_OBJECT") == attacks + 1);
	CHECK(logHasLine(*r.input, "MSG_DO_ATTACK_OBJECT o" + std::to_string(foe->getID())));
	CHECK(r.input->ui().isSelected(attacker->getID())); // the right click ordered, it did not select the blocker

	// the drag box over the blocker, the twin and the enemy behind: every own unit inside is selected (no ranking, no single pick)
	r.input->commandTranslator().setUseAlternateMouse(false);
	r.input->ui().deselectAll(true);
	const ICoord2D pb = at(blocker), pf = at(foe);
	r.leftDrag(std::min(pb.x, pf.x) - 30, std::min(pf.y, pb.y) - 40, pt.x + 30, std::max(pf.y, pb.y) + 30);
	CHECK(r.input->ui().isSelected(blocker->getID()));
	CHECK(r.input->ui().isSelected(twin->getID()));
	CHECK_FALSE(r.input->ui().isSelected(foe->getID()));
}
