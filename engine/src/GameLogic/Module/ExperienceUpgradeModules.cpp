// OpenBFME. GPL-3.0.
// See GameLogic/Module/ExperienceUpgradeModules.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ExperienceUpgradeModules.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const FieldParse kLevelUp[] = {
	{ "LevelsToGain", INI::parseInt, nullptr, (int)offsetof(LevelUpUpgradeModuleData, m_levelsToGain) },
	{ "LevelCap", INI::parseInt, nullptr, (int)offsetof(LevelUpUpgradeModuleData, m_levelCap) },
	{ nullptr, nullptr, nullptr, 0 },
};
const FieldParse kExperienceScalar[] = {
	{ "AddXPScalar", INI::parseReal, nullptr, (int)offsetof(ExperienceScalarUpgradeModuleData, m_addXPScalar) },
	{ nullptr, nullptr, nullptr, 0 },
};

template <class Runtime, class Data>
void bindRuntime(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}
} // namespace

void LevelUpUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kLevelUp);
}

void ExperienceScalarUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kExperienceScalar);
}

unsigned long long &LevelUpUpgrade::hordeSlotCalls()
{
	static unsigned long long n = 0;
	return n;
}

void LevelUpUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	ExperienceTracker *t = obj->getExperienceTracker();
	if (!t)
	{
		return;
	}
	const int room = m_data->m_levelCap - t->getRank();
	const int n = m_data->m_levelsToGain < room ? m_data->m_levelsToGain : room; // RW 0x8B7FB5 (jl: the smaller one)
	if (n >= 1)
	{
		t->gainExpForLevel(n, true, false);
	}
	Object *horde = obj->getHordeObject(false);
	if (horde && horde->getContain() && horde->getContain()->getHordeContainInterface() && n > 0)
	{
		hordeSlotCalls() += (unsigned long long)n; // RW 0x8B7FE2: HordeContain slot 0xC4, not identified (S-635)
	}
}

void ExperienceScalarUpgrade::upgradeImplementation()
{
	if (ExperienceTracker *t = getObject()->getExperienceTracker())
	{
		t->setExperienceScalar(SimMath::addf32(m_data->m_addXPScalar, t->getExperienceScalar()));
	}
}

void ExperienceScalarUpgrade::processUpgradeRemoval()
{
	if (ExperienceTracker *t = getObject()->getExperienceTracker())
	{
		t->setExperienceScalar(SimMath::subf32(t->getExperienceScalar(), m_data->m_addXPScalar));
	}
}

void ExperienceUpgradeModules::registerAll(ModuleFactory &modules)
{
	bindRuntime<LevelUpUpgrade, LevelUpUpgradeModuleData>(modules, "LevelUpUpgrade");
	bindRuntime<ExperienceScalarUpgrade, ExperienceScalarUpgradeModuleData>(modules, "ExperienceScalarUpgrade");
}
