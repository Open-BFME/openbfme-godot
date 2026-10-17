// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ControlBar (ZH Include/GameClient/ControlBar.h, Source/GameClient/GUI/ControlBar/ControlBar.cpp, ControlBarCommand.cpp getCommandAvailability / populateCommand,
// ControlBarCommandProcessing.cpp processCommandUI; B1 the same files), lane HUD-1: the UI model of the selection's commands. It reads the local player's selection, finds the
// object's CommandSet, evaluates each command button (hidden / enabled / restricted / can't afford) and the production queue, and turns a pressed button into the messages
// of the lockstep command stream or into a GUI command mode waiting for a target (InGameUI::setGUICommand). It draws nothing: AptPalantir shows it, tests read it.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; spec production.md): a CommandSet is the object's override name, else its template's CommandSet field (RW 0x69156B, Object::
// getCommandSetName); the set holds 33 slots and InitialVisible (RW 0x7205B9, ControlBarCommands.h); PUSH_VISIBLE_COMMAND_RANGE / POP_VISIBLE_COMMAND_RANGE buttons carry
// CommandRangeStart / CommandRangeCount (RW +0x22C / +0x230); a UNIT_BUILD press queues through MSG_QUEUE_UNIT_CREATE with the RotWK argument layout (GameLogicDispatch.h);
// BuildAssistant::canMakeUnit (RW 0x793ECB) answers the build conditions.
// TARGET FACTS (lane HUD-4, the placement; caveat S-001): the control bar has 33 command windows (ControlBar + 0xDC, made by RW 0x71E1E7); populate (RW 0x943D6F)
// gives window i the button of slot rangeStart + i for i < rangeCount (the range stack at ControlBar + 0x2B0; empty: (0, InitialVisible) is pushed, RW 0x97D718).
// The Palantir arc (PalantirCommandUI update RW 0x930035 / 0x92FF5C) shows windows 0 .. 5, each only when its button is InPalantir (CommandButton + 0x102); the
// side command bar (RW 0x92F082) walks all 33 windows in order and packs the buttons whose Radial (+ 0x101) is set into its frames (at most 15; the movie has 12).
// A button can show in both (the Forge Works' build buttons) or in neither (a horde's Attack-Move / Stop at slots 13 / 14: retail reaches them by hotkey).
// INFERENCE (stop S-289): the windows a hidden or unavailable button leaves are not re-packed in the arc (the window stays empty).
// NOT PORTED (stop S-290): the availability rules that need ports of other lanes (special power ready / cooldown, science purchase, castle and gate commands, weapon toggles,
// stances, horde formations beyond the toggle message, hero revive, spell book), the single use and script status bits, disabled objects; those buttons show as restricted and
// their presses are counted by unportedPresses(), never dropped.

#pragma once

#include "GameClient/ControlBarCommands.h"
#include "GameClient/HudContext.h"
#include "GameLogic/ObjectTypes.h"

#include <map>

class Player;
class SpecialPowerTemplate;
struct HeroRecord;
#include <string>
#include <vector>

enum class ButtonState
{
	Hidden,     ///< no button in the slot, or not offered (Buildable = No)
	Enabled,
	Restricted, ///< shown, cannot be used now (prerequisites, queue full, maxed out, factory disabled, not ported)
	CantAfford,
	NotReady,   ///< cooling down (not ported: special powers)
	Active,     ///< a toggle that is on
	// lane HUD-7: RotWK's availability 8 (RW 0x9431A6): an upgrade the player or object has or is researching (PLAYER_UPGRADE, OBJECT_UPGRADE, CASTLE_UPGRADE).
	// updateContextCommand (RW 0x943C1F .. 0x943C46) disables the window and sets the status 0x80000000 and 0x01000000, which the command button clip shows as
	// "_cantAfford" (RW 0x9D2C31: a disabled window with 0x01000000 -> state 3 of the table RW 0xC8CAF8). Unlike availability 0 it is never hidden by
	// HIDE_WHILE_DISABLED (RW 0x943B39 tests 0 only), and a press reaches processCommandUI as for a _cantAfford clip
	Purchased
};

