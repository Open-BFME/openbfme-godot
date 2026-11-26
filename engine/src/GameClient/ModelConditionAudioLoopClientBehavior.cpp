// OpenBFME. GPL-3.0.
// See GameClient/ModelConditionAudioLoopClientBehavior.h for the target facts and the addresses. Client presentation: no simulation state.

#if defined(__GNUC__) || defined(__clang__)
// RW 0x8D09E1 (the ModelCondition field's parse)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameClient/ModelConditionAudioLoopClientBehavior.h"

#include "Common/AsciiString.h"
#include "Common/Audio/GameAudio.h"
#include "Common/INIException.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameClient/AnimationSoundClientBehavior.h"
#include "GameClient/Drawable.h"
#include "GameLogic/ContainParseHooks.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>

namespace
{
bool same(const char *a, const char *b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0; // RW _strcmpi (RW 0xBD051C)
}

#define MCAL_OFF(member) (int)offsetof(ModelConditionAudioLoopClientBehaviorModuleData, member)
// RW 0xC761A4
const FieldParse kFieldParse[] = {
	{ "ModelCondition", ModelConditionAudioLoopClientBehaviorModuleData::parseModelCondition, nullptr, MCAL_OFF(m_entries) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef MCAL_OFF
} // namespace

void ModelConditionAudioLoopClientBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFieldParse);
}

void ModelConditionAudioLoopClientBehaviorModuleData::parseModelCondition(INI *ini, void *, void *store, const void *)
{
	const char *const seps = ini->getSepsColon(); // RW INI + 0x420 (" \n\r\t=:", RW 0x42CB02)
	ModelConditionAudioLoopEntry entry;
	// RW 0x4B5DA2: a name that is no flag ends the list and is read as the next keyword
	auto readFlags = [&](ModelConditionFlags &mask) {
		const char *tok = ini->getNextTokenOrNull(seps);
		while (tok)
		{
			const int bit = ModelCondition::indexOf(tok);
			if (bit < 0)
			{
				break;
			}
			mask.set(bit);
			tok = ini->getNextTokenOrNull(seps);
		}
		return tok;
	};
	const char *tok = ini->getNextTokenOrNull(seps);
	while (tok)
	{
		if (same(tok, "Required"))
		{
			tok = readFlags(entry.required);
		}
		else if (same(tok, "Excluded"))
		{
			tok = readFlags(entry.excluded);
		}
		else if (same(tok, "Sound"))
		{
			tok = ini->getNextToken(seps); // RW 0x42DC9F: the value must exist
			if (!same(tok, "NoSound"))     // RW 0x8D08FB: NoSound stays the next keyword, which the next round refuses
			{
				// TheAudio's event table (RW vslot 0x12C): the world's parsed table during an object load, or a test's own; neither installed is a loud error (S-083's rule)
				const std::function<bool(const std::string &)> &exists =
					AnimationSoundClientBehaviorModuleData::soundExists() ? AnimationSoundClientBehaviorModuleData::soundExists() : TheContainParseHooks().audioEventExists;
				if (!exists)
				{
					throw INIException(8, "ModelCondition line: TheAudio's event table is not installed");
				}
				if (!exists(tok))
				{
					throw INIException(3, "Unknown sound %s", tok); // RW 0x8D099D
				}
				entry.sound = tok;
				tok = ini->getNextTokenOrNull(seps);
			}
		}
		else
		{
			throw INIException(5, "Expected one of %s, %s, or %s next", "Required", "Excluded", "Sound"); // RW 0x8D09B3
		}
	}
	static_cast<std::vector<ModelConditionAudioLoopEntry> *>(store)->push_back(entry); // RW 0x8D0977
}

