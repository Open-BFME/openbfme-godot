// OpenBFME. GPL-3.0.
// See GameLogic/Module/GateModules.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/GateModules.h"

#include "Common/AsciiString.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

// Common/ModelState.h (the name table of the model conditions) cannot be included next to GameLogic/BitFlags.h (as UpgradeModule.cpp)
namespace ModelCondition
{
int indexOf(const std::string &name);
}

namespace
{
const FieldParse kGateOpenAndClose[] = {
	{ "OpenByDefault", INI::parseBool, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_openByDefault) },
	{ "ResetTimeInMilliseconds", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_resetTime) },
	{ "PercentOpenForPathing", INI::parseUnsignedInt, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_percentOpenForPathing) },
	{ "Proxy", INI::parseAsciiString, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_proxy) },
	{ "RepelCollidingUnits", INI::parseBool, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_repelCollidingUnits) },
	{ "GeometryForOpen", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_geometryForOpen) },
	{ "GeometryForClosed", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_geometryForClosed) },
	{ "SoundOpeningGateLoop", OpenContainModuleData::parseAudioEvent, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_soundOpeningLoop) },
	{ "SoundFinishedOpeningGate", OpenContainModuleData::parseAudioEvent, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_soundFinishedOpening) },
	{ "SoundClosingGateLoop", OpenContainModuleData::parseAudioEvent, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_soundClosingLoop) },
	{ "SoundFinishedClosingGate", OpenContainModuleData::parseAudioEvent, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_soundFinishedClosing) },
	{ "TimeBeforePlayingOpenSound", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_timeBeforeOpenSound) },
	{ "TimeBeforePlayingClosedSound", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(GateOpenAndCloseBehaviorModuleData, m_timeBeforeClosedSound) },
	{ nullptr, nullptr, nullptr, 0 }
};

const FieldParse kAIGateUpdate[] = {
	{ "TriggerWidthX", INI::parseReal, nullptr, (int)offsetof(AIGateUpdateModuleData, m_triggerWidthX) },
	{ "TriggerWidthY", INI::parseReal, nullptr, (int)offsetof(AIGateUpdateModuleData, m_triggerWidthY) },
	{ nullptr, nullptr, nullptr, 0 }
};

int doorOpening()
{
	static const int bit = ModelCondition::indexOf("DOOR_1_OPENING"); // bit 21: Object + 0x10C word 0, 0x200000
	return bit;
}
int doorClosing()
{
	static const int bit = ModelCondition::indexOf("DOOR_1_CLOSING"); // bit 22: 0x400000
	return bit;
}

// the decomp's clearAndSetModelConditionState(clear, set) with single bits (RW 0x89CDDB ..: nothing when the set bit is on and the clear bit off)
void clearAndSet(Object &obj, int clearBit, int setBit)
{
	if (obj.testModelCondition(setBit) && !obj.testModelCondition(clearBit))
	{
		return;
	}
	obj.setModelConditionState(clearBit, false);
	obj.setModelConditionState(setBit, true);
}

template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}
} // namespace

void GateOpenAndCloseBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kGateOpenAndClose); // RW 0xC07D08
}

void AIGateUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kAIGateUpdate); // RW 0xC6D0C4
}

// ---- GateOpenAndCloseBehavior -------------------------------------------------------------------------------------------------------
// RW 0x89C04B
GateOpenAndCloseBehavior::GateOpenAndCloseBehavior(Thing *thing, const GateOpenAndCloseBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	m_state = data->m_openByDefault ? OPEN : CLOSED;
	m_settled = true;
	m_percent = 100.0f;
	m_stateFrame = getObject() ? getObject()->logic().getFrame() : 0;
	m_geometryState = GEOMETRY_NONE;
	m_request = -1;
	m_soundPlayed = false;
}

GateOpenAndCloseBehavior *GateOpenAndCloseBehavior::findGate(const Object &obj)
{
	return dynamic_cast<GateOpenAndCloseBehavior *>(obj.findModule("GateOpenAndCloseBehavior"));
}

// RW 0x89C97A
void GateOpenAndCloseBehavior::open()
{
	if (isSettled() && !isOpen())
	{
		setOpenCloseState(OPENING);
		m_settled = false;
		m_percent = 0.0f;
		m_stateFrame = getObject()->logic().getFrame();
	}
}

// RW 0x89C9BA
void GateOpenAndCloseBehavior::close()
{
	if (isSettled() && isOpen())
	{
		setOpenCloseState(CLOSING);
		m_settled = false;
		m_percent = 0.0f;
		m_stateFrame = getObject()->logic().getFrame();
	}
}

