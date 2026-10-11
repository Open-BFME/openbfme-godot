// OpenBFME retail tests of the Palantir's hero bar (lane UI-4, GameClient/InGameHeroSelect.h): the hero / builder lists (RW 0x92CD78 / 0x92C428), the update's
// movie calls (RW 0x92CF64), a button press and its second click (RW 0x92DB91), select all (RW 0x92C8C4) and the nearest idle builder (RW 0x92D9F4). The movie
// is a recorder of the calls the interface makes. SKIP loudly without the installs. GPL-3.0.

#include "HudTestUtil.h"

#include "GameClient/InGameHeroSelect.h"
#include "GameLogic/Object/Object.h"

#include <map>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
struct Recorder
{
	std::vector<std::string> calls;
	std::map<std::string, std::string> images, texts;
	InGameHeroSelect::Movie movie()
	{
		InGameHeroSelect::Movie m;
		m.call = [this](const std::string &fn, const std::vector<std::string> &args) {
			std::string c = fn + "(";
			for (size_t i = 0; i < args.size(); ++i)
			{
				c += (i ? "," : "") + args[i];
			}
			calls.push_back(c + ")");
			return true;
		};
		m.setImage = [this](const std::string &key, const std::string &image) {
			if (image.empty())
			{
				images.erase(key);
			}
			else
			{
				images[key] = image;
			}
		};
		m.setText = [this](const std::string &record, const std::string &text) { texts[record] = text; };
		return m;
	}
	bool called(const std::string &c) const
	{
		for (const std::string &x : calls)
		{
			if (x == c)
			{
				return true;
			}
		}
		return false;
	}
};

std::vector<ObjectID> idsOf(const std::list<InGameHeroSelect::Hero> &l)
{
	std::vector<ObjectID> out;
	for (const InGameHeroSelect::Hero &h : l)
	{
		out.push_back(h.id);
	}
	return out;
}
} // namespace