struct ControlBarButton
{
	int slot = -1;                          ///< 0-based CommandSet slot
	const CommandButton *button = nullptr;
	ButtonState state = ButtonState::Hidden;
	bool inPalantir = false;
	std::string image;                      ///< the first ButtonImage name
	std::string textLabel, descriptLabel;   ///< game text labels (tooltip)
	int cost = -1;                          ///< the build cost for the local player, -1 when the button has none
	int queued = 0;                         ///< units of the button's template in the producer's queue
	float timer = -1.0f;                    ///< 0..1 progress of the first queue entry of the template (-1: none)
	int hotkey = 0;                         ///< the KEY_ code of the `&` accelerator of the label, 0 none
	int reviveIndex = -1;                   ///< lane HERO-1: a REVIVE button's hero record index
	ObjectID rider = INVALID_ID;            ///< lane UI-1: an EXIT_CONTAINER button's rider (MSG_EXIT's argument 0); a rider's extra slots carry it too, disabled
	int position = -1;                      ///< lane HUD-4: the arc's window (slot - range start, 0 .. 5) or the side bar's packed position (0 ..)
	bool autoAbility = false;               ///< lane AUTOCAST-1: the glow (RW 0x9D2D0E -> SetAutoAbilityOverlayState): the source object's AutoAbilityBehavior auto casts this button
};

struct ControlBarQueueEntry
{
	std::string templateName;
	std::string image;       ///< the button image of the first button that builds it
	float percent = 0.0f;    ///< 0..100
	int quantityTotal = 0, quantityProduced = 0;
	int cost = 0;
};

class ControlBar
{
public:
	explicit ControlBar(HudContext &ctx) : m_ctx(ctx) {}

	// Re-evaluates everything from the selection and the logic (call once per client frame).
	void update();

	// ---- the evaluated state ----
	ObjectID sourceObject() const { return m_source; }
	const std::string &commandSetName() const { return m_setName; }
	const std::vector<ControlBarButton> &palantirButtons() const { return m_palantir; }
	const std::vector<ControlBarButton> &sideButtons() const { return m_fortressSide.empty() ? m_side : m_fortressSide; }
	const std::vector<ControlBarButton> &radialButtons() const { return m_side; }
	// lane HUD-4: the visible buttons that neither the arc nor the side bar shows (retail too: InPalantir beyond window 5, or neither InPalantir nor Radial);
	// the hotkeys (STOP, TOGGLE_ATTACKMOVE, STANCE_*) and the context commands reach their commands
	const std::vector<ControlBarButton> &offBarButtons() const { return m_offBar; }
	enum
	{
		kPalantirWindows = 6 ///< RW 0x930035: PalantirCommandUI reads the control bar's command windows 0 .. 5 (ControlBar + 0xDC)
	};
	// the bars that show the button of `slot` with the visible range starting at `rangeStart` (RW 0x92FF5C: InPalantir in windows 0 .. 5; RW 0x92F082: Radial)
	struct Placement
	{
		bool arc = false, side = false;
		int window = -1;
	};
	static Placement placementOf(const CommandButton &b, int slot, int rangeStart);
	const std::vector<ControlBarQueueEntry> &queue() const { return m_queue; }
	std::uint32_t money() const { return m_money; }
	int commandPointsUsed() const { return m_cpUsed; }
	int commandPointsLimit() const { return m_cpLimit; }
	int visibleRangeStart() const { return m_rangeStack.back().first; }
	int visibleRangeCount() const { return m_rangeStack.back().second; }
	// the portrait image name of the source object ("" none), its display name label
	const std::string &portrait() const { return m_portrait; }
	const std::string &displayNameLabel() const { return m_displayName; }
	// a counter bumped whenever the visible buttons or their states change (the Palantir redraws on a change)
	unsigned version() const { return m_version; }
	// lane PLAY-4: the selection is in RotWK's under-construction context (evaluateContextUI RW 0x71EBDA -> switchToContext(6) -> populateUnderConstruction
	// RW 0x944EFB): the command set is not shown, only the cancel construction button
	bool underConstruction() const { return m_underConstruction; }
	// RW 0x71EE67 .. 0x71EF1E: the object's GettingBuiltBehavior (behavior slot 0x80, RW 0x68C3E6) rebuilds it (+ 0x35, slot 14 RW 0x71C2EA) or has + 0x36 set
	// (slot 11 RW 0x8227A4) while it is UNDER_CONSTRUCTION, or has + 0x36 set when it is not; or the object has the model condition BASE_BUILD (RW 0x46E918(0xDB))
	static bool isUnderConstructionContext(const Object &obj);
	// lane PLAY-4: StanceToButtonSlot (RW InGameToggleStanceCommandButton, BFME2 0x567807): the index of the object's StancesBehavior stance in the button's
	// Stances list, -1 when it is not there (or the object has no stances)
	static int stanceSlotOf(const CommandButton &b, Object &obj);

