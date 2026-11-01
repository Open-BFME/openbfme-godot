// OpenBFME. GPL-3.0. Retail selection -> command bar -> lockstep command regressions.
#include "HudTestUtil.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/WeaponSetToggle.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/Network.h"

#include <algorithm>
#include <deque>

namespace
{
struct GroupRig
{
	hudtest::Rig rig;
	ControlBar bar;
	std::vector<Object *> units;
	explicit GroupRig() : rig(hudtest::shared()), bar(rig.input->context())
	{
		rig.input->context().commands = &hudtest::shared().world->commands();
	}
	Object *make(const char *name)
	{
		const Coord3D c = rig.freeSpot(2400.0f, 1800.0f, 250.0f);
		Object *o = rig.make(name, c.x, c.y);
		units.push_back(o);
		return o;
	}
	void select(const std::vector<Object *> &objects)
	{
		rig.input->ui().deselectAll(true);
		bool add = false;
		for (Object *o : objects)
		{
			rig.lookAt(*o->getPosition());
			const Coord3D p = *o->getPosition();
			const ICoord2D pixel = rig.screenOf({ p.x, p.y, p.z + 6.0f });
			if (add) rig.setModifier(KEY_LSHIFT, KEY_STATE_LSHIFT, true);
			rig.leftClick(pixel.x, pixel.y);
			if (add) rig.setModifier(KEY_LSHIFT, KEY_STATE_LSHIFT, false);
			REQUIRE(rig.input->ui().isSelected(o->getID()));
			add = true;
		}
		REQUIRE(rig.input->ui().selected().size() == objects.size());
		bar.update();
	}
	void press(GUICommandType command)
	{
		bar.update();
		const ControlBarButton *button = nullptr;
		for (const auto *list : { &bar.palantirButtons(), &bar.sideButtons(), &bar.offBarButtons() })
			for (const auto &b : *list)
				if (b.button && b.button->m_command == command) button = &b;
		REQUIRE(button);
		REQUIRE(bar.pressButton(button->slot, button->inPalantir));
		rig.input->update();
	}
};

HordeContain &contain(Object *o)
{
	auto *h = dynamic_cast<HordeContain *>(o->getContain());
	REQUIRE(h);
	return *h;
}

bool porcupine(Object *o) { return contain(o).hordeData().m_isPorcupineFormation; }
}

TEST_CASE("group command retail: multi-horde formation uses one voted target and one set message per horde")
{
	if (!hudtest::haveWorld("group command retail")) return;
	GroupRig g;
	Object *a = g.make("GondorTowerShieldGuardHorde");
	Object *b = g.make("GondorTowerShieldGuardHorde");
	Object *c = g.make("GondorTowerShieldGuardHorde");
	std::vector<Object *> selected{ a, b, c };
	bool mixed = false;
	SUBCASE("all have the command") {}
	SUBCASE("only some have it: archers and a single soldier must not become pikemen")
	{
		selected.insert(selected.begin(), g.make("GondorArcherHorde"));
		selected.insert(selected.begin(), g.make("GondorFighter"));
	}
	SUBCASE("already on for one: majority next formation wins, not the source's inverse") { mixed = true; }
	g.rig.frame(5);
	for (Object *o : { a, b, c }) REQUIRE(contain(o).canToggleFormation());
	if (mixed)
	{
		g.select({ c });
		g.press(GUI_COMMAND_HORDE_TOGGLE_FORMATION);
		g.rig.frame();
		REQUIRE(porcupine(c));
	}
	g.select(selected); // c is the source; in the mixed case it disagrees with the majority.
	const auto before = g.rig.logic().computeStateHash();
	g.press(GUI_COMMAND_HORDE_TOGGLE_FORMATION);
	CHECK(g.rig.logic().computeStateHash() == before); // no client-side simulation mutation
	unsigned count = 0;
	const ThingTemplate *target = hudtest::shared().world->things().findTemplate("GondorTowerShieldGuardHordePorcupine");
	REQUIRE(target);
	for (const GameMessage &m : g.rig.game->commands().messages())
	{
		CHECK(m.getType() != MSG_HORDE_TOGGLE_FORMATION);
		if (m.getType() != MSG_HORDE_SET_FORMATION) continue;
		REQUIRE(m.getArgumentCount() == 2);
		CHECK(m.getArgument(0)->type == ARGUMENTDATATYPE_OBJECTID);
		CHECK(m.getArgument(1)->type == ARGUMENTDATATYPE_INTEGER);
		CHECK(m.getArgument(1)->integer == target->getTemplateID());
		CHECK(m.getPlayerIndex() == g.rig.index());
		++count;
	}
	CHECK(count == (selected.size() == 3 ? 3u : 4u));
	g.rig.frame();
	for (Object *o : { a, b, c }) CHECK(porcupine(o));
	CHECK(g.rig.local->selection().size() == selected.size());
	for (Object *o : selected)
	{
		if (o == a || o == b || o == c) continue;
		CHECK(o->getTemplate()->getName() != target->getName());
		if (o->getContain()) CHECK_FALSE(porcupine(o));
	}
	g.press(GUI_COMMAND_HORDE_TOGGLE_FORMATION);
	g.rig.frame();
	for (Object *o : { a, b, c }) CHECK_FALSE(porcupine(o));
}

