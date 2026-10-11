// OpenBFME. GPL-3.0.
// See AptPalantir.h.

#include "GameClient/GUI/AptScreens/AptPalantir.h"

#include <cstdio>
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"

#include "GameClient/GUI/ShellServices.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{
// "index=3&name=[MovieClip]" -> the value of `key`
std::string queryValue(const std::string &arg, const std::string &key)
{
	const std::string needle = key + "=";
	size_t p = 0;
	while ((p = arg.find(needle, p)) != std::string::npos)
	{
		if (p == 0 || arg[p - 1] == '&')
		{
			const size_t b = p + needle.size();
			const size_t e = arg.find('&', b);
			return arg.substr(b, e == std::string::npos ? std::string::npos : e - b);
		}
		p += needle.size();
	}
	return std::string();
}
} // namespace

const std::vector<std::string> &AptPalantir::retailCommandNames()
{
	static const std::vector<std::string> names = {
		"AptPalantir::OnPlanningModeUIUnloaded", "AptPalantir::OnPlanningModeUILoaded", "AptPalantir::OnHeroSelectUnloaded", "AptPalantir::OnHeroSelectLoaded",
		"AptPalantir::OnHelpBoxUnloaded", "AptPalantir::OnHelpBoxLoaded", "AptPalantir::OnBttnMessenger", "AptPalantir::OnBttnObservePriorPlayer",
		"AptPalantir::OnBttnObserveNextPlayer", "AptPalantir::OnBttnMovie", "AptPalantir::OnBttnObjectives", "AptPalantir::OnBttnOptions", "AptPalantir::OnBttnSpellStore",
		"AptPalantir::OnClosed", "AptPalantir::OnInitialized",
		"PalantirCommandUI::OnToggleFlashUnloaded", "PalantirCommandUI::OnToggleFlashLoaded", "PalantirCommandUI::OnSubMenuUnloaded", "PalantirCommandUI::OnSubMenuLoaded",
		"PalantirCommandUI::OnButtonFrameUnloaded", "PalantirCommandUI::OnButtonFrameLoaded",
		"OnAptInGameSideCommandBarButtonFrameUnloaded", "OnAptInGameSideCommandBarButtonFrameLoaded", "OnAptInGameSideCommandBarFadeOutComplete",
		"OnAptInGameSideCommandBarFadeInComplete", "OnAptInGameSideCommandBarUnloaded", "OnAptInGameSideCommandBarLoaded",
		"OnAptInGameSpellBookButtonPressed", "OnAptInGameSpellBookShown", "OnAptInGameSpellBookUnloaded", "OnAptInGameSpellBookLoaded"
	};
	return names;
}

void AptPalantir::hook(const std::string &name)
{
	registerCommand(name, [this, name](const std::string &arg) {
		m_log.push_back(name + "(" + arg + ")");
		if (name == "AptPalantir::OnInitialized")
		{
			m_initialized = true;
			windows().note("palantir-initialized", "AptPalantir::OnInitialized: the HUD movie is running");
		}
		else if (name == "PalantirCommandUI::OnButtonFrameLoaded" || name == "OnAptInGameSideCommandBarButtonFrameLoaded")
		{
			m_buttonFrames.push_back({ queryValue(arg, "index"), queryValue(arg, "name") });
			frameLoaded(name == "PalantirCommandUI::OnButtonFrameLoaded", arg, true);
		}
		else if (name == "PalantirCommandUI::OnButtonFrameUnloaded" || name == "OnAptInGameSideCommandBarButtonFrameUnloaded")
		{
			frameLoaded(name == "PalantirCommandUI::OnButtonFrameUnloaded", arg, false);
		}
		else if (name.find("Loaded") != std::string::npos)
		{
			++m_loaded[name];
		}
		if ((name == "AptPalantir::OnHelpBoxLoaded" || name == "AptPalantir::OnHelpBoxUnloaded") && m_helpBoxHandler)
		{
			m_helpBoxHandler(arg, name == "AptPalantir::OnHelpBoxLoaded"); // lane HUD-6: RW 0x6D4F28 / its unload twin
		}
		// lane HUD-4: the side command bar's movie (RW 0x92EE5C / 0x92F07A: its clip kept at + 0x14; its frames and fades wait for it)
		if (name == "OnAptInGameSideCommandBarLoaded")
		{
			m_sideBarLoaded = true;
			sideBarLoaded(arg); // lane HUD-6: RW 0x92EE5C keeps the argument as the movie's prefix
		}
		else if (name == "OnAptInGameSideCommandBarUnloaded")
		{
			m_sideBarLoaded = false;
			for (std::string &f : m_sideFramePath)
			{
				f.clear();
			}
			// HUD-4 review (Sol): an unloaded side bar forgets what it showed, so a reload with the same selection recreates its content
			m_sideShown = false;
			for (Slot &s : m_side)
			{
				s = Slot();
			}
			sideBarLoaded(std::string()); // lane HUD-6: RW 0x92EE76 -> 0x92ED9C
		}
		// lane UI-4: the hero bar's movie (AptPalantir::OnHeroSelectLoaded makes InGameHeroSelectInterface, ctor RW 0x92DDE7, with the clip path)
		if (name == "AptPalantir::OnHeroSelectLoaded" || name == "AptPalantir::OnHeroSelectUnloaded")
		{
			const bool loaded = name == "AptPalantir::OnHeroSelectLoaded";
			m_heroSelectPath = loaded ? arg : std::string();
			if (m_heroSelect)
			{
				m_heroSelect(arg, loaded);
			}
		}
		// lane SPELL-2: the spell book's callbacks (RW 0x931698 / 0x9310B5 / 0x930DE5) and the spell store button
		if (name == "OnAptInGameSpellBookLoaded")
		{
			m_spellPath = arg; // RW 0x9316A9: the clip path
			m_spellShown = false;
		}
		else if (name == "OnAptInGameSpellBookUnloaded")
		{
			m_spellPath.clear();
			m_spellShown = false;
		}
		else if (name == "OnAptInGameSpellBookShown")
		{
			m_spellShown = true; // RW 0x930F0F: the slot cache restarts
			for (int i = 0; i < kSpellSlots; ++i)
			{
				m_spellState[i] = -1;
				m_spellButton[i] = -1;
			}
		}
		else if (name == "OnAptInGameSpellBookButtonPressed")
		{
			pressSpellSlot(std::atoi(arg.c_str())); // RW 0x930DEC
		}
		else if (name == "AptPalantir::OnBttnSpellStore" && m_spellStoreOpen)
		{
			m_spellStoreOpen();
		}
		else if (name == "AptPalantir::OnBttnObjectives")
		{
			// lane PLAY-1: RW 0x6D40C9 (the flag above the radar): PlayerTribute.apt in a skirmish / multiplayer game (RW 0x914EF0), the objectives otherwise
			if (m_services)
			{
				m_services->request(ShellRequest{ ShellAction::PalantirObjectives, arg });
			}
			else
			{
				windows().note("command-unwired", "AptPalantir::OnBttnObjectives: no shell services [S-1922]");
			}
		}
		else if (name == "AptPalantir::OnBttnOptions")
		{
			if (m_services)
			{
				m_services->request(ShellRequest{ ShellAction::ToggleQuitMenu, arg }); // RW 0x6D40C1 (lane END-2)
			}
			else
			{
				windows().note("command-unwired", "AptPalantir::OnBttnOptions: no shell services for the quit menu [S-1064]");
			}
		}
	});
}

