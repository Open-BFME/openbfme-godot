// OpenBFME. GPL-3.0.
// See GameLogic/Combat/ProjectileLauncher.h for the target facts.

#include "GameLogic/Combat/ProjectileLauncher.h"

#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/WeaponNugget.h"

void ObjectProjectileLauncher::launch(GameLogic &logic, const ProjectileShot &shot)
{
	Object *source = shot.source != INVALID_ID ? logic.findObjectByID(shot.source) : nullptr;
	if (!source || !shot.nugget)
	{
		return;
	}
	const std::string &name = shot.nugget->m_projectileTemplateName;
	if (name.empty() || name == "None" || name == "NONE")
	{
		return; // RW 0x90FAF7: no projectile template
	}
	const ThingTemplate *tt = logic.things().findTemplate(name);
	if (!tt)
	{
		logic.reportError("weapon " + (shot.weapon ? shot.weapon->getName() : std::string("?")) + ": the ProjectileNugget's projectile '" + name + "' is not an object template");
		return;
	}
	Player *owner = source->getControllingPlayer();
	Team *team = owner ? owner->getDefaultTeam() : nullptr;
	Object *projectile = logic.newObject(tt, team, ObjectStatusMaskType{});
	if (!projectile)
	{
		logic.reportError("weapon " + (shot.weapon ? shot.weapon->getName() : std::string("?")) + ": the projectile '" + name + "' could not be created");
		return;
	}
	ProjectileUpdateInterface *iface = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : projectile->modules())
	{
		iface = dynamic_cast<ProjectileUpdateInterface *>(m.get());
		if (iface)
		{
			break;
		}
	}
	if (!iface)
	{
		logic.destroyObject(projectile); // RW 0x90FB6C
		logic.reportError("projectile '" + name + "' has no projectile update module (BezierProjectileBehavior)");
		return;
	}
	projectile->setProducer(source);
	int slot = shot.slot;
	const int override = shot.nugget->m_weaponLaunchBoneSlotOverride;
	if (override >= 0 && override < 6)
	{
		slot = override;
	}
	const Object *victim = shot.victim != INVALID_ID ? logic.findObjectByID(shot.victim) : nullptr;
	if (victim)
	{
		iface->projectileLaunchAtObjectOrPosition(victim, nullptr, source, slot, shot.barrel, shot.weapon, shot.warhead);
	}
	else
	{
		Coord3D pos = shot.targetPosition;
		iface->projectileLaunchAtObjectOrPosition(nullptr, &pos, source, slot, shot.barrel, shot.weapon, shot.warhead);
	}
}
