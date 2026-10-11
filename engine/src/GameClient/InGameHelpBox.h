// OpenBFME. GPL-3.0.
//
// InGameHelpBox (lane HUD-6): the Palantir's help box movie (InGameHelpBox.apt, loaded by Palantir.apt into its clip `helpBox`) and the control bar's help
// provider that decides when it shows what. See CommandButtonHelp.h for the content.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp is the donor, tier A at every address):
//   * OnHelpBoxLoaded (RW 0x6D4F28): the argument is the clip ("_level0.helpBox"); the help box object keeps the level and the name without the level and
//     registers the custom render "_level%d.<name>_Content" (RW 0x92E689, strings RW 0xC7C4A4 / 0xC7EFF0); the movie's content clip carries
//     `_type = String(_parent) + "_Content"` (InGameHelpBox.apt), so the engine draws the help into that clip;
//   * the movie clip's update (InGameHelpBoxMovieClip::Update RW 0x92E462, once per Palantir update): state 0 calls SampleContentWidth and waits (1); the render
//     callback in state 1 takes the content clip's width, max((int)(width + 0.5f), 1) (RW 0x92E2AC); state 1 with a width calls Hide (2); state 2 with a help
//     calls Show(help->SetWidthAndComputeHeight(width) * the Apt player's y scale, "%f") (3, shown); state 4 counts down and then hides (RW 0x92E362: Hide,
//     state 2, the help released); in states 3 / 4 the render callback draws the help into the content clip (vslot 2 of the help, RW 0x7FF5E0 -> Render);
//   * its vtable (RW 0xC7EFFC): setHelp (RW 0x92E570: hide when shown (3 / 4: Hide, state 2), then keep the new help), hideSoon (RW 0x92E229: shown -> state 4
//     with a countdown of 5 updates), isShown (RW 0x92E23E: state 3);
//   * the control bar's help provider (ControlBar + 0x2AC; showHelp RW 0x807848, called for the command button under the pointer: the Palantir's buttons through
//     their content updaters, RW 0x9D2D45 -> RW 0x807A00 (window, button)): a provider equal to the kept one (RW 0x8075A3: the same kind and window and
//     button) marks the frame (+ 0x21C) and, once now > the hover start + the window's tooltip delay (+ 0x1C8, RW 0x8075E4) and not yet shown, shows: the help
//     is composed (vslot 3, RW 0x807A81) and handed to the Palantir's help box (RW 0x807676 -> RW 0x6D4728); another provider while the help box shows only
//     asks it to hide soon (RW 0x6D473D); with nothing kept or the box not shown the new provider is kept, the frame marked, the hover start reset (RW 0x807701 /
//     0x80795B);
//   * ControlBar::update (RW 0x71FC08, byte-matched as the decomp's ControlBarUpdateVirtual.cpp): a frame without a mark hides the box soon (when it shows)
//     and drops the provider; the mark is cleared.
// INFERENCE (stop S-2703): the hovered command button of the Palantir's movie is the Apt player's current button (the button under the pointer) inside a
// content clip the control bar filled (the content updaters' rollover route, vslot 2 of RW 0xC8C3D4, was not traced to its caller); the Apt player's y scale
// is the stage height over the window height; the guards of showHelp (InGameUI vslot 0x1BC / 0x17C, RW 0xDE3BAC + 0x1A204, RW 0xDEA328) and RW 0xA0CB16
// are not ported; the hero bar's and the spell book's buttons are not providers yet.

#pragma once

#include "GameClient/CommandButtonHelp.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class InGameHelpBox
{
public:
	typedef std::function<bool(const std::string &path, const std::string &fn, const std::vector<std::string> &args)> CallFn;

	// RW 0x6D4F28 / its unload twin: `clip` is the movie's argument ("_level0.helpBox")
	void loaded(const std::string &clip, CallFn call);
	void unloaded();
	bool isLoaded() const { return m_loaded; }
	// "_level%d.<name>_Content": the custom render the content clip's `_type` names
	const std::string &renderName() const { return m_renderName; }

	// ---- InGameHelpBoxMovieClip ----
	void setHelp(std::shared_ptr<CommandButtonHelp> help); ///< RW 0x92E570
	void hideSoon();                                      ///< RW 0x92E229
	bool isShown() const { return m_state == 3; }         ///< RW 0x92E23E
	// RW 0x92E462; `measure` computes the help's height for the sampled width (SetWidthAndComputeHeight with the fonts and scale), `stageScaleY` turns
	// window pixels into stage units
	void update(const std::function<int(CommandButtonHelp &, int width)> &measure, float stageScaleY);
	// RW 0x92E2AC: the content clip drawn at (x, y, w, h) window pixels; the help's ops in states 3 / 4
	void render(float x, float y, float w, float h, std::vector<HelpDrawOp> &out);
	int state() const { return m_state; }
	int sampledWidth() const { return m_width; }
	const CommandButtonHelp *help() const { return m_help.get(); }

	// ---- the control bar's provider ----
	struct Provider
	{
		int bar = -1;  ///< 0 the Palantir arc, 1 the side bar, 2 the radial ring (the "kind" of RW 0x8075A3)
		int slot = -1; ///< the command set slot (the window)
		const CommandButton *button = nullptr;
		bool operator==(const Provider &o) const { return bar == o.bar && slot == o.slot && button == o.button; }
	};
	// RW 0x71FC08's test, once per HUD update before the hover: a frame nobody marked hides the box soon and drops the provider
	void beginFrame();
	// RW 0x807848 for the provider under the pointer; `compose` makes the help when it is time to show it
	void showHelp(const Provider &p, std::uint32_t nowMs, int tooltipDelayMs, const std::function<std::shared_ptr<CommandButtonHelp>()> &compose);
	const Provider &provider() const { return m_provider; }
	unsigned helpsShown() const { return m_helpsShown; }
	const std::vector<std::string> &calls() const { return m_calls; }
	static std::vector<std::string> acceptanceStops();

private:
	bool call(const std::string &fn, const std::vector<std::string> &args);
	CallFn m_call;
	bool m_loaded = false;
	std::string m_name, m_renderName;
	int m_state = 0;      ///< + 0xC
	int m_countdown = 0;  ///< + 0x14
	int m_width = -1;     ///< + 0x18
	std::shared_ptr<CommandButtonHelp> m_help; ///< + 0x1C
	// the provider
	Provider m_provider;
	bool m_haveProvider = false, m_marked = false, m_shownForProvider = false;
	std::uint32_t m_hoverStart = 0;
	bool m_hoverStartSet = false;
	unsigned m_helpsShown = 0;
	std::vector<std::string> m_calls;
};
