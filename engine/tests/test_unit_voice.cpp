// OpenBFME unit tests (lane AUDIO-2): the unit voice picker (RW 0x8DEDBB, GameClient/UnitVoiceResponse) on synthetic templates, and the voice rows of every
// retail template against the retail audio and Eva tables.

#include "doctest.h"

#include "HudTestUtil.h"
#include "LogicTestUtil.h"
#include "RetailTestMount.h"

#include "Common/Audio/AudioIni.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "GameClient/LiveGameAudio.h"
#include "GameClient/UnitVoiceResponse.h"
#include "GameLogic/GameMessage.h"

#include <cstdio>
#include <set>

namespace
{
struct RecordingSink : UnitVoiceResponse::Sink
{
	std::set<std::string> known;
	std::vector<std::string> sounds, eva;
	std::vector<ObjectID> owners;
	bool soundExists(const std::string &name) override { return known.count(name) != 0; }
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

const char kVoiceObjects[] =
	"Object Soldier\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VoicePriority = 10\n"
	"  VoiceSelect = SoldierSelect\n"
	"  VoiceMove = SoldierMove\n"
	"  VoiceAttack = SoldierAttack\n"
	"  VoiceAttackCharge = SoldierCharge\n"
	"  VoiceAttackChargeTimeout = 2000\n" // 10 frames
	"  VoiceEnterStateMove = SoldierGo\n"
	"  UnitSpecificSounds\n"
	"    VoiceBombard = SoldierBombard ; a comment\n"
	"    VoiceAttackUnitTower = SoldierHatesTowers\n"
	"  End\n"
	"End\n"
	"Object Archer\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VoicePriority = 10\n"
	"  VoiceSelect = ArcherSelect\n"
	"End\n"
	"Object Captain\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VoicePriority = 20\n"
	"  VoiceSelect = CaptainSelect\n"
	"End\n"
	"Object Champion\n"
	"  KindOf = INFANTRY SELECTABLE HERO\n"
	"  VoicePriority = 1\n"
	"  VoiceSelect = ChampionSelect\n"
	"End\n"
	"Object Builder\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VoiceSelect = EVA:BuilderSelected\n"
	"  VoiceFullyCreated = EVA:BuilderReady\n"
	"  VoiceFullyCreated = +SOUND:BuilderVoxReady\n"
	"  VoiceCreated = +SOUND:BuilderVoxCreated\n"
	"End\n"
	"Object Tower\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  VoiceSelect = NoSound\n"
	"End\n"
	"Object Wall\n"
	"  KindOf = STRUCTURE\n"
	"End\n";

struct VoiceRig
{
	logictest::LogicWorld f;
	RecordingSink sink;
	std::unique_ptr<UnitVoiceResponse> voice;
	VoiceRig()
	{
		const std::string err = f.w.load(kVoiceObjects, INI_LOAD_OVERWRITE, "voices.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		for (const char *s : { "SoldierSelect", "SoldierMove", "SoldierAttack", "SoldierCharge", "SoldierGo", "SoldierBombard", "SoldierHatesTowers", "ArcherSelect",
				 "CaptainSelect", "ChampionSelect", "BuilderVoxReady" })
		{
			sink.known.insert(s);
		}
		voice = std::make_unique<UnitVoiceResponse>(*f.logic, sink);
		voice->setLocalPlayerIndex(f.players.findPlayerWithName("Alice")->getPlayerIndex());
	}
	ObjectID make(const char *name, const char *player = "Alice")
	{
		Object *o = f.make(name, f.teamOf(player));
		return o->getID();
	}
};
} // namespace

