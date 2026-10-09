// OpenBFME. GPL-3.0. See LiveGameAudio.h.

#include "GameClient/LiveGameAudio.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include "Common/AsciiString.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/RawModuleData.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/AnimationSoundClientBehavior.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/Eva.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MusicScripts.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LargeGroupAudioLink.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

// Common/ModelState.h cannot be included next to Object.h (two ModelConditionFlags typedefs); the one function used is declared here.
namespace ModelCondition
{
int indexOf(const std::string &name);
}

namespace
{
std::string soundField(const ThingTemplate &tt, const char *name)
{
	// RW 0x73AD07 -> 0x73ACAF: the sound rows of the audio table; "NoSound" is none
	const FieldValue *f = tt.findField(name);
	const RawTokens *raw = f ? std::get_if<RawTokens>(f) : nullptr;
	if (!raw || raw->tokens.empty() || AsciiStringUtil::compareNoCase(raw->tokens.front(), "NoSound") == 0)
	{
		return std::string();
	}
	return raw->tokens.front();
}

AudioRelationship toAudio(Relationship r)
{
	return r == ALLIES ? AR_ALLIES : r == ENEMIES ? AR_ENEMIES : AR_NEUTRAL;
}
} // namespace

// Lane AUDIO-4: the client half of TheLargeGroupAudio's member calls. The LargeGroupAudioUpdate modules (logic, GameLogic/Module/LargeGroupAudioUpdate.h)
// call add / update / remove inside the logic frame (RW 0x60D5FF / 0x60D5CB / 0x60D633, through LargeGroupAudioLink); the sink queues each call with the member
// state the maps read during it, and the render side applies them in order at a worker-idle point. TARGET FACTS of a map's three calls (caveat S-001):
//   * a member qualifies for a map when its model condition flags hold the map's Required and none of its Excluded flags (RW 0x7EEEDF -> 0x5DF56A, map + 0x28 /
//     + 0x74), its status bits likewise (RW 0x7EEEF3), and, with IgnoreStealthedUnits (map + 0xE5), it is not stealthed (RW 0x694C0D, no viewer);
//   * add (RW 0x7EF032): qualifying with its current state: every Sound block of the map adds it (RW 0x971326, which keeps it only when one of its keys is one of
//     the block's, RW 0x970617);
//   * update (RW 0x7EEF0D): "new" = qualifying with the current state, "old" = qualifying with the stored state AND its stored frame after TheLargeGroupAudio
//     + 0x3C (a member last told at or before the gate was never counted); both: an unchanged x, y does nothing, else every block moves it (RW 0x971157);
//     new only: add (RW 0x971326); old only: remove (RW 0x971392);
//   * remove (RW 0x7EF0A1): "old" as above: every block removes it (RW 0x971392).
// What a Sound block does with its members (the cells, thresholds, hand-off, ducking: RW 0x9712C9 / 0x970326 / 0x970E81 ...) is not decoded (S-248):
// largeGroupUpdate below counts the members per cell by the INI's documented rules.
struct LiveGameAudio::LgaRuntime : public LargeGroupAudioSink
{
	struct Member
	{
		float x = 0.0f, y = 0.0f;
		std::uint16_t weight = 1;
	};
	// (map name, Sound block name) -> object -> member
	std::map<std::pair<std::string, std::string>, std::map<ObjectID, Member>> members;
	std::vector<LargeGroupAudioEvent> pending;
	std::vector<std::int32_t> pendingGate; ///< TheLargeGroupAudio + 0x3C at each queued call
	const LargeGroupAudioLink *link = nullptr;
	std::uint64_t adds = 0, moves = 0, removes = 0, events = 0;

	void onLargeGroupAudioEvent(const LargeGroupAudioEvent &event) override
	{
		// on the simulation owner: capture only (the sounds belong to the render side)
		pending.push_back(event);
		pendingGate.push_back(link ? link->gateFrame() : -1);
	}

