// OpenBFME. GPL-3.0.
// See GameLogic/SpellCommands.h. Lane SPELL-1.

#include "GameLogic/SpellCommands.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char *const kLane = "SPELL-1";
}

void SpellCommands::registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_PURCHASE_SCIENCE, kLane, [this](GameLogic &l, const GameMessage &m) { return purchaseScience(l, m); });
	d.registerHandler(MSG_DO_SPECIAL_POWER, kLane, [this](GameLogic &l, const GameMessage &m) { return doSpecialPower(l, m, 0); });
	d.registerHandler(MSG_DO_SPECIAL_POWER_AT_LOCATION, kLane, [this](GameLogic &l, const GameMessage &m) { return doSpecialPower(l, m, 1); });
	d.registerHandler(MSG_DO_SPECIAL_POWER_AT_OBJECT, kLane, [this](GameLogic &l, const GameMessage &m) { return doSpecialPower(l, m, 2); });
	d.registerHandler(MSG_DO_SPELLBOOK_SPECIAL_POWER, kLane, [this](GameLogic &l, const GameMessage &m) { return doSpecialPower(l, m, 3); });
}

namespace
{
const GameMessageArgument *arg(const GameMessage &m, size_t i, GameMessageArgumentDataType t)
{
	const GameMessageArgument *a = m.getArgument(i);
	return a && a->type == t ? a : nullptr;
}
} // namespace

// RW 0x77A42C / 0x77A502 / 0x77A5D7 / 0x77B886 (see the header)
bool SpellCommands::doSpecialPower(GameLogic &logic, const GameMessage &msg, int kind)
{
	const GameMessageArgument *id = arg(msg, 0, ARGUMENTDATATYPE_INTEGER);
	if (!id || !TheSpecialPowerStore)
	{
		++m_malformed;
		return false;
	}
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplateByID((unsigned)id->integer); // RW 0x7B1B05
	Coord3D loc{};
	Object *target = nullptr;
	ObjectID sourceId = INVALID_ID;
	unsigned options = 0;
	switch (kind)
	{
	case 0: // {id, options, source}
	{
		const GameMessageArgument *o = arg(msg, 1, ARGUMENTDATATYPE_INTEGER);
		const GameMessageArgument *s = arg(msg, 2, ARGUMENTDATATYPE_OBJECTID);
		if (!o || !s)
		{
			++m_malformed;
			return false;
		}
		options = (unsigned)o->integer;
		sourceId = s->objectID;
		break;
	}
	case 1: // {id, location, target, options, source}
	{
		const GameMessageArgument *l = arg(msg, 1, ARGUMENTDATATYPE_LOCATION);
		const GameMessageArgument *tg = arg(msg, 2, ARGUMENTDATATYPE_OBJECTID);
		const GameMessageArgument *o = arg(msg, 3, ARGUMENTDATATYPE_INTEGER);
		const GameMessageArgument *s = arg(msg, 4, ARGUMENTDATATYPE_OBJECTID);
		if (!l || !tg || !o || !s)
		{
			++m_malformed;
			return false;
		}
		loc = l->location;
		target = tg->objectID != INVALID_ID ? logic.findObjectByID(tg->objectID) : nullptr;
		options = (unsigned)o->integer;
		sourceId = s->objectID;
		break;
	}
	case 2: // {id, target, options, source}
	{
		const GameMessageArgument *tg = arg(msg, 1, ARGUMENTDATATYPE_OBJECTID);
		const GameMessageArgument *o = arg(msg, 2, ARGUMENTDATATYPE_INTEGER);
		const GameMessageArgument *s = arg(msg, 3, ARGUMENTDATATYPE_OBJECTID);
		if (!tg || !o || !s)
		{
			++m_malformed;
			return false;
		}
		target = logic.findObjectByID(tg->objectID);
		if (!target)
		{
			return true; // RW 0x77A5FE
		}
		options = (unsigned)o->integer;
		sourceId = s->objectID;
		break;
	}
	default: // {id, options}: the issuer's spell book
	{
		const GameMessageArgument *o = arg(msg, 1, ARGUMENTDATATYPE_INTEGER);
		if (!o)
		{
			++m_malformed;
			return false;
		}
		options = (unsigned)o->integer;
		break;
	}
	}
	if (!t)
	{
		return true; // RW 0x77B9CF and the groupDo* null template exits
	}
	std::vector<Object *> group;
	if (kind == 3)
	{
		Player *issuer = logic.players().getNthPlayer(msg.getPlayerIndex());
		Object *book = issuer ? SpecialPowerModules::getSpellBookObject(logic, *issuer) : nullptr; // RW 0x6AD0F8
		if (!book)
		{
			return true;
		}
		group.push_back(book);
	}
	else if (Object *src = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr)
	{
		group.push_back(src);
	}
	else
	{
		Player *issuer = logic.players().getNthPlayer(msg.getPlayerIndex());
		if (!issuer)
		{
			return true;
		}
		for (ObjectID oid : issuer->selection())
		{
			Object *o = logic.findObjectByID(oid);
			if (o && o->getControllingPlayer() == issuer) // RW 0x756E5C filter 3 (INFERENCE: the controlled objects)
			{
				group.push_back(o);
			}
		}
	}
	for (Object *member : group)
	{
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*member, t);
		// RW 0x82DFB7 (ported part): the module, fully ready, its requirements
		if (!sp || sp->getPercentReady() < 1.0f || !sp->requirementsMet())
		{
			++m_refused;
			continue;
		}
		bool done = false;
		switch (kind)
		{
		case 1:
			done = SpecialPowerModules::doSpecialPowerAtLocation(*member, t, loc, options, false);
			break;
		case 2:
			done = SpecialPowerModules::doSpecialPowerAtObject(*member, t, target, options, false);
			break;
		default:
			done = SpecialPowerModules::doSpecialPower(*member, t, options, false);
			break;
		}
		if (done)
		{
			++m_casts;
		}
		else
		{
			++m_refused;
		}
	}
	return true;
}

// RW 0x77A8D3
bool SpellCommands::purchaseScience(GameLogic &logic, const GameMessage &msg)
{
	const GameMessageArgument *a0 = msg.getArgument(0);
	const GameMessageArgument *a1 = msg.getArgument(1);
	if (!a0 || !a1 || a0->type != ARGUMENTDATATYPE_INTEGER || a1->type != ARGUMENTDATATYPE_INTEGER)
	{
		++m_malformed;
		return false;
	}
	const ScienceType science = a1->integer;
	if (science == SCIENCE_INVALID)
	{
		return true;
	}
	if (!logic.players().getNthPlayer(msg.getPlayerIndex()))
	{
		return true; // RW 0x77A8F1: no issuing player
	}
	Player *p = logic.players().getNthPlayer(a0->integer); // RW 0x6A844E
	if (!p)
	{
		return true;
	}
	if (p->science().attemptToPurchaseScience(science))
	{
		++m_purchases;
	}
	return true;
}