TEST_CASE("group command retail: stances and weapon toggles retain their group dispatch")
{
	if (!hudtest::haveWorld("group command retail")) return;
	GroupRig g;
	Object *a = g.make("RohanRohirrimHorde");
	Object *b = g.make("RohanRohirrimHorde");
	Object *single = g.make("GondorFighterHorde");
	g.rig.frame(5);
	StancesBehavior *stanceA = StancesBehavior::of(*a);
	StancesBehavior *stanceB = StancesBehavior::of(*b);
	StancesBehavior *singleStance = StancesBehavior::of(*single);
	REQUIRE(stanceA);
	REQUIRE(stanceB);
	REQUIRE(singleStance);
	g.select({ a });
	g.press(GUI_COMMAND_TOGGLE_WEAPONSET);
	g.rig.frame();
	const int bit = WeaponSetToggle::WEAPONSET_TOGGLE_1;
	auto on = [bit](Object *o) { return (o->getWeaponSetFlags()[(size_t)bit / 32] & (1u << (bit % 32))) != 0; };
	REQUIRE(on(a));
	REQUIRE_FALSE(on(b));
	g.select({ single, a, b });
	g.press(GUI_COMMAND_TOGGLE_WEAPONSET);
	g.rig.frame();
	CHECK(on(a));
	CHECK(on(b));
	CHECK_FALSE(on(single));
	// lane CAMPUI-1: the stance toggle's own press sends no stance in retail (RW 0x96FAC6 opens the Palantir's radial, test_campui1_stance.cpp); the group
	// dispatch of a stance is a SET_STANCE press (the same MSG_CHANGE_STANCE, RW 0x96F53C)
	g.press(GUI_COMMAND_SET_STANCE);
	g.rig.frame();
	const auto expected = stanceB->getStance();
	CHECK(stanceA->getStance() == expected);
	CHECK(singleStance->getStance() == expected);
}

TEST_CASE("group command retail: shield wall and wedge dispatch use the same formation path")
{
	if (!hudtest::haveWorld("group command retail")) return;
	const char *name = "GondorFighterHorde";
	SUBCASE("shield wall") {}
	SUBCASE("wedge") { name = "RohanRohirrimArcherHorde"; }
	GroupRig g;
	Object *a = g.make(name);
	Object *b = g.make(name);
	g.rig.frame(5);
	const std::string next = contain(a).hordeData().m_alternateFormation;
	REQUIRE_FALSE(next.empty());
	REQUIRE(contain(a).canToggleFormation());
	REQUIRE(contain(b).canToggleFormation());
	g.select({ a, b });
	g.press(GUI_COMMAND_HORDE_TOGGLE_FORMATION);
	g.rig.frame();
	for (Object *o : { a, b })
	{
		CHECK(contain(o).hordeData().m_alternateFormation == name);
		CHECK_FALSE(contain(o).hordeData().m_thisFormationIsTheMainFormation);
	}
	g.press(GUI_COMMAND_HORDE_TOGGLE_FORMATION);
	g.rig.frame();
	for (Object *o : { a, b })
	{
		CHECK(contain(o).hordeData().m_alternateFormation == next);
		CHECK(contain(o).hordeData().m_thisFormationIsTheMainFormation);
	}
}