AptPalantir::AptPalantir(WindowManager &windows, Shell &shell) : AptScreen(windows, shell, "Palantir.apt", "AptPalantir")
{
	for (const std::string &name : retailCommandNames())
	{
		hook(name);
	}
	// the movie reads extern.PalantirMinLOD at start (Palantir.apt InitialSetup). TARGET FACT (lane HUD-4): the provider RW 0x6D3F99 answers "1" when
	// [0xDE3B84] + 0x1778 <= 1 (the static LOD), else "0"; the extern value is an Apt string (RW 0x4A8C36 -> 0xAEB240), so even "0" is true and the movie goes on
	// to write extern.MinLOD (QA-1 U18)
	registerProvider("PalantirMinLOD", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "0";
		}
		return true;
	});
	// lane HUD-4 (QA-1 U18): InitialSetup (flags 0x16A00: register 2 is `extern`) then writes extern.MinLOD = true and traces extern.MinLOD. RotWK registers no
	// handler under "MinLOD" (the binary has no such string; the only extern registration of the Palantir is PalantirMinLOD, RW 0x6D693E), and its host answers
	// an unknown name as RW 0x623B47 / 0x623AD3 do: a read is the empty string (the buffer is cleared first), a write is dropped. That is the answer here; each
	// use is noted ("extern-retail-unhandled"), never silent
	registerProvider("MinLOD", [this](const std::string &, std::string &value, bool setting) {
		this->windows().note("extern-retail-unhandled", std::string("MinLOD ") + (setting ? "write dropped" : "read: \"\"") + " (no RotWK handler: RW 0x623B47 / 0x623AD3)");
		if (!setting)
		{
			value.clear();
		}
		return true;
	});
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the engine's side of the movie
// ---------------------------------------------------------------------------------------------------------------------------------
std::string AptPalantir::levelPrefix() const
{
	return "_level" + std::to_string(level()) + ".";
}

bool AptPalantir::call(const std::string &path, const std::string &fn, const std::vector<std::string> &args)
{
	++m_calls;
	std::string error;
	if (!windows().invokeASAt(level(), path, fn, args, nullptr, &error))
	{
		if (m_callErrors.size() < 200)
		{
			m_callErrors.push_back(path + "." + fn + ": " + error);
		}
		return false;
	}
	return true;
}

