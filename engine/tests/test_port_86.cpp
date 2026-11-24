// OpenBFME unit tests for PORT-86:
//   Unit voice messages 0x7DB .. 0x7DD:
//     - 0x7DB VoiceDesperateAttack: posted by AIBackAwayState::update (RW 0x754772 .. 0x7547B4 -> 0x8DEDBB),
//       resolved via UnitSpecificSounds "VoiceDesperateAttack" (RW 0x8DF9D2 -> 0x8DDB2A, string RW 0xC77AB4).
//     - 0x7DC VoiceRapidFire: posted by FiringTracker::speedUp on CONTINUOUS_FIRE_MEAN -> CONTINUOUS_FIRE_FAST
//       (RW 0x8E3174 .. 0x8E3205 -> 0x8DEDBB), resolved via UnitSpecificSounds "VoiceRapidFire"
//       (RW 0x8DF9AD -> 0x8DDB2A, string RW 0xC77ACC).
//     - 0x7DD VoiceCaptureBuildingComplete: posted by SpecialAbilityUpdate::startPacking(true) when
//       m_specialPowerTemplate->getFinalOverride()->m_type is SPECIAL_BLACKLOTUS_CAPTURE_BUILDING (0x1A) or
//       SPECIAL_INFANTRY_CAPTURE_BUILDING (0x1D) (RW 0x85361C .. 0x85368D -> 0x8DEDBB), resolved via
//       UnitSpecificSounds "VoiceCaptureBuildingComplete" (RW 0x8DF97F -> 0x8DDB2A, string RW 0xC77ADC).
//
// Everything here is synthetic: no RotWK files are mounted.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "GameClient/UnitVoiceResponse.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameLogic/WeaponState.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
struct RecordingSink : UnitVoiceResponse::Sink
{
	std::set<std::string> known;
	std::vector<std::string> sounds;
	std::vector<std::string> eva;
	std::vector<ObjectID> owners;

	bool soundExists(const std::string &name) override { return known.count(name) == 1u; }
	void playSound(const std::string &name, ObjectID object, int) override
	{
		sounds.push_back(name);
		owners.push_back(object);
	}
	void reportEva(const std::string &name, const Coord3D *) override { eva.push_back(name); }
	void clear()
	{
		sounds.clear();
		eva.clear();
		owners.clear();
	}
};

