// OpenBFME. GPL-3.0.
// See GameLogic/ObjectFilterMatch.h for the target facts and the addresses.

#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SnapshotXfer.h"

#include "Common/Xfer.h"

#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <stdexcept>
#include <variant>

namespace
{
bool anyBit(const KindOfMaskType &a, const KindOfMaskType &b)
{
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i] & b[i])
		{
			return true; // RW 0x661359
		}
	}
	return false;
}
bool nonEmpty(const KindOfMaskType &a)
{
	for (std::uint32_t w : a)
	{
		if (w != 0)
		{
			return true; // RW 0x6C824C
		}
	}
	return false;
}
bool subsetOf(const KindOfMaskType &super, const KindOfMaskType &sub)
{
	for (size_t i = 0; i < super.size(); ++i)
	{
		if ((sub[i] & super[i]) != sub[i])
		{
			return false; // RW 0x70B8C7 with an empty must-be-clear mask
		}
	}
	return true;
}
const std::vector<std::string> *listField(const ThingTemplate &tt, const char *name)
{
	const FieldValue *v = tt.findField(name);
	return v ? std::get_if<std::vector<std::string>>(v) : nullptr;
}
bool listHas(const std::vector<std::string> *list, const std::string &name)
{
	if (list)
	{
		for (const std::string &s : *list)
		{
			if (s == name)
			{
				return true;
			}
		}
	}
	return false;
}
bool isSName(const std::string &n)
{
	return n.size() >= 3 && n[0] == 'S' && n[1] == ':';
}
} // namespace

bool ObjectFilterMatch::isValid(const ObjectFilter *filter)
{
	return filter != nullptr && filter->flag;
}

void ObjectFilterMatch::crc(StateHasher &h, const ObjectFilter *filter)
{
	h.addBool(filter != nullptr);
	if (!filter)
	{
		return;
	}
	h.addU32((std::uint32_t)filter->includeNames.size());
	for (const std::string &s : filter->includeNames)
	{
		h.addString(s);
	}
	h.addU32((std::uint32_t)filter->excludeNames.size());
	for (const std::string &s : filter->excludeNames)
	{
		h.addString(s);
	}
	for (std::uint32_t w : filter->includeKindOf)
	{
		h.addU32(w);
	}
	for (std::uint32_t w : filter->excludeKindOf)
	{
		h.addU32(w);
	}
	h.addI32(filter->rule);
	h.addU32(filter->relationships);
	h.addBool(filter->flag);
	h.addI32(filter->side);
}

void ObjectFilterMatch::xfer(Xfer &x, std::shared_ptr<const ObjectFilter> &filter)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	bool present = filter != nullptr;
	x.xferBool(&present);
	if (!present)
	{
		filter.reset();
		return;
	}
	ObjectFilter f = filter ? *filter : ObjectFilter();
	for (std::vector<std::string> *names : { &f.includeNames, &f.excludeNames })
	{
		x.xferVectorSize(*names, 1); // a string: its length byte
		for (std::string &s : *names)
		{
			x.xferAsciiString(&s);
		}
	}
	for (KindOfMaskType *m : { &f.includeKindOf, &f.excludeKindOf })
	{
		for (std::uint32_t &w : *m)
		{
			x.xferUnsignedInt(&w);
		}
	}
	x.xferInt(&f.rule);
	x.xferUnsignedInt(&f.relationships);
	x.xferBool(&f.flag);
	x.xferInt(&f.side);
	if (x.isLoading())
	{
		// lane SAVE-4e (closed sets): the KindOf masks hold KindOf names' bits, the rule is NONE / ANY / ALL, the relationships the four, the side 0 .. 2
		for (const KindOfMaskType *m : { &f.includeKindOf, &f.excludeKindOf })
		{
			for (int bit = SnapshotXfer::kindOfCount(); bit < (int)m->size() * 32; ++bit)
			{
				if (((*m)[(size_t)bit >> 5] >> (bit & 31)) & 1u)
				{
					throw XferException("an object filter's KindOf mask holds bit " + std::to_string(bit) + ", beyond the KindOf names");
				}
			}
		}
		SnapshotXfer::checkRange(f.rule, ObjectFilter::RULE_NONE, ObjectFilter::RULE_ALL, "an object filter's rule");
		SnapshotXfer::checkMask(f.relationships, 0xF, "an object filter's relationships", "the four relationships");
		SnapshotXfer::checkRange(f.side, ObjectFilter::SIDE_ANY, ObjectFilter::SIDE_GOOD, "an object filter's side");
		filter = std::make_shared<const ObjectFilter>(f);
	}
}

bool ObjectFilterMatch::isEquivalentTo(const ThingTemplate *a, const ThingTemplate *b)
{
	if (!a || !b)
	{
		return false;
	}
	if (a == b || a->getFinalOverride() == b->getFinalOverride())
	{
		return true;
	}
	const std::vector<std::string> *aEq = listField(*a, "EquivalentTo");
	const std::vector<std::string> *bEq = listField(*b, "EquivalentTo");
	if (listHas(aEq, b->getName()))
	{
		return true;
	}
	if (bEq)
	{
		for (const std::string &s : *bEq)
		{
			if (s == a->getName() || listHas(aEq, s))
			{
				return true;
			}
		}
	}
	if (listHas(listField(*a, "BuildVariations"), b->getName()))
	{
		return true;
	}
	return listHas(listField(*b, "BuildVariations"), a->getName());
}

