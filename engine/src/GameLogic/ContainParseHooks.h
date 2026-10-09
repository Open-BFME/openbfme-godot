// OpenBFME. GPL-3.0.
//
// Name lookups the contain module data parsers perform against stores this lane does not own. Retail
// resolves these names at parse time through globals: TheEva (RW 0xDE3670), TheAudio (RW 0xDE42FC),
// TheWeaponStore (RW 0xDE4A1C). Until those stores exist the owner of the load installs the lookup
// here; a field that needs a lookup that is not installed is a loud INIException (code 8), never a
// silently accepted name (PLAN rule 10, acceptance stop S-083).
//
// TARGET FACTS (RW):
//   * audio events (EnterSound / ExitSound / InitiateVoice; RW 0x73B217, 0x73AA94, 0x73AB45): "NoSound"
//     (stricmp) clears; otherwise TheAudio is asked for the event and an unknown name is
//     INIException(3, "Invalid Sound '%s'"). 0x73AB45 (ComboHorde InitiateVoice) also accepts
//     "EVA:<event>" (TheEva lookup, unknown: INIException(3, "Unknown EVA event in EVA:%s"); TheEva
//     missing: code 8) and "+SOUND:<event>".
//   * EVA event index (EvaEventLastMemberDeath; RW 0x5DE588 -> 0x5DE0D8): "None" (stricmp) is -1; an
//     unknown name throws after 0x5DE14E (message not decoded here).
//   * weapon templates (GrabWeapon, ThrowOutPassengersLandingWarhead; RW 0x73AE79 -> 0x6CC5DF): the
//     template pointer, null when the name is unknown (no error).

#pragma once

#include <functional>
#include <string>

struct ContainParseHooks
{
	std::function<int(const std::string &name)> evaEventIndex;          // -1 when unknown
	std::function<bool(const std::string &name)> audioEventExists;
	std::function<bool(const std::string &name)> weaponTemplateExists;
};

// The process-wide hooks (empty until installed).
ContainParseHooks &TheContainParseHooks();
