// OpenBFME. GPL-3.0.
// See GameLogic/Module/PoisonedBehavior.h for the RotWK 2.01 facts and BFME2 donor references.

#include "GameLogic/Module/PoisonedBehavior.h"

#include "Common/INI.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <algorithm>
#include <stdexcept>

namespace
{
constexpr UnsignedInt kTintStatusPoisoned = 0x04u; // RW 0x008862E0: Drawable + 0x118 bit 2 (TINT_STATUS_POISONED)
constexpr int kDamageFXPoison = 27;                // RW 0x00886408: 0x1B = TheDamageFXTypeNames[27] ("POISON")

// RW 0xC60134 (DONOR BFME2 0xC49918)
const FieldParse kPoisonedBehaviorFieldParse[] = {
	{ "PoisonDamageInterval", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(PoisonedBehaviorModuleData, m_poisonDamageInterval) },
	{ "PoisonDuration", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(PoisonedBehaviorModuleData, m_poisonDuration) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

// RW 0x008861E9 / DONOR BFME2 0x00482F7D
void PoisonedBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kPoisonedBehaviorFieldParse);
}

// RW 0x008861FA / DONOR BFME2 0x00482F7D
PoisonedBehavior::PoisonedBehavior(Thing *thing, const PoisonedBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (Object *obj = getObject())
	{
		setWakeFrame(obj, UPDATE_SLEEP_FOREVER); // RW 0x0088625C -> RW 0x00850C32
	}
}

// RW 0x00886145 (~PoisonedBehavior), RW 0x00886272 (scalar deleting destructor)
PoisonedBehavior::~PoisonedBehavior() = default;

// RW 0x0088616B / DONOR BFME2 0x00482EFF
UpdateSleepTime PoisonedBehavior::calcSleepTime() const
{
	if (m_poisonOverallStopFrame == 0u)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const UnsignedInt now = getObject()->logic().getFrame();
	if (m_poisonOverallStopFrame == now)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	// RW 0x00850BE9 (UpdateModule::frameToSleepTime): earliest of m_poisonDamageFrame, m_poisonOverallStopFrame, UPDATE_SLEEP_FOREVER
	UnsignedInt earliest = m_poisonDamageFrame;
	if (earliest > m_poisonOverallStopFrame) earliest = m_poisonOverallStopFrame;
	if (earliest > (UnsignedInt)UPDATE_SLEEP_FOREVER) earliest = (UnsignedInt)UPDATE_SLEEP_FOREVER;
	return earliest > now ? (UpdateSleepTime)(earliest - now) : UPDATE_SLEEP_NONE;
}

// RW 0x0088628E / DONOR BFME2 0x00483011
void PoisonedBehavior::startPoisonedEffects(const DamageInfo &damageInfo)
{
	Object *obj = getObject();
	if (!obj || !m_data)
	{
		return;
	}
	const UnsignedInt now = obj->logic().getFrame();
	m_poisonDamageAmount = damageInfo.m_output.m_actualDamageDealt; // RW 0x008862A3 (+0x70)
	m_poisonOverallStopFrame = now + m_data->m_poisonDuration;       // RW 0x008862A9 (+0x0C)

	const UnsignedInt nextDamageFrame = now + m_data->m_poisonDamageInterval; // RW 0x008862B1 (+0x08)
	if (m_poisonDamageFrame != 0u)
	{
		m_poisonDamageFrame = std::min(m_poisonDamageFrame, nextDamageFrame); // RW 0x008862BE..0x008862CC
	}
	else
	{
		m_poisonDamageFrame = nextDamageFrame;
	}

	m_deathType = damageInfo.m_input.m_deathType; // RW 0x008862CE (+0x1C -> +0x30)

	if (ObjectClientHooks *client = obj->logic().clientHooks())
	{
		client->setDrawableTintStatus(*obj, kTintStatusPoisoned); // RW 0x008862E0: Drawable + 0x118 |= 4
	}
	setWakeFrame(obj, calcSleepTime()); // RW 0x008862EC -> RW 0x00850C32
}

// RW 0x00886300 / DONOR BFME2 0x00483083
void PoisonedBehavior::stopPoisonedEffects()
{
	m_poisonDamageFrame = 0u;
	m_poisonOverallStopFrame = 0u;
	m_poisonDamageAmount = 0.0f;
	if (Object *obj = getObject())
	{
		if (ObjectClientHooks *client = obj->logic().clientHooks())
		{
			client->clearDrawableTintStatus(*obj, kTintStatusPoisoned); // RW 0x00886320 -> RW 0x00670913
		}
	}
}

// RW 0x00886378 / DONOR BFME2 0x004830FB
void PoisonedBehavior::onDamage(const DamageInfo &info)
{
	if (info.m_input.m_damageType == DAMAGE_POISON) // RW 0x0088637C: cmp [eax + 0x10], 0x1A
	{
		startPoisonedEffects(info);
	}
}

// RW 0x0088638E / DONOR BFME2 0x00483111
void PoisonedBehavior::onHealing(const DamageInfo &)
{
	stopPoisonedEffects();
	if (Object *obj = getObject())
	{
		setWakeFrame(obj, UPDATE_SLEEP_FOREVER); // RW 0x008863A6 -> RW 0x00850C32
	}
}

// RW 0x008863B0 / DONOR BFME2 0x00483133
UpdateSleepTime PoisonedBehavior::update()
{
	Object *obj = getObject();
	if (!obj || !m_data)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const UnsignedInt now = obj->logic().getFrame();
	if (m_poisonOverallStopFrame == 0u) // RW 0x008863C2
	{
		return UPDATE_SLEEP_FOREVER;
	}

	if (m_poisonDamageFrame != 0u && now >= m_poisonDamageFrame) // RW 0x008863D3..0x008863DC
	{
		DamageInfo dmg;
		dmg.m_input.m_sourceID = INVALID_ID;                 // RW 0x008863F1
		dmg.m_input.m_damageType = DAMAGE_UNRESISTABLE;      // RW 0x00886401 (8)
		dmg.m_input.m_damageFXOverride = kDamageFXPoison;    // RW 0x00886408 (0x1B = 27)
		dmg.m_input.m_deathType = m_deathType;               // RW 0x008863F5
		dmg.m_input.m_amount = m_poisonDamageAmount;         // RW 0x008863FC
		obj->attemptDamage(dmg);                             // RW 0x0088640F -> RW 0x698E7D
		m_poisonDamageFrame = now + m_data->m_poisonDamageInterval; // RW 0x00886414
	}

	// RW 0x0088641C..0x00886436: stop effects once overall stop frame is reached unless already dead
	if (m_poisonOverallStopFrame != 0u && now >= m_poisonOverallStopFrame && !obj->isEffectivelyDead())
	{
		stopPoisonedEffects();
	}

	return calcSleepTime(); // RW 0x0088643E
}

void PoisonedBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(m_poisonDamageFrame);
	hasher.addU32(m_poisonOverallStopFrame);
	hasher.addFloat(m_poisonDamageAmount);
	hasher.addI32(m_deathType);
}