const char kPort86Ini[] =
	"Weapon RapidBlade\n"
	"  AttackRange = 30.0\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 200\n"
	"  PreAttackDelay = 0\n"
	"  FiringDuration = 0\n"
	"  ContinuousFireOne = 1\n"
	"  ContinuousFireTwo = 2\n"
	"  ContinuousFireCoast = 4000\n"
	"  DamageNugget\n"
	"    Damage = 1\n"
	"    Radius = 0.0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"SpecialPower SpecialPowerCaptureInfantry\n"
	"  Enum = SPECIAL_INFANTRY_CAPTURE_BUILDING\n"
	"  ReloadTime = 0\n"
	"  ObjectFilter = ANY +STRUCTURE\n"
	"End\n"
	"SpecialPower SpecialPowerCaptureLotus\n"
	"  Enum = SPECIAL_BLACKLOTUS_CAPTURE_BUILDING\n"
	"  ReloadTime = 0\n"
	"  ObjectFilter = ANY +STRUCTURE\n"
	"End\n"
	"SpecialPower SpecialPowerNonCapture\n"
	"  Enum = SPECIAL_GENERAL_TARGETLESS\n"
	"  ReloadTime = 0\n"
	"End\n"
	"Object DesperateSoldier\n"
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
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"  UnitSpecificSounds\n"
	"    VoiceDesperateAttack = DesperateAttackVox\n"
	"  End\n"
	"End\n"
	"Object SilentDesperateSoldier\n"
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
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object DesperateSoldierHorde\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:DesperateSoldier Position:X:20 Y:0 Position:X:20 Y:15 Position:X:20 Y:-15\n"
	"    InitialPayload = DesperateSoldier 3\n"
	"    BackUpMinDelayTime = 1\n"
	"    BackUpMaxDelayTime = 200\n"
	"    BackUpMinDistance = 1\n"
	"    BackUpMaxDistance = 2\n"
	"    BackupPercentage = 100%\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n"
	"Object RapidShooter\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY RapidBlade\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"  UnitSpecificSounds\n"
	"    VoiceRapidFire = RapidFireVox\n"
	"  End\n"
	"End\n"
	"Object SilentRapidShooter\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY RapidBlade\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object TargetDummy\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 5000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object CapturerInfantry\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
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
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPowerCaptureInfantry\n"
	"  End\n"
	"  Behavior = SpecialAbilityUpdate ModuleTag_Ability\n"
	"    SpecialPowerTemplate = SpecialPowerCaptureInfantry\n"
	"    StartAbilityRange = 50.0\n"
	"    AbilityAbortRange = 80.0\n"
	"    PreparationTime = 200\n"
	"    PackTime = 1\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"  UnitSpecificSounds\n"
	"    VoiceCaptureBuildingComplete = CaptureCompleteVox\n"
	"  End\n"
	"End\n"
	"Object CapturerLotus\n"
	"  KindOf = INFANTRY SELECTABLE HERO CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
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
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPowerCaptureLotus\n"
	"  End\n"
	"  Behavior = SpecialAbilityUpdate ModuleTag_Ability\n"
	"    SpecialPowerTemplate = SpecialPowerCaptureLotus\n"
	"    StartAbilityRange = 50.0\n"
	"    AbilityAbortRange = 80.0\n"
	"    PreparationTime = 200\n"
	"    PackTime = 1\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"  UnitSpecificSounds\n"
	"    VoiceCaptureBuildingComplete = CaptureCompleteVox\n"
	"  End\n"
	"End\n"
	"Object SilentCapturerInfantry\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
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
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPowerCaptureInfantry\n"
	"  End\n"
	"  Behavior = SpecialAbilityUpdate ModuleTag_Ability\n"
	"    SpecialPowerTemplate = SpecialPowerCaptureInfantry\n"
	"    StartAbilityRange = 50.0\n"
	"    AbilityAbortRange = 80.0\n"
	"    PreparationTime = 200\n"
	"    PackTime = 1\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"End\n"
	"Object NonCapturePacker\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
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
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPowerNonCapture\n"
	"  End\n"
	"  Behavior = SpecialAbilityUpdate ModuleTag_Ability\n"
	"    SpecialPowerTemplate = SpecialPowerNonCapture\n"
	"    PreparationTime = 0\n"
	"    PackTime = 200\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"  UnitSpecificSounds\n"
	"    VoiceCaptureBuildingComplete = CaptureCompleteVox\n"
	"  End\n"
	"End\n"
	"Object EvaCapturer\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  UnitSpecificSounds\n"
	"    VoiceCaptureBuildingComplete = EVA:CaptureCompleteEva\n"
	"  End\n"
	"End\n"
	"Object CapturableHall\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE CAPTURABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n";

struct Port86World : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	RecordingSink sink;
	std::unique_ptr<UnitVoiceResponse> voice;
	struct Post
	{
		int voiceEvent;
		std::uint32_t objectId;
	};
	std::vector<Post> posts;

	Port86World()
		: combattest::CombatWorld("", 100)
	{
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		const std::string err = w.load(kPort86Ini, INI_LOAD_OVERWRITE, "port86.ini");
		REQUIRE_MESSAGE(err.empty(), err);

		for (const char *s : { "DesperateAttackVox", "RapidFireVox", "CaptureCompleteVox" })
		{
			sink.known.insert(s);
		}
		voice = std::make_unique<UnitVoiceResponse>(*logic, sink);
		voice->setLocalPlayerIndex(players.findPlayerWithName("Alice")->getPlayerIndex());

		AudioApi::installUnitVoiceHandler([this](int voiceEvent, std::uint32_t objectId, std::uint32_t producerId) {
			posts.push_back({ voiceEvent, objectId });
			UnitVoiceResponse::Info info;
			info.producer = producerId;
			voice->pickAndPlay({ objectId }, voiceEvent, &info);
		}, this);
	}

	~Port86World()
	{
		AudioApi::uninstallUnitVoiceHandler(this);
		TheSpecialPowerStore = savedPowers;
	}

	void clearAudio()
	{
		posts.clear();
		sink.clear();
	}
};

