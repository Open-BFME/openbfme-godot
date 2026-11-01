// OpenBFME retail tests (lane FACT-B): the Bloodthirsty sacrifice command through the real HUD.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"
#include "FactbRig.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/UnitVoiceResponse.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BloodthirstyUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameNetwork/NetPacket.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
struct BloodthirstyVoiceSink : UnitVoiceResponse::Sink
{
	std::vector<std::string> sounds;
	std::vector<std::string> eva;

	bool soundExists(const std::string &) override { return true; }
	void playSound(const std::string &name, ObjectID, int) override { sounds.push_back(name); }
	void reportEva(const std::string &name, const Coord3D *) override { eva.push_back(name); }
};

bool reachableSpot(hudtest::Rig &rig, Object *walker, float distance, Coord3D &out)
{
	const Coord3D from = *walker->getPosition();
	for (int direction = 0; direction < 16; ++direction)
	{
		const float angle = 6.2831853f * (float)direction / 16.0f;
		const Coord3D target{ from.x + std::cos(angle) * distance, from.y + std::sin(angle) * distance, 0.0f };
		walker->getAIUpdateInterface()->aiMoveToPosition(target, CMD_FROM_AI);
		for (int frame = 0; frame < 80; ++frame)
		{
			rig.game->advance(0.2);
		}
		const float remaining = std::hypot(walker->getPosition()->x - target.x, walker->getPosition()->y - target.y);
		walker->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
		Coord3D restore = from;
		walker->setPosition(&restore);
		for (int frame = 0; frame < 20; ++frame)
		{
			rig.game->advance(0.2);
		}
		if (remaining < 30.0f)
		{
			out = target;
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("STOPS-B1 S-1991 retail: the Isengard Bloodthirsty button selects an ally target and sacrifices it")
{
	if (!hudtest::haveWorld("S-1991 Bloodthirsty"))
	{
		return;
	}
	factb::HudGame g(hudtest::shared(), "FactionIsengard");
	g.logic().combat().setAutoAcquireEnabled(false);

	const Coord3D home = g.home();
	const Coord3D sourceAt = g.rig.freeSpot(home.x, home.y, 350.0f);
	Object *source = g.rig.make("IsengardFighterHorde", sourceAt.x, sourceAt.y);
	REQUIRE(source);
	Coord3D victimAt{};
	REQUIRE(reachableSpot(g.rig, source, 220.0f, victimAt));
	// The retail SacrificeFilter is ALL: this is deliberately an allied horde, so the
	// award assertion below cannot be satisfied by ordinary enemy-kill credit.
	Object *victim = g.rig.make("MordorFighterHorde", victimAt.x, victimAt.y);
	REQUIRE(victim);
	const ObjectID sourceID = source->getID();
	const ObjectID victimID = victim->getID();
	REQUIRE(source->getExperienceTracker());
	g.step(20);
	source = g.logic().findObjectByID(source->getID());
	victim = g.logic().findObjectByID(victim->getID());
	REQUIRE(source);
	REQUIRE(victim);
	g.step(5);
	REQUIRE(source->getContain());
	// tolerance: the retail horde's member count is data-defined; this path needs an existing source member.
	REQUIRE(source->getContain()->getContainCount() > 0);
	REQUIRE(victim->getContain());
	// tolerance: the retail horde's member count is data-defined; this path needs an existing target member.
	REQUIRE(!victim->getContain()->getContainedItemsList()->empty());
	Object *victimMember = victim->getContain()->getContainedItemsList()->front();
	REQUIRE(victimMember);
	HordeContain *sourceContain = dynamic_cast<HordeContain *>(source->getContain());
	REQUIRE(sourceContain);
	Object *acceptedMember = g.logic().newObject(g.logic().things().findTemplate("IsengardFighter"), source->getTeam(), {});
	REQUIRE(acceptedMember);
	const Coord3D acceptedPosition = *source->getPosition();
	acceptedMember->setPosition(&acceptedPosition);
	testutil::completeHorde(acceptedMember); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
	REQUIRE(sourceContain->acceptCreatedMember(acceptedMember));
	CHECK(acceptedMember->getProducerID() == sourceID);
	CHECK(acceptedMember->getContainedBy() == source);
	std::map<unsigned short, int> victimAwards;
	std::map<unsigned short, int> victimRoster;
	for (Object *member : *victim->getContain()->getContainedItemsList())
	{
		REQUIRE(member);
		REQUIRE(member->getProducerID() == victim->getID());
		const auto *memberTemplate = static_cast<const ThingTemplate *>(member->getTemplate());
		const unsigned short templateID = memberTemplate->getTemplateID();
		++victimRoster[templateID];
		ExperienceTracker *memberXp = member->getExperienceTracker();
		REQUIRE(memberXp);
		const int award = memberXp->getExperienceValue(*source, true);
		auto [it, inserted] = victimAwards.emplace(templateID, award);
		if (!inserted)
		{
			REQUIRE(it->second == award);
		}
	}
	for (Object *member : *source->getContain()->getContainedItemsList())
	{
		REQUIRE(member);
		CHECK(member->getProducerID() == source->getID());
	}
	AIUpdateInterface *sourceAI = source->getAIUpdateInterface();
	REQUIRE(sourceAI);
	ExperienceTracker *sourceXp = source->getExperienceTracker();
	REQUIRE(sourceXp);
	BloodthirstyUpdate *sourceBloodthirsty = BloodthirstyUpdate::find(*source);
	BloodthirstyUpdate *victimBloodthirsty = BloodthirstyUpdate::find(*victim);
	REQUIRE(sourceBloodthirsty);
	REQUIRE(victimBloodthirsty);
	CHECK(sourceBloodthirsty->canSacrifice(victimMember));
	REQUIRE(g.select(source));
	const std::vector<ObjectID> selected = g.rig.game->dispatch().selection(g.rig.local->getPlayerIndex());
	REQUIRE(!selected.empty());
	Object *firstSelected = g.logic().findObjectByID(selected.front());
	REQUIRE(firstSelected);
	CHECK(BloodthirstyUpdate::find(*firstSelected) == sourceBloodthirsty);
	CHECK(g.rig.game->dispatch().handlerLane(MSG_SACRIFICE) == "FACT-B");
	const float xpBefore = sourceXp->getExperience();
	const int victimsBefore = victim->getContain()->getContainCount();
	std::vector<GameMessage> sacrificeMessages;
	g.hud->input().setLogicMessageObserver([&](const GameMessage &message) {
		if (message.getType() == MSG_SACRIFICE)
		{
			sacrificeMessages.push_back(message);
		}
	});

	ControlBarButton button;
	REQUIRE(g.reveal(source,
					 [](const CommandButton &b) { return b.m_command == GUI_COMMAND_BLOODTHIRSTY; },
					 button));
	REQUIRE(button.button);
	REQUIRE(button.state != ButtonState::Hidden);
	CHECK(g.press(button));
	g.clickAt(*victimMember->getPosition());
	CHECK(g.lastMessages(12).find("MSG_SACRIFICE") != std::string::npos);
	REQUIRE(sacrificeMessages.size() == 1);
	CHECK(sacrificeMessages.front().getType() == MSG_SACRIFICE);
	REQUIRE(sacrificeMessages.front().getArgumentCount() == 1);
	const GameMessageArgument *targetArgument = sacrificeMessages.front().getArgument(0);
	REQUIRE(targetArgument);
	CHECK(targetArgument->type == ARGUMENTDATATYPE_OBJECTID);
	CHECK(targetArgument->objectID == victimID);
	g.step(1);
	CHECK(g.rig.game->dispatch().unhandled().count(MSG_SACRIFICE) == 0);
	CHECK(sourceBloodthirsty->bestTargetID() == victimID);
	CHECK(victimBloodthirsty->sacrificerID() == sourceID);
	CHECK(sourceAI->isAttacking());
	CHECK(sourceAI->currentVictimId() == victimID);
	CHECK(source->testStatus((unsigned)CombatNames::status("BLOODTHIRSTY")));
	CHECK(source->getContain()->getContainedItemsList()->front()->testStatus((unsigned)CombatNames::status("BLOODTHIRSTY")));
	const std::map<unsigned short, float> initialPools = sourceContain->experiencePools();
	int firstKillFrame = -1;
	int cancellationFrame = -1;
	bool victimConditionObserved = false;
	bool victimRemoved = false;
	int victimsAfterNaturalCombat = victimsBefore;
	std::map<unsigned short, int> remainingRoster;
	const int maxNaturalFrames = 1800;
	for (int frame = 1; frame <= maxNaturalFrames; ++frame)
	{
		g.step(1);
		source = g.logic().findObjectByID(sourceID);
		victim = g.logic().findObjectByID(victimID);
		REQUIRE(source);
		sourceBloodthirsty = BloodthirstyUpdate::find(*source);
		REQUIRE(sourceBloodthirsty);
		if (!victim)
		{
			victimRemoved = true;
			victimsAfterNaturalCombat = 0;
			remainingRoster.clear();
			cancellationFrame = frame;
			break;
		}
		victimBloodthirsty = BloodthirstyUpdate::find(*victim);
		REQUIRE(victimBloodthirsty);
		victimsAfterNaturalCombat = (int)victim->getContain()->getContainCount();
		remainingRoster.clear();
		for (Object *member : *victim->getContain()->getContainedItemsList())
		{
			REQUIRE(member);
			++remainingRoster[static_cast<const ThingTemplate *>(member->getTemplate())->getTemplateID()];
		}
		if (firstKillFrame < 0 && sourceBloodthirsty->kills() > 0)
		{
			firstKillFrame = frame;
		}
		if (sourceBloodthirsty->kills() > 0)
		{
			victimConditionObserved = victimConditionObserved || victim->testModelCondition(0x40);
		}
		if (sourceBloodthirsty->bestTargetID() == INVALID_ID)
		{
			cancellationFrame = frame;
			break;
		}
	}
	REQUIRE(firstKillFrame != -1);
	// tolerance: target removal or cancellation happens after the first natural kill; the exact frame is simulation-dependent.
	REQUIRE(cancellationFrame > firstKillFrame);
	CHECK(victimRemoved);
	// tolerance: natural combat may kill multiple members in a frame; XP below uses the actual per-template kill count.
	CHECK(victimsAfterNaturalCombat < victimsBefore);
	// RW 0x850EEC awards to the source horde, so its template keys the pool.
	const unsigned short sourceTemplateID = static_cast<const ThingTemplate *>(source->getTemplate())->getTemplateID();
	const auto initialPool = initialPools.find(sourceTemplateID);
	float expectedPool = initialPool == initialPools.end() ? 1.0f : initialPool->second;
	int killedCount = 0;
	for (const auto &[templateID, rosterCount] : victimRoster)
	{
		const int killed = rosterCount - remainingRoster[templateID];
		// tolerance: natural combat can remove any number of this template, but cannot grow the roster.
		REQUIRE(killed >= 0);
		killedCount += killed;
		if (killed == 0)
		{
			continue;
		}
		const float amount = static_cast<float>(victimAwards.at(templateID)) * 1.1f;
		for (int kill = 0; kill < killed; ++kill)
		{
			expectedPool += amount;
		}
	}
	CHECK(killedCount == (victimsBefore - victimsAfterNaturalCombat));
	// tolerance: native experience pools use float arithmetic and can accumulate a small rounding difference.
	CHECK(sourceXp->getExperience() == doctest::Approx(std::max(xpBefore, expectedPool)).epsilon(0.0001f));
	CHECK(victimConditionObserved);
	CHECK(sourceBloodthirsty->bestTargetID() == INVALID_ID);
	CHECK_FALSE(source->testStatus((unsigned)CombatNames::status("BLOODTHIRSTY")));
	if (victim)
	{
		CHECK(victimBloodthirsty->sacrificerID() == INVALID_ID);
		CHECK_FALSE(victim->testStatus((unsigned)CombatNames::status("IGNORE_AI_COMMAND")));
		CHECK_FALSE(victim->testModelCondition(0x40));
		for (Object *member : *victim->getContain()->getContainedItemsList())
		{
			CHECK_FALSE(member->testModelCondition(0x40));
		}
	}
}

TEST_CASE("STOPS-B1 S-1991 retail: invalid targets, stop release and initiate voice")
{
	if (!hudtest::haveWorld("S-1991 Bloodthirsty lifecycle"))
	{
		return;
	}
	factb::HudGame g(hudtest::shared(), "FactionIsengard");
	g.logic().combat().setAutoAcquireEnabled(false);
	REQUIRE(g.enemy);

	const Coord3D home = g.home();
	const Coord3D sourceAt = g.rig.freeSpot(home.x, home.y, 350.0f);
	const Coord3D victimAt = g.rig.freeSpot(sourceAt.x + 220.0f, sourceAt.y, 220.0f);
	Object *source = g.rig.make("IsengardFighterHorde", sourceAt.x, sourceAt.y);
	Object *victim = g.rig.make("MordorFighterHorde", victimAt.x, victimAt.y, g.enemy);
	REQUIRE(source);
	REQUIRE(victim);
	REQUIRE(source->getExperienceTracker());
	source->getExperienceTracker()->gainExpForLevel(10, false);
	g.step(20);
	source = g.logic().findObjectByID(source->getID());
	victim = g.logic().findObjectByID(victim->getID());
	REQUIRE(source);
	REQUIRE(victim);
	g.step(5);
	REQUIRE(source->getContain());
	REQUIRE(victim->getContain());
	// tolerance: the retail horde's member count is data-defined; this path needs an existing source member.
	REQUIRE(source->getContain()->getContainCount() > 0);
	// tolerance: the retail horde's member count is data-defined; this path needs an existing target member.
	REQUIRE(!victim->getContain()->getContainedItemsList()->empty());
	Object *sourceMember = source->getContain()->getContainedItemsList()->front();
	Object *victimMember = victim->getContain()->getContainedItemsList()->front();
	REQUIRE(sourceMember);
	REQUIRE(victimMember);
	BloodthirstyUpdate *sourceBloodthirsty = BloodthirstyUpdate::find(*source);
	BloodthirstyUpdate *victimBloodthirsty = BloodthirstyUpdate::find(*victim);
	AIUpdateInterface *sourceAI = source->getAIUpdateInterface();
	AIUpdateInterface *victimAI = victim->getAIUpdateInterface();
	REQUIRE(sourceBloodthirsty);
	REQUIRE(victimBloodthirsty);
	REQUIRE(sourceAI);
	REQUIRE(victimAI);
	REQUIRE_FALSE(sourceBloodthirsty->initiateVoice().empty());

	REQUIRE(g.select(source));
	ControlBarButton button;
	REQUIRE(g.reveal(source,
					 [](const CommandButton &b) { return b.m_command == GUI_COMMAND_BLOODTHIRSTY; },
					 button));
	REQUIRE(g.press(button));
	auto sacrificeCount = [&]() {
		const auto &log = g.hud->input().stream().log();
		return std::count_if(log.begin(), log.end(), [](const std::string &line) { return line.find("MSG_SACRIFICE") != std::string::npos; });
	};
	size_t messagesBefore = sacrificeCount();
	g.clickAt(*sourceMember->getPosition());
	CHECK(sacrificeCount() == messagesBefore);
	CHECK(sourceBloodthirsty->bestTargetID() == INVALID_ID);
	CHECK(victimBloodthirsty->sacrificerID() == INVALID_ID);

	victimAI->aiMoveToPosition(Coord3D{ victimAt.x + 200.0f, victimAt.y, victimAt.z }, CMD_FROM_AI);
	CHECK_FALSE(victimAI->isIdle());
	CHECK_FALSE(sourceBloodthirsty->canSacrifice(victimMember));
	messagesBefore = sacrificeCount();
	g.clickAt(*victimMember->getPosition());
	CHECK(sacrificeCount() == messagesBefore);

	BloodthirstyVoiceSink sink;
	UnitVoiceResponse voice(g.logic(), sink);
	voice.setLocalPlayerIndex(g.rig.local->getPlayerIndex());
	CHECK(voice.pickAndPlay({ source->getID() }, UnitVoiceResponse::VOICE_EVENT_BLOODTHIRSTY));
	// tolerance: retail voice configuration can resolve to EVA or a sound cue; either sink proves the event was decoded.
	CHECK(sink.sounds.size() + sink.eva.size() > 0);
	Object *plain = g.rig.make("MordorFighter", sourceAt.x + 80.0f, sourceAt.y + 80.0f);
	REQUIRE(plain);
	REQUIRE_FALSE(BloodthirstyUpdate::find(*plain));
	REQUIRE_FALSE(plain->getHordeObject(false));
	sink.sounds.clear();
	sink.eva.clear();
	CHECK_FALSE(voice.pickAndPlay({ plain->getID() }, UnitVoiceResponse::VOICE_EVENT_BLOODTHIRSTY));
	CHECK(sink.sounds.empty());
	CHECK(sink.eva.empty());

	victimAI->aiIdle(CMD_FROM_AI);
	REQUIRE(victimAI->isIdle());
	sourceBloodthirsty->sacrificeHorde(victimMember, CMD_FROM_PLAYER);
	CHECK(sourceBloodthirsty->bestTargetID() == victim->getID());
	CHECK(victimBloodthirsty->sacrificerID() == source->getID());
	// lane DEVPORT-7 (SAVE-4b): the sacrifice's state is in the snapshot (BloodthirstyUpdate::xfer, the crc's fields)
	XferSave savedSource, savedVictim;
	sourceBloodthirsty->xfer(savedSource);
	victimBloodthirsty->xfer(savedVictim);
	sourceAI->aiIdle(CMD_FROM_PLAYER);
	CHECK(sourceBloodthirsty->bestTargetID() == INVALID_ID);
	CHECK(victimBloodthirsty->sacrificerID() == INVALID_ID);
	{
		XferLoad ls(savedSource.data()), lv(savedVictim.data());
		sourceBloodthirsty->xfer(ls);
		victimBloodthirsty->xfer(lv);
	}
	CHECK(sourceBloodthirsty->bestTargetID() == victim->getID());
	CHECK(victimBloodthirsty->sacrificerID() == source->getID());
	std::vector<std::uint8_t> truncated = savedSource.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(sourceBloodthirsty->xfer(lt), XferException);
	sourceBloodthirsty->cancelSacrifice();
	CHECK(sourceBloodthirsty->bestTargetID() == INVALID_ID);
	CHECK(victimBloodthirsty->sacrificerID() == INVALID_ID);
	CHECK_FALSE(source->testStatus((unsigned)CombatNames::status("BLOODTHIRSTY")));
	CHECK_FALSE(victim->testStatus((unsigned)CombatNames::status("IGNORE_AI_COMMAND")));
}

TEST_CASE("STOPS-B1 S-1991 retail: MSG_SACRIFICE round-trips through two deterministic peer batches")
{
	if (!hudtest::haveWorld("S-1991 Bloodthirsty network"))
	{
		return;
	}
	std::vector<std::uint32_t> senderHashes;
	GameMessage sacrifice(MSG_SACRIFICE, 0);
	ObjectID senderSourceID = INVALID_ID;
	ObjectID senderVictimID = INVALID_ID;
	{
		factb::HudGame sender(hudtest::shared(), "FactionIsengard");
		sender.logic().combat().setAutoAcquireEnabled(false);
		const Coord3D home = sender.home();
		const Coord3D sourceAt = sender.rig.freeSpot(home.x, home.y, 350.0f);
		Object *source = sender.rig.make("IsengardFighterHorde", sourceAt.x, sourceAt.y);
		REQUIRE(source);
		Coord3D victimAt{};
		REQUIRE(reachableSpot(sender.rig, source, 220.0f, victimAt));
		Object *victim = sender.rig.make("MordorFighterHorde", victimAt.x, victimAt.y);
		REQUIRE(victim);
		senderSourceID = source->getID();
		senderVictimID = victim->getID();
		sender.step(25);
		source = sender.logic().findObjectByID(senderSourceID);
		victim = sender.logic().findObjectByID(senderVictimID);
		REQUIRE(source);
		REQUIRE(victim);
		REQUIRE(victim->getContain());
		REQUIRE(!victim->getContain()->getContainedItemsList()->empty());
		REQUIRE(sender.select(source));
		ControlBarButton button;
		REQUIRE(sender.reveal(source, [](const CommandButton &b) { return b.m_command == GUI_COMMAND_BLOODTHIRSTY; }, button));
		REQUIRE(sender.press(button));
		sender.clickAt(*victim->getContain()->getContainedItemsList()->front()->getPosition());
		const auto &messages = sender.rig.game->commands().messages();
		for (const GameMessage &message : messages)
		{
			if (message.getType() == MSG_SACRIFICE)
			{
				sacrifice = message;
			}
		}
		REQUIRE(sacrifice.getType() == MSG_SACRIFICE);
		NetByteWriter writer;
		std::string error;
		REQUIRE_MESSAGE(NetPacket::writeGameMessage(writer, sacrifice, &error), error);
		NetByteReader reader(writer.data());
		std::unique_ptr<GameMessage> decoded = NetPacket::readGameMessage(reader, &error);
		REQUIRE_MESSAGE(decoded, error);
		REQUIRE(reader.atEnd());
		CHECK(decoded->getType() == MSG_SACRIFICE);
		REQUIRE(decoded->getArgumentCount() == 1);
		CHECK(decoded->getArgument(0)->objectID == senderVictimID);
		sacrifice = *decoded;
		sender.step(1);
		senderHashes.push_back(sender.logic().computeStateHash());
		source = sender.logic().findObjectByID(senderSourceID);
		REQUIRE(source);
		BloodthirstyUpdate *senderBloodthirsty = BloodthirstyUpdate::find(*source);
		REQUIRE(senderBloodthirsty);
		const int senderVictimsBefore = (int)victim->getContain()->getContainCount();
		bool senderProgressed = false;
		bool senderRemoved = false;
		for (int frame = 0; frame < 1800; ++frame)
		{
			sender.step(1);
			senderHashes.push_back(sender.logic().computeStateHash());
			victim = sender.logic().findObjectByID(senderVictimID);
			if (!victim)
			{
				senderRemoved = true;
				break;
			}
			REQUIRE(victim->getContain());
			senderProgressed = senderProgressed || (int)victim->getContain()->getContainCount() < senderVictimsBefore;
		}
		REQUIRE(senderRemoved);
		CHECK(senderProgressed);
		CHECK(senderBloodthirsty->bestTargetID() == INVALID_ID);
		CHECK_FALSE(source->testStatus((unsigned)CombatNames::status("BLOODTHIRSTY")));
		sender.step(1);
		senderHashes.push_back(sender.logic().computeStateHash());
	}

	factb::HudGame receiver(hudtest::shared(), "FactionIsengard");
	receiver.logic().combat().setAutoAcquireEnabled(false);
	const Coord3D home = receiver.home();
	const Coord3D sourceAt = receiver.rig.freeSpot(home.x, home.y, 350.0f);
	Object *source = receiver.rig.make("IsengardFighterHorde", sourceAt.x, sourceAt.y);
	REQUIRE(source);
	Coord3D victimAt{};
	REQUIRE(reachableSpot(receiver.rig, source, 220.0f, victimAt));
	Object *victim = receiver.rig.make("MordorFighterHorde", victimAt.x, victimAt.y);
	REQUIRE(victim);
	CHECK(source->getID() == senderSourceID);
	CHECK(victim->getID() == senderVictimID);
	receiver.step(25);
	source = receiver.logic().findObjectByID(senderSourceID);
	victim = receiver.logic().findObjectByID(senderVictimID);
	REQUIRE(source);
	REQUIRE(victim);
	REQUIRE(victim->getContain());
	const int receiverVictimsBefore = (int)victim->getContain()->getContainCount();
	bool receiverProgressed = false;
	bool receiverRemoved = false;
	REQUIRE(receiver.select(source));
	receiver.rig.game->commands().append(sacrifice);
	for (size_t i = 0; i < senderHashes.size(); ++i)
	{
		receiver.step(1);
		CHECK(receiver.logic().computeStateHash() == senderHashes[i]);
		victim = receiver.logic().findObjectByID(senderVictimID);
		if (!victim)
		{
			receiverRemoved = true;
			continue;
		}
		REQUIRE(victim->getContain());
		receiverProgressed = receiverProgressed || (int)victim->getContain()->getContainCount() < receiverVictimsBefore;
	}
	CHECK(receiverProgressed);
	CHECK(receiverRemoved);
	source = receiver.logic().findObjectByID(senderSourceID);
	REQUIRE(source);
	BloodthirstyUpdate *receiverBloodthirsty = BloodthirstyUpdate::find(*source);
	REQUIRE(receiverBloodthirsty);
	CHECK(receiverBloodthirsty->bestTargetID() == INVALID_ID);
	CHECK_FALSE(source->testStatus((unsigned)CombatNames::status("BLOODTHIRSTY")));
	CHECK(receiver.logic().computeStateHash() == senderHashes.back());
}

// ---- review DEVPORT-7e (Sol #2 .. #4) ----
namespace
{
struct SacrificePair
{
	Object *source = nullptr;
	Object *victim = nullptr;
};

// an IsengardFighterHorde of the local player and a MordorFighterHorde of `victimOwner` (null: the local player) it can walk to
SacrificePair makePair(factb::HudGame &g, Player *victimOwner)
{
	const Coord3D home = g.home();
	const Coord3D sourceAt = g.rig.freeSpot(home.x, home.y, 350.0f);
	SacrificePair p;
	p.source = g.rig.make("IsengardFighterHorde", sourceAt.x, sourceAt.y);
	REQUIRE(p.source);
	Coord3D victimAt{};
	REQUIRE(reachableSpot(g.rig, p.source, 220.0f, victimAt));
	p.victim = g.rig.make("MordorFighterHorde", victimAt.x, victimAt.y, victimOwner);
	REQUIRE(p.victim);
	const ObjectID s = p.source->getID(), v = p.victim->getID();
	g.step(25);
	p.source = g.logic().findObjectByID(s);
	p.victim = g.logic().findObjectByID(v);
	REQUIRE(p.source);
	REQUIRE(p.victim);
	REQUIRE(p.victim->getContain());
	// tolerance: the retail horde's member count is data-defined; the tests need a member to click / name
	REQUIRE(!p.victim->getContain()->getContainedItemsList()->empty());
	return p;
}

Object *frontMember(Object *horde)
{
	return horde->getContain()->getContainedItemsList()->front();
}

// nothing of a sacrifice exists between `source` and `target`'s horde: no reservation on either module, no BLOODTHIRSTY, no IGNORE_AI_COMMAND, no attack
void checkNoSacrifice(factb::HudGame &g, Object *source, Object *target)
{
	BloodthirstyUpdate *s = BloodthirstyUpdate::find(*source);
	REQUIRE(s);
	CHECK(s->bestTargetID() == INVALID_ID);
	CHECK_FALSE(source->testStatus((unsigned)CombatNames::status("BLOODTHIRSTY")));
	CHECK_FALSE(source->getAIUpdateInterface()->isAttacking());
	if (Object *horde = target->getHordeObject(false))
	{
		if (BloodthirstyUpdate *t = BloodthirstyUpdate::find(*horde))
		{
			CHECK(t->sacrificerID() == INVALID_ID);
		}
		CHECK_FALSE(horde->testStatus((unsigned)CombatNames::status("IGNORE_AI_COMMAND")));
	}
	(void)g;
}

GameMessage throughCodec(const GameMessage &m, int playerIndex)
{
	NetByteWriter writer;
	std::string error;
	REQUIRE_MESSAGE(NetPacket::writeGameMessage(writer, m, &error), error);
	NetByteReader reader(writer.data());
	std::unique_ptr<GameMessage> decoded = NetPacket::readGameMessage(reader, &error);
	REQUIRE_MESSAGE(decoded, error);
	REQUIRE(reader.atEnd());
	decoded->friend_setPlayerIndex(playerIndex); // Network::file: the sender's slot's player
	return *decoded;
}
} // namespace

// RW 0x81E60B: a context command's relationship bits (retail's Command_*BloodThirstySacrifice: NEED_TARGET_ALLY_OBJECT) drop the target before the action
// check. An idle ENEMY horde passes canSacrifice (SacrificeFilter ALL), so only the relationship gate refuses the click: no MSG_SACRIFICE, nothing reserved
TEST_CASE("STOPS-B1 S-1991 retail review: the Bloodthirsty button's NEED_TARGET_ALLY_OBJECT refuses an idle enemy horde at the click (RW 0x81E60B)")
{
	if (!hudtest::haveWorld("S-1991 Bloodthirsty relationship"))
	{
		return;
	}
	factb::HudGame g(hudtest::shared(), "FactionIsengard");
	g.logic().combat().setAutoAcquireEnabled(false);
	REQUIRE(g.enemy);
	SacrificePair p = makePair(g, g.enemy);
	Object *enemyMember = frontMember(p.victim);
	BloodthirstyUpdate *sourceBloodthirsty = BloodthirstyUpdate::find(*p.source);
	REQUIRE(sourceBloodthirsty);
	REQUIRE(g.rig.local->getRelationship(p.victim->getTeam()) == ENEMIES);
	REQUIRE(p.victim->getAIUpdateInterface()->isIdle());
	REQUIRE(sourceBloodthirsty->canSacrifice(enemyMember)); // the precondition: without the relationship gate this click would be a sacrifice
	const CommandButton *button = BloodthirstyUpdate::sacrificeButton(*p.source);
	REQUIRE(button);
	CHECK(button->m_options == (COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT | COMMAND_OPTION_CONTEXTMODE_COMMAND)); // retail commandbutton.ini

	REQUIRE(g.select(p.source));
	ControlBarButton bar;
	REQUIRE(g.reveal(p.source, [](const CommandButton &b) { return b.m_command == GUI_COMMAND_BLOODTHIRSTY; }, bar));
	REQUIRE(g.press(bar));
	std::vector<GameMessage> sacrifices;
	g.hud->input().setLogicMessageObserver([&](const GameMessage &message) {
		if (message.getType() == MSG_SACRIFICE)
		{
			sacrifices.push_back(message);
		}
	});
	g.clickAt(*enemyMember->getPosition());
	g.step(2);
	CHECK(sacrifices.empty());
	checkNoSacrifice(g, p.source, p.victim);
}

// a forged MSG_SACRIFICE (through the packet codec, stamped with the sender's player as the network does) is re-tested at dispatch against everything
// the client tests: each refused target leaves no trace of a sacrifice; the allied idle horde, the one target the UI accepts, is then reserved (control)
TEST_CASE("STOPS-B1 S-1991 retail review: a forged MSG_SACRIFICE naming an enemy, the own, a busy, a reserved or a non-horde target is refused at dispatch")
{
	if (!hudtest::haveWorld("S-1991 Bloodthirsty forged"))
	{
		return;
	}
	factb::HudGame g(hudtest::shared(), "FactionIsengard");
	g.logic().combat().setAutoAcquireEnabled(false);
	REQUIRE(g.enemy);
	SacrificePair ally = makePair(g, nullptr);
	const Coord3D at = *ally.victim->getPosition();
	const Coord3D enemyAt = g.rig.freeSpot(at.x + 120.0f, at.y, 200.0f);
	Object *enemy = g.rig.make("MordorFighterHorde", enemyAt.x, enemyAt.y, g.enemy);
	const Coord3D busyAt = g.rig.freeSpot(at.x - 120.0f, at.y, 200.0f);
	Object *busy = g.rig.make("MordorFighterHorde", busyAt.x, busyAt.y);
	const Coord3D reservedAt = g.rig.freeSpot(at.x, at.y + 120.0f, 200.0f);
	Object *reserved = g.rig.make("MordorFighterHorde", reservedAt.x, reservedAt.y);
	const Coord3D otherAt = g.rig.freeSpot(at.x, at.y - 160.0f, 200.0f);
	Object *other = g.rig.make("IsengardFighterHorde", otherAt.x, otherAt.y);
	Object *plain = g.rig.make("MordorFighter", at.x + 60.0f, at.y + 60.0f);
	REQUIRE(enemy);
	REQUIRE(busy);
	REQUIRE(reserved);
	REQUIRE(other);
	REQUIRE(plain);
	g.step(25);
	for (Object *h : { enemy, busy, reserved, other })
	{
		REQUIRE(h->getContain());
		REQUIRE(!h->getContain()->getContainedItemsList()->empty());
	}
	REQUIRE_FALSE(plain->getHordeObject(false));
	// `reserved` is the target of another Bloodthirsty horde of the same player
	BloodthirstyUpdate *otherBloodthirsty = BloodthirstyUpdate::find(*other);
	REQUIRE(otherBloodthirsty);
	otherBloodthirsty->sacrificeHorde(frontMember(reserved), CMD_FROM_PLAYER);
	REQUIRE(BloodthirstyUpdate::find(*reserved)->sacrificerID() == other->getID());
	busy->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ busyAt.x - 300.0f, busyAt.y, busyAt.z }, CMD_FROM_AI);
	REQUIRE(g.rig.local->getRelationship(enemy->getTeam()) == ENEMIES);
	REQUIRE(g.select(ally.source));
	const int issuer = g.rig.local->getPlayerIndex();
	// `clean`: the horde that must carry no victim state afterwards (the reserved horde keeps the other sacrifice's, checked below)
	struct Case
	{
		std::string name;
		Object *target;
		Object *clean;
	};
	const Case cases[] = { { "enemy horde", frontMember(enemy), enemy }, { "the source's own horde", frontMember(ally.source), ally.source },
		{ "a moving allied horde", frontMember(busy), busy }, { "a horde another sacrificer reserved", frontMember(reserved), ally.victim },
		{ "a unit outside any horde", plain, ally.victim } };
	for (const Case &c : cases)
	{
		CAPTURE(c.name);
		REQUIRE_FALSE(busy->getAIUpdateInterface()->isIdle());
		GameMessage forged(MSG_SACRIFICE, issuer);
		forged.appendObjectIDArgument(c.target->getID());
		g.rig.game->commands().append(throughCodec(forged, issuer));
		g.step(2);
		CHECK(g.rig.game->dispatch().unhandled().count(MSG_SACRIFICE) == 0);
		checkNoSacrifice(g, ally.source, c.clean);
	}
	CHECK(BloodthirstyUpdate::find(*reserved)->sacrificerID() == other->getID()); // the other reservation is untouched
	GameMessage good(MSG_SACRIFICE, issuer);
	good.appendObjectIDArgument(frontMember(ally.victim)->getID());
	g.rig.game->commands().append(throughCodec(good, issuer));
	g.step(2);
	CHECK(BloodthirstyUpdate::find(*ally.source)->bestTargetID() == ally.victim->getID());
	CHECK(BloodthirstyUpdate::find(*ally.victim)->sacrificerID() == ally.source->getID());
	CHECK(ally.victim->testStatus((unsigned)CombatNames::status("IGNORE_AI_COMMAND")));
}

// RotWK: the constructor RW 0x850D74 wakes the module at once, its phase (RW 0x64DFDA) is 2 = PHASE_NORMAL, update RW 0x85123F sleeps 2 s + 1 frame
// (11 frames) and the disabled types it still processes (RW 0x8B2A30 -> 0x850F76(mask, 3, 4)) are DISABLED_HELD and DISABLED_PARALYZED. A held or
// paralyzed victim horde whose sacrificer gave up therefore clears its victim state on its next wake and sleeps 11 frames again; a horde disabled by a
// type outside that mask (DISABLED_SCRIPT_DISABLED) is skipped and re-tested every frame, keeping its victim state (the scheduler RW 0x62E4E8)
TEST_CASE("STOPS-B1 S-1991 retail review: BloodthirstyUpdate's phase, 11-frame sleep and HELD / PARALYZED processing mask (RW 0x85123F / 0x8B2A30)")
{
	if (!hudtest::haveWorld("S-1991 Bloodthirsty schedule"))
	{
		return;
	}
	for (const unsigned type : { (unsigned)DISABLED_HELD, (unsigned)DISABLED_PARALYZED, (unsigned)DISABLED_SCRIPT_DISABLED })
	{
		CAPTURE(type);
		const bool processed = type != (unsigned)DISABLED_SCRIPT_DISABLED;
		factb::HudGame g(hudtest::shared(), "FactionIsengard");
		g.logic().combat().setAutoAcquireEnabled(false);
		SacrificePair p = makePair(g, nullptr);
		BloodthirstyUpdate *source = BloodthirstyUpdate::find(*p.source);
		BloodthirstyUpdate *victim = BloodthirstyUpdate::find(*p.victim);
		REQUIRE(source);
		REQUIRE(victim);
		CHECK(victim->friend_getPhaseInLogic() == (int)PHASE_NORMAL);
		CHECK(source->friend_getPhaseInLogic() == (int)PHASE_NORMAL);
		// the undisturbed rhythm: successive wakes are 11 frames apart
		auto nextWake = [&](BloodthirstyUpdate *m) {
			const UnsignedInt due = m->friend_getNextCallFrame();
			for (int i = 0; i < 12 && m->friend_getNextCallFrame() == due; ++i)
			{
				g.step(1);
			}
			return std::make_pair(due, m->friend_getNextCallFrame());
		};
		const auto [due0, after0] = nextWake(victim);
		CHECK(after0 == due0 + 11u);
		source->sacrificeHorde(frontMember(p.victim), CMD_FROM_PLAYER);
		REQUIRE(victim->sacrificerID() == p.source->getID());
		REQUIRE(p.victim->testStatus((unsigned)CombatNames::status("IGNORE_AI_COMMAND")));
		p.victim->setDisabled(type, g.logic().getFrame() + 1000u);
		REQUIRE(p.victim->isDisabledByType(type));
		p.source->setStatusAcrossHorde((unsigned)CombatNames::status("BLOODTHIRSTY"), false); // the sacrificer gave up: the victim's next update clears it
		const auto [due1, after1] = nextWake(victim);
		if (processed)
		{
			CHECK(after1 == due1 + 11u);
			CHECK(victim->sacrificerID() == INVALID_ID);
			CHECK_FALSE(p.victim->testStatus((unsigned)CombatNames::status("IGNORE_AI_COMMAND")));
		}
		else
		{
			CHECK(after1 == due1 + 1u); // skipped: re-tested the next frame
			CHECK(victim->sacrificerID() == p.source->getID());
			CHECK(p.victim->testStatus((unsigned)CombatNames::status("IGNORE_AI_COMMAND")));
		}
		CHECK(p.victim->isDisabledByType(type)); // still disabled while it ran
	}
}

// a whole saved game resumes a sacrifice in flight with kills: the snapshot is taken after the first kills, the game plays on until more kills land, then
// the snapshot is loaded into the same game (LiveGame::loadSnapshot) and the same frames are replayed: the kill counter is the saved one (not the later
// one) and every replayed frame hashes as the first run did
TEST_CASE("STOPS-B1 S-1991 retail review: a saved game with a sacrifice in flight and nonzero kills resumes to the same frames")
{
	if (!hudtest::haveWorld("S-1991 Bloodthirsty save resume"))
	{
		return;
	}
	factb::HudGame g(hudtest::shared(), "FactionIsengard");
	g.logic().combat().setAutoAcquireEnabled(false);
	SacrificePair p = makePair(g, nullptr);
	const ObjectID sourceID = p.source->getID(), victimID = p.victim->getID();
	REQUIRE(g.select(p.source));
	ControlBarButton bar;
	REQUIRE(g.reveal(p.source, [](const CommandButton &b) { return b.m_command == GUI_COMMAND_BLOODTHIRSTY; }, bar));
	REQUIRE(g.press(bar));
	g.clickAt(*frontMember(p.victim)->getPosition());
	auto kills = [&]() -> unsigned {
		Object *s = g.logic().findObjectByID(sourceID);
		REQUIRE(s);
		BloodthirstyUpdate *m = BloodthirstyUpdate::find(*s);
		REQUIRE(m);
		return m->kills();
	};
	auto inFlight = [&]() {
		Object *s = g.logic().findObjectByID(sourceID);
		Object *v = g.logic().findObjectByID(victimID);
		return s && v && BloodthirstyUpdate::find(*s)->bestTargetID() == victimID && BloodthirstyUpdate::find(*v)->sacrificerID() == sourceID;
	};
	g.step(1);
	REQUIRE(inFlight());
	for (int i = 0; i < 1800 && kills() == 0; ++i)
	{
		g.step(1);
	}
	// tolerance: natural combat decides how many members fall on the first kill frame; the test needs at least one kill saved
	REQUIRE(kills() > 0);
	REQUIRE(inFlight());
	const unsigned savedKills = kills();
	std::vector<std::uint8_t> snapshot;
	std::string error;
	REQUIRE_MESSAGE(g.logic().saveSnapshot(snapshot, &error), error);
	const UnsignedInt savedFrame = g.logic().getFrame();
	std::vector<std::uint32_t> hashes;
	std::vector<unsigned> killTrace;
	for (int i = 0; i < 1800 && (kills() == savedKills || hashes.size() < 20); ++i)
	{
		g.step(1);
		hashes.push_back(g.logic().computeStateHash());
		killTrace.push_back(inFlight() ? kills() : 0u);
	}
	// tolerance: natural combat decides how many more kills land; the counter check needs the live counter past the saved one
	REQUIRE(kills() > savedKills); // the precondition of the counter check: the live counter moved past the saved one
	REQUIRE_MESSAGE(g.rig.game->loadSnapshot(snapshot, &error), error);
	CHECK(g.logic().getFrame() == savedFrame);
	REQUIRE(inFlight());
	CHECK(kills() == savedKills);
	for (size_t i = 0; i < hashes.size(); ++i)
	{
		g.step(1);
		CHECK(g.logic().computeStateHash() == hashes[i]);
		CHECK((inFlight() ? kills() : 0u) == killTrace[i]);
	}
}