TEST_CASE("group command retail: a tied formation vote keeps the lowest template id independent of selection order")
{
	if (!hudtest::haveWorld("group command retail")) return;
	GroupRig g;
	Object *a = g.make("GondorTowerShieldGuardHorde");
	Object *b = g.make("GondorTowerShieldGuardHorde");
	g.rig.frame(5);
	g.select({ a });
	g.press(GUI_COMMAND_HORDE_TOGGLE_FORMATION);
	g.rig.frame();
	REQUIRE(porcupine(a));
	REQUIRE_FALSE(porcupine(b));
	std::vector<Object *> selected{ a, b };
	SUBCASE("off source") {}
	SUBCASE("on source") { selected = { b, a }; }
	const auto *main = hudtest::shared().world->things().findTemplate("GondorTowerShieldGuardHorde");
	const auto *alt = hudtest::shared().world->things().findTemplate("GondorTowerShieldGuardHordePorcupine");
	REQUIRE(main);
	REQUIRE(alt);
	const int wanted = std::min(main->getTemplateID(), alt->getTemplateID());
	g.select(selected);
	g.press(GUI_COMMAND_HORDE_TOGGLE_FORMATION);
	unsigned count = 0;
	for (const GameMessage &m : g.rig.game->commands().messages())
		if (m.getType() == MSG_HORDE_SET_FORMATION)
		{
			REQUIRE(m.getArgumentCount() == 2);
			CHECK(m.getArgument(1)->integer == wanted);
			++count;
		}
	CHECK(count == 2);
	g.rig.frame();
	CHECK(porcupine(a) == (wanted == alt->getTemplateID()));
	CHECK(porcupine(b) == (wanted == alt->getTemplateID()));
}

TEST_CASE("group command retail: single siege units share the reference hold-fire state")
{
	if (!hudtest::haveWorld("group command retail")) return;
	GroupRig g;
	Object *a = g.make("GondorTrebuchet");
	Object *b = g.make("GondorTrebuchet");
	g.rig.frame(5);
	const int status = CombatNames::status("NO_AUTO_ACQUIRE");
	REQUIRE(status != -1);
	REQUIRE_FALSE(a->testStatus((unsigned)status));
	REQUIRE_FALSE(b->testStatus((unsigned)status));
	g.select({ a });
	g.press(GUI_COMMAND_TOGGLE_NO_AUTO_ACQUIRE);
	g.rig.frame();
	REQUIRE(a->testStatus((unsigned)status));
	g.select({ a, b });
	const auto before = g.rig.logic().computeStateHash();
	g.press(GUI_COMMAND_TOGGLE_NO_AUTO_ACQUIRE);
	CHECK(g.rig.logic().computeStateHash() == before);
	g.rig.frame();
	CHECK(a->testStatus((unsigned)status));
	CHECK(b->testStatus((unsigned)status));
	g.press(GUI_COMMAND_TOGGLE_NO_AUTO_ACQUIRE);
	g.rig.frame();
	CHECK_FALSE(a->testStatus((unsigned)status));
	CHECK_FALSE(b->testStatus((unsigned)status));
}

namespace
{
struct GroupLink : NetTransport
{
	GroupLink *other = nullptr;
	int slot = 0;
	std::deque<std::pair<int, std::vector<std::uint8_t>>> inbox;
	bool sendTo(int to, const std::vector<std::uint8_t> &bytes, std::string *) override
	{
		if (!other || to != other->slot) return false;
		other->inbox.push_back({ slot, bytes });
		return true;
	}
	bool receive(int &from, std::vector<std::uint8_t> &bytes) override
	{
		if (inbox.empty()) return false;
		from = inbox.front().first;
		bytes = std::move(inbox.front().second);
		inbox.pop_front();
		return true;
	}
};

struct HashedGroupDriver : LockstepDriver
{
	explicit HashedGroupDriver(Network &net) : LockstepDriver(&net, nullptr) {}
	Capture capture() const override
	{
		Capture c = LockstepDriver::capture();
		c.hashEveryFrame = true;
		return c;
	}
};
}