SpecialAbilityUpdate *abilityOf(Object *o)
{
	auto *m = dynamic_cast<SpecialAbilityUpdate *>(o->findModule("SpecialAbilityUpdate"));
	REQUIRE(m);
	return m;
}

void dealAiSlashHit(Object &victim, Object &attacker)
{
	DamageInfo dmg{};
	dmg.m_input.m_sourceID = attacker.getID();
	dmg.m_input.m_damageType = DAMAGE_SLASH;
	dmg.m_input.m_deathType = DEATH_NORMAL;
	dmg.m_input.m_amount = 5.0f;
	victim.attemptDamage(dmg);
}
} // namespace

TEST_CASE("PORT-86 VoiceDesperateAttack: unlocked BACK_AWAY exit on AI enemy hit posts 0x7DB once and resolves UnitSpecificSounds (RW 0x754772 .. 0x7547B4, 0x8DF9D2)")
{
	CHECK(static_cast<int>(UnitVoiceResponse::VOICE_EVENT_DESPERATE_ATTACK) == 0x7DB);
	CHECK(UnitVoiceResponse::handles(0x7DB));

	Port86World f;
	Object *scarer = f.unit("TargetDummy", 'B', 560.0f, 500.0f);
	Object *attacker = f.unit("Swordsman", 'B', 520.0f, 500.0f);

	// 1. Unit WITH VoiceDesperateAttack: locked BACK_AWAY ignores the hit; unlocked BACK_AWAY posts 0x7DB once and plays the row once.
	{
		Object *soldier = f.unit("DesperateSoldier", 'A', 500.0f, 500.0f);
		AIUpdateInterface *ai = soldier->getAIUpdateInterface();
		REQUIRE(ai);

		ai->emotionEnterAIState(EMOTION_AI_BACK_AWAY, scarer);
		REQUIRE(ai->stateMachine().temporaryStateId() == static_cast<unsigned>(AI_BACK_AWAY));
		REQUIRE(ai->stateMachine().temporaryStateLocked());

		// Hit while locked (-1): RW 0x75476B keeps state 48 and posts nothing.
		f.clearAudio();
		dealAiSlashHit(*soldier, *attacker);
		f.frames(1);
		CHECK(ai->stateMachine().temporaryStateId() == static_cast<unsigned>(AI_BACK_AWAY));
		CHECK(f.posts.empty());
		CHECK(f.sink.sounds.empty());

		// Wait 21 frames (> 4 s = 20 frames) so the earlier hit ages out, then unlock (-2) and hit again.
		f.frames(21);
		ai->emotionLockElapsed();
		REQUIRE_FALSE(ai->stateMachine().temporaryStateLocked());
		REQUIRE(ai->stateMachine().temporaryStateId() == static_cast<unsigned>(AI_BACK_AWAY));

		f.clearAudio();
		dealAiSlashHit(*soldier, *attacker);
		f.frames(1);
		CHECK(ai->stateMachine().temporaryStateId() == static_cast<unsigned>(AI_NO_STATE));
		REQUIRE(f.posts.size() == 1u);
		CHECK(f.posts[0].voiceEvent == 0x7DB);
		CHECK(f.posts[0].objectId == soldier->getID());
		CHECK(f.sink.sounds == std::vector<std::string>{ "DesperateAttackVox" });
		CHECK(f.sink.owners == std::vector<ObjectID>{ soldier->getID() });

		// Further frames after leaving BACK_AWAY do not re-post 0x7DB.
		f.frames(5);
		CHECK(f.posts.size() == 1u);
		CHECK(f.sink.sounds.size() == 1u);
	}

	// 2. Horde container (a horde enters BACK_AWAY, the member template carries VoiceDesperateAttack):
	//    UnitVoiceResponse expands the horde to its living members (RW 0x8DEE95 .. 0x8DEF11) and plays the row once.
	{
		Object *horde = f.unit("DesperateSoldierHorde", 'A', 400.0f, 400.0f);
		f.frames(10);
		AIUpdateInterface *ai = horde->getAIUpdateInterface();
		REQUIRE(ai);
		const std::vector<Object *> members = f.membersOf(horde);
		REQUIRE(members.size() == 3u);

		ai->emotionEnterAIState(EMOTION_AI_BACK_AWAY, scarer);
		ai->emotionLockElapsed();
		REQUIRE(ai->stateMachine().temporaryStateId() == static_cast<unsigned>(AI_BACK_AWAY));
		REQUIRE_FALSE(ai->stateMachine().temporaryStateLocked());

		f.clearAudio();
		dealAiSlashHit(*members[0], *attacker);
		f.frames(1);
		CHECK(ai->stateMachine().temporaryStateId() == static_cast<unsigned>(AI_NO_STATE));
		REQUIRE(f.posts.size() == 1u);
		CHECK(f.posts[0].voiceEvent == 0x7DB);
		CHECK(f.posts[0].objectId == horde->getID());
		CHECK(f.sink.sounds == std::vector<std::string>{ "DesperateAttackVox" });
		REQUIRE(f.sink.owners.size() == 1u);
		const bool ownerIsMember = std::find_if(members.begin(), members.end(), [&](const Object *m) {
			return m->getID() == f.sink.owners[0];
		}) != members.end();
		CHECK(ownerIsMember);
	}

	// 3. Unit WITHOUT VoiceDesperateAttack: posts 0x7DB once, UnitVoiceResponse stays silent without counting unportedMessages.
	{
		Object *silent = f.unit("SilentDesperateSoldier", 'A', 300.0f, 300.0f);
		AIUpdateInterface *ai = silent->getAIUpdateInterface();
		REQUIRE(ai);

		ai->emotionEnterAIState(EMOTION_AI_BACK_AWAY, scarer);
		ai->emotionLockElapsed();
		f.clearAudio();
		const std::uint64_t unportedBefore = f.voice->stats().unportedMessages;
		dealAiSlashHit(*silent, *attacker);
		f.frames(1);
		CHECK(ai->stateMachine().temporaryStateId() == static_cast<unsigned>(AI_NO_STATE));
		REQUIRE(f.posts.size() == 1u);
		CHECK(f.posts[0].voiceEvent == 0x7DB);
		CHECK(f.sink.sounds.empty());
		CHECK(f.sink.eva.empty());
		CHECK(f.voice->stats().unportedMessages == unportedBefore);
	}
}

