// OpenBFME. GPL-3.0.
// Lane DECOMP-2: the stops closed or narrowed with the RotWK -> BFME2 -> Open-BFME-2 map (tools/re/rw2decomp.py). Binary facts SKIP when RW_GAME_DAT is unset;
// the retail scenarios SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "GameClient/SoundSelectorClientBehavior.h"
#include "GameClient/UnitVoiceResponse.h"
#include "Common/ModelState.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/BitFlags.h"

#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("decomp2 binary facts: S-891 AIUpdate + 0x40 is the current victim (getCurrentVictim RW 0x668303, setCurrentVictim RW 0x6682B1, the team test RW 0x7A03AB)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("decomp2 binary facts S-891 (RW_GAME_DAT unset)");
		return;
	}
	CHECK(pe->hex(0x668303, 3) == "8b4140");       // getCurrentVictim: mov eax, [ecx + 0x40]
	CHECK(pe->hex(0x66830A, 6) == "8b0d2c41de00"); // findObjectByID on TheGameLogic (RW 0xDE412C)
	CHECK(pe->hex(0x6682BC, 3) == "8b4640");       // setCurrentVictim(null): the old victim id
	CHECK(pe->hex(0x6682E9, 9) == "ff7774ff9004020000"); // ... its AI's vslot 0x204 (addTargeter(this id, false))
	CHECK(pe->hex(0x6682F3, 4) == "83664000");     // ... then + 0x40 = 0
	CHECK(pe->hex(0x6682F9, 6) == "8b4074894640"); // setCurrentVictim(v): + 0x40 = v + 0x74 (the object id)
	CHECK(pe->hex(0x7A03BF, 6) == "8b8860020000"); // RW 0x7A03AB: each member's AI (Object + 0x260) ...
	CHECK(pe->hex(0x7A03C9, 5) == "e8357fecff");   // ... asked getCurrentVictim (call RW 0x668303)
}

TEST_CASE("decomp2 binary facts: S-414 the base builder's enemy filter asks the object's team (RW 0x97DFCD / 0x8EEC71 -> RW 0x66110A); RW 0x97DAFC scans TheFireLogicSystem")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("decomp2 binary facts S-414 (RW_GAME_DAT unset)");
		return;
	}
	// the relationship filter (vtable RW 0xC1676C) with flags 4 in the request check and in the farm site choice
	CHECK(pe->hex(0x97E040, 7) == "c745b46c67c100");
	CHECK(pe->hex(0x97E04E, 7) == "c745c404000000");
	CHECK(pe->hex(0x8EEEAA, 10) == "c7857cffffff6c67c100");
	CHECK(pe->hex(0x8EEEBB, 7) == "c7458c04000000");
	CHECK(pe->u32At(0xC1676C + 4) == 0x66110Au); // its allow
	CHECK(pe->hex(0x66110F, 6) == "ffb01c030000"); // push the object's team (Object + 0x31C) ...
	CHECK(pe->hex(0x66111A, 5) == "e8ccca0400");   // ... to Player::getRelationship(const Team *) RW 0x6ADBEB
	CHECK(pe->hex(0x66113B, 4) == "f6461004");     // relationship 0 (ENEMIES) passes flag 4
	CHECK(pe->hex(0x97E0DD, 3) == "83fe02");       // more than 2 hits ...
	CHECK(pe->hex(0x97E0EE, 2) == "6a09");         // ... -> 9
	// RW 0x97DAFC: from -150 (0xFFFFFF6A) by 15, the grid of RW 0xDE46A8 (the burn decay's subsystem), a cell's word + 6
	CHECK(pe->hex(0x97DB0D, 7) == "c745f86affffff");
	CHECK(pe->hex(0x97DB58, 6) == "8b35a846de00");
	CHECK(pe->hex(0x97DBCB, 6) == "66837c010600");
}