TEST_CASE("unit voice: the template rows and UnitSpecificSounds resolve like RW 0x73AB45 (NoSound, EVA:, +SOUND: keeps the Eva id)")
{
	VoiceRig r;
	const ThingTemplate &builder = *r.f.w.get("Builder");
	const UnitVoiceResponse::Voice full = UnitVoiceResponse::templateVoice(builder, UnitVoiceResponse::VOICE_FULLY_CREATED);
	CHECK(full.eva == "BuilderReady");
	CHECK(full.sound == "BuilderVoxReady");
	// a +SOUND: line without an earlier EVA: line of the row keeps an empty id
	const UnitVoiceResponse::Voice created = UnitVoiceResponse::templateVoice(builder, UnitVoiceResponse::VOICE_CREATED);
	CHECK(created.eva.empty());
	CHECK(created.sound == "BuilderVoxCreated");
	CHECK(UnitVoiceResponse::templateVoice(builder, UnitVoiceResponse::VOICE_SELECT).eva == "BuilderSelected");
	CHECK_FALSE(UnitVoiceResponse::templateVoice(*r.f.w.get("Tower"), UnitVoiceResponse::VOICE_SELECT).any());
	CHECK_FALSE(UnitVoiceResponse::templateVoice(*r.f.w.get("Wall"), UnitVoiceResponse::VOICE_SELECT).any());
	const ThingTemplate &soldier = *r.f.w.get("Soldier");
	CHECK(UnitVoiceResponse::unitSpecificVoice(soldier, "VoiceBombard").sound == "SoldierBombard");
	CHECK_FALSE(UnitVoiceResponse::unitSpecificVoice(soldier, "VoiceGarrison").any());
	CHECK(std::string(UnitVoiceResponse::rowName(UnitVoiceResponse::VOICE_ENTER_STATE_ATTACK)) == "VoiceEnterStateAttack");
	CHECK(std::string(UnitVoiceResponse::rowName(32)) == "VoiceEnterStateMoveWhileAttacking");
}