// RW 0x9D26D2 (TARGET FACT): the engine registers an extern provider under "_level%u.%s_ContentName" (the clip's path; string RW 0xC8C3BC) for the length
// of the CreateContent call (string RW 0xC8C3AC) and takes it away again; the movie's CreateContent writes the content's name into that extern, and the
// creation counts only when it did (the provider's flag decides the result). Without the provider every button creation wrote to an unregistered extern
// (thousands of "no extern provider" errors a game). INFERENCE: the content instance and name retail keeps from the provider are not used here.
bool AptPalantir::createContent(const std::string &frame)
{
	bool written = false;
	const std::string key = levelPrefix() + frame + "_ContentName";
	windows().registerProvider(key, [&written](const std::string &, std::string &, bool setting) {
		written = written || setting;
		return setting;
	});
	const bool ok = call(frame, "CreateContent", { "CommandButton", "content" });
	windows().unregisterProvider(key);
	return ok && written;
}

const AptPalantir::NativeImage *AptPalantir::imageFor(const std::string &key) const
{
	auto it = m_images.find(key);
	return it == m_images.end() ? nullptr : &it->second;
}

float AptPalantir::timerFor(const std::string &key) const
{
	auto it = m_timers.find(key);
	return it == m_timers.end() ? -1.0f : it->second;
}

// lane HUD-5: the clip state of a command button (RW 0x9D2BEE, from its window's status; the populate RW 0x943C04 sets the status from the availability):
// status 0x400000 (not ready) -> _notReady; an enabled window -> _up, or _static for a NONPRESSABLE button (RW 0x9D2BFA: (options >> 27) bit 1, option index 28,
// names RW 0xDA4C88); a disabled window: 0x40000000 -> _visuallyEnabled, RW 0x729854 -> _extraDisabled, 0x1000000 (can't afford) -> _cantAfford, else _disabled.
// The populate enables the window for an available (1) and an active (2) button alike (jump table RW 0x943D47 -> RW 0x943C7C), so an active button (the
// gate's TOGGLE_GATE) is _up, not _visuallyEnabled, whose clip takes no press. Restricted (0) -> winEnable(FALSE) (RW 0x943C1F); can't afford / not ready set
// 0x1000000 / 0x400000 on a disabled window (RW 0x943C51 / 0x943C4D)
const char *AptPalantir::commandStateName(ButtonState s, std::uint32_t options)
{
	switch (s)
	{
		case ButtonState::Enabled:
		case ButtonState::Active: return (options & COMMAND_OPTION_NONPRESSABLE) ? "_static" : "_up";
		case ButtonState::Restricted: return "_disabled";
		case ButtonState::CantAfford: return "_cantAfford";
		case ButtonState::NotReady: return "_notReady";
		default: return "_unused";
	}
}

// lane HUD-4 (QA-1 U19): TARGET FACTS (RotWK game.dat, caveat S-001): the engine keeps a button frame only after the movie reported it:
// PalantirCommandUI::OnButtonFrameLoaded (RW 0x9300F8) takes atoi(index) in [0, 6) (RW 0x930088) and the clip from `name` (level prefix removed, RW 0x815563)
// into its frame table ((index + 5) * 0x14), OnButtonFrameUnloaded (RW 0x930200) drops it; OnAptInGameSideCommandBarButtonFrameLoaded (RW 0x92EE8B) takes
// [0, 16) and only while the side bar's movie is loaded (+ 0x14), RW 0x92F1CB drops it. The update fills only positions whose frame exists (RW 0x92FF5C:
// `*frame == 0` does nothing; the side bar, RW 0x92F082, stops at the first missing frame), so no call reaches a clip before its frame loaded.
void AptPalantir::frameLoaded(bool arc, const std::string &arg, bool loaded)
{
	const std::string index = queryValue(arg, "index");
	const int i = std::atoi(index.c_str());
	const int limit = arc ? kArcPositions : 16;
	if (index.empty() || i < 0 || i >= limit || (!arc && !m_sideBarLoaded))
	{
		return;
	}
	std::string path = loaded ? queryValue(arg, "name") : std::string();
	if (path.rfind("_level", 0) == 0) // RW 0x815563: the clip path without its "_level<N>." prefix
	{
		const size_t dot = path.find('.');
		path = dot == std::string::npos ? std::string() : path.substr(dot + 1);
	}
	std::string &slot = arc ? m_arcFramePath[i] : m_sideFramePath[i];
	if (!loaded || slot.empty())
	{
		slot = path;
	}
	if (!loaded && i < (arc ? kArcPositions : kSidePositions))
	{
		(arc ? m_arc : m_side)[i] = Slot(); // the frame's content went with it
	}
}

