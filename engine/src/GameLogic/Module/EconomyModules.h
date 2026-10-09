// OpenBFME. GPL-3.0.
//
// Registers the economy modules (lane ECON-1) with the ModuleFactory: the typed data of TerrainResourceBehavior, AutoDepositUpdate, CommandPointsUpgrade,
// CostModifierUpgrade, RefundDie, PillageModule and SalvageCrateCollide, and the runtime class of each but the last. Every other economy class of the binary's registry
// (SupplyCenterCreate, SupplyCenterDockUpdate, SupplyCenterProductionExitUpdate, WorkerAIUpdate, HordeWorkerAIUpdate) is a leftover of Zero Hour's supply system that no
// retail skirmish object reaches; they stay unported modules (stop S-140).

#pragma once

class ModuleFactory;

namespace EconomyModules
{
void registerAll(ModuleFactory &modules);
}
