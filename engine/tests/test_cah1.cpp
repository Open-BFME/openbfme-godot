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
#include "GameClient/GameTextTableSource.h"
#include "GameNetwork/LANAPI.h"
#include "GameClient/GUI/CreateAHero/AptMyHero.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Object/Object.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/VictoryConditions.h"
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

TEST_CASE("cahcam1 retail: main-menu preview resets on page and hero selection, not a repeated page")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	AptCreateAHero *scr = c.screen();
	REQUIRE(scr);
	CHECK(c.fx->services.countRequests(ShellAction::CreateAHero) == 1);
	const CreateAHeroSubClass *sub = s->world->createAHeroSystem().subClass(0, 1);
	REQUIRE(sub);
	CHECK(sub->viewInfo.loaded);
	CHECK(sub->viewInfo.normalCam == 0.5f); // Omitted in the retail include: constructor, not zero initialization.
	for (char page : { 'C', 'A', 'P', 'M' })
	{
		const auto revision = scr->cameraRevision();
		scr->showPage(page);
		CHECK(scr->cameraRevision() == revision + 1);
		scr->showPage(page);
		CHECK(scr->cameraRevision() == revision + 1);
	}
	auto revision = scr->cameraRevision();
	scr->selectHero(0);
	CHECK(scr->cameraRevision() == revision + 1);
	revision = scr->cameraRevision();
	scr->setClassAndType(0, 1);
	CHECK(scr->cameraRevision() == revision + 1);
	revision = scr->cameraRevision();
	scr->setClassAndType(-1, -1);
	CHECK(scr->cameraRevision() == revision);
}

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
	CHECK(AptCreateAHero::stopLines()[3] == "[S-1403] Create-a-Hero awards: statistics files, kill counters, award triggers and Manager presentation are implemented; reward UpgradeAwards drawable bling and in-game award popup are not ported; match-result timing is inferred from EndGameView, not yet verified against retail (RW 0x80B2AB); WotR result counters and PrimaryKey/SecondaryKey statistics remain unverified");
	REQUIRE(scr->heroList());
	CHECK(GadgetListBoxGetNumEntries(scr->heroList()) == 8);
	CHECK(scr->selectedHero() == 0);
	// Real list notifications carry the gadget in data1 and row in data2.
	GadgetListBoxSetSelected(scr->heroList(), 1);
	CHECK(scr->selectedHero() == scr->rowEntries()[1]);
	REQUIRE(scr->displayedHero());
	CHECK(scr->displayedHero()->name == c.heroes.at(scr->rowEntries()[1])->hero.name);
	// Every list selection refreshes all ten power icons, not only the hero
	// model and name. Seed stale icons to pin clearing of empty slots too.
	for (int row = 0; row < 8; ++row)
	{
		for (int i = 1; i <= 10; ++i)
		{
			c.fx->wm->setAptImage("MyPowerIcon_" + std::to_string(i), "stale");
		}
		GadgetListBoxSetSelected(scr->heroList(), row);
		const auto &hero = c.heroes.at(scr->rowEntries()[(size_t)row])->hero;
		for (int i = 0; i < 10; ++i)
		{
			const CommandButton *button = TheCommandStore->findCommandButton(hero.powers[(size_t)i].commandButton);
			const std::string expected = !button ? std::string() : !button->m_createAHeroUIIconImageName.empty()
				? button->m_createAHeroUIIconImageName : button->m_buttonImageName.empty() ? std::string() : button->m_buttonImageName.front();
			const std::string *image = c.fx->wm->aptImage("MyPowerIcon_" + std::to_string(i + 1));
			CHECK((image ? *image : std::string()) == expected);
		}
	}
	GadgetListBoxSetSelected(scr->heroList(), 0);
	CHECK(scr->selectedHero() == scr->rowEntries()[0]);
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
	REQUIRE(c.fx->wm->aptImage("Cah::Portrait"));
	CHECK(*c.fx->wm->aptImage("Cah::Portrait") == "HPGandalf");
	// The applied preview supplies the hero portrait. Text refreshes must not
	// replace it with the class's faction emblem.
	c.fx->wm->setAptImage("Cah::Portrait", "CPShieldMaiden");
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Class::SetClassAndType", "2 1"));
	REQUIRE(scr->displayedHero());
	CHECK(scr->displayedHero()->classIndex == 2);
	CHECK(scr->displayedHero()->subClassIndex == 1);
	c.show(scr, "Appearance");
	CHECK(scr->currentPage() == 'A');
	REQUIRE(scr->myHero());
	CHECK(scr->myHero()->hero().classIndex == 2);
	CHECK(scr->myHero()->hero().subClassIndex == 1);
	CHECK(*c.fx->wm->aptImage("Cah::Portrait") == "CPShieldMaiden");
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
	REQUIRE(hero != nullptr);
	CHECK(static_cast<ComboBoxData *>(hero->winGetUserData())->maxDisplay == 8); // RW 0x72414F, slot 0
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
	// the same hero with another appearance, edited before any game runs (lane MAPRESET-1: a game's end reloads the world, and `sys` is its
	// CreateAHeroSystem: S-3808)
	CreateAHeroHero other = saved;
	my.edit(other, false);
	my.step(AptMyHero::KIND_APPEARANCE, 0, 1);
	NewGameMessage changed = message;
	REQUIRE(changed.game.slots[0].setCreateAHero(my.hero(), &error));
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

