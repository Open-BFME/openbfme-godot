// OpenBFME. GPL-3.0.
//
// The runtime module classes lane LOGIC-1 ports (spec: the modules the creation path needs): ActiveBody (typed data + class) and
// HordeContain / HorseHordeContain (class; the typed data is MapHordeSpawn::bindHordeContainData). Every other registered class stays an
// UnportedModule (stop S-140). Call after ModuleFactory::init() and the typed data bindings.

#pragma once

class ModuleFactory;

namespace LogicModules
{
// Pass the mounted data mode so BFME1-only runtimes do not alter the RotWK registry.
void registerAll(ModuleFactory &modules, bool bfme1Data = false);
}