// RW 0x89BFB5
void GateOpenAndCloseBehavior::toggle()
{
	if (isOpen())
	{
		close();
	}
	else
	{
		open();
	}
}

// RW 0x89BFE0
void GateOpenAndCloseBehavior::changeWantOpenCount(int delta)
{
	if (delta == -1 || delta == 1)
	{
		if (m_request < 0)
		{
			m_request = 0;
		}
		m_request += delta;
	}
}

// RW 0x89C261
void GateOpenAndCloseBehavior::setOpenCloseState(int state)
{
	if (m_state == state)
	{
		return;
	}
	switch (state)
	{
		case OPENING:
			if (!m_data->m_soundOpeningLoop.name.empty())
			{
				m_sounds.push_back("opening");
			}
			m_soundPlayed = false;
			break;
		case CLOSING:
			if (!m_data->m_soundClosingLoop.name.empty())
			{
				m_sounds.push_back("closing");
			}
			m_soundPlayed = false;
			break;
		case OPEN:
		case CLOSED:
			if (!m_soundPlayed)
			{
				playFinishedSound();
			}
			break;
	}
	m_state = state;
}

// RW 0x89C1A4: the finished sound of the state (opening / open: the opened sound, closing / closed: the closed one); the loop's handle is removed first
void GateOpenAndCloseBehavior::playFinishedSound()
{
	if ((m_state == OPENING || m_state == OPEN) && !m_data->m_soundFinishedOpening.name.empty())
	{
		m_sounds.push_back("opened");
	}
	else if ((m_state == CLOSING || m_state == CLOSED) && !m_data->m_soundFinishedClosing.name.empty())
	{
		m_sounds.push_back("closed");
	}
	m_soundPlayed = true;
}

// RW 0x89CA84 (open geometry) / RW 0x89C9FA (closed geometry). `scan`: the push-out scan RW 0x89C759 runs first (not ported, S-1940)
void GateOpenAndCloseBehavior::setGeometry(bool openGeometry, bool scan)
{
	(void)scan;
	const int wanted = openGeometry ? GEOMETRY_OPEN : GEOMETRY_CLOSED;
	if (m_geometryState == wanted)
	{
		return;
	}
	Object *obj = getObject();
	AIWorld *ai = obj->logic().aiWorld();
	if (ai)
	{
		ai->removeObjectFromPathfindMap(*obj); // RW 0x6E861E
	}
	m_geometryState = wanted;
	for (const std::string &n : m_data->m_geometryForOpen)
	{
		obj->setGeometryActive(n, openGeometry); // RW 0xAD3520
	}
	for (const std::string &n : m_data->m_geometryForClosed)
	{
		obj->setGeometryActive(n, !openGeometry);
	}
	if (ai)
	{
		ai->addObjectToPathfindMap(*obj); // RW 0x68B244(1), RW 0x6E85E9
	}
}

// RW 0x89CE0F .. 0x89CE33: (frame - state frame) / reset time * 100 at the x87's PC24 precision (fild of the signed elapsed frames, fild of the unsigned reset
// time with the 2^32 correction, fdivp, fmul 100.0f RW 0xBD88D8, fst dword)
float GateOpenAndCloseBehavior::elapsedPercent() const
{
	// the state frame is a past frame (set by open / close / the constructor), so the signed fild of the difference equals its unsigned value
	const UnsignedInt elapsed = getObject()->logic().getFrame() - m_stateFrame;
	const double q = SimMath::pc24DivW(SimMath::fildU32(elapsed), SimMath::fildU32(m_data->m_resetTime));
	return SimMath::fstpDword(SimMath::pc24MulW(q, 100.0));
}

