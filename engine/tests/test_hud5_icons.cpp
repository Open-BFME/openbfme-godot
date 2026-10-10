// OpenBFME retail tests, lane HUD-5 (the owner's report: no construction progress, no level marks, no health bars over units). GPL-3.0.
//
// DrawableIconUI (GameClient/DrawableIconUI.h): the construction text (RW 0x677CF9), the health bar's gating, rectangles and band colours (RW 0x676346 /
// 0x6723C3 / 0x671142) and the veterancy marks (RW 0x6780DF) on a retail game, from GameData's ShowObjectHealth / VeterancyPipDrawObjectFilter, lotr.str and the
// mapped images. The tests SKIP loudly without the installs.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameClient/DrawableIconUI.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/Image.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
struct IconRig
{
	Rig rig;
	GameTextTableSource text;
	MappedImageCollection images;
	INIEnvironment imageEnv;
	CameraSettings gd;
	IconUISettings settings;
	DrawableIconUI ui;
	explicit IconRig(SharedWorld &s) : rig(s)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(s.mount->fs->readFile("data/lotr.str", bytes, &error), error);
		REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
		imageEnv.fileSystem = s.mount->fs.get();
		images.registerBlocks(imageEnv.blocks);
		INI ini(imageEnv);
		ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
		REQUIRE_MESSAGE(CameraSettings::load(*s.mount->fs, gd, &error), error);
		settings.showObjectHealth = gd.showObjectHealth;
		settings.veterancyFilter = gd.haveVeterancyPipFilter ? &gd.veterancyPipFilter : nullptr;
		settings.images = &images;
		settings.text = &text;
		settings.zoom = 1.0f;
	}
	Object *place(const char *tmpl, Player *p, float x, float y)
	{
		std::string error;
		Object *o = rig.game->createObject(tmpl, p->getPlayerIndex(), Coord3D{ x, y, rig.game->logic().getGroundHeight(x, y) }, 0.0f, &error);
		REQUIRE_MESSAGE(o, error);
		return o;
	}
	void look(const Object &o)
	{
		const Coord3D &p = *o.getPosition();
		rig.view.set(Coord3D{ p.x, p.y - 300.0f, p.z + 400.0f }, p);
	}
	std::vector<IconUIOp> build(const std::set<ObjectID> &selected, ObjectID over)
	{
		std::vector<IconUIOp> ops;
		const auto worldContext = rig.sh.world->enterContext(); // the level store (TheExperienceLevelSystem) is this world's, per thread
		ui.build(rig.game->logic(), rig.game->drawables(), rig.view, settings, selected, over, ops);
		return ops;
	}
	static std::vector<IconUIOp> of(const std::vector<IconUIOp> &ops, ObjectID id)
	{
		std::vector<IconUIOp> out;
		for (const IconUIOp &op : ops)
		{
			if (op.object == id)
			{
				out.push_back(op);
			}
		}
		return out;
	}
};
} // namespace

TEST_CASE("hud5 icons retail: GameData's ShowObjectHealth and VeterancyPipDrawObjectFilter are read")
{
	if (!haveWorld("hud5 icons gamedata"))
	{
		return;
	}
	IconRig r(shared());
	CHECK(r.gd.showObjectHealth); // retail GameData: ShowObjectHealth = Yes
	CHECK(r.gd.haveVeterancyPipFilter); // ANY +HORDE +MONSTER +DwarvenBattleWagon +IsengardBeserker -HERO
	CHECK(r.images.findImageByName("Good_Vet_Dot") != nullptr);
	CHECK(r.images.findImageByName("Evil_Vet") != nullptr);
}

TEST_CASE("hud5 icons retail: a building under construction shows lotr.str's \"Building: %.0f%%\" with its percent; a finished one does not")
{
	if (!haveWorld("hud5 icons construction"))
	{
		return;
	}
	IconRig r(shared());
	Object *b = r.place("GondorBarracks", r.rig.local, 600.0f, 600.0f);
	r.rig.game->advance(0.2);
	r.look(*b);
	b->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	b->setConstructionPercent(37.4f);
	std::vector<IconUIOp> ops = IconRig::of(r.build({}, 0), b->getID());
	int texts = 0;
	for (const IconUIOp &op : ops)
	{
		if (op.kind == IconUIOp::TEXT)
		{
			++texts;
			CHECK(op.text == "Building: 37%");
			CHECK(op.color == 0xFFFFFFFFu);
			CHECK(op.dropColor == 0xFF000000u);
		}
	}
	CHECK(texts == 1);
	b->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
	ops = IconRig::of(r.build({}, 0), b->getID());
	for (const IconUIOp &op : ops)
	{
		CHECK(op.kind != IconUIOp::TEXT);
	}
}

