// OpenBFME unit tests (lane AUDIO-4): QA-1's sound follow-ups. U20: the music scripts' PLAYER_ACQUIRED_SCIENCE / PLAYER_HAS_REACHED_LEVEL_CAP and the
// acquired-science queue they read (RW 0x7E4FE0, 0x7E50F6, 0x759646, 0x759A4E, 0x75950A), in the logic ScriptConditions and the client's MusicScripts.

#include "doctest.h"

#include "HudTestUtil.h"
#include "IniTestUtil.h"
#include "LogicTestUtil.h"
#include "RetailTestMount.h"
#include "ScriptTestUtil.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/INI.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/AnimationSoundClientBehavior.h"
#include "GameClient/ClientBehaviorModules.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LiveGameAudio.h"
#include "GameClient/LogicSnapshot.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameClient/MusicScripts.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LargeGroupAudioLink.h"
#include "GameLogic/Module/LargeGroupAudioUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SpellStores.h"

#include <string>
#include <vector>

namespace
{

// the stores of TheScienceStore / TheRankInfoStore with two sciences and two ranks
struct Sciences
{
	initest::Fixture fx;
	NameKeyGenerator keys;
	SpellStores stores;
	Sciences()
		: stores(keys)
	{
		keys.init();
		stores.install();
		SpellStores::registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "science.ini",
			"Science SCIENCE_A\nEnd\nScience SCIENCE_B\nEnd\n"
			"Rank 1\n  SkillPointsNeededDefault = 0\nEnd\nRank 2\n  SkillPointsNeededDefault = 100\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~Sciences() { stores.uninstall(); }
	ScienceType sci(const char *name) const { return stores.sciences().getScienceFromInternalName(name); }
};

const char kAudio[] =
	"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n  SoundsExtension = wav\n"
	"  SampleCount2D = 4\n  SampleCount3D = 4\n  StreamCount = 3\n  MinSampleVolume = 2%\n  GlobalMinRange = 5000\n  GlobalMaxRange = 5000000\n  TimeToFadeAudio = 300\n"
	"  AmbientStreamHysteresisVolume = 10\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n  DefaultMusicVolume = 100%\n  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 4\n  MinRange = 160\n  MaxRange = 640\n  PitchShift = 0 0\n  PlayPercent = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"MusicTrack DefaultMusicTrack\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  SubmixSlider = music\nEnd\n"
	"DialogEvent DefaultDialog\n  Volume = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"AmbientStream DefaultAmbientStream\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  MinRange = 100\n  MaxRange = 1000\n  SubmixSlider = ambient\nEnd\n";

std::string wavBytes(double ms)
{
	const std::uint32_t rate = 22050, frames = (std::uint32_t)(ms * rate / 1000.0);
	std::string out;
	auto u32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			out.push_back((char)(std::uint8_t)(v >> (8 * i)));
		}
	};
	auto u16 = [&](std::uint16_t v) {
		out.push_back((char)(std::uint8_t)v);
		out.push_back((char)(std::uint8_t)(v >> 8));
	};
	out += "RIFF";
	u32(36 + frames * 2);
	out += "WAVEfmt ";
	u32(16);
	u16(1);
	u16(1);
	u32(rate);
	u32(rate * 2);
	u16(2);
	u16(16);
	out += "data";
	u32(frames * 2);
	out.append(frames * 2, '\0');
	return out;
}

struct Audio
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	explicit Audio(const std::string &events = std::string())
	{
		fx.mount({ { "data\\audio\\sounds\\a.wav", wavBytes(500.0) } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini", std::string(kAudio) + events);
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 1u << 20);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 7);
	}
};

ScriptParameter msp(const std::string &s, int i = 0)
{
	ScriptParameter p;
	p.stringValue = s;
	p.intValue = i;
	return p;
}

// a music script: when `condition` holds, SET_FLAG(flag, true) (ordinal 1)
ScriptItem musicScript(const std::string &name, ScriptCondition condition, const std::string &flag, bool oneShot)
{
	ScriptItem it;
	it.script = std::make_unique<Script>();
	it.script->name = name;
	it.script->isActive = true;
	it.script->isOneShot = oneShot;
	it.script->easy = it.script->normal = it.script->hard = true;
	OrCondition orc;
	orc.conditions.push_back(std::move(condition));
	it.script->orConditions.push_back(std::move(orc));
	ScriptActionRec a;
	a.type = 1;
	a.params = { msp(flag), msp("", 1) };
	it.script->actions.push_back(std::move(a));
	return it;
}

ScriptCondition musicCond(int type, std::vector<ScriptParameter> params)
{
	ScriptCondition c;
	c.type = type;
	c.params = std::move(params);
	return c;
}

} // namespace

TEST_CASE("audio4 U20: the acquired-science queue replays the player's script notices (RW 0x759A4E push, 0x75950A erase, 0x759646 find / consume)")
{
	PlayerScience ps;
	ps.bind(nullptr, SpellGameMode{}, nullptr);
	CHECK(ps.scriptNotices().empty());
	REQUIRE(ps.addScience(10));
	REQUIRE(ps.addScience(11));
	CHECK_FALSE(ps.addScience(10)); // already owned: no notice (RW 0x6AE186)
	REQUIRE(ps.scriptNotices().size() == 2);
	AcquiredScienceQueue q;
	CHECK(q.didAcquire(ps, 10, false)); // found, kept
	CHECK(q.didAcquire(ps, 10, true));  // found, consumed
	CHECK_FALSE(q.didAcquire(ps, 10, true));
	CHECK(q.pending() == std::vector<ScienceType>{ 11 });
	// the player's reset (RW 0x6AEE11 -> 0x6AED4B): each owned science reported removed (10 is no longer queued: nothing; 11: erased), the list rebuilt
	// (no template, no ranks: empty), the new list notified again (nothing)
	ps.reset();
	CHECK_FALSE(q.didAcquire(ps, 11, false));
	CHECK(q.pending().empty());
	// ScriptEngine::reset: the queue empties and what the player said before is not replayed; what it says after is
	REQUIRE(ps.addScience(12));
	q.clear(&ps);
	// review r1: an acquisition between the clear and the next read is kept (the clear takes the player's position at once)
	REQUIRE(ps.addScience(13));
	CHECK_FALSE(q.didAcquire(ps, 12, false));
	CHECK(q.didAcquire(ps, 13, false));
	// a clear with no player at that index: a later player's whole history is replayed
	AcquiredScienceQueue none;
	none.clear(nullptr);
	CHECK(none.didAcquire(ps, 12, false));
	// a new game's player (bind): its whole history is new
	ps.bind(nullptr, SpellGameMode{}, nullptr);
	REQUIRE(ps.addScience(14));
	CHECK(q.pending().size() == 1); // not replayed before the next read
	CHECK(q.didAcquire(ps, 14, false));
	CHECK(q.pending() == std::vector<ScienceType>{ 14 });
	// two readers of one player keep their own queues
	AcquiredScienceQueue other;
	CHECK(other.didAcquire(ps, 14, true));
	CHECK(q.didAcquire(ps, 14, true));
}