TEST_CASE("ui6 cah1 retail lobby: the chosen Create-a-Hero is kept with the lobby (OpenBFME's profile key, by unique id) and chosen again on return")
{
	OPENBFME_REQUIRE_START(s);
	TempDir dir;
	CreateAHeroHeroList heroes;
	heroes.load(s->mount->fs.get(), dir.path, nullptr);
	REQUIRE(heroes.size() == 8);
	CreateAHeroScreenContext ctx;
	ctx.system = &s->world->createAHeroSystem();
	ctx.heroes = &heroes;
	MemorySkirmishProfiles store;
	starttest::ShellFx fx(*s, 77u);
	fx.text.set("GUI:Random", "Random");
	fx.text.set("VALUE:Default", " (Default)");
	fx.environment.createAHero = &ctx;
	fx.environment.profiles = &store;
	fx.shell->push("Skirmish.apt");
	auto *screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(screen);
	fx.tick(60);
	REQUIRE(screen->profileEntryWindow());
	fx.wm->invokeCallback("AptSkirmish::OnNewProfileMenu", "");
	GadgetTextEntrySetText(screen->profileEntryWindow(), u"Gimli");
	fx.wm->invokeCallback("AptSkirmish::OnAddProfileAccept", "");
	fx.tick(40);
	REQUIRE(store.current() == u"Gimli");
	GameWindow *hero = screen->slotGadget(0, "Hero");
	REQUIRE(hero);
	REQUIRE(GadgetComboBoxGetLength(hero) == 2 + 8);
	int chosenRow = -1;
	for (int r = 2; r < GadgetComboBoxGetLength(hero) && chosenRow < 0; ++r)
	{
		starttest::chooseComboRow(fx, hero, r);
		chosenRow = screen->slotHero(0) == itemOf(hero, r) ? r : -1;
	}
	REQUIRE(chosenRow >= 0);
	const int chosen = itemOf(hero, chosenRow);
	const std::string id = heroes.at(chosen)->hero.uniqueID;
	REQUIRE(screen->setup()->info().slots[0].hasCreateAHero);
	fx.shell->pop();
	fx.tick(30);
	CHECK(store.profileValue(0, "OpenBFMEHeroes").rfind(id + ",", 0) == 0);
	fx.shell->push("Skirmish.apt");
	screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(screen);
	fx.tick(60);
	CHECK(screen->slotHero(0) == chosen);
	REQUIRE(screen->setup()->info().slots[0].hasCreateAHero);
	CHECK(screen->setup()->info().slots[0].createAHero.uniqueID == id);
}