TEST_CASE("hud5 icons retail: a structure's health bar when selected or under the mouse only: frames, the full green rows, a damaged one's bands")
{
	if (!haveWorld("hud5 icons health"))
	{
		return;
	}
	IconRig r(shared());
	Object *b = r.place("GondorBarracks", r.rig.local, 600.0f, 600.0f);
	r.rig.game->advance(0.2);
	r.look(*b);
	CHECK(IconRig::of(r.build({}, 0), b->getID()).empty()); // not selected, not under the mouse
	const std::vector<IconUIOp> sel = IconRig::of(r.build({ b->getID() }, 0), b->getID());
	// a STRUCTURE's look (RW 0x6721CC): frames 10 / 8 / 6 high and four rows
	REQUIRE(sel.size() == 7);
	CHECK(sel[0].kind == IconUIOp::OPEN_RECT);
	CHECK(sel[0].color == 0x7F000000u);
	CHECK(sel[0].h == 10.0f);
	CHECK(sel[1].color == 0xFFBA9252u);
	CHECK(sel[1].h == 8.0f);
	CHECK(sel[2].kind == IconUIOp::FILL_RECT);
	CHECK(sel[2].color == 0xFF000000u);
	CHECK(sel[2].h == 6.0f);
	const float width = sel[0].w - 6.0f;
	CHECK(width >= 20.0f);
	// full health: the four green rows of RW 0xC10FA8 (11 128 8), (255 255 128), (77 180 3), (4 93 3), each the bar's whole width
	CHECK(sel[3].color == 0xFF0B8008u);
	CHECK(sel[4].color == 0xFFFFFF80u);
	CHECK(sel[5].color == 0xFF4DB403u);
	CHECK(sel[6].color == 0xFF045D03u);
	CHECK(sel[3].w == width);
	CHECK(sel[3].y + 1.0f == sel[4].y);
	// the mouse over it shows it too
	CHECK(IconRig::of(r.build({}, b->getID()), b->getID()).size() == 7);
	// half health: amber (0.4 .. 0.6, RW 0xC10F68), half the width
	BodyModuleInterface *body = b->getBodyModule();
	REQUIRE(body);
	DamageInfo di;
	di.m_input.m_amount = body->getMaxHealth() * 0.5f;
	di.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	body->attemptDamage(di);
	r.rig.game->advance(0.2);
	const std::vector<IconUIOp> half = IconRig::of(r.build({ b->getID() }, 0), b->getID());
	REQUIRE(half.size() == 7);
	const float ratio = body->getHealth() / body->getMaxHealth();
	MESSAGE("health ratio " << ratio);
	if (ratio >= 0.4f && ratio < 0.6f)
	{
		CHECK(half[3].color == 0xFFD09000u); // (208 144 0)
	}
	CHECK(half[3].w == doctest::Approx(width * ratio));
	// a unit keeps the 9 / 7 / 5 frames and three rows (RW 0x6723C3): a selected hero
	Object *hero = r.place("GondorBoromir", r.rig.local, 660.0f, 600.0f);
	r.rig.game->advance(0.2);
	const std::vector<IconUIOp> unit = IconRig::of(r.build({ hero->getID() }, 0), hero->getID());
	REQUIRE(unit.size() == 6);
	CHECK(unit[0].h == 9.0f);
	CHECK(unit[3].color == 0xFF0C9C24u); // RW 0xC11048 (12 156 36)
}

// RW 0x671FD5: a building under construction has the building frames and four rows blended from RW 0xC10EA8 (blue) to RW 0xC10EE8 (cyan) by its health ratio
TEST_CASE("hud5 icons retail: a construction's health bar: the building frames and the blue-to-cyan rows of RW 0x670FA0")
{
	if (!haveWorld("hud5 icons construction bar"))
	{
		return;
	}
	IconRig r(shared());
	Object *b = r.place("GondorBarracks", r.rig.local, 800.0f, 600.0f);
	r.rig.game->advance(0.2);
	r.look(*b);
	b->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	b->setConstructionPercent(40.0f);
	REQUIRE(b->isUnderConstruction());
	BodyModuleInterface *body = b->getBodyModule();
	REQUIRE(body);
	const float ratio = body->getHealth() / body->getMaxHealth();
	std::vector<IconUIOp> ops;
	for (const IconUIOp &op : IconRig::of(r.build({ b->getID() }, 0), b->getID()))
	{
		if (op.kind != IconUIOp::TEXT)
		{
			ops.push_back(op);
		}
	}
	REQUIRE(ops.size() == 7);
	CHECK(ops[0].h == 10.0f);
	auto lerp = [&](int lo, int hi) { return (std::uint32_t)(int)((float)lo * (1.0f - ratio) + (float)hi * ratio) & 0xFFu; };
	const std::uint32_t row0 = 0xFF000000u | (lerp(21, 28) << 16) | (lerp(86, 207) << 8) | lerp(173, 251);
	MESSAGE("construction health ratio " << ratio);
	CHECK(ops[3].color == row0);
	CHECK(ops[6].y == ops[3].y + 3.0f);
}

