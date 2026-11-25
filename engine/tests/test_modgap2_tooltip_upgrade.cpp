// Both editions: RW 0x8BBBCC / 0x8BBC0A and BFME2 0x8B7A9D / 0x8B7ADB.
#include "doctest.h"
#include "MoveTestUtil.h"
#include "Common/StateHash.h"
#include "Common/Upgrade.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/TooltipUpgrade.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

namespace
{
const char kTooltipObjects[] =
	"Upgrade Upgrade_Tooltip1\n Type = OBJECT\nEnd\n"
	"Upgrade Upgrade_TooltipReset\n Type = OBJECT\nEnd\n"
	"Object TestTooltipUnit\n KindOf = INFANTRY SELECTABLE\n"
	" Behavior = AIUpdateInterface AI\n End\n"
	" Behavior = TooltipUpgrade Upgrade1\n"
	"  TriggeredBy = Upgrade_Tooltip1\n"
	"  DisplayName = OBJECT:CustomName\n"
	"  Description = CONTROLBAR:CustomDesc\n"
	" End\n"
	" Behavior = TooltipUpgrade UpgradeReset\n"
	"  TriggeredBy = Upgrade_TooltipReset\n"
	"  DisplayName = ReferenceDisplayName\n"
	"  Description = \n"
	" End\n"
	"End\n";
} // namespace

TEST_CASE("modgap2 tooltip upgrade: grant and removal update canonical tooltip strings and report stop S-6013 in both editions")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		movetest::MoveWorld w;
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		const std::string error = w.w.load(kTooltipObjects);
		REQUIRE_MESSAGE(error.empty(), error);
		w.buildMap();

		Object *obj = w.spawn("TestTooltipUnit", 300, 300, 0, w.teamOf("Alice"));
		REQUIRE(obj);

		auto *upgradeMod = dynamic_cast<TooltipUpgrade *>(obj->findModule("TooltipUpgrade"));
		REQUIRE(upgradeMod);
		CHECK(upgradeMod->getTooltipUpgradeModuleData()->m_displayName == "OBJECT:CustomName");
		CHECK(upgradeMod->getTooltipUpgradeModuleData()->m_description == "CONTROLBAR:CustomDesc");

		CHECK(TooltipUpgrade::displayNameOf(*obj).empty());
		CHECK(TooltipUpgrade::descriptionOf(*obj).empty());
		CHECK(upgradeMod->activeDisplayName().empty());
		CHECK(upgradeMod->activeDescription().empty());

		const UpgradeTemplate *up1 = TheUpgradeCenter->findUpgrade("Upgrade_Tooltip1");
		REQUIRE(up1);
		const UpgradeTemplate *upReset = TheUpgradeCenter->findUpgrade("Upgrade_TooltipReset");
		REQUIRE(upReset);

		obj->giveUpgrade(up1);
		CHECK(TooltipUpgrade::displayNameOf(*obj) == "OBJECT:CustomName");
		CHECK(TooltipUpgrade::descriptionOf(*obj) == "CONTROLBAR:CustomDesc");
		CHECK(upgradeMod->activeDisplayName() == "OBJECT:CustomName");
		CHECK(upgradeMod->activeDescription() == "CONTROLBAR:CustomDesc");

		bool stopFound = false;
		for (const auto &line : w.logic->report().stops)
		{
			if (line == TooltipUpgrade::stopLine())
			{
				stopFound = true;
				break;
			}
		}
		CHECK(stopFound);

		obj->giveUpgrade(upReset);
		CHECK(TooltipUpgrade::displayNameOf(*obj).empty());
		CHECK(TooltipUpgrade::descriptionOf(*obj).empty());

		obj->removeUpgrade(upReset);
		upgradeMod->postUpgradeCheck();
		CHECK(TooltipUpgrade::displayNameOf(*obj) == "OBJECT:CustomName");
		CHECK(TooltipUpgrade::descriptionOf(*obj) == "CONTROLBAR:CustomDesc");

		obj->removeUpgrade(up1);
		CHECK(TooltipUpgrade::displayNameOf(*obj).empty());
		CHECK(TooltipUpgrade::descriptionOf(*obj).empty());
	}
}

TEST_CASE("modgap2 tooltip upgrade: snapshot xfer round-trip")
{
	movetest::MoveWorld w;
	w.logic->setEdition(GameEdition::RotWK201);
	const std::string error = w.w.load(kTooltipObjects);
	REQUIRE_MESSAGE(error.empty(), error);
	w.buildMap();

	Object *obj = w.spawn("TestTooltipUnit", 300, 300, 0, w.teamOf("Alice"));
	REQUIRE(obj);
	auto *upgradeMod = dynamic_cast<TooltipUpgrade *>(obj->findModule("TooltipUpgrade"));
	REQUIRE(upgradeMod);

	const UpgradeTemplate *up1 = TheUpgradeCenter->findUpgrade("Upgrade_Tooltip1");
	REQUIRE(up1);
	obj->giveUpgrade(up1);
	CHECK(upgradeMod->isAlreadyUpgraded());
	CHECK(TooltipUpgrade::displayNameOf(*obj) == "OBJECT:CustomName");

	XferSave saved;
	upgradeMod->xfer(saved);
	REQUIRE(!saved.data().empty());

	obj->removeUpgrade(up1);
	CHECK(!upgradeMod->isAlreadyUpgraded());
	CHECK(TooltipUpgrade::displayNameOf(*obj).empty());

	XferLoad loader(saved.data());
	upgradeMod->xfer(loader);
	CHECK(upgradeMod->isAlreadyUpgraded());
	CHECK(TooltipUpgrade::displayNameOf(*obj) == "OBJECT:CustomName");
}
