// OpenBFME. GPL-3.0.
// See GameNetwork/ScriptedPlayer.h.

#include "GameNetwork/ScriptedPlayer.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerHeroList.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "Common/BuildAssistant.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/WallSpan.h"

#include <string>
#include <vector>

// The scripted human (deterministic from the shared state, issued only by the owning peer): every 15 frames a group of up to 12 of the player's own
// mobile objects is selected and sent somewhere: around its start, a third of the way toward another start position, or stopped.
static unsigned moveCommands(LiveGame &game, int playerIndex, int startPos, CommandList &out)
{
	const UnsignedInt f = game.frame();
	if (f == 0 || f % 15 != 0)
	{
		return 0;
	}
	GameLogic &logic = game.logic();
	const Player *me = logic.players().getNthPlayer(playerIndex);
	if (!me)
	{
		return 0;
	}
	std::vector<ObjectID> ids;
	for (Object *o = logic.getFirstObject(); o && ids.size() < 12; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == me && !o->isEffectivelyDead() && !o->isKindOfName("STRUCTURE"))
		{
			ids.push_back(o->getID());
		}
	}
	if (ids.empty())
	{
		return 0;
	}
	const Waypoint *home = logic.terrain()->findWaypointByName("Player_" + std::to_string(startPos + 1) + "_Start");
	if (!home)
	{
		return 0;
	}
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, playerIndex);
	sel.appendBooleanArgument(true);
	for (ObjectID id : ids)
	{
		sel.appendObjectIDArgument(id);
	}
	out.append(sel);
	const int k = (int)((f / 15) % 8) % 4;
	Coord3D to = home->location;
	const float off = (float)(int)((f * 37u) % 200u) - 100.0f; // a deterministic wobble
	if (k == 1)
	{
		to.x += 150.0f + off;
		to.y += 120.0f - off;
	}
	else if (k == 2)
	{
		// a third of the way toward the first other start position
		for (int n = 1; n <= MAX_SLOTS; ++n)
		{
			const Waypoint *other = n != startPos + 1 ? logic.terrain()->findWaypointByName("Player_" + std::to_string(n) + "_Start") : nullptr;
			if (other)
			{
				to.x = home->location.x + (other->location.x - home->location.x) * 0.35f;
				to.y = home->location.y + (other->location.y - home->location.y) * 0.35f;
				break;
			}
		}
	}
	to.z = logic.getGroundHeight(to.x, to.y);
	if (k == 3)
	{
		out.append(GameMessage(MSG_DO_STOP, playerIndex));
	}
	else
	{
		GameMessage mv(k == 2 ? MSG_DO_ATTACKMOVETO : MSG_DO_MOVETO, playerIndex);
		mv.appendLocationArgument(to);
		out.append(mv);
	}
	return 2;
}


// ---- lane MP-1 (merge with the archive): the features of the other lanes in the scripted game ------------------------------------------------------------
// Every rule below reads the shared logic state only (never changes it: the script runs on one peer, so a change outside the command list would be a desync
// by construction) and issues the same GameMessages the HUD issues. Choices are data-driven (the objects' CommandSets, the science store, the spell book):
// nothing here names retail content.
namespace
{
struct Mine
{
	std::vector<Object *> structures, foundations, dozers, hubs;
};

Mine collect(GameLogic &logic, const Player *me)
{
	Mine m;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != me || o->isEffectivelyDead() || o->isDestroyed())
		{
			continue;
		}
		if (o->isKindOfName("BASE_FOUNDATION"))
		{
			m.foundations.push_back(o);
		}
		else if (o->isKindOfName("STRUCTURE") && !o->isUnderConstruction())
		{
			m.structures.push_back(o);
			if (o->isKindOfName("WALL_HUB"))
			{
				m.hubs.push_back(o);
			}
		}
		else if (o->isKindOfName("DOZER"))
		{
			m.dozers.push_back(o);
		}
	}
	return m;
}

// the buttons of `obj`'s command set with `command`, in button order
std::vector<const CommandButton *> buttons(const Object &obj, int command)
{
	std::vector<const CommandButton *> out;
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(obj.getCommandSetName()) : nullptr;
	for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		b = b ? b->getFinalOverride() : nullptr;
		if (b && b->m_command == command)
		{
			out.push_back(b);
		}
	}
	return out;
}