	static bool masks(const LargeGroupAudioMap &map, const ModelConditionMask &mc, const ObjectStatusMaskType &st)
	{
		for (size_t w = 0; w < mc.size(); ++w)
		{
			if ((mc[w] & map.requiredModelConditionFlags[w]) != map.requiredModelConditionFlags[w] || (mc[w] & map.excludedModelConditionFlags[w]) != 0)
			{
				return false;
			}
		}
		for (size_t w = 0; w < st.size(); ++w)
		{
			if ((st[w] & map.requiredObjectStatusBits[w]) != map.requiredObjectStatusBits[w] || (st[w] & map.excludedObjectStatusBits[w]) != 0)
			{
				return false;
			}
		}
		return true;
	}
	static bool keyed(const LargeGroupAudioSound &snd, const std::vector<std::string> *keys)
	{
		if (!keys)
		{
			return false;
		}
		for (const std::string &k : snd.keys)
		{
			for (const std::string &mk : *keys)
			{
				if (k == mk) // RW 0x970617: the interned keys (exact bytes, RW 0x7EE97A)
				{
					return true;
				}
			}
		}
		return false;
	}
	void add(const LargeGroupAudioMap &map, const LargeGroupAudioEvent &e)
	{
		for (const LargeGroupAudioSound &snd : map.sounds)
		{
			if (keyed(snd, e.keys))
			{
				members[{ map.name, snd.name }][e.object] = Member{ e.x, e.y, e.weight };
				++adds;
			}
		}
	}
	void move(const LargeGroupAudioMap &map, const LargeGroupAudioEvent &e)
	{
		for (const LargeGroupAudioSound &snd : map.sounds)
		{
			auto it = members.find({ map.name, snd.name });
			if (it != members.end() && it->second.count(e.object))
			{
				it->second[e.object] = Member{ e.x, e.y, e.weight };
				++moves;
			}
		}
	}
	void remove(const LargeGroupAudioMap &map, const LargeGroupAudioEvent &e)
	{
		for (const LargeGroupAudioSound &snd : map.sounds)
		{
			auto it = members.find({ map.name, snd.name });
			if (it != members.end() && it->second.erase(e.object))
			{
				++removes;
			}
		}
	}
	void apply(const LargeGroupAudioStore &store)
	{
		for (size_t i = 0; i < pending.size(); ++i)
		{
			const LargeGroupAudioEvent &e = pending[i];
			const std::int32_t gate = pendingGate[i];
			++events;
			for (const LargeGroupAudioMap &map : store.maps())
			{
				const bool nowQ = masks(map, e.conditions, e.status) && !(map.ignoreStealthedUnits && e.stealthed);
				const bool oldQ = masks(map, e.storedConditions, e.storedStatus) && !(map.ignoreStealthedUnits && e.storedStealthed) && (std::int32_t)e.storedFrame > gate;
				switch (e.kind)
				{
					case LargeGroupAudioEvent::ADD:
						if (nowQ)
						{
							add(map, e);
						}
						break;
					case LargeGroupAudioEvent::UPDATE:
						if (nowQ && oldQ)
						{
							if (!(e.x == e.storedX && e.y == e.storedY))
							{
								move(map, e);
							}
						}
						else if (nowQ)
						{
							add(map, e);
						}
						else if (oldQ)
						{
							remove(map, e);
						}
						break;
					case LargeGroupAudioEvent::REMOVE:
						if (oldQ)
						{
							remove(map, e);
						}
						break;
				}
			}
		}
		pending.clear();
		pendingGate.clear();
	}
};

class LiveGameAudio::Resolver : public AudioOwnerResolver
{
public:
	explicit Resolver(const LiveGameAudio &owner) : m_owner(owner) {}
	bool objectPosition(std::uint32_t id, Coord3D *pos) const override { return m_owner.objectPosition(id, pos); }
	bool drawablePosition(std::uint32_t id, Coord3D *pos) const override { return m_owner.drawablePosition(id, pos); }

private:
	const LiveGameAudio &m_owner;
};

std::unique_ptr<LiveGameAudio> LiveGameAudio::attachToInstalled(LiveGame &game, ArchiveFileSystem *fs, std::string *error, std::string *evaSide)
{
	AudioManager *audio = AudioApi::current();
	if (!audio)
	{
		if (error)
		{
			*error = "no audio manager installed (boot a GameAudio first)";
		}
		return nullptr;
	}
	std::unique_ptr<LiveGameAudio> out(new LiveGameAudio(game, *audio));
	// TheEva speaks for the local player's side (Eva.ini SideSound Side = the PlayerTemplate's Side)
	if (Eva *eva = AudioApi::currentEva())
	{
		if (const Player *local = game.players().getLocalPlayer())
		{
			eva->setLocalSide(local->getSide());
			eva->setLocalPlayerIndex(local->getPlayerIndex());
			if (evaSide)
			{
				*evaSide = local->getSide();
			}
		}
	}
	if (fs)
	{
		out->startMusic(*fs);
	}
	return out;
}

void LiveGameAudio::startMusic(ArchiveFileSystem &fs)
{
	// S-708 / S-710: the in-game music is the map AudioSettings MusicScriptLibraryName names (its scripts are the music system)
	std::string path = m_audio.ini().settings.musicScriptLibraryName;
	if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
	{
		path = path.substr(1, path.size() - 2);
	}
	m_music = std::make_unique<MusicScripts>(m_audio, m_game.logic(), m_game.players(), AudioApi::currentEva());
	m_musicError.clear();
	if (path.empty())
	{
		m_musicError = "AudioSettings MusicScriptLibraryName is empty: no in-game music";
		m_music.reset();
		return;
	}
	if (!m_music->load(fs, path, &m_musicError))
	{
		m_music.reset();
	}
}

LiveGameAudio::ApiCounters LiveGameAudio::apiCounters()
{
	ApiCounters c;
	c.callsWithoutAudio = AudioApi::callsWithoutAudio();
	c.callsWithoutEva = AudioApi::callsWithoutEva();
	c.unitVoicesWithoutHandler = AudioApi::unitVoicesWithoutHandler();
	return c;
}