TEST_CASE("unit voice: one voice per command, the most common sound of the best rank (RW 0x8DD663 / 0x8DFF8B)")
{
	VoiceRig r;
	const ObjectID s1 = r.make("Soldier"), s2 = r.make("Soldier"), a1 = r.make("Archer"), cap = r.make("Captain"), hero = r.make("Champion");
	// equal VoicePriority: the sound two candidates share beats the one of a single candidate, the first object that had it speaks
	CHECK(r.voice->pickAndPlay({ a1, s1, s2 }, MSG_CREATE_SELECTED_GROUP));
	REQUIRE(r.sink.sounds.size() == 1);
	CHECK(r.sink.sounds[0] == "SoldierSelect");
	CHECK(r.sink.owners[0] == s1);
	CHECK(r.voice->last().count == 2);
	// a higher VoicePriority wins alone
	r.sink.clear();
	CHECK(r.voice->pickAndPlay({ s1, s2, a1, cap }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.sink.sounds == std::vector<std::string>{ "CaptainSelect" });
	// a hero beats every non hero, whatever its priority
	r.sink.clear();
	CHECK(r.voice->pickAndPlay({ cap, s1, hero }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.sink.sounds == std::vector<std::string>{ "ChampionSelect" });
	// a tie in count: the first key in (case-insensitive) name order
	r.sink.clear();
	CHECK(r.voice->pickAndPlay({ s1, a1 }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.sink.sounds == std::vector<std::string>{ "ArcherSelect" });
	// move: VoiceMove; a force move says nothing (0x431 is not in the switch)
	r.sink.clear();
	UnitVoiceResponse::Info dest;
	dest.hasPosition = true;
	dest.position = Coord3D{ 100.0f, 100.0f, 0.0f };
	CHECK(r.voice->pickAndPlay({ s1 }, MSG_DO_MOVETO, &dest));
	CHECK(r.sink.sounds == std::vector<std::string>{ "SoldierMove" });
	CHECK_FALSE(r.voice->pickAndPlay({ s1 }, MSG_DO_FORCEMOVETO, &dest));
	// nothing selected that can talk
	CHECK_FALSE(r.voice->pickAndPlay({ r.make("Tower") }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.voice->stats().silent >= 2);
}

TEST_CASE("unit voice: the attack rules: Bombard, AttackUnit<target>, the charge voice and its timeout (RW 0x8DDF3B, 0x671ED8 / 0x671F16)")
{
	VoiceRig r;
	const ObjectID s1 = r.make("Soldier");
	const ObjectID tower = r.make("Tower", "Bob");
	UnitVoiceResponse::Info atTower;
	atTower.target = tower;
	CHECK(r.voice->pickAndPlay({ s1 }, MSG_DO_ATTACK_OBJECT, &atTower));
	CHECK(r.sink.sounds.back() == "SoldierHatesTowers");
	UnitVoiceResponse::Info ground;
	ground.hasPosition = true;
	CHECK(r.voice->pickAndPlay({ s1 }, MSG_DO_FORCE_ATTACK_GROUND, &ground));
	CHECK(r.sink.sounds.back() == "SoldierBombard");
	// the attack above started the charge timeout (10 frames): an attack inside it is the plain voice, after it the charge voice again
	const ObjectID s2 = r.make("Soldier");
	CHECK(r.voice->pickAndPlay({ s2 }, MSG_DO_ATTACK_OBJECT, nullptr));
	CHECK(r.sink.sounds.back() == "SoldierCharge");
	CHECK(r.voice->pickAndPlay({ s2 }, MSG_DO_ATTACK_OBJECT, nullptr));
	CHECK(r.sink.sounds.back() == "SoldierAttack");
	for (int i = 0; i < 10; ++i)
	{
		r.f.logic->runLogicFrame();
	}
	CHECK(r.voice->pickAndPlay({ s2 }, MSG_DO_ATTACK_OBJECT, nullptr));
	CHECK(r.sink.sounds.back() == "SoldierCharge");
}

TEST_CASE("unit voice: Eva ids go to TheEva for the local player's objects, sounds attach to the speaker, unknown sounds stay silent")
{
	VoiceRig r;
	const ObjectID mine = r.make("Builder"), theirs = r.make("Builder", "Bob");
	CHECK(r.voice->pickAndPlay({ mine }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.sink.eva == std::vector<std::string>{ "BuilderSelected" });
	CHECK(r.sink.sounds.empty());
	r.sink.clear();
	CHECK_FALSE(r.voice->pickAndPlay({ theirs }, MSG_CREATE_SELECTED_GROUP)); // RW 0x8E0038: not the local player, not an enter-state voice
	CHECK(r.sink.eva.empty());
	// the fully-created voice: the Eva event and the sound both
	r.sink.clear();
	CHECK(r.voice->pickAndPlay({ mine }, UnitVoiceResponse::VOICE_EVENT_FULLY_CREATED));
	CHECK(r.sink.eva == std::vector<std::string>{ "BuilderReady" });
	CHECK(r.sink.sounds == std::vector<std::string>{ "BuilderVoxReady" });
	// VoiceCreated names a sound the audio table does not have: retail's isValid check (TheAudio vtable + 0x7C) plays nothing
	r.sink.clear();
	CHECK_FALSE(r.voice->pickAndPlay({ mine }, UnitVoiceResponse::VOICE_EVENT_CREATED));
	CHECK(r.voice->stats().unknownSounds == 1);
}

TEST_CASE("unit voice: an enter-state voice repeats only after MinDelayBetweenEnterStateVoice (RW 0x6769D9 / 0x676A23)")
{
	VoiceRig r;
	r.voice->setMinDelayBetweenEnterStateVoiceFrames(5);
	const ObjectID s1 = r.make("Soldier");
	CHECK(r.voice->pickAndPlay({ s1 }, UnitVoiceResponse::VOICE_EVENT_ENTER_STATE_MOVE));
	CHECK(r.sink.sounds.back() == "SoldierGo");
	CHECK_FALSE(r.voice->pickAndPlay({ s1 }, UnitVoiceResponse::VOICE_EVENT_ENTER_STATE_MOVE));
	CHECK(r.voice->stats().repeatSuppressed == 1);
	for (int i = 0; i < 6; ++i)
	{
		r.f.logic->runLogicFrame();
	}
	CHECK(r.voice->pickAndPlay({ s1 }, UnitVoiceResponse::VOICE_EVENT_ENTER_STATE_MOVE));
}

TEST_CASE("unit voice: the acceptance stops S-700 .. S-705 are reported, and keys without a loaded crowd response are counted (S-700)")
{
	const std::vector<std::string> stops = UnitVoiceResponse::acceptanceStops();
	REQUIRE(stops.size() == 6);
	for (int i = 0; i < 6; ++i)
	{
		CHECK(stops[(size_t)i].rfind("[S-70" + std::to_string(i) + "] unit voice:", 0) == 0);
	}
	VoiceRig r;
	CHECK(r.f.w.load("Object Crowd\n  KindOf = INFANTRY\n  VoiceSelect = SoldierSelect\n  CrowdResponseKey = GoodMen\nEnd\n", INI_LOAD_OVERWRITE, "crowd.ini").empty());
	const ObjectID a = r.make("Crowd"), b = r.make("Crowd");
	CHECK(r.voice->pickAndPlay({ a, b }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.voice->stats().crowdResponseSkipped == 2); // no CrowdResponse.ini loaded: the key names nothing
	// a voice message type this port does not answer is counted, not guessed (S-703)
	CHECK_FALSE(r.voice->pickAndPlay({ a }, MSG_DO_SPECIAL_POWER));
	CHECK(r.voice->stats().unportedMessages == 1);
}

TEST_CASE("unit voice retail: every voice row of every template names a retail audio event or Eva event")
{
	if (!hudtest::haveWorld("unit voice retail"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	const AudioIniState &audio = s.world->audio();
	size_t rows = 0, eva = 0, sounds = 0, templatesWithVoice = 0, unitSpecific = 0;
	std::vector<std::string> unknownSounds, unknownEva;
	for (const ThingTemplate *tt : s.world->things().templates())
	{
		bool any = false;
		for (int row = 0; row < UnitVoiceResponse::VOICE_ROW_COUNT; ++row)
		{
			const UnitVoiceResponse::Voice v = UnitVoiceResponse::templateVoice(*tt, row);
			if (!v.any())
			{
				continue;
			}
			any = true;
			++rows;
			if (!v.eva.empty())
			{
				++eva;
				if (audio.eva.findIndex(v.eva, false) < 0)
				{
					unknownEva.push_back(tt->getName() + "." + UnitVoiceResponse::rowName(row) + "=" + v.eva);
				}
			}
			if (!v.sound.empty())
			{
				++sounds;
				if (!audio.infos.contains(v.sound))
				{
					unknownSounds.push_back(tt->getName() + "." + UnitVoiceResponse::rowName(row) + "=" + v.sound);
				}
			}
		}
		for (const RawBlock &b : tt->rawBlocks())
		{
			unitSpecific += b.field == "UnitSpecificSounds" ? 1 : 0;
		}
		templatesWithVoice += any ? 1 : 0;
	}
	std::printf("unit voice retail: %zu templates with voices, %zu voice rows (%zu Eva ids, %zu sounds), %zu UnitSpecificSounds blocks; unknown: %zu sounds, %zu Eva\n",
		templatesWithVoice, rows, eva, sounds, unitSpecific, unknownSounds.size(), unknownEva.size());
	for (size_t i = 0; i < unknownSounds.size() && i < 10; ++i)
	{
		std::printf("  unknown sound %s\n", unknownSounds[i].c_str());
	}
	for (size_t i = 0; i < unknownEva.size() && i < 10; ++i)
	{
		std::printf("  unknown eva %s\n", unknownEva[i].c_str());
	}
	CHECK(templatesWithVoice > 500);
	CHECK(eva > 0);
	// retail validates every voice at parse time (RW 0x73AC8C "Invalid Sound", RW 0xC25028 "Unknown EVA event"): a retail template cannot name a missing one
	CHECK(unknownSounds.empty());
	CHECK(unknownEva.empty());
}

TEST_CASE("object ambient sounds: the damage state's sound with ZH's pristine fallback (LiveGameAudio::ambientSoundFor)")
{
	logictest::LogicWorld f;
	const std::string err = f.w.load("Object Mill\n  KindOf = STRUCTURE\n  SoundAmbient = MillLoop\n  SoundAmbientRubble = MillRuins\nEnd\n"
									 "Object Forge\n  KindOf = STRUCTURE\n  SoundAmbient = NoSound\nEnd\n",
		INI_LOAD_OVERWRITE, "ambient.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	Object *mill = f.make("Mill", f.teamOf("Alice"));
	Object *forge = f.make("Forge", f.teamOf("Alice"));
	// no body module: pristine
	CHECK(LiveGameAudio::ambientSoundFor(*mill) == "MillLoop");
	CHECK(LiveGameAudio::ambientSoundFor(*forge).empty());
	const std::vector<std::string> stops = LiveGameAudio::acceptanceStops();
	REQUIRE(stops.size() == 9); // S-708 (no in-game music) is closed: MusicScripts (S-710 / S-711); lane AUDIO-4 adds S-1463 and S-1464
	CHECK(stops[0].rfind("[S-706]", 0) == 0);
	CHECK(stops[1].rfind("[S-707]", 0) == 0);
	CHECK(stops[2].rfind("[S-712]", 0) == 0);
	CHECK(stops[3].rfind("[S-709]", 0) == 0);
	CHECK(stops[4].rfind("[S-1240]", 0) == 0); // lane AUDIO-3
	CHECK(stops[5].rfind("[S-1241]", 0) == 0);
	CHECK(stops[6].rfind("[S-1463]", 0) == 0); // lane AUDIO-4
	CHECK(stops[7].rfind("[S-1464]", 0) == 0);
	CHECK(stops[8].rfind("[S-1242]", 0) == 0);
}

TEST_CASE("audio entry points: the logic's unit voice events reach the installed handler, and are counted without one (AudioApi::postUnitVoice)")
{
	const std::uint64_t before = AudioApi::unitVoicesWithoutHandler();
	AudioApi::postUnitVoice(UnitVoiceResponse::VOICE_EVENT_CREATED, 7, 3);
	CHECK(AudioApi::unitVoicesWithoutHandler() == before + 1);
	int seenEvent = 0;
	std::uint32_t seenObject = 0, seenProducer = 0;
	AudioApi::installUnitVoiceHandler([&](int e, std::uint32_t o, std::uint32_t p) {
		seenEvent = e;
		seenObject = o;
		seenProducer = p;
	});
	AudioApi::postUnitVoice(UnitVoiceResponse::VOICE_EVENT_CREATED, 7, 3);
	AudioApi::installUnitVoiceHandler(AudioApi::UnitVoiceHandler());
	CHECK(seenEvent == 0x7DA);
	CHECK(seenObject == 7);
	CHECK(seenProducer == 3);
	CHECK(AudioApi::unitVoicesWithoutHandler() == before + 1);
	// TheEva not installed: a report is counted, never silently dropped
	const std::uint64_t evaBefore = AudioApi::callsWithoutEva();
	CHECK_FALSE(AudioApi::reportEva("UnitReady", nullptr));
	CHECK(AudioApi::callsWithoutEva() == evaBefore + 1);
}

TEST_CASE("unit voice: the crowd response adds its Threshold's sound for the weighted winning key (S-700, RW 0x8DEF63 / 0x825C45)")
{
	VoiceRig r;
	REQUIRE(r.voice->parseCrowdResponses("CrowdResponse GoodMen\n  Threshold 2\n    VoiceSelect = CrowdSmall ; two or more\n  End\n  Threshold 4\n    VoiceSelect = CrowdLarge\n"
										 "    UnitSpecificSounds\n      VoiceGarrison = NoSound\n    End\n  End\n  Weight = 100\nEnd\n",
		nullptr));
	r.sink.known.insert("CrowdSmall");
	r.sink.known.insert("CrowdLarge");
	CHECK(r.f.w.load("Object Crowd\n  KindOf = INFANTRY\n  VoiceSelect = SoldierSelect\n  CrowdResponseKey = GoodMen\nEnd\n", INI_LOAD_OVERWRITE, "crowd.ini").empty());
	const ObjectID a = r.make("Crowd"), b = r.make("Crowd"), c = r.make("Crowd");
	CHECK(r.voice->pickAndPlay({ a }, MSG_CREATE_SELECTED_GROUP)); // one unit: below the first threshold, no crowd sound
	CHECK(r.sink.sounds == std::vector<std::string>{ "SoldierSelect" });
	r.sink.clear();
	CHECK(r.voice->pickAndPlay({ a, b, c }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.sink.sounds == (std::vector<std::string>{ "SoldierSelect", "CrowdSmall" }));
	CHECK(r.voice->stats().crowdSounds == 1);
	std::string error;
	CHECK_FALSE(r.voice->parseCrowdResponses("CrowdResponse Bad\n  Threshold 1\n    VoiceSelect\n  End\nEnd\n", &error));
	CHECK(error.find("has no value") != std::string::npos);
}

TEST_CASE("unit voice retail: CrowdResponse.ini parses and names the keys the retail templates use")
{
	if (!hudtest::haveWorld("unit voice crowd retail"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(s.mount->fs->readFile("Data\\INI\\CrowdResponse.ini", bytes, &error), error);
	logictest::LogicWorld f;
	RecordingSink sink;
	UnitVoiceResponse voice(*f.logic, sink);
	REQUIRE_MESSAGE(voice.parseCrowdResponses(std::string(bytes.begin(), bytes.end()), &error), error);
	CHECK(voice.crowdResponses().size() >= 9);
	CHECK(voice.crowdResponses().count("GoodMen") == 1);
	std::set<std::string> missing;
	for (const ThingTemplate *tt : s.world->things().templates())
	{
		const std::string key = UnitVoiceResponse::templateVoice(*tt, 0).sound.empty() ? std::string() : std::string();
		const FieldValue *fv = tt->findField("CrowdResponseKey");
		if (const RawTokens *raw = fv ? std::get_if<RawTokens>(fv) : nullptr)
		{
			if (!raw->tokens.empty() && !voice.crowdResponses().count(raw->tokens.front()))
			{
				missing.insert(raw->tokens.front());
			}
		}
		(void)key;
	}
	for (const std::string &m : missing)
	{
		MESSAGE("CrowdResponseKey without a CrowdResponse: " << m);
	}
	CHECK(missing.size() <= 2);
}

TEST_CASE("unit voice review r1: EVA: changes only the Eva half, +SOUND: only the sound, NoSound both, through lines and template copies (RW 0x73AB45)")
{
	logictest::LogicWorld f;
	const std::string err = f.w.load("Object Herald\n  KindOf = INFANTRY\n  VoiceSelect = OldSound\n  VoiceSelect = EVA:Selected\n  VoiceMove = EVA:Moving\n  VoiceMove = PlainMove\n"
									 "  VoiceGuard = GuardSound\n  VoiceGuard = EVA:Guarding\n  VoiceGuard = NoSound\nEnd\n"
									 "ChildObject HeraldKid Herald\n  VoiceSelect = +SOUND:KidSound\nEnd\n",
		INI_LOAD_OVERWRITE, "herald.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	const ThingTemplate &h = *f.w.get("Herald");
	UnitVoiceResponse::Voice v = UnitVoiceResponse::templateVoice(h, UnitVoiceResponse::VOICE_SELECT);
	CHECK(v.eva == "Selected");
	CHECK(v.sound == "OldSound"); // the EVA: line kept the earlier sound
	v = UnitVoiceResponse::templateVoice(h, UnitVoiceResponse::VOICE_MOVE);
	CHECK(v.eva.empty()); // a plain sound clears the Eva half
	CHECK(v.sound == "PlainMove");
	CHECK_FALSE(UnitVoiceResponse::templateVoice(h, UnitVoiceResponse::VOICE_GUARD).any());
	// the child starts from the parent's row state: +SOUND: replaces the sound, keeps the inherited Eva id
	v = UnitVoiceResponse::templateVoice(*f.w.get("HeraldKid"), UnitVoiceResponse::VOICE_SELECT);
	CHECK(v.eva == "Selected");
	CHECK(v.sound == "KidSound");
}

TEST_CASE("unit voice review r1: one bucket per primary sound whatever the Eva id; Eva-only voices stay out of the repeat memory (RW 0x8DFF02 / 0x6769D9)")
{
	VoiceRig r;
	REQUIRE(r.f.w.load("Object EvaA\n  KindOf = INFANTRY\n  VoiceSelect = Shared\n  VoiceSelect = EVA:FirstEva\nEnd\n"
					   "Object EvaB\n  KindOf = INFANTRY\n  VoiceSelect = Shared\n  VoiceSelect = EVA:SecondEva\nEnd\n"
					   "Object Herald2\n  KindOf = INFANTRY\n  VoiceEnterStateMove = EVA:OnTheMove\nEnd\n",
		INI_LOAD_OVERWRITE, "eva.ini")
			  .empty());
	r.sink.known.insert("Shared");
	const ObjectID a = r.make("EvaA"), b = r.make("EvaB");
	CHECK(r.voice->pickAndPlay({ a, b }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.voice->last().count == 2);
	CHECK(r.voice->last().voice.eva == "FirstEva"); // the bucket keeps the first choice's slots
	// an Eva-only enter-state voice is never remembered, so it is never suppressed as a repeat
	r.voice->setMinDelayBetweenEnterStateVoiceFrames(100);
	const ObjectID h = r.make("Herald2");
	r.sink.clear();
	CHECK(r.voice->pickAndPlay({ h }, UnitVoiceResponse::VOICE_EVENT_ENTER_STATE_MOVE));
	CHECK(r.voice->pickAndPlay({ h }, UnitVoiceResponse::VOICE_EVENT_ENTER_STATE_MOVE));
	CHECK(r.sink.eva.size() == 2);
	CHECK(r.voice->stats().repeatSuppressed == 0);
}

TEST_CASE("unit voice review r1: no charge voice while the unit is ATTACKING (RW 0x671ED8 -> 0x694154(0x25))")
{
	VoiceRig r;
	Object *s = r.f.make("Soldier", r.f.teamOf("Alice"));
	s->setModelConditionState(37, true); // ATTACKING (RW model condition 0x25)
	CHECK(r.voice->pickAndPlay({ s->getID() }, MSG_DO_ATTACK_OBJECT, nullptr));
	CHECK(r.sink.sounds.back() == "SoldierAttack");
	s->setModelConditionState(37, false);
	const ObjectID fresh = r.make("Soldier");
	CHECK(r.voice->pickAndPlay({ fresh }, MSG_DO_ATTACK_OBJECT, nullptr));
	CHECK(r.sink.sounds.back() == "SoldierCharge");
}
