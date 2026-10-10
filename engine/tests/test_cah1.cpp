// OpenBFME tests (lane CAH-1): the Create-a-Hero screens. The hero list (system heroes of Data1.big, profile files saved, reloaded and deleted), the edited
// hero's slots and points (AptMyHero, RW 0x9C0AEE / 0x9BF539 / 0x9BFD40), the power matrix (RW 0x9C392E / 0x9C293D), and the builder CreateAHero.apt run
// through the shell with the retail movies: a hero built page by page and saved, then valid for the game setup (CreateAHeroSystem::validateHero).
// Independent expectations: the points and slot ranges are recomputed here from the INI data (the subclass's Attribute upgrades in the group lists), the
// system hero Fhaleen's powers were read from its .cah file with a separate Python reader (the slot rule: a new power row takes the next palantir slot after
// the template's one button, its later tiers keep it).

#include "doctest.h"

#include "StartShellFx.h"

#include "Common/CreateAHeroRecord.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/CreateAHeroHeroList.h"
#include "GameClient/GUI/AptScreens/AptCreateAHero.h"
#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptColorPicker.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameNetwork/LANAPI.h"
#include "GameClient/GUI/CreateAHero/AptMyHero.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameClient/LiveGame.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameLogic/NewGame/NewGame.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>

namespace
{
std::string tempProfileDir()
{
	std::random_device rd;
	const std::filesystem::path p = std::filesystem::temp_directory_path() / ("openbfme-cah1-" + std::to_string(rd()));
	std::filesystem::create_directories(p);
	return p.u8string();
}

struct TempDir
{
	std::string path = tempProfileDir();
	~TempDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(std::filesystem::u8path(path), ec);
	}
};

int itemOf(GameWindow *combo, int row)
{
	return (int)reinterpret_cast<std::intptr_t>(GadgetComboBoxGetItemData(combo, row));
}

// the position in the subclass's group list of the bling of `upgrade`
int positionOf(const CreateAHeroSystem &sys, const CreateAHeroSubClass &sub, const std::string &group, const std::string &upgrade)
{
	const std::vector<int> *list = sub.findGroup(group);
	REQUIRE(list);
	for (size_t i = 0; i < list->size(); ++i)
	{
		if (sys.bling((*list)[i])->upgradeName == upgrade)
		{
			return (int)i;
		}
	}
	return -1;
}
} // namespace

TEST_CASE("cah1: a profile hero is saved as MyHero_<id>.cah, listed after the system heroes, loads back equal and is deleted with its file")
{
	TempDir dir;
	CreateAHeroHeroList list;
	std::vector<std::string> errors;
	list.load(nullptr, dir.path, &errors);
	CHECK(errors.empty());
	CHECK(list.size() == 0);
	CreateAHeroHero h;
	h.name = u"Testhero";
	h.classIndex = 1;
	h.subClassIndex = 2;
	h.setBling("CreateAHero_Weapon", 1);
	h.powers[0] = { "Command_Foo", 0, 1 };
	std::string error;
	CHECK_FALSE(list.save(h, &error)); // no unique id yet (RW 0x80A352 gives one when the hero is named)
	h.uniqueID = CreateAHeroHeroList::newUniqueID();
	CHECK(h.uniqueID.size() >= 7);
	int index = -1;
	REQUIRE_MESSAGE(list.save(h, &error, &index), error);
	CHECK(index == 0);
	CHECK(std::filesystem::exists(std::filesystem::u8path(dir.path) / ("MyHero_" + h.uniqueID + ".cah")));
	CreateAHeroHeroList again;
	again.load(nullptr, dir.path, &errors);
	REQUIRE(again.size() == 1);
	CHECK(again.at(0)->hero.name == u"Testhero");
	CHECK(again.at(0)->hero.valid);
	CHECK_FALSE(again.at(0)->system);
	CHECK(again.at(0)->hero.powers[0].commandButton == "Command_Foo");
	// saving again replaces the entry
	h.name = u"Renamed";
	REQUIRE(again.save(h, &error));
	CHECK(again.size() == 1);
	CHECK(again.at(0)->hero.name == u"Renamed");
	// a broken file is an error and is left out
	{
		std::ofstream bad(std::filesystem::u8path(dir.path) / "MyHero_broken.cah", std::ios::binary);
		bad << "nope";
	}
	CreateAHeroHeroList third;
	errors.clear();
	third.load(nullptr, dir.path, &errors);
	CHECK(third.size() == 1);
	CHECK(errors.size() == 1);
	REQUIRE(again.remove(0, &error));
	CHECK(again.size() == 0);
	CHECK_FALSE(std::filesystem::exists(std::filesystem::u8path(dir.path) / ("MyHero_" + h.uniqueID + ".cah")));
}

TEST_CASE("cah1 retail: the hero list holds the 8 system heroes of Data1.big first; a system hero cannot be deleted")
{
	OPENBFME_REQUIRE_START(s);
	TempDir dir;
	CreateAHeroHeroList list;
	std::vector<std::string> errors;
	list.load(s->mount->fs.get(), dir.path, &errors);
	CHECK(errors.empty());
	REQUIRE(list.size() == 8);
	for (const CreateAHeroListEntry &e : list.entries())
	{
		CHECK(e.system);
		CHECK(e.hero.isSystemHero);
		CHECK(e.hero.valid);
	}
	std::string error;
	CHECK_FALSE(list.remove(0, &error));
	CreateAHeroHero mine = list.at(0)->hero;
	mine.isSystemHero = false;
	mine.uniqueID = "ABCDEF1";
	mine.name = u"Mine";
	REQUIRE(list.save(mine, &error));
	REQUIRE(list.size() == 9);
	CHECK(list.at(8)->hero.name == u"Mine");
	CHECK_FALSE(list.at(8)->system);
}