void AptPalantir::syncFrames(const std::vector<ControlBarButton> &buttons, bool arc)
{
	const int positions = arc ? kArcPositions : kSidePositions;
	Slot *slots = arc ? m_arc : m_side;
	// lane HUD-4: the arc is positional (window i of the control bar, RW 0x930035: ControlBarButton::position 0 .. 5); the side bar packs its Radial buttons in
	// window order into its frames and stops at the first missing frame (RW 0x92F082: the movie has Button0 .. Button11): a Radial button beyond the frames is not
	// shown in retail either and is reported once, never dropped silently
	const ControlBarButton *at[kSidePositions] = {};
	for (size_t k = 0; k < buttons.size(); ++k)
	{
		const ControlBarButton &b = buttons[k];
		const int p = arc ? b.position : (int)k;
		if (p >= 0 && p < positions)
		{
			at[p] = &b;
			continue;
		}
		const std::string name = b.button ? b.button->m_name : std::string("?");
		if (m_overflowReported.insert(name).second && m_callErrors.size() < 200)
		{
			m_callErrors.push_back(std::string(arc ? "arc" : "side bar") + " overflow: " + name + " (position " + std::to_string(p) + " of " + std::to_string(positions) +
				") has no frame in the movie (RW 0x92F082 stops at the first missing frame)");
		}
	}
	for (int i = 0; i < positions; ++i)
	{
		// the clip of position i: the frame the movie reported for it (lane HUD-4; the arc's are CommandButtons.<i>, the side bar's
		// SideCommandBar.ButtonSet.Button<i>.Button); no frame yet: nothing is called (RW 0x92FF5C), the side bar stops there (RW 0x92F082)
		const std::string &frame = arc ? m_arcFramePath[i] : m_sideFramePath[i];
		Slot &s = slots[i];
		if (frame.empty())
		{
			if (!arc)
			{
				break;
			}
			continue;
		}
		if (!at[i])
		{
			if (s.created)
			{
				const std::string key = levelPrefix() + frame + ".content";
				call(frame, "DeleteContent", {});
				m_images.erase(key + "_Image");
				m_timers.erase(key + "_Timer");
				windows().setAptText("APT:" + key + "_ProductionCount", " ");
				s = Slot();
			}
			continue;
		}
		const ControlBarButton &b = *at[i];
		const std::string key = levelPrefix() + frame + ".content";
		const std::string sig = std::to_string(b.slot) + "|" + std::to_string((int)b.state) + "|" + std::to_string(b.queued) + "|" + b.image + "|" + std::to_string((int)(b.timer * 100.0f));
		if (s.created && s.sig == sig)
		{
			continue;
		}
		if (!s.created)
		{
			if (!createContent(frame))
			{
				continue;
			}
			// the callback belongs to the position (arc / side identity and index) and reads the slot the position shows when it is pressed: the frame is reused when
			// the selection or its range changes (review HUD-1 r1 #3); a position that shows nothing ignores the press
			const int position = i;
			const bool inArc = arc;
			if (m_pressRegistered.insert(key).second)
			{
				registerCommand(key + "_OnPress", [this, position, inArc](const std::string &) {
					const Slot &shown = (inArc ? m_arc : m_side)[position];
					if (m_press && shown.created && shown.slot >= 0)
					{
						m_press(shown.slot, inArc);
					}
				});
			}
			// lane HUD-4 (QA-1 U19): RW 0x9D72DB registers "<content>_OnInitialized" (RW 0x9D6E4D sets + 0x4C) and the content's update (RW 0x9D6ED8) calls
			// into the clip only once it is set: the attached clip's own scripts announce themselves before the engine calls SetState
			if (m_initRegistered.insert(key).second)
			{
				registerCommand(key + "_OnInitialized", [this, position, inArc](const std::string &) {
					Slot &shown = (inArc ? m_arc : m_side)[position];
					if (shown.created)
					{
						shown.initialized = true;
					}
				});
			}
			s.created = true;
		}
		s.slot = b.slot;
		if (!s.initialized)
		{
			continue; // retried at the next sync, after the content's _OnInitialized
		}
		if (!call(frame + ".content", "SetState", { commandStateName(b.state, b.button ? b.button->m_options : 0u) }))
		{
			continue;
		}
		s.sig = sig;
		NativeImage &img = m_images[key + "_Image"];
		img.image = b.image;
		img.grayscale = b.state == ButtonState::Restricted;
		if (b.timer >= 0.0f)
		{
			m_timers[key + "_Timer"] = b.timer;
		}
		else
		{
			m_timers.erase(key + "_Timer");
		}
		windows().setAptText("APT:" + key + "_ProductionCount", b.queued > 0 ? "x" + std::to_string(b.queued) : std::string(" ")); // a space: an empty record would show the label (ResolveAptText)
	}
}

