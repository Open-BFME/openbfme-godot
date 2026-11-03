// OpenBFME. GPL-3.0.
// See GameLogic/Module/LogicModules.h.

#include "GameLogic/Module/AttachUpdate.h"
#include "GameLogic/Module/AISpecialPowerUpdate.h"
#include "GameLogic/Module/RepairSpecialPower.h"
#include "GameLogic/Module/GrabPassengerSpecialPower.h"
#include "GameLogic/Module/HordeDispatchSpecialPower.h"
#include "GameLogic/Module/ObjectCreationUpgrade.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpawnUnitBehavior.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/LargeGroupAudioUpdate.h"
#include "GameClient/ClientBehaviorModules.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Module/ExperienceUpgradeModules.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Module/BannerCarrierUpdate.h"
#include "GameLogic/Module/ExtraModules.h"
#include "GameLogic/Module/TransitionDamageFX.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/AreaScanModules.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/HitReactionBehavior.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/PortalModules.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameClient/MapHordeSpawn.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CombatModules.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ObjectCreationUpgrade.h"
#include "GameLogic/Module/ReplaceSelfUpgrade.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/WallSpan.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/DockUpdate.h"
#include "GameLogic/Module/SupplyCenterCreate.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/ProductionExitModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/Module/EconomyModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameLogic/Module/SiegeModules.h"
#include "GameLogic/Module/PickupStuffUpdate.h"
#include "GameLogic/Module/AutoPickUpUpdate.h"
#include "GameLogic/Module/BoredUpdate.h"

#include <stdexcept>

