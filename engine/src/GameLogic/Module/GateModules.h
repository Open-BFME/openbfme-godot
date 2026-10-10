// OpenBFME. GPL-3.0.
//
// Lane HUD-5 (the owner's report: clicking a gate on Helm's Deep does not open it): GateOpenAndCloseBehavior, AIGateUpdate, the gate messages
// MSG_OPEN_GATE / MSG_CLOSE_GATE and the gate command buttons.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 decomp map: the module's functions are tier A / B with byte-matched C++ in
// Code/GameEngine/Source/GameLogic/Object/Behavior/GateOpenAndCloseBehavior*.cpp, read as the donor; RotWK's offsets and edits taken from the disassembly):
//   * the module data (constructor BFME2 0x898E2E, field table RW 0xC07D08): OpenByDefault (parseBool, +8, false), ResetTimeInMilliseconds
//     (parseDurationUnsignedInt, +0xC, 50 frames), PercentOpenForPathing (parseUnsignedInt RW 0x42ECB2, +0x10, 50), Proxy (parseAsciiString, +0x14),
//     RepelCollidingUnits (parseBool, +0x18, true), GeometryForOpen / GeometryForClosed (parseAsciiStringVectorAppend RW 0x42E59E, +0x2C / +0x38, the
//     constructor's {"OpenLeft", "OpenRight"} / {"Closed"}), the sounds SoundOpeningGateLoop / SoundFinishedOpeningGate / SoundClosingGateLoop /
//     SoundFinishedClosingGate (RW 0x73B217, +0x1C .. +0x28), TimeBeforePlayingOpenSound / TimeBeforePlayingClosedSound (parseDurationUnsignedInt, +0x44 /
//     +0x48, -1 = the reset time);
//   * the module (constructor RW 0x89C04B): the state (+0x28) starts open (1) with OpenByDefault, else closed (3); settled (+0x30) true; percent 100; the
//     state frame now; the geometry state (+0x2C) 0, the want-open request (+0x40) -1. States: 0 opening, 1 open, 2 closing, 3 closed;
//   * the primary interface (vtable RW 0xC668E0): slot 6 isOpen (state == 1, RW 0x8DC9AB), slot 7 open (RW 0x89C97A: when settled and not open: the push-out
//     scan, state 0, unsettled, percent 0, the state frame now), slot 8 close (RW 0x89C9BA: the same when settled and open, state 2), slot 9 toggle
//     (RW 0x89BFB5), slot 10 settled (RW 0x86277F: + 0x30); changeWantOpenCount (RW 0x89BFE0: +-1 only; a request below 0 starts from 0);
//   * the update (RW 0x89CC29, every frame): under construction (status 2): unsettled. Dead: a non-open gate becomes opening at 100 %, the OPEN geometry, its
//     sound removed, the gate list entry dropped, unsettled, done. A request >= 0 toggles a settled gate whose isOpen differs from (request > 0) (a request of
//     0 then becomes -1). Then by state: closed (3): 100 %, settled, model condition DOOR_1_CLOSING on / DOOR_1_OPENING off, the CLOSED geometry; closing (2):
//     percent = elapsed frames / reset time * 100 (x87 PC24), at >= 100 the state becomes closed, above 100 - PercentOpenForPathing the CLOSED geometry, the
//     finished sound once elapsed > TimeBeforePlayingClosedSound, unsettled, DOOR_1_CLOSING on; open (1): 100 %, settled, DOOR_1_OPENING on / DOOR_1_CLOSING
//     off, the OPEN geometry; opening (0): the percent, at >= 100 open, above PercentOpenForPathing the OPEN geometry, the open sound after
//     TimeBeforePlayingOpenSound, unsettled, DOOR_1_OPENING on. Under construction again at the end: unsettled.
//   * the geometry (RW 0x89C9FA closed / RW 0x89CA84 open): when it changes, the push-out scan, the object leaves the pathfinder's map (RW 0x6E861E), its
//     shapes named in GeometryForOpen go active (open) or inactive (closed) and those in GeometryForClosed the other way (GeometryInfo::setActive RW 0xAD3520),
//     the object's bounds are refreshed (RW 0x68B244) and it enters the map again (RW 0x6E85E9);
//   * MSG_OPEN_GATE (1079) / MSG_CLOSE_GATE (1081) (dispatcher RW 0x779A3D, cases RW 0x77BF7B / 0x77C037): argument 0's object; its GateOpenAndCloseBehavior,
//     else its GateProxyBehavior; open when not open and settled / close when open and settled. No owner test;
//   * the buttons (processCommandUI RW 0x9410D7): TOGGLE_GATE on the context object with a settled gate: MSG_CLOSE_GATE when open, else MSG_OPEN_GATE, the
//     object id as the argument (RW 0x94152F); OPEN_GATE / CLOSE_GATE send their message (RW 0x9410AB). Availability (RW 0x9436E9): restricted (0) under
//     construction, dead, without a gate module or unsettled; TOGGLE_GATE active (2); OPEN_GATE active when not open, CLOSE_GATE active when open;
//   * AIGateUpdate (update RW 0x8B4D34, an edited BFME2 0x8B0C27): only while the gate's controlling player has a skirmish AI (TheSkirmishAIManager's AI
//     groups, RW 0x6A950B) and the gate has a GateOpenAndCloseBehavior (RW 0x89BEE0): once, a polygon trigger "AIGateUpdateTrigger_%d" (RW 0x8B4BB7) of the
//     box TriggerWidthX x TriggerWidthY (parseReal, +8 / +0xC; none when either is not > 0) around the gate, turned by its angle (RW 0x8B4B31: x' = pos.x +
//     (c x - s y), y' = pos.y + (c y + s x), CRT sin / cos), corners (hx, hy), (hx, -hy), (-hx, -hy), (-hx, hy); its callback (RW 0x8B4AF4) counts the objects
//     entering / leaving it by the gate's relationship to them: allies (+0x28), enemies (+0x2C). Each update with a settled gate: an open gate with no ally
//     inside closes (RotWK dropped BFME2's "and an enemy inside"), a closed gate with an ally inside opens.
// INFERENCE / NOT PORTED (stops S-1940 .. S-1942): the push-out scan of the units in the gate's way (RW 0x89C759) and the collide interface's repel (RW 0x89C6F5,
// RepelCollidingUnits), GateProxyBehavior and the Proxy link (onObjectCreated RW 0x89CB19), the gate sounds (no logic sound path: counted, not played), the
// object interface call RW 0x68C3E6 -> vslot 2 at the start of the update, the AI notification RW 0x68BE59(9), the skirmish AI's gate list (TheSkirmishAIManager
// + 0xA74) and FakePathfindPortalBehaviour; the pathfinder's "keeping gate flags" removal (RW 0x6E861E) is the plain removal here.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class GameLogicDispatch;
class ModuleFactory;
class MultiIniFieldParse;
class Object;