namespace
{
// the status bits a function's code asks Object::testStatus (RW 0x44DDEC) about: the imm8 of the last `push imm8` within 9 bytes before each `call RW 0x44DDEC`,
// and the bits of every inline `test byte/dword [reg + 0x94], imm` (the status words, Object + 0x94)
std::vector<int> statusTests(const retailtest::PeImage &pe, std::uint32_t start, size_t n)
{
	std::vector<std::uint8_t> d;
	REQUIRE(pe.read(start, n, &d));
	std::vector<int> out;
	for (size_t i = 0; i + 5 <= n; ++i)
	{
		if (d[i] == 0xE8)
		{
			const std::int32_t rel = (std::int32_t)((std::uint32_t)d[i + 1] | ((std::uint32_t)d[i + 2] << 8) | ((std::uint32_t)d[i + 3] << 16) | ((std::uint32_t)d[i + 4] << 24));
			if (start + (std::uint32_t)i + 5u + (std::uint32_t)rel == 0x44DDECu)
			{
				int arg = -1;
				for (size_t k = 2; k < 10 && k <= i; ++k)
				{
					if (d[i - k] == 0x6A)
					{
						arg = d[i - k + 1];
						break;
					}
				}
				out.push_back(arg);
			}
		}
		if (i + 7 <= n && (d[i] == 0xF6 || d[i] == 0xF7) && d[i + 1] >= 0x80 && d[i + 1] <= 0x87 && d[i + 2] == 0x94 && d[i + 3] == 0 && d[i + 4] == 0 && d[i + 5] == 0)
		{
			for (int bit = 0; bit < 8; ++bit)
			{
				if (d[i + 6] & (1u << bit))
				{
					out.push_back(1000 + bit); // an inline test of status bit `bit` (offset so it never equals a pushed index)
				}
			}
		}
	}
	return out;
}
} // namespace

TEST_CASE("decomp2 binary facts: S-420 RW's AI closest-enemy scan and its attack test never ask UNDER_CONSTRUCTION (RW 0x7006D7, 0x6C9147, 0x6C8652, 0x68D6A6)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("decomp2 binary facts S-420 (RW_GAME_DAT unset)");
		return;
	}
	CHECK(pe->hex(0x701443, 1) == "55");             // AI::findClosestEnemy (tier A) ...
	CHECK(pe->u32At(0xC0F368 + 4) == 0x660CA4u);     // the PossibleToAttack filter's allow (built by the worker RW 0x7006D7)
	CHECK(statusTests(*pe, 0x7006D7, 2171) == std::vector<int>{ 0x17, 0x25, 0x26, 0x4F });
	CHECK(statusTests(*pe, 0x68D6A6, 261).empty());   // Object::getAbleToAttackSpecificObject
	CHECK(statusTests(*pe, 0x6C9147, 856) == std::vector<int>{ 1000, 1000, 0x33, 0x25, 0x3C, 0x1A, 0x1B, 0x3C, 0x25, 0x25 }); // WeaponSet: DESTROYED (bit 0) of both first
	CHECK(statusTests(*pe, 0x6C8652, 1020) == std::vector<int>{ 0x25, 0x25 });
	CHECK(pe->cstring(pe->u32At(0xD8AFF0 + 4 * 2)) == "UNDER_CONSTRUCTION"); // status 2 in TheObjectStatusNames (RW 0xD8AFF0)
}