// RW 0x89CC29
UpdateSleepTime GateOpenAndCloseBehavior::update()
{
	Object *obj = getObject();
	if (!obj)
	{
		return UPDATE_SLEEP_NONE;
	}
	if (obj->isUnderConstruction()) // status 2
	{
		m_settled = false;
	}
	if (obj->isEffectivelyDead())
	{
		if (m_state != OPEN)
		{
			m_state = OPENING;
			m_percent = 100.0f;
		}
		setGeometry(true, true);
		m_settled = false;
		return UPDATE_SLEEP_NONE;
	}
	if (m_request >= 0 && isSettled() && isOpen() != (m_request > 0))
	{
		toggle();
		if (m_request == 0)
		{
			m_request = -1;
		}
	}
	const UnsignedInt elapsed = obj->logic().getFrame() - m_stateFrame;
	switch (m_state)
	{
		case CLOSED:
			m_percent = 100.0f;
			m_settled = true;
			clearAndSet(*obj, doorOpening(), doorClosing());
			setGeometry(false, true);
			break;
		case CLOSING:
		{
			m_percent = elapsedPercent();
			if (!(m_percent < 100.0f)) // RW 0x89CE3E: fcompi, jb skips
			{
				setOpenCloseState(CLOSED);
			}
			if (m_percent > (float)SimMath::fstpDword(SimMath::fildU32(100u - m_data->m_percentOpenForPathing)))
			{
				setGeometry(false, true);
			}
			const unsigned soundTime = m_data->m_timeBeforeClosedSound == 0xFFFFFFFFu ? m_data->m_resetTime : m_data->m_timeBeforeClosedSound; // RW 0x89BED1
			if (elapsed > soundTime && !m_soundPlayed)
			{
				playFinishedSound();
			}
			m_settled = false;
			clearAndSet(*obj, doorOpening(), doorClosing());
			break;
		}
		case OPEN:
			m_percent = 100.0f;
			m_settled = true;
			clearAndSet(*obj, doorClosing(), doorOpening());
			setGeometry(true, true);
			break;
		case OPENING:
		{
			m_percent = elapsedPercent();
			if (!(m_percent < 100.0f))
			{
				setOpenCloseState(OPEN);
			}
			if (m_percent > (float)SimMath::fstpDword(SimMath::fildU32(m_data->m_percentOpenForPathing)))
			{
				setGeometry(true, true);
			}
			const unsigned soundTime = m_data->m_timeBeforeOpenSound == 0xFFFFFFFFu ? m_data->m_resetTime : m_data->m_timeBeforeOpenSound; // RW 0x89BEC2
			if (elapsed > soundTime && !m_soundPlayed)
			{
				playFinishedSound();
			}
			m_settled = false;
			clearAndSet(*obj, doorClosing(), doorOpening());
			break;
		}
	}
	if (obj->isUnderConstruction())
	{
		m_settled = false;
	}
	return UPDATE_SLEEP_NONE;
}

void GateOpenAndCloseBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32((std::uint32_t)m_state);
	hasher.addU32((std::uint32_t)m_geometryState);
	hasher.addBool(m_settled);
	hasher.addFloat(m_percent);
	hasher.addU32(m_stateFrame);
	hasher.addU32((std::uint32_t)m_request);
	hasher.addBool(m_soundPlayed);
}

// ---- AIGateUpdate ------------------------------------------------------------------------------------------------------------------------
AIGateUpdate::AIGateUpdate(Thing *thing, const AIGateUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

// RW 0x8B4BB7 (the corners RW 0x8B4B31)
void AIGateUpdate::loadTrigger()
{
	const float zero = 0.0f;
	if (m_data->m_triggerWidthX > zero && m_data->m_triggerWidthY > zero)
	{
		Object *obj = getObject();
		const float hx = SimMath::mulf32(m_data->m_triggerWidthX, 0.5f); // RW 0xBD869C
		const float hy = SimMath::mulf32(m_data->m_triggerWidthY, 0.5f);
		const float angle = obj->getOrientation();
		const float s = SimMath::sinf32(angle);
		const float c = SimMath::cosf32(angle);
		const Coord3D &pos = *obj->getPosition();
		TriggerArea area;
		static int s_counter = 0; // RW 0xA03D80 (a process-wide counter in retail: the name only, not the logic)
		area.name = "AIGateUpdateTrigger_" + std::to_string(s_counter++);
		const float corners[4][2] = { { hx, hy }, { hx, -hy }, { -hx, -hy }, { -hx, hy } };
		for (const auto &k : corners)
		{
			Point2F p;
			p.x = SimMath::addf32(pos.x, SimMath::subf32(SimMath::mulf32(c, k[0]), SimMath::mulf32(s, k[1])));
			p.y = SimMath::addf32(pos.y, SimMath::addf32(SimMath::mulf32(c, k[1]), SimMath::mulf32(s, k[0])));
			area.points.push_back(p);
		}
		GameLogic &logic = obj->logic();
		const ObjectID gateId = obj->getID();
		m_trigger = logic.scriptEngine().addPolygonTrigger(area, [&logic, gateId](Object &other, bool entered) {
			Object *gate = logic.findObjectByID(gateId);
			AIGateUpdate *u = gate ? dynamic_cast<AIGateUpdate *>(gate->findModule("AIGateUpdate")) : nullptr;
			if (u)
			{
				u->onTrigger(other, entered);
			}
		});
	}
	m_loaded = true;
}

// RW 0x8B4AF4: the gate's relationship to the object: ALLIES counts +0x28, ENEMIES +0x2C
void AIGateUpdate::onTrigger(Object &obj, bool entered)
{
	const Relationship r = getObject()->getRelationship(obj);
	const int d = entered ? 1 : -1;
	if (r == ALLIES)
	{
		m_allies += d;
	}
	else if (r == ENEMIES)
	{
		m_enemies += d;
	}
}

// RW 0x8B4D34
UpdateSleepTime AIGateUpdate::update()
{
	Object *obj = getObject();
	GateOpenAndCloseBehavior *gate = obj ? GateOpenAndCloseBehavior::findGate(*obj) : nullptr; // RW 0x89BEE0
	if (!m_enabled)
	{
		const Player *owner = obj ? obj->getControllingPlayer() : nullptr;
		m_enabled = owner && obj->logic().skirmishAI().findAI(owner->getPlayerIndex()) != nullptr && gate != nullptr; // RW 0x6A950B
		if (!m_enabled)
		{
			return UPDATE_SLEEP_FOREVER;
		}
	}
	if (!m_loaded)
	{
		loadTrigger();
	}
	if (gate && gate->isSettled())
	{
		if (gate->isOpen())
		{
			if (m_allies == 0)
			{
				gate->close(); // RotWK RW 0x8B4DA6: no ally inside
			}
		}
		else if (m_allies > 0)
		{
			gate->open();
		}
	}
	return UPDATE_SLEEP_NONE;
}

void AIGateUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32((std::uint32_t)m_trigger);
	hasher.addU32((std::uint32_t)m_allies);
	hasher.addU32((std::uint32_t)m_enemies);
	hasher.addBool(m_loaded);
	hasher.addBool(m_enabled);
}