TEST_CASE("cah1 retail: the edited hero's attribute slots, points and steps follow RW 0x9C0AEE / 0x9BF539 / 0x9BFD40")
{
	OPENBFME_REQUIRE_START(s);
	const CreateAHeroSystem &sys = s->world->createAHeroSystem();
	REQUIRE(sys.classes().size() > 0);
	const CreateAHeroSubClass *sub = sys.subClass(0, 0);
	REQUIRE(sub);
	AptMyHero my(sys, nullptr, nullptr);
	CreateAHeroHero h;
	h.classIndex = 0;
	h.subClassIndex = 0;
	my.edit(h, true);
	// a new hero: every attribute at its minimum, every appearance at its default, all points left
	CHECK(my.maximumPoints() == sub->spendableAttributePoints);
	CHECK(my.remainingPoints() == sub->spendableAttributePoints);
	const std::vector<AptMyHero::Slot> &attributes = my.slots(AptMyHero::KIND_ATTRIBUTE);
	REQUIRE(attributes.size() == 5);
	int defaultCost = 0;
	for (size_t i = 0; i < attributes.size(); ++i)
	{
		const AptMyHero::Slot &a = attributes[i];
		const CreateAHeroAttribute *attr = sub->findAttribute(a.group);
		REQUIRE_MESSAGE(attr, a.group);
		CHECK(a.minimum == positionOf(sys, *sub, a.group, attr->minValueUpgrade));
		CHECK(a.maximum == positionOf(sys, *sub, a.group, attr->maxValueUpgrade));
		CHECK(a.defaultIndex == positionOf(sys, *sub, a.group, attr->defaultValueUpgrade));
		CHECK(a.count == a.maximum - a.minimum + 1);
		CHECK(my.currentIndex(AptMyHero::KIND_ATTRIBUTE, (int)i) == a.minimum);
		defaultCost += a.defaultIndex - a.minimum;
	}
	// AttribRecommend: the defaults cost their distance from the minimum
	my.setDefaults(AptMyHero::KIND_ATTRIBUTE);
	CHECK(my.remainingPoints() == sub->spendableAttributePoints - defaultCost);
	// a step up costs a point; down returns it; never below the minimum
	const int before = my.remainingPoints();
	my.step(AptMyHero::KIND_ATTRIBUTE, 0, -1);
	CHECK(my.remainingPoints() == before + 1);
	my.step(AptMyHero::KIND_ATTRIBUTE, 0, 1);
	CHECK(my.remainingPoints() == before);
	if (before == 0) // the retail recommendation spends every point: a further step is refused
	{
		const int cur = my.currentIndex(AptMyHero::KIND_ATTRIBUTE, 1);
		my.step(AptMyHero::KIND_ATTRIBUTE, 1, 1);
		CHECK(my.currentIndex(AptMyHero::KIND_ATTRIBUTE, 1) == cur);
		CHECK(my.remainingPoints() == 0);
	}
	my.resetToMinimum(AptMyHero::KIND_ATTRIBUTE);
	my.step(AptMyHero::KIND_ATTRIBUTE, 0, -1);
	CHECK(my.currentIndex(AptMyHero::KIND_ATTRIBUTE, 0) == attributes[0].minimum);
	CHECK(my.remainingPoints() == sub->spendableAttributePoints);
	// spending every point stops the steps
	for (int k = 0; k < 400; ++k)
	{
		my.step(AptMyHero::KIND_ATTRIBUTE, k % 5, 1);
	}
	CHECK(my.remainingPoints() >= 0);
	int spent = 0;
	for (size_t i = 0; i < attributes.size(); ++i)
	{
		spent += my.currentIndex(AptMyHero::KIND_ATTRIBUTE, (int)i) - attributes[i].minimum;
		CHECK(my.currentIndex(AptMyHero::KIND_ATTRIBUTE, (int)i) <= attributes[i].maximum);
	}
	CHECK(spent + my.remainingPoints() == sub->spendableAttributePoints);
	// the appearance arrows wrap
	const std::vector<AptMyHero::Slot> &app = my.slots(AptMyHero::KIND_APPEARANCE);
	REQUIRE(app.size() >= 1);
	for (size_t i = 0; i < app.size(); ++i)
	{
		if (app[i].count < 2)
		{
			continue;
		}
		my.setSlotIndex(AptMyHero::KIND_APPEARANCE, (int)i, app[i].maximum);
		my.step(AptMyHero::KIND_APPEARANCE, (int)i, 1);
		CHECK(my.currentIndex(AptMyHero::KIND_APPEARANCE, (int)i) == app[i].minimum);
		my.step(AptMyHero::KIND_APPEARANCE, (int)i, -1);
		CHECK(my.currentIndex(AptMyHero::KIND_APPEARANCE, (int)i) == app[i].maximum);
	}
	// the providers
	std::string v;
	CHECK(my.provide("MyHero::MaxAttribute", v));
	CHECK(v == "20");
	CHECK(my.provide("MyHero::BaseAttrib_0", v));
	CHECK(v == std::to_string(attributes[0].minimum + 1));
	CHECK(my.provide("MyHero::IsSystemHero", v));
	CHECK(v == "0");
}