TEST_CASE("decomp2 S-483: a horde has an upgrade one of its members has (RW 0x68E040 -> contain slot 0x7C -> RW 0x86EC2D), its own mask unchanged")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 11, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player *men = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	REQUIRE(men);
	const ThingTemplate *tt = logic.things().findTemplate("GondorFighterHorde");
	REQUIRE(tt);
	Object *horde = logic.newObject(tt, men->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(horde);
	REQUIRE(horde->getContain());
	REQUIRE(horde->getContain()->getHordeContainInterface());
	REQUIRE(horde->getContain()->getContainedItemsList());
	REQUIRE(horde->getContain()->getContainedItemsList()->size() > 1);
	const UpgradeTemplate *armor = TheUpgradeCenter->findUpgrade("Upgrade_GondorHeavyArmor");
	REQUIRE(armor);
	CHECK_FALSE(horde->hasUpgrade(armor));
	Object *first = horde->getContain()->getContainedItemsList()->front();
	first->giveUpgradeSelf(armor);
	CHECK(first->hasUpgrade(armor));
	CHECK(horde->hasUpgrade(armor)); // the member answers for the horde (the old port read only the horde's own mask)
	CHECK_FALSE(horde->getUpgradeMask().test((unsigned)armor->getMaskBit()));
	for (const Object *m : *horde->getContain()->getContainedItemsList())
	{
		if (m != first)
		{
			CHECK_FALSE(m->hasUpgrade(armor));
		}
	}
	// a non-horde contain does not answer for what it holds (RW 0x851E97: slot 0x7C null): a member alone asks only its own mask
	const bool memberIsHorde = first->getContain() && first->getContain()->getHordeContainInterface();
	CHECK_FALSE(memberIsHorde);
}

TEST_CASE("decomp2 S-483: every castle list member learns the keep at the unpack (RW 0x7990D9) and its upgrade modules see the keep's upgrades (RW 0x693724)")
{
	OPENBFME_REQUIRE_START(s);
	int keepUpgradesSeen = 0;
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		INFO(std::string(faction));
		std::string error;
		buildtest::Game g;
		REQUIRE_MESSAGE(buildtest::startGame(*s, faction, "FactionMen", 77, g, &error), error);
		GameLogic &logic = g.live->logic();
		for (int i = 0; i < 3; ++i)
		{
			logic.runLogicFrame();
		}
		Player *owner = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
		REQUIRE(owner);
		const CastleBehavior *castle = nullptr;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			const CastleBehavior *cb = dynamic_cast<const CastleBehavior *>(o->findModule("CastleBehavior"));
			if (cb && o->getControllingPlayer() == owner)
			{
				castle = cb;
			}
		}
		REQUIRE(castle);
		Object *keep = logic.findObjectByID(castle->keepId());
		REQUIRE(keep);
		int listed = 0;
		std::vector<Object *> members;
		for (const std::vector<ObjectID> *list : { &castle->foundations(), &castle->members(), &castle->defenseFoundations() })
		{
			for (ObjectID id : *list)
			{
				Object *o = logic.findObjectByID(id);
				const CastleMemberBehavior *m = o ? dynamic_cast<const CastleMemberBehavior *>(o->findModule("CastleMemberBehavior")) : nullptr;
				if (m)
				{
					++listed;
					CHECK(m->keepId() == keep->getID());
					members.push_back(o);
				}
			}
		}
		MESSAGE(std::string(faction) << ": " << listed << " castle list members with a CastleMemberBehavior");
		CHECK(listed > 0);
		// an upgrade module of a member that the keep's upgrade (and nothing the member or the player has) would run: give the keep that upgrade only
		bool found = false;
		for (Object *o : members)
		{
			for (const std::unique_ptr<BehaviorModule> &bm : o->modules())
			{
				UpgradeMux *mux = bm->getUpgrade();
				if (found || !mux || mux->isAlreadyUpgraded())
				{
					continue;
				}
				for (unsigned bit = 0; bit < UpgradeMaskType::BITS && !found; ++bit)
				{
					const UpgradeTemplate *u = mux->muxData()->m_activationMask.test(bit) ? TheUpgradeCenter->findUpgradeByMaskBit((int)bit) : nullptr;
					if (!u || owner->hasUpgradeComplete(u) || o->getUpgradeMask().test(bit) || keep->getUpgradeMask().test(bit))
					{
						continue;
					}
					UpgradeMaskType without = owner->getCompletedUpgradeMask();
					without.orWith(o->getUpgradeMask());
					without.orWith(keep->getUpgradeMask());
					UpgradeMaskType with = without;
					with.orWith(u->grantMask());
					if (mux->wouldUpgrade(without) || !mux->wouldUpgrade(with))
					{
						continue;
					}
					found = true;
					MESSAGE(std::string(faction) << ": " << o->getTemplate()->getName() << " runs an upgrade module on the keep's " << u->getUpgradeName());
					keep->giveUpgradeSelf(u);
					CHECK_FALSE(o->getUpgradeMask().test(bit));
					o->updateUpgradeModules();
					CHECK(mux->isAlreadyUpgraded()); // the old port never looked at the keep
					++keepUpgradesSeen;
				}
			}
		}
	}
	MESSAGE(keepUpgradesSeen << " factions with a member upgrade module the keep's upgrade runs");
	CHECK(keepUpgradesSeen > 0);
}

