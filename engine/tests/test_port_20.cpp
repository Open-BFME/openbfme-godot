// OpenBFME. PORT-20 tests: the contain's vslot 0x64 onSelling — the call BuildAssistant::sellObject makes on a sold building's contain.
//
// RotWK 2.01 facts (game.dat md5 daed3668, caveat S-001), each read with tools/mod/rw.py on the address in the comment:
//
//   * the sale's call: RW 0x796804 `mov ecx, [esi + 0x258]` (the contain interface, the object's ContainModuleInterface pointer), `mov eax, [ecx]`, RW 0x796806
//     `call dword ptr [eax + 0x64]` — contain vslot 0x64, no argument. It is the only thing the sale leaves to the contain.
//   * slot 0x64 is NOT overridden by any contain class: the same body RW 0x86561F sits at + 0x64 of all 16 contain tables of 2.01 (OpenContain RW 0xC59AF0
//     -> 0xC59B54, TransportContain 0xC5A690 -> 0xC5A6F4, HordeTransportContain 0xC5C370 -> 0xC5C3D4, GarrisonContain 0xC5C698 -> 0xC5C6FC,
//     HordeGarrisonContain 0xC5C9F0 -> 0xC5CA54, SiegeEngineContain 0xC5D668, HordeSiegeEngineContain 0xC5DA10, TunnelContain and the rest). BFME2's
//     GarrisonContain::onSelling (removeAllContained first) is not in RotWK.
//   * RW 0x86561F: `mov eax, [ecx]; push 2 (CMD_FROM_AI); mov byte ptr [ecx + 0xC1], 0 (module + 0xE1); call dword ptr [eax + 0x80]` (contain vslot 0x80,
//     orderAllPassengersToExit). So the sale clears the kill-on-death byte and then orders the riders out — through the virtual, so a horde garrison answers
//     with its own slot 0x80 (RW 0x87CE8F -> RW 0x991027) and any other contain with RW 0x867D6A (every rider that has an AI: aiExit, RW 0x7716C1).
//   * the byte: 1 when the contain is built (RW 0x86806C), read by RW 0x86588C = `[module + 0xE1]` AND `[module data + 0x83]` (KillPassengersOnDeath). Its
//     readers are GarrisonContain::onRemoving (RW 0x87C0B6, the gate of the fling-and-kill branch RW 0x87C0DD), HordeGarrisonContain::onDie (RW 0x87CF7D) and
//     the horde transport's die (RW 0x87AE29). The sale is what clears it: a garrison destroyed WITHOUT a sale keeps it and takes the branch.
//
// Synthetic data only (no retail files): a sellable tower with a GarrisonContain and soldiers with an AI. KillPassengersOnDeath is Yes here so that the
// gate above can be seen at all (no sellable retail structure sets it: the only retail Yes is GondorGwaihir's TransportContain). The expectations are
// RW's, not the ported code's.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/BuildAssistantSell.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <string>

using namespace combattest;

namespace
{
// A tower the sale accepts (KindOf STRUCTURE, a CommandSet with a SELL button, RW 0x7966C6 .. 0x7966F6) with a garrison that kills its riders on death.
const char kPort20Objects[] =
	"Object Port20Tower\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  CommandSet = Port20TowerSet\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 60\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = GarrisonContain ModuleTag_Contain\n"
	"    ContainMax = 5\n"
	"    DamagePercentToUnits = 0%\n"
	"    KillPassengersOnDeath = Yes\n"
	"  End\n"
	"End\n"
	"Object Port20Soldier\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

// the button the tower's command set offers (GUI_COMMAND_SELL: RW 0x793D57 looks for it in the 33 slots) and a set holding it
const char kPort20Commands[] =
	"CommandButton Command_Port20Sell\n"
	"  Command = SELL\n"
	"End\n"
	"CommandSet Port20TowerSet\n"
	"  1 = Command_Port20Sell\n"
	"End\n";

struct Port20World : CombatWorld
{
	CommandStore commands;
	CommandStore *savedStore = nullptr;