// RW 0x00886326 / DONOR BFME2 0x004830A9. Version 2 is the native layout (UpdateModule + poison timers/damage/death type).
// Version 1 is the pre-port UnportedUpdateModule shell (UpdateModule + m_calls), which resets poison state to defaults.
void PoisonedBehavior::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	xferUpdateModule(x); // RW 0x0088632F -> RW 0x00850C60
	if (v.current < 2)
	{
		std::uint32_t legacyCalls = 0; // UnportedUpdateModule::m_calls
		x.xferUnsignedInt(&legacyCalls);
		m_poisonDamageFrame = 0;
		m_poisonOverallStopFrame = 0;
		m_poisonDamageAmount = 0.0f;
		m_deathType = DEATH_POISONED;
		return;
	}
	x.xferUnsignedInt(&m_poisonDamageFrame);
	x.xferUnsignedInt(&m_poisonOverallStopFrame);
	x.xferReal(&m_poisonDamageAmount);
	SnapshotXfer::enumIn(x, m_deathType, DEATH_NORMAL, DEATH_NUM_TYPES - 1, "PoisonedBehavior death type");
}

// RW 0x00658636, getModuleName RW 0x00886165 ("PoisonedBehavior"), getModuleNameKey RW 0x00886193
void PoisonedBehavior::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<PoisonedBehaviorModuleData>("PoisonedBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(
		"PoisonedBehavior",
		MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const PoisonedBehaviorModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("PoisonedBehavior: the module data is not typed");
			}
			return std::make_unique<PoisonedBehavior>(thing, typed);
		});
}