// Lane MENUWORK-1: the hero list's headers sort it as RotWK's Manager does (RW 0x9C4EBD keys, RW 0x9C54C4 comparator, RW 0x9C5A66 list); a row is
// still its own hero after the sort; Take Picture, Play Game and the awards tell the player they are not available (S-4755)
TEST_CASE("menuwork1 cah retail shell: the hero list's headers sort it (RW 0x9C4EBD / 0x9C54C4); Take Picture and Play Game report their gaps; award selection shows its description")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	AptCreateAHero *scr = c.screen();
	REQUIRE(scr);
	REQUIRE(scr->heroList());
	const int rows = GadgetListBoxGetNumEntries(scr->heroList());
	REQUIRE(rows == 8);
	auto lower = [](std::u16string t) {
		for (char16_t &ch : t)
		{
			ch = (ch >= u'A' && ch <= u'Z') ? (char16_t)(ch + 32) : ch;
		}
		return t;
	};
	auto rowNames = [&] {
		std::vector<std::u16string> out;
		for (int r = 0; r < rows; ++r)
		{
			out.push_back(lower(GadgetListBoxGetText(scr->heroList(), r, 1)));
		}
		return out;
	};
	const std::vector<std::u16string> before = rowNames();
	// Name: ascending without case (key 2, the old key 0 the second)
	REQUIRE(c.fx->wm->invokeCallback("Mission::OnSortName", ""));
	CHECK(scr->heroSortKey0() == 2);
	CHECK(scr->heroSortKey1() == 0);
	std::vector<std::u16string> sorted = before;
	std::sort(sorted.begin(), sorted.end());
	CHECK(rowNames() == sorted);
	CHECK(rowNames() != before); // the press changed the order
	// Name again: descending (key 3)
	REQUIRE(c.fx->wm->invokeCallback("Mission::OnSortName", ""));
	CHECK(scr->heroSortKey0() == 3);
	CHECK(scr->heroSortKey1() == 0); // the old key was the new key's twin: the second stays
	std::reverse(sorted.begin(), sorted.end());
	CHECK(rowNames() == sorted);
	// Type: class / subclass ascending (key 4); the rows' heroes follow their names (a row selects its own hero)
	REQUIRE(c.fx->wm->invokeCallback("Mission::OnSortType", ""));
	CHECK(scr->heroSortKey0() == 4);
	CHECK(scr->heroSortKey1() == 3);
	const CreateAHeroHeroList &list = c.heroes;
	REQUIRE(scr->rowEntries().size() == (size_t)rows);
	for (int r = 1; r < rows; ++r)
	{
		const CreateAHeroHero &a = list.entries()[(size_t)scr->rowEntries()[(size_t)r - 1]].hero;
		const CreateAHeroHero &b = list.entries()[(size_t)scr->rowEntries()[(size_t)r]].hero;
		CHECK(a.classIndex * 100 + a.subClassIndex <= b.classIndex * 100 + b.subClassIndex); // tolerance: the order between neighbours is the property
		CHECK(lower(GadgetListBoxGetText(scr->heroList(), r, 1)) == lower(b.name));
	}
	int selected = -1;
	GadgetListBoxGetSelected(scr->heroList(), &selected);
	REQUIRE(selected >= 0); // tolerance: whichever row; the property is that it holds the selected hero
	CHECK(scr->selectedHero() == scr->rowEntries()[(size_t)selected]);
	CHECK(c.fx->wm->noteCount("command-unhandled") == 0);
	// the controls with nothing behind them say so
	// lane CAHPIC-1: Take Picture is ported (the cahpic1 cases below); a press before the guard clip was drawn twice arms nothing (RW 0x919D8F)
	const std::string beforePicture = scr->lastMessage();
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::OnTakePicture", ""));
	CHECK_FALSE(scr->takePicturePending());
	CHECK(scr->lastMessage() == beforePicture);
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Manager::OnPlayGame", ""));
	CHECK(scr->lastMessage() == "OpenBFME: Trying a hero on a test map is not available yet: play a skirmish with the hero instead.");
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Manager::OnSelectAward", "1"));
	REQUIRE(c.fx->wm->aptText("Cah:SelectedAwardDesc"));
	const auto &hero = scr->myHero()->hero();
	const auto *sub = c.ctx.system->subClass(hero.classIndex, hero.subClassIndex);
	REQUIRE(sub);
	const auto *award = c.ctx.system->awardSystem.award(sub->awards[1]);
	REQUIRE(award);
	CHECK(*c.fx->wm->aptText("Cah:SelectedAwardDesc") == loadScreenU16ToUtf8(fetchOrMissing(c.fx->environment.gameText, award->descriptionTag)));
}