TEST_CASE("cah1 retail: the power matrix rebuilds Fhaleen's powers on the same slots (RW 0x9C392E / 0x9C293D)")
{
	OPENBFME_REQUIRE_START(s);
	const CreateAHeroSystem &sys = s->world->createAHeroSystem();
	CreateAHeroHeroList list;
	list.load(s->mount->fs.get(), std::string(), nullptr);
	const CreateAHeroHero *fhaleen = nullptr;
	for (const CreateAHeroListEntry &e : list.entries())
	{
		if (e.hero.name == u"Fhaleen")
		{
			fhaleen = &e.hero;
		}
	}
	REQUIRE(fhaleen);
	CreateAHeroHero h = *fhaleen;
	CahPowers powers;
	powers.build(h, sys);
	CHECK(powers.templateButtons() == 1);
	CHECK(powers.chosenCount() == 10);
	for (int i = 0; i < CreateAHeroHero::POWER_COUNT; ++i)
	{
		CHECK_MESSAGE(h.powers[(size_t)i].commandButton == fhaleen->powers[(size_t)i].commandButton, i);
		CHECK_MESSAGE(h.powers[(size_t)i].expLevel == fhaleen->powers[(size_t)i].expLevel, i);
		CHECK_MESSAGE(h.powers[(size_t)i].buttonIndex == fhaleen->powers[(size_t)i].buttonIndex, i);
	}
	// taking back the last choice and choosing it again
	powers.truncate(9, h);
	CHECK(h.powers[9].commandButton.empty());
	CHECK(powers.chosenCount() == 9);
	// the book is full at 10
	powers.truncate(0, h);
	CHECK(powers.chosenCount() == 0);
	for (int i = 0; i < 10; ++i)
	{
		CHECK(powers.select(nullptr, h)); // "no power" any number of times
	}
	CHECK_FALSE(powers.select(nullptr, h));
	CHECK(h.powers[0].buttonIndex == (std::uint32_t)(CahPowers::NO_POWER_SLOT + powers.templateButtons()));
}

namespace
{
struct CahShellFx
{
	starttest::Shared *s;
	TempDir dir;
	CreateAHeroHeroList heroes;
	CreateAHeroScreenContext ctx;
	std::unique_ptr<starttest::ShellFx> fx;
	explicit CahShellFx(starttest::Shared *shared, bool suppressPromo) : s(shared)
	{
		std::vector<std::string> errors;
		heroes.load(s->mount->fs.get(), dir.path, &errors);
		REQUIRE(errors.empty());
		ctx.system = &s->world->createAHeroSystem();
		ctx.heroes = &heroes;
		ctx.suppressPromo = suppressPromo;
		ctx.random = [](int lo, int hi) { return (lo + hi) / 2; };
		fx = std::make_unique<starttest::ShellFx>(*s, 1u);
		fx->environment.createAHero = &ctx;
		fx->environment.services = &fx->services;
		fx->shell->push("MainMenu.apt");
		fx->tick(20);
		REQUIRE(fx->wm->invokeCallback("AptMainMenu::CreateAHero", ""));
		fx->tick(60);
	}
	AptCreateAHero *screen() { return dynamic_cast<AptCreateAHero *>(fx->shell->findScreenByFilename("CreateAHero.apt")); }
	void show(AptCreateAHero *scr, const std::string &page)
	{
		std::string error;
		REQUIRE_MESSAGE(fx->wm->invokeAS(scr->level(), "ShowScreen", { page }, nullptr, &error), error);
		fx->tick(40);
	}
};
} // namespace

TEST_CASE("cah1 retail shell: the main menu's Create-a-Hero pushes CreateAHero.apt, the promo page first, then the Manager lists the system heroes")
{
	OPENBFME_REQUIRE_START(s);
	{
		CahShellFx c(s, false);
		AptCreateAHero *scr = c.screen();
		REQUIRE(scr);
		CHECK(c.fx->services.countRequests(ShellAction::CreateAHero) == 1);
		// SuppressCAHPromo "0": the promo movie CahNewFeatures, which is no page (OnShowScreen 'N' leaves the page, RW 0x919CEE)
		CHECK(scr->currentPage() == 0);
		CHECK(c.fx->wm->movieLoads().size() > 0);
		CHECK(c.fx->wm->movieLoads().back().movie.find("CahNewFeatures") != std::string::npos);
		for (const WindowManagerNote &n : c.fx->wm->notes())
		{
			if (n.kind != "create-a-hero-stop")
			{
				MESSAGE("note " << n.kind << ": " << n.detail);
			}
		}
		for (const std::string &e : c.fx->wm->errors())
		{
			MESSAGE("apt error: " << e);
		}
	}
	CahShellFx c(s, true);
	AptCreateAHero *scr = c.screen();
	REQUIRE(scr);
	CHECK(scr->currentPage() == 'M');
	// stops S-1400 .. S-1405 and S-1408 (lane CAH-2): every one is reported by the screen
	REQUIRE(AptCreateAHero::stopLines().size() == 7);
	CHECK(c.fx->wm->noteCount("create-a-hero-stop") == 7);
	for (int k = 0; k < 6; ++k)
	{
		CHECK(AptCreateAHero::stopLines()[(size_t)k].rfind("[S-140" + std::to_string(k) + "]", 0) == 0);
	}
	CHECK(AptCreateAHero::stopLines()[6].rfind("[S-1408]", 0) == 0);
	REQUIRE(scr->heroList());
	CHECK(GadgetListBoxGetNumEntries(scr->heroList()) == 8);
	CHECK(scr->selectedHero() == 0);
	for (const WindowManagerNote &n : c.fx->wm->notes())
	{
		if (n.kind != "unported-command" && n.kind != "create-a-hero-stop")
		{
			MESSAGE("note " << n.kind << ": " << n.detail);
		}
	}
	CHECK(c.fx->wm->errors().empty());
	for (const std::string &e : c.fx->wm->errors())
	{
		MESSAGE("apt error: " << e);
	}
}