LiveGameAudio::LiveGameAudio(LiveGame &game, AudioManager &audio)
	: m_game(game)
	, m_audio(audio)
	, m_resolver(new Resolver(*this))
	, m_audioAlive(audio.lifetimeToken())
{
	m_audio.setOwnerResolver(m_resolver.get());
	// SMOOTH-1 (merge with AUDIO-2): the audio manager asks these at any time on the render side (its update), also while the logic worker runs a frame:
	// they read the players of the latest published snapshot, never the live player list
	AudioWorldQueries q;
	LiveGame *lg = &m_game;
	q.localPlayerIndex = [lg]() {
		const std::shared_ptr<const LogicSnapshot> s = lg->latestSnapshot();
		return s && s->players ? s->players->localIndex : -1;
	};
	q.playerExists = [lg](int index) {
		const std::shared_ptr<const LogicSnapshot> s = lg->latestSnapshot();
		return s && s->players && index >= 0 && index < s->players->count;
	};
	q.relationship = [lg](int owner, int local) {
		const std::shared_ptr<const LogicSnapshot> s = lg->latestSnapshot();
		if (!s || !s->players || owner < 0 || local < 0 || owner >= s->players->count || local >= s->players->count)
		{
			return AR_NEUTRAL;
		}
		// ZH AudioManager::shouldPlayLocally: the owner's relationship TO the local player
		return toAudio((Relationship)s->players->relationshipOf(owner, local));
	};
	// lane AUDIO-4 (QA-1 U21): RotWK's shroud test RW 0x452003: ThePartitionManager's status (RW 0xB4D9A0 -> 0xB4FB20) for the local player at the
	// position is not CLEAR: shrouded. The snapshot's ShroudView is that status for the local player (ShroudView::statusAt, RW 0xB4FB20); a game
	// without a shroud manager has no view, which is retail's ThePartitionManager == 0 branch (RW 0x452024): not shrouded
	q.isShroudClear = [lg](int playerIndex, const Coord3D &pos) {
		const std::shared_ptr<const LogicSnapshot> s = lg->latestSnapshot();
		if (!s || !s->shroud)
		{
			return true;
		}
		if (s->shroud->localPlayer != playerIndex)
		{
			return false; // the view holds the local player's cells only (callers ask for the local player; -1: no local player, no clear cell)
		}
		return s->shroud->statusAt(pos.x, pos.y) == CELLSHROUD_CLEAR;
	};
	m_audio.bindWorldQueries(q, this);
	// lane AUDIO-4: the footsteps' manager plays on this attachment's manager and finds a drawable's object like the owner resolver does
	AnimationSoundModuleManager &footsteps = m_game.drawables().animationSounds();
	AudioManager *audioPtr = &m_audio;
	footsteps.audio = [audioPtr]() { return audioPtr; };
	footsteps.objectPosition = [this](std::uint32_t id, Coord3D *pos) { return objectPosition(id, pos); };
	// lane AUDIO-4: TheLargeGroupAudio's member calls from the logic
	m_lga = std::make_unique<LgaRuntime>();
	m_lga->link = &m_game.logic().largeGroupAudio();
	m_game.logic().largeGroupAudio().setSink(m_lga.get());
}

bool LiveGameAudio::audioAlive() const
{
	return !m_audioAlive.expired();
}

LiveGameAudio::~LiveGameAudio()
{
	if (m_game.logic().largeGroupAudio().sink() == m_lga.get())
	{
		m_game.logic().largeGroupAudio().setSink(nullptr); // lane AUDIO-4
	}
	m_game.drawables().animationSounds().audio = nullptr; // lane AUDIO-4: the footsteps stop with this attachment
	m_game.drawables().animationSounds().objectPosition = nullptr;
	// review r1 fix 1: the manager's own lifetime decides, not whether it is the installed one. A destroyed manager is not touched; a surviving one (even
	// when another manager is installed now) loses exactly this attachment's resolver, queries and sounds
	if (m_logFile)
	{
		if (audioAlive())
		{
			flushEventLog();
			m_audio.enableEventLog(false);
		}
		std::fclose(m_logFile);
	}
	if (!audioAlive())
	{
		return;
	}
	for (auto &kv : m_ambients)
	{
		stopAmbient(kv.first, kv.second);
	}
	for (auto &kv : m_groups)
	{
		m_audio.removeAudioEvent(kv.second.handle);
	}
	m_audio.clearOwnerResolver(m_resolver.get());
	m_audio.clearWorldQueries(this);
}

bool LiveGameAudio::objectPosition(std::uint32_t objectId, Coord3D *pos) const
{
	// SMOOTH-1 (merge with AUDIO-2): the audio manager follows its owners on the render side at any time: the object's drawable (render side, where it is
	// drawn), else its record in the latest published snapshot; never the live object (the logic worker may be running)
	if (const Drawable *d = m_game.drawables().findByObject((ObjectID)objectId))
	{
		*pos = *d->getPosition();
		return true;
	}
	const std::shared_ptr<const LogicSnapshot> s = m_game.latestSnapshot();
	const ObjectSnapshot *rec = s ? s->find((ObjectID)objectId) : nullptr;
	if (!rec)
	{
		return false; // gone (or never published)
	}
	*pos = rec->position;
	return true;
}

