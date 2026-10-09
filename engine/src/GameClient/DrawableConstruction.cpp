// OpenBFME. GPL-3.0. Drawable::updateConstruction (lane RENDER-2): the construction look of a structure being built, client side only (see Drawable.h).
// It reads the object (construction percent, calcTimeToBuild, geometry) and writes nothing but the draw modules' animation frames and the
// entries' render offsets: no simulation state, no logic random (excluded from the simulation audit in tools/sim/sim_policy.json).

#include "GameClient/Drawable.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include <stdexcept>

void Drawable::updateConstruction(const ObjectSnapshot &o, UnsignedInt snapshotFrame, double alpha)
{
	// SMOOTH-1: the object's construction state comes from the completed frame's snapshot record (percent, calcTimeToBuild of the frame)
	static const int kActivelyBeingConstructed = ModelCondition::indexOf("ACTIVELY_BEING_CONSTRUCTED"); // RW 0x6730EC: bit 0x45
	const bool active = kActivelyBeingConstructed >= 0 && m_flags.test(kActivelyBeingConstructed);
	bool adjusting = false;
	for (const DrawEntry &e : m_entries)
	{
		adjusting = adjusting || (e.draw && e.draw->adjustsHeightByConstruction());
	}
	if (!active && !adjusting)
	{
		return;
	}
	// RW 0x63252F: the cached percent is refreshed on the first client frame of a logic frame
	const unsigned logicFrame = snapshotFrame;
	const bool refresh = logicFrame != m_constructionLogicFrame;
	m_constructionLogicFrame = logicFrame;
	// RW 0x68BD71: 1.0 / calcTimeToBuild(template, owner, no producer, -1) (0 without a controlling player); lane BUILD-2: computed as retail does (the logic's
	// GettingBuiltBehavior no longer holds a build length for every structure)
	float rate = 0.0f;
	if (o.buildFrames != 0)
	{
		rate = (float)(1.0 / (double)o.buildFrames); // LogicSnapshot::build took calcTimeToBuild (0: no controlling player)
	}
	const float a = (float)alpha;
	const float percent = o.constructionPercent;
	bool changed = false;
	for (DrawEntry &e : m_entries)
	{
		if (!e.draw)
		{
			continue;
		}
		if (active)
		{
			changed = e.draw->updateConstructionFrame(percent, refresh, rate, a) || changed;
		}
		float offset = 0.0f;
		if (e.draw->adjustsHeightByConstruction())
		{
			if (m_geometryHeight < 0.0f)
			{
				m_geometryHeight = ObjectGeometry::maxHeightAbovePosition(ObjectGeometry::shapesOf(*o.tmpl)); // RW 0xAD1920
			}
			offset = e.draw->constructionHeightOffset(percent, refresh, rate, a, m_geometryHeight);
		}
		if (offset != e.constructionOffsetZ)
		{
			e.constructionOffsetZ = offset;
			changed = true;
		}
	}
	if (changed)
	{
		++m_changeCount;
	}
}

Coord3D Drawable::entryPosition(const DrawEntry &e) const
{
	Coord3D p = *getPosition();
	if (e.constructionOffsetZ != 0.0f)
	{
		// RW 0x4B6C5A..: the offset times the transform's third column (the model's own Z axis, carrying the drawable's scale)
		const float *b = getBasis();
		const float k = e.constructionOffsetZ * getInstanceScale();
		p.x += b[2] * k;
		p.y += b[5] * k;
		p.z += b[8] * k;
	}
	return p;
}
