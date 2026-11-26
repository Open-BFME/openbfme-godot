// OpenBFME. GPL-3.0.
//
// DrawableTornado: the drawable's W3DTornadoDraw entry (RW 0x4D102E / DONOR BFME2 0x4D181D): the runtime on a NOT_DRAWN entry, the decals' follow of the drawable's
// transform (RW 0x4D1000) and their creation when the drawable is bound to its object (RW 0x4D120A). CLIENT ONLY: it reads the drawable's transform and writes
// only the client decal list; it never reaches the simulation (tools/sim/sim_policy.json excludes it). The DrawEntry special members are here because DrawEntry holds
// a std::unique_ptr<W3DTornadoDraw> that Drawable.h only forward-declares.

#include "GameClient/Drawable.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTornadoDraw.h"

DrawEntry::DrawEntry() = default;
DrawEntry::~DrawEntry() = default;
DrawEntry::DrawEntry(DrawEntry &&) = default;
DrawEntry &DrawEntry::operator=(DrawEntry &&) = default;

void Drawable::makeTornadoEntry(DrawEntry &e, const ThingTemplate::Nugget &nugget)
{
	if (const W3DTornadoDrawModuleData *td = dynamic_cast<const W3DTornadoDrawModuleData *>(nugget.data.get()))
	{
		e.tornadoDraw = std::make_unique<W3DTornadoDraw>(this, td);
		e.animated = true;
	}
}

void Drawable::tornadoTransformChanged(const Coord3D *oldPos, float oldAngle)
{
	for (DrawEntry &e : m_entries)
	{
		if (e.tornadoDraw)
		{
			e.tornadoDraw->reactToTransformChange(oldPos, oldAngle);
		}
	}
}

void Drawable::tornadoBound()
{
	for (DrawEntry &e : m_entries)
	{
		if (e.tornadoDraw)
		{
			e.tornadoDraw->onDrawableBoundToObject();
		}
	}
}