TEST_CASE("group command retail: multiplayer relays the HUD selection and formation messages with identical simulation hashes")
{
	if (!hudtest::haveWorld("group command retail")) return;
	std::vector<FrameBatch> received;
	std::vector<std::uint32_t> senderHashes;
	UnsignedInt firstFrame = 0;
	{
		GroupRig g;
		Object *a = g.make("GondorTowerShieldGuardHorde");
		Object *b = g.make("GondorTowerShieldGuardHorde");
		Object *single = g.make("GondorFighter");
		g.rig.frame(5);
		firstFrame = g.rig.logic().getFrame();
		g.select({ single, a, b });
		g.press(GUI_COMMAND_HORDE_TOGGLE_FORMATION);
		CHECK_FALSE(porcupine(a));
		CHECK_FALSE(porcupine(b));
		CHECK(g.rig.local->selection().empty());
		GroupLink links[2];
		links[0].other = &links[1];
		links[1].other = &links[0];
		links[1].slot = 1;
		NetworkConfig config;
		config.slotPlayerIndex[0] = g.rig.index();
		config.slotPlayerIndex[1] = g.rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex();
		config.crcInterval = 0;
		config.runAhead = 1;
		Network sender(config, links[0]);
		config.localSlot = 1;
		Network receiver(config, links[1]);
		sender.sendLoadComplete();
		receiver.sendLoadComplete();
		HashedGroupDriver local(sender);
		LockstepDriver remote(&receiver, nullptr);
		const UnsignedInt stopFrame = firstFrame + 4;
		local.setStopFrame(stopFrame);
		remote.setStopFrame(stopFrame);
		local.setOnCompleted([&](const FrameCompletion &c) {
			REQUIRE(c.hashed);
			senderHashes.push_back(c.hash);
		});
		g.rig.game->setFrameDriver(&local);
		UnsignedInt remoteFrame = firstFrame;
		CommandList empty;
		for (unsigned tick = 0; tick < 100 && (g.rig.logic().getFrame() < stopFrame || remoteFrame < stopFrame); ++tick)
		{
			remote.pump(remoteFrame, empty);
			FrameBatch batch;
			if (remote.acquire(remoteFrame, batch))
			{
				received.push_back(batch);
				FrameCompletion complete;
				complete.batchFrame = remoteFrame;
				complete.frame = ++remoteFrame;
				remote.completed(complete);
			}
			g.rig.frame();
		}
		g.rig.game->setFrameDriver(nullptr);
		REQUIRE(g.rig.logic().getFrame() == stopFrame);
		REQUIRE(received.size() == 4);
		REQUIRE(senderHashes.size() == 4);
		CHECK(sender.errors().empty());
		CHECK(receiver.errors().empty());
		CHECK(local.errors().empty());
		CHECK(remote.errors().empty());
		CHECK(porcupine(a));
		CHECK(porcupine(b));
		CHECK(g.rig.local->selection().size() == 3);
	}
	// Replay the other peer's decoded batches in a fresh identical world, without a local selection or button press.
	GroupRig peer;
	Object *a = peer.make("GondorTowerShieldGuardHorde");
	Object *b = peer.make("GondorTowerShieldGuardHorde");
	peer.make("GondorFighter");
	peer.rig.frame(5);
	for (size_t i = 0; i < received.size(); ++i)
	{
		const FrameBatch &batch = received[i];
		REQUIRE(batch.frame == peer.rig.logic().getFrame());
		unsigned formations = 0;
		for (const GameMessage &m : batch.commands)
		{
			CHECK(m.getPlayerIndex() == peer.rig.index());
			formations += m.getType() == MSG_HORDE_SET_FORMATION ? 1u : 0u;
			peer.rig.game->commands().append(m);
		}
		CHECK(formations == (batch.frame == firstFrame + 1 ? 2u : 0u));
		peer.rig.frame();
		CHECK(porcupine(a) == (batch.frame != firstFrame));
		CHECK(porcupine(b) == (batch.frame != firstFrame));
		CHECK(peer.rig.logic().computeStateHash() == senderHashes[i]);
	}
	CHECK(peer.rig.input->ui().selected().empty());
	CHECK(peer.rig.local->selection().size() == 3);
}