TEST_CASE("PORT-86 VoiceRapidFire: FiringTracker MEAN -> FAST posts 0x7DC once and resolves UnitSpecificSounds (RW 0x8E3174 .. 0x8E3205, 0x8DF9AD)")
{
	CHECK(static_cast<int>(UnitVoiceResponse::VOICE_EVENT_RAPID_FIRE) == 0x7DC);
	CHECK(UnitVoiceResponse::handles(0x7DC));

	const std::uint32_t kMeanMask = 1u << FiringTracker::CONDITION_CONTINUOUS_FIRE_MEAN;
	const std::uint32_t kFastMask = 1u << FiringTracker::CONDITION_CONTINUOUS_FIRE_FAST;

	Port86World f;
	Object *dummy = f.unit("TargetDummy", 'B', 515.0f, 500.0f);

	// 1. Shooter WITH VoiceRapidFire:
	//    shot 1 -> MEAN (no voice); shot 2 -> FAST (0x7DC once, plays RapidFireVox once); shot 3+ while FAST -> no repeat post.
	{
		Object *shooter = f.unit("RapidShooter", 'A', 500.0f, 500.0f);
		f.clearAudio();
		shooter->getAIUpdateInterface()->aiAttackObject(dummy, CMD_FROM_PLAYER);

		// Advance until the first shot lands (ContinuousFireOne = 1 -> MEAN).
		for (int i = 0; i < 15 && (shooter->weaponBonusConditionMask() & kMeanMask) == 0u; ++i)
		{
			f.frames(1);
		}
		CHECK((shooter->weaponBonusConditionMask() & kMeanMask) == kMeanMask);
		CHECK((shooter->weaponBonusConditionMask() & kFastMask) == 0u);
		CHECK(f.posts.empty());
		CHECK(f.sink.sounds.empty());

		// Advance until the second shot lands (ContinuousFireTwo = 2 -> MEAN clears, FAST sets, 0x7DC posts once).
		for (int i = 0; i < 15 && (shooter->weaponBonusConditionMask() & kFastMask) == 0u; ++i)
		{
			f.frames(1);
		}
		CHECK((shooter->weaponBonusConditionMask() & kMeanMask) == 0u);
		CHECK((shooter->weaponBonusConditionMask() & kFastMask) == kFastMask);
		REQUIRE(f.posts.size() == 1u);
		CHECK(f.posts[0].voiceEvent == 0x7DC);
		CHECK(f.posts[0].objectId == shooter->getID());
		CHECK(f.sink.sounds == std::vector<std::string>{ "RapidFireVox" });
		CHECK(f.sink.owners == std::vector<ObjectID>{ shooter->getID() });

		// Keep firing several more shots while already in CONTINUOUS_FIRE_FAST: RW 0x8E31B8 returns early without re-posting 0x7DC.
		const float hpAfterTwo = dummy->getBodyModule()->getHealth();
		f.frames(6);
		const bool dealtMoreShots = dummy->getBodyModule()->getHealth() != hpAfterTwo;
		CHECK(dealtMoreShots);
		CHECK((shooter->weaponBonusConditionMask() & kFastMask) == kFastMask);
		CHECK(f.posts.size() == 1u);
		CHECK(f.sink.sounds.size() == 1u);

		shooter->getAIUpdateInterface()->aiIdle(CMD_FROM_PLAYER);
	}

	// 2. Shooter WITHOUT VoiceRapidFire: reaches FAST and posts 0x7DC once, plays nothing.
	{
		Object *silent = f.unit("SilentRapidShooter", 'A', 500.0f, 515.0f);
		f.clearAudio();
		const std::uint64_t unportedBefore = f.voice->stats().unportedMessages;
		silent->getAIUpdateInterface()->aiAttackObject(dummy, CMD_FROM_PLAYER);

		for (int i = 0; i < 20 && (silent->weaponBonusConditionMask() & kFastMask) == 0u; ++i)
		{
			f.frames(1);
		}
		CHECK((silent->weaponBonusConditionMask() & kFastMask) == kFastMask);
		f.frames(4);
		REQUIRE(f.posts.size() == 1u);
		CHECK(f.posts[0].voiceEvent == 0x7DC);
		CHECK(f.sink.sounds.empty());
		CHECK(f.sink.eva.empty());
		CHECK(f.voice->stats().unportedMessages == unportedBefore);
	}
}