TEST_CASE("cah1 retail shell: a hero is built page by page through the movies (class, appearance, name, powers), saved, and valid for the game setup")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	AptCreateAHero *scr = c.screen();
	REQUIRE(scr);
	std::string error;
	REQUIRE_MESSAGE(c.fx->wm->invokeAS(scr->level(), "SetCreateNewHero", { "true" }, nullptr, &error), error);
	c.show(scr, "Class");
	CHECK(scr->currentPage() == 'C');
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Class::SetClassAndType", "2 1"));
	REQUIRE(scr->displayedHero());
	CHECK(scr->displayedHero()->classIndex == 2);
	CHECK(scr->displayedHero()->subClassIndex == 1);
	c.show(scr, "Appearance");
	CHECK(scr->currentPage() == 'A');
	REQUIRE(scr->myHero());
	CHECK(scr->myHero()->hero().classIndex == 2);
	CHECK(scr->myHero()->hero().subClassIndex == 1);
	// the appearance arrows and the attribute buttons through their commands
	const int before = scr->myHero()->currentIndex(AptMyHero::KIND_APPEARANCE, 0);
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Appearance::NextAppearance", "0"));
	CHECK(scr->myHero()->currentIndex(AptMyHero::KIND_APPEARANCE, 0) != before);
	const int points = scr->myHero()->remainingPoints(); // a new hero: every attribute at its minimum
	REQUIRE(points > 0);
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Appearance::IncreaseAttribute", "1"));
	CHECK(scr->myHero()->remainingPoints() == points - 1);
	// the name: the entry, then the provider the Next button asks
	REQUIRE(scr->nameEntry());
	GadgetTextEntrySetText(scr->nameEntry(), u"Gwaihir Junior");
	std::string v;
	CHECK(c.fx->wm->getExtern("CahAppearance::HeroNameSet", v) == AptExternResult::Number);
	CHECK(v == "1");
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Appearance::OnComplete", ""));
	CHECK(scr->myHero()->hero().name == u"Gwaihir Junior");
	CHECK_FALSE(scr->myHero()->hero().uniqueID.empty());
	c.show(scr, "Powers");
	CHECK(scr->currentPage() == 'P');
	CHECK(scr->powers().rowCount() > 0);
	{
		// the matrix starts at its first row (S-1404): mcRows at its mask
		AptCharacterInst *rows = c.fx->at(scr->level(), "Main.mcPowersScreen.Screen.mcPowersMenu.mcRows");
		AptCharacterInst *mask = c.fx->at(scr->level(), "Main.mcPowersScreen.Screen.mcPowersMenu.mcMaskAvailPowers");
		REQUIRE(rows);
		REQUIRE(mask);
		CHECK(rows->matrix.ty == doctest::Approx(mask->matrix.ty));
	}
	// the first available power of the matrix, then "no power", then another
	int chosen = 0;
	for (int k = 0; k < 3; ++k)
	{
		bool picked = false;
		for (int r = 0; r < scr->powers().rowCount() && !picked; ++r)
		{
			for (int col = 0; col < CahPowers::NUM_COLUMNS && !picked; ++col)
			{
				CahPowers::Cell *cell = scr->powers().cell(r, col);
				if (cell && cell->state == CahPowers::STATE_AVAILABLE)
				{
					REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::OnPowerSelect", std::to_string(r + 1) + "," + std::to_string(col + 1)));
					c.fx->tick(2);
					picked = true;
				}
			}
		}
		if (picked)
		{
			++chosen;
		}
		if (k == 0)
		{
			REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::OnNoPowerSelect", ""));
			c.fx->tick(2);
			++chosen;
		}
	}
	CHECK(scr->powers().chosenCount() == chosen);
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::OnPowerSelectionComplete", ""));
	REQUIRE(c.heroes.size() == 9);
	const int saved = c.heroes.findByUniqueID(scr->myHero()->hero().uniqueID);
	REQUIRE(saved >= 0);
	const CreateAHeroHero &h = c.heroes.at(saved)->hero;
	CHECK(h.name == u"Gwaihir Junior");
	CHECK(h.valid);
	CHECK_FALSE(h.isSystemHero);
	// what the lobby would send: the saved record passes the game setup's field checks
	REQUIRE(TheCommandStore);
	std::string why;
	CHECK_MESSAGE(s->world->createAHeroSystem().validateHero(h, *TheCommandStore, &why), why);
	// back on the Manager it is listed and selected
	c.show(scr, "Manager");
	REQUIRE(scr->heroList());
	CHECK(scr->currentPage() == 'M');
	CHECK(GadgetListBoxGetNumEntries(scr->heroList()) == 9);
	CHECK(scr->selectedHero() == saved);
	// and the delete removes it after the question's Yes (lane CAH-2: RW 0x9C5D6F / 0x9C5D16; a system hero is refused)
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Manager::OnDeleteHero", ""));
	REQUIRE(scr->messageBox());
	c.fx->tick(3);
	REQUIRE(c.fx->wm->invokeCallback("_level" + std::to_string(scr->messageBox()->level()) + ".MessageBox_OnButtonYes", ""));
	c.fx->tick(3);
	CHECK(c.heroes.size() == 8);
	CHECK(c.fx->wm->errors().empty());
	for (const std::string &e : c.fx->wm->errors())
	{
		MESSAGE("apt error: " << e);
	}
}

