// OpenBFME. GPL-3.0.
// See GameLogic/Module/ClickReactionBehavior.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ClickReactionBehavior.h"

#include "Common/AsciiString.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/PlayerCommands.h"
#include "GameLogic/SnapshotXfer.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace
{
typedef ClickReactionBehaviorModuleData Data;

// RW 0xC57B28
const FieldParse kFields[] = {
	{ "ClickReactionTimer", INI::parseInt, nullptr, (int)offsetof(Data, m_clickReactionTimer) },
	{ "ReactionFrames1", INI::parseInt, nullptr, (int)offsetof(Data, m_reactionFrames[0]) },
	{ "ReactionFrames2", INI::parseInt, nullptr, (int)offsetof(Data, m_reactionFrames[1]) },
	{ "ReactionFrames3", INI::parseInt, nullptr, (int)offsetof(Data, m_reactionFrames[2]) },
	{ "ReactionFrames4", INI::parseInt, nullptr, (int)offsetof(Data, m_reactionFrames[3]) },
	{ "ReactionFrames5", INI::parseInt, nullptr, (int)offsetof(Data, m_reactionFrames[4]) },
	{ nullptr, nullptr, nullptr, 0 }
};

int nameIndex(const char *const *names, const char *name)
{
	for (int i = 0; names[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(names[i], name) == 0)
		{
			return i;
		}
	}
	throw std::logic_error(std::string("ClickReactionBehavior: the binary's name table lacks ") + name);
}

struct Bits
{
	int rampageAnimationOnly = nameIndex(TheModelConditionNames, "RAMPAGE_ANIMATION_ONLY"); // 162
	int hitReaction = nameIndex(TheModelConditionNames, "HIT_REACTION");                    // 172
	int react[6] = { nameIndex(TheModelConditionNames, "REACT_1"), nameIndex(TheModelConditionNames, "REACT_2"), nameIndex(TheModelConditionNames, "REACT_3"),
		nameIndex(TheModelConditionNames, "REACT_4"), nameIndex(TheModelConditionNames, "REACT_5"), nameIndex(TheModelConditionNames, "REACT_6") }; // 164 .. 169
	int rampage = nameIndex(TheWeaponConditionNames, "RAMPAGE");          // weapon set flag 8
	int unselectable = nameIndex(TheObjectStatusNames, "UNSELECTABLE");   // status 3
	int horde = nameIndex(TheKindOfNames, "HORDE");                       // 109
	int structure = nameIndex(TheKindOfNames, "STRUCTURE");               // 7
};
const Bits &bits()
{
	static const Bits b;
	return b;
}

bool weaponSetFlag(const Object &obj, int bit)
{
	const WeaponConditionFlags &f = obj.getWeaponSetFlags();
	return bit >= 0 && ((f[(size_t)bit >> 5] >> (bit & 31)) & 1u) != 0;
}

const char kFile[] = "ClickReactionBehavior.cpp"; // RW 0xC57B98 (the retail source path's file)
} // namespace

void ClickReactionBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void ClickReactionBehavior::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "ClickReactionBehavior";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<ClickReactionBehavior>(thing, typed);
	});
}

// RW 0x85C682: + 0x24 = + 0x28 = 0; wake next frame
ClickReactionBehavior::ClickReactionBehavior(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1));
}

// RW 0x68C409
ClickReactionBehavior *ClickReactionBehavior::find(Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (ClickReactionBehavior *c = dynamic_cast<ClickReactionBehavior *>(m.get()))
		{
			return c;
		}
	}
	return nullptr;
}

// RW 0x6937C0
bool ClickReactionBehavior::selectableForClick(const Object &obj)
{
	const Bits &b = bits();
	if (obj.getProducerID() != INVALID_ID) // + 0x78
	{
		const Object *producer = obj.logic().findObjectByID(obj.getProducerID());
		if (producer && producer->isKindOf((unsigned)b.horde) && obj.testStatus((unsigned)b.unselectable)) // RW 0x44DDEC(3) on the object (+ 0x94)
		{
			return false;
		}
	}
	return PlayerCommands::isSelectable(obj) && !obj.isKindOf((unsigned)b.structure); // RW 0x68DE58, template + 0x108 bit 7
}

// RW 0x85C75E
void ClickReactionBehavior::onClick()
{
	Object *obj = getObject();
	const Bits &b = bits();
	if (weaponSetFlag(*obj, b.rampage) || obj->testModelCondition(b.rampageAnimationOnly) || obj->testModelCondition(b.hitReaction))
	{
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface(); // + 0x260
	if (!ai || !ai->isIdle())                            // AI slot 0x1B8
	{
		return;
	}
	const int timer = m_data->m_clickReactionTimer;
	if (timer <= 0)
	{
		return;
	}
	m_clicks = (int)((unsigned)m_clicks + (unsigned)timer);           // add [ecx], edi (32-bit wrap as retail)
	m_clicks = std::min((int)((unsigned)timer * 5u), m_clicks); // RW 0x85C7EA .. 0x85C804: imul, min
	if (m_countdown > 0)
	{
		return;
	}
	int reaction = m_clicks / timer; // idiv
	if (reaction <= 3)
	{
		reaction = obj->logic().random().getValue(1, 3, kFile, 116); // RW 0x6D328E, line 0x74
	}
	if (reaction <= 0)
	{
		return;
	}
	const int bit = b.react[reaction >= 1 && reaction <= 5 ? reaction - 1 : 4]; // RW 0x85C833 .. 0x85C855: 1 .. 5 -> REACT_n, else REACT_5
	if (!obj->testModelCondition(bit))
	{
		obj->setModelConditionState(bit, true); // + 0x120 bit, RW 0x68B53C
	}
	obj->flushDrawableModelConditions(); // RW 0x67449C(0): the client's
	if (reaction - 1 >= 0 && reaction - 1 < 5)
	{
		m_countdown = m_data->m_reactionFrames[reaction - 1];
	}
}

// RW 0x85C8C9
void ClickReactionBehavior::clearReaction(bool apply)
{
	Object *obj = getObject();
	m_countdown = 0;
	Object::ModelConditionBits clear{}, none{};
	for (int bit : bits().react)
	{
		clear[(size_t)bit >> 5] |= 1u << (bit & 31); // mask 0x3F0 of word 5
	}
	obj->clearAndSetModelConditionFlags(clear, none); // RW 0x5E3B79
	if (apply)
	{
		obj->flushDrawableModelConditions(); // RW 0x67449C(0)
	}
}

// RW 0x85C704
UpdateSleepTime ClickReactionBehavior::update()
{
	AIUpdateInterface *ai = getObject()->getAIUpdateInterface();
	if (!ai)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (m_clicks > 0)
	{
		--m_clicks;
	}
	if (m_countdown > 0)
	{
		--m_countdown;
		if (m_countdown == 0 || !ai->isIdle())
		{
			clearReaction(true); // click interface slot 2 (1); it also zeroes the countdown
		}
	}
	return UPDATE_SLEEP_NONE;
}

void ClickReactionBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addI32(m_clicks);
	hasher.addI32(m_countdown);
}

// RW 0x85C5DE (retail's layout)
void ClickReactionBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	x.xferInt(&m_clicks);
	x.xferInt(&m_countdown);
}
