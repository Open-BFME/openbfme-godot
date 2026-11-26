// OpenBFME retail tests (lane BLOBSHADOW-2): a SHADOW_DECAL without ShadowSizeX / Y takes its size from the model (RW 0x50C01D: the render
// object's object-space box, vslot 0x110, its Extent.X / Extent.Y doubled). Among those without a size: the animals (Bear, DeerMale, Dog), the creep
// lairs (CaveTrollLair, HillTrollLair, FireDrakeLair) and camps; before this lane they drew no blob (the workers' model states size theirs: 19).
// SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"

#include "GameClient/BlobShadow.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"

#include <cmath>

using namespace hudtest;

namespace
{
// the first model draw of the object's drawable that names a blob: its spec and its model's extents
bool blobOf(Rig &rig, const Object &o, BlobShadow::Spec &spec, float &extentX, float &extentY)
{
	const Drawable *d = rig.game->drawables().findByObject(o.getID());
	REQUIRE(d != nullptr);
	for (const DrawEntry &e : d->entries())
	{
		if (e.kind != W3D_DRAWKIND_MODEL || !e.draw)
		{
			continue;
		}
		const auto *module = dynamic_cast<const W3DModelDrawModuleData *>(e.data);
		if (module && BlobShadow::resolve(d->getTemplate(), module, e.draw->currentModelState(), false, spec))
		{
			BlobShadow::modelExtent(e.draw->currentModel(), extentX, extentY);
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("blobshadow2 retail: a blob without a size takes twice the model's half extents (RW 0x50C01D); a sized one keeps its own")
{
	if (!haveWorld("blobshadow2"))
	{
		return;
	}
	Rig rig(shared());
	const Coord3D c = rig.freeSpot(2800, 1400, 200.0f);
	Object *bear = rig.make("Bear", c.x, c.y);
	Object *soldier = rig.make("GondorFighter", c.x + 60.0f, c.y);
	for (int i = 0; i < 5; ++i)
	{
		rig.game->advance(0.033);
	}

	BlobShadow::Spec spec;
	float ex = -1.0f, ey = -1.0f;
	REQUIRE(blobOf(rig, *bear, spec, ex, ey));
	CHECK(spec.sizeX == 0.0f); // the INI names no ShadowSizeX / Y
	CHECK(spec.sizeY == 0.0f);
	CHECK(spec.texture == "shadow.tga"); // no ShadowTexture either (RW 0x50BFA6)
	// tolerance: the extents come from the private retail model; a bear is longer (x, its facing) than wide, some units to either side
	CHECK(ex > ey);
	CHECK(ex > 5.0f);  // tolerance: the private model's size (17.5 in the 2.01 data): a real body, not a degenerate box
	CHECK(ex < 40.0f); // tolerance: the private model's size: an animal, not a building-sized box
	CHECK(ey > 2.0f);  // tolerance: the private model's size (5.8 in the 2.01 data)
	BlobShadow::Quad q;
	REQUIRE(BlobShadow::quad(spec, c.x, c.y, 0.0f, ex, ey, q));
	CHECK(q.ux == ex); // facing 0: u is the world x axis, half of 2 * extent
	CHECK(q.vy == ey); // the size Y is negated (RW 0x50C054), v = (sin, -cos): + extent
	// without the extents (the port before BLOBSHADOW-2) there is no quad
	CHECK_FALSE(BlobShadow::quad(spec, c.x, c.y, 0.0f, 0.0f, 0.0f, q));

	// a soldier's INI size wins; its extents are not asked for
	REQUIRE(blobOf(rig, *soldier, spec, ex, ey));
	CHECK(spec.sizeX != 0.0f); // tolerance: the soldier's INI size (private data); only that it has one matters here
	REQUIRE(BlobShadow::quad(spec, c.x, c.y, 0.0f, ex, ey, q));
	CHECK(q.ux == 0.5f * spec.sizeX);
}
