// OpenBFME. GPL-3.0.
//
// Both editions: RW 0x00890AE0, BFME2 0x0088D370. See ProjectileStreamUpdate.h.

#include "GameLogic/Module/ProjectileStreamUpdate.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

void ProjectileStreamUpdateModuleData::buildFieldParse(MultiIniFieldParse & /*p*/)
{
	// Retail has no fields for ProjectileStreamUpdateModuleData (RW 0x00654AD2 / BFME2 0x0024FC91)
}

ProjectileStreamUpdate::ProjectileStreamUpdate(Thing *thing, const ModuleData *moduleData)
	: UpdateModule(thing, moduleData)
{
	for (int i = 0; i < MAX_PROJECTILE_STREAM; ++i)
	{
		m_projectileIDs[i] = 0;
	}
	m_nextFreeIndex = 0;
	m_firstValidIndex = 0;
	m_owningObject = 0;
	m_targetObject = 0;
	m_targetPosition = Coord3D{};
}

// RW 0x00740AE5 (DONOR BFME2 ProjectileStreamUpdate::update): cullFrontOfList, considerDying -> destroyObject, returns UPDATE_SLEEP_NONE
UpdateSleepTime ProjectileStreamUpdate::update()
{
	cullFrontOfList();

	if (considerDying())
	{
		Object *me = getObject();
		if (me)
		{
			me->logic().destroyObject(me);
		}
	}

	return UPDATE_SLEEP_NONE;
}

void ProjectileStreamUpdate::addProjectile(ObjectID sourceID, ObjectID newID, ObjectID victimID, const Coord3D *victimPos)
{
	if (m_owningObject == 0)
	{
		m_owningObject = sourceID;
	}

	if (victimID != 0)
	{
		if (victimID != m_targetObject)
		{
			// Changed targets: insert hole to break the stream
			m_projectileIDs[m_nextFreeIndex] = 0;
			m_nextFreeIndex = (m_nextFreeIndex + 1) % MAX_PROJECTILE_STREAM;
			m_targetObject = victimID;
		}
		m_targetPosition = Coord3D{};
	}
	else if (victimPos != nullptr)
	{
		if (m_targetPosition.x != victimPos->x || m_targetPosition.y != victimPos->y || m_targetPosition.z != victimPos->z)
		{
			// New position: insert hole
			m_projectileIDs[m_nextFreeIndex] = 0;
			m_nextFreeIndex = (m_nextFreeIndex + 1) % MAX_PROJECTILE_STREAM;
			m_targetPosition = *victimPos;
		}
		m_targetObject = 0;
	}

	// Keep track of the id in a circular array
	m_projectileIDs[m_nextFreeIndex] = newID;
	m_nextFreeIndex = (m_nextFreeIndex + 1) % MAX_PROJECTILE_STREAM;
}

// RW 0x007409F9 (DONOR BFME2 ProjectileStreamUpdate::cullFrontOfList): advances m_firstValidIndex (+0x74) mod 20 while front object is gone
void ProjectileStreamUpdate::cullFrontOfList()
{
	Object *me = getObject();
	if (!me)
	{
		return;
	}
	GameLogic &logic = me->logic();

	while (m_firstValidIndex != m_nextFreeIndex)
	{
		Object *obj = logic.findObjectByID(m_projectileIDs[m_firstValidIndex]);
		if (obj && !obj->isDestroyed())
		{
			break;
		}
		m_firstValidIndex = (m_firstValidIndex + 1) % MAX_PROJECTILE_STREAM;
	}
}

// RW 0x00740A31 (DONOR BFME2 ProjectileStreamUpdate::considerDying): true when buffer empty, m_owningObject (+0x78) != 0 and owner is gone
bool ProjectileStreamUpdate::considerDying()
{
	if (m_firstValidIndex == m_nextFreeIndex && m_owningObject != 0)
	{
		Object *me = getObject();
		if (me)
		{
			Object *owner = me->logic().findObjectByID(m_owningObject);
			if (!owner || owner->isDestroyed())
			{
				return true;
			}
		}
	}
	return false;
}

// RW 0x00740A56 (DONOR BFME2 ProjectileStreamUpdate::getAllPoints): walks circular buffer m_firstValidIndex..m_nextFreeIndex (mod 20)
void ProjectileStreamUpdate::getAllPoints(Coord3D *points, int *count) const
{
	Object *me = const_cast<ProjectileStreamUpdate *>(this)->getObject();
	GameLogic *logic = me ? &me->logic() : nullptr;

	int pointCount = 0;
	int pointIndex = m_firstValidIndex;
	Coord3D *out = points;

	while (pointIndex != m_nextFreeIndex)
	{
		Coord3D pos{};
		if (logic && m_projectileIDs[pointIndex] != 0)
		{
			Object *projectile = logic->findObjectByID(m_projectileIDs[pointIndex]);
			if (projectile && !projectile->isDestroyed() && projectile->getPosition())
			{
				pos = *projectile->getPosition();
			}
		}
		if (out)
		{
			*out++ = pos;
		}
		pointIndex = (pointIndex + 1) % MAX_PROJECTILE_STREAM;
		pointCount++;
	}

	if (count)
	{
		*count = pointCount;
	}
}

void ProjectileStreamUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	xferUpdateModule(x);

	for (int i = 0; i < MAX_PROJECTILE_STREAM; ++i)
	{
		x.xferObjectID(&m_projectileIDs[i]);
	}
	x.xferInt(&m_nextFreeIndex);
	SnapshotXfer::checkRange(m_nextFreeIndex, 0, MAX_PROJECTILE_STREAM - 1, "ProjectileStreamUpdate m_nextFreeIndex");
	x.xferInt(&m_firstValidIndex);
	SnapshotXfer::checkRange(m_firstValidIndex, 0, MAX_PROJECTILE_STREAM - 1, "ProjectileStreamUpdate m_firstValidIndex");
	x.xferObjectID(&m_owningObject);

	if (v.current >= 2)
	{
		x.xferObjectID(&m_targetObject);
		SnapshotXfer::coord3D(x, m_targetPosition);
	}
}

void ProjectileStreamUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	for (int i = 0; i < MAX_PROJECTILE_STREAM; ++i)
	{
		hasher.addU32(m_projectileIDs[i]);
	}
	hasher.addI32(m_nextFreeIndex);
	hasher.addI32(m_firstValidIndex);
	hasher.addU32(m_owningObject);
	hasher.addU32(m_targetObject);
	hasher.addFloat(m_targetPosition.x);
	hasher.addFloat(m_targetPosition.y);
	hasher.addFloat(m_targetPosition.z);
}

// getModuleName RW 0x00740844, getModuleNameKey RW 0x0074084A
void ProjectileStreamUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<ProjectileStreamUpdateModuleData>("ProjectileStreamUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("ProjectileStreamUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const ProjectileStreamUpdateModuleData *>(data);
		return std::make_unique<ProjectileStreamUpdate>(thing, typed);
	});
}