TEST_CASE("audio4 U20 review r1: the notices and every queue are hashed; equal hashes give equal science conditions (the reset's capture is hashed)")
{
	using namespace scripttest;
	// two players with the same sciences but different notice histories hash differently
	PlayerScience a, b;
	a.bind(nullptr, SpellGameMode{}, nullptr);
	b.bind(nullptr, SpellGameMode{}, nullptr);
	REQUIRE(a.addScience(10));
	REQUIRE(b.addScience(10));
	b.reset();             // removal notice, the rebuilt list (empty) ...
	REQUIRE(b.addScience(10)); // ... and the science again: the same list as a's, a longer history
	StateHasher ha, hb;
	a.crc(ha);
	b.crc(hb);
	CHECK(a.sciences() == b.sciences());
	CHECK(ha.value() != hb.value());
	// two games: the acquisition before the new game (dropped by its reset) or after it (kept): the same sciences, different queues, different hashes
	// and different answers
	Sciences s;
	ScriptGame before, after;
	REQUIRE(before.lw.players.findPlayerWithName("Alice")->science().addScience(s.sci("SCIENCE_A")));
	before.start();
	after.start();
	REQUIRE(after.lw.players.findPlayerWithName("Alice")->science().addScience(s.sci("SCIENCE_A")));
	CHECK(before.lw.logic->computeStateHash() != after.lw.logic->computeStateHash());
	const ScriptCondition acquiredA = cond("PLAYER_ACQUIRED_SCIENCE", { sp(11, "Alice"), sp(32, "SCIENCE_A") });
	CHECK_FALSE(before.engine().evaluateCondition(acquiredA));
	CHECK(after.engine().evaluateCondition(acquiredA));
}

TEST_CASE("audio4 U20: ScriptConditions PLAYER_ACQUIRED_SCIENCE (RW 0x7E4FE0) consumes the engine's queue; PLAYER_HAS_REACHED_LEVEL_CAP (RW 0x7E50F6) compares the rank with the max rank")
{
	using namespace scripttest;
	ScriptGame g;
	Sciences s;
	g.start();
	Player *alice = g.lw.players.findPlayerWithName("Alice");
	REQUIRE(alice);
	const ScriptCondition acquiredA = cond("PLAYER_ACQUIRED_SCIENCE", { sp(11, "Alice"), sp(32, "SCIENCE_A") });
	const ScriptCondition acquiredB = cond("PLAYER_ACQUIRED_SCIENCE", { sp(11, "Alice"), sp(32, "SCIENCE_B") });
	const ScriptCondition unknown = cond("PLAYER_ACQUIRED_SCIENCE", { sp(11, "Alice"), sp(32, "SCIENCE_Nothing") });
	CHECK_FALSE(g.engine().evaluateCondition(acquiredA));
	REQUIRE(alice->science().addScience(s.sci("SCIENCE_A")));
	CHECK_FALSE(g.engine().evaluateCondition(acquiredB));
	CHECK(g.engine().evaluateCondition(acquiredA));
	CHECK_FALSE(g.engine().evaluateCondition(acquiredA)); // consumed (RW 0x759646 with consume = 1)
	CHECK_FALSE(g.engine().evaluateCondition(unknown));    // RW 0x5FEF8F -1: false
	CHECK(g.engine().stats().unportedConditions.count("PLAYER_ACQUIRED_SCIENCE") == 0);
	// another player of the mask: "<All Players>"-style masks walk in index order; Bob's science is Bob's
	Player *bob = g.lw.players.findPlayerWithName("Bob");
	REQUIRE(bob);
	REQUIRE(bob->science().addScience(s.sci("SCIENCE_B")));
	CHECK_FALSE(g.engine().evaluateCondition(acquiredB));
	CHECK(g.engine().evaluateCondition(cond("PLAYER_ACQUIRED_SCIENCE", { sp(11, "Bob"), sp(32, "SCIENCE_B") })));

	// the level cap: rank (1) >= the max rank, min(rank count 2, MaxLevelMP / SP of the template)
	const ScriptCondition cap = cond("PLAYER_HAS_REACHED_LEVEL_CAP", { sp(11, "Alice") });
	PlayerTemplate *pt = const_cast<PlayerTemplate *>(alice->getPlayerTemplate());
	REQUIRE(pt);
	const int savedMP = pt->m_maxLevelMP, savedSP = pt->m_maxLevelSP;
	pt->m_maxLevelMP = pt->m_maxLevelSP = 0; // no MaxLevel: the max rank is 0, any rank has reached it
	CHECK(g.engine().evaluateCondition(cap));
	pt->m_maxLevelMP = pt->m_maxLevelSP = 5; // min(2 ranks, 5) = 2
	CHECK(alice->science().getMaxRankLevel() == 2);
	CHECK_FALSE(g.engine().evaluateCondition(cap));
	REQUIRE(alice->science().setRankLevel(2));
	CHECK(g.engine().evaluateCondition(cap));
	CHECK(g.engine().stats().unportedConditions.count("PLAYER_HAS_REACHED_LEVEL_CAP") == 0);
	pt->m_maxLevelMP = savedMP;
	pt->m_maxLevelSP = savedSP;
}

