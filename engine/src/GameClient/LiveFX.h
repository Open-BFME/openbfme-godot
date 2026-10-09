// OpenBFME. GPL-3.0.
//
// LiveFX (lane FX-2): the client effect player of a live game. It connects the logic's effect calls (GameLogic/FXEvents.h) and the drawables' state effects
// to FX-1's stack (GameClient/FXPlayback: FXList dispatcher, particle simulator) and is the FXPlaybackWorld that stack runs in: the map's terrain, the live
// objects and drawables (attachments, bones), the audio entry points. Client-only: it reads the logic, never writes it, and draws only the CLIENT random
// stream (FXPlayback's).
//
// What it plays, each with its source:
//   * logic events (installed as the GameLogic's FXEventSink, called at the moment of the retail call):
//       OBJECT_FX -> FXList::doFXObj(fx, primary, secondary) (RW 0x4B1B5A), POSITION_FX -> FXList::doFXPos(fx, pos, matrix or null) (RW 0x494615),
//       WEAPON_FIRE_FX -> the client half of RW 0x6CC915: no drawable on the shooter, no FX; then Drawable::handleWeaponFireFX (RW 0x671402 -> every model
//       draw's slot 0x58; DONOR ZH W3DModelDraw::handleWeaponFireFX: the slot's barrel list of the current model state, the barrel's fire FX bone, doFXPos at
//       the bone's world transform with the weapon speed and victim position), and when no draw module took it, doFXObj(fx, shooter, victim).
//   * EnteringStateFX of the model draws (RW 0x4BF222: doFXObj on the drawable's object), through DrawableFXHost.
//   * ParticleSysBone systems of the current model condition state and animation state of every model draw (DONOR ZH W3DModelDraw::
//     recalcBonesForClientParticleSystems / stopClientParticleSystems: on a state change the old systems are destroyed, each entry's system is created at the
//     bone's model-space position (drawable scale applied) turned by the bone's Z rotation and attached to the drawable). Construction dust, fires and smoke
//     of damaged buildings and projectile trails are this path.
//   * sounds of Sound nuggets through AudioApi::playSoundAtPosition.
//   * the FXEvent entries of the current ANIMATION state of every model draw (RW 0x4BCE68, called from W3DModelDraw::doDrawModule RW 0x4C72BE): per entry
//     {FrameStep +0, Frame +4, FrameStop +8, FireWhenSkipped +0xC, Bone +0x10, FXList +0x14} against the animation's integer frame (+0x114) and previous
//     frame (+0x118, direction +0x124); a firing entry plays through RW 0x4BABBA: no bone -> doFXObj(fx, the drawable's object, null), a bone ->
//     doFXPos(fx, bone position, bone matrix, 0, null). INFERENCE (S-682): the entries are evaluated once per change of the integer frame (RW evaluates
//     every draw at its 30 Hz cap; this renderer runs faster), the FireWhenSkipped single-frame test is RW 0x4B337B (mode and direction); NOT PORTED (S-683): the FXEvent entries of MODEL CONDITION states and the FXEvents whose FXList is routed to the timed particle
//     list (FXList byte +8, the second loop of RW 0x4BCE68).
//
// INFERENCE / not ported (stop S-682, reported by stops()):
//   * bone positions come from the model's bind pose (ZH asks the live render object's current pose; FollowBone systems are not moved with the animation);
//   * ParticleSysBone FXTrigger / Persist / PersistID / HouseColor are ignored; OnlyIfOnWater systems are not created (no water query), OnlyIfOnLand ones are;
//   * the shooter's stealth (RW 0x694C0D with a NULL viewer, RW 0x6CCB91; lane STEALTH-1) is read when the effect plays, not at the shot;
//   * the muzzle flash and recoil of handleWeaponFireFX are not drawn;
//   * isWater is false everywhere; the shroud is clear (VIS-1 owns it).
// Data\INI\ParticleSystem.ini (329 ZH-format `ParticleSystem` blocks) is not loaded, as in retail (lane FX-3, retires stop S-684). TARGET FACTS (RotWK
// game.dat, S-001 caveat): GameEngine::init never names TheParticleSystemManager (the string is not in the image; the legend entry is never requested,
// spec ini-and-object-model 3.5); no INI block registration names `ParticleSystem` (the only particle block is `FXParticleSystem` -> RW 0x5FC7DB, record
// RW 0xD9E440); the ZH ParticleSystem fields VelocityType / VolumeType / SpinRate exist nowhere in the image; ParticleSysBone (RW 0x73AECB) and the FXList
// ParticleSystem nugget resolve names in TheFXParticleSystemManager alone. 317 of the file's 329 names are also FXParticleSystem templates (those play);
// the 12 defined only there (InfantryDustTrails, the RainOfFire* set, ...) play nothing in retail and are listed in Stats::unresolvedParticleSystems.

