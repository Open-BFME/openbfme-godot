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
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

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
	std::vector<Object *> group;
	bool fromSelection = false;
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
	else if (sourceId != INVALID_ID && logic.findObjectByID(sourceId))
	{
		// OpenBFME divergence (Sol review HEROCAST-1 r1, as DEVPORT-7e's GameLogicDispatch::issuerObject): RW takes the named object as it is; a source the
		// issuing player does not control drops the message, so another player cannot reach the object's AI (+ 0x48 ends a running ability, RW 0x854F0B)
		Object *src = GameLogicDispatch::issuerObject(logic, msg, sourceId);
		if (!src)
		{
			++m_foreignSources;
			return true;
		}
		group.push_back(src);
	}
	else
	{
		Player *issuer = logic.players().getNthPlayer(msg.getPlayerIndex());
		if (!issuer)
		{
			return true;
		}
		fromSelection = true;
		for (ObjectID oid : issuer->selection())
		{
			Object *o = logic.findObjectByID(oid);
			if (o && o->getControllingPlayer() == issuer) // RW 0x756E5C filter 3 (INFERENCE: the controlled objects)
			{
				group.push_back(o);
			}
		}
	}
	// lane HEROCAST-1: the command source each case passes to its groupDo* (RW 0x77A4C4 / 0x77A4F2 / 0x77B8D2: 2 for MSG_DO_SPECIAL_POWER and the spell book;
	// RW 0x77A58E: 2 for _AT_LOCATION from the message's source object, RW 0x77A5C1: 0 from the selection; RW 0x77A69A / 0x77A6DB: 0 for _AT_OBJECT)
	const CommandSourceType source = kind == 2 || (kind == 1 && fromSelection) ? CMD_FROM_PLAYER : CMD_FROM_AI;
	castWithGroup(logic, group, kind, t, loc, target, options, source);
	return true;
}

namespace
{
// RW 0x403720 Coord3D::GetLengthEstimate2D: |the larger| + 0.25 * |the smaller| (x87)
float lengthEstimate2D(float x, float y)
{
	const float ax = SimMath::absD(x), ay = SimMath::absD(y);
	const float big = ax <= ay ? ay : ax, small = ax <= ay ? ax : ay;
	return SimMath::pc24Add(SimMath::pc24Mul(small, 0.25f), big);
}
} // namespace

// lane HEROCAST-1: AIGroup::groupDoSpecialPower (RW 0x76F5DB), groupDoSpecialPowerAtLocation (RW 0x76F774) and groupDoSpecialPowerAtObject (RW 0x77097E)
// (see the header; tier B with BFME2 0x76D869 / 0x76DA02 / 0x76E658, the decomp AIGroup_groupDoSpecialPower*.cpp; pair_diff: only Object field offsets
// differ; the Ghidra listing of each RotWK body agrees)
void SpellCommands::castWithGroup(GameLogic &logic, const std::vector<Object *> &group, int kind, const SpecialPowerTemplate *t, const Coord3D &loc,
	Object *target, unsigned options, CommandSourceType source)
{
	static const int kHero = ObjectTemplateInfoBuilder::kindOfIndex("HERO"); // template + 0x113 bit 2: KindOf 90
	const bool multiExecute = (options & COMMAND_OPTION_OK_FOR_MULTI_EXECUTE) != 0;
	// the senior hero: the HERO member with the highest current level rank, the lower ObjectID on a tie; a hero without a valid level is no candidate (and
	// _AT_OBJECT leaves it out of its member list too). OK_FOR_MULTI_EXECUTE skips the choice in groupDoSpecialPower and _AT_OBJECT, not in _AT_LOCATION
	Object *best = nullptr;
	int bestRank = 0;
	std::vector<Object *> members;
	std::vector<float> distances;
	for (Object *o : group)
	{
		if (kHero >= 0 && (kind == 1 || !multiExecute) && MaskTest(logic.templateInfo(o->getTemplate()).kindOf, (unsigned)kHero))
		{
			const ExperienceTracker *xp = o->getExperienceTracker();
			int rank = 0;
			// INFERENCE: RW dereferences the tracker (+0x26C) unchecked; a missing one counts as no level
			if (xp && xp->currentLevelRank(rank))
			{
				if (!best || rank > bestRank || (rank == bestRank && o->getID() < best->getID()))
				{
					best = o;
					bestRank = rank;
				}
			}
			else if (kind == 2)
			{
				continue; // RW 0x770A0D: _AT_OBJECT does not list it
			}
		}
		if (kind != 2)
		{
			members.push_back(o);
			continue;
		}
		if (best)
		{
			continue; // RW 0x770A4B: once a hero is chosen no member is listed
		}
		// _AT_OBJECT: the members in order of their 2D distance estimate to the target (SSE subtraction), a member after the ones at its distance
		float d = 0.0f;
		if (target)
		{
			const Coord3D &tp = *target->getPosition(), &op = *o->getPosition();
			d = lengthEstimate2D(SimMath::subf32(tp.x, op.x), SimMath::subf32(tp.y, op.y));
		}
		size_t at = 0;
		while (at < distances.size() && !(d < distances[at]))
		{
			++at;
		}
		members.insert(members.begin() + (std::ptrdiff_t)at, o);
		distances.insert(distances.begin() + (std::ptrdiff_t)at, d);
	}
	// one member's turn: true when it cast
	auto turn = [&](Object &member, bool gated) -> bool {
		if (AIUpdateInterface *ai = member.getAIUpdateInterface())
		{
			if (source == CMD_FROM_PLAYER && ai->preventPlayerCommands())
			{
				return false; // AI + 0x3C5 (PreventPlayerCommands)
			}
			ai->setLastCommandSource(source); // AI + 0x48
		}
		if (kind == 1 && source == CMD_FROM_PLAYER)
		{
			member.abortAbilitiesInUse(); // RW 0x693919 (_AT_LOCATION only)
		}
		if (!t)
		{
			return false;
		}
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(member, t);
		// the ActionManager test (RW 0x82D5DA / 0x82DFB7 / 0x82D925, ported part): the module, fully ready, its requirements. The mod gate wraps the member
		// loops' tests only (RW 0x76F6DA / 0x770B67), not the hero's
		if (!sp || sp->getPercentReady() < 1.0f || !sp->requirementsMet() ||
			(gated && (kind == 0 || kind == 2) && ModEngineFeatures::has(ModEngineFeature::EdainUnchainedMultiExecuteGate) && !multiExecuteGateAllows(member, *t)))
		{
			++m_refused;
			return false;
		}
		bool done = false;
		switch (kind)
		{
		case 1:
			done = SpecialPowerModules::doSpecialPowerAtLocation(member, t, loc, options, false);
			break;
		case 2:
			done = SpecialPowerModules::doSpecialPowerAtObject(member, t, target, options, false);
			break;
		default:
			done = SpecialPowerModules::doSpecialPower(member, t, options, false);
			break;
		}
		// then friend_setUndetectedDefector(false) (RW 0x68B303): no defection is ported, the bit is never set (S-141)
		if (done)
		{
			++m_casts;
		}
		else
		{
			++m_refused;
		}
		return done;
	};
	if (best)
	{
		turn(*best, false); // only the hero is tried: skipped or refused, nobody casts
		return;
	}
	for (Object *member : members)
	{
		if (turn(*member, true) && kind == 0 && !multiExecute)
		{
			return; // groupDoSpecialPower: without OK_FOR_MULTI_EXECUTE the first member that casts ends it
		}
	}
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