// ---- registration, messages ----------------------------------------------------------------------------------------------------------
void GateModules::registerAll(ModuleFactory &modules)
{
	bind<GateOpenAndCloseBehavior, GateOpenAndCloseBehaviorModuleData>(modules, "GateOpenAndCloseBehavior");
	bind<AIGateUpdate, AIGateUpdateModuleData>(modules, "AIGateUpdate");
}

void GateModules::registerHandlers(GameLogicDispatch &dispatch)
{
	// RW 0x77BF7B / 0x77C037: argument 0's object, its gate; no owner test
	for (const int type : { (int)MSG_OPEN_GATE, (int)MSG_CLOSE_GATE })
	{
		dispatch.registerHandler(type, "HUD-5", [type](GameLogic &logic, const GameMessage &m) {
			const GameMessageArgument *a = m.getArgument(0);
			Object *obj = a && a->type == ARGUMENTDATATYPE_OBJECTID ? logic.findObjectByID(a->objectID) : nullptr;
			GateOpenAndCloseBehavior *gate = obj ? GateOpenAndCloseBehavior::findGate(*obj) : nullptr;
			if (!gate)
			{
				return true;
			}
			if (type == MSG_OPEN_GATE)
			{
				if (!gate->isOpen() && gate->isSettled())
				{
					gate->open();
				}
			}
			else if (gate->isOpen() && gate->isSettled())
			{
				gate->close();
			}
			return true;
		});
	}
}

std::vector<std::string> GateModules::acceptanceStops()
{
	return {
		"[S-1940] gates (HUD-5): GateOpenAndCloseBehavior's states, timing, model conditions and open / closed geometry (RW 0x89CC29), MSG_OPEN_GATE / MSG_CLOSE_GATE "
		"(RW 0x77BF7B / 0x77C037), the gate buttons (RW 0x9410D7 / 0x9436E9) and AIGateUpdate (RW 0x8B4D34) run; not ported: the push-out scan of the units in "
		"the gate's way (RW 0x89C759), the collide interface's repel (RW 0x89C6F5, RepelCollidingUnits), the object interface call RW 0x68C3E6 at the start of the "
		"update, the AI notification RW 0x68BE59(9), the pathfinder's gate-flag keeping removal (RW 0x6E861E: the plain removal here), the skirmish AI's gate list "
		"(TheSkirmishAIManager + 0xA74) and FakePathfindPortalBehaviour",
		"[S-1941] GateProxyBehavior and the Proxy link of GateOpenAndCloseBehavior (onObjectCreated RW 0x89CB19, Orthanc) are not ported: the messages and buttons find "
		"only a GateOpenAndCloseBehavior",
		"[S-1942] the gate sounds (SoundOpeningGateLoop / SoundFinishedOpeningGate / SoundClosingGateLoop / SoundFinishedClosingGate, RW 0x89C261 / 0x89C1A4) are "
		"recorded, not played: the logic has no object sound path",
	};
}