class GateOpenAndCloseBehaviorModuleData : public ModuleData
{
public:
	bool m_openByDefault = false;                 ///< +8
	unsigned m_resetTime = 50;                    ///< +0xC (frames)
	unsigned m_percentOpenForPathing = 50;        ///< +0x10
	std::string m_proxy;                          ///< +0x14
	bool m_repelCollidingUnits = true;            ///< +0x18
	StoreReference m_soundOpeningLoop, m_soundFinishedOpening, m_soundClosingLoop, m_soundFinishedClosing; ///< +0x1C .. +0x28
	std::vector<std::string> m_geometryForOpen{ "OpenLeft", "OpenRight" }; ///< +0x2C
	std::vector<std::string> m_geometryForClosed{ "Closed" };             ///< +0x38
	unsigned m_timeBeforeOpenSound = 0xFFFFFFFFu;  ///< +0x44 (-1: the reset time)
	unsigned m_timeBeforeClosedSound = 0xFFFFFFFFu; ///< +0x48
	static void buildFieldParse(MultiIniFieldParse &p);
};

class GateOpenAndCloseBehavior : public UpdateModule
{
public:
	enum State
	{
		OPENING = 0,
		OPEN = 1,
		CLOSING = 2,
		CLOSED = 3
	};
	enum GeometryState
	{
		GEOMETRY_NONE = 0,
		GEOMETRY_CLOSED = 1,
		GEOMETRY_OPEN = 2
	};
	GateOpenAndCloseBehavior(Thing *thing, const GateOpenAndCloseBehaviorModuleData *data);
	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;

	bool isOpen() const { return m_state == OPEN; } // slot 6
	bool isSettled() const { return m_settled; }    // slot 10
	void open();                                     // slot 7
	void close();                                    // slot 8
	void toggle();                                   // slot 9
	void changeWantOpenCount(int delta);             // RW 0x89BFE0

	State state() const { return (State)m_state; }
	GeometryState geometryState() const { return (GeometryState)m_geometryState; }
	float percent() const { return m_percent; }
	int wantOpenRequest() const { return m_request; }
	// the sounds the gate would have started (stop S-1942: not played), in order: "opening", "closing", "opened", "closed"
	const std::vector<std::string> &soundRequests() const { return m_sounds; }

	// the gate interface of an object (its GateOpenAndCloseBehavior; GateProxyBehavior is not ported), null without one
	static GateOpenAndCloseBehavior *findGate(const Object &obj);

private:
	void setOpenCloseState(int state); // RW 0x89C261
	void playFinishedSound();          // RW 0x89C1A4
	void setGeometry(bool openGeometry, bool scan); // RW 0x89CA84 / 0x89C9FA
	float elapsedPercent() const;

	const GateOpenAndCloseBehaviorModuleData *m_data;
	int m_state = CLOSED;            ///< +0x28
	int m_geometryState = GEOMETRY_NONE; ///< +0x2C
	bool m_settled = true;           ///< +0x30
	float m_percent = 100.0f;        ///< +0x34
	UnsignedInt m_stateFrame = 0;    ///< +0x3C
	int m_request = -1;              ///< +0x40
	bool m_soundPlayed = false;      ///< +0x48
	std::vector<std::string> m_sounds;
};

class AIGateUpdateModuleData : public ModuleData
{
public:
	float m_triggerWidthX = 0.0f; ///< +8
	float m_triggerWidthY = 0.0f; ///< +0xC
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AIGateUpdate : public UpdateModule
{
public:
	AIGateUpdate(Thing *thing, const AIGateUpdateModuleData *data);
	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;
	// the trigger's callback (RW 0x8B4AF4)
	void onTrigger(Object &obj, bool entered);
	int alliesInside() const { return m_allies; }
	int enemiesInside() const { return m_enemies; }
	int triggerIndex() const { return m_trigger; }

private:
	void loadTrigger(); // RW 0x8B4BB7
	const AIGateUpdateModuleData *m_data;
	int m_trigger = -1;     ///< the trigger's index in the script engine's list (+0x24 holds its id)
	int m_allies = 0;       ///< +0x28
	int m_enemies = 0;      ///< +0x2C
	bool m_loaded = false;  ///< +0x30
	bool m_enabled = false; ///< +0x31
};

namespace GateModules
{
void registerAll(ModuleFactory &modules);
// MSG_OPEN_GATE / MSG_CLOSE_GATE (RW 0x77BF7B / 0x77C037)
void registerHandlers(GameLogicDispatch &dispatch);
std::vector<std::string> acceptanceStops();
} // namespace GateModules