TEST_CASE("cah statistics retail: saved counters populate the manager and unlock the exact Vanquisher threshold")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	auto *screen = c.screen();
	REQUIRE(screen);
	const auto &system = *c.ctx.system;
	CHECK(system.awardSystem.awards.size() == 28);
	CHECK(system.awardSystem.statistics.size() == 71);
	const auto *entry = c.heroes.at(0);
	REQUIRE(entry);
	const auto *sub = system.subClass(entry->hero.classIndex, entry->hero.subClassIndex);
	REQUIRE(sub);
	const auto it = std::find(sub->awards.begin(), sub->awards.end(), "Vanquisher");
	REQUIRE(it != sub->awards.end());
	const auto index = (size_t)(it - sub->awards.begin());
	const std::string provider = "MyHero::AwardState_" + std::to_string(index);
	CreateAHeroStats stats;
	stats.add("ENEMIES_KILLED", 2999);
	std::string error, value;
	REQUIRE(stats.save(c.dir.path, entry->hero.uniqueID, &error));
	screen->selectHero(0);
	REQUIRE(screen->myHero()->provide(provider, value));
	CHECK(value == "0");
	CHECK(c.fx->wm->getExtern(provider, value) == AptExternResult::Value);
	stats.add("ENEMIES_KILLED");
	REQUIRE(stats.save(c.dir.path, entry->hero.uniqueID, &error));
	screen->selectHero(0);
	REQUIRE(screen->myHero()->provide(provider, value));
	CHECK(value == "1");
	CHECK(c.fx->wm->noteCount("create-a-hero-statistics-error") == 0);
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::Manager::OnSelectAward", std::to_string(index)));
	const auto *description = c.fx->wm->aptText("Cah:SelectedAwardDesc");
	REQUIRE(description);
	CHECK(*description == loadScreenU16ToUtf8(fetchOrMissing(c.fx->environment.gameText, "Award:Vanquisher_Desc")));
}