void AptPalantir::syncLocal()
{
	// TARGET FACTS (RotWK game.dat, caveat S-001), the engine's Palantir update:
	// - RW 0x6D5D8E .. 0x6D5DB9: SetPlayerButtonsState(the local player's template Evil (PlayerTemplate +0x1BC, field table RW 0xBF84F8) ? "_ring" : "_evenstar"),
	//   on the first update and whenever the flag changes (cached in bit 2 of +0x7E, "sent" in bit 1). The movie sends PalantirButtons.Buttons.PlayerMagic and
	//   .Objectives to that label: PlayerMagic's frame 0 `_blank` has no ButtonClip, `_evenstar` / `_ring` place the Evenstar (char 217) or the Ring (char 231).
	// - RW 0x6D7967 .. 0x6D7999: SetPlayerPowerCapState is sent only in the Living World branch, guarded by [0xDE4950] != 0 && 0x441E4A() (RW 0x6D796F / 0x6D797D),
	//   with that branch's own flag ([0xDE87AC] +0x2C against the cached byte +0xF4). The skirmish game never takes it, so it is not sent here (the Living World
	//   campaign is not part of this port).
	// - RW 0x6D7A17 .. 0x6D7A7A: SetPlayerFaction(the local player's side name (Player +0x76C), else its template's Side (+0x18) when PlayableSide (+0x151)) when
	//   it differs from the cached name (+0xF8).
	// - RW 0x6D5D8E / 0x6D5DD8 .. 0x6D5DEB: EnablePlayerMagicButton("1" / "0") with the player-magic switch (0x822D6C: the byte of the static at 0xDE8AD8 + 4,
	//   1 from its initialisation at RW 0x822A6D; its setter 0x822D75 is not ported) whenever it differs from the cached bit 3 of +0x7E (0 at construction);
	//   in the update that sends SetPlayerButtonsState the cached value is taken instead, so the call follows one update later. The movie sends
	//   PlayerMagic.ButtonClip to `_up` (the Evenstar 205 / the Ring 219 with their rims) or `_disabled` (the flat 162 / 176).
	bool buttonsSentNow = false;
	if (!m_buttonsStateSent || m_buttonsStateEvil != m_local.evil)
	{
		if (call("", "SetPlayerButtonsState", { m_local.evil ? "_ring" : "_evenstar" }))
		{
			m_buttonsStateSent = true;
			m_buttonsStateEvil = m_local.evil;
			buttonsSentNow = true;
		}
	}
	const bool magicEnabled = true;
	if (buttonsSentNow)
	{
		m_magicHighlightedSent = false; // RW 0x6D5DC4: SetPlayerButtonsState clears the cached enabled / highlighted bits
	}
	if (!buttonsSentNow && m_buttonsStateSent && magicEnabled != m_magicEnabledSent)
	{
		if (call("", "EnablePlayerMagicButton", { magicEnabled ? "1" : "0" }))
		{
			m_magicEnabledSent = magicEnabled;
		}
	}
	syncPlayerStats();
	if (m_local.faction != m_factionSent)
	{
		call("", "SetPlayerFaction", { m_local.faction });
		m_factionSent = m_local.faction;
	}
	// PalantirCommandUI (PalantirCommandUI.h): a context switch to another drawable or CommandSet resets the interface, the next update shows it and its portrait
	if (m_local.context && (!m_commandContext || m_local.contextObject != m_contextObject || m_local.commandSet != m_contextSet))
	{
		hideRank(); // RW 0x92F990: HideRankInterface when shown
		if (m_commandShown)
		{
			call("", "HideCommandInterface", {});
			m_commandShown = false;
		}
		if (!m_portraitShown.empty())
		{
			m_images.erase(portraitKey());
			m_portraitShown.clear();
		}
		m_commandContext = true;
		m_contextObject = m_local.contextObject;
		m_contextSet = m_local.commandSet;
	}
	if (m_commandContext && !m_commandShown)
	{
		m_commandShown = call("", "ShowCommandInterface", {});
	}
	if (m_commandShown && m_local.portrait != m_portraitShown)
	{
		if (m_local.portrait.empty())
		{
			m_images.erase(portraitKey());
		}
		else
		{
			m_images[portraitKey()] = NativeImage{ m_local.portrait, false };
		}
		m_portraitShown = m_local.portrait;
	}
	if (m_commandShown)
	{
		syncRank(); // RW 0x930963: each update while the command interface is up
	}
}

void AptPalantir::rankCall(const std::string &fn, const std::vector<std::string> &args)
{
	call("", fn, args);
	std::string line = fn + "(";
	for (size_t i = 0; i < args.size(); ++i)
	{
		line += (i ? "," : "") + args[i];
	}
	m_rankCalls.push_back(line + ")");
	if (m_rankCalls.size() > 64)
	{
		m_rankCalls.erase(m_rankCalls.begin());
	}
}

void AptPalantir::hideRank()
{
	if (m_rankShown)
	{
		rankCall("HideRankInterface", {});
		m_rankShown = false;
	}
}

// lane HUD-5: RW 0x9305CE's second half (PalantirCommandUI.h): the type changed -> hidden; none -> hidden; else shown once (ShowRankInterface, the bar's cache -1),
// the rank text (type 0, when it changed: APT:HeroRank = APT:RankLabel formatted with the rank, RW 0x92FB90 -> 0x92FACB) or the time text (type 1, when just shown:
// APT:PalantirTimeRemaining, RW 0x930729 .. 0x9307C1), then the bar when its value changed (RW 0x9304DB: ShowRankProgress / SetRankProgressBar(clamp(1 +
// trunc(100 v), 1, 100)) for v >= 0, HideRankProgress for v < 0)
void AptPalantir::syncRank()
{
	if (m_local.rankType != m_rankType)
	{
		hideRank();
		m_rankType = m_local.rankType;
	}
	if (m_rankType == 2)
	{
		hideRank();
		return;
	}
	const bool wasShown = m_rankShown;
	if (!m_rankShown)
	{
		rankCall("ShowRankInterface", {});
		m_rankShown = true;
		m_rankSet = false;
		m_rankProgress = -1.0f;
	}
	if (m_rankType == 0)
	{
		if (!m_rankSet || m_local.rank != m_rankValue)
		{
			char buf[256];
			std::snprintf(buf, sizeof(buf), m_local.rankLabelFormat.c_str(), m_local.rank);
			windows().setAptText("APT:HeroRank", buf);
			m_rankValue = m_local.rank;
			m_rankSet = true;
		}
	}
	else if (m_rankType == 1 && !wasShown)
	{
		windows().setAptText("APT:HeroRank", m_local.timeRemainingText);
	}
	const float v = m_local.rankProgress;
	if (v != m_rankProgress)
	{
		if (v >= 0.0f)
		{
			if (m_rankProgress < 0.0f)
			{
				rankCall("ShowRankProgress", {});
			}
			int iv = 1 - (int)(v * -100.0f);
			iv = iv < 1 ? 1 : (iv > 100 ? 100 : iv);
			rankCall("SetRankProgressBar", { std::to_string(iv) });
		}
		else if (m_rankProgress >= 0.0f)
		{
			rankCall("HideRankProgress", {});
		}
		m_rankProgress = v;
	}
}