bool LiveGameAudio::drawablePosition(std::uint32_t drawableId, Coord3D *pos) const
{
	const Drawable *d = m_game.drawables().find((DrawableID)drawableId);
	if (!d || d->getObjectID() == INVALID_ID)
	{
		return false; // a drawable without an object (client only props carry no sounds here)
	}
	*pos = *d->getPosition(); // SMOOTH-1: the drawable's own (render side) position
	return true;
}

const std::string &LiveGameAudio::cachedAmbientSoundFor(const Object &obj)
{
	// lane PERF-1 r2: ambientSoundFor reads the template's sound rows (field lookups by name); its result depends on the template and the damage state
	// alone, so update() asks it once per pair
	BodyDamageType dt = BODY_PRISTINE;
	if (const BodyModuleInterface *body = obj.getBodyModule())
	{
		dt = body->getDamageState();
	}
	const auto key = std::make_pair(obj.getTemplate(), (int)dt);
	auto it = m_ambientByTemplate.find(key);
	if (it == m_ambientByTemplate.end())
	{
		it = m_ambientByTemplate.emplace(key, ambientSoundFor(obj)).first;
	}
	return it->second;
}

std::string LiveGameAudio::ambientSoundFor(const Object &obj)
{
	// ZH Drawable::startAmbientSound(dt): the sound of the damage state, else for DAMAGED / REALLYDAMAGED the pristine one (RUBBLE has no fallback)
	const ThingTemplate &tt = *obj.getTemplate();
	BodyDamageType dt = BODY_PRISTINE;
	if (const BodyModuleInterface *body = obj.getBodyModule())
	{
		dt = body->getDamageState();
	}
	static const char *const kField[BODYDAMAGETYPE_COUNT] = { "SoundAmbient", "SoundAmbientDamaged", "SoundAmbientReallyDamaged", "SoundAmbientRubble" };
	std::string s = soundField(tt, kField[dt]);
	if (s.empty() && dt != BODY_PRISTINE && dt != BODY_RUBBLE)
	{
		s = soundField(tt, kField[BODY_PRISTINE]);
	}
	return s;
}

void LiveGameAudio::stopAmbient(ObjectID, Ambient &a)
{
	if (a.handle >= AHSV_FirstHandle)
	{
		m_audio.removeAudioEvent(a.handle);
		++m_stats.ambientStopped;
	}
	a.handle = 0;
	a.tried = false;
}

std::string LiveGameAudio::evaField(const Object &obj, const char *field)
{
	// the template's Eva event rows (RW 0x5DE588: an Eva event name; "" = none)
	const FieldValue *f = obj.getTemplate()->findField(field);
	if (const RawTokens *raw = f ? std::get_if<RawTokens>(f) : nullptr)
	{
		return raw->tokens.empty() ? std::string() : raw->tokens.front();
	}
	if (const std::string *str = f ? std::get_if<std::string>(f) : nullptr)
	{
		return *str;
	}
	return std::string();
}

void LiveGameAudio::evaWatch(const Object &o)
{
	// INFERENCE (S-709): retail reports these from the logic's damage and death code (the call sites were not decoded); here the client watches the body:
	// a health drop of a local player's object reports EvaEventDamagedOwner at its position, a death EvaEventDieOwner / Ally / Enemy by the relationship of the
	// object's player to the local player. Eva's own TimeBetweenEventsMS / QuietTime rules (Eva.ini) throttle the repeats.
	const BodyModuleInterface *body = o.getBodyModule();
	if (!body)
	{
		return;
	}
	const Player *local = m_game.players().getLocalPlayer();
	const Player *owner = o.getControllingPlayer();
	if (!local || !owner)
	{
		return;
	}
	EvaWatch &w = m_eva[o.getID()];
	const float health = body->getHealth();
	const bool dead = o.isEffectivelyDead() || o.isDestroyed();
	if (w.known && !w.dead)
	{
		if (dead)
		{
			const Relationship r = owner == local ? ALLIES : owner->getRelationship(local);
			const char *field = owner == local ? "EvaEventDieOwner" : r == ALLIES ? "EvaEventDieAlly" : r == ENEMIES ? "EvaEventDieEnemy" : nullptr;
			const std::string ev = field ? evaField(o, field) : std::string();
			if (!ev.empty())
			{
				AudioApi::reportEva(ev, o.getPosition());
				++m_stats.evaDeaths;
			}
		}
		else if (health < w.health && owner == local)
		{
			const std::string ev = evaField(o, "EvaEventDamagedOwner");
			if (!ev.empty())
			{
				AudioApi::reportEva(ev, o.getPosition());
				++m_stats.evaDamaged;
			}
		}
	}
	w.known = true;
	w.dead = dead;
	w.health = health;
}

