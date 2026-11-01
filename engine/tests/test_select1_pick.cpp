// OpenBFME retail tests (lane SELECT-1): a click on a soldier of a battalion picks it the way RotWK does. The owner (2026-10-22, playing the 10-22
// nightly): "if I sometimes select one unit in a battalion it doesn't select the whole battalion at all", and right-click attacks "feel off".
// RETAIL: W3DView::pickDrawable (RW 0x48AB28) casts the pick ray at the scene (RW 0x471A5B) with the ray test's +0x42 flag set; the soldier's HLod
// (HLodClass::Cast_Ray, RW 0x59C680) then hands the ray to the first top-LOD child of class 0x1B (OBBoxRenderObjClass, vtable RW 0xBEABC8; its Cast_Ray RW
// 0x575220) and returns that answer alone. The box is pickable: HLod::Set_Collision_Type (RW 0x53B5C0) copies the draw's collision type to every sub object.
// So the click meets the soldier's oriented BOUNDINGBOX, not the thin animated body (the port tested the bind-pose triangles: a click on the soldier's
// box beside its legs or arms met the ground, deselected, or turned a right-click attack into a move).
// The tests SKIP loudly without the installs. GPL-3.0.

#include "HudTestUtil.h"

#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/DrawablePick.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/HudObjects.h"
#include "GameClient/InGameHud.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
struct PickRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit PickRig(SharedWorld &s) : rig(s)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
	}
	~PickRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	void click(HudInput::Button b, const ICoord2D &px)
	{
		timeMs += 1000; // never a double click
		hud->mouseMove(px.x, px.y);
		hud->mouseButton(b, true, px.x, px.y, 0, timeMs);
		hud->mouseButton(b, false, px.x, px.y, 0, timeMs + 40);
		frames(3);
	}
	int timeMs = 1000;
};

std::vector<Object *> membersOf(Object *horde)
{
	std::vector<Object *> out;
	const ContainModuleInterface *contain = horde ? horde->getContain() : nullptr;
	if (contain && contain->getContainedItemsList())
	{
		for (Object *m : *contain->getContainedItemsList())
		{
			out.push_back(m);
		}
	}
	return out;
}

} // namespace