TEST_CASE("cah statistics retail: local hero kills persist once and client profile recording leaves the simulation hash unchanged")
{
	OPENBFME_REQUIRE_START(s);
	TempDir dir;
	CreateAHeroHeroList heroes;
	heroes.load(s->mount->fs.get(), dir.path, nullptr);
	REQUIRE(heroes.at(0));
	const CreateAHeroHero hero = heroes.at(0)->hero;
	NewGameMessage message;
	std::string error;
	{
		starttest::ShellFx fx(*s, 99u);
		SkirmishGameSetup setup(fx.setup, &fx.text);
		REQUIRE(setup.init(u"StatsTest", 99u, "maps/map mp evendim/map mp evendim.map", &error));
		REQUIRE(setup.setSlotCreateAHero(0, &hero, &error));
		message = setup.makeNewGameMessage();
	}
	std::vector<MapCacheEntry> cache;
	REQUIRE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error));
	std::vector<std::uint32_t> hashes;
	for (bool record : { false, true })
	{
		NewGameStart start(RandomAlgorithm::ZH_CarryChain);
		REQUIRE(NewGame::prepareNewGame(message, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error));
		ArchiveW3DFileSource source(*s->mount->fs);
		WW3DAssetManager assets(source);
		LiveGame game(*s->world, *s->mount->fs, assets, s->options);
		LiveGame::Options options;
		options.start = &start;
		options.heroStatsProfile = record ? dir.path : "";
		REQUIRE_MESSAGE(game.load(options, &error), error);
		auto *local = game.players().getLocalPlayer();
		REQUIRE(local);
		auto *killer = game.createObject("CreateAHero", local->getPlayerIndex(), { 1000.0f, 1000.0f, 0.0f }, 0.0f, &error);
		auto *victim = game.createObject("GondorFighter", local->getPlayerIndex(), { 1050.0f, 1000.0f, 0.0f }, 0.0f, &error);
		REQUIRE(killer);
		REQUIRE(victim);
		const auto blocked = std::filesystem::u8path(dir.path) / (hero.uniqueID + ".tmp");
		if (record) std::filesystem::create_directory(blocked); // fail writes even as an administrator
		// RW 0x69560B records before the relationship gate, even for this allied victim.
		killer->scoreTheKill(*victim);
		killer->scoreTheKill(*victim);
		game.advance(0.0);
		game.advance(0.0);
		hashes.push_back(game.logic().computeStateHash());
		if (record)
		{
			CreateAHeroStats stats;
			REQUIRE_MESSAGE(stats.load(dir.path, hero.uniqueID, &error), error);
			CHECK(stats.get("ENEMIES_KILLED") == 0);
			CHECK_FALSE(game.report().errors.empty());
			const auto errorCount = game.report().errors.size();
			game.advance(0.0);
			CHECK(game.report().errors.size() == errorCount);
			std::filesystem::remove(blocked);
			game.advance(0.0); // no new events: retry the dirty counters
			REQUIRE_MESSAGE(stats.load(dir.path, hero.uniqueID, &error), error);
			CHECK(stats.get("ENEMIES_KILLED") == 1);
			CHECK(stats.get("MEN_OF_THE_WEST_INFANTRY_KILLED") == 1);
			std::filesystem::create_directory(blocked);
			game.logic().victory().selfDestruct(local, false);
			// Wait for the published defeat, with a watchdog rather than assuming
			// the delayed self-destruct/presentation completes at a guessed frame.
			for (int frame = 0; frame < 100; ++frame)
			{
				game.advance(0.2);
				REQUIRE_MESSAGE(stats.load(dir.path, hero.uniqueID, &error), error);
				if (game.logic().victory().localAlliedDefeat()) break;
			}
			CHECK(game.logic().victory().localAlliedDefeat());
			for (int frame = 0; frame < 5; ++frame) game.advance(0.2);
			REQUIRE_MESSAGE(stats.load(dir.path, hero.uniqueID, &error), error);
			CHECK(stats.get(s->world->createAHeroSystem().statNames[4]) == 0);
			std::filesystem::remove(blocked);
			game.advance(0.0);
			REQUIRE_MESSAGE(stats.load(dir.path, hero.uniqueID, &error), error);
			CHECK(stats.get(s->world->createAHeroSystem().statNames[4]) == 1);
			for (int frame = 0; frame < 5; ++frame) game.advance(0.2);
			REQUIRE_MESSAGE(stats.load(dir.path, hero.uniqueID, &error), error);
			CHECK(stats.get(s->world->createAHeroSystem().statNames[4]) == 1);
		}
	}
	REQUIRE(hashes.size() == 2);
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("cah attributes: duplicate tick initializers position all thirty-two segments of each bar")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	auto *scr = c.screen();
	REQUIRE(scr);
	REQUIRE(c.fx->wm->invokeAS(scr->level(), "SetCreateNewHero", { "true" }));
	c.show(scr, "Class");
	scr->setClassAndType(0, 1);
	c.show(scr, "Appearance");
	REQUIRE(c.fx->wm->invokeASAt(scr->level(), "Main.mcAppearanceScreen.Screen.TypeWindow", "gotoAndPlay", { "_attributes" }));
	c.fx->tick(60);
	CHECK(c.fx->wm->apt().noteCount("duplicate-init-object-unverified") == 1);
	for (int attribute = 0; attribute < 5; ++attribute)
	{
		const std::string path = "Main.mcAppearanceScreen.Screen.TypeWindow.instance12." + std::to_string(attribute) + ".AttribBar";
		for (int tick = 0; tick < 32; ++tick)
		{
			auto *segment = c.fx->at(scr->level(), path + ".Tick_" + std::to_string(tick));
			REQUIRE(segment);
			// The retail movie starts at -100 and advances by its 9-pixel tick width.
			CHECK(segment->matrix.tx == -100.0f + 9.0f * tick);
			CHECK(segment->matrix.ty == -242.0f);
		}
	}
}

