// OpenBFME. GPL-3.0.
//
// FXPlayback: the FX stack wired together without Godot (stores, simulator, services) so the viewer, the smoke test and the
// unit tests drive the same code. It loads the retail FXParticleSystem.ini and FXList.ini through the INI system, owns the
// ParticleSystemManager and a ParticleEnvironment / FXServices implementation for a flat test world, and steps the simulator at
// the fixed client rate of RW (30 Hz, RW 0x449d48 once per Display::draw under the 30 FPS limit).
//
// This is the lane's interface for the rest of the engine too: other lanes replace the world (terrain, shroud, objects, drawables)
// by implementing ParticleEnvironment and FXServices; they never see Object classes from this lane.

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "GameClient/FXList.h"
#include "GameClient/FXParticleSystem.h"
#include "GameClient/ParticleSys.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/part_emt.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// A stand-in object for FXList::doFXObj in the viewer and the tests: everything an FXObject must answer, with fixed values.
class FXDummyObject : public FXObject
{
public:
	std::uint32_t id = 1;
	Coord3D pos;
	Matrix3D mtx;
	ModelConditionFlags conditions;
	float radius = 20.0f;

	std::uint32_t objectId() const override { return id; }
	Coord3D position() const override { return pos; }
	Matrix3D transform() const override { return mtx; }
	ModelConditionFlags modelConditions() const override { return conditions; }
	bool passesObjectFilter(const ObjectFilter &) const override { return true; }
	bool hasDrawable() const override { return false; }
	float boundingCircleRadius() const override { return radius; }
	int controllingPlayerIndex() const override { return 0; }
	bool isControlledByLocalPlayer() const override { return true; }
	int relationshipToLocalPlayer() const override { return 2; }
	bool hasKindOf(const char *) const override { return false; }
	bool isHorde() const override { return false; }
};

// The world a live game puts under the FX stack (lane FX-2: GameClient/LiveFX implements it over a LiveGame). Without one, FXPlayback is the flat test world
// of FX-1 (ground at setGroundHeight, nothing attached, sounds noted as events).
class FXPlaybackWorld
{
public:
	virtual ~FXPlaybackWorld() = default;
	virtual std::uint32_t logicFrame() const = 0;                                ///< RW TheGameLogic +0x40 (the FXList cull window)
	virtual float groundHeight(float x, float y) = 0;                            ///< RW TerrainLogic vtbl+0x18
	virtual bool isWater(float x, float y) = 0;
	virtual int shroudStatusAt(const Coord3D &pos) = 0;                         ///< 0 clear, 1 fogged, 2 shrouded for the local player
	virtual bool objectShroudedForLocalPlayer(const FXObject &obj) = 0;
	virtual FXParticleSystem::ParticleAttachInfo attachedDrawable(std::uint32_t drawableId, const std::string &boneName) = 0;
	virtual FXParticleSystem::ParticleAttachInfo attachedObject(std::uint32_t objectId, const std::string &boneName, int localPlayerIndex) = 0;
	virtual bool boneWorldMatrix(const FXObject &obj, const std::string &boneName, Matrix3D &out) = 0;
	virtual std::vector<FXBoneTransform> boneWorldTransforms(const FXObject &obj, const std::string &boneName, int startIndex, int maxBones) = 0;
	// a Sound nugget (RW 0x5DF7F3 / 0x5DF85E): true when the world played it (else it is noted as an event)
	virtual bool playSound(const std::string &eventName, const Coord3D *pos, int playerIndex) = 0;
};

class FXPlayback
{
public:
	// One engine call a nugget made that this lane does not implement (audio, camera, lights, decals ...).
	struct Event
	{
		std::string kind;   ///< "sound", "eva", "viewShake", "cameraShaker", "lightPulse", "scorch", "decal", "tint", "buff", "rayEffect", ...
		std::string detail;
		Coord3D pos;
		std::uint32_t frame = 0;
	};

	// `random` has no default: the caller names the client generator (S-093).
	FXPlayback(ArchiveFileSystem &fs, RandomAlgorithm random);
	~FXPlayback();