void AptPalantir::statsCall(const std::string &fn, const std::vector<std::string> &args)
{
	call("", fn, args);
	std::string line = fn + "(";
	for (size_t i = 0; i < args.size(); ++i)
	{
		line += (i ? "," : "") + args[i];
	}
	m_statsCalls.push_back(line + ")");
	if (m_statsCalls.size() > 64)
	{
		m_statsCalls.erase(m_statsCalls.begin());
	}
}

// lane HUD-5: Palantir::Impl::UpdatePlayerStats (RW 0x6D5C0F; BFME2 0x6D4BDB tier A, decomp Palantir.cpp byte-matched). TARGET FACTS (RotWK game.dat, caveat S-001):
// - the level (Player + 0x1C = the rank level): a change to a higher level than a known one (the cache starts at -1) calls PlayPlayerLevelUpEffect (RW 0x8002BD);
// - APT:PlayerRank (RW 0x800707, the record name RW 0xC4E584) gets "%d" of Player + 0x24 = PlayerScience + 0x1C, the SCIENCE PURCHASE POINTS (the badge on the
//   Palantir's PlayerMagic button: the owner's "1" was lotr.str's default text of APT:PlayerRank, never replaced), on the first update and on every change;
// - SetPlayerMagicProgress (RW 0x800218, the argument "%d") with the progress into the next rank: (int)(skill points - this rank's need) * 100 / (next rank's need
//   - this rank's need), clamped to 1..100, 1 when the two needs are equal; sent when it changes (the cache starts at 1). RW 0x800218's own guard (`cmp 1; jge
//   send; cmp 0x64; jg skip`) passes every clamped value;
// - the highlight: the 20 buttons of the purchase set that could be bought now (ControlBar RW 0x71FA12); a button that became purchasable since the last update
//   lights HighlightPlayerMagicButton("1") (RW 0x80017F); it goes out when the powers screen is open (RW 0x822A35), the player-magic switch is off (RW 0x822D6C)
//   or nothing is purchasable; sent on a change.
void AptPalantir::syncPlayerStats()
{
	if (!m_local.havePlayer)
	{
		return;
	}
	if (m_local.rankLevel != m_lastLevel)
	{
		if (m_lastLevel >= 0 && m_local.rankLevel > m_lastLevel)
		{
			statsCall("PlayPlayerLevelUpEffect", {});
		}
		m_lastLevel = m_local.rankLevel;
	}
	if (!m_rankSent || m_local.purchasePoints != m_lastRank)
	{
		windows().setAptText("APT:PlayerRank", std::to_string(m_local.purchasePoints)); // RW 0x800707: UnicodeString::format(L"%d")
		m_rankSent = true;
		m_lastRank = m_local.purchasePoints;
	}
	int percent = 1;
	const int range = m_local.skillPointsNext - m_local.skillPointsThis;
	if (range != 0) // RW 0x6D5C76 .. 0x6D5CB2 (subss, cvttss2si, imul 100, idiv)
	{
		const float diff = m_local.skillPoints - (float)m_local.skillPointsThis;
		// cvttss2si: out of range (or NaN) is the integer indefinite 0x80000000; imul wraps at 32 bits
		const std::int32_t whole = (diff >= -2147483648.0f && diff < 2147483648.0f) ? (std::int32_t)diff : INT32_MIN;
		percent = (std::int32_t)((std::uint32_t)whole * 100u) / range;
		percent = percent < 1 ? 1 : (percent > 100 ? 100 : percent);
	}
	if (percent != m_lastProgress)
	{
		statsCall("SetPlayerMagicProgress", { std::to_string(percent) });
		m_lastProgress = percent;
	}
	const bool magicEnabled = true; // RW 0x822D6C (see syncLocal)
	bool any = false;
	if (!m_highlighted)
	{
		for (int i = 0; i < 20; ++i)
		{
			if (m_local.purchasable[i] && !m_previousPurchasable[i])
			{
				m_highlighted = true;
				break;
			}
		}
	}
	else
	{
		for (bool b : m_local.purchasable)
		{
			any = any || b;
		}
		if (m_local.storeOpen || !magicEnabled || !any)
		{
			m_highlighted = false;
		}
	}
	std::copy(m_local.purchasable, m_local.purchasable + 20, m_previousPurchasable);
	if (m_magicHighlightedSent != m_highlighted)
	{
		statsCall("HighlightPlayerMagicButton", { m_highlighted ? "1" : "0" });
		m_magicHighlightedSent = m_highlighted;
	}
}