bool ObjectFilterMatch::allows(GameLogic &logic, const ObjectFilter &f, const ThingTemplate *tmpl, const Player *a, const Player *b)
{
	if (!tmpl)
	{
		return false;
	}
	// 1. side
	if (f.side != ObjectFilter::SIDE_ANY)
	{
		const PlayerTemplate *pt = a ? a->getPlayerTemplate() : nullptr;
		if (!pt)
		{
			return false; // retail dereferences the player's template here
		}
		const bool evil = pt->m_evil;
		if (f.side == ObjectFilter::SIDE_EVIL ? !evil : evil)
		{
			return false;
		}
	}
	// 2. relationships
	if (f.relationships != 0)
	{
		if (!a || !b)
		{
			return false;
		}
		const Relationship rel = b->getRelationship(a->getDefaultTeam());
		bool ok = false;
		if (rel == ENEMIES)
		{
			ok = (f.relationships & ObjectFilter::REL_ENEMIES) != 0;
		}
		else if (rel == NEUTRAL)
		{
			ok = (f.relationships & ObjectFilter::REL_NEUTRAL) != 0;
		}
		else if (rel == ALLIES)
		{
			if (f.relationships & ObjectFilter::REL_ALLIES)
			{
				ok = true;
			}
			else
			{
				ok = b->getPlayerIndex() == a->getPlayerIndex() && (f.relationships & ObjectFilter::REL_SAME_PLAYER) != 0;
			}
		}
		if (!ok)
		{
			return false;
		}
	}
	const ThingFactory &things = logic.things();
	// 3. the S: names
	for (const std::string &n : f.includeNames)
	{
		if (isSName(n) && n.compare(2, std::string::npos, tmpl->getName()) == 0)
		{
			return true;
		}
	}
	for (const std::string &n : f.excludeNames)
	{
		if (isSName(n) && n.compare(2, std::string::npos, tmpl->getName()) == 0)
		{
			return false;
		}
	}
	// 4. the other names (templates, equivalence)
	for (const std::string &n : f.includeNames)
	{
		if (!isSName(n) && isEquivalentTo(tmpl, things.findTemplate(n)))
		{
			return true;
		}
	}
	for (const std::string &n : f.excludeNames)
	{
		if (!isSName(n) && isEquivalentTo(tmpl, things.findTemplate(n)))
		{
			return false;
		}
	}
	// 5. the exclude mask
	const KindOfMaskType &kindOf = logic.templateInfo(tmpl->getFinalOverride()).kindOf;
	if (nonEmpty(f.excludeKindOf) && anyBit(f.excludeKindOf, kindOf))
	{
		return false;
	}
	// 6. the rule
	switch (f.rule)
	{
	case ObjectFilter::RULE_NONE:
		return nonEmpty(f.includeKindOf) && subsetOf(kindOf, f.includeKindOf);
	case ObjectFilter::RULE_ANY:
		return nonEmpty(f.includeKindOf) && anyBit(f.includeKindOf, kindOf);
	default:
		return true; // RULE_ALL, and rule 0 (retail rewrites it to 3)
	}
}

bool ObjectFilterMatch::allows(GameLogic &logic, const ObjectFilter &f, const Object &obj, const Player *other)
{
	return allows(logic, f, obj.getTemplate(), obj.getControllingPlayer(), other);
}

std::vector<std::string> ObjectFilterMatch::unresolvedNames(const ThingFactory &things, const ObjectFilter &f)
{
	std::vector<std::string> missing;
	for (const std::vector<std::string> *list : { &f.includeNames, &f.excludeNames })
	{
		for (const std::string &n : *list)
		{
			const std::string name = isSName(n) ? n.substr(2) : n;
			if (!things.findTemplate(name))
			{
				missing.push_back(n);
			}
		}
	}
	return missing;
}

// lane MODCOMPAT-MOD1: RW 0x76379C (see the header)
bool ObjectFilterMatch::allowsKindOf(const ObjectFilter &f, const KindOfMaskType &kindOf)
{
	if (f.side != ObjectFilter::SIDE_ANY)
	{
		throw std::logic_error("ObjectFilter: a side filter asked about a KindOf mask without a player (RW 0x76379C reads the null player's template, + 0x34)");
	}
	if (f.relationships != 0) // RW 0x763849: no players
	{
		return false;
	}
	if (nonEmpty(f.excludeKindOf) && anyBit(f.excludeKindOf, kindOf)) // RW 0x76389D
	{
		return false;
	}
	switch (f.rule) // RW 0x7638B6
	{
	case ObjectFilter::RULE_NONE:
		return nonEmpty(f.includeKindOf) && subsetOf(kindOf, f.includeKindOf);
	case ObjectFilter::RULE_ANY:
		return nonEmpty(f.includeKindOf) && anyBit(f.includeKindOf, kindOf);
	default:
		return true; // RULE_ALL, and rule 0 (retail rewrites it to 3)
	}
}
