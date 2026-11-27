// OpenBFME unit tests. GPL-3.0.
// LOCAL-132: InGameSpellBookModel chained CommandTrigger dispatch.
// Retail 2.01 code path, exercised by synthetic data:
//   - RW 0x81EBA8..0x81EBC5 -> CommandTranslator::issueSpecialPowerCommand (RW 0x81E147, 0x81E2DE..0x81E3FC):
//     GUI_COMMAND_SPELL_BOOK (0x20) with NEED_TARGET_POS emits MSG_DO_SPECIAL_POWER_AT_LOCATION (0x411)
//     for the primary power and for every CommandTrigger (+0x188) button with m_command == GUI_COMMAND_SPECIAL_POWER
//     (0x18) and a valid SpecialPowerTemplate, passing sourceObjectID = localPlayer->getSpellBookObject()->getID().
//   - RW 0x94157D..0x94160F: targetless press emits MSG_DO_SPECIAL_POWER (0x410) for the primary power and
//     every SPECIAL_POWER CommandTrigger button with sourceObjectID = spell book.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/SpellBookUI.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/System/ShroudManager.h"

#include <string>
#include <vector>

namespace
{
const char kLocal132Ini[] =
	"SpecialPower SpellBookL132Shared\n"
	"  Enum = SPECIAL_SPELL_BOOK_DUNEDAIN_ALLIES\n"
	"  ReloadTime = 60000\n"
	"  RadiusCursorRadius = 80.0\n"
	"End\n"
	"SpecialPower SpellBookL132VariantA\n"
	"  Enum = SPECIAL_SPELL_BOOK_DUNEDAIN_ALLIES\n"
	"  ReloadTime = 60000\n"
	"  RadiusCursorRadius = 80.0\n"
	"End\n"
	"SpecialPower SpellBookL132InstantMain\n"
	"  Enum = SPECIAL_SPELL_BOOK_RALLYING_CALL\n"
	"  ReloadTime = 60000\n"
	"End\n"
	"SpecialPower SpellBookL132InstantTriggered\n"
	"  Enum = SPECIAL_SPELL_BOOK_RALLYING_CALL\n"
	"  ReloadTime = 60000\n"
	"End\n"
	"ObjectCreationList OCL_L132Summon\n"
	"  CreateObject\n"
	"    ObjectNames = L132SummonedUnit\n"
	"    Count = 1\n"
	"  End\n"
	"End\n"
	"CommandButton Command_L132TriggerSpawn\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = SpellBookL132VariantA\n"
	"End\n"
	"CommandButton Command_L132NonPower\n"
	"  Command = STOP\n"
	"End\n"
	"CommandButton Command_L132SpellButton\n"
	"  Command = SPELL_BOOK\n"
	"  Options = NEED_TARGET_POS\n"
	"  SpecialPower = SpellBookL132Shared\n"
	"  CommandTrigger = Command_L132TriggerSpawn Command_L132NonPower Command_L132DoesNotExist\n"
	"End\n"
	"CommandButton Command_L132TriggerInstant\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = SpellBookL132InstantTriggered\n"
	"End\n"
	"CommandButton Command_L132InstantSpellButton\n"
	"  Command = SPELL_BOOK\n"
	"  SpecialPower = SpellBookL132InstantMain\n"
	"  CommandTrigger = Command_L132TriggerInstant Command_L132NonPower\n"
	"End\n"
	"CommandSet L132SpellBookCommandSet\n"
	"  1 = Command_L132SpellButton\n"
	"  2 = Command_L132InstantSpellButton\n"
	"End\n"
	"Object L132SummonedUnit\n"
	"  Side = Men\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object L132SpellBook\n"
	"  Side = Men\n"
	"  KindOf = SPELL_BOOK UNATTACKABLE INERT\n"
	"  CommandSet = L132SpellBookCommandSet\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = OCLSpecialPower ModuleTag_Shared\n"
	"    SpecialPowerTemplate = SpellBookL132Shared\n"
	"    CreateLocation = CREATE_AT_LOCATION\n"
	"    AvailableAtStart = Yes\n"
	"  End\n"
	"  Behavior = OCLSpecialPower ModuleTag_VariantA\n"
	"    SpecialPowerTemplate = SpellBookL132VariantA\n"
	"    OCL = OCL_L132Summon\n"
	"    CreateLocation = CREATE_AT_LOCATION\n"
	"    AvailableAtStart = Yes\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_InstantMain\n"
	"    SpecialPowerTemplate = SpellBookL132InstantMain\n"
	"    AvailableAtStart = Yes\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_InstantTriggered\n"
	"    SpecialPowerTemplate = SpellBookL132InstantTriggered\n"
	"    AvailableAtStart = Yes\n"
	"  End\n"
	"End\n";

struct Local132Rig : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	CommandStore commandStore;
	CommandStore *savedCommands = TheCommandStore;
	ObjectCreationListStore ocls;
	ObjectCreationListStore *savedOcls = TheObjectCreationListStore;
	ShroudManager shroud{ *logic };
	SpellCommands spellCommands;
	Player *alice = nullptr;