void LiveGameAudio::largeGroupUpdate()
{
	// INFERENCE (S-248): the per-cell rules EA documents at the top of LargeGroupAudio.ini; RW's Sound block runtime is not decoded. Per map and Sound block:
	// the block's members (LgaRuntime, from the modules' calls) are counted (UnitWeight each) in a grid of Size cells; a cell starts its sound at
	// StartThreshold and stops below StopThreshold; the sound sits at the cell's weighted centre and moves toward it at most MaximumAudioSpeed per logic
	// frame. Ducking, hand-off between cells and the burning-cell key are not ported.
	const LargeGroupAudioStore &store = m_audio.ini().largeGroupAudio;
	m_lga->apply(store);
	if (store.maps().empty())
	{
		return;
	}
	std::map<std::string, bool> live;
	for (const LargeGroupAudioMap &map : store.maps())
	{
		if (map.size <= 0.0f)
		{
			continue;
		}
		for (const LargeGroupAudioSound &snd : map.sounds)
		{
			if (snd.sound.empty())
			{
				continue;
			}
			std::map<std::pair<int, int>, std::pair<int, Coord3D>> cells; // cell -> weight, weighted position sum
			const auto group = m_lga->members.find({ map.name, snd.name });
			if (group != m_lga->members.end())
			{
				for (const auto &mv : group->second)
				{
					const LgaRuntime::Member &m = mv.second;
					// the height of the sound: the member's drawable (the maps keep x, y only)
					Coord3D at{ m.x, m.y, 0.0f };
					Coord3D live;
					if (objectPosition((std::uint32_t)mv.first, &live))
					{
						at.z = live.z;
					}
					const std::pair<int, int> cell((int)std::floor(m.x / map.size), (int)std::floor(m.y / map.size));
					auto &c = cells[cell];
					c.first += m.weight;
					c.second.x += at.x * (float)m.weight;
					c.second.y += at.y * (float)m.weight;
					c.second.z += at.z * (float)m.weight;
				}
			}
			for (const auto &kv : cells)
			{
				const std::string key = map.name + "/" + snd.name + "/" + std::to_string(kv.first.first) + "," + std::to_string(kv.first.second);
				auto it = m_groups.find(key);
				const int weight = kv.second.first;
				const Coord3D centre{ kv.second.second.x / (float)weight, kv.second.second.y / (float)weight, kv.second.second.z / (float)weight };
				// qualification (the start / stop thresholds) is kept apart from playback (review r2 fix 4)
				if (it == m_groups.end())
				{
					if (weight < (int)map.startThreshold)
					{
						continue;
					}
					Group g;
					g.position = centre;
					it = m_groups.emplace(key, g).first;
				}
				else if (weight < (int)map.stopThreshold)
				{
					continue; // dropped below: not marked live, stopped below
				}
				// the sound follows the group's centre at MaximumAudioSpeed (world units per logic frame)
				Group &g = it->second;
				const float dx = centre.x - g.position.x, dy = centre.y - g.position.y;
				const float d = std::sqrt(dx * dx + dy * dy);
				const float step = map.maximumAudioSpeed;
				if (d > step && d > 0.0f)
				{
					g.position.x += dx * step / d;
					g.position.y += dy * step / d;
				}
				else
				{
					g.position = centre;
				}
				g.position.z = centre.z;
				// playback: a looping sound that is neither queued nor playing (culled by distance, finished, refused) is started again at the current position;
				// a one-shot sound plays once per qualification. The playing / pending event follows the moving position (review r2 fix 2)
				const auto info = m_audio.ini().infos.find(snd.sound);
				const bool looping = info && (info->control & AC_LOOP);
				const bool alive = g.handle >= AHSV_FirstHandle && m_audio.isCurrentlyPlaying(g.handle);
				if (!alive && (looping || !g.startedOnce))
				{
					AudioLog::Scope logScope([&] { return "LargeGroupAudio " + key; });
					const AudioHandle h = m_audio.playSoundAt(snd.sound, g.position);
					g.startedOnce = true;
					g.handle = h >= AHSV_FirstHandle ? h : 0;
					if (g.handle)
					{
						++m_stats.largeGroupStarted;
					}
				}
				else if (alive)
				{
					m_audio.setEventPosition(g.handle, g.position);
				}
				live[key] = true;
			}
		}
	}
	for (auto it = m_groups.begin(); it != m_groups.end();)
	{
		if (!live.count(it->first))
		{
			if (it->second.handle)
			{
				m_audio.removeAudioEvent(it->second.handle);
			}
			++m_stats.largeGroupStopped;
			it = m_groups.erase(it);
		}
		else
		{
			++it;
		}
	}
	m_stats.largeGroupPlaying = 0;
	for (const auto &kv : m_groups)
	{
		m_stats.largeGroupPlaying += kv.second.handle && m_audio.isCurrentlyPlaying(kv.second.handle) ? 1 : 0;
	}
	m_stats.largeGroupQualified = m_groups.size();
	m_stats.largeGroupEvents = m_lga->events;
	m_stats.largeGroupMembers = 0;
	for (const auto &kv : m_lga->members)
	{
		m_stats.largeGroupMembers += kv.second.size();
	}
}

void LiveGameAudio::drainDeferredAudio()
{
	AudioApi::drainDeferred();
}

