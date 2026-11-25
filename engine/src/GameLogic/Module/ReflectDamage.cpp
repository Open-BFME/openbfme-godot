// OpenBFME. GPL-3.0.
// See GameLogic/Module/ReflectDamage.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ReflectDamage.h"
#include "GameLogic/SnapshotXfer.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
typedef ReflectDamageModuleData Data;

const INITypeFlagListSpec kDamageTypeFlags = { TheDamageNames, 1 }; // RW 0x73A5C9: 1 << (type - 1)

#define RD_OFF(member) (int)offsetof(Data, member)
const FieldParse kReflectDamageFieldParse[] = { // RW 0xC70928
	{ "DamageTypesToReflect", INI::parseTypeFlagList, &kDamageTypeFlags, RD_OFF(m_damageTypes) },
	{ "ReflectDamagePercentage", INI::parsePercentToReal, nullptr, RD_OFF(m_reflectPercentage) },
	{ "MinimumDamageToReflect", INI::parseReal, nullptr, RD_OFF(m_reflectMinimum) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RD_OFF
} // namespace

void ReflectDamageModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kReflectDamageFieldParse);
}

ReflectDamage::ReflectDamage(Thing *thing, const ReflectDamageModuleData *data)
	: BehaviorModule(thing, data)
	, m_data(data)
{
}

// RW 0x8BED43
void ReflectDamage::onDamage(const DamageInfo &info)
{
	if (info.m_input.m_damageType == DAMAGE_REFLECTED) // cmp [DamageInfo + 0x10], 0xD: what this module deals is never reflected
	{
		return;
	}
	const unsigned bit = 1u << ((unsigned)(info.m_input.m_damageType - 1) & 31u); // dec ecx; shl eax, cl
	if ((m_data->m_damageTypes & bit) == 0)
	{
		return;
	}
	Object *self = getObject();
	Object *source = self->logic().findObjectByID(info.m_input.m_sourceID); // RW 0x449681
	if (!source)
	{
		return;
	}
	DamageInfo reflected; // RW 0x66365E
	float amount = SimMath::mulf32(info.m_input.m_amount, m_data->m_reflectPercentage); // mulss: the INPUT amount (before the armour) times the percentage
	if (!(amount > m_data->m_reflectMinimum))                                          // comiss; ja
	{
		amount = m_data->m_reflectMinimum;
	}
	reflected.m_input.m_deathType = DEATH_CRUSHED;       // + 0x1C = 2
	reflected.m_input.m_damageFXOverride = 2;            // + 0x14 = 2
	reflected.m_input.m_damageType = DAMAGE_REFLECTED;   // + 0x10 = 0xD
	reflected.m_input.m_sourceID = self->getID();        // + 0x08 = Object + 0x74
	reflected.m_input.m_amount = amount;                 // + 0x20
	source->attemptDamage(reflected);                    // RW 0x698E7D
}

// RW 0x8BED43's module has no state of its own: DamageModule's version and BehaviorModule's
void ReflectDamage::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
}

void ReflectDamage::registerClass(ModuleFactory &modules)
{
	const char *name = "ReflectDamage";
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<ReflectDamage>(thing, typed);
	});
}