void AptPalantir::sync(const ControlBar &bar)
{
	if (!m_initialized)
	{
		return;
	}
	if (!m_started)
	{
		// Palantir.apt's own functions (read from its scripts): SetPalantirFrameState("_good" / "_evil") shows the radar, the globe and the command arc of the double frame
		// (the faction icon of the resource bar: syncLocal)
		m_started = true;
		call("", "SetPalantirFrameState", { m_evil ? "_evil" : "_good" });
		m_arcShown = true; // the arc starts shown by the frame state: hide it until a selection has commands
		m_sideShown = true;
	}
	syncLocal();
	const std::string texts = std::to_string(bar.money()) + "|" + std::to_string(bar.commandPointsUsed()) + "/" + std::to_string(bar.commandPointsLimit());
	if (texts != m_lastTexts)
	{
		m_lastTexts = texts;
		windows().setAptText("APT:PalantirResources", std::to_string(bar.money()));
		windows().setAptText("APT:PalantirCommandPoints", std::to_string(bar.commandPointsUsed()) + "/" + std::to_string(bar.commandPointsLimit()));
		windows().setAptText("APT:PalantirResourceMultiplier", "x1"); // the territory multiplier is the economy's (stop S-290)
	}
	const bool wantArc = !bar.palantirButtons().empty();
	if (wantArc != m_arcShown)
	{
		m_arcShown = wantArc;
		call("CommandButtons", "gotoAndPlay", { wantArc ? "_show" : "_hide" });
	}
	if (wantArc)
	{
		syncFrames(bar.palantirButtons(), true);
	}
	else
	{
		syncFrames({}, true);
	}
	syncSideBar(bar);
}

std::vector<std::string> AptPalantir::acceptanceStops()
{
	return {
		"[S-292] Apt player behaviour the Palantir needs and the binary was not read for: a movie clip converts to its target path in ToString / string addition (the movies build FSCommand and extern names from "
		"the clip itself; BFME1's value-string function prints \"[MovieClip]\"), and attachMovie runs the new clip's first frame at once (CreateContent sizes the clip right after attaching it; whether "
		"retail flushes the new-instance list, 0x00AE4390, there or its `_width` setter remembers the request is unknown)",
		"[S-293] Palantir protocol: the engine's calls into the movie (SetPalantirFrameState, SetPlayerFaction, CommandButtons.gotoAndPlay _show / _hide, CreateContent(\"CommandButton\", \"content\") and SetState on a button "
		"frame, SideCommandBar.FadeIn / FadeOut) and the keys of the native tables (`<clip path>_Image`, `_Timer`, `_ProductionCount`) are derived from the movies' scripts and the binary's strings; "
		"their timing follows RotWK (HUD-4: a frame only after the movie reported it, RW 0x930088 / 0x92EE8B; the side bar's fades after its movie loaded; SetState only after the "
		"content's _OnInitialized, RW 0x9D6E4D / 0x9D6ED8); the rest of the engine's sequence (the frame controllers RW 0x9D2900 .. 0x9D3550, the side bar update RW 0x92F082's SetButtonState) "
		"was not read, so the order of the calls within a frame may differ",
		"[S-294] Palantir features not driven: the rank interface of CommandUI (the portrait: S-760), hero select, help box (the power points badge, its progress and highlight: lane HUD-5), planning mode, observer buttons, the movie and messenger "
		"buttons, objectives / options buttons (their commands are registered and logged), radar pings, button flash / auto-ability overlays, tooltips and hotkeys of the buttons, the territory resource "
		"multiplier (shown as x1); the movie / faction icon render components are not drawn (the globe: S-762)",
		"[S-763] Apt text layout and the PlayerMagic switch (HUD-3): a single-line edit text is placed as RotWK's display string draw does (RW 0x4A8F95: aligned, centred "
		"vertically, squeezed when wider than its box, whole pixels, unrotated); INFERENCE: the box and the string are both measured in window pixels and the string height is the "
		"Godot font's line height (GDI tmHeight in retail); EnablePlayerMagicButton follows the player-magic switch at RW 0xDE8ADC, always on here (its setter RW 0x822D75 "
		"is not ported)",
		"[S-764] Apt clips and masks (HUD-3 round 2): an engine call into a movie no longer flushes the new clips it created (BFME2's call entry 0x00ACCB80 reaches neither the "
		"flush 0x00AE4390 nor the pool 0x00AE6540): they first advance in the next step; the device's mask groups clear a 5-pixel margin (Godot's default) so their edges "
		"do not sample stale pixels. INFERENCE: RotWK's own call path (RW 0x0062279C -> 0x00AE0CF0) was checked only for direct calls; a bitmap fill's texture coordinates "
		"are the fill matrix over the texture size with no half-texel offset (retail's D3D8 vertex offset and sampler state were not read)",
		"[S-762] Palantir globe (HUD-2): AptPalantir::RenderGlobe (RW 0x6D42EE -> RW 0x504019) draws the W3D model \"palantir\" (BFME2 art\\w3d\\pa\\palantir.w3d) through the "
		"device's W3D renderer, each dome alone in a viewport of its own (SPHERE01 over black, SPHERE02 over white; the model at the identity; the camera, set every draw, at "
		"(0, 160, 0) turned -pi/2 about X, 50 degrees, aspect 1), composited in gamma space as retail's frame buffer blends them (SPHERE01 adds, then SPHERE02 multiplies: "
		"clamp(D + A) * M, RW 0x518000 / 0x576240); INFERENCE: the UV scroll clock is the renderer's, not WW3D's sync time; the pictures are 256 x 256 stretched to the clip",
		AptPlayerTribute::stopLine(), // lane PLAY-1: the flag's screen
	};
}