TEST_CASE("select1 pick: a click anywhere in a soldier's pick box selects its whole battalion (RotWK HLodClass::Cast_Ray RW 0x59C680 -> OBBox RW 0x575220)")
{
	if (!haveWorld("select1 pick box"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	const HudContext &ctx = h.hud->input().context();
	const char *hordes[] = { "GondorFighterHorde", "GondorArcherHorde", "GondorTowerShieldGuardHorde", "RohanRohirrimHorde", "MordorFighterHorde",
		"MordorArcherHorde", "IsengardFighterHorde", "IsengardUrukCrossbowHorde", "ElvenLorienWarriorHorde", "DwarvenPhalanxHorde", "GoblinFighterHorde",
		"AngmarDarkDunedainHorde" };
	int boxed = 0, unboxed = 0, tried = 0, missed = 0;
	float x = 1400.0f;
	for (const char *name : hordes)
	{
		const Coord3D c = h.rig.freeSpot(x, 1800.0f, 200.0f);
		x += 260.0f;
		Object *horde = h.rig.make(name, c.x, c.y);
		h.rig.lookAt({ c.x, c.y, h.rig.logic().getGroundHeight(c.x, c.y) });
		h.frames(30);
		std::vector<Object *> members = membersOf(horde);
		REQUIRE_MESSAGE(!members.empty(), name);
		int hordeMissed = 0, hordeTried = 0, hordeBoxed = 0;
		for (size_t i = 0; i < members.size() && i < 6; ++i)
		{
			const Drawable *d = h.rig.game->drawables().findByObject(members[i]->getID());
			REQUIRE(d != nullptr);
			const std::vector<Coord3D> points = pickBoxPoints(*d);
			if (points.empty())
			{
				++unboxed;
				continue;
			}
			++boxed;
			++hordeBoxed;
			for (const Coord3D &p : points)
			{
				ICoord2D px{ -1, -1 };
				if (!h.rig.view.worldToScreen(p, px))
				{
					continue;
				}
				++tried;
				++hordeTried;
				Object *picked = HudObjects::pickForSelection(ctx, px);
				if (picked != horde)
				{
					++missed;
					++hordeMissed;
				}
			}
		}
		std::printf("  info: select1 %s: %zu members, %d with a pick box, %d of %d clicks in the boxes missed the battalion\n", name, members.size(), hordeBoxed,
			hordeMissed, hordeTried);
		h.rig.logic().destroyObject(horde);
		h.frames(2);
	}
	std::printf("  info: select1 pick box: %d soldiers with an oriented box, %d without; %d of %d clicks missed the battalion\n", boxed, unboxed, missed, tried);
	REQUIRE(boxed == 72); // every sampled retail soldier (6 of each of the 12 battalions) carries the box the RotWK pick prefers
	CHECK(unboxed == 0);
	CHECK(tried == 72 * 7); // every sample point is on screen
	CHECK(missed == 0);
}

TEST_CASE("select1 pick: the real clicks: a left click beside a soldier's body selects the battalion, a right click beside an enemy soldier attacks its battalion")
{
	if (!haveWorld("select1 pick clicks"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	Player *enemy = h.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const Coord3D c = h.rig.freeSpot(2400.0f, 1800.0f, 260.0f);
	Object *ours = h.rig.make("GondorFighterHorde", c.x - 60.0f, c.y);
	Object *theirs = h.rig.make("MordorFighterHorde", c.x + 60.0f, c.y, enemy);
	h.rig.lookAt({ c.x, c.y, h.rig.logic().getGroundHeight(c.x, c.y) });
	h.hud->input().commandTranslator().setUseAlternateMouse(true);
	h.frames(20);
	// a point well inside the box, off the body's centre line: the box's +-X extent at its centre height
	auto beside = [&](Object *horde) {
		for (Object *m : membersOf(horde))
		{
			const Drawable *d = h.rig.game->drawables().findByObject(m->getID());
			const std::vector<Coord3D> points = d ? pickBoxPoints(*d) : std::vector<Coord3D>{};
			if (points.size() == 7)
			{
				return h.rig.screenOf(points[1]);
			}
		}
		FAIL("no member with a pick box");
		return ICoord2D{ -1, -1 };
	};
	h.click(HudInput::Button::Left, beside(ours));
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ ours->getID() });
	auto count = [&](const char *prefix) {
		size_t n = 0;
		for (const std::string &l : h.hud->input().messageLog())
		{
			n += l.rfind(prefix, 0) == 0 ? 1 : 0;
		}
		return n;
	};
	const size_t attacksBefore = count("MSG_DO_ATTACK_OBJECT"), movesBefore = count("MSG_DO_MOVETO");
	h.click(HudInput::Button::Right, beside(theirs));
	CHECK(count("MSG_DO_ATTACK_OBJECT") == attacksBefore + 1);
	CHECK(count("MSG_DO_MOVETO") == movesBefore);
	CHECK(h.hud->input().ui().selected() == std::vector<ObjectID>{ ours->getID() });
	REQUIRE(ours->getAIUpdateInterface() != nullptr);
	h.frames(3);
	CHECK(ours->getAIUpdateInterface()->isAttacking());
}

TEST_CASE("select1 drag: a drag box over one soldier at the battalion's edge selects the battalion (RotWK addDrawableToList RW 0x7108F2: the soldier stands for its HORDE producer)")
{
	if (!haveWorld("select1 drag"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	const Coord3D c = h.rig.freeSpot(2400.0f, 1800.0f, 260.0f);
	Object *horde = h.rig.make("GondorFighterHorde", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, h.rig.logic().getGroundHeight(c.x, c.y) });
	h.hud->input().commandTranslator().setUseAlternateMouse(true);
	h.frames(30);
	const ICoord2D centre = h.rig.screenOf(*horde->getPosition());
	// the soldier drawn farthest from the horde's centre on screen
	ICoord2D far{ -1, -1 };
	int best = -1;
	for (Object *m : membersOf(horde))
	{
		const ICoord2D p = h.rig.screenOf(*m->getPosition());
		const int d2 = (p.x - centre.x) * (p.x - centre.x) + (p.y - centre.y) * (p.y - centre.y);
		if (d2 > best)
		{
			best = d2;
			far = p;
		}
	}
	REQUIRE(best > 12 * 12); // tolerance: the 12-pixel drag box below must leave the horde's centre out (any farther soldier does)
	const ICoord2D a{ far.x - 6, far.y - 6 }, b{ far.x + 6, far.y + 6 };
	h.timeMs += 1000;
	h.hud->mouseMove(a.x, a.y);
	h.hud->mouseButton(HudInput::Button::Left, true, a.x, a.y, 0, h.timeMs);
	for (int i = 1; i <= 6; ++i)
	{
		h.hud->mouseMove(a.x + (b.x - a.x) * i / 6, a.y + (b.y - a.y) * i / 6);
		h.frames(1);
	}
	h.hud->mouseButton(HudInput::Button::Left, false, b.x, b.y, 0, h.timeMs + 400);
	h.frames(3);
	CHECK(h.hud->input().ui().selected() == std::vector<ObjectID>{ horde->getID() });
	CHECK(h.rig.local->selection() == std::vector<ObjectID>{ horde->getID() });
}