TEST_CASE("cah statistics retail: award tab binds each medal image after child initialization and survives reopening")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	auto *screen = c.screen();
	REQUIRE(screen);
	auto &apt = c.fx->wm->apt();
	auto *root = apt.level(screen->level());
	const std::string tabs = "Main.mcManagerScreen.Screen.TabNav";
	const std::string grid = tabs + ".ScreenAwards.AwardList.mcAwards";
	for (int visit = 0; visit < 2; ++visit)
	{
		auto *button = starttest::firstButtonIn(apt.resolvePath(root, tabs + ".Tab_Nav_Button.UnselectedTabButtons.Awards"));
		REQUIRE(button);
		starttest::clickButton(*c.fx, *button);
		c.fx->tick(40);
		const auto &hero = screen->myHero()->hero();
		const auto *sub = c.ctx.system->subClass(hero.classIndex, hero.subClassIndex);
		REQUIRE(sub);
		for (size_t i = 0; i < sub->awards.size(); ++i)
		{
			auto *image = apt.resolvePath(root, grid + "." + std::to_string(i) + ".Image");
			REQUIRE(image);
			AptValue binding;
			REQUIRE(image->getOwn("_imageMap", binding));
			CHECK(binding.toString() == "CahAward:Image_" + std::to_string(i));
			AptValue mode;
			REQUIRE(image->getOwn("_mode", mode));
			CHECK(mode.toString() == "GRAYSCALE");
			const auto *record = c.fx->wm->aptImage(binding.toString());
			REQUIRE(record);
			CHECK(*record == c.ctx.system->awardSystem.award(sub->awards[i])->image);
			auto *name = apt.resolvePath(root, grid + "." + std::to_string(i) + ".AwardName");
			REQUIRE(name);
			AptValue text;
			REQUIRE(name->getMember("text", text));
			CHECK(text.toString() == "$CahAward:Name_" + std::to_string(i));
		}
		auto *appearance = starttest::firstButtonIn(apt.resolvePath(root, tabs + ".Tab_Nav_Button.UnselectedTabButtons.Appearance"));
		REQUIRE(appearance);
		starttest::clickButton(*c.fx, *appearance);
		c.fx->tick(40);
	}
	const auto &hero = screen->myHero()->hero();
	const auto *sub = c.ctx.system->subClass(hero.classIndex, hero.subClassIndex);
	REQUIRE(sub);
	const auto *award = c.ctx.system->awardSystem.award(sub->awards.front());
	REQUIRE(award);
	CreateAHeroStats stats;
	for (const auto &trigger : award->triggers)
	{
		REQUIRE_FALSE(trigger.stats.empty());
		stats.add(trigger.stats.front(), trigger.threshold);
	}
	std::string error;
	REQUIRE(stats.save(c.dir.path, hero.uniqueID, &error));
	screen->selectHero(screen->selectedHero());
	auto *button = starttest::firstButtonIn(apt.resolvePath(root, tabs + ".Tab_Nav_Button.UnselectedTabButtons.Awards"));
	REQUIRE(button);
	starttest::clickButton(*c.fx, *button);
	c.fx->tick(40);
	auto *earned = apt.resolvePath(root, grid + ".0.Image");
	REQUIRE(earned);
	AptValue mode;
	REQUIRE(earned->getOwn("_mode", mode));
	CHECK(mode.toString() == "ALPHA");
}

TEST_CASE("cahpic1 picture name: the hero's name, the short date and the time with seconds, the characters a file name cannot take made '-' (RW 0x91A20A)")
{
	// GetDateFormatW(DATE_SHORTDATE) / GetTimeFormatW(0) in US English: "10/21/2026", "3:04:05 PM"; '/' and ':' are refused by flag 8 (RW 0x75E4DF)
	AptCreateAHero::PictureTime t;
	t.year = 2026;
	t.month = 10;
	t.day = 21;
	t.hour = 15;
	t.minute = 4;
	t.second = 5;
	CHECK(AptCreateAHero::pictureFileName(u"Fhaleen", t) == u"Fhaleen 10-21-2026 3-04-05 PM");
	t.hour = 0; // midnight is 12 AM
	t.minute = 0;
	t.second = 9;
	CHECK(AptCreateAHero::pictureFileName(u"Fhaleen", t) == u"Fhaleen 10-21-2026 12-00-09 AM");
	t.hour = 12; // noon is 12 PM
	CHECK(AptCreateAHero::pictureFileName(u"A*B?C\\D|E<F>G\"H", t) == u"A-B-C-D-E-F-G-H 10-21-2026 12-00-09 PM");
	CHECK(AptCreateAHero::pictureFileName(u"", t) == u" 10-21-2026 12-00-09 PM"); // an unnamed hero: retail's empty name and its space
}