// ---- lane UI-4: the hero bar ----
void AptPalantir::setHeroSelectHandler(std::function<void(const std::string &, bool)> handler)
{
	m_heroSelect = std::move(handler);
	if (m_heroSelect && !m_heroSelectPath.empty())
	{
		m_heroSelect(m_heroSelectPath, true);
	}
}

void AptPalantir::setNativeImage(const std::string &key, const std::string &image)
{
	if (image.empty())
	{
		m_images.erase(key);
	}
	else
	{
		m_images[key] = NativeImage{ image, false };
	}
}

// ---- lane SPELL-2: the spell book ----------------------------------------------------------------------------------------------------------
const char *AptPalantir::spellStateName(int state)
{
	static const char *const kNames[] = { "_unused", "_disabled", "_cantAfford", "_static", "_notReady", "_up", "_visuallyEnabled" }; // RW 0xC7F4DC
	return state >= 0 && state <= SPELL_VISUALLY_ENABLED ? kNames[state] : "_unused";
}

void AptPalantir::syncSpellBook(const std::vector<SpellSlot> &slots)
{
	if (m_spellPath.empty())
	{
		return;
	}
	if (!m_spellShown)
	{
		call(m_spellPath, "SetState", { "_show" }); // RW 0x9312DE
		return;
	}
	for (int i = 0; i < kSpellSlots; ++i)
	{
		const SpellSlot empty;
		const SpellSlot &sl = i < (int)slots.size() ? slots[(size_t)i] : empty;
		const std::string imageKey = "InGameSpellBookSpell" + std::to_string(i + 1) + "Image"; // RW 0x930E6E
		const std::string timerKey = "InGameSpellBookSpell" + std::to_string(i + 1) + "Timer"; // RW 0x93191F
		if (sl.image != m_spellImage[i])
		{
			if (sl.image.empty())
			{
				m_images.erase(imageKey); // RW 0x623790
			}
			else
			{
				m_images[imageKey] = NativeImage{ sl.image, false }; // RW 0x6236DE
			}
			m_spellImage[i] = sl.image;
		}
		if (sl.timer >= 0.0f && sl.timer < 1.0f)
		{
			m_timers[timerKey] = sl.timer;
		}
		else
		{
			m_timers.erase(timerKey);
		}
		m_spellButton[i] = sl.buttonIndex;
		if (sl.state != m_spellState[i])
		{
			const bool wasUsable = m_spellState[i] == SPELL_UP || m_spellState[i] == SPELL_STATIC;
			call(m_spellPath, "SetButtonState", { std::to_string(i + 1), spellStateName(sl.state) }); // RW 0x93125F
			const bool usable = sl.state == SPELL_UP || sl.state == SPELL_STATIC;
			if (usable && !wasUsable && m_spellState[i] >= 0)
			{
				call(m_spellPath, "FlashButton", { std::to_string(i + 1) }); // RW 0x931467 .. 0x9314C3
			}
			m_spellState[i] = sl.state;
		}
	}
}

bool AptPalantir::pressSpellSlot(int slot)
{
	if (slot < 0 || slot >= kSpellSlots || m_spellButton[slot] < 0 || !m_spellPress)
	{
		return false;
	}
	m_spellPress(m_spellButton[slot]);
	return true;
}

// lane RADAR-1 (see AptPalantir.h)
bool AptPalantir::createRadarPing(int id, const std::string &name)
{
	++m_radarPingCalls;
	return call("", "CreateRadarPing", { std::to_string(id), name });
}

bool AptPalantir::moveRadarPing(int id, float stageX, float stageY)
{
	++m_radarPingCalls;
	char x[64], y[64];
	std::snprintf(x, sizeof(x), "%f", (double)stageX);
	std::snprintf(y, sizeof(y), "%f", (double)stageY);
	return call("", "MoveRadarPing", { std::to_string(id), x, y });
}

bool AptPalantir::fadeOutRadarPing(int id)
{
	++m_radarPingCalls;
	return call("", "FadeOutRadarPing", { std::to_string(id) });
}

bool AptPalantir::requestObjectives()
{
	if (!m_services)
	{
		windows().note("command-unwired", "DIPLOMACY: no shell services [S-1922]");
		return false;
	}
	m_services->request(ShellRequest{ ShellAction::PalantirObjectives, std::string() });
	return true;
}