TEST_CASE("cah1 retail lobby: the Skirmish lobby's Hero combo lists '-', Random and every hero (RW 0x842C93); a chosen hero travels whole in the new-game message")
{
	OPENBFME_REQUIRE_START(s);
	TempDir dir;
	CreateAHeroHeroList heroes;
	heroes.load(s->mount->fs.get(), dir.path, nullptr);
	REQUIRE(heroes.size() == 8);
	CreateAHeroScreenContext ctx;
	ctx.system = &s->world->createAHeroSystem();
	ctx.heroes = &heroes;
	starttest::ShellFx fx(*s, 77u);
	fx.text.set("GUI:Random", "Random");
	fx.text.set("VALUE:Default", " (Default)");
	fx.environment.createAHero = &ctx;
	fx.shell->push("MainMenu.apt");
	fx.tick(10);
	const int menuLevel = fx.shell->top()->level();
	std::string asError;
	REQUIRE_MESSAGE(fx.wm->invokeAS(menuLevel, "ShowMainMenu", {}, nullptr, &asError), asError);
	fx.tick(60);
	starttest::clickNavItem(fx, menuLevel, "SoloPlayNav", "Skirmish");
	auto *screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(screen);
	fx.tick(60);
	REQUIRE(screen->profileEntryWindow());
	starttest::typeText(fx, screen->profileEntryWindow(), u"Gimli");
	AptButtonInst *select = starttest::popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	starttest::clickButton(fx, *select);
	fx.tick(40);
	// slot 0 plays the Men
	int men = -1;
	for (int i = 0; i < (int)fx.setup.factions().size(); ++i)
	{
		if (fx.setup.factions()[(size_t)i].templateName == "FactionMen")
		{
			men = i;
		}
	}
	REQUIRE(men >= 0);
	GameWindow *tcombo = screen->slotGadget(0, "PlayerTemplate");
	REQUIRE(tcombo);
	int tpos = -1;
	for (int r = 0; r < GadgetComboBoxGetLength(tcombo); ++r)
	{
		if (GadgetComboBoxGetItemData(tcombo, r) == reinterpret_cast<void *>(static_cast<std::intptr_t>(men)))
		{
			tpos = r;
		}
	}
	REQUIRE(tpos >= 0);
	starttest::chooseComboRow(fx, tcombo, tpos);
	GameWindow *hero = screen->slotGadget(0, "Hero");
	REQUIRE(hero);
	REQUIRE(GadgetComboBoxGetLength(hero) == 2 + 8);
	// stop S-1406: reported once by the lobby
	CHECK(fx.wm->noteCount("create-a-hero-stop") == 1);
	CHECK(std::string(AptSkirmish::kHeroComboStop).rfind("[S-1406]", 0) == 0);
	CHECK(itemOf(hero, 0) == -1);
	CHECK(itemOf(hero, 1) == -2);
	// the heroes from the last to the first; a system hero's name ends with VALUE:Default; one the Men cannot use is greyed and cannot be chosen
	const CreateAHeroSystem &sys = s->world->createAHeroSystem();
	int usableRow = -1, blockedRow = -1;
	for (int r = 2; r < GadgetComboBoxGetLength(hero); ++r)
	{
		const int index = itemOf(hero, r);
		CHECK(index == 8 - 1 - (r - 2));
		const CreateAHeroHero &h = heroes.at(index)->hero;
		const CreateAHeroSubClass *sub = sys.subClass(h.classIndex, h.subClassIndex);
		REQUIRE(sub);
		const bool usable = std::find(sub->usableFactions.begin(), sub->usableFactions.end(), 0) != sub->usableFactions.end(); // 0 = Men
		if (usable && usableRow < 0)
		{
			usableRow = r;
		}
		if (!usable && blockedRow < 0)
		{
			blockedRow = r;
		}
	}
	REQUIRE(usableRow >= 0);
	if (blockedRow >= 0)
	{
		starttest::chooseComboRow(fx, hero, blockedRow);
		CHECK(screen->slotHero(0) == -1);
		CHECK_FALSE(screen->setup()->info().slots[0].hasCreateAHero);
	}
	starttest::chooseComboRow(fx, hero, usableRow);
	const int chosen = itemOf(hero, usableRow);
	CHECK(screen->slotHero(0) == chosen);
	REQUIRE(screen->setup()->info().slots[0].hasCreateAHero);
	CHECK(screen->setup()->info().slots[0].createAHero.uniqueID == heroes.at(chosen)->hero.uniqueID);
	// Start: the message carries the record
	AptButtonInst *start = starttest::popupButton(fx, "lobby.StartGame");
	REQUIRE(start);
	starttest::clickButton(fx, *start);
	fx.tick(30);
	REQUIRE(fx.sink.messages.size() == 1);
	const SkirmishGameSlot &slot0 = fx.sink.messages[0].game.slots[0];
	REQUIRE(slot0.hasCreateAHero);
	CHECK(slot0.createAHero.save() == heroes.at(chosen)->hero.save());
}

namespace
{
// a game started from `message` (as test_start_retail.cpp's runStartedGame): the state hash of the load's first frame and of every advanced frame
std::vector<std::uint32_t> cahRunHashes(starttest::Shared &s, const NewGameMessage &message, int frames, std::string *installed)
{
	std::string error;
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	std::vector<std::uint32_t> hashes{ game.logic().computeStateHash() };
	for (int i = 0; i < frames; ++i)
	{
		game.advance(0.2);
		hashes.push_back(game.logic().computeStateHash());
	}
	if (installed)
	{
		installed->clear();
		for (const auto &h : game.logic().createAHeroes().heroes())
		{
			*installed += h.second.uniqueID + ";";
		}
	}
	return hashes;
}
} // namespace