GameMessage select(int player, const Object &o)
{
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(o.getID());
	return sel;
}

// a structure's unit (heroes included: they are UNIT_BUILD buttons) the player can make now; rotates through the choices with `turn`
unsigned produce(GameLogic &logic, int player, const Mine &m, unsigned turn, CommandList &out)
{
	(void)logic;
	std::vector<std::pair<Object *, const ThingTemplate *>> choices;
	for (Object *o : m.structures)
	{
		for (const CommandButton *b : buttons(*o, GUI_COMMAND_UNIT_BUILD))
		{
			const ThingTemplate *tt = b->getThingTemplate();
			if (tt && BuildAssistant::canMakeUnit(*o, tt, -1) == CANMAKE_OK)
			{
				choices.push_back({ o, tt });
			}
		}
	}
	if (choices.empty())
	{
		return 0;
	}
	// a hero every other turn when one can be made (the template's KindOf HERO: the pure builder, no logic cache touched)
	size_t pick = turn % choices.size();
	const int heroBit = ObjectTemplateInfoBuilder::kindOfIndex("HERO");
	for (size_t i = 0; (turn & 1u) == 1 && heroBit >= 0 && i < choices.size(); ++i)
	{
		if (MaskTest(ObjectTemplateInfoBuilder::build(*choices[i].second).kindOf, (unsigned)heroBit))
		{
			pick = i;
			break;
		}
	}
	const auto &c = choices[pick];
	out.append(select(player, *c.first));
	GameMessage q(MSG_QUEUE_UNIT_CREATE, player); // RW 0x77A764 { byte fromBuildIndex, int templateId, int value30, byte batch, byte secondary }
	q.appendBooleanArgument(false);
	q.appendIntegerArgument((int)c.second->getTemplateID());
	q.appendIntegerArgument(-1);
	q.appendBooleanArgument(false);
	q.appendBooleanArgument(false);
	out.append(q);
	return 2;
}

// a builder (KindOf DOZER) constructs one of its DOZER_CONSTRUCT buttons the player can make, on a ring around the start (the dispatcher refuses an illegal
// site: counted, deterministic): barracks and the other producers, whose heroes produce() then offers
unsigned construct(GameLogic &logic, int player, const Mine &m, const Coord3D &home, unsigned turn, CommandList &out)
{
	static const float kRing[8][2] = { { 380.0f, 0.0f }, { 0.0f, 380.0f }, { -380.0f, 0.0f }, { 0.0f, -380.0f },
		{ 270.0f, 270.0f }, { -270.0f, 270.0f }, { 270.0f, -270.0f }, { -270.0f, -270.0f } };
	for (Object *d : m.dozers)
	{
		std::vector<const ThingTemplate *> choices;
		for (const CommandButton *b : buttons(*d, GUI_COMMAND_DOZER_CONSTRUCT))
		{
			const ThingTemplate *tt = b->getThingTemplate();
			if (tt && BuildAssistant::canMakeUnit(*d, tt, -1) == CANMAKE_OK)
			{
				choices.push_back(tt);
			}
		}
		if (choices.empty())
		{
			continue;
		}
		const ThingTemplate *tt = choices[turn % choices.size()];
		Coord3D at = home;
		at.x += kRing[turn % 8][0];
		at.y += kRing[turn % 8][1];
		at.z = logic.getGroundHeight(at.x, at.y);
		out.append(select(player, *d));
		GameMessage q(MSG_DOZER_CONSTRUCT, player); // RW 0x77B011 { int templateId, location, real angle }
		q.appendIntegerArgument((int)tt->getTemplateID());
		q.appendLocationArgument(at);
		q.appendRealArgument(0.0f);
		out.append(q);
		return 2;
	}
	return 0;
}

