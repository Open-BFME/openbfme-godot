// OpenBFME. GPL-3.0.
// Lane MODULES-1: the registration of the behaviour module classes this lane ports (ExtraUpgradeModules.h, ExtraDieModules.h, ...). Each binds the typed
// data and the runtime class of a class the binary registers (ModuleFactory::bindTypedData / bindModuleProc).

#pragma once

class ModuleFactory;

namespace ExtraModules
{
void registerAll(ModuleFactory &modules);
}