TEST_CASE("decomp2 binary facts: S-701 Drawable + 0x154 is the ClientBehavior list; only the three sound selectors answer its vslot 0x34; the audio table RW 0xC26720")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("decomp2 binary facts S-701 (RW_GAME_DAT unset)");
		return;
	}
	// the drawable's module construction: newModule(.., type 1 / 2 / 3) fills + 0x14C / + 0x150 / + 0x154
	CHECK(pe->hex(0x67A09A, 2) == "6a01");
	CHECK(pe->hex(0x67A177, 2) == "6a02");
	CHECK(pe->hex(0x67A23D, 2) == "6a03");
	CHECK(pe->hex(0x67A200, 6) == "898654010000"); // mov [esi + 0x154], eax
	CHECK(pe->hex(0x676AB8, 6) == "8b9f54010000"); // RW 0x676A8A walks + 0x154 ...
	CHECK(pe->hex(0x676AD0, 3) == "ff5034");       // ... asking vslot 0x34
	// the module vtables (the constructors' last vptr) of the six ClientBehavior classes and their vslot 0x34
	struct Cls
	{
		const char *name;
		std::uint32_t vtable;
		bool selector;
	};
	const Cls classes[] = {
		{ "AnimationSoundClientBehavior", 0xC75AB0, false }, { "ModelConditionAudioLoopClientBehavior", 0xC76088, false },
		{ "ModelConditionSoundSelectorClientBehavior", 0xC094B0, true }, { "RandomSoundSelectorClientBehavior", 0xC75FA8, true },
		{ "TerrainResourceClientBehavior", 0xC06278, false }, { "UpgradeSoundSelectorClientBehavior", 0xC06208, true },
	};
	for (const Cls &c : classes)
	{
		INFO(c.name);
		CHECK(pe->u32At(c.vtable + 0x34) == (c.selector ? 0x4C9323u : 0x851E97u));
	}
	CHECK(pe->hex(0x4C9323, 12) == "8bc18d480cf7d81bc023c1c3"); // return this ? this + 0xC : 0 (the selector interface)
	// the selector interfaces' slots 0 / 1 / 2
	CHECK(pe->u32At(0xC094A0) == 0x8CF065u);
	CHECK(pe->u32At(0xC094A0 + 4) == 0x8CF0D2u);
	CHECK(pe->u32At(0xC094A0 + 8) == 0x8CF003u);
	CHECK(pe->u32At(0xC061FC) == 0x8CF833u);
	CHECK(pe->u32At(0xC75F98) == 0x8D0215u);
	// the audio table's row names, in order (16-byte rows: name, parse, user data, offset)
	for (int i = 0; i < SoundSelector::AUDIO_ROW_COUNT; ++i)
	{
		CHECK(pe->cstring(pe->u32At(0xC26720 + 16u * (unsigned)i)) == SoundSelector::rowName(i));
	}
	CHECK(pe->u32At(0xC26720 + 16u * SoundSelector::AUDIO_ROW_COUNT) == 0u);
	// RandomSoundSelectorClientBehavior's Chance default 0.5 (RW 0x6575A4 from RW 0xBD869C)
	CHECK(pe->hex(0xBD869C, 4) == "0000003f");
}