	Port20World() : CombatWorld(kPort20Objects)
	{
		savedStore = TheCommandStore;
		TheCommandStore = &commands;
		commands.setThingFactory(&w.things);
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		const std::string err = w.load(kPort20Commands, INI_LOAD_OVERWRITE, "port20-command.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		logic->sellList().registerHandlers(dispatcher); // MSG_SELL, RW 0x77B2C8
		// GameData SellPercentage loaded: the refund is not under test here, and without the row the sale itself notes S-1981 (RW 0x794707 .. 0x794736)
		logic->settings().sellPercentageLoaded = true;
	}
	~Port20World() { TheCommandStore = savedStore; }

	// one MSG_SELL for `tower` from A's player, as the Sell press sends it (the group the message sells is that player's selection)
	void sell(Object *tower, char side = 'A')
	{
		Player *p = playerOf(side);
		p->selection().clear();
		p->selection().push_back(tower->getID());
		dispatcher.dispatch(GameMessage(MSG_SELL, p->getPlayerIndex()));
	}
	// the payout and the kill BuildAssistant::update does in the frame's phase 5 (RW 0x79468B), called here without the frame so that the sold object and its
	// contain are still readable after the death (the frame's destroy list would delete them)
	void payOut() { logic->sellList().update(); }
	static OpenContain *containOf(Object *o) { return dynamic_cast<OpenContain *>(o->getContain()); }
};

// the contain's kill-on-death question through its one reader (RW 0x86588C: the byte AND the data's KillPassengersOnDeath row)
bool killingRiders(Object *tower) { return Port20World::containOf(tower)->getKillPassengersOnDeath(); }

// the rider is on its way out of `tower` (aiExit, RW 0x7716C1: AI_EXIT, the command source, the container as the goal)
void checkOrderedOut(Object *rider, Object *tower)
{
	AIUpdateInterface *ai = rider->getAIUpdateInterface();
	REQUIRE(ai);
	CHECK(ai->stateMachine().currentStateId() == (unsigned)AI_EXIT);
	CHECK(ai->lastCommandSource() == CMD_FROM_AI);
	CHECK(ai->stateMachine().goalObjectID() == tower->getID());
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// the sale's slot 0x64: RW 0x796806 -> RW 0x86561F
// ---------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("PORT-20 sell: the contain hears of the sale (RW 0x796806 -> vslot 0x64 = RW 0x86561F) — every rider is ordered out with CMD_FROM_AI and the "
	"kill-on-death byte is cleared")
{
	Port20World w;
	Object *tower = w.unit("Port20Tower", 'A', 300, 300);
	Object *first = w.unit("Port20Soldier", 'A', 330, 300);
	Object *second = w.unit("Port20Soldier", 'A', 330, 340);
	OpenContain *contain = Port20World::containOf(tower);
	REQUIRE(contain);
	REQUIRE(contain->addToContain(first));
	REQUIRE(contain->addToContain(second));
	REQUIRE(contain->getContainCount() == 2u);
	// RW 0x86806C sets the byte to 1 in every contain; RW 0x86588C answers 1 while it is set and the data says KillPassengersOnDeath = Yes
	REQUIRE(killingRiders(tower));

	w.sell(tower);

	// RW 0x865623: the byte is cleared by the sale, so the port's reader of it answers 0 from here on
	CHECK_FALSE(killingRiders(tower));
	// RW 0x86562A: then vslot 0x80 with CMD_FROM_AI — every rider of the list, in the list's order (RW 0x867D6A walks the contained list front to back)
	checkOrderedOut(first, tower);
	checkOrderedOut(second, tower);
	// RW 0x79675B: the entry is in the sell list, so the structure dies in the first phase 5 after the sale (RW 0x794885)
	REQUIRE(w.logic->sellList().list().size() == 1u);
	w.payOut();
	CHECK(w.logic->sellList().list().empty());
	CHECK(tower->isEffectivelyDead());
	// the riders leave the dead tower free and alive: the byte the sale cleared keeps RW 0x87C0B6's killing branch away from them
	CHECK(first->getContainedBy() == nullptr);
	CHECK(second->getContainedBy() == nullptr);
	CHECK_FALSE(first->isEffectivelyDead());
	CHECK_FALSE(second->isEffectivelyDead());
	CHECK(contain->openStats().ridersDestroyedOnDeath == 0u);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the neighbour: the same tower destroyed WITHOUT a sale keeps the byte (RW 0x86588C) and its rider meets RW 0x87C0B6's branch
// ---------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("PORT-20 sell: a garrison destroyed without a sale keeps the kill-on-death byte, so its rider meets the branch the sale spared it")
{
	Port20World w;
	Object *sold = w.unit("Port20Tower", 'A', 300, 300);
	Object *killed = w.unit("Port20Tower", 'A', 500, 300);
	Object *soldRider = w.unit("Port20Soldier", 'A', 330, 300);
	Object *killedRider = w.unit("Port20Soldier", 'A', 530, 300);
	OpenContain *soldContain = Port20World::containOf(sold);
	OpenContain *killedContain = Port20World::containOf(killed);
	REQUIRE(soldContain);
	REQUIRE(killedContain);
	REQUIRE(soldContain->addToContain(soldRider));
	REQUIRE(killedContain->addToContain(killedRider));

	w.sell(sold);
	w.payOut();
	REQUIRE(sold->isEffectivelyDead());
	// the second tower's byte was never cleared: only the sale clears it (RW 0x865623 is reached from RW 0x796806 alone)
	REQUIRE(killingRiders(killed));
	killed->kill(DAMAGE_UNRESISTABLE, DEATH_SUICIDED); // the death the sale gives a structure (RW 0x794885)
	REQUIRE(killed->isEffectivelyDead());

	// both riders are out of their dead tower and alive; the difference is the gate of RW 0x87C0B6, whose branch the port still reports (S-1100)
	CHECK(soldRider->getContainedBy() == nullptr);
	CHECK(killedRider->getContainedBy() == nullptr);
	CHECK_FALSE(soldRider->isEffectivelyDead());
	CHECK_FALSE(killedRider->isEffectivelyDead());
	CHECK(soldContain->openStats().ridersDestroyedOnDeath == 0u);
	CHECK(killedContain->openStats().ridersDestroyedOnDeath == 1u);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the sale of an empty garrison: the byte is cleared, nobody is ordered, the structure still dies
// ---------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("PORT-20 sell: an empty garrison's sale clears the byte and orders nobody")
{
	Port20World w;
	Object *tower = w.unit("Port20Tower", 'A', 300, 300);
	OpenContain *contain = Port20World::containOf(tower);
	REQUIRE(contain);
	REQUIRE(contain->getContainCount() == 0u);
	REQUIRE(killingRiders(tower));
	w.sell(tower);
	CHECK_FALSE(killingRiders(tower));
	REQUIRE(w.logic->sellList().list().size() == 1u);
	w.payOut();
	CHECK(tower->isEffectivelyDead());
	CHECK(contain->openStats().ejectedOnDeath == 0u);
}