#pragma once

#include "GameClient/Drawable.h"
#include "GameClient/FXPlayback.h"
#include "GameLogic/FXEvents.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class LiveGame;
class Object;

class LiveFX : public FXEventSink, public FXPlaybackWorld, public DrawableFXHost
{
public:
	// installs itself as the logic's sink, the drawables' FX host and the playback's world; the destructor uninstalls. Both must outlive it.
	LiveFX(LiveGame &game, FXPlayback &playback);
	~LiveFX() override;
	LiveFX(const LiveFX &) = delete;
	LiveFX &operator=(const LiveFX &) = delete;

	// FXEventSink. SMOOTH-1 (merge with FX-2): called on the simulation owner (the logic worker) at the moment of the retail call; it only CAPTURES what the
	// effect will read of its objects (position, transform, conditions, owner, relationship, kinds, radius, drawable, the list's object filters) and queues
	// it; flushPending plays the queue on the render side at a worker-idle point, in call order
	void onFXEvent(const FXEvent &event) override;
	// plays the queued logic effects and entering-state effects, in order (the render side, while the logic is idle)
	void flushPending();
	size_t pendingEvents() const { return m_pending.size() + m_pendingStates.size(); }
	// DrawableFXHost
	bool enteringStateFX(DrawableID id, const std::string &fx) override;

	// once per client frame, after the drawables' poses were synced: the ParticleSysBone systems follow every model draw's current states; the systems of
	// drawables that are gone are destroyed
	void updateAttachedSystems();

	// FXPlaybackWorld
	std::uint32_t logicFrame() const override;
	float groundHeight(float x, float y) override;
	bool isWater(float, float) override { return false; }
	int shroudStatusAt(const Coord3D &) override { return 0; }
	bool objectShroudedForLocalPlayer(const FXObject &) override { return false; }
	FXParticleSystem::ParticleAttachInfo attachedDrawable(std::uint32_t drawableId, const std::string &boneName) override;
	FXParticleSystem::ParticleAttachInfo attachedObject(std::uint32_t objectId, const std::string &boneName, int localPlayerIndex) override;
	bool boneWorldMatrix(const FXObject &obj, const std::string &boneName, Matrix3D &out) override;
	std::vector<FXBoneTransform> boneWorldTransforms(const FXObject &obj, const std::string &boneName, int startIndex, int maxBones) override;
	bool playSound(const std::string &eventName, const Coord3D *pos, int playerIndex) override;