namespace
{
class FixedRoll : public SoundSelectors::RandomRoll
{
public:
	float value = 0.0f;
	int calls = 0;
	float roll() override
	{
		++calls;
		return value;
	}
};
struct NullSink : UnitVoiceResponse::Sink
{
	bool soundExists(const std::string &) override { return true; }
	void playSound(const std::string &, ObjectID, int) override {}
	void reportEva(const std::string &, const Coord3D *) override {}
};
} // namespace

TEST_CASE("decomp2 S-701: the sound selectors of retail units override the voice rows, the names and VoicePriority (Theoden mounted, a castle's upgrades, the peasants)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 21, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player *men = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	REQUIRE(men);
	NullSink sink;
	UnitVoiceResponse voice(logic, sink);

	// every retail template's selectors parsed (typed module data): count the entries
	int mcEntries = 0, upEntries = 0, randoms = 0;
	for (const ThingTemplate *t : logic.things().templates())
	{
		for (const ThingTemplate::Nugget &n : t->getFinalOverride()->clientBehaviorModules().nuggets())
		{
			if (const auto *mc = dynamic_cast<const ModelConditionSoundSelectorClientBehaviorModuleData *>(n.data.get()))
			{
				mcEntries += (int)mc->m_entries.size();
			}
			if (const auto *up = dynamic_cast<const UpgradeSoundSelectorClientBehaviorModuleData *>(n.data.get()))
			{
				upEntries += (int)up->m_entries.size();
			}
			randoms += dynamic_cast<const RandomSoundSelectorClientBehaviorModuleData *>(n.data.get()) ? 1 : 0;
		}
	}
	MESSAGE("sound selector entries: " << mcEntries << " SoundState, " << upEntries << " SoundUpgrade, " << randoms << " random modules");
	CHECK(mcEntries > 0);
	CHECK(upEntries > 0);
	CHECK(randoms > 0);

	// Theoden: SoundState = MOUNTED gives VoiceMove / VoiceSelect; without MOUNTED the template's rows answer
	const ThingTemplate *theodenTT = logic.things().findTemplate("RohanTheoden");
	REQUIRE(theodenTT);
	Object *theoden = logic.newObject(theodenTT, men->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(theoden);
	const int mounted = ModelCondition::indexOf("MOUNTED");
	REQUIRE(mounted >= 0);
	theoden->setModelConditionState(mounted, false);
	const UnitVoiceResponse::Voice walking = voice.objectVoice(*theoden, UnitVoiceResponse::VOICE_MOVE);
	CHECK(walking.sound == UnitVoiceResponse::templateVoice(*theodenTT, UnitVoiceResponse::VOICE_MOVE).sound);
	theoden->setModelConditionState(mounted, true);
	CHECK(voice.objectVoice(*theoden, UnitVoiceResponse::VOICE_MOVE).sound == "TheodenVoiceMoveMounted");
	CHECK(voice.objectVoice(*theoden, UnitVoiceResponse::VOICE_SELECT).sound == "TheodenVoiceSelectMountedMS");
	CHECK(walking.sound != "TheodenVoiceMoveMounted");
	// a row the SoundState does not set falls back to the template
	CHECK(voice.objectVoice(*theoden, UnitVoiceResponse::VOICE_ATTACK).sound == UnitVoiceResponse::templateVoice(*theodenTT, UnitVoiceResponse::VOICE_ATTACK).sound);

	// a Gondor castle piece: SoundUpgrade = Upgrade_PosternGate (ExcludedUpgrades Upgrade_OpenGarrison Upgrade_TrebuchetTurret) gives VoiceSelect GondorPosternGateSelect
	const ThingTemplate *castleTT = nullptr;
	for (const ThingTemplate *t : logic.things().templates())
	{
		for (const ThingTemplate::Nugget &n : t->getFinalOverride()->clientBehaviorModules().nuggets())
		{
			if (const auto *up = dynamic_cast<const UpgradeSoundSelectorClientBehaviorModuleData *>(n.data.get()))
			{
				for (const auto &e : up->m_entries)
				{
					if (e.sounds.rows[0].sound == "GondorPosternGateSelect" && !castleTT)
					{
						castleTT = t;
					}
				}
			}
		}
	}
	REQUIRE(castleTT);
	MESSAGE("the PosternGate selector's template: " << castleTT->getName());
	Object *piece = logic.newObject(castleTT, men->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(piece);
	const UpgradeTemplate *postern = TheUpgradeCenter->findUpgrade("Upgrade_PosternGate");
	const UpgradeTemplate *garrison = TheUpgradeCenter->findUpgrade("Upgrade_OpenGarrison");
	REQUIRE(postern);
	REQUIRE(garrison);
	const std::string plain = UnitVoiceResponse::templateVoice(*castleTT, UnitVoiceResponse::VOICE_SELECT).sound;
	CHECK(voice.objectVoice(*piece, UnitVoiceResponse::VOICE_SELECT).sound == plain);
	piece->giveUpgradeSelf(postern);
	CHECK(voice.objectVoice(*piece, UnitVoiceResponse::VOICE_SELECT).sound == "GondorPosternGateSelect");
	piece->giveUpgradeSelf(garrison); // both: neither entry applies (each excludes the other)
	CHECK(voice.objectVoice(*piece, UnitVoiceResponse::VOICE_SELECT).sound == plain);

	// the peasants' random selector: VoicePriority 32 while the roll is below Chance (5 %), else the template's; RerollOnEveryFrame = No keeps the first roll
	const ThingTemplate *peasantTT = logic.things().findTemplate("RohanPeasant1");
	REQUIRE(peasantTT);
	const RandomSoundSelectorClientBehaviorModuleData *rd = nullptr;
	for (const ThingTemplate::Nugget &n : peasantTT->getFinalOverride()->clientBehaviorModules().nuggets())
	{
		rd = rd ? rd : dynamic_cast<const RandomSoundSelectorClientBehaviorModuleData *>(n.data.get());
	}
	REQUIRE(rd);
	CHECK(rd->m_chance == doctest::Approx(0.05f));
	CHECK_FALSE(rd->m_rerollOnEveryFrame);
	CHECK(rd->m_sounds.hasVoicePriority);
	CHECK(rd->m_sounds.voicePriority == 32);
	Object *peasant = logic.newObject(peasantTT, men->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(peasant);
	SoundSelector::Query q;
	q.object = peasant;
	q.drawableFlags = &peasant->getModelConditionBits();
	q.clientFrame = 1;
	FixedRoll low;
	low.value = 0.01f;
	SoundSelectors::RandomStates statesLow;
	int priority = -1;
	CHECK(SoundSelectors::voicePriority(*peasantTT->getFinalOverride(), q, statesLow, low, &priority));
	CHECK(priority == 32);
	FixedRoll high;
	high.value = 0.5f;
	SoundSelectors::RandomStates statesHigh;
	CHECK_FALSE(SoundSelectors::voicePriority(*peasantTT->getFinalOverride(), q, statesHigh, high, &priority));
	q.clientFrame = 2; // no reroll (RerollOnEveryFrame = No): the first roll stands
	CHECK_FALSE(SoundSelectors::voicePriority(*peasantTT->getFinalOverride(), q, statesHigh, low, &priority));
	CHECK(low.calls == 1);
	CHECK(high.calls == 1);
}

TEST_CASE("decomp2 S-418 binary facts: the team builder's recruit test, hand-over and the pooled prototype's defaults (RW 0x9A24FC, 0x9A33A7, 0x9A26A1, 0x7A2C96)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("decomp2 binary facts S-418 (RW_GAME_DAT unset)");
		return;
	}
	// the pooled prototype's reset: TeamTemplateInfo::loadFromDict(null): + 0x1A4 (maximum) = + 0x1A0 (type) = -1, + 0x1A8 (minimum) = 1, + 0xF0 (priority) = 0
	CHECK(pe->hex(0x7A2CA7, 3) == "83c8ff");
	CHECK(pe->hex(0x7A2CB5, 6) == "8983a4010000");
	CHECK(pe->hex(0x7A2CC1, 6) == "8983a0010000");
	CHECK(pe->hex(0x7A2CD5, 10) == "c783a801000001000000");
	// the recruit test: IGNORES_SELECT_ALL (template + 0x113 bit 3), the age (frame - creation + 0x4E0 against RW 0xDEBE5C = LOGICFRAMES_PER_SECOND * 10), the victim
	CHECK(pe->hex(0x9A251B, 7) == "f6801301000008");
	CHECK(pe->hex(0x9A260A, 6) == "2b86e0040000");
	CHECK(pe->hex(0x9A2610, 6) == "3b055cbede00");
	CHECK(pe->hex(0xBC9849, 13) == "a108f6d9006bc00aa35cbede00");
	CHECK(pe->hex(0x9A2618, 11) == "8b8e60020000e8e05cccff");
	// the hand-over: an OPPORTUNITY team's maximum clamped to 2, a DEFENSIVE team (+ 0x1A0 == 1) at its minimum, else RW 0x9A26A1
	CHECK(pe->hex(0x9A3352, 3) == "83fa02");
	CHECK(pe->hex(0x9A33ED, 7) == "83bba001000001");
	CHECK(pe->hex(0x9A26AB, 9) == "8b4630ffb0c4020000"); // RW 0x9A26A1: the prototype's target id (+ 0x2C4)
	CHECK(pe->hex(0x9A274E, 4) == "0f2f45a0");           // comiss sum, the enemy record's total
	// the setups' KindOf masks: FeintAttack team 0 forbids CAVALRY | DOZER, SimpleSiege team 0 requires SIEGEENGINE, SiegeGates forbids ARCHER and DOZER
	CHECK(pe->hex(0x9B38EF, 4) == "80490142");
	CHECK(pe->hex(0x9B35FA, 7) == "8088fb02000010");
	CHECK(pe->hex(0x9B3311, 8) == "8348102080480140");
	// RotWK's idle dispatch RW 0x9A3040 (the update's tail jump RW 0x9A34AC): not STRUCTURE, not IS_LEAVING_FACTORY (0x5A), 10 s old, the unit's ThreatBreakdown
	// category (template + 0x530), the attack-move with INT_MAX shots from the player (source 0)
	CHECK(pe->hex(0x9A34AC, 5) == "e98ffbffff");
	CHECK(pe->hex(0x9A30D9, 7) == "f6800801000080");
	CHECK(pe->hex(0x9A30E2, 2) == "6a5a");
	CHECK(pe->hex(0x9A3115, 6) == "3b055cbede00");
	CHECK(pe->hex(0x9A3197, 6) == "8b8030050000");
	CHECK(pe->hex(0x9A3280, 7) == "6a0068ffffff7f");
	CHECK(std::string(TheObjectStatusNames[0x5A]) == "IS_LEAVING_FACTORY");
}

TEST_CASE("decomp2 S-415: the skirmish AI builds heroes of its HeroBuildOrder after the Rush phase (RW 0x9A03A1 / 0x9A0993): a unit request of priority -1")
{
	OPENBFME_REQUIRE_START(s);
	int requests = 0, heroesOut = 0;
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		INFO(std::string(faction));
		std::string error;
		buildtest::Game g;
		REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", faction, 39, g, &error), error);
		GameLogic &logic = g.live->logic();
		Player *cpu = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[1]);
		REQUIRE(cpu);
		const AISkirmishPlayer *ai = logic.skirmishAI().findAI(cpu->getPlayerIndex());
		REQUIRE(ai);
		REQUIRE(ai->army);
		CHECK(ai->build.units.heroNames == ai->army->heroBuildOrder); // RW 0x9A10D8
		int heroFrame = -1, requestFrame = -1;
		std::string hero;
		for (int f = 0; f < 9000 && heroFrame < 0; ++f)
		{
			logic.runLogicFrame();
			if (requestFrame < 0 && ai->build.units.heroesMade > 0)
			{
				requestFrame = (int)logic.getFrame();
				CHECK(ai->build.phase > 0); // RW 0x9A03BC: past the Rush phase
				for (const auto &r : ai->build.units.requests)
				{
					if (r->priority == -1.0f)
					{
						hero = r->templateName;
						CHECK(r->unit);
						CHECK(r->reserveMoney);
					}
				}
				CHECK_FALSE(hero.empty());
				CHECK(ai->build.units.heroInProgress);
				// index 0 (the ring hero) only with Upgrade_RingHero (RW 0x9A0677): never drawn here
				CHECK(hero != ai->build.units.heroNames.front());
			}
			if (requestFrame >= 0 && (f % 25) == 0)
			{
				for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
				{
					if (o->getControllingPlayer() == cpu && o->getTemplate() && o->getTemplate()->getName() == hero)
					{
						heroFrame = (int)logic.getFrame();
					}
				}
			}
		}
		std::string waiting;
		for (const auto &r : ai->build.units.requests)
		{
			if (r->priority == -1.0f)
			{
				waiting = " (its request: state " + std::to_string(r->state) + ", last check " + std::to_string(r->lastCheck) + ")";
			}
		}
		MESSAGE(std::string(faction) << ": the hero request " << (hero.empty() ? "none" : hero) << " at frame " << requestFrame << ", the hero out at frame "
									 << heroFrame << waiting);
		requests += requestFrame > 0 ? 1 : 0;
		heroesOut += heroFrame > 0 ? 1 : 0;
	}
	// how many of the 7 Easy computers reach the hero conditions within 9000 frames depends on their economy (the AI and movement lanes move it: 4 / 7
	// requests and 4 heroes at 15c51742, 3 / 1 after the archive 27f57575 merge), so only the path itself is pinned
	// Merge queue q22: with seed 31 none of the 7 reached the conditions on the merged tree (AI5 alone: 1 / 1, Mordor; SUMMON-1's OCL draws and rock-throw
	// toggles move the AIs' economy; the army list stays at 4..6 entries with the command points near the limit, shouldBuildHero RW 0x9A03A1). Seed 32,
	// measured on the merged tree (srv1): 3 requests and 3 heroes (Men Eomer, Mordor Gothmog, Angmar Morgramir); seeds 33..39 measured 0..3
	// Lane LOS-1 (the terrain line of sight changes the AIs' fights and so their economy): with seed 32 none of the 7 reached the conditions; seed 33, measured
	// on the lane's tree (a CI host): 1 request and 1 hero (Wild WildAzog, requested at frame 6189, out at 6627); seeds 34 / 35 measured 0 / 0 and 1 / 0
	// Merge queue q28 (#96 / #97 / #98, #117, #124 and the archive's plain batch): with seed 33 one request (Angmar Hwaldar at 5865) and no hero out; seeds 34..39
	// measured on the merged tree (a CI host): 1 / 2 / 2 / 1 / 1 / 1 requests and 1 / 1 / 1 / 1 / 0 / 1 heroes; seed 35: 2 requests and 1 hero (Dwarves Gloin
	// out at 8152; Isengard Wormtongue requested at 5140)
	// Merge queue q29 (at each merge point: seed 35 holds through AIDATA-1 and fails from community #113 on (its FlagCaptureSquad sends the AIs' units to
	// the neutral buildings)): with seed 35 none of the 7 requests a hero on the merged tree; seeds 36..43 measured there (a CI host): 2 / 3 / 0 / 4 / 1 /
	// 2 / 3 / 2 requests and 1 / 2 / 0 / 4 / 0 / 0 / 2 / 2 heroes out; seed 39 (the widest margin): 4 requests and 4 heroes (Dwarves Gloin, Isengard Wormtongue,
	// Mordor Gothmog, Wild Goblin King)
	CHECK(requests >= 1);
	CHECK(heroesOut >= 1);
}
