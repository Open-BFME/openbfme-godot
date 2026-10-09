// OpenBFME. GPL-3.0.
// See GameLogic/Module/HitReactionBehavior.h for the target facts and the stop S-1024.

#include "GameLogic/Module/HitReactionBehavior.h"

#include "Common/AsciiString.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kStop =
	"[S-1024] HitReactionBehavior: ported (RW 0x85C30F; the drawable status bit Drawable + 0x26C bit 12 is the drawable's synced copy of the model condition "
	"HIT_REACTION, RW 0x679512 / 0x68B53C, read here on the object); not identified: the interface RW 0x68C409 finds (behavior slot 0x84) and its slot 8(0) "
	"after a reaction (noted); the drawable refresh RW 0x67449C is the client's; every object counts as having a drawable (RW 0x628882 makes one for each)";

#define HR_OFF(field) (int)offsetof(HitReactionBehaviorModuleData, field)

// RW 0xC57920
const FieldParse kParse[] = {
	{ "HitReactionLifeTimer1", INI::parseDurationUnsignedInt, nullptr, HR_OFF(m_lifeTimer[0]) },
	{ "HitReactionLifeTimer2", INI::parseDurationUnsignedInt, nullptr, HR_OFF(m_lifeTimer[1]) },
	{ "HitReactionLifeTimer3", INI::parseDurationUnsignedInt, nullptr, HR_OFF(m_lifeTimer[2]) },
	{ "HitReactionThreshold1", INI::parseReal, nullptr, HR_OFF(m_threshold[0]) },
	{ "HitReactionThreshold2", INI::parseReal, nullptr, HR_OFF(m_threshold[1]) },
	{ "HitReactionThreshold3", INI::parseReal, nullptr, HR_OFF(m_threshold[2]) },
	{ "FastHitsResetReaction", INI::parseBool, nullptr, HR_OFF(m_fastHitsResetReaction) },
	{ "HitsParalyze", INI::parseBool, nullptr, HR_OFF(m_hitsParalyze) },
	{ nullptr, nullptr, nullptr, 0 }
};

int conditionBit(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(TheModelConditionNames[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}

struct Bits
{
	int reaction = conditionBit("HIT_REACTION");                                                              // 172
	int level[3] = { conditionBit("HIT_LEVEL_1"), conditionBit("HIT_LEVEL_2"), conditionBit("HIT_LEVEL_3") }; // 173 .. 175
};
const Bits &bits()
{
	static const Bits b;
	return b;
}

constexpr unsigned kDisabledParalyzed = 4; // RW 0x6907F1(4, frame)
} // namespace

void HitReactionBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kParse);
}

HitReactionBehavior::HitReactionBehavior(Thing *thing, const HitReactionBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x85C2DD
}

std::vector<std::string> HitReactionBehavior::stopLines()
{
	return { kStop };
}

void HitReactionBehavior::clearReaction(Object &obj)
{
	// RW 0x85C38B .. 0x85C3E5: + 0x121 bits 4 .. 7 = HIT_REACTION, HIT_LEVEL_1 .. 3, each cleared when set (RW 0x68B53C after each)
	const Bits &b = bits();
	const int all[4] = { b.reaction, b.level[0], b.level[1], b.level[2] };
	for (int bit : all)
	{
		if (bit >= 0 && obj.testModelCondition(bit))
		{
			obj.setModelConditionState(bit, false);
		}
	}
}

UpdateSleepTime HitReactionBehavior::update()
{
	// RW 0x85C30F
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	BodyModuleInterface *body = obj->getBodyModule();
	if (!ai || !body)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	GameLogic &logic = obj->logic();
	const Bits &b = bits();
	const float health = body->getHealth(); // body slot 0x10
	// the drawable's flag (Drawable + 0x26C bit 12): its synced copy of HIT_REACTION
	auto shown = [&]() { return b.reaction >= 0 && obj->testModelCondition(b.reaction); };
	bool skip = false;
	if (0 < m_countdown)
	{
		--m_countdown;
		if (m_countdown < 1 || obj->isEffectivelyDead() || !ai->isIdle())
		{
			if (shown())
			{
				clearReaction(*obj);
			}
			m_countdown = 0;
		}
		skip = !m_data->m_fastHitsResetReaction;
	}
	if (!skip && (ai->isIdle() || m_data->m_hitsParalyze) && health < m_lastHealth)
	{
		if (shown())
		{
			clearReaction(*obj);
		}
		const float lost = SimMath::sseSub(m_lastHealth, health);
		int level = -1;
		if (!(lost < m_data->m_threshold[2]))
		{
			level = 2;
		}
		else if (!(lost < m_data->m_threshold[1]))
		{
			level = 1;
		}
		else if (!(lost < m_data->m_threshold[0]))
		{
			level = 0;
		}
		if (level >= 0)
		{
			m_countdown = (int)m_data->m_lifeTimer[level];
			if (0 < m_countdown && !shown())
			{
				if (b.reaction >= 0)
				{
					obj->setModelConditionState(b.reaction, true); // RW 0x85C51E: + 0x120 bit 12
				}
				if (b.level[level] >= 0)
				{
					obj->setModelConditionState(b.level[level], true); // bit 13 / 14 / 15
				}
				logic.noteStop("[S-1024] HitReactionBehavior: the interface RW 0x68C409 finds (behavior slot 0x84) is not identified: its slot 8(0) after a reaction is not run");
				if (m_data->m_hitsParalyze)
				{
					obj->setDisabled(kDisabledParalyzed, logic.getFrame() + (UnsignedInt)m_countdown); // RW 0x85C57B
				}
			}
		}
	}
	m_lastHealth = health;
	return obj->isEffectivelyDead() ? UPDATE_SLEEP_FOREVER : UPDATE_SLEEP_NONE;
}

void HitReactionBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addI32(m_countdown);
	h.addFloat(m_lastHealth);
}

void HitReactionBehavior::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<HitReactionBehaviorModuleData>("HitReactionBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("HitReactionBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const HitReactionBehaviorModuleData *typed = dynamic_cast<const HitReactionBehaviorModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("HitReactionBehavior: the module data is not typed");
		}
		return std::make_unique<HitReactionBehavior>(thing, typed);
	});
}
