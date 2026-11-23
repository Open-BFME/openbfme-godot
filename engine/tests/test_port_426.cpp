// PORT-426 (fork issue 426, P-108): a Shift press of a UNIT_BUILD button queues five units.
// Synthetic INI only: no ROTWK_INSTALL / OPENBFME_ENNORATH_INSTALL, no retail files.
//
// RW 2.01 (workspace/RotWK/game.dat, md5 daed3668...):
//   * RW 0x940435 ControlBar::processCommandUI, its UNIT_BUILD case RW 0x940B0E;
//   * RW 0x940BB3 -> RW 0x63F122 Keyboard::isShift (word +0xC, bits 0x30 / 0x400);
//   * RW 0x940BC2 excludes REVIVE, RW 0x940BCF reads ThingTemplate + 0x5F0
//     (MaxSimultaneousOfType, field table offset 1520), RW 0x940BD9 sets `multiple`,
//     RW 0x940BE6..0x940BF0 is the repeated-press gate (not ported);
//   * RW 0x940C06 message 0x417 (MSG_QUEUE_UNIT_CREATE), its fourth argument
//     `multiple` is appended at RW 0x940C3E;
//   * RW 0x77A764 dispatcher reads that argument at 0x77A7D1 and hands it to
//     ProductionUpdate::queueCreateUnit (slot +0x20, RW 0x77A83C);
//   * RW 0x8A11D2 queueCreateUnit: count = batch ? 5 : 1 at RW 0x8A1260
//     (`lea eax,[eax*4+1]`), queue limit RW 0x8A1287, Money::withdraw RW 0x7B17EF,
//     early exit when cash < cost at RW 0x8A13C0.
// DONOR BFME2 0x00539DCB (ControlBarCommandProcessing.cpp:514, `multiple`),
// DONOR BFME2 0x0049DA34 (ProductionUpdateQueue.cpp:1919, `five ? 5 : 1`).

#include "doctest.h"

#include "ProdTestUtil.h"

#include "Common/BuildAssistant.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include <string>
#include <vector>

namespace
{
struct Rig : prodtest::ProdWorld
{
	PinholeView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{ *logic, nullptr, view, ui, stream, &commands, {} };
	ControlBar bar{ ctx };
	GameLogicDispatch dispatch{ *logic };
	Object *barracks = nullptr;

	explicit Rig(const char *soldierExtra = "")
	{
		const std::string objects = std::string(
			"Object P426Soldier\n"
			"  BuildCost = 100\n"
			"  BuildTime = 10.0\n"
			"  CommandPoints = 4\n"
			"  KindOf = SELECTABLE INFANTRY\n"
			"  Behavior = AIUpdateInterface ModuleTag_AI\n"
			"  End\n") + soldierExtra +
			"End\n"
			"Object P426Barracks\n"
			"  KindOf = STRUCTURE SELECTABLE\n"
			"  CommandSet = P426Set\n"
			"  Behavior = ProductionUpdate ModuleTag_PU\n"
			"    MaxQueueEntries = 10\n" // RW 0x8A1287: a batch cannot pass the queue's own limit
			"  End\n"
			"End\n";
		load(objects);
		load("CommandButton P426Build\n"
		     "  Command = UNIT_BUILD\n"
		     "  Object = P426Soldier\n"
		     "  InPalantir = Yes\n"
		     "  Options = CANCELABLE\n"
		     "End\n"
		     "CommandSet P426Set\n"
		     "  1 = P426Build\n"
		     "End\n");
		barracks = make("P426Barracks", teamOf("Alice"));
		REQUIRE(barracks != nullptr);
		const Coord3D pos{ 100.0f, 100.0f, 0.0f };
		barracks->setPosition(&pos);
		players.setLocalPlayer(alice());
		ui.selectObject(barracks->getID());
		bar.update();
		GameMessage selected(MSG_CREATE_SELECTED_GROUP, alice()->getPlayerIndex());
		selected.appendBooleanArgument(true);
		selected.appendObjectIDArgument(barracks->getID());
		dispatch.dispatch(selected); // RW 0x779A21: the producer is the first object of the player's selection
	}

