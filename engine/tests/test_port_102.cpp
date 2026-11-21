// OpenBFME unit tests. GPL-3.0.
// Port 102 (issue fork issue 262): GeometryUsedForHealthBox (RW 0xAD1730, shape +0x21) gates only the health-box height
// (RW 0x677985 -> RW 0xAD1990). The general geometry height (RW 0xAD1920) and the health bar's width, from the
// bounding circle at Object +0xB8 (RW 0x68DF9D), still include every active shape.

#include "doctest.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "GameClient/DrawableIconUI.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/ObjectGeometry.h"

namespace
{
ThingTemplate building(const char *healthFlag)
{
	ThingTemplate tt;
	tt.friend_noteGeometryRow("Geometry", { "CYLINDER" });
	tt.friend_noteGeometryRow("GeometryHeight", { "40" });
	tt.friend_noteGeometryRow("GeometryMajorRadius", { "10" });
	tt.friend_noteGeometryRow("AdditionalGeometry", { "CYLINDER" });
	tt.friend_noteGeometryRow("GeometryHeight", { "120" });
	tt.friend_noteGeometryRow("GeometryMajorRadius", { "80" });
	if (healthFlag != nullptr)
	{
		tt.friend_noteGeometryRow("GeometryUsedForHealthBox", { healthFlag });
	}
	return tt;
}

}

struct DrawableIconUITestAccess
{
	using Box = DrawableIconUI::TemplateBox;
	static const Box &boxOf(DrawableIconUI &ui, const ThingTemplate &tt) { return ui.boxOf(tt); }
};

namespace
{
const DrawableIconUITestAccess::Box &box(DrawableIconUI &ui, const ThingTemplate &tt)
{
	return DrawableIconUITestAccess::boxOf(ui, tt);
}
}

TEST_CASE("PORT-102 health box: No excludes the tall additional geometry from height but not from general geometry or bar width")
{
	ThingTemplate tt = building("No");
	DrawableIconUI ui;
	const auto &b = box(ui, tt);
	CHECK(b.maxHeight == 40.0f); // without the fix this is the general height, 120
	CHECK(b.radius == 80.0f);    // RW 0x68DF5B: width still uses the whole active bounding circle
	CHECK(ObjectGeometry::maxHeightAbovePosition(ObjectGeometry::shapesOf(tt)) == 120.0f);
}

TEST_CASE("PORT-102 health box: Yes and the absent row keep the default all-active-shapes height")
{
	for (const char *flag : { "Yes", static_cast<const char *>(nullptr) })
	{
		ThingTemplate tt = building(flag);
		DrawableIconUI ui;
		const auto &b = box(ui, tt);
		CHECK(b.maxHeight == 120.0f);
		CHECK(b.radius == 80.0f);
	}
}

TEST_CASE("PORT-102 health box: a later geometry starts with the retail default Yes")
{
	ThingTemplate tt = building("No");
	tt.friend_noteGeometryRow("AdditionalGeometry", { "SPHERE" });
	tt.friend_noteGeometryRow("GeometryMajorRadius", { "60" });
	DrawableIconUI ui;
	const auto &b = box(ui, tt);
	CHECK(b.maxHeight == 60.0f);
	CHECK(ObjectGeometry::maxHeightAbovePosition(ObjectGeometry::shapesOf(tt)) == 120.0f);
}