TEST_CASE("cahpic1 retail shell: TAKE PICTURE arms after two guard draws, names the picture for the hero and shows RotWK's OK box")
{
	OPENBFME_REQUIRE_START(s);
	CahShellFx c(s, true);
	AptCreateAHero *scr = c.screen();
	REQUIRE(scr);
	REQUIRE(scr->myHero());
	const std::u16string hero = scr->myHero()->hero().name;
	// the two box texts from the 2.01 string file (the fixture's text source holds test labels only)
	{
		std::vector<std::uint8_t> str;
		std::string error;
		REQUIRE_MESSAGE(s->mount->fs->readFile("data/lotr.str", str, &error), error);
		GameTextTableSource retail;
		REQUIRE_MESSAGE(retail.table.parse(str, &error), error);
		for (const char *label : { "GUI:OnPictureTakenTitle", "GUI:OnPictureTaken" })
		{
			std::u16string value;
			REQUIRE(retail.fetch(label, value));
			c.fx->text.labels[label] = value;
		}
	}
	// the movie's PrepareToTakePicture zeroes the guard count (RW 0x919D85); OnTakePicture arms only from the second guard draw (RW 0x919D8F)
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::PrepareToTakePicture", ""));
	CHECK(scr->pictureFrames() == 0);
	scr->pictureGuardDrawn();
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::OnTakePicture", ""));
	CHECK_FALSE(scr->takePicturePending());
	scr->pictureGuardDrawn();
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::OnTakePicture", ""));
	CHECK(scr->takePicturePending());
	// the 3D view's next draw: the flag goes, the file is named for the hero (RW 0x91A46A / 0x91A47C)
	AptCreateAHero::PictureTime t;
	t.year = 2026;
	t.month = 10;
	t.day = 21;
	t.hour = 9;
	t.minute = 30;
	t.second = 0;
	const std::u16string name = scr->beginPicture(t);
	CHECK_FALSE(scr->takePicturePending());
	CHECK(name == AptCreateAHero::pictureFileName(hero, t));
	// the movie's loop asks again before the device has saved: no second picture (RotWK answers within the same draw)
	REQUIRE(c.fx->wm->invokeCallback("AptCreateAHero::OnTakePicture", ""));
	CHECK_FALSE(scr->takePicturePending());
	// saved: the OK box GUI:OnPictureTakenTitle ("Picture saved for %s") / GUI:OnPictureTaken ("Picture was saved as:\n%s.") with the folder + name
	const std::u16string shown = u"/pictures/The Lord of the Rings, The Rise of the Witch-king Screenshots/" + name;
	scr->pictureTaken(shown, std::string());
	c.fx->tick(5);
	REQUIRE(scr->messageBox());
	CHECK(scr->lastMessage() == "GUI:OnPictureTakenTitle: GUI:OnPictureTaken");
	CHECK(scr->messageBox()->title() == u"Picture saved for " + hero);
	CHECK(scr->messageBox()->text() == u"Picture was saved as:\n" + shown + u".");
	CHECK(c.fx->wm->noteCount("command-unhandled") == 0);
	// a picture the device could not save is said plainly, and the movie still reopens its panels
	scr->pictureTaken(u"", "the folder cannot be written");
	CHECK(scr->lastMessage() == "OpenBFME: The picture could not be saved: the folder cannot be written");
}

