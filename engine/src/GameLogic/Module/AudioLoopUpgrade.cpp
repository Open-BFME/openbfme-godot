// OpenBFME. GPL-3.0.
// See GameLogic/Module/AudioLoopUpgrade.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/AudioLoopUpgrade.h"

#include "Common/Audio/AudioRequests.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <atomic>
#include <cstddef>
#include <stdexcept>

namespace
{
typedef AudioLoopUpgradeModuleData Data;

// RW 0xC6F9F8
const FieldParse kFields[] = {
	{ "SoundToPlay", OpenContainModuleData::parseAudioEvent, nullptr, (int)offsetof(Data, m_soundToPlay) }, // RW 0x73B217
	{ "KillAfterMS", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_killAfter) },          // RW 0x73A429
	{ "KillOnDeath", INI::parseBool, nullptr, (int)offsetof(Data, m_killOnDeath) },                       // RW 0x42E558
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xC76BD8 (the DieMux data at +0x144)
const FieldParse kDieMux[] = {
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, (int)offsetof(Data, m_dieMux.m_deathTypes) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, (int)offsetof(Data, m_dieMux.m_exemptStatus) },
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, (int)offsetof(Data, m_dieMux.m_requiredStatus) },
	{ "DamageAmountRequired", INI::parseReal, nullptr, (int)offsetof(Data, m_dieMux.m_damageAmountRequired) },
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, (int)offsetof(Data, m_dieMux.m_minKillerAngle) },
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, (int)offsetof(Data, m_dieMux.m_maxKillerAngle) },
	{ nullptr, nullptr, nullptr, 0 }
};

// the audio side's holder keys of the loops (one per module instance; not logic state)
std::uint32_t nextSoundKey()
{
	static std::atomic<std::uint32_t> next{ 1 };
	return next.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

// the registry's order: the upgrade table (extra 20), the DieMux table (extra 324), RW 0xC6F9F8
void AudioLoopUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p); // RW 0xC76AD8
	p.add(kDieMux);         // RW 0xC76BD8
	p.add(kFields);         // RW 0xC6F9F8
}

void AudioLoopUpgrade::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "AudioLoopUpgrade";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<AudioLoopUpgrade>(thing, typed);
	});
}

// RW 0x8BBDDA: handle = 1, the update asleep forever
AudioLoopUpgrade::AudioLoopUpgrade(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, UpgradeMux(thing ? thing->asObject() : nullptr, data)
	, m_data(data)
	, m_soundKey(nextSoundKey())
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x8BBC4F (the destructor, deleting form RW 0x8BBE55 = primary vtable slot 0): TheAudio slot 0x6C removes the handle with the module
AudioLoopUpgrade::~AudioLoopUpgrade()
{
	if (m_soundHeld)
	{
		AudioApi::stopHeldSound(AudioApi::HELD_UPGRADE_LOOP, m_soundKey);
	}
}

void AudioLoopUpgrade::startSound()
{
	// RW 0x8BBE92 .. 0x8BBED5: a held handle is removed, then SoundToPlay for the object's id is added and its handle kept (postHeldSound does both)
	AudioApi::postHeldSound(AudioApi::HELD_UPGRADE_LOOP, m_data->m_soundToPlay.name, m_soundKey, getObject()->getID());
	m_soundHeld = true;
	++m_soundStarts;
}

void AudioLoopUpgrade::stopSound()
{
	if (m_soundHeld) // RW removes handle 1 too: a no-op on the audio side
	{
		AudioApi::stopHeldSound(AudioApi::HELD_UPGRADE_LOOP, m_soundKey);
		++m_soundStops;
	}
	m_soundHeld = false; // handle = 1
}

// RW 0x8BBE71
void AudioLoopUpgrade::upgradeImplementation()
{
	startSound();
	if (m_data->m_killAfter > 0) // unsigned compare (jbe)
	{
		setWakeFrame(getObject(), (UpdateSleepTime)m_data->m_killAfter); // RW 0x850C32(obj, KillAfterMS)
	}
}

// RW 0x8BBEFE
void AudioLoopUpgrade::processUpgradeRemoval()
{
	stopSound();
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x8BBCB6
UpdateSleepTime AudioLoopUpgrade::update()
{
	stopSound();
	return UPDATE_SLEEP_FOREVER;
}

// RW 0x8BBF5A (no DieMux test)
void AudioLoopUpgrade::onDie(const DieModuleInterface::Event &)
{
	if (!m_data->m_killOnDeath)
	{
		return;
	}
	stopSound();
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// the logic state: the scheduler's and the mux's (the sound is the audio side's and is not hashed)
void AudioLoopUpgrade::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	crcMux(hasher);
}

// RW 0x8BBF93: version (1, 1), the audio handle (here: whether it is held), UpdateModule, the mux
void AudioLoopUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	bool held = m_soundHeld;
	x.xferBool(&held); // RW: TheAudio->xferAudioHandle(+0x2C)
	xferUpdateModule(x); // RW 0x850C60
	xferMux(x);          // RW 0x8D27E8
	if (x.isLoading())
	{
		if (held)
		{
			startSound(); // inference: the loaded game hears the loop it saved
		}
		else
		{
			stopSound();
		}
	}
}