	struct Stats
	{
		std::map<std::string, unsigned long long> played;   ///< FXLists played, per site (logic sites, "EnteringStateFX")
		std::map<std::string, unsigned long long> skipped;  ///< calls not played, per reason ("no drawable", "suspended", ...)
		std::set<std::string> missingFXLists;               ///< names the FXList store does not hold (retail rejects them at parse time)
		std::set<std::string> unresolvedParticleSystems;    ///< ParticleSysBone systems the FXParticleSystem store does not hold (RW 0x73AECB: NULL, not played)
		unsigned long long fireFXAtBone = 0;                ///< fire FX played at a barrel's fire FX bone
		unsigned long long fireFXOnObject = 0;              ///< fire FX played through doFXObj (no draw module took it)
		unsigned long long attachedCreated = 0, attachedDestroyed = 0, attachedLive = 0;
		unsigned long long boneMisses = 0;                  ///< ParticleSysBone bones the model lacks (RW 0x4C6514: the system plays at the drawable's origin)
		std::set<std::string> missingBones;                 ///< "MODEL:bone" of the bone misses of every path (at most 200): retail data, played as retail does
		unsigned long long sounds = 0;
		unsigned long long frameEventsFired = 0;           ///< animation state FXEvent entries fired (RW 0x4BCE68)
		unsigned long long uncapturedFilters = 0;          ///< SMOOTH-1: an object filter asked at play time that the capture at the call did not evaluate (fails)
	};
	const Stats &stats() const { return m_stats; }
	// RW 0x4B337B, the single-frame test of a FireWhenSkipped FXEvent (mode: W3D_ANIM_MODE_*, direction +1 / -1)
	static bool framePassed(int prev, int cur, int frame, int mode, int direction);
	// the stop lines of the live effects (S-680 .. S-686: the logic side's FXEventLog::stops() and this player's), for the report
	static std::vector<std::string> stops();

private:
	struct Attached
	{
		std::vector<const void *> states;                     ///< per draw entry: model state, animation state (compared, never ordered)
		std::vector<FXParticleSystem::ParticleSystemID> systems;
	};
	Object *object(ObjectID id) const;
	struct Captured; // the captured view of an event's object (LiveFX.cpp)
	struct Pending
	{
		FXEvent event;
		std::shared_ptr<Captured> primary, secondary;
	};
	void play(const Pending &p);
	bool playEnteringStateFX(DrawableID id, const std::string &fx);
	std::vector<Pending> m_pending;                                     ///< filled on the simulation owner, drained at idle points
	std::vector<std::pair<DrawableID, std::string>> m_pendingStates;    ///< EnteringStateFX of drawables (render side), played at idle points
	bool modelBone(const RenderObjPrototype *proto, const std::string &bone, float scale, Matrix3D &out, bool subObjects = true);
	bool handleWeaponFireFX(const Drawable &shooter, const FXEvent &e, const FXList *fx);
	void destroySystems(Attached &a);
	void createSystems(Drawable &d, Attached &a);
	struct FrameEventState
	{
		const void *animState = nullptr; ///< the animation state the frames belong to
		int lastFrame = -1;              ///< the integer frame the events were last evaluated for
	};
	using FrameEventMap = std::map<std::pair<DrawableID, size_t>, FrameEventState>;
	// lane PERF-3: `pos` walks m_frameEvents alongside updateAttachedSystems' walk over the drawables in id order (both are in key order): each entry's
	// lookup is a step of the walk instead of a search of the map
	void frameEvents(Drawable &d, size_t entryIndex, const DrawEntry &e, FrameEventMap::iterator &pos);
	void fireFrameEvent(Drawable &d, const FXEventInfo &ev, const RenderObjPrototype *model);

	LiveGame &m_game;
	FXPlayback &m_playback;
	Stats m_stats;
	std::map<DrawableID, Attached> m_attached;
	FrameEventMap m_frameEvents; // looked up only (and pruned), never ordered by pointer
	W3DDrawFrame m_eventFrame;   // lane PERF-3: frameEvents' frame of the entry (its storage reused)
	std::vector<const void *> m_states; // lane PERF-3: updateAttachedSystems' states of the drawable (its storage reused)
	std::map<std::pair<const RenderObjPrototype *, std::string>, std::pair<bool, Matrix3D>> m_boneCache; // looked up only, never iterated
};