TEST_CASE("audio4 U20: the music scripts answer PLAYER_ACQUIRED_SCIENCE (100) and PLAYER_HAS_REACHED_LEVEL_CAP (173) from their own queue, never the logic's")
{
	using namespace scripttest;
	ScriptGame g;
	Sciences s;
	g.start();
	Audio a;
	Player *alice = g.lw.players.findPlayerWithName("Alice");
	REQUIRE(alice);
	REQUIRE(g.lw.players.getLocalPlayer() == alice);
	PlayerScriptsList psl;
	psl.lists.resize(1);
	psl.lists[0].items.push_back(musicScript("Science", musicCond(100, { msp("<Local Player>"), msp("SCIENCE_A") }), "GotA", false));
	psl.lists[0].items.push_back(musicScript("Cap", musicCond(173, { msp("<Local Player>") }), "AtCap", true));
	MusicScripts m(*a.mgr, *g.lw.logic, g.lw.players, nullptr);
	m.loadScripts(psl);
	PlayerTemplate *pt = const_cast<PlayerTemplate *>(alice->getPlayerTemplate());
	const int savedMP = pt->m_maxLevelMP, savedSP = pt->m_maxLevelSP;
	pt->m_maxLevelMP = pt->m_maxLevelSP = 2;
	g.frame();
	m.update(g.lw.logic->getFrame());
	CHECK_FALSE(m.flag("GotA"));
	CHECK_FALSE(m.flag("AtCap"));
	REQUIRE(alice->science().addScience(s.sci("SCIENCE_A")));
	const std::uint32_t before = g.lw.logic->computeStateHash();
	g.frame();
	const std::uint32_t logicHash = g.lw.logic->computeStateHash();
	m.update(g.lw.logic->getFrame());
	CHECK(m.flag("GotA"));
	CHECK(g.lw.logic->computeStateHash() == logicHash); // the music changed nothing the logic hashes
	CHECK(before != logicHash);                         // (the frame number moved)
	// the logic's own queue still holds the science: the music took from its own copy
	const ScriptCondition acquiredA = cond("PLAYER_ACQUIRED_SCIENCE", { sp(11, "Alice"), sp(32, "SCIENCE_A") });
	CHECK(g.engine().evaluateCondition(acquiredA));
	REQUIRE(alice->science().setRankLevel(2));
	g.frame();
	m.update(g.lw.logic->getFrame());
	CHECK(m.flag("AtCap"));
	CHECK(m.stats().unported.empty());
	pt->m_maxLevelMP = savedMP;
	pt->m_maxLevelSP = savedSP;
}

TEST_CASE("audio4 U21: RotWK's shouldPlayLocally (RW 0x4533A9): owner == local asks PLAYER, an ally ALLIES, anyone else ENEMIES; Living World events are reported")
{
	Audio a("AudioEvent Mine\n  Sounds = a\n  Type = ui player\n  Limit = 0\nEnd\n"
			"AudioEvent Ally\n  Sounds = a\n  Type = ui allies\n  Limit = 0\nEnd\n"
			"AudioEvent Foe\n  Sounds = a\n  Type = ui enemies\n  Limit = 0\nEnd\n"
			"AudioEvent MineAndAllies\n  Sounds = a\n  Type = world player allies\n  Limit = 0\nEnd\n"
			"AudioEvent WorldMine\n  Sounds = a\n  Type = world player\n  Limit = 0\nEnd\n");
	AudioWorldQueries q;
	q.localPlayerIndex = [] { return 1; };
	q.playerExists = [](int i) { return i >= 0 && i < 4; };
	q.relationship = [](int owner, int) { return owner == 2 ? AR_ALLIES : (owner == 3 ? AR_ENEMIES : AR_NEUTRAL); };
	a.mgr->setWorldQueries(q);
	CHECK(a.mgr->playSoundForPlayer("Mine", 1) >= AHSV_FirstHandle);
	CHECK(a.mgr->playSoundForPlayer("Mine", 2) == AHSV_NotForLocal);
	CHECK(a.mgr->playSoundForPlayer("Ally", 2) >= AHSV_FirstHandle);
	CHECK(a.mgr->playSoundForPlayer("Ally", 1) == AHSV_NotForLocal);
	CHECK(a.mgr->playSoundForPlayer("Foe", 3) >= AHSV_FirstHandle);
	CHECK(a.mgr->playSoundForPlayer("Foe", 0) >= AHSV_FirstHandle); // a NEUTRAL owner takes the ENEMIES bit (ZH asked for ENEMIES: refused)
	CHECK(a.mgr->playSoundForPlayer("Foe", 2) == AHSV_NotForLocal);
	CHECK(a.mgr->playSoundForPlayer("MineAndAllies", 1) >= AHSV_FirstHandle);
	CHECK(a.mgr->playSoundForPlayer("MineAndAllies", 2) >= AHSV_FirstHandle);
	CHECK(a.mgr->playSoundForPlayer("MineAndAllies", 3) == AHSV_NotForLocal);
	CHECK(a.mgr->playSoundForPlayer("Mine", -1) >= AHSV_FirstHandle);      // PLAYER + UI without an owner
	CHECK(a.mgr->playSoundForPlayer("WorldMine", -1) == AHSV_NotForLocal); // no owner, not UI: refused
	CHECK(a.mgr->playSoundForPlayer("WorldMine", 9) == AHSV_NotForLocal);  // no such player
	CHECK(a.mgr->report().missingHooks.empty());
	// a Living World event (view type 1, RW AudioEventRTS + 0x30) asks the Living World's army owner: not ported, reported (S-1462)
	AudioEventRTS lw("Foe", VIEW_LIVING_WORLD);
	lw.setPlayerIndex(2);
	CHECK(a.mgr->addAudioEvent(lw) >= AHSV_FirstHandle);
	REQUIRE(a.mgr->report().missingHooks.size() == 1);
	CHECK(a.mgr->report().missingHooks.begin()->first.find("S-1462") != std::string::npos);
}

