// OpenBFME. GPL-3.0.
//
// CommandButtonHelp (lane HUD-6): RotWK's in-game help box for a command button, the carved box that shows the button's name, "Shortcut: L", the cost and
// the description while the pointer is on any command button (the Palantir arc, the side bar, the radial bubbles, the spell book, the hero bar).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp is the donor, tier A (identical code) at every address marked so):
//   * the text (the help provider's vslot 3, RW 0x807A81, tier B edited from BFME2 0x805E3D, read in the binary): the name is the button's TextLabel
//     (RW 0x75CE47) fetched; the description its DescriptLabel (RW 0x75CECD) fetched; for a button with an object template (RW 0x75D1DC) that is neither an
//     upgrade button (PLAYER_ / OBJECT_ / CASTLE_UPGRADE with an upgrade) nor PURCHASE_SCIENCE / HORDE_SET_FORMATION: "TOOLTIP:Cost" with
//     ThingTemplate::calcCostToBuild(player, selection, -1) when it is above 0 and the template's + 0x11B bit 0x20 is clear, else "TOOLTIP:CostFree";
//     "TOOLTIP:CommandPoints" with the template's CommandPoints (+ 0x628) when above 0; the template's prerequisites formatted into
//     "CONTROLBAR:Requirements" and appended to the description after a newline (RW 0x8F9BBF per prerequisite, ", " between them); "TOOLTIP:BuildDisabled"
//     appended when the LOCAL player may not build it (RW 0x8081F7 -> 0x6AC856); an upgrade button (an upgrade, PLAYER_ / OBJECT_ / CASTLE_UPGRADE): "TOOLTIP:Cost" with
//     UpgradeTemplate::calcCostToBuild (RW 0x66F2C8) when above 0, a description led by "TOOLTIP:AlreadyUpgradedDefault" (or the button's
//     + 0x70 label) when the selection has it, and, only right after a DescriptLabel's text, an upgrade the selection's player lacks gets "\n\n" +
//     "TOOLTIP:TooltipCannotPurchaseBecauseQueueFull" when the selection's queue holds 0x14 entries, else "TOOLTIP:TooltipNotEnoughMoneyToBuild" when the
//     local player cannot afford it (RW 0x807E15 .. 0x807EB0, RW 0x66F492);
//     a single-science PURCHASE_SCIENCE button "TOOLTIP:ScienceCost" with the science's cost (RW 0x5FEC64); the "resource" image suffix is the Palantir's
//     faction (RW 0x6D4750: Palantir + 0xF8, SetPlayerFaction);
//   * InGameCommandButtonHelp::Impl (the decomp's InGameCommandButtonHelpDisplayStrings.cpp / ...SetWidth.cpp / ...Render.cpp, tier A for RW 0x974EEB /
//     0x9744E4 / 0x974795): built from (name, cost, command points, description, resource suffix) - the decomp's (name, title, description, cost); the
//     name loses its '&' markers and the first alphanumeric after one is the shortcut key (towupper); the title icon is "Resource_Icon", the second
//     "ResourceBar_<suffix>" (a mapped image with a zero size counts as none); the fonts: name = HelpBoxNameFont, title and the second line =
//     HelpBoxCostFont, shortcut = HelpBoxShortcutFont, the long text = HelpBoxDescriptionFont, each at its point size times min(scale x, y) of the Apt
//     player (InGameUI + 0x7AC / 0x7BC / 0x7CC / 0x7DC with the GlobalLanguage overrides + 0x104 / 0x110 / 0x11C / 0x128 when they name a font, RW 0x6A030D
//     / 0x69E29E / 0x69E320 / 0x69E3A2), word wrap centred, the INI colour;
//   * SetWidthAndComputeHeight(width) (RW 0x9744E4): the name and the long text wrap at the width; with no title but a second line the two swap; the height
//     is name + (title or shortcut line: the larger of title (at least the icon height when it has text) and shortcut) + the second line (at least the icon
//     height) + the long text (" " when empty); the shortcut line is "TOOLTIP:Shortcut" formatted with the key;
//   * Render(position, size) (RW 0x974795): the name centred at the top; with a shortcut: the title with its icon left and the shortcut right-aligned on one
//     line (or the shortcut alone centred); without: the title with its icon centred; then the second line with its icon (centred when there is no
//     shortcut); then the long text centred; positions rounded as floor(v + 0.5).
// INFERENCE (stop S-2702): the word wrap breaks at spaces and at '\n' and centres each line (W3D Render2DSentenceClass not read); the prerequisite text
// (RW 0x8F9BBF) is the prerequisite object's DisplayName list joined by " or " (ZH ProductionPrerequisite::getRequiresList); not ported (a description
// noted in the help's `unported`): the special power cost branches (START_SELF_REPAIR / START_NEIGHBORHOOD_REPAIR, SPECIAL_POWER's "TOOLTIP:UnitCost"),
// REVIVE / CASTLE_UNPACK costs, SPELL_BOOK's "TOOLTIP:ScienceDisabled", the multi-science PURCHASE_SCIENCE pick, the upgrade's conflict / prerequisite labels.