TEST_CASE("cah1 retail determinism: a hero built in the builder and chosen in the lobby: two runs agree on every frame hash; no hero or another appearance changes the world")
{
	OPENBFME_REQUIRE_START(s);
	TempDir dir;
	CreateAHeroHeroList heroes;
	heroes.load(s->mount->fs.get(), dir.path, nullptr);
	const CreateAHeroSystem &sys = s->world->createAHeroSystem();
	// the builder's model: a Shield Maiden (class 0, subclass 1) with the recommended attributes, the next helmet and ten powers
	AptMyHero my(sys, nullptr, nullptr);
	CreateAHeroHero h;
	h.classIndex = 0;
	h.subClassIndex = 1;
	my.edit(h, true);
	my.setDefaults(AptMyHero::KIND_ATTRIBUTE);
	my.step(AptMyHero::KIND_APPEARANCE, 0, 1);
	my.setName(u"Hurin");
	CahPowers powers;
	powers.build(my.hero(), sys);
	for (int k = 0; k < 10; ++k)
	{
		CahPowers::Cell *pick = nullptr;
		for (int r = 0; r < powers.rowCount() && !pick; ++r)
		{
			for (int c = 0; c < CahPowers::NUM_COLUMNS && !pick; ++c)
			{
				CahPowers::Cell *cell = powers.cell(r, c);
				pick = cell && cell->state == CahPowers::STATE_AVAILABLE ? cell : nullptr;
			}
		}
		REQUIRE(powers.select(pick, my.hero()));
	}
	std::string error;
	REQUIRE(heroes.save(my.hero(), &error));
	const CreateAHeroHero &saved = heroes.at(heroes.findByUniqueID(my.hero().uniqueID))->hero;
	REQUIRE(TheCommandStore);
	std::string why;
	REQUIRE_MESSAGE(sys.validateHero(saved, *TheCommandStore, &why), why);

	// the lobby's message (the record travels whole on slot 0, the human)
	NewGameMessage message;
	{
		starttest::ShellFx fx(*s, 99u);
		CreateAHeroScreenContext ctx;
		ctx.system = &sys;
		ctx.heroes = &heroes;
		fx.environment.createAHero = &ctx;
		SkirmishGameSetup setup(fx.setup, &fx.text);
		REQUIRE_MESSAGE(setup.init(u"Gimli", 99u, "maps/map mp evendim/map mp evendim.map", &error), error);
		int men = -1;
		for (int i = 0; i < (int)fx.setup.factions().size(); ++i)
		{
			men = fx.setup.factions()[(size_t)i].templateName == "FactionMen" ? i : men;
		}
		REQUIRE(setup.setSlotTemplate(0, men));
		REQUIRE(setup.setSlotCreateAHero(0, &saved, &error));
		message = setup.makeNewGameMessage();
	}
	REQUIRE(message.game.slots[0].hasCreateAHero);
	std::string installedA, installedB, installedNone;
	const std::vector<std::uint32_t> a = cahRunHashes(*s, message, 8, &installedA);
	const std::vector<std::uint32_t> b = cahRunHashes(*s, message, 8, &installedB);
	CHECK(a == b);
	CHECK(installedA == saved.uniqueID + ";");
	NewGameMessage none = message;
	none.game.slots[0].clearCreateAHero();
	const std::vector<std::uint32_t> c = cahRunHashes(*s, none, 8, &installedNone);
	CHECK(installedNone.empty());
	CHECK(a != c);
	CreateAHeroHero other = saved;
	my.edit(other, false);
	my.step(AptMyHero::KIND_APPEARANCE, 0, 1);
	NewGameMessage changed = message;
	REQUIRE(changed.game.slots[0].setCreateAHero(my.hero(), &error));
	CHECK(cahRunHashes(*s, changed, 8, nullptr) != a);
}

TEST_CASE("cah1 retail LAN: the joiner's Hero combo goes through the host: every player's setup holds the record, the host shows it as text, the start carries it")
{
	OPENBFME_REQUIRE_START(s);
	TempDir dir;
	CreateAHeroHeroList heroes;
	heroes.load(s->mount->fs.get(), dir.path, nullptr);
	REQUIRE(heroes.size() == 8);
	CreateAHeroScreenContext ctx;
	ctx.system = &s->world->createAHeroSystem();
	ctx.heroes = &heroes;
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	const ProfileIdentity profile = ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {});
	auto lanOf = [&](const std::u16string &name) {
		LANAPI::Options o;
		o.playerTemplateCount = s->world->playerTemplates().getPlayerTemplateCount();
		o.colorCount = (int)s->settings.multiplayerColors.size();
		std::random_device rd;
		o.lobbyPortBase = (std::uint16_t)(45000 + (rd() % 900) * 16);
		o.lobbyPorts = 8;
		o.broadcast = false;
		o.extraTargets = { 0x7F000001u };
		o.resendMs = 300;
		o.actionTimeoutMs = 2000;
		o.validateCreateAHero = [&](const CreateAHeroHero &h, std::string *why) { return s->world->createAHeroSystem().validateHero(h, s->world->commands(), why); };
		return o;
	};
	LANAPI::Options ho = lanOf(u"Host");
	LANAPI::Options jo = ho; // the same port range: they find each other on localhost
	auto hostLan = std::make_unique<LANAPI>(profile, u"Host", ho);
	auto joinLan = std::make_unique<LANAPI>(profile, u"Joiner", jo);
	std::string error;
	REQUIRE_MESSAGE(hostLan->open(&error), error);
	REQUIRE_MESSAGE(joinLan->open(&error), error);
	starttest::ShellFx host(*s, 21);
	starttest::ShellFx join(*s, 22);
	for (starttest::ShellFx *fx : { &host, &join })
	{
		fx->environment.createAHero = &ctx;
		fx->environment.services = &fx->services;
		fx->text.set("GUI:Random", "Random");
		fx->text.set("VALUE:Default", " (Default)");
	}
	host.environment.lan = hostLan.get();
	join.environment.lan = joinLan.get();
	host.shell->push("LanLobby.apt");
	join.shell->push("LanLobby.apt");
	AptLanLobby *hs = dynamic_cast<AptLanLobby *>(host.shell->top());
	AptLanLobby *js = dynamic_cast<AptLanLobby *>(join.shell->top());
	REQUIRE(hs);
	REQUIRE(js);
	auto run = [&](const std::function<bool()> &done, int limit = 300) {
		for (int i = 0; i < limit; ++i)
		{
			host.tick(1);
			join.tick(1);
			NetSleepMilliseconds(2);
			if (done())
			{
				return true;
			}
		}
		return false;
	};
	REQUIRE(run([&] { return hs->state() == 1 && js->state() == 1; }));
	CHECK(host.wm->invokeCallback("AptLanLobby::OnCreateGameBttn", ""));
	REQUIRE(run([&] { return hs->state() == 4; }));
	REQUIRE(run([&] { return js->gameRows().size() == 1; }));
	REQUIRE(js->gamesList());
	GadgetListBoxSetSelected(js->gamesList(), 0);
	CHECK(join.wm->invokeCallback("AptLanLobby::OnJoinGameBttn", ""));
	REQUIRE(run([&] { return js->state() == 4 && hostLan->currentGame()->info.slots[1].isHuman(); }));
	// the joiner's faction: the Men (index of FactionMen), then its Hero combo
	int men = -1;
	for (int i = 0; i < (int)join.setup.factions().size(); ++i)
	{
		men = join.setup.factions()[(size_t)i].templateName == "FactionMen" ? i : men;
	}
	REQUIRE(run([&] {
		GameWindow *f = js->slotGadget(1, "PlayerTemplate");
		GameWindow *h = js->slotGadget(1, "Hero");
		return f && h && GadgetComboBoxGetLength(f) > 2;
	}));
	GameWindow *faction = js->slotGadget(1, "PlayerTemplate");
	for (int r = 0; r < GadgetComboBoxGetLength(faction); ++r)
	{
		if (itemOf(faction, r) == men)
		{
			GadgetComboBoxSetSelectedPos(faction, r, false);
		}
	}
	REQUIRE(run([&] { return hostLan->currentGame()->info.slots[1].playerTemplate == men; }));
	// a system hero the Men can use
	const CreateAHeroSystem &sys = s->world->createAHeroSystem();
	int chosen = -1;
	for (int i = 0; i < (int)heroes.size() && chosen < 0; ++i)
	{
		const CreateAHeroSubClass *sub = sys.subClass(heroes.at(i)->hero.classIndex, heroes.at(i)->hero.subClassIndex);
		if (sub && std::find(sub->usableFactions.begin(), sub->usableFactions.end(), 0) != sub->usableFactions.end())
		{
			chosen = i;
		}
	}
	REQUIRE(chosen >= 0);
	REQUIRE(run([&] { return js->slotGadget(1, "Hero") && GadgetComboBoxGetLength(js->slotGadget(1, "Hero")) == 10; }));
	GameWindow *heroCombo = js->slotGadget(1, "Hero");
	int row = -1;
	for (int r = 0; r < GadgetComboBoxGetLength(heroCombo); ++r)
	{
		row = itemOf(heroCombo, r) == chosen ? r : row;
	}
	REQUIRE(row >= 0);
	GadgetComboBoxSetSelectedPos(heroCombo, row, false);
	// the host takes it (Options::validateCreateAHero) and every player's setup holds it
	const std::string id = heroes.at(chosen)->hero.uniqueID;
	REQUIRE(run([&] { return hostLan->currentGame()->info.slots[1].hasCreateAHero && hostLan->currentGame()->info.slots[1].createAHero.uniqueID == id; }));
	REQUIRE(run([&] { return hs->setup()->info().slots[1].hasCreateAHero && js->setup()->info().slots[1].hasCreateAHero; }));
	// the host shows the joiner's choice as text (RW 0x841A9E): a system hero's name and VALUE:Default
	REQUIRE(run([&] { return hs->slotGadget(1, "Hero") && GadgetComboBoxGetLength(hs->slotGadget(1, "Hero")) == 1; }));
	CHECK(GadgetComboBoxGetText(hs->slotGadget(1, "Hero")) == heroes.at(chosen)->hero.name + u" (Default)");
	// accept and start: both starts carry the record
	CHECK(join.wm->invokeCallback("MpGameSetup::OnReadyPress", "1"));
	REQUIRE(run([&] { return hostLan->currentGame()->accepted[1]; }));
	CHECK(host.wm->invokeCallback("AptLanLobby::OnStartGameBttn", ""));
	// lane UI-1: the start counts down first (six seconds of lines, five of the QM:STARTINGGAME box)
	REQUIRE(run([&] { return hostLan->started() && joinLan->started(); }, 20000));
	CHECK(joinLan->start().game.game.slots[1].hasCreateAHero);
	CHECK(joinLan->start().game.game.slots[1].createAHero.save() == heroes.at(chosen)->hero.save());
	CHECK(hostLan->start().game.game.slots[1].createAHero.save() == heroes.at(chosen)->hero.save());
	CHECK(hostLan->errors().empty());
	CHECK(joinLan->errors().empty());
}