TEST_CASE("audio4 U21: outside a game the shell's player list answers (no missing hook): Gui_ShellMap* ('ui player', no owner) play, an ownerless 'ui allies' does not")
{
	Audio a("AudioEvent Gui_ShellMapSelect\n  Sounds = a\n  Type = ui player\n  Limit = 0\nEnd\n"
			"AudioEvent AllyUi\n  Sounds = a\n  Type = ui allies\n  Limit = 0\nEnd\n");
	// no queries at all: the filter cannot be made, the event plays and the hook is reported (the state QA-1 met in every game)
	CHECK(a.mgr->playSound("Gui_ShellMapSelect") >= AHSV_FirstHandle);
	CHECK(a.mgr->report().missingHooks.size() == 1);
	Audio b("AudioEvent Gui_ShellMapSelect\n  Sounds = a\n  Type = ui player\n  Limit = 0\nEnd\n"
			"AudioEvent AllyUi\n  Sounds = a\n  Type = ui allies\n  Limit = 0\nEnd\n");
	b.mgr->setIdleWorldQueries(AudioWorldQueries::noGamePlayerList()); // what the GameAudio node installs at boot
	CHECK(b.mgr->playSound("Gui_ShellMapSelect") >= AHSV_FirstHandle);
	CHECK(b.mgr->playSound("AllyUi") == AHSV_NotForLocal);
	CHECK(b.mgr->report().missingHooks.empty());
	// a live game binds its own list and, when it goes, the shell's comes back
	int owner = 0;
	AudioWorldQueries live;
	live.localPlayerIndex = [] { return 1; };
	live.playerExists = [](int i) { return i == 1; };
	live.relationship = [](int, int) { return AR_NEUTRAL; };
	b.mgr->bindWorldQueries(live, &owner);
	CHECK(b.mgr->playSoundForPlayer("Gui_ShellMapSelect", 1) >= AHSV_FirstHandle);
	b.mgr->clearWorldQueries(&owner);
	CHECK(b.mgr->playSoundForPlayer("Gui_ShellMapSelect", 1) >= AHSV_FirstHandle); // no player 1 in the shell: ownerless PLAYER + UI
	CHECK(b.mgr->playSound("AllyUi") == AHSV_NotForLocal);
	CHECK(b.mgr->report().missingHooks.empty());
}

TEST_CASE("audio4 U10 retail: QuitMenu.apt's exit confirmation asks for Gui_ShellMapSelect1, which no INI defines: refused as AHSV_NoSound (RW 0x45CEA7), reported by name")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("audio4 U10 retail");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	// the movie's own string: the "1" is retail data (QuitMenu.apt's constant file, the string beside its "PlaySound" call), not appended by the port
	std::vector<std::uint8_t> movie;
	std::string error;
	REQUIRE_MESSAGE(mount->fs->readFile("QuitMenu.const", movie, &error), error);
	const std::string text(movie.begin(), movie.end());
	const size_t at = text.find(std::string("Gui_ShellMapSelect1") + '\0');
	REQUIRE(at != std::string::npos);
	CHECK(text.find("PlaySound", at) == at + 20);
	INIEnvironment env;
	env.fileSystem = mount->fs.get();
	AudioIniState ini;
	ini.registerBlocks(env.blocks);
	INI reader(env);
	ini.loadAll(reader);
	CHECK((ini.infos.find("Gui_ShellMapSelect") != nullptr));
	CHECK((ini.infos.find("Gui_ShellMapSelect1") == nullptr));
	AudioAssetCache cache(mount->fs.get(), 8u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager mgr(ini, cache, device, RandomAlgorithm::ZH_CarryChain, 3);
	mgr.setIdleWorldQueries(AudioWorldQueries::noGamePlayerList());
	CHECK(mgr.playSound("Gui_ShellMapSelect1") == AHSV_NoSound);
	CHECK(mgr.playSound("Gui_ShellMapSelect") >= AHSV_FirstHandle);
	CHECK(mgr.report().unknownEventNames == std::map<std::string, std::uint64_t>{ { "Gui_ShellMapSelect1", 1 } });
	CHECK(mgr.report().errors.empty());
	CHECK(mgr.report().missingHooks.empty());
}