// a PLAYER_UPGRADE / OBJECT_UPGRADE button of a structure (the dispatcher refuses what cannot be researched: counted, deterministic)
unsigned research(int player, const Mine &m, unsigned turn, CommandList &out)
{
	std::vector<std::pair<Object *, const UpgradeTemplate *>> choices;
	for (Object *o : m.structures)
	{
		for (int cmd : { (int)GUI_COMMAND_PLAYER_UPGRADE, (int)GUI_COMMAND_OBJECT_UPGRADE })
		{
			for (const CommandButton *b : buttons(*o, cmd))
			{
				const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(b->m_upgradeName) : nullptr;
				if (u)
				{
					choices.push_back({ o, u });
				}
			}
		}
	}
	if (choices.empty())
	{
		return 0;
	}
	const auto &c = choices[turn % choices.size()];
	out.append(select(player, *c.first));
	GameMessage q(MSG_QUEUE_UPGRADE, player); // RW 0x77A6FD { object id, mask bit }
	q.appendObjectIDArgument(c.first->getID());
	q.appendIntegerArgument(c.second->getMaskBit());
	out.append(q);
	return 2;
}

// a FOUNDATION_CONSTRUCT button of an empty fortress pad (wall hubs, towers, ...)
unsigned foundation(int player, const Mine &m, unsigned turn, CommandList &out)
{
	std::vector<std::pair<Object *, const ThingTemplate *>> choices;
	for (Object *o : m.foundations)
	{
		for (const CommandButton *b : buttons(*o, GUI_COMMAND_FOUNDATION_CONSTRUCT))
		{
			const ThingTemplate *tt = b->getThingTemplate();
			if (tt && BuildAssistant::canMakeUnit(*o, tt, -1) == CANMAKE_OK)
			{
				choices.push_back({ o, tt });
			}
		}
	}
	if (choices.empty())
	{
		return 0;
	}
	// prefer a wall hub every other turn when one is offered (the walls of the scripted game)
	size_t pick = turn % choices.size();
	if ((turn & 1u) == 0)
	{
		for (size_t i = 0; i < choices.size(); ++i)
		{
			const int bit = ObjectTemplateInfoBuilder::kindOfIndex("WALL_HUB");
			if (bit >= 0 && MaskTest(ObjectTemplateInfoBuilder::build(*choices[i].second).kindOf, (unsigned)bit)) // the pure builder: no logic cache touched
			{
				pick = i;
				break;
			}
		}
	}
	const auto &c = choices[pick];
	out.append(select(player, *c.first));
	GameMessage q(MSG_FOUNDATION_CONSTRUCT, player); // RW 0x77A91B { int templateId, location, real angle }
	q.appendIntegerArgument((int)c.second->getTemplateID());
	q.appendLocationArgument(*c.first->getPosition());
	q.appendRealArgument(0.0f);
	out.append(q);
	return 2;
}

// a wall span from a finished hub, outward from the player's start
unsigned wall(GameLogic &logic, int player, const Mine &m, const Coord3D &home, unsigned turn, CommandList &out)
{
	for (size_t n = 0; n < m.hubs.size(); ++n)
	{
		Object *hub = m.hubs[(turn + n) % m.hubs.size()];
		WallHubBehavior *wh = WallHubBehavior::find(*hub, 0);
		const ThingTemplate *cap = wh ? wh->hubCapTemplate() : nullptr;
		if (!cap)
		{
			continue;
		}
		const Coord3D start = *hub->getPosition();
		Coord3D end = start;
		const float dx = start.x - home.x, dy = start.y - home.y;
		// a fixed-length span along the outward axis (the larger component) so no square root is needed
		const float sx = dx >= 0.0f ? 1.0f : -1.0f, sy = dy >= 0.0f ? 1.0f : -1.0f;
		if ((dx >= 0.0f ? dx : -dx) >= (dy >= 0.0f ? dy : -dy))
		{
			end.x += sx * 300.0f;
		}
		else
		{
			end.y += sy * 300.0f;
		}
		end.z = logic.getGroundHeight(end.x, end.y);
		GameMessage w(MSG_WALL_HUB_CONSTRUCT_SPAN, player); // RW 0x77C4C3 { hub cap template id, start, end, options, hub }
		w.appendIntegerArgument((int)cap->getFinalOverride()->getTemplateID());
		w.appendLocationArgument(start);
		w.appendLocationArgument(end);
		w.appendIntegerArgument((int)wh->data()->m_options);
		w.appendObjectIDArgument(hub->getID());
		out.append(w);
		return 1;
	}
	return 0;
}