bool LiveGameAudio::applyScriptRequest(const ScriptClientRequest &r)
{
	const std::string &a = r.action;
	const bool sound = a == "PLAY_SOUND_EFFECT" || a == "SPEECH_PLAY" || a == "SOUND_PLAY_NAMED" || a == "PLAY_SOUND_EFFECT_AT";
	const bool music = a == "MUSIC_SET_TRACK" || a == "MUSIC_PLAY_TRACK_FINITE_TIMES";
	const bool volume = a == "MUSIC_SET_VOLUME" || a == "SOUND_SET_VOLUME" || a == "SPEECH_SET_VOLUME";
	if (!sound && !music && !volume)
	{
		return false;
	}
	if (volume)
	{
		// lane SCRIPT-3: RW 0x7CCFBD / 0x7CCFD0 / 0x7CCFE3 -> RW 0x7BCAAE(affect, percent): percent * 0.01 clamped to [0, 1], TheAudio vslot 0xEC
		// (setVolume(v, affect, 0): the script volume, not the options sliders); affect 1 music, 0x16 sound (2D | 3D | ambient), 8 speech
		if (audioAlive() && !r.params.empty())
		{
			float v = r.params[0].realValue * 0.01f;
			v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
			const unsigned affect = a == "MUSIC_SET_VOLUME" ? 0x01u : (a == "SOUND_SET_VOLUME" ? 0x16u : 0x08u);
			m_audio.setVolume(v, affect);
			++m_scriptAudio.volumes;
		}
		return true;
	}
	if (!audioAlive() || r.params.empty())
	{
		return true;
	}
	const std::string &name = r.params[0].stringValue;
	if (music)
	{
		// RW 0x7C0D9B: the playing music stops (TheAudio vslot 0x8C, faded when the fade-out parameter is set), the track by name starts, faded in
		// when the fade-in parameter is set (event + shouldFade, RW 0x6DA774). MUSIC_SET_TRACK (track, fadeOut, fadeIn); MUSIC_PLAY_TRACK_FINITE_TIMES
		// (track, count, fadeOut, fadeIn). INFERENCE (S-1182): the count and the notify name of the finite form are not applied
		const size_t fo = a == "MUSIC_SET_TRACK" ? 1 : 2;
		const bool fadeOut = r.params.size() > fo && r.params[fo].intValue != 0;
		const bool fadeIn = r.params.size() > fo + 1 && r.params[fo + 1].intValue != 0;
		m_audio.stopMusic(fadeOut);
		if (m_audio.playMusic(name, fadeIn) == 0)
		{
			++m_scriptAudio.unknownEvents;
		}
		else
		{
			++m_scriptAudio.played;
		}
		return true;
	}
	AudioEventRTS event(name);
	const Player *local = m_game.logic().players().getLocalPlayer();
	if (a == "SOUND_PLAY_NAMED")
	{
		const Object *o = r.objectId ? m_game.logic().findObjectByID(r.objectId) : nullptr;
		if (!o)
		{
			return true; // RW 0x7BE239: no object, no sound
		}
		event.setObjectID(o->getID());
		if (o->getControllingPlayer())
		{
			event.setPlayerIndex(o->getControllingPlayer()->getPlayerIndex());
		}
	}
	else
	{
		if (a == "PLAY_SOUND_EFFECT_AT" && r.hasPosition)
		{
			event.setPosition(r.position);
		}
		if (local)
		{
			event.setPlayerIndex(local->getPlayerIndex()); // RW 0x7BE11F: ThePlayerList's local player (+ 0x54)
		}
		if (a == "SPEECH_PLAY")
		{
			event.setUninterruptable(!(r.params.size() > 1 && r.params[1].intValue != 0)); // RW 0x7BE33A: event + 0x4A = !allowOverlap
		}
	}
	const AudioHandle h = m_audio.addAudioEvent(event);
	(void)h;
	++m_scriptAudio.played;
	m_scriptAudio.lastEvents.push_back(name);
	if (m_scriptAudio.lastEvents.size() > 16)
	{
		m_scriptAudio.lastEvents.erase(m_scriptAudio.lastEvents.begin());
	}
	return true;
}