namespace
{
struct LgaCapture : LargeGroupAudioSink
{
	std::vector<LargeGroupAudioEvent> events;
	void onLargeGroupAudioEvent(const LargeGroupAudioEvent &e) override { events.push_back(e); }
};

const char kLgaObjects[] = "Object Grunt\n  KindOf = INFANTRY\n  Behavior = LargeGroupAudioUpdate ModuleTag_LGAU ; tie in\n"
						   "    Key = Orc Unit Orc ; a key twice is added once\n    Key = Hero\n    UnitWeight = 2\n  End\nEnd\n"
						   "Object Plain\n  KindOf = INFANTRY\n  Behavior = LargeGroupAudioUpdate ModuleTag_LGAU\n  End\nEnd\n";

const LargeGroupAudioUpdateModuleData *lgaData(const ThingTemplate &tt)
{
	for (const auto &n : tt.behaviorModules().nuggets())
	{
		if (const auto *d = dynamic_cast<const LargeGroupAudioUpdateModuleData *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}

LargeGroupAudioUpdate *lgaModule(Object &o)
{
	for (const auto &m : o.modules())
	{
		if (auto *u = dynamic_cast<LargeGroupAudioUpdate *>(m.get()))
		{
			return u;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("audio4 LGA: LargeGroupAudioUpdate's data (RW 0xC6B380): Key lines add their tokens once, UnitWeight 0 .. 65535, the defaults 3 frames / 1 frame / 1")
{
	logictest::LogicWorld f;
	LargeGroupAudioUpdate::registerClass(f.w.modules);
	const std::string err = f.w.load(kLgaObjects, INI_LOAD_OVERWRITE, "lga.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	const LargeGroupAudioUpdateModuleData *grunt = lgaData(*f.w.get("Grunt"));
	REQUIRE(grunt);
	CHECK(grunt->m_keys == std::vector<std::string>{ "Orc", "Unit", "Hero" });
	CHECK(grunt->m_unitWeight == 2);
	const LargeGroupAudioUpdateModuleData *plain = lgaData(*f.w.get("Plain"));
	REQUIRE(plain);
	CHECK(plain->m_keys.empty());
	CHECK(plain->m_unitWeight == 1);
	CHECK(plain->m_timeBetweenUpdatesMin == 3u);      // RW 0x8AF565: ceil(0.005f * 500.0f)
	CHECK(plain->m_timeBetweenUpdatesVariation == 1u); // RW 0x8AF590
	CHECK(f.w.load("Object Timed\n  Behavior = LargeGroupAudioUpdate ModuleTag_T\n    TimeBetweenUpdatesMin = 1000\n    TimeBetweenUpdatesVariation = 2000\n  End\nEnd\n",
			  INI_LOAD_OVERWRITE, "t.ini")
			  .empty());
	CHECK(lgaData(*f.w.get("Timed"))->m_timeBetweenUpdatesMin == 5u);
	CHECK(lgaData(*f.w.get("Timed"))->m_timeBetweenUpdatesVariation == 10u);
	// RW 0x42EC11: outside 0 .. 0xFFFF is an INI error
	CHECK_FALSE(f.w.load("Object Heavy\n  Behavior = LargeGroupAudioUpdate ModuleTag_H\n    UnitWeight = 70000\n  End\nEnd\n", INI_LOAD_OVERWRITE, "h.ini").empty());
}

TEST_CASE("audio4 LGA: the module joins at creation, wakes every min + GameLogicRandomValue(0, variation) + 1 frames (RW 0x8AEEC3), notifies a change or a stale gate, leaves at deletion")
{
	logictest::LogicWorld f;
	LargeGroupAudioUpdate::registerClass(f.w.modules);
	const std::string err = f.w.load(kLgaObjects, INI_LOAD_OVERWRITE, "lga.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	LgaCapture sink;
	LargeGroupAudioLink &link = f.logic->largeGroupAudio();
	link.setSink(&sink);
	CHECK(link.disabled()); // RW 0x60DA95: until the new game enables it
	link.enable(f.logic->getFrame()); // RW 0x60D4D3 at the new game: the gate is this frame
	const UnsignedInt gate = f.logic->getFrame();
	f.logic->random().enableCallLog(true);
	Object *o = f.make("Grunt");
	REQUIRE(o);
	LargeGroupAudioUpdate *m = lgaModule(*o);
	REQUIRE(m);
	CHECK(m->registered());
	// the join happened in the gate frame: dropped (RW 0x60D5FF), one random draw for the wake
	CHECK(sink.events.empty());
	CHECK(link.dropped() == 1);
	std::vector<GameLogicRandom::Call> lgaDraws;
	for (const auto &c : f.logic->random().callLog())
	{
		if (c.file == "LargeGroupAudioUpdate.cpp")
		{
			lgaDraws.push_back(c);
		}
	}
	REQUIRE(lgaDraws.size() == 1);
	const auto &draw = lgaDraws[0];
	CHECK(draw.lo == 0);
	CHECK(draw.hi == 1);
	CHECK(draw.file == "LargeGroupAudioUpdate.cpp");
	CHECK(draw.line == 0xA7);
	CHECK(m->friend_getNextCallFrame() == gate + (UnsignedInt)draw.result + 3u + 1u);
	// the first wake: nothing changed, but the stored frame is not after the gate: the maps are told (update) with the stored state
	while (sink.events.empty() && f.logic->getFrame() < gate + 20)
	{
		f.logic->runLogicFrame();
	}
	REQUIRE(sink.events.size() == 1);
	CHECK(sink.events[0].kind == LargeGroupAudioEvent::UPDATE);
	CHECK(sink.events[0].storedFrame == gate);
	CHECK(sink.events[0].weight == 2);
	CHECK(sink.events[0].keys == &lgaData(*f.w.get("Grunt"))->m_keys);
	CHECK(m->storedFrame() > (std::int32_t)gate);
	// the next wakes with nothing changed: silent, one draw each
	f.logic->random().clearCallLog();
	for (int i = 0; i < 12; ++i)
	{
		f.logic->runLogicFrame();
	}
	CHECK(sink.events.size() == 1);
	size_t draws = 0;
	for (const auto &c : f.logic->random().callLog())
	{
		draws += c.file == "LargeGroupAudioUpdate.cpp" ? 1 : 0;
	}
	CHECK(draws >= 2);
	CHECK(draws <= 3);
	// a move: the maps get the stored and the new position
	const Coord3D from = *o->getPosition();
	Coord3D to = from;
	to.x += 50.0f;
	o->setPosition(&to);
	for (int i = 0; i < 6; ++i)
	{
		f.logic->runLogicFrame();
	}
	REQUIRE(sink.events.size() == 2);
	CHECK(sink.events[1].kind == LargeGroupAudioEvent::UPDATE);
	CHECK(sink.events[1].storedX == from.x);
	CHECK(sink.events[1].x == to.x);
	// deletion: remove (RW 0x8AF09D)
	f.logic->destroyObject(o);
	f.logic->runLogicFrame();
	REQUIRE(sink.events.size() == 3);
	CHECK(sink.events[2].kind == LargeGroupAudioEvent::REMOVE);
	link.setSink(nullptr);
}

namespace
{
const AnimationSoundClientBehaviorModuleData *footsteps(const ThingTemplate &tt)
{
	for (const auto &n : tt.clientBehaviorModules().nuggets())
	{
		if (const auto *d = dynamic_cast<const AnimationSoundClientBehaviorModuleData *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}

std::string loadSteps(logictest::LogicWorld &f, const std::string &name, const std::string &lines)
{
	return f.w.load("Object " + name + "\n  ClientBehavior = AnimationSoundClientBehavior ModuleTag_Steps\n" + lines + "  End\nEnd\n", INI_LOAD_OVERWRITE, name + ".ini");
}

struct FakeAnim : HAnimClass
{
	std::string name = "SKL.WALK";
	int frames = 30;
	const std::string &Get_Name() const override { return name; }
	const std::string &Get_HName() const override { return name; }
	int Get_Num_Frames() const override { return frames; }
	float Get_Frame_Rate() const override { return 30.0f; }
	int Get_Num_Pivots() const override { return 0; }
	void Get_Translation(Vector3 &, int, float) const override {}
	bool Get_Orientation(Quaternion &, int, float) const override { return false; }
	bool Get_Visibility(int, float) const override { return true; }
	float Get_Fade(int, float) const override { return 1.0f; }
	bool Is_Node_Motion_Present(int) const override { return false; }
	bool Frame_Is_Defined(int, float) const override { return true; }
};
} // namespace

TEST_CASE("audio4 footsteps: the AnimationSound line (RW 0x8CEBCB): Sound, RequiredMC / ExcludedMC, Animation / Frames groups into (animation, frame) order; the retail errors")
{
	logictest::LogicWorld f;
	ClientBehaviorModules::registerAll(f.w.modules);
	{
		// neither the test's check nor the world's table (an earlier test may have left the world's hook installed): a loud error
		const std::function<bool(const std::string &)> saved = TheContainParseHooks().audioEventExists;
		TheContainParseHooks().audioEventExists = nullptr;
		CHECK(loadSteps(f, "NoTable", "    AnimationSound = Sound:StepA Animation:SKL.WALK Frames:3\n").find("event table is not installed") != std::string::npos);
		TheContainParseHooks().audioEventExists = saved;
	}
	AnimationSoundClientBehaviorModuleData::soundExists() = [](const std::string &n) { return n == "StepA" || n == "StepB" || n == "RequiredMC"; };
	REQUIRE(loadSteps(f, "Walker",
				"    MaxUpdateRangeCap = 800\n"
				"    AnimationSound = Sound: StepB RequiredMC: MOVING ExcludedMC: DYING Animation:skl.walk Frames: 12 3 Animation:SKL.RUN Frames:1\n"
				"    AnimationSound = Sound:StepA\tAnimation:SKL.WALK\tFrames:3\n")
				.empty());
	const AnimationSoundClientBehaviorModuleData *d = footsteps(*f.w.get("Walker"));
	REQUIRE(d);
	CHECK(d->m_maxUpdateRangeCap == 800.0f);
	REQUIRE(d->m_entries.size() == 4);
	// ordered by (animation, frame); equal keys in line order
	CHECK(d->m_entries[0].animation == "SKL.RUN");
	CHECK(d->m_entries[1].animation == "SKL.WALK");
	CHECK(d->m_entries[1].frame == 3.0f);
	CHECK(d->m_entries[1].sound == "StepB");
	CHECK(d->m_entries[2].frame == 3.0f);
	CHECK(d->m_entries[2].sound == "StepA");
	CHECK(d->m_entries[3].frame == 12.0f);
	CHECK(d->m_entries[1].hasConditions);
	CHECK(d->m_entries[1].required.test(ModelCondition::indexOf("MOVING")));
	CHECK(d->m_entries[1].excluded.test(ModelCondition::indexOf("DYING")));
	CHECK_FALSE(d->m_entries[2].hasConditions);
	REQUIRE(loadSteps(f, "Plain", "    AnimationSound = Sound:StepA Animation:SKL.WALK Frames:3\n").empty());
	CHECK(footsteps(*f.w.get("Plain"))->m_maxUpdateRangeCap == 3.4028234663852886e+38f); // RW 0xBD1910
	// the retail errors
	auto err = [&](const char *line) { return loadSteps(f, "Bad", std::string("    AnimationSound = ") + line + "\n"); };
	CHECK(err("Sound: RequiredMC: MOVING").find("expected 'Animation' next, got 'MOVING'") != std::string::npos); // "RequiredMC" is the sound name here
	CHECK(err("Sound:StepA RequiredMC: FLYING_HIGH Animation:A Frames:1").find("unknown model condition 'FLYING_HIGH' in RequiredMC list") != std::string::npos);
	CHECK(err("Sound:StepA Frames:1").find("expected 'Animation' next, got 'Frames'") != std::string::npos);
	CHECK(err("Sound:StepA").find("expected 'Animation' next, got '<End of line>'") != std::string::npos);
	CHECK(err("Sound:StepA RequiredMC: MOVING").find("expected 'Animation' next, got '<End of line>'") != std::string::npos);
	CHECK(err("Sound:Nope Animation:A Frames:1").find("unknown sound 'Nope'") != std::string::npos);
	CHECK(err("Sound:StepA Animation:A Frames:1").empty());
	AnimationSoundClientBehaviorModuleData::soundExists() = nullptr;
}

TEST_CASE("audio4 footsteps: the frame windows of a track's step (RW 0x4C0CBC) and the entries a window crosses (RW 0x8CE8F7: lo <= frame <= hi, not the first end)")
{
	FakeAnim anim;
	W3DDrawTrack t;
	t.anim = &anim;
	AnimationSoundClientBehavior::Window a, b;
	t.prevFrame = 2.0f;
	t.frame = 4.0f;
	t.completed = false;
	AnimationSoundClientBehavior::frameWindows(t, 30, a, b);
	CHECK(a.a == 2.0f);
	CHECK(a.b == 4.0f);
	CHECK(b.a == b.b);
	// a loop that wrapped: (prev, frames) and (-1e-5, frame)
	t.mode = W3D_ANIM_MODE_LOOP;
	t.completed = true;
	t.prevFrame = 28.0f;
	t.frame = 1.0f;
	AnimationSoundClientBehavior::frameWindows(t, 30, a, b);
	CHECK(a.a == 28.0f);
	CHECK(a.b == 30.0f);
	CHECK(b.a == -9.999999747378752e-06f);
	CHECK(b.b == 1.0f);
	// backwards: (prev, 0) and (frames, frame)
	t.mode = W3D_ANIM_MODE_LOOP_BACKWARDS;
	t.prevFrame = 1.0f;
	t.frame = 28.0f;
	AnimationSoundClientBehavior::frameWindows(t, 30, a, b);
	CHECK(a.a == 1.0f);
	CHECK(a.b == 0.0f);
	CHECK(b.a == 30.0f);
	CHECK(b.b == 28.0f);
	// ping pong reflected at the end (now going back): (frames - 0.99999, frame); at the start (going forward): (-1e-5, frame)
	t.mode = W3D_ANIM_MODE_LOOP_PINGPONG;
	t.direction = -1;
	t.prevFrame = 28.5f;
	t.frame = 28.0f;
	AnimationSoundClientBehavior::frameWindows(t, 30, a, b);
	CHECK(a.a == 30.0f - 0.9999899864196777f);
	CHECK(a.b == 28.0f);
	t.direction = 1;
	t.prevFrame = 0.5f;
	t.frame = 1.0f;
	AnimationSoundClientBehavior::frameWindows(t, 30, a, b);
	CHECK(a.a == -9.999999747378752e-06f);
	CHECK(a.b == 1.0f);
	t.anim = nullptr;
	AnimationSoundClientBehavior::frameWindows(t, 30, a, b);
	CHECK(a.a == a.b);

	// the entries: frames 0, 3 (conditions: MOVING), 3, 12 of SKL.WALK
	std::vector<AnimationSoundEntry> entries(4);
	for (AnimationSoundEntry &e : entries)
	{
		e.animation = "SKL.WALK";
	}
	entries[0].frame = 0.0f;
	entries[0].sound = "Land";
	entries[1].frame = 3.0f;
	entries[1].sound = "StepMoving";
	entries[1].required.set(ModelCondition::indexOf("MOVING"));
	entries[1].hasConditions = true;
	entries[2].frame = 3.0f;
	entries[2].sound = "Step";
	entries[3].frame = 12.0f;
	entries[3].sound = "Step2";
	ModelConditionFlags idle, moving;
	moving.set(ModelCondition::indexOf("MOVING"));
	std::vector<const AnimationSoundEntry *> hits;
	AnimationSoundClientBehavior::entriesCrossed(entries, "SKL.WALK", { 2.0f, 4.0f }, idle, hits);
	REQUIRE(hits.size() == 1);
	CHECK(hits[0]->sound == "Step");
	hits.clear();
	AnimationSoundClientBehavior::entriesCrossed(entries, "SKL.WALK", { 2.0f, 4.0f }, moving, hits);
	REQUIRE(hits.size() == 2);
	CHECK(hits[0]->sound == "StepMoving");
	hits.clear();
	AnimationSoundClientBehavior::entriesCrossed(entries, "SKL.WALK", { 3.0f, 12.0f }, idle, hits); // the first end (3) does not play, the last (12) does
	REQUIRE(hits.size() == 1);
	CHECK(hits[0]->sound == "Step2");
	hits.clear();
	AnimationSoundClientBehavior::entriesCrossed(entries, "SKL.WALK", { -9.999999747378752e-06f, 1.0f }, idle, hits); // the wrap's second window plays frame 0
	REQUIRE(hits.size() == 1);
	CHECK(hits[0]->sound == "Land");
	hits.clear();
	AnimationSoundClientBehavior::entriesCrossed(entries, "SKL.RUN", { 0.0f, 30.0f }, idle, hits);
	CHECK(hits.empty());
}

TEST_CASE("audio4 footsteps retail: a Gondor soldier's death fall plays BodyFallGeneric1 at its frame through TheAnimationSoundModuleManager (RW 0x83F321 / 0x8CE75B)")
{
	if (!hudtest::haveWorld("audio4 footsteps retail"))
	{
		return;
	}
	hudtest::Rig game(hudtest::shared());
	retailtest::Mount *mount = hudtest::shared().mount;
	INIEnvironment env;
	env.fileSystem = mount->fs.get();
	AudioIniState ini;
	ini.registerBlocks(env.blocks);
	INI reader(env);
	ini.loadAll(reader);
	AudioAssetCache cache(mount->fs.get(), 32u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager mgr(ini, cache, device, RandomAlgorithm::ZH_CarryChain, 3);
	mgr.enableEventLog(true);
	LiveGameAudio att(*game.game, mgr);
	AnimationSoundModuleManager &steps = game.game->drawables().animationSounds();
	const Coord3D p{ 1200.0f, 1200.0f, 0.0f };
	std::string error;
	Object *soldier = game.game->createObject("GondorFighter", game.index(), p, 0.0f, &error);
	REQUIRE_MESSAGE(soldier != nullptr, error);
	mgr.setListenerPosition(*soldier->getPosition(), Coord3D{ 0.0f, 1.0f, 0.0f });
	double t = 0.0;
	auto frames = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			game.logic().runLogicFrame();
			for (int c = 0; c < 6; ++c) // 30 client frames per second
			{
				game.game->refreshClient(1000.0 / 30.0, (double)c / 6.0);
				t += 1000.0 / 30.0;
				device.setTime(t);
				mgr.update(t);
			}
		}
	};
	frames(5);
	REQUIRE(steps.moduleCount() >= 1);
	CHECK(steps.stats().updates > 0);
	CHECK(steps.stats().moduleUpdates > 0);
	const std::uint64_t before = steps.stats().played;
	soldier->kill(0);
	frames(40); // 8 s: every death animation of the template (DIEB .. DIEE, LNDA) has its BodyFallGeneric1 frame well inside
	CHECK(steps.stats().played > before);
	bool fall = false;
	for (const AudioLogEntry &e : mgr.takeEventLog())
	{
		fall = fall || e.event == "BodyFallGeneric1";
	}
	CHECK(fall);
}

TEST_CASE("audio4 U21 live: a live game installs the player filter and the shroud query (RW 0x452003 -> 0xB4FB20 through the snapshot's ShroudView); a SHROUDED sound in the shroud is culled")
{
	if (!hudtest::haveWorld("audio4 U21 live"))
	{
		return;
	}
	hudtest::Rig game(hudtest::shared());
	Audio a("AudioEvent Hidden\n  Sounds = a\n  Type = world everyone shrouded\n  MaxRange = 100000\n  Limit = 0\nEnd\n");
	a.mgr->setIdleWorldQueries(AudioWorldQueries::noGamePlayerList());
	{
		LiveGameAudio att(*game.game, *a.mgr);
		std::string error;
		REQUIRE_MESSAGE(game.game->createObject("GondorFighter", game.index(), Coord3D{ 1200.0f, 1200.0f, 0.0f }, 0.0f, &error) != nullptr, error); // its vision clears cells
		for (int i = 0; i < 5; ++i)
		{
			game.logic().runLogicFrame();
		}
		game.game->refreshClient(1000.0 / 30.0, 0.0); // publishes the snapshot the queries read
		const std::shared_ptr<const LogicSnapshot> snap = game.game->latestSnapshot();
		REQUIRE(snap.get() != nullptr);
		REQUIRE(snap->shroud.get() != nullptr);
		REQUIRE(snap->players.get() != nullptr);
		// a clear cell and a shrouded cell of the local player's view
		Coord3D clear{}, hidden{};
		bool haveClear = false, haveHidden = false;
		const ShroudView &v = *snap->shroud;
		for (int y = 0; y < v.countY && !(haveClear && haveHidden); ++y)
		{
			for (int x = 0; x < v.countX; ++x)
			{
				const Coord3D c{ v.originX + ((float)x + 0.5f) * v.cellSize, v.originY + ((float)y + 0.5f) * v.cellSize, 0.0f };
				if (!haveClear && v.cellStatus(x, y) == CELLSHROUD_CLEAR)
				{
					clear = c;
					haveClear = true;
				}
				if (!haveHidden && v.cellStatus(x, y) == CELLSHROUD_SHROUDED)
				{
					hidden = c;
					haveHidden = true;
				}
			}
		}
		REQUIRE(haveClear);
		REQUIRE(haveHidden);
		a.mgr->setListenerPosition(clear, Coord3D{ 0.0f, 1.0f, 0.0f });
		CHECK(a.mgr->playSoundAt("Hidden", clear) >= AHSV_FirstHandle);
		CHECK(a.mgr->playSoundAt("Hidden", hidden) == AHSV_NoSound);
		CHECK(a.mgr->report().culledShroud == 1);
		CHECK(a.mgr->report().missingHooks.empty());
	}
	// the attachment is gone: the shell's list is back (an EVERYONE sound needs no query; nothing is reported missing)
	CHECK(a.mgr->playSoundForPlayer("Hidden", -1) >= AHSV_FirstHandle);
	CHECK(a.mgr->report().missingHooks.empty());
}

TEST_CASE("audio4 review r1: a Living World event is never shrouded (RW 0x452003 asks the shroud for the tactical view only); TheLargeGroupAudio's gate is hashed")
{
	Audio a("AudioEvent Hidden\n  Sounds = a\n  Type = world everyone shrouded\n  MaxRange = 100000\n  Limit = 0\nEnd\n");
	AudioWorldQueries q = AudioWorldQueries::noGamePlayerList();
	q.localPlayerIndex = [] { return 0; };
	q.isShroudClear = [](int, const Coord3D &) { return false; }; // everything shrouded
	a.mgr->setWorldQueries(q);
	a.mgr->setListenerPosition(Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 0.0f, 1.0f, 0.0f });
	CHECK(a.mgr->playSoundAt("Hidden", Coord3D{ 10.0f, 0.0f, 0.0f }) == AHSV_NoSound); // tactical: shrouded
	CHECK(a.mgr->report().culledShroud == 1);
	AudioEventRTS lw("Hidden", Coord3D{ 10.0f, 0.0f, 0.0f }, VIEW_LIVING_WORLD);
	CHECK(a.mgr->addAudioEvent(lw) >= AHSV_FirstHandle);
	CHECK(a.mgr->report().culledShroud == 1);

	logictest::LogicWorld f;
	const std::uint32_t before = f.logic->computeStateHash();
	f.logic->largeGroupAudio().enable(f.logic->getFrame()); // RW 0x60D4D3: + 0x38 cleared, + 0x3C the frame
	const std::uint32_t enabled = f.logic->computeStateHash();
	CHECK(enabled != before);
	f.logic->largeGroupAudio().enable(f.logic->getFrame() + 1);
	CHECK(f.logic->computeStateHash() != enabled);
}

TEST_CASE("audio4 footsteps review r1: a drawable made before the audio attached gets its update range when the audio arrives, and its death fall plays")
{
	if (!hudtest::haveWorld("audio4 footsteps r1"))
	{
		return;
	}
	hudtest::Rig game(hudtest::shared());
	retailtest::Mount *mount = hudtest::shared().mount;
	const Coord3D p{ 1200.0f, 1200.0f, 0.0f };
	std::string error;
	Object *soldier = game.game->createObject("GondorFighter", game.index(), p, 0.0f, &error); // before any audio
	REQUIRE_MESSAGE(soldier != nullptr, error);
	game.logic().runLogicFrame();
	game.game->refreshClient(1000.0 / 30.0, 0.0); // the drawable (and its module, range 0: no audio) exists now
	auto stepsModule = [&]() -> AnimationSoundClientBehavior * {
		Drawable *d = game.game->drawables().findByObject(soldier->getID());
		if (!d)
		{
			return nullptr;
		}
		for (const auto &m : d->clientModules())
		{
			if (auto *a = dynamic_cast<AnimationSoundClientBehavior *>(m.get()))
			{
				return a;
			}
		}
		return nullptr;
	};
	REQUIRE(stepsModule() != nullptr);
	CHECK(stepsModule()->rangeSquared() == 0.0f);
	INIEnvironment env;
	env.fileSystem = mount->fs.get();
	AudioIniState ini;
	ini.registerBlocks(env.blocks);
	INI reader(env);
	ini.loadAll(reader);
	AudioAssetCache cache(mount->fs.get(), 32u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager mgr(ini, cache, device, RandomAlgorithm::ZH_CarryChain, 3);
	mgr.enableEventLog(true);
	LiveGameAudio att(*game.game, mgr);
	AnimationSoundModuleManager &steps = game.game->drawables().animationSounds();
	mgr.setListenerPosition(*soldier->getPosition(), Coord3D{ 0.0f, 1.0f, 0.0f });
	double t = 0.0;
	auto frames = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			game.logic().runLogicFrame();
			for (int c = 0; c < 6; ++c)
			{
				game.game->refreshClient(1000.0 / 30.0, (double)c / 6.0);
				t += 1000.0 / 30.0;
				device.setTime(t);
				mgr.update(t);
			}
		}
	};
	frames(3);
	// the range was taken when the audio arrived (RW 0x8CE4CC .. 0x8CE531: the sounds' largest MaxRange, at most GondorFighter's MaxUpdateRangeCap 800)
	REQUIRE(stepsModule() != nullptr);
	CHECK(stepsModule()->rangeSquared() > 0.0f);
	CHECK(stepsModule()->rangeSquared() <= 800.0f * 800.0f);
	soldier->kill(0);
	frames(40);
	CHECK(steps.stats().played > 0);
	bool fall = false;
	for (const AudioLogEntry &e : mgr.takeEventLog())
	{
		fall = fall || e.event == "BodyFallGeneric1";
	}
	CHECK(fall);
}