// a science that unlocks a power of the spell book (the first the player can buy now), then a ready power of the book cast near the player's start
unsigned spells(GameLogic &logic, int player, const Coord3D &home, unsigned turn, CommandList &out)
{
	unsigned n = 0;
	const Player *me = logic.players().getNthPlayer(player);
	Object *book = me ? SpecialPowerModules::findSpellBookObject(logic, *me) : nullptr;
	if (!book)
	{
		return 0;
	}
	std::vector<const SpecialPowerTemplate *> ready;
	bool bought = false;
	for (const auto &mod : book->modules())
	{
		SpecialPowerModuleInterface *sp = mod->getSpecialPower();
		const SpecialPowerTemplate *t = sp ? sp->getSpecialPowerTemplate() : nullptr;
		if (!t)
		{
			continue;
		}
		bool owned = t->getRequiredSciences().empty();
		for (ScienceType st : t->getRequiredSciences())
		{
			owned = owned || me->science().hasScience(st);
		}
		if (!owned && !bought)
		{
			for (ScienceType st : t->getRequiredSciences())
			{
				if (me->science().isCapableOfPurchasingScience(st))
				{
					GameMessage m(MSG_PURCHASE_SCIENCE, player);
					m.appendIntegerArgument(player);
					m.appendIntegerArgument(st);
					out.append(m);
					++n;
					bought = true;
					break;
				}
			}
		}
		if (owned && sp->isReadyForDisplay())
		{
			ready.push_back(t);
		}
	}
	if (ready.empty())
	{
		return n;
	}
	const SpecialPowerTemplate *t = ready[turn % ready.size()];
	Coord3D at = home;
	at.x += 120.0f;
	at.z = logic.getGroundHeight(at.x, at.y);
	GameMessage c(MSG_DO_SPECIAL_POWER_AT_LOCATION, player); // as the HUD's spell book: { power id, location, object, options, source (the book) }
	c.appendIntegerArgument((int)t->getID());
	c.appendLocationArgument(at);
	c.appendObjectIDArgument(INVALID_ID);
	c.appendIntegerArgument(0);
	c.appendObjectIDArgument(book->getID());
	out.append(c);
	return n + 1;
}
} // namespace

unsigned ScriptedPlayer::issue(LiveGame &game, int playerIndex, int startPos, CommandList &out)
{
	const UnsignedInt f = game.frame();
	if (f == 0 || f % 15 != 0)
	{
		return 0;
	}
	const int k = (int)((f / 15) % 8);
	if (k < 4)
	{
		return moveCommands(game, playerIndex, startPos, out);
	}
	GameLogic &logic = game.logic();
	const Player *me = logic.players().getNthPlayer(playerIndex);
	const Waypoint *home = logic.terrain() ? logic.terrain()->findWaypointByName("Player_" + std::to_string(startPos + 1) + "_Start") : nullptr;
	if (!me || !home)
	{
		return 0;
	}
	const Mine m = collect(logic, me);
	const unsigned turn = (unsigned)(f / 120);
	switch (k)
	{
	case 4:
		return produce(logic, playerIndex, m, turn, out) + construct(logic, playerIndex, m, home->location, turn, out);
	case 5:
		return research(playerIndex, m, turn, out);
	case 6:
		return foundation(playerIndex, m, turn, out) + wall(logic, playerIndex, m, home->location, turn, out);
	default:
		return spells(logic, playerIndex, home->location, turn, out);
	}
}

unsigned ScriptedPlayer::recruitCreateAHero(LiveGame &game, int playerIndex, CommandList &out)
{
	GameLogic &logic = game.logic();
	Player *player = logic.players().getNthPlayer(playerIndex);
	const ThingTemplate *cah = logic.things().findTemplate("CreateAHero");
	if (!player || !cah || !logic.createAHeroes().heroOf(*player))
	{
		return 0;
	}
	const int index = player->heroes().findIndex(*cah, 0xFFFFFFFFu, 0);
	if (index < 0)
	{
		return 0;
	}
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && !o->isEffectivelyDead() && o->getProductionUpdate() && !buttons(*o, GUI_COMMAND_REVIVE).empty())
		{
			out.append(select(playerIndex, *o));
			GameMessage q(MSG_QUEUE_UNIT_CREATE, playerIndex);
			q.appendBooleanArgument(true); // from the hero list (RW 0x77A764)
			q.appendIntegerArgument(index);
			q.appendIntegerArgument(-1);
			q.appendBooleanArgument(false);
			q.appendBooleanArgument(false);
			out.append(q);
			return 2;
		}
	}
	return 0;
}