void LiveGameAudio::update()
{
	if (!audioAlive())
	{
		return; // see the destructor
	}
	flushEventLog(); // AUDIO-3: the requests since the last update (their drawables and objects are looked up now)
	GameLogic &logic = m_game.logic();
	m_audio.setLogFrame(logic.getFrame());
	AudioApi::updateWeaponFireSounds(logic.getFrame()); // the looping fire sounds whose FireSoundLoopTime ran out
	if (logic.getFrame() != m_lgaFrame)
	{
		m_lgaFrame = logic.getFrame();
		largeGroupUpdate();
	}
	// the music scripts run once per logic frame (ZH ScriptEngine::update is a logic-frame update)
	if (m_music && logic.getFrame() != m_musicFrame)
	{
		m_musicFrame = logic.getFrame();
		AudioLog::Scope logScope("music scripts");
		m_music->update(m_musicFrame);
	}
	std::map<ObjectID, bool> seen;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		const ThingTemplate *tt = o->getTemplate();
		if (!tt)
		{
			continue;
		}
		if (!m_battleCounted.count(o->getID()))
		{
			m_battleCounted[o->getID()] = true;
			m_stats.battleAmbientTemplates += soundField(*tt, "SoundAmbientBattle").empty() ? 0 : 1;
		}
		{
			AudioLog::Scope logScope("eva body watch");
			evaWatch(*o);
		}
		// ZH: a drawable's ambient sound is enabled once the object is complete (construction keeps it off) and stops when the object dies
		std::string want;
		if (!o->isDestroyed() && !o->isUnderConstruction() && !o->isEffectivelyDead())
		{
			want = cachedAmbientSoundFor(*o);
		}
		if (want.empty() && !m_ambients.count(o->getID()))
		{
			continue;
		}
		seen[o->getID()] = true;
		Ambient &a = m_ambients[o->getID()];
		if (a.event != want)
		{
			stopAmbient(o->getID(), a);
			a.event = want;
		}
		if (a.event.empty() || a.tried)
		{
			continue;
		}
		const auto info = m_audio.ini().infos.find(a.event);
		if (!info)
		{
			++m_stats.ambientUnknownEvent;
			a.tried = true;
			continue;
		}
		// ZH Drawable.cpp:4518: global or critical sounds always start; others only inside MaxRange of the listener (retried here every frame until they
		// start: INFERENCE, ZH only tries when the drawable starts or changes damage state)
		if (!(info->type & ST_GLOBAL) && info->priority != AP_CRITICAL)
		{
			const Coord3D &l = m_audio.getListenerPosition();
			const Coord3D &p = *o->getPosition();
			const float dx = p.x - l.x, dy = p.y - l.y, dz = p.z - l.z;
			if (dx * dx + dy * dy + dz * dz >= info->maxRange * info->maxRange)
			{
				++m_stats.ambientOutOfRange;
				continue;
			}
		}
		const Player *owner = o->getControllingPlayer();
		const Drawable *d = m_game.drawables().findByObject(o->getID()); // SMOOTH-1: the render side's drawable (update runs at a worker-idle point)
		AudioLog::Scope logScope("ambient");
		a.handle = d ? m_audio.playSoundForDrawable(a.event, (std::uint32_t)d->getID(), owner ? owner->getPlayerIndex() : -1)
					 : m_audio.playSoundForObject(a.event, (std::uint32_t)o->getID(), owner ? owner->getPlayerIndex() : -1);
		a.tried = true;
		++m_stats.ambientStarted;
	}
	for (auto it = m_ambients.begin(); it != m_ambients.end();)
	{
		if (!seen.count(it->first))
		{
			stopAmbient(it->first, it->second);
			it = m_ambients.erase(it);
		}
		else
		{
			++it;
		}
	}
	m_stats.ambientPlaying = 0;
	for (const auto &kv : m_ambients)
	{
		m_stats.ambientPlaying += kv.second.handle >= AHSV_FirstHandle ? 1 : 0;
	}
}

void LiveGameAudio::enableEventLog(bool on)
{
	if (audioAlive())
	{
		m_audio.enableEventLog(on);
	}
}

std::vector<std::string> LiveGameAudio::takeEventLogLines()
{
	std::vector<std::string> out;
	if (!audioAlive())
	{
		return out;
	}
	GameLogic &logic = m_game.logic();
	for (const AudioLogEntry &e : m_audio.takeEventLog())
	{
		// the object the request names: its own, its drawable's, or the one an FX tag carries ("... obj <id>")
		std::uint32_t objectId = e.objectId;
		const ThingTemplate *tt = nullptr;
		if (e.drawableId)
		{
			if (const Drawable *d = m_game.drawables().find((DrawableID)e.drawableId))
			{
				objectId = d->getObjectID();
				tt = d->getTemplate();
			}
		}
		if (!objectId)
		{
			const size_t at = e.origin.rfind(" obj ");
			if (at != std::string::npos)
			{
				objectId = (std::uint32_t)std::strtoul(e.origin.c_str() + at + 5, nullptr, 10);
			}
		}
		if (!tt && objectId)
		{
			if (const Object *o = logic.findObjectByID((ObjectID)objectId))
			{
				tt = o->getTemplate();
			}
		}
		char head[160];
		std::snprintf(head, sizeof head, "%.1f\t%u\t%s\t%u\t%u\t%u\t", e.clockMs, (unsigned)e.frame, e.play ? "play" : "request", (unsigned)e.handle,
			(unsigned)objectId, (unsigned)e.drawableId);
		char pos[96] = "-";
		if (e.positional)
		{
			std::snprintf(pos, sizeof pos, "%.0f,%.0f,%.0f", e.position.x, e.position.y, e.position.z);
		}
		out.push_back(std::string(head) + e.event + "\t" + (tt ? tt->getName() : std::string("-")) + "\t" + pos + "\t" + std::to_string(e.player) + "\t" +
			(e.origin.empty() ? std::string("-") : e.origin) + "\t" + e.outcome);
	}
	return out;
}

bool LiveGameAudio::openEventLogFile(const std::string &path, std::string *error)
{
	if (m_logFile)
	{
		std::fclose(m_logFile);
		m_logFile = nullptr;
	}
	m_logFile = std::fopen(path.c_str(), "w");
	if (!m_logFile)
	{
		if (error)
		{
			*error = "cannot write the audio log " + path;
		}
		return false;
	}
	std::fprintf(m_logFile, "%s\n", eventLogHeader());
	enableEventLog(true);
	return true;
}