TEST_CASE("cah2 retail shell: Delete Hero asks GUI:AreYouSureDelete in a Yes / No box (RW 0x9C5D6F): No keeps the hero, Yes deletes it with its file; NamePrompt shows an Ok box")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	AptCreateAHero *scr = c.screen();
	REQUIRE(scr);
	const size_t systemHeroes = c.heroes.size();
	REQUIRE(systemHeroes == 8);
	CreateAHeroHero h = c.heroes.at(0)->hero;
	h.isSystemHero = false;
	h.name = u"Deleteme";
	h.uniqueID = CreateAHeroHeroList::newUniqueID();
	std::string error;
	int index = -1;
	REQUIRE_MESSAGE(c.heroes.save(h, &error, &index), error);
	const std::filesystem::path file = std::filesystem::u8path(c.dir.path) / CreateAHeroHeroList::fileNameOf(h.uniqueID);
	REQUIRE(std::filesystem::exists(file));
	c.fx->tick(5);
	// a system hero is never asked about
	scr->selectHero(0);
	CHECK_FALSE(scr->deleteSelectedHero());
	CHECK(scr->messageBox() == nullptr);
	scr->selectHero(index);
	REQUIRE(scr->deleteSelectedHero());
	REQUIRE(scr->messageBox());
	c.fx->tick(5);
	CHECK(scr->messageBox()->type() == AptMessageBox::TYPE_YES_NO);
	REQUIRE_FALSE(scr->messageBox()->calls().empty());
	CHECK(scr->messageBox()->calls().back() == "Show YesNo false");
	CHECK(scr->messageBox()->text() == fetchOrMissing(c.fx->environment.gameText, "GUI:AreYouSureDelete"));
	CHECK(scr->messageBox()->title() == fetchOrMissing(c.fx->environment.gameText, "GUI:DeleteFile"));
	const std::string box = "_level" + std::to_string(scr->messageBox()->level()) + ".MessageBox";
	REQUIRE(c.fx->wm->invokeCallback(box + "_OnButtonNo", ""));
	c.fx->tick(5);
	CHECK(c.heroes.size() == systemHeroes + 1);
	CHECK(std::filesystem::exists(file));
	REQUIRE(scr->deleteSelectedHero());
	REQUIRE(c.fx->wm->invokeCallback(box + "_OnButtonYes", ""));
	c.fx->tick(5);
	CHECK(c.heroes.size() == systemHeroes);
	CHECK_FALSE(std::filesystem::exists(file));
	// RW 0x9C3DC7: APT:EnterNameError (no name) or LAN:ErrorDuplicateName (a taken one) in an Ok box titled APT:EnterNameErrorTitle (RW 0x81A375)
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Appearance::NamePrompt", ""));
	c.fx->tick(5);
	CHECK(scr->messageBox()->type() == AptMessageBox::TYPE_OK);
	CHECK(scr->messageBox()->title() == fetchOrMissing(c.fx->environment.gameText, "APT:EnterNameErrorTitle"));
	CHECK(scr->lastMessage().rfind("APT:EnterNameErrorTitle: ", 0) == 0);
	const std::string label = scr->lastMessage().substr(25);
	CHECK((label == "APT:EnterNameError" || label == "LAN:ErrorDuplicateName"));
	CHECK(scr->messageBox()->text() == fetchOrMissing(c.fx->environment.gameText, label));
	CHECK(c.fx->wm->errors().empty());
}