	// Defines the GameData macros, then loads Data\INI\FXParticleSystem.ini and Data\INI\FXList.ini (the files of the retail
	// subsystem legend). Returns the errors (empty = success); nothing is silently skipped.
	std::vector<std::string> loadRetailData();

	// Loads INI text (FXParticleSystem / FXList blocks) as a file named `name` into the stores (tests, mods); returns the errors.
	std::vector<std::string> loadIniText(const std::string &name, const std::string &text);

	FXParticleSystem::FXParticleSystemTemplateStore &particleTemplates() { return m_templates; }
	FXListStore &fxLists() { return m_lists; }
	FXParticleSystem::ParticleSystemManager &particles() { return *m_manager; }
	const FXParticleSystem::ParticleSystemManager &particles() const { return *m_manager; }
	ArchiveFileSystem &fileSystem() { return m_fs; }

	void seedClientRandom(std::uint32_t seed) { m_random.seed(seed); }
	std::uint32_t clientFrame() const { return m_frame; }

	// SAGE world space (x east, y north, z up). The flat test world is the plane z = groundZ.
	void setGroundHeight(float z) { m_groundZ = z; }
	// lane FX-2: the live world (null: the flat test world). Not owned; must outlive its use.
	void setWorld(FXPlaybackWorld *world) { m_world = world; }
	FXPlaybackWorld *world() const { return m_world; }
	// the services the nuggets call (the live game plays FXLists on its own objects through FXList::doFXObj / doFXPos with these)
	FXServices &services();

	// Creates a system from a template name at a position (RW createParticleSystem + setPosition). 0 when the template is unknown.
	FXParticleSystem::ParticleSystemID playParticleSystem(const std::string &name, const Coord3D &pos);
	// Plays an FXList by name: at a position (doFXPos, filters of object nuggets skipped) or on a dummy object at the position (doFXObj).
	// false when the list is unknown. Goes through the static wrappers (cull test, superseded redirect, shroud).
	bool playFXList(const std::string &name, const Coord3D &pos, bool asObject);

	// W3D model emitters (WW3D2 ParticleEmitterClass): loads art\w3d\<first two>\<stem>.w3d, takes its emitter (the file must hold exactly
	// one) and starts it at `pos` (the emitter transform is a translation; a bone attachment calls setEmitterTransform each frame).
	// Returns the instance, or nullptr with *error set when the file is missing or malformed (never a silent skip).
	ParticleEmitterInstance *playW3DEmitter(const std::string &stem, const Coord3D &pos, std::string *error);
	W3DEmitterWorld &emitters() { return m_emitterWorld; }
	const W3DEmitterWorld &emitters() const { return m_emitterWorld; }

	// One client step: the frame advances, then the manager updates (RW 0x632431 / 0x449d48). The W3D emitter clock advances by
	// 33 ms per step (WW3D::Sync, notes fx-w3d-emitters.md section 3) and the emitters emit.
	void step();

	const std::vector<Event> &events() const { return m_events; }
	void clearEvents() { m_events.clear(); }

	// Every acceptance-stop line of the FX stack (S-093, S-190 .. S-193 ...), for the report.
	std::vector<std::string> unverified() const;

	// The client stream (also used by the renderer's per-draw samples through a separate instance, never this one).
	W3DDrawRandom &clientRandom() { return m_random; }

private:
	class Environment;
	class Services;

	ArchiveFileSystem &m_fs;
	INIEnvironment m_iniEnv;
	FXParticleSystem::FXParticleSystemTemplateStore m_templates;
	FXListStore m_lists;
	W3DClientRandom m_random;
	std::uint32_t m_frame = 0;
	float m_groundZ = 0.0f;
	FXPlaybackWorld *m_world = nullptr;
	std::vector<Event> m_events;
	W3DEmitterWorld m_emitterWorld;
	std::map<std::string, std::shared_ptr<W3DFileContents>> m_emitterFiles;
	std::unique_ptr<Environment> m_env;
	std::unique_ptr<Services> m_services;
	std::unique_ptr<FXParticleSystem::ParticleSystemManager> m_manager;
};