	void cash(unsigned amount)
	{
		auto *money = alice()->getMoney();
		money->withdraw(money->countMoney(), false);
		money->deposit(amount);
	}
	ProductionUpdateInterface *pu() { return barracks->getProductionUpdate(); }
	const ControlBarButton *buildButton()
	{
		for (const ControlBarButton &b : bar.palantirButtons())
		{
			if (b.button && b.button->m_name == "P426Build")
			{
				return &b;
			}
		}
		return nullptr;
	}
	// Press as the HUD does, keep the client message and run it through the lockstep dispatcher.
	ClientMessage press(bool shift)
	{
		bar.update();
		const ControlBarButton *b = buildButton();
		REQUIRE(b != nullptr);
		REQUIRE(bar.pressButton(b->slot, true, shift));
		std::vector<ClientMessage> out = stream.propagate();
		for (const ClientMessage &m : out)
		{
			if (m.type() == MSG_QUEUE_UNIT_CREATE)
			{
				ClientMessage kept = m;
				dispatch.dispatch(toGameMessage(m, alice()->getPlayerIndex()));
				return kept;
			}
		}
		FAIL("no MSG_QUEUE_UNIT_CREATE reached the command stream");
		return ClientMessage(0);
	}
};
} // namespace

TEST_CASE("PORT-426 Shift on a production button sends the RW batch flag and queues five; a plain press queues one")
{
	Rig r;
	r.cash(1000);
	const ClientMessage shift = r.press(true);
	REQUIRE(shift.argumentCount() == 5);
	CHECK(shift.arg(3).kind == ClientArgKind::Boolean);
	CHECK(shift.arg(3).boolean);                     // RW 0x940C3E: `multiple`
	CHECK(r.pu()->getProductionCount() == 5);        // RW 0x8A1260: count = batch ? 5 : 1
	CHECK(r.alice()->getMoney()->countMoney() == 500); // five withdrawals of the 100-cost soldier, RW 0x8A1292

	Rig plain;
	plain.cash(1000);
	const ClientMessage single = plain.press(false); // neighbour: the same button without Shift
	REQUIRE(single.argumentCount() == 5);
	CHECK_FALSE(single.arg(3).boolean);
	CHECK(plain.pu()->getProductionCount() == 1);
	CHECK(plain.alice()->getMoney()->countMoney() == 900);
}

TEST_CASE("PORT-426 money for two units stops the batch at two (RW 0x8A13C0)")
{
	Rig two;
	two.cash(200);
	const ClientMessage m = two.press(true);
	CHECK(m.arg(3).boolean);                          // the client still asks for a batch
	CHECK(two.pu()->getProductionCount() == 2);       // RW 0x8A13C0: cash < cost ends the loop
	CHECK(two.alice()->getMoney()->countMoney() == 0);

	Rig shortCash; // 250 leaves 50 after two units: the loop stops before a third withdrawal
	shortCash.cash(250);
	shortCash.press(true);
	CHECK(shortCash.pu()->getProductionCount() == 2);
	CHECK(shortCash.alice()->getMoney()->countMoney() == 50);
}

TEST_CASE("PORT-426 MaxSimultaneousOfType keeps a Shift press at one unit (RW 0x940BCF)")
{
	Rig r("  MaxSimultaneousOfType = 5\n");
	REQUIRE(BuildAssistant::maxSimultaneousOfType(*r.w.get("P426Soldier")) == 5);
	r.cash(1000);
	const ClientMessage m = r.press(true);
	REQUIRE(m.argumentCount() == 5);
	CHECK_FALSE(m.arg(3).boolean);                   // RW 0x940BCF: [tmpl+0x5F0] != 0 clears `multiple`
	CHECK(r.pu()->getProductionCount() == 1);
	CHECK(r.alice()->getMoney()->countMoney() == 900);
}