	Local132Rig()
		: combattest::CombatWorld("", 40)
		, ocls(w.keys)
	{
		TheSpecialPowerStore = &powers;
		TheCommandStore = &commandStore;
		TheObjectCreationListStore = &ocls;
		commandStore.setThingFactory(&w.things);

		w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });

		SpecialPowerModules::registerAll(w.modules);
		SpecialPowerModules::installScienceHooks(*logic);
		spellCommands.registerHandlers(dispatcher);

		const std::string err = w.load(kLocal132Ini, INI_LOAD_OVERWRITE, "local132.ini");
		REQUIRE_MESSAGE(err.empty(), err);

		alice = players.findPlayerWithName("Alice");
		REQUIRE(alice != nullptr);
		players.setLocalPlayer(alice);
		shroud.init(40.0f, 1);
		shroud.setExtent(0.0f, 0.0f, 2000.0f, 2000.0f);
		shroud.attach();
		logic->setShroud(&shroud);
		shroud.revealMapForPlayerPermanently(alice->getPlayerIndex());
	}

	~Local132Rig()
	{
		logic->setShroud(nullptr);
		logic->reset();
		TheObjectCreationListStore = savedOcls;
		TheCommandStore = savedCommands;
		TheSpecialPowerStore = savedPowers;
	}
};

size_t countTemplate(GameLogic &logic, const std::string &name)
{
	size_t n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate() && o->getTemplate()->getName() == name)
		{
			++n;
		}
	}
	return n;
}
} // namespace