TEST_CASE("hud5 icons retail: health bars only on selection or hover; AllHealthBars lets a selected horde's soldiers have them, never all the time")
{
	if (!haveWorld("hud5 icons bar visibility"))
	{
		return;
	}
	IconRig r(shared());
	Object *h = r.place("GondorFighterHorde", r.rig.local, 900.0f, 900.0f);
	Object *hero = r.place("GondorBoromir", r.rig.local, 980.0f, 900.0f);
	for (int i = 0; i < 5; ++i)
	{
		r.rig.game->advance(0.2);
	}
	r.look(*h);
	REQUIRE(h->getContain());
	std::vector<ObjectID> soldiers;
	for (const Object *m : *h->getContain()->getContainedItemsList())
	{
		soldiers.push_back(m->getID());
	}
	REQUIRE(!soldiers.empty());
	auto bars = [&](const std::set<ObjectID> &sel, ObjectID over) {
		int n = 0;
		for (const IconUIOp &op : r.build(sel, over))
		{
			n += op.kind == IconUIOp::OPEN_RECT && op.color == 0x7F000000u ? 1 : 0; // a bar's outer frame
		}
		return n;
	};
	for (bool all : { false, true })
	{
		r.settings.allHealthBars = all;
		CAPTURE(all);
		CHECK(bars({}, 0) == 0); // nothing selected, nothing under the mouse: no bar at all
		CHECK(bars({ hero->getID() }, 0) == 1); // a hero: its bar with or without the option
		CHECK(bars({}, hero->getID()) == 1);
		CHECK(bars({ h->getID() }, 0) == (all ? (int)soldiers.size() : 0)); // the selected horde's soldiers (INFANTRY) only with AllHealthBars
		CHECK(bars({}, soldiers[0]) == (all ? 1 : 0));
	}
}