const std::string &ModelConditionAudioLoopClientBehaviorModuleData::selectSound(const ModelConditionFlags &flags) const
{
	static const std::string none;
	for (const ModelConditionAudioLoopEntry &e : m_entries)
	{
		if (flags.testForAll(e.required) && flags.testForNone(e.excluded)) // RW 0x5DF56A
		{
			return e.sound;
		}
	}
	return none;
}

ModelConditionAudioLoopClientBehavior::ModelConditionAudioLoopClientBehavior(Thing *thing, const ModelConditionAudioLoopClientBehaviorModuleData *data)
	: DrawableModule(thing, data)
	, m_data(data)
{
	Drawable *d = getDrawable();
	m_manager = d ? d->drawServices().animationSounds : nullptr;
	if (m_manager)
	{
		m_manager->addAudioLoop(this);
	}
}

// RW 0x008D055C (~ModelConditionAudioLoopClientBehavior), RW 0x008D05D7 (scalar deleting dtor)
ModelConditionAudioLoopClientBehavior::~ModelConditionAudioLoopClientBehavior()
{
	clear(); // RW 0x8D055C
	if (m_manager)
	{
		m_manager->removeAudioLoop(this);
	}
}

AudioManager *ModelConditionAudioLoopClientBehavior::audio() const
{
	return m_manager && m_manager->audio ? m_manager->audio() : nullptr;
}

void ModelConditionAudioLoopClientBehavior::clear()
{
	AudioManager *a = audio();
	if (a && m_handle >= AHSV_FirstHandle) // RW 0x8D0401 .. 0x8D040B
	{
		a->removeAudioEvent(m_handle); // TheAudio vslot 0x6C
		m_handle = AHSV_NoSound;
		m_held.clear(); // RW 0x8D041E
	}
}

void ModelConditionAudioLoopClientBehavior::modelConditionFlagsChanged(const ModelConditionFlags &flags)
{
	const std::string &selected = m_data->selectSound(flags);
	if (AsciiStringUtil::compareNoCase(selected.c_str(), m_held.c_str()) == 0)
	{
		return; // RW 0x8D0454: the same event
	}
	clear();
	m_held = selected;
	if (m_held.empty())
	{
		return;
	}
	Drawable *d = getDrawable();
	AudioManager *a = audio();
	if (d && a)
	{
		m_handle = a->playSoundForDrawable(m_held, (std::uint32_t)d->getID()); // RW 0x6DB62D (owner type 1), TheAudio vslot 0x64
		m_playedOn = a;
	}
}

void ModelConditionAudioLoopClientBehavior::becameAudible()
{
	if (Drawable *d = getDrawable())
	{
		modelConditionFlagsChanged(d->getModelConditionFlags()); // RW 0x8D04D9: + 0x258
	}
}

void ModelConditionAudioLoopClientBehavior::becameInaudible()
{
	clear();
}

void ModelConditionAudioLoopClientBehavior::audioAttached()
{
	if (m_playedOn && m_playedOn == audio())
	{
		return; // a change after the attachment already played here: its handle is good, and forgetting it would leave the loop playing
	}
	audioGone();
	becameAudible();
}

void ModelConditionAudioLoopClientBehavior::audioGone()
{
	m_handle = AHSV_NoSound;
	m_held.clear();
	m_playedOn = nullptr;
}

// RW 0x008D0380 (getModuleName), RW 0x008D0386 (getModuleNameKey), RW 0x008CE37F (shared empty ClientBehavior::xfer)
void ModelConditionAudioLoopClientBehavior::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<ModelConditionAudioLoopClientBehaviorModuleData>("ModelConditionAudioLoopClientBehavior", MODULETYPE_CLIENT_BEHAVIOR);
	modules.bindModuleProc("ModelConditionAudioLoopClientBehavior", MODULETYPE_CLIENT_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const ModelConditionAudioLoopClientBehaviorModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("ModelConditionAudioLoopClientBehavior: the module data is not typed");
			}
			return std::make_unique<ModelConditionAudioLoopClientBehavior>(thing, typed);
		});
}