void LogicModules::registerAll(ModuleFactory &modules)
{
	// Only its S-207 offer is ported; keep the runtime's unported registration.
	modules.bindTypedData<SpawnUnitBehaviorModuleData>("SpawnUnitBehavior", MODULETYPE_BEHAVIOR);
	modules.bindTypedData<ActiveBodyModuleData>("ActiveBody", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("ActiveBody", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const ActiveBodyModuleData *typed = dynamic_cast<const ActiveBodyModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("ActiveBody: the module data is not typed (ActiveBodyModuleData)");
		}
		return std::make_unique<ActiveBody>(thing, typed);
	});
	MapHordeSpawn::bindHordeContainData(modules); // the typed HordeContain data (also bound by RetailObjectWorld: binding twice is harmless)
	HordeContain::registerClasses(modules);
	QueueProductionExitUpdate::registerClass(modules); // lane PROD-1
	ProductionUpdate::registerClass(modules);
	DefaultProductionExitUpdate::registerClass(modules);
	SupplyCenterProductionExitUpdate::registerClass(modules);
	SpawnPointProductionExitUpdate::registerClass(modules);
	AIUpdateInterface::registerClasses(modules); // MOVE-1: every AI module class of the registry runs the AI (S-222)
	EconomyModules::registerAll(modules); // lane ECON-1
	DozerAIUpdate::registerClass(modules); // lane BUILD-1 (after the AI classes: it replaces the base AI's binding of DozerAIUpdate)
	GiantBirdAIUpdate::registerClass(modules); // lane FLY-1 (after the AI classes: it replaces the base AI's binding of GiantBirdAIUpdate)
	DozerAIUpdate::registerWorkerClass(modules); // lane BUILD-2 / LUMBER1: WorkerAIUpdate, the dozer half and the supply half
	SupplyCenterCreate::registerClass(modules);     // lane LUMBER1: the lumber mills' registration with the players' ResourceGatheringManager
	SupplyCenterDockUpdate::registerClass(modules); // lane LUMBER1: the lumber mills' dock (the income of a delivery)
	ConstructionModules::registerAll(modules);
	WallSpan::registerModules(modules); // lane BUILD-2: WallHubBehavior
	CastleModules::registerAll(modules);
	CombatModules::registerAll(modules);  // lane COMBAT-1: ImmortalBody, HighlanderBody and the die modules
	ProjectileModules::registerAll(modules); // lane PROJ-1: BezierProjectileBehavior
	StructureModules::registerAll(modules); // lane COMBAT-2: StructureBody, InactiveBody, StructureCollapseUpdate
	UpgradeModuleClasses::registerAll(modules); // lane UPGRADE-1: the upgrade module classes
	ObjectCreationUpgrade::registerClass(modules); // lane FACT-A: the object an upgrade makes (the Crystal Moat)
	ReplaceSelfUpgrade::registerClass(modules);    // lane FACT-A: the wall segment's gates, towers and hubs
	AttributeModifierPool::registerClass(modules); // lane XP-1: AttributeModifierPoolUpdate (every object's attribute modifier pool)
	ExperienceUpgradeModules::registerAll(modules); // lane XP-1: LevelUpUpgrade, ExperienceScalarUpgrade
	SquishCollide::registerClass(modules);  // lane HORDE-2: the crush
	HordeMemberCollide::registerClass(modules); // lane HORDE-2: contact keeps the melee alive
	BannerCarrierUpdate::registerClass(modules); // lane HORDE-2: the banner carrier's replenishment
	StancesBehavior::registerClass(modules); // lane INTEG-1: the stances through the attribute modifier pool (S-585)
	SpecialPowerModules::registerAll(modules); // lane SPELL-1: the SpecialPowerModule / PlayerHealSpecialPower data, PlayerHealSpecialPower's heal
	HeroModules::registerAll(modules); // lane HERO-1: RespawnUpdate, BuildableHeroListUpgrade
	SpecialAbilityModules::registerAll(modules); // lane HERO-1: the SpecialAbilityUpdate family
	ExtraModules::registerAll(modules); // lane MODULES-1: the remaining behaviour modules the base game uses
	GateModules::registerAll(modules); // lane HUD-5: GateOpenAndCloseBehavior, AIGateUpdate
	PortalModules::registerAll(modules); // lane WALL-2: DynamicPortalBehaviour, FakePathfindPortalBehaviour
	EmotionModules::registerAll(modules); // lane MODULES-2: EmotionTrackerUpdate, RadiateFearUpdate
	AreaScanModules::registerAll(modules); // lane MODULES-2: LargeGroupBonusUpdate, PassiveAreaEffectBehavior
	NotifyCrushModules::registerAll(modules); // lane MODULES-3: (Horde)NotifyTargetsOfImminentProbableCrushingUpdate
	HitReactionBehavior::registerClass(modules); // lane MODULES-2
	PhysicsBehavior::registerClass(modules); // lane PHYS-1: the fling flight
	InvisibilityModules::registerAll(modules); // lane STEALTH-1: InvisibilityUpdate, StealthDetectorUpdate, StealthUpdate (data)
	GarrisonContain::registerClasses(modules); // lane GARRISON-1: GarrisonContain, HordeGarrisonContain
	TransportContain::registerClasses(modules); // lane GARRISON-2: TransportContain, HordeTransportContain, SiegeEngineContain, HordeSiegeEngineContain
	AttachUpdate::registerClass(modules); // lane CAMP-1: the carried objects (MAP ANG Dark Eye's palantir shards)
	AISpecialPowerUpdate::registerClass(modules); // lane MOD-4: the units' own use of their powers (computer players)
	RepairSpecialPower::registerClass(modules); // lane MOD-4: the builders' repair power
	HordeDispatchSpecialPower::registerClass(modules); // lane FACT-C: a horde's power passed on to its members
	GrabPassengerSpecialPower::registerClass(modules); // lane MODCOMPAT-MOD1: the grabs' special power
	ObjectCreationUpgrade::registerClass(modules); // lane FACT-C: the delayed spawn / grant upgrade (spike moats, wall catapults, the Sanctum's readiness)
	SpawnBehavior::registerClass(modules); // lane MOD-4: the lairs' spawns
	TransitionDamageFXModule::registerClass(modules); // lane FX-4: the damage state transition effects (S-686)
	SlavedUpdate::registerClass(modules); // lane MOD-4: the spawns' guard
	PickupStuffUpdate::registerModule(modules); // lane MODCOMPAT-MOD1: a skirmish AI's hordes walk to the crates near them (S-1422)
	AutoPickUpUpdate::registerModule(modules); // lane MODCOMPAT-MOD1: the monsters' grab / eat scans and the WAKE_AUTO_PICKUP button (S-7160)
	BoredUpdate::registerModule(modules); // lane MODCOMPAT-MOD1: an idle troll clubs a troll friend (S-7161)
	LargeGroupAudioUpdate::registerClass(modules); // lane AUDIO-4: the logic half of the large group battle sounds
	ClientBehaviorModules::registerAll(modules); // lane AUDIO-4: the footsteps (a ClientBehavior: the drawables make it, GameClient/)
	SiegeModules::registerAll(modules); // lane SIEGE-1: SiegeDockingBehavior, SiegeDeploySpecialPower, StopSpecialPower, SiegeAIUpdate (after the AI classes)
}