TEST_CASE("ui4 herobar retail: idle builders take slot 1, heroes follow by HeroSortOrder at the start; a click selects, the second moves the camera")
{
	if (!haveWorld("ui4 herobar"))
	{
		return;
	}
	Rig rig(shared());
	HudContext &ctx = rig.input->context();
	InGameHeroSelect hs(ctx);
	Recorder rec;
	// the starting objects of the map (the fortress and its builders), then two heroes created in the first frames (logic frame < 6: sorted by HeroSortOrder)
	const Coord3D c = rig.freeSpot(2800, 1400, 200.0f);
	Object *faramir = rig.make("GondorFaramir", c.x + 200.0f, c.y);
	Object *boromir = rig.make("GondorBoromir", c.x - 200.0f, c.y);
	rig.make("MenPorter", c.x, c.y + 150.0f);
	rig.make("MenPorter", c.x + 100.0f, c.y + 150.0f);
	REQUIRE(rig.logic().getFrame() < 6);
	hs.trackObjects();
	// the HeroSortOrder of each (a template that sets none keeps the ctor's 0x7FFFFFFF, RW 0x74006E); equal keys keep the creation order
	auto sortOf = [](const Object *o) -> long long {
		const FieldValue *v = o->getTemplate()->findField("HeroSortOrder");
		return v ? std::get<long long>(*v) : 0x7FFFFFFFLL;
	};
	MESSAGE("HeroSortOrder: Faramir " << sortOf(faramir) << ", Boromir " << sortOf(boromir));
	REQUIRE(hs.heroes().size() == 2);
	const std::vector<ObjectID> expectOrder = sortOf(boromir) < sortOf(faramir) ? std::vector<ObjectID>{ boromir->getID(), faramir->getID() }
																			   : std::vector<ObjectID>{ faramir->getID(), boromir->getID() };
	CHECK(idsOf(hs.heroes()) == expectOrder);
	CHECK(hs.builders().size() == 2); // KindOf PORTER

	hs.attach(3, "_level3.HeroSelectUI", rec.movie(), "Men");
	CHECK(hs.name() == "HeroSelectUI");
	CHECK(hs.commandPrefix() == "_level3.HeroSelectUI");
	CHECK(rec.called("SetFaction(Men)"));
	// the first update only shows the movie
	hs.update();
	CHECK(rec.called("Show()"));
	CHECK(hs.shown());
	CHECK_FALSE(hs.slot(0).builder);
	rec.calls.clear();
	hs.update();
	// slot 1: the idle builders (their count as the rank record), then the heroes
	REQUIRE(hs.slot(0).builder);
	CHECK(rec.called("SetButtonState(1,_up)"));
	CHECK(rec.called("SetButtonHealthBar(1,100)"));
	CHECK(rec.called("SetButtonRankProgress(1,1)"));
	CHECK(rec.texts["APT:_level3.HeroSelectUI_Hero1Rank"] == std::to_string(hs.slot(0).rank));
	CHECK(hs.slot(0).rank == 2); // both porters are idle
	CHECK(!rec.images["_level3.HeroSelectUI_Hero1Image"].empty());
	CHECK(hs.slot(1).hero == expectOrder[0]);
	CHECK(hs.slot(2).hero == expectOrder[1]);
	CHECK(rec.images["_level3.HeroSelectUI_Hero2Image"] == InGameHeroSelect::buttonImage(*rig.logic().findObjectByID(expectOrder[0])));
	CHECK(rec.called("SetButtonState(2,_up)"));
	CHECK(rec.called("SetButtonState(3,_up)"));
	CHECK(rec.called("SetButtonHealthBar(2,100)"));
	CHECK(rec.texts["APT:_level3.HeroSelectUI_Hero2Rank"] == "1");
	CHECK(rec.called("SetSelectAllHeroesButtonState(_up)"));
	CHECK(hs.selectAllShown());
	// the slots past the heroes stay unused
	CHECK(hs.slot(3).hero == INVALID_ID);
	CHECK(rec.images.count("_level3.HeroSelectUI_Hero4Image") == 0);

	// a click on Hero2 selects that hero (MSG_CREATE_SELECTED_GROUP); the second click on it, the only selection, moves the camera to it
	const ObjectID first = expectOrder[0];
	hs.onButtonPressed("Hero2");
	REQUIRE(ctx.ui.selected().size() == 1);
	CHECK(ctx.ui.selected().front() == first);
	rig.lookAt({ c.x + 3000.0f, c.y + 3000.0f, 0 });
	hs.onButtonPressed("Hero2");
	const Coord3D look = ctx.view.position();
	const Coord3D *at = rig.logic().findObjectByID(first)->getPosition();
	CHECK(std::abs(look.x - at->x) < 1.0f);
	CHECK(std::abs(look.y - at->y) < 1.0f);
	rec.calls.clear();
	hs.update();
	CHECK(rec.called("SetButtonSelectedHighlightState(2,_show)"));

	// select all: both heroes
	hs.selectAllHeroes();
	CHECK(ctx.ui.selected().size() == 2);
	CHECK(ctx.ui.isSelected(boromir->getID()));
	CHECK(ctx.ui.isSelected(faramir->getID()));

	// the builder button selects an idle builder
	hs.onButtonPressed("Hero1");
	REQUIRE(ctx.ui.selected().size() == 1);
	bool isBuilder = false;
	for (const InGameHeroSelect::Builder &b : hs.builders())
	{
		isBuilder = isBuilder || b.id == ctx.ui.selected().front();
	}
	CHECK(isBuilder);

	// a hero with a HeroSortOrder created in the first frames goes before the ones without (Argeleb: 50)
	{
		InGameHeroSelect early(ctx);
		Object *argeleb = rig.make("ArnorArgeleb", c.x, c.y - 250.0f);
		REQUIRE(rig.logic().getFrame() < 6);
		early.trackObjects();
		REQUIRE(early.heroes().size() == 3);
		// Argeleb (50) after Boromir (50, equal: creation order), before Faramir (none: 0x7FFFFFFF)
		auto it = early.heroes().begin();
		CHECK((it++)->id == boromir->getID());
		CHECK((it++)->id == argeleb->getID());
		CHECK(it->id == faramir->getID());
		rig.logic().destroyObject(argeleb);
	}
	// a hero created later (frame >= 6) is appended; a destroyed hero's slot goes unused
	rig.frame(8);
	Object *third = rig.make("RohanEowyn", c.x, c.y + 250.0f);
	hs.trackObjects();
	REQUIRE(hs.heroes().size() == 3);
	CHECK(hs.heroes().back().id == third->getID());
	hs.update();
	CHECK(hs.slot(3).hero == third->getID()); // builder, Boromir, Faramir, Eowyn
	rig.logic().destroyObject(boromir);
	rig.frame(2);
	hs.trackObjects();
	CHECK(hs.heroes().size() == 2);
	rec.calls.clear();
	hs.update();
	CHECK(rec.called("SetButtonState(4,_unused)"));
	CHECK(rec.images.count("_level3.HeroSelectUI_Hero4Image") == 0);
}