// Expectations from RotWK 2.01 RW 0x81EBA8..0x81EBC5 -> RW 0x81E147 (0x81E2DE..0x81E3FC):
// GUI_COMMAND_SPELL_BOOK (0x20) with NEED_TARGET_POS emits MSG_DO_SPECIAL_POWER_AT_LOCATION (0x411)
// for the primary power and for every CommandTrigger (+0x188) whose m_command == GUI_COMMAND_SPECIAL_POWER (0x18),
// skipping non-SPECIAL_POWER and unresolved triggers, with source = spell book ObjectID.
TEST_CASE("LOCAL-132 InGameSpellBookModel::clickWorld emits CommandTrigger SPECIAL_POWER messages with source = spell book (RW 0x81EBA8 -> 0x81E147)")
{
	Local132Rig rig;
	Object *book = rig.unit("L132SpellBook", 'A', 100.0f, 100.0f);
	REQUIRE(book);

	InGameSpellBookModel bar;
	REQUIRE(bar.refresh(rig.commandStore, *rig.logic, *rig.alice));
	REQUIRE(bar.buttons().size() == 2u);

	const InGameSpellBookModel::Button &btn = bar.buttons()[0];
	REQUIRE(btn.power);
	REQUIRE(btn.power->getName() == "SpellBookL132Shared");
	CHECK(btn.owned);
	CHECK(btn.usable);
	CHECK(btn.ready);
	CHECK(btn.needsPosition);

	std::vector<GameMessage> out;
	REQUIRE(bar.press(btn.index, rig.alice->getPlayerIndex(), out));
	CHECK(out.empty());
	REQUIRE(bar.targeting());

	const Coord3D targetPos{ 250.0f, 250.0f, 0.0f };
	REQUIRE(bar.clickWorld(targetPos, rig.alice->getPlayerIndex(), out));
	CHECK_FALSE(bar.targeting());

	// RW 0x81E2DE..0x81E3FC: primary + Command_L132TriggerSpawn (Command_L132NonPower and Command_L132DoesNotExist skipped)
	REQUIRE(out.size() == 2u);
	const SpecialPowerTemplate *spMain = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookL132Shared");
	const SpecialPowerTemplate *spTrig = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookL132VariantA");
	REQUIRE(spMain);
	REQUIRE(spTrig);

	CHECK(out[0].getType() == MSG_DO_SPECIAL_POWER_AT_LOCATION);
	CHECK(out[0].getArgument(0)->integer == (int)spMain->getID());
	CHECK(out[0].getArgument(4)->objectID == book->getID());

	CHECK(out[1].getType() == MSG_DO_SPECIAL_POWER_AT_LOCATION);
	CHECK(out[1].getArgument(0)->integer == (int)spTrig->getID());
	CHECK(out[1].getArgument(4)->objectID == book->getID());

	for (const GameMessage &m : out)
	{
		rig.dispatcher.dispatch(m);
	}
	rig.logic->runLogicFrame();
	CHECK(countTemplate(*rig.logic, "L132SummonedUnit") == 1u);
}

// Expectations from RotWK 2.01 RW 0x94157D..0x94160F:
// Targetless press emits MSG_DO_SPECIAL_POWER (0x410) for the primary power and every SPECIAL_POWER
// CommandTrigger button with source = spell book ObjectID, starting recharge on both modules.
TEST_CASE("LOCAL-132 InGameSpellBookModel::press (targetless) emits CommandTrigger SPECIAL_POWER messages with source = spell book (RW 0x94157D..0x94160F)")
{
	Local132Rig rig;
	Object *book = rig.unit("L132SpellBook", 'A', 100.0f, 100.0f);
	REQUIRE(book);

	InGameSpellBookModel bar;
	REQUIRE(bar.refresh(rig.commandStore, *rig.logic, *rig.alice));
	REQUIRE(bar.buttons().size() == 2u);

	const InGameSpellBookModel::Button &btn = bar.buttons()[1];
	REQUIRE(btn.power);
	REQUIRE(btn.power->getName() == "SpellBookL132InstantMain");
	CHECK_FALSE(btn.needsPosition);

	std::vector<GameMessage> out;
	REQUIRE(bar.press(btn.index, rig.alice->getPlayerIndex(), out));
	CHECK_FALSE(bar.targeting());

	REQUIRE(out.size() == 2u);
	const SpecialPowerTemplate *spMain = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookL132InstantMain");
	const SpecialPowerTemplate *spTrig = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookL132InstantTriggered");
	REQUIRE(spMain);
	REQUIRE(spTrig);

	CHECK(out[0].getType() == MSG_DO_SPECIAL_POWER);
	CHECK(out[0].getArgument(0)->integer == (int)spMain->getID());
	CHECK(out[0].getArgument(2)->objectID == book->getID());

	CHECK(out[1].getType() == MSG_DO_SPECIAL_POWER);
	CHECK(out[1].getArgument(0)->integer == (int)spTrig->getID());
	CHECK(out[1].getArgument(2)->objectID == book->getID());

	for (const GameMessage &m : out)
	{
		rig.dispatcher.dispatch(m);
	}
	SpecialPowerModuleInterface *mMain = SpecialPowerModules::findModule(*book, spMain);
	SpecialPowerModuleInterface *mTrig = SpecialPowerModules::findModule(*book, spTrig);
	REQUIRE(mMain);
	REQUIRE(mTrig);
	CHECK_FALSE(mMain->isReady());
	CHECK_FALSE(mTrig->isReady());
}