TEST_CASE("hud5 icons retail: veterancy marks: a horde of rank 3 shows three Good_Vet_Dot, rank 6 one Good_Vet over one dot; an evil player's are Evil_Vet")
{
	if (!haveWorld("hud5 icons veterancy"))
	{
		return;
	}
	IconRig r(shared());
	REQUIRE(r.settings.veterancyFilter);
	Object *h = r.place("GondorFighterHorde", r.rig.local, 700.0f, 700.0f);
	r.rig.game->advance(0.2);
	r.look(*h);
	ExperienceTracker *xp = h->getExperienceTracker();
	REQUIRE(xp);
	auto images = [&](const std::vector<IconUIOp> &ops) {
		std::vector<std::string> out;
		for (const IconUIOp &op : ops)
		{
			if (op.kind == IconUIOp::IMAGE)
			{
				out.push_back(op.image);
			}
		}
		return out;
	};
	CHECK(images(IconRig::of(r.build({}, 0), h->getID())).empty()); // rank 1: none
	xp->gainLevels(3 - xp->getRank(), false);
	r.rig.game->advance(0.2);
	REQUIRE(xp->getRank() == 3);
	CHECK(images(IconRig::of(r.build({}, 0), h->getID())) == std::vector<std::string>{ "Good_Vet_Dot", "Good_Vet_Dot", "Good_Vet_Dot" });
	{
		// RW 0x67939F: a HORDE's marks are drawn on its banner carrier's drawable (H+0x26C), else its first member's, at that member's own anchor (RW 0x6778F4:
		// the drawable's position raised by its template's HealthBoxHeightOffset and its geometry's height), centred, their bottom 7 pixels above (RW 0x6780DF)
		const HordeContain *hc = dynamic_cast<const HordeContain *>(h->getContain());
		REQUIRE(hc);
		const Object *carrier = hc->bannerCarrier() ? r.rig.game->logic().findObjectByID(hc->bannerCarrier()) : nullptr;
		if (!carrier)
		{
			REQUIRE(!hc->getContainedItemsList()->empty());
			carrier = hc->getContainedItemsList()->front();
		}
		MESSAGE("marks carrier " << carrier->getTemplate()->getName() << (hc->bannerCarrier() ? std::string(" (banner carrier)") : std::string(" (first member)")));
		const Drawable *cd = r.rig.game->drawables().findByObject(carrier->getID());
		REQUIRE(cd);
		ICoord2D sp;
		REQUIRE(r.rig.view.worldToScreen(*cd->getPosition(), sp));
		float lo = 1e9f, hi = -1e9f, bottom = 0.0f;
		for (const IconUIOp &op : IconRig::of(r.build({}, 0), h->getID()))
		{
			if (op.kind == IconUIOp::IMAGE)
			{
				lo = std::min(lo, op.x);
				hi = std::max(hi, op.x + op.w);
				bottom = op.y + op.h;
			}
		}
		MESSAGE("marks " << lo << " .. " << hi << " bottom " << bottom << "; the carrier's foot on screen " << sp.x << ", " << sp.y);
		CHECK(std::fabs((lo + hi) * 0.5f - (float)sp.x) <= 2.0f);
		CHECK(bottom < (float)sp.y - 7.0f); // above the member, raised by its height
		// the members themselves draw none (RW 0x6939DF)
		for (const Object *m : *hc->getContainedItemsList())
		{
			for (const IconUIOp &op : IconRig::of(r.build({}, 0), m->getID()))
			{
				CHECK(op.kind != IconUIOp::IMAGE);
			}
		}
	}
	xp->gainLevels(6 - xp->getRank(), false);
	r.rig.game->advance(0.2);
	if (xp->getRank() == 6)
	{
		const std::vector<IconUIOp> ops = IconRig::of(r.build({}, 0), h->getID());
		CHECK(images(ops) == std::vector<std::string>{ "Good_Vet_Dot", "Good_Vet" });
		// the big icon sits above the dots
		CHECK(ops[1].y < ops[0].y);
	}
	Player *mordor = r.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(mordor);
	Object *m = r.place("MordorFighterHorde", mordor, 760.0f, 700.0f);
	r.rig.game->advance(0.2);
	m->getExperienceTracker()->gainLevels(1, false);
	r.rig.game->advance(0.2);
	REQUIRE(m->getExperienceTracker()->getRank() == 2);
	CHECK(images(IconRig::of(r.build({}, 0), m->getID())) == std::vector<std::string>{ "Evil_Vet_Dot", "Evil_Vet_Dot" });
}

// RW 0x4853EB / 0x6763DA: an enemy's invisible object (model condition INVISIBLE_STEALTH, RW 0x68FC2F) is not drawn for the local player and queues no icons:
// no bar on hover or selection, no marks; the local player's own invisible hero keeps its bar
TEST_CASE("hud5 icons retail: an enemy's invisible unit shows no health bar or marks; an own invisible hero keeps its bar")
{
	if (!haveWorld("hud5 icons stealth"))
	{
		return;
	}
	IconRig r(shared());
	Player *enemy = r.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	const int invisible = CombatNames::modelCondition("INVISIBLE_STEALTH");
	REQUIRE(invisible >= 0);
	Object *foe = r.place("MordorGothmog", enemy, 1000.0f, 800.0f);
	Object *own = r.place("GondorBoromir", r.rig.local, 1080.0f, 800.0f);
	r.rig.game->advance(0.2);
	r.look(*foe);
	CHECK(!IconRig::of(r.build({ foe->getID() }, 0), foe->getID()).empty()); // visible: its bar when selected
	foe->setModelConditionState(invisible, true);
	own->setModelConditionState(invisible, true);
	CHECK(IconRig::of(r.build({ foe->getID() }, 0), foe->getID()).empty());
	CHECK(IconRig::of(r.build({}, foe->getID()), foe->getID()).empty());
	CHECK(IconRig::of(r.build({ own->getID() }, 0), own->getID()).size() == 6);
}

TEST_CASE("hud5 icons: the acceptance stops are reported")
{
	const std::vector<std::string> stops = DrawableIconUI::acceptanceStops();
	REQUIRE(stops.size() == 2);
	CHECK(stops[0].rfind("[S-1950]", 0) == 0);
	CHECK(stops[1].rfind("[S-1951]", 0) == 0);
}