TEST_CASE("PORT-86 VoiceCaptureBuildingComplete: startPacking(true) on 0x1D and 0x1A posts 0x7DD once and resolves UnitSpecificSounds (RW 0x85361C .. 0x85368D, 0x8DF97F)")
{
	CHECK(static_cast<int>(UnitVoiceResponse::VOICE_EVENT_CAPTURE_BUILDING_COMPLETE) == 0x7DD);
	CHECK(UnitVoiceResponse::handles(0x7DD));

	Port86World f;

	// 1. SPECIAL_INFANTRY_CAPTURE_BUILDING (0x1D) WITH VoiceCaptureBuildingComplete:
	//    PackTime = 1  enters startPacking(true) on completion and plays once.
	{
		Object *capturer = f.unit("CapturerInfantry", 'A', 300.0f, 300.0f);
		Object *hall = f.unit("CapturableHall", 'B', 325.0f, 300.0f);
		SpecialAbilityUpdate *u = abilityOf(capturer);
		const SpecialPowerTemplate *spt = u->abilityData()->m_specialPowerTemplate;
		REQUIRE(spt);
		CHECK(spt->m_type == 0x1D);

		f.clearAudio();
		u->initiateIntentToDoSpecialPower(spt, hall, nullptr, 0, 0);
		for (int i = 0; i < 10 && hall->getTeam() != f.teamA(); ++i)
		{
			f.frames(1);
		}
		CHECK(hall->getTeam() == f.teamA());
		f.frames(3); // finish packing too: must not post a second time
		REQUIRE(f.posts.size() == 1u);
		CHECK(f.posts[0].voiceEvent == 0x7DD);
		CHECK(f.posts[0].objectId == capturer->getID());
		CHECK(f.sink.sounds == std::vector<std::string>{ "CaptureCompleteVox" });
		CHECK(f.sink.owners == std::vector<ObjectID>{ capturer->getID() });
	}

	// 2. SPECIAL_BLACKLOTUS_CAPTURE_BUILDING (0x1A) WITH VoiceCaptureBuildingComplete: also posts 0x7DD once.
	{
		Object *lotus = f.unit("CapturerLotus", 'A', 400.0f, 300.0f);
		Object *hall = f.unit("CapturableHall", 'B', 425.0f, 300.0f);
		SpecialAbilityUpdate *u = abilityOf(lotus);
		const SpecialPowerTemplate *spt = u->abilityData()->m_specialPowerTemplate;
		REQUIRE(spt);
		CHECK(spt->m_type == 0x1A);

		f.clearAudio();
		u->initiateIntentToDoSpecialPower(spt, hall, nullptr, 0, 0);
		for (int i = 0; i < 10 && hall->getTeam() != f.teamA(); ++i)
		{
			f.frames(1);
		}
		CHECK(hall->getTeam() == f.teamA());
		f.frames(3);
		REQUIRE(f.posts.size() == 1u);
		CHECK(f.posts[0].voiceEvent == 0x7DD);
		CHECK(f.posts[0].objectId == lotus->getID());
		CHECK(f.sink.sounds == std::vector<std::string>{ "CaptureCompleteVox" });
		CHECK(f.sink.owners == std::vector<ObjectID>{ lotus->getID() });
	}

	// 3. Capturer WITHOUT VoiceCaptureBuildingComplete: captures the building, posts 0x7DD once, plays nothing.
	{
		Object *silent = f.unit("SilentCapturerInfantry", 'A', 300.0f, 400.0f);
		Object *hall = f.unit("CapturableHall", 'B', 325.0f, 400.0f);
		SpecialAbilityUpdate *u = abilityOf(silent);
		const SpecialPowerTemplate *spt = u->abilityData()->m_specialPowerTemplate;
		REQUIRE(spt);

		f.clearAudio();
		const std::uint64_t unportedBefore = f.voice->stats().unportedMessages;
		u->initiateIntentToDoSpecialPower(spt, hall, nullptr, 0, 0);
		for (int i = 0; i < 10 && hall->getTeam() != f.teamA(); ++i)
		{
			f.frames(1);
		}
		CHECK(hall->getTeam() == f.teamA());
		f.frames(3);
		REQUIRE(f.posts.size() == 1u);
		CHECK(f.posts[0].voiceEvent == 0x7DD);
		CHECK(f.sink.sounds.empty());
		CHECK(f.sink.eva.empty());
		CHECK(f.voice->stats().unportedMessages == unportedBefore);
	}

	// 4. Aborted capture (target already allied when preparation checks continuePreparation -> startPacking(false)): does NOT post 0x7DD.
	{
		Object *capturer = f.unit("CapturerInfantry", 'A', 500.0f, 400.0f);
		Object *alliedHall = f.unit("CapturableHall", 'A', 525.0f, 400.0f);
		SpecialAbilityUpdate *u = abilityOf(capturer);
		const SpecialPowerTemplate *spt = u->abilityData()->m_specialPowerTemplate;
		REQUIRE(spt);

		f.clearAudio();
		u->initiateIntentToDoSpecialPower(spt, alliedHall, nullptr, 0, 0);
		f.frames(5);
		CHECK(f.posts.empty());
		CHECK(f.sink.sounds.empty());
	}
}

