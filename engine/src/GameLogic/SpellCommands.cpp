// OpenBFME. GPL-3.0.
// See GameLogic/SpellCommands.h. Lane SPELL-1.

#include "GameLogic/SpellCommands.h"

#include "Common/ModEngineFeatures.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
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
// lane MODS-3: edain-unchained.multi-execute-gate (its 0xEE3000, wrapping the ActionManager tests RW 0x82D5DA / 0x82D925 in the member loops of
// AIGroup::groupDoSpecialPower RW 0x76F6DA and groupDoSpecialPowerAtObject RW 0x770B67): the member's base command set (RW 0x69156B, 0x71EFA2; none:
// allowed), its first SPECIAL_POWER button (slots 0 .. 63) whose power is this one; RW 0x942490 answering 3 for it (a DisableOnModelCondition the member
// holds, or an EnableOnModelCondition it holds none of) refuses the member
bool multiExecuteGateAllows(const Object &member, const SpecialPowerTemplate &power)
{
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(member.getCommandSetName()) : nullptr;
	if (!set)
	{
		return true;
	}
	for (int i = 0; i < CommandSet::STORAGE; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		if (!b || b->m_command != GUI_COMMAND_SPECIAL_POWER || b->m_specialPowerName.empty())
		{
			continue;
		}
		const SpecialPowerTemplate *bp = TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName); // RW 0x688D3C: the final override
		if (!bp || bp->getID() != power.getID())
		{
			continue;
		}
		bool needs = false, enabled = false, disabled = false;
		for (size_t w = 0; w < b->m_enableOnModelCondition.size(); ++w)
		{
			const auto bits = member.getModelConditionBits()[w];
			needs = needs || b->m_enableOnModelCondition[w] != 0;
			enabled = enabled || (bits & b->m_enableOnModelCondition[w]) != 0;
			disabled = disabled || (bits & b->m_disableOnModelCondition[w]) != 0;
		}
		return !disabled && (!needs || enabled);
	}
	return true;
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
		if (!sp || sp->getPercentReady() < 1.0f || !sp->requirementsMet() ||
			((kind == 0 || kind == 2) && t && ModEngineFeatures::has(ModEngineFeature::EdainUnchainedMultiExecuteGate) && !multiExecuteGateAllows(*member, *t)))
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
