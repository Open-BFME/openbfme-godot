// OpenBFME. GPL-3.0.
// See GameLogic/Module/LargeGroupAudioUpdate.h for the target facts and the addresses (lane AUDIO-4).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/LargeGroupAudioUpdate.h"
#include "GameLogic/SnapshotXfer.h"

#include "Common/GameCommon.h"
#include "Common/NumericState.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LargeGroupAudioLink.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <cstddef>
#include <stdexcept>

namespace
{
// RW 0x7EEC77: every token up to the end of the line is a key (RW 0x7EEB0C adds it once to the data's set)
void parseKeys(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> &keys = *static_cast<std::vector<std::string> *>(store);
	while (const char *t = ini->getNextTokenOrNull())
	{
		bool known = false;
		for (const std::string &k : keys)
		{
			known = known || k == t; // RW 0x7EE97A: the interned name (exact bytes); a key already in the set is not added again
		}
		if (!known)
		{
			keys.push_back(t);
		}
	}
}

#define LGA_OFF(member) (int)offsetof(LargeGroupAudioUpdateModuleData, member)
// RW 0xC6B380, in the table's order
const FieldParse kLargeGroupAudioUpdateFieldParse[] = {
	{ "TimeBetweenUpdatesMin", INI::parseDurationUnsignedInt, nullptr, LGA_OFF(m_timeBetweenUpdatesMin) },
	{ "TimeBetweenUpdatesVariation", INI::parseDurationUnsignedInt, nullptr, LGA_OFF(m_timeBetweenUpdatesVariation) },
	{ "UnitWeight", INI::parseUnsignedShort, nullptr, LGA_OFF(m_unitWeight) },
	{ "Key", parseKeys, nullptr, LGA_OFF(m_keys) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef LGA_OFF

const int kInaudible = ObjectTemplateInfoBuilder::objectStatusIndex("INAUDIBLE"); // 52: Object + 0x98 bit 20 (RW 0x8AF018 .. 0x8AF025)
} // namespace

LargeGroupAudioUpdateModuleData::LargeGroupAudioUpdateModuleData()
{
	// RW 0x8AF565 .. 0x8AF587: fld 0.005f (RW 0xD9F610), fmul 500.0f (RW 0xBDD97C), CRT ceil, ftol: the duration parser's own product for 500 ms
	m_timeBetweenUpdatesMin = NumericState::ceilScaled(500u, LOGICFRAMES_PER_MSEC_REAL);
}

void LargeGroupAudioUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kLargeGroupAudioUpdateFieldParse);
}