	// ---- input ----
	// ZH ControlBar::processCommandUI: the press of the button in `slot`. Returns false when the slot has no usable button (counted, reported through ui messages).
	bool pressButton(int slot, bool inPalantir);
	// right press on a build button: cancels one queued unit of that template (MSG_CANCEL_UNIT_CREATE by type); lane HUD-7: on a REVIVE button the hero record by
	// its index (RW 0x940C57)
	bool cancelQueued(int slot, bool inPalantir);
	// lane AUTOCAST-1: the right press of a command button: the clip's gate (RW 0x9D28D4: a _disabled / _cantAfford button only when CANCELABLE) and
	// processCommandUI's right press (RW 0x940435 with its second argument 0): an AutoAbility (+ 0x10C) or CANCELABLE button of the source object sends
	// MSG_DO_AUTO_ABILITY { power id, object } (SPECIAL_POWER / SPECIAL_POWER_TOGGLE, RW 0x941511 / 0x9418F7) or MSG_DO_AUTO_ABILITY_WEAPON { weapon slot,
	// object } (FIRE_WEAPON, RW 0x941391); a CANCELABLE build button cancels (cancelQueued); a target-needing button without either cancels the GUI command mode
	// (RW 0x94089C with null). true when a message went out
	bool rightPressButton(int slot, bool inPalantir);
	unsigned autoAbilityToggles() const { return m_autoToggles; }
	const std::map<std::string, unsigned> &unportedPresses() const { return m_unported; }

	static std::vector<std::string> acceptanceStops();

private:
	ButtonState evaluate(const CommandButton &b, Object &obj);
	static const SpecialPowerTemplate *specialPowerOf(const CommandButton &b);
	// lane FACT-C: the castle object of a castle member (its CastleMemberBehavior's castle id, module + 0x18; RW 0x943140 .. 0x94318E), or null
	static Object *castleOfMember(Object &obj);
	std::string heroRecordImage(const HeroRecord &r, const Player &player);
	bool reviveButton(ControlBarButton &cb, int n, Player &player, Object &producer);
	// lane UI-1: the riders on the EXIT_CONTAINER buttons of the object's command set (RW 0x94251F), and the inventory of a garrison without a command set
	// (evaluateContextUI RW 0x71EBDA -> context 2 -> RW 0x94518D)
	void doTransportInventoryUI(Object &obj);
	bool structureInventory(Object &obj, Player *local);
	static std::string templateButtonImage(const ThingTemplate *tt);
	void resetRanges() { m_rangeStack.assign(1, { 0, CommandSet::MAX_BUTTONS }); }
	// lane HUD-4: the window of a button (RW 0x943D6F: window i shows slot rangeStart + i) decides the arc, its Radial flag the side bar (RW 0x92F082)
	void place(ControlBarButton cb, int rangeStart);
	const ControlBarButton *find(int slot, bool inPalantir) const;
	void emitStateKey();
	// RW 0x944EFB: window 0 shows Command_CancelConstruction (RW 0xC80400) unless the template is DONT_USE_CANCEL_BUILD_BUTTON (+ 0x122 & 4); enabled for the
	// object's controller
	void populateUnderConstruction(Object &obj, bool mine);

	HudContext &m_ctx;
	ObjectID m_source = INVALID_ID;
	std::string m_setName;
	std::vector<ControlBarButton> m_palantir, m_side, m_offBar, m_fortressSide;
	std::vector<ControlBarQueueEntry> m_queue;
	std::uint32_t m_money = 0;
	int m_cpUsed = 0, m_cpLimit = 0;
	std::string m_portrait, m_displayName;
	std::vector<std::pair<int, int>> m_rangeStack{ { 0, CommandSet::MAX_BUTTONS } };
	std::string m_lastKey;
	unsigned m_version = 0;
	std::map<std::string, unsigned> m_unported;
	unsigned m_autoToggles = 0;
	bool m_underConstruction = false;
};