TEST_CASE("PORT-86 neighbors: non-capture packing, scarer-death BACK_AWAY exit, non-local speaker filter, and S-703 unported events stay intact")
{
	Port86World f;

	// 1. Non-capture SpecialAbilityUpdate with PackTime = 200 on a unit carrying VoiceCaptureBuildingComplete does NOT post 0x7DD.
	{
		Object *packer = f.unit("NonCapturePacker", 'A', 200.0f, 200.0f);
		SpecialAbilityUpdate *u = abilityOf(packer);
		const SpecialPowerTemplate *spt = u->abilityData()->m_specialPowerTemplate;
		REQUIRE(spt);

		f.clearAudio();
		u->initiateIntentToDoSpecialPower(spt, nullptr, nullptr, 0, 0);
		f.frames(6);
		CHECK(f.posts.empty());
		CHECK(f.sink.sounds.empty());
	}

	// 2. BACK_AWAY ending because the scarer dies (RW 0x75470D) does NOT post 0x7DB.
	{
		Object *soldier = f.unit("DesperateSoldier", 'A', 250.0f, 250.0f);
		Object *scarer = f.unit("TargetDummy", 'B', 280.0f, 250.0f);
		AIUpdateInterface *ai = soldier->getAIUpdateInterface();
		REQUIRE(ai);

		ai->emotionEnterAIState(EMOTION_AI_BACK_AWAY, scarer);
		ai->emotionLockElapsed();
		REQUIRE(ai->stateMachine().temporaryStateId() == static_cast<unsigned>(AI_BACK_AWAY));

		f.clearAudio();
		DamageInfo lethal{};
		lethal.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		lethal.m_input.m_deathType = DEATH_NORMAL;
		lethal.m_input.m_amount = 10000.0f;
		scarer->attemptDamage(lethal);
		f.frames(1);
		CHECK(ai->stateMachine().temporaryStateId() == static_cast<unsigned>(AI_NO_STATE));
		CHECK(f.posts.empty());
		CHECK(f.sink.sounds.empty());
	}

	// 3. isEnterState (RW 0x8DD63C) is false for 0x7DB .. 0x7DD:
	//    - MinDelayBetweenEnterStateVoice (RW 0x8DFF9D) does NOT suppress consecutive 0x7DB / 0x7DC / 0x7DD calls;
	//    - EVA: rows obey RW 0x8E0038 (reported for the local player 'A', suppressed for non-local player 'B').
	{
		f.voice->setMinDelayBetweenEnterStateVoiceFrames(20);
		Object *soldier = f.unit("DesperateSoldier", 'A', 600.0f, 200.0f);
		f.clearAudio();
		CHECK(f.voice->pickAndPlay({ soldier->getID() }, UnitVoiceResponse::VOICE_EVENT_DESPERATE_ATTACK));
		CHECK(f.voice->pickAndPlay({ soldier->getID() }, UnitVoiceResponse::VOICE_EVENT_DESPERATE_ATTACK));
		CHECK(f.sink.sounds.size() == 2u);
		CHECK(f.voice->stats().repeatSuppressed == 0u);

		Object *mineEva = f.unit("EvaCapturer", 'A', 620.0f, 200.0f);
		Object *enemyEva = f.unit("EvaCapturer", 'B', 640.0f, 200.0f);
		f.clearAudio();
		CHECK(f.voice->pickAndPlay({ mineEva->getID() }, UnitVoiceResponse::VOICE_EVENT_CAPTURE_BUILDING_COMPLETE));
		CHECK(f.sink.eva == std::vector<std::string>{ "CaptureCompleteEva" });
		f.clearAudio();
		CHECK_FALSE(f.voice->pickAndPlay({ enemyEva->getID() }, UnitVoiceResponse::VOICE_EVENT_CAPTURE_BUILDING_COMPLETE));
		CHECK(f.sink.eva.empty());
	}

	// 4. Remaining unported S-703 message types (0x423, 0x7EA, 0x7EB) still increment unportedMessages, whereas 0x7DB .. 0x7DD do not.
	{
		Object *soldier = f.unit("DesperateSoldier", 'A', 200.0f, 300.0f);
		const std::uint64_t before = f.voice->stats().unportedMessages;
		CHECK_FALSE(f.voice->pickAndPlay({ soldier->getID() }, MSG_COMBINE_HORDES_WITH_OBJECT));
		CHECK_FALSE(f.voice->pickAndPlay({ soldier->getID() }, 0x7EA));
		CHECK_FALSE(f.voice->pickAndPlay({ soldier->getID() }, 0x7EB));
		CHECK(f.voice->stats().unportedMessages == before + 3u);

		const std::vector<std::string> stops = UnitVoiceResponse::acceptanceStops();
		REQUIRE(stops.size() == 6u);
		CHECK(stops[3].find("VoiceDesperateAttack") == std::string::npos);
		CHECK(stops[3].find("RapidFire") == std::string::npos);
		CHECK(stops[3].find("CaptureBuildingComplete") == std::string::npos);
		CHECK(stops[3].find("VoiceStartCharging") != std::string::npos);
	}
}