#pragma once

#include "GameClient/ControlBar.h"
#include "GameClient/GUI/GameWindow.h"

#include <cstdint>
#include <string>
#include <vector>

class ArchiveFileSystem;
class FontMetricsSource;
class GameLogic;
class GameTextSource;
class Object;
class Player;

struct HelpBoxFont
{
	std::string name;
	int pointSize = 0;
	bool bold = false;
	std::uint32_t color = 0xFFFFFFFFu; ///< ARGB (parseColorInt RW 0x42F13E)
};

// InGameUI's HelpBox* fields (data\ini\ingameui.ini, rows RW 0xC126C0 .. 0xC127B0) with Language.ini's HelpBox*Font (rows RW 0xBF4DD0 .. 0xBF4E00)
struct HelpBoxSettings
{
	bool loaded = false;
	HelpBoxFont name, cost, shortcut, description;
	// a missing file, block or field is an error (no default)
	static bool load(ArchiveFileSystem &fs, HelpBoxSettings &out, std::string *error);
	static bool scan(const std::string &inGameUi, const std::string &language, HelpBoxSettings &out, std::string *error);
};

// What the help box draws (window pixels), in order.
struct HelpDrawOp
{
	enum Kind
	{
		TEXT, ///< one line of `text` with `font` (its point size already scaled), its top-left at (x, y)
		IMAGE ///< a mapped image over (x, y, w, h)
	};
	Kind kind = TEXT;
	std::string text; ///< UTF-8
	HelpBoxFont font;
	std::string image;
	float x = 0, y = 0, w = 0, h = 0;
};

class CommandButtonHelp
{
public:
	// the size of a mapped image (its texture rectangle); false when there is none
	typedef bool (*ImageSizeFn)(void *ctx, const std::string &name, int &w, int &h);

	struct Text
	{
		UnicodeString name, cost, commandPoints, description;
		std::string resourceSuffix;
		std::vector<std::string> unported; ///< the branches of RW 0x807A81 this button needed that are not ported
	};
	// RW 0x807A81: the texts of `button` for the local player and the selection's object (null without one)
	// `player` is the selection's controlling player (the local player without a selection), `localPlayer` the local one (RW 0x6A8839 / 0xDE4928 + 0x10)
	static Text compose(const ControlBarButton &button, const GameTextSource *text, GameLogic &logic, Player *player, Player *localPlayer, Object *selection,
	                    const std::string &faction);

	CommandButtonHelp() = default;
	// RW 0x974EEB (Impl's constructor): the name's '&' markers go, the shortcut key is kept; the icons are looked up
	CommandButtonHelp(const Text &t, const GameTextSource *text, ImageSizeFn imageSize, void *imageCtx);

	// RW 0x9744E4 with the fonts scaled by `fontScale` (min of the Apt player's x / y scale); returns the height in window pixels
	int setWidthAndComputeHeight(int width, const HelpBoxSettings &settings, float fontScale, float iconScaleX, float iconScaleY, FontMetricsSource &metrics);
	// RW 0x974795: the ops for the content rectangle (window pixels); needs setWidthAndComputeHeight first
	void render(float x, float y, float w, float h, std::vector<HelpDrawOp> &out) const;

	const UnicodeString &name() const { return m_name; }
	const UnicodeString &title() const { return m_title; }
	const UnicodeString &description() const { return m_description; }
	const UnicodeString &longText() const { return m_cost; }
	char16_t shortcutKey() const { return m_key; }
	const std::vector<std::string> &unported() const { return m_unported; }
	static std::vector<std::string> acceptanceStops();

	// the word wrap (INFERENCE, S-2702): the lines of `text` within `width` pixels of `font` (0 = no wrap); the widest line's width
	static std::vector<UnicodeString> wrap(const UnicodeString &text, const GameFont &font, int width, FontMetricsSource &metrics, int *widest);

private:
	struct Measured
	{
		std::vector<UnicodeString> lines;
		HelpBoxFont font;
		int w = 0, h = 0;
	};
	Measured measure(const UnicodeString &text, const HelpBoxFont &font, int wrapWidth, FontMetricsSource &metrics) const;
	void emit(const Measured &m, float x, float y, std::vector<HelpDrawOp> &out) const;

	UnicodeString m_name, m_title, m_description, m_cost, m_shortcut;
	char16_t m_key = 0;
	std::string m_icon30, m_icon34; ///< "Resource_Icon", "ResourceBar_<suffix>" ("" none)
	int m_icon30W = 0, m_icon30H = 0, m_icon34W = 0, m_icon34H = 0;
	float m_scaleX = 1.0f, m_scaleY = 1.0f;
	int m_width = 0;
	Measured m_nameS, m_titleS, m_descriptionS, m_shortcutS, m_costS;
	std::vector<std::string> m_unported;
};