// lane STABLEQA-1 (the stable candidate's unit sweep made bare Create-a-Hero records and saw none attack): every class's hero, built as the builder
// builds it (the recommended attributes, an appearance, the powers it can take) and chosen in the lobby, is made at its player's base in the started
// skirmish and ordered to attack an enemy Mordor fighter beside it; the fighter must take damage
TEST_CASE("stableqa1 cah retail: a builder-made Create-a-Hero of every class attacks an enemy beside it and damages it")
{
	OPENBFME_REQUIRE_START(s);
	const size_t classes = s->world->createAHeroSystem().classes().size();
	REQUIRE(classes > 0u); // tolerance: a precondition (the system holds classes; each is tested below)
	std::string error;
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	for (size_t cls = 0; cls < classes; ++cls)
	{
		CAPTURE(cls);
		// the world's CreateAHeroSystem, read again for each game: a game's end reloads the world (lane MAPRESET-1, S-3808)
		const CreateAHeroSystem &sys = s->world->createAHeroSystem();
		TempDir dir;
		CreateAHeroHeroList heroes;
		heroes.load(s->mount->fs.get(), dir.path, nullptr);
		AptMyHero my(sys, nullptr, nullptr);
		CreateAHeroHero h;
		h.classIndex = (int)cls;
		h.subClassIndex = 0;
		my.edit(h, true);
		my.setDefaults(AptMyHero::KIND_ATTRIBUTE);
		my.step(AptMyHero::KIND_APPEARANCE, 0, 1);
		my.setName(u"Sweep");
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
			if (!pick || !powers.select(pick, my.hero()))
			{
				break;
			}
		}
		REQUIRE(heroes.save(my.hero(), &error));
		const CreateAHeroHero &saved = heroes.at(heroes.findByUniqueID(my.hero().uniqueID))->hero;
		std::string why;
		REQUIRE_MESSAGE(sys.validateHero(saved, *TheCommandStore, &why), why);
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
		NewGameStart start(RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
		ArchiveW3DFileSource source(*s->mount->fs);
		WW3DAssetManager assets(source);
		LiveGame game(*s->world, *s->mount->fs, assets, s->options);
		LiveGame::Options o;
		o.start = &start;
		REQUIRE_MESSAGE(game.load(o, &error), error);
		GameLogic &logic = game.logic();
		REQUIRE(logic.createAHeroes().heroes().size() == 1u);
		const int heroPlayer = logic.createAHeroes().heroes().begin()->first;
		Player *mine = logic.players().getNthPlayer(heroPlayer);
		REQUIRE(mine);
		// the player's base: its first object
		const Object *base = nullptr;
		for (Object *x = logic.getFirstObject(); x && !base; x = x->getNextObject())
		{
			base = x->getControllingPlayer() == mine ? x : nullptr;
		}
		REQUIRE(base);
		int enemyPlayer = -1;
		for (int i = 0; i < logic.players().getPlayerCount() && enemyPlayer < 0; ++i)
		{
			Player *p = logic.players().getNthPlayer(i);
			enemyPlayer = p && p != mine && mine->getRelationship(p) == ENEMIES ? i : -1;
		}
		REQUIRE(enemyPlayer >= 0); // tolerance: a precondition (an enemy player exists)
		const Coord3D at{ base->getPosition()->x + 220.0f, base->getPosition()->y + 220.0f, 0.0f };
		Object *hero = game.createObject("CreateAHero", heroPlayer, at, 0.0f, &error);
		REQUIRE_MESSAGE(hero, error);
		const ObjectID heroId = hero->getID();
		Object *enemy = game.createObject("MordorFighter", enemyPlayer, Coord3D{ at.x + 40.0f, at.y, 0.0f }, 0.0f, &error);
		REQUIRE_MESSAGE(enemy, error);
		const ObjectID enemyId = enemy->getID();
		REQUIRE(enemy->getBodyModule());
		const float hp0 = enemy->getBodyModule()->getHealth();
		game.advance(0.2);
		hero = logic.findObjectByID(heroId);
		REQUIRE(hero);
		REQUIRE(hero->getAIUpdateInterface());
		CHECK(hero->getAIUpdateInterface()->aiAttackObject(logic.findObjectByID(enemyId), CMD_FROM_PLAYER));
		bool hurt = false;
		for (int f = 0; f < 600 && !hurt; ++f)
		{
			game.advance(0.2);
			const Object *e = logic.findObjectByID(enemyId);
			hurt = !e || e->isEffectivelyDead() || (e->getBodyModule() && e->getBodyModule()->getHealth() < hp0);
		}
		CHECK_MESSAGE(hurt, "class " << cls << " (" << sys.classes()[cls].nameTag << "): the hero did no damage in 600 frames");
	}
}