LargeGroupAudioUpdate::LargeGroupAudioUpdate(Thing *thing, const LargeGroupAudioUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

UpdateSleepTime LargeGroupAudioUpdate::sleepTime() const
{
	// RW 0x8AEEC3: GameLogicRandomValue(0, variation, file, 0xA7) + min + 1 (`lea eax, [eax + ecx + 1]`)
	const int r = getObject()->logic().random().getValue(0, (int)m_data->m_timeBetweenUpdatesVariation, "LargeGroupAudioUpdate.cpp", 0xA7); // RW 0x6D328E
	return UPDATE_SLEEP((int)((std::uint32_t)r + m_data->m_timeBetweenUpdatesMin + 1u));
}

void LargeGroupAudioUpdate::fillEvent(LargeGroupAudioEvent &e, bool stealthed) const
{
	// Member interface (module + 0x24, vtable RW 0xC6B3D0): slots 0..10 (slot 11 is deleting dtor thunk RW 0x008AEF8A -> RW 0x008AF216 -> RW 0x008AEFD7)
	const Object *obj = getObject();
	e.frame = obj->logic().getFrame();
	e.object = obj->getID();
	e.keys = &m_data->m_keys;                    // slot 7: RW 0x008AF1BF (DONOR BFME2 0x004ABAC4)
	e.weight = m_data->m_unitWeight;             // slot 6: RW 0x008AF1B7 (DONOR BFME2 0x004ABABC)
	e.storedX = m_storedX;                       // slot 0: RW 0x008AF17C (DONOR BFME2 0x004ABA81)
	e.storedY = m_storedY;
	e.x = obj->getPosition()->x;                 // slot 1: RW 0x008AF18E (DONOR BFME2 0x004ABA93)
	e.y = obj->getPosition()->y;
	e.storedConditions = m_storedConditions;     // slot 2: RW 0x009D39B1
	e.conditions = obj->getModelConditionBits(); // slot 3: RW 0x008AF1A5 (DONOR BFME2 0x004ABAAA)
	e.storedStatus = m_storedStatus;             // slot 4: RW 0x008F1161
	e.status = obj->getStatusBits();             // slot 5: RW 0x008AF1AE (DONOR BFME2 0x004ABAB3)
	e.storedStealthed = m_storedStealthed;       // slot 9: RW 0x008A4A91
	e.stealthed = stealthed;                     // slot 8: RW 0x008AF1C6 -> RW 0x00694C0D(0) (DONOR BFME2 0x004ABACB)
	e.storedFrame = (UnsignedInt)m_storedFrame;  // slot 10: RW 0x006DA64A
}

void LargeGroupAudioUpdate::store(bool stealthed)
{
	const Object *obj = getObject();
	m_storedX = obj->getPosition()->x;
	m_storedY = obj->getPosition()->y;
	m_storedConditions = obj->getModelConditionBits();
	m_storedStatus = obj->getStatusBits();
	m_storedStealthed = stealthed;
	m_storedFrame = (std::int32_t)obj->logic().getFrame();
}

void LargeGroupAudioUpdate::join()
{
	Object *obj = getObject();
	if (m_registered || (kInaudible >= 0 && obj->testStatus((unsigned)kInaudible)))
	{
		return;
	}
	m_registered = true;
	const bool stealthed = InvisibilityManager::isStealthedAndUndetected(*obj, nullptr); // RW 0x8AF07F: RW 0x694C0D(0)
	LargeGroupAudioEvent e;
	e.kind = LargeGroupAudioEvent::ADD;
	fillEvent(e, stealthed);
	obj->logic().largeGroupAudio().notify(e); // RW 0x8AF03B: RW 0x60D5FF
	setWakeFrame(obj, sleepTime());           // RW 0x8AF046 .. 0x8AF04F
	store(stealthed);                         // RW 0x8AF054 .. 0x8AF094
}

void LargeGroupAudioUpdate::leave()
{
	if (!m_registered)
	{
		return;
	}
	Object *obj = getObject();
	LargeGroupAudioEvent e;
	e.kind = LargeGroupAudioEvent::REMOVE;
	fillEvent(e, m_storedStealthed);
	obj->logic().largeGroupAudio().notify(e); // RW 0x8AF0B3: RW 0x60D633
	setWakeFrame(obj, UPDATE_SLEEP_FOREVER);   // RW 0x8AF0C2
	m_registered = false;
}

void LargeGroupAudioUpdate::onObjectCreated()
{
	join(); // RW 0x8AF232: only when not registered
}

void LargeGroupAudioUpdate::onDelete()
{
	leave(); // RW 0x8AF241 -> 0x8AF09D
}

UpdateSleepTime LargeGroupAudioUpdate::update()
{
	Object *obj = getObject();
	if (!m_registered || !obj)
	{
		return sleepTime(); // RW 0x8AF25B / 0x8AF26D
	}
	const bool stealthed = InvisibilityManager::isStealthedAndUndetected(*obj, nullptr); // RW 0x8AF28B: RW 0x694C0D(0)
	const Coord3D &pos = *obj->getPosition();
	LargeGroupAudioLink &link = obj->logic().largeGroupAudio();
	// RW 0x8AF290 .. 0x8AF2EA: ucomiss x, y (an unordered compare counts as changed), the flags (RW 0x4B37BC), the status (RW 0x66329B), the stealthed
	// byte, then the stored frame against TheLargeGroupAudio + 0x3C (signed: `jg` skips the notification)
	const bool same = pos.x == m_storedX && pos.y == m_storedY && obj->getModelConditionBits() == m_storedConditions && obj->getStatusBits() == m_storedStatus &&
		stealthed == m_storedStealthed && m_storedFrame > link.gateFrame();
	if (!same)
	{
		LargeGroupAudioEvent e;
		e.kind = LargeGroupAudioEvent::UPDATE;
		fillEvent(e, stealthed);
		link.notify(e);  // RW 0x8AF2FF: RW 0x60D5CB with the stored values still in place
		store(stealthed); // RW 0x8AF304 .. 0x8AF344
	}
	return sleepTime(); // RW 0x8AF34D
}

void LargeGroupAudioUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addFloat(m_storedX);
	h.addFloat(m_storedY);
	for (std::uint32_t w : m_storedConditions)
	{
		h.addU32(w);
	}
	for (std::uint32_t w : m_storedStatus)
	{
		h.addU32(w);
	}
	h.addBool(m_storedStealthed);
	h.addBool(m_registered);
	h.addI32(m_storedFrame);
}

void LargeGroupAudioUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<LargeGroupAudioUpdateModuleData>("LargeGroupAudioUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("LargeGroupAudioUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const LargeGroupAudioUpdateModuleData *typed = dynamic_cast<const LargeGroupAudioUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("LargeGroupAudioUpdate: the module data is not typed");
		}
		return std::make_unique<LargeGroupAudioUpdate>(thing, typed);
	});
}

// ---- lane SAVE-4b: the snapshot (OpenBFME native, S-4190: the crc's fields; the large group audio's own registry is SAVE-4d's obfme:LargeGroupAudio) ---
void LargeGroupAudioUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferReal(&m_storedX);
	x.xferReal(&m_storedY);
	SnapshotXfer::maskWords(x, m_storedConditions, SnapshotXfer::modelConditionCount(), "a large group audio's stored conditions", "the model conditions"); // lane SAVE-4e
	SnapshotXfer::maskWords(x, m_storedStatus, SnapshotXfer::objectStatusCount(), "a large group audio's stored status", "the object status names");
	x.xferBool(&m_storedStealthed);
	x.xferBool(&m_registered);
	x.xferInt(&m_storedFrame);
}