void LiveGameAudio::flushEventLog()
{
	if (!m_logFile)
	{
		return;
	}
	for (const std::string &l : takeEventLogLines())
	{
		std::fprintf(m_logFile, "%s\n", l.c_str());
	}
	std::fflush(m_logFile);
}

const char *LiveGameAudio::eventLogHeader()
{
	return "ms\tframe\tline\thandle\tobject\tdrawable\tevent\ttemplate\tposition\tplayer\torigin\toutcome";
}

std::map<ObjectID, std::string> LiveGameAudio::playingAmbients() const
{
	std::map<ObjectID, std::string> out;
	for (const auto &kv : m_ambients)
	{
		if (kv.second.handle >= AHSV_FirstHandle)
		{
			out[kv.first] = kv.second.event;
		}
	}
	return out;
}

std::vector<std::string> LiveGameAudio::acceptanceStops()
{
	return {
		"[S-706] object ambient sounds: the rules are ZH's Drawable::startAmbientSound (damage-state sound with the pristine fallback, MaxRange gate for non global "
		"non critical sounds) read as donor; RotWK's own drawable ambient code was not read: SoundAmbientBattle, the per-object custom ambient info "
		"(ZH m_customSoundAmbientInfo), the time of day and the script enable flags are not ported, and an out-of-range ambient is retried every frame",
		"[S-707] object sounds not ported: SoundCreated, SoundOnDamaged / ReallyDamaged, SoundEnter / Exit, SoundPromoted*, SoundStealthOn / Off, SoundImpact, "
		"SoundCrushing, SoundFallingFromPlane, SoundAmbientBattle (the death, impact and FireFX sounds are FX list nuggets: FX-2's lane). Done in AUDIO-2 "
		"part 2: the weapon FireSound (RW 0x8E3411), SoundMoveStart / Loop (S-712), VoiceFullyCreated at construction completion (RW 0x857D6C .. 0x857DBA: "
		"GettingBuiltBehavior::checkCompletion posts it right before m_completedOnce = true, guarded by !m_completedOnce && frame >= 5; retail's +0x33 is "
		"m_completedOnce), LargeGroupAudio (S-248)",
		"[S-712] fire and move sounds: the fire sound loop's stop frame and handle are kept by the audio side, not on the FiringTracker (RW + 0x54 / + 0x58, "
		"whose pending stop frame also keeps the tracker awake in retail); the move sounds are AIInternalMoveToState's (lane AUDIO-3: startMoveSound RW 0x748C0B "
		"at onEnter RW 0x74E06F, the loop removed at onEnter / onExit) with the move loop's handle kept by the audio side (AudioApi::postHeldSound), not at "
		"state + 0x40, and without the draw module's sound overrides (RW 0x676A8A, S-701)",
		"[S-709] Eva from damage and death: EvaEventDamagedOwner and EvaEventDieOwner / Ally / Enemy are reported by a client watch of the bodies (a health drop of a "
		"local object, a death); retail's logic call sites (ActiveBody / the death pipeline) were not decoded, nor the other Eva rows (DamagedFromShroudedSource, "
		"DamagedByFire, SecondDamageFarFromFirst, Ambushed, the sighting and detection events)",
		"[S-1240] held sounds (lane AUDIO-3): the handles retail keeps in its modules (GettingBuiltBehavior + 0x24, the dozer's building sound, "
		"AIInternalMoveToState + 0x40) are kept by the audio side keyed by the holder object (AudioApi::postHeldSound); an object keeps one move loop (retail: one per "
		"move state instance); the dozer's building sound starts at RW's dock sub task 2 -> 3 (RW 0x88DD65, ported by lane BUILD-3)",
		"[S-1241] request queue (lane AUDIO-3): RotWK's SoundManager::addAudioEvent requeue (RW 0x45CC91: a culled event waits when it loops or its Delay is at least "
		"33.33 ms) and re-check (RW 0x461A83) are ported; not ported: canPlayNow's refusal of an event whose position cannot be resolved (RW 0x45CB92: the port skips "
		"the distance gate instead), the processing pass's early check and preload of a soon-due request (RW 0x461AA1 .. 0x461B07, FUN 0x4A7C71); a requeued loop "
		"whose owner is gone is dropped (port)",
		"[S-1463] large group audio: RotWK drops a member that leaves the world (RW 0x692313 -> 0x690728 sets INAUDIBLE and calls the module's leave, "
		"RW 0x8AF09D); the port never sets INAUDIBLE, so a garrisoned or transported member keeps counting (lane AUDIO-4)",
		"[S-1464] footsteps (lane AUDIO-4: AnimationSoundClientBehavior and TheAnimationSoundModuleManager, RW 0x8CE75B / 0x83F321): the module's slot 11 is read as "
		"reactToTransformChange (every re-posed drawable is dirty each render frame), the manager runs once per render frame after the animations advanced "
		"(retail: per client frame with the lazy advance RW 0x4BFAF4), animations are matched by upper-cased name (retail: name keys)",
		"[S-1242] retail sound triggers that never fire here (lane AUDIO-3 sweep): CrushDie's TotalCrushSound / BackEndCrushSound / FrontEndCrushSound (RW 0x8895D8: the module is not ported; no retail template uses it), and the S-707 object sounds",
	};
}