TEST_CASE("cah2 retail shell: the hero name holds 22 typed characters (RW 0x9C3F2A, RW 0x72260B) and refuses U+0E01 (RW 0x75E4DF); the scripted setter types through the same entry")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	AptCreateAHero *scr = c.screen();
	REQUIRE(scr);
	std::string error;
	REQUIRE_MESSAGE(c.fx->wm->invokeAS(scr->level(), "SetCreateNewHero", { "true" }, nullptr, &error), error);
	c.show(scr, "Class");
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Class::SetClassAndType", "2 1"));
	c.show(scr, "Appearance");
	REQUIRE(scr->nameEntry());
	const std::u16string twentyTwo = u"Abcdefghijklmnopqrstuv";
	REQUIRE(twentyTwo.size() == 22);
	CHECK(scr->typeName(twentyTwo));
	CHECK(GadgetTextEntryGetText(scr->nameEntry()) == twentyTwo);
	CHECK_FALSE(scr->typeName(twentyTwo + u"w")); // the 23rd is refused
	CHECK(GadgetTextEntryGetText(scr->nameEntry()) == twentyTwo);
	CHECK_FALSE(scr->typeName(u"Hurกin")); // the Thai block is refused whatever the flags
	CHECK(GadgetTextEntryGetText(scr->nameEntry()) == u"Hurin");
	// typed through the window manager as keys arrive (GWM_IME_CHAR): the same limit
	GadgetTextEntrySetText(scr->nameEntry(), std::u16string());
	for (char16_t ch : twentyTwo + u"xyz")
	{
		scr->nameEntry()->manager().winSendInputMsg(scr->nameEntry(), GWM_IME_CHAR, (WindowMsgData)ch, 0);
	}
	CHECK(GadgetTextEntryGetText(scr->nameEntry()) == twentyTwo);
}

TEST_CASE("cah2 retail shell: the Appearance page's ColorPicker reads the palette texel under its cursor into the hero's colour and places the cursor for a colour (RW 0xB552BF)")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	AptCreateAHero *scr = c.screen();
	REQUIRE(scr);
	std::string error;
	REQUIRE_MESSAGE(c.fx->wm->invokeAS(scr->level(), "SetCreateNewHero", { "true" }, nullptr, &error), error);
	c.show(scr, "Class");
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Class::SetClassAndType", "0 1"));
	c.show(scr, "Appearance");
	c.fx->tick(30);
	AptGadgetLayer *layer = c.fx->wm->gadgetLayer();
	REQUIRE(layer);
	REQUIRE(layer->colorPickers());
	const AptColorPickers::Picker *picker = nullptr;
	for (const AptColorPickers::Picker &p : layer->colorPickers()->pickers())
	{
		MESSAGE("picker " << p.instancePath << " id '" << p.componentId << "' image '" << p.imageName << "' path '" << p.path << "'");
		if (p.initialised && p.image && !picker)
		{
			picker = &p;
		}
	}
	REQUIRE(picker);
	CHECK(picker->imageName == "AptColorChooserPallete");
	const std::string id = picker->componentId;
	REQUIRE((id == "PaintColor" || id == "SkinColor" || id == "HairColor"));
	auto heroColor = [&]() {
		const CreateAHeroHero &h = scr->myHero()->hero();
		return id == "PaintColor" ? h.tertiaryColor : id == "SkinColor" ? h.secondaryColor : h.primaryColor; // OnPaintColor / OnSkinColor / OnHairColor
	};
	const std::uint32_t before = heroColor();
	// the movie's drag writes the cursor (SetExtern <id>Cursor "x y"); the next update reads the texel and calls the clip's SetColor -> OnColorChange ->
	// GameCode("Appearance::On" + id, colour)
	bool changed = false;
	for (const char *cursor : { "30 8", "120 20", "250 5", "60 30" })
	{
		REQUIRE(c.fx->wm->setExtern(id + "Cursor", cursor));
		c.fx->tick(3);
		const std::uint32_t picked = layer->colorPickers()->pickers()[(size_t)(picker - layer->colorPickers()->pickers().data())].color;
		MESSAGE("cursor " << std::string(cursor) << " -> " << picked << " hero " << heroColor());
		CHECK(heroColor() == picked);
		changed = changed || heroColor() != before;
	}
	CHECK(changed);
	std::string value;
	CHECK(c.fx->wm->getExtern(id + "Color", value) == AptExternResult::Value);
	CHECK(value == std::to_string(heroColor()));
	// a colour from outside (the page's init writes <id>Color): the cursor goes to the nearest palette texel
	REQUIRE(c.fx->wm->setExtern(id + "Color", std::to_string(0xFF0000FFu)));
	c.fx->tick(3);
	const AptColorPickers::Picker &after = layer->colorPickers()->pickers()[(size_t)(picker - layer->colorPickers()->pickers().data())];
	CHECK_FALSE(after.colorDirty);
	MESSAGE("cursor for blue: " << after.cursor[0] << " " << after.cursor[1]);
	CHECK(c.fx->wm->errors().empty());
}
