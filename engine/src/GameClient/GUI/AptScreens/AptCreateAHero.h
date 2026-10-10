// OpenBFME. GPL-3.0.
//
// AptCreateAHero (lane CAH-1): the Create-a-Hero builder screen, CreateAHero.apt (CodePrefix AptCreateAHero) and the five page movies it loads into its
// `screen` clip (ShowScreen(name): loadMovie("Cah" + name + ".swf")): CahManager (the saved heroes), CahClass (class and type), CahAppearance (the name,
// the appearance and the attributes), CahPowers (the powers per rank) and CahNewFeatures (the promo page, "B"). The hero being built is an AptMyHero
// (GameClient/GUI/CreateAHero/AptMyHero.h); the saved and system heroes are a CreateAHeroHeroList (GameClient/CreateAHeroHeroList.h).
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the main menu's CreateAHero (RW 0x91A018): CreateAHero.apt is pushed, GlobalData's map name (+0xAC0) becomes Maps\CreateAHero\CreateAHero.map
//     (RW 0xC7CBF0) and that map is started as a game of mode 7 (RW 0x6D3261, RW 0x7111E5(7)): the 3D view behind the screen ("map mode");
//   * the constructor (RW 0x91A6BF, vtable RW 0xC7CBB8): TheCreateAHeroSystem + 0x18C = 1 (in the builder), the five pages (+0x420 Manager RW 0x9C5EA4,
//     +0x424 Class RW 0x9C4AE0, +0x428 Appearance RW 0x9C4255, +0x42C Powers RW 0x9C338B, +0x430 Bonus RW 0x9C16C4), and the registrations
//     AptCreateAHero::OnShowScreen / PrepareToTakePicture / OnTakePicture / RotateLeft / RotateRight / ZoomIn / ZoomOut (commands), the render components
//     CreateAHero::RenderPictureGuard and CreateAHero::DrawMapComponent (RW 0x91A3A9: the tactical view drawn into the clip's rectangle), the providers
//     CreateAHeroDemo (RW 0x91A5E9 arg 0: "0" when GlobalData + 0x9D1, else "1") and AptCreateAHero::SuppressCAHPromo (arg 1: the option preference
//     "SuppressCahPromo" RW 0xC1B320, read "1" / "0", written by a set) and "CreateAHero" (RW 0x92B12F, the current page's vslot 7);
//   * OnShowScreen (BFME2 0x5139B0, the same body in RotWK): the page the argument's first letter names (A, B, C, M, P) becomes current, the previous one
//     is hidden (+0x41C remembers it) and the new one shown; Rotate / Zoom keep the held state of their button ("true" while held: the first letter);
//   * Manager (RW 0x9C5EA4): Manager::OnDeleteHero (RW 0x9C5D6F: a selected hero that is not a system hero, after GUI:AreYouSureDelete, is removed,
//     RW 0x80CDA2, and the list refilled), Manager::OnPlayGame (RW 0x9C4DED), Manager::OnSelectAward (RW 0x9C513E), OnMyPowerRollOver / RollOut, the
//     Mission::OnSort* names, CahManager::InitGadgets (SavedHeroList: 3 columns 9 / 46 / 45, RW 0x9C5A66 fills it with every hero's name and class
//     label; SavedHeroStats: 2 columns 80 / 20); a list selection (GLM_SELECTED 0x4014) makes the row's hero the edited one (RW 0x9C4F66);
//   * Class (RW 0x9C4AE0): one hero per (class, subclass) made at the start (RW 0x9C4951, RW 0x618F52) and the image records Cah::ClassIcon<i> = the
//     class's IconImage (RW 0x61ADD3); Class::SetClassAndType ("%d %d" RW 0xBF99D8 -> RW 0x9C47D3): that pair's hero is shown as a new hero
//     (RW 0x9C11B5(hero, 1, 1)), Cah::TypeIcon<i> = the class's subclass images (RW 0x61AF37), APT:CahClassDescription / APT:CahTypeDescription = the
//     game text of the class's / subclass's DescriptionTag; CahClass::NumClasses / NumClassTypes (RW 0x9C476B);
//   * Appearance (RW 0x9C4255): AutoChangeBttn (RW 0x9C3B7D), OnComplete (RW 0x9C4054: after the Manager page the edited hero is named and saved,
//     RW 0x9C3EC9 / 0x9C4EE3), NamePrompt (RW 0x9C3DC7: APT:EnterNameError, or LAN:ErrorDuplicateName, in a message box titled APT:EnterNameErrorTitle),
//     OnPaintColor / OnSkinColor / OnHairColor (atoi, RW 0x80976F / 0x80975A / 0x809745), Increase / DecreaseAttribute, Next / PrevAppearance (atoi of
//     the slot, RW 0x9BFD40 with kind 0 / 1 and step +1 / -1), CahAppearance::InitGadgets ("CreateAHero::HeroName": a 22 character entry holding the name,
//     RW 0x9C3F2A), the providers CahAppearance::ShowNamePrompt (RW 0x9C4076 arg 0: "1" when the entry is empty), CahAppearance::HeroNameSet (arg 1: "1"
//     when the trimmed entry is not empty and no other hero has that name, compared without case), CahAppearance::HairColor / SkinColor / PaintColor
//     ("%u");
//   * Powers (RW 0x9C338B; see CahPowers below): AptCreateAHero::OnPowerSelect ("%d,%d" RW 0xC8AD10, 1-based row and column, RW 0x9C238A),
//     OnMyPowerSelect (RW 0x9C17F4), OnNoPowerSelect (RW 0x9C180F), PowersIconsUpdate (RW 0x9C1818), OnPowerSelectionComplete (RW 0x9C181F: a new hero
//     is copied into a new list entry, RW 0x80BC55 / 0x80CF9C; after the Manager page the Manager is refreshed), OnResetBttn (RW 0x9C1887), the tooltips
//     PalantirToolTip / MatrixToolTip / MyPowerToolTip, the providers NumPowerRows / DisablePowerInstructions / CurrentPowerIndex / NumCurrentPowers
//     (RW 0x9C1A55), CahPowers::InitGadgets;
//   * Class::Exit (RW 0xC8B4CC; the Manager's and NewFeatures' back buttons) leaves the screen.
// NOT PORTED / INFERENCE (stops S-1400 .. S-1405): the take-picture path (OnTakePicture, the portrait file), the awards / statistics page
// (OnSelectAward, SavedHeroStats rows, MyHero::AwardState_<i>), OnPlayGame (the test map), the message boxes (an error is a note and the screen's
// lastMessage()), the delete confirmation (the delete happens at once), the SuppressCahPromo preference is kept for the session only, the camera
// views of ViewInfo are the device's (GodotDevice), the sort of the power rows, which page "CreateAHero" forwards to.

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/CreateAHero/AptMyHero.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AptMessageBox;
class CommandButton;
class CreateAHeroHeroList;
class CreateAHeroSystem;

// What the builder needs besides the shell (ShellEnvironment::createAHero): the system (TheCreateAHeroSystem of the shell's world), the heroes, the client
// random of AppearanceRandom, and the SuppressCahPromo option (RW OptionPreferences "SuppressCahPromo"; the device keeps it).
struct CreateAHeroScreenContext
{
	const CreateAHeroSystem *system = nullptr;
	CreateAHeroHeroList *heroes = nullptr;
	std::function<int(int, int)> random; ///< inclusive client random (RW 0x6D32E4); null: the minimum
	bool suppressPromo = false;
};

// CahPowers: the power matrix (RW 0x9C392E) and the chosen powers (RW 0x9C293D).
class CahPowers
{
public:
	enum
	{
		NUM_COLUMNS = 4,       ///< the row's four tiers (RW 0x9C3A3B)
		MAX_POWERS = 10,       ///< RW 0x9C2949: + 0x54 < 10
		PALANTIR_BUTTONS = 6,  ///< RW 0x9C2961: 6 - the template's buttons
		NO_POWER_SLOT = 7      ///< RW 0x9C2972
	};
	enum State ///< the cell states (TOOLTIP names RW 0xDB919C)
	{
		STATE_NONE = 0,
		STATE_AVAILABLE = 1,     ///< TOOLTIP:CAH_IS_AVAILABLE, icon _avail
		STATE_NEEDS_PREREQ = 2,  ///< TOOLTIP:CAH_NEEDS_PREREQ, _needPrereq
		STATE_LEVEL_TOO_LOW = 3, ///< TOOLTIP:CAH_LEVEL_TOO_LOW, _levelLow
		STATE_SELECTED = 4,      ///< TOOLTIP:CAH_IS_SELECTED, _selected
		STATE_PALANTIR_FULL = 5, ///< TOOLTIP:CAH_PALANTIR_FULL, _palantirFull
		STATE_BOOK_FULL = 6      ///< TOOLTIP:CAH_POWER_BOOK_FULL
	};
	struct Cell
	{
		const CommandButton *button = nullptr;
		int row = -1, column = -1;
		int palantirSlot = -1; ///< + 0xC
		int powerIndex = -1;   ///< + 0x10: the rank index it was chosen at
		bool selected = false; ///< + 0x14
		int state = STATE_NONE; ///< + 0x18
	};

	// RW 0x9C392E: the matrix of the hero's class (every button whose CreateAHeroUIAllowableUpgrades names the class's upgrade, RW 0x80BD66), then the
	// hero's chosen powers selected again in order
	void build(CreateAHeroHero &hero, const CreateAHeroSystem &system);
	// RW 0x9C293D: choose `cell` (the no-power cell when null) as the next power; false when refused
	bool select(Cell *cell, CreateAHeroHero &hero);
	// OnMyPowerSelect / OnResetBttn: keep only the first `count` choices
	void truncate(int count, CreateAHeroHero &hero);
	void updateStates();

	Cell *cell(int row, int column);
	int rowCount() const { return (int)m_rows.size(); }
	int chosenCount() const { return (int)m_chosen.size(); }
	const std::vector<Cell *> &chosen() const { return m_chosen; }
	int templateButtons() const { return m_templateButtons; }
	Cell &noPower() { return m_noPower; }

private:
	std::vector<std::unique_ptr<Cell>> m_cells;
	std::vector<std::array<Cell *, NUM_COLUMNS>> m_rows;
	std::vector<Cell *> m_chosen; ///< + 0x2C .. (10 entries)
	Cell m_noPower;               ///< + 0x24
	int m_nextSlot = 0;           ///< + 0x58
	int m_templateButtons = 0;    ///< RW 0x61900A
};

class AptCreateAHero : public AptScreen
{
public:
	AptCreateAHero(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	~AptCreateAHero() override;

	static const std::vector<std::string> &retailNames(); ///< every AptCreateAHero:: / page name the binary registers (the registry test)
	// the screen's acceptance stops ("[S-1400] ..." .. "[S-1405] ..."), each also a "create-a-hero-stop" note of the window manager when the screen is made
	static const std::vector<std::string> &stopLines();

	// ---- state (tests, the device's preview) ----
	char currentPage() const { return m_page; } ///< 'M', 'C', 'A', 'P', 'B' or 0
	char previousPage() const { return m_previousPage; }
	AptMyHero *myHero() { return m_myHero.get(); }
	CahPowers &powers() { return m_powers; }
	int selectedHero() const { return m_selectedHero; }
	// the hero the 3D view shows (the class page's pair hero or the edited one) and a counter that moves whenever it changes
	const CreateAHeroHero *displayedHero() const;
	std::uint32_t displayRevision() const { return m_displayRevision; }
	bool rotateLeft() const { return m_rotateLeft; }
	bool rotateRight() const { return m_rotateRight; }
	bool zoomIn() const { return m_zoomIn; }
	bool zoomOut() const { return m_zoomOut; }
	const std::string &lastMessage() const { return m_lastMessage; } ///< the labels of the last message box shown ("title: text")
	AptMessageBox *messageBox() { return m_box.get(); }            ///< lane CAH-2: the screen's message box (GuiFX.apt), null until one was shown
	bool exitRequested() const { return m_exitRequested; }
	GameWindow *heroList() const { return m_heroList; }
	GameWindow *nameEntry() const { return m_nameEntry; }
	CreateAHeroScreenContext *context() { return m_env.createAHero; }

	// the page operations the movie's commands run (the tests call them too)
	void showPage(char page);                  ///< OnShowScreen
	void selectHero(int index);                ///< RW 0x9C4F66
	void setClassAndType(int cls, int type);   ///< RW 0x9C47D3
	bool completeAppearance();                 ///< Appearance::OnComplete
	bool completePowers();                     ///< OnPowerSelectionComplete
	bool deleteSelectedHero();                 ///< Manager::OnDeleteHero: asks GUI:AreYouSureDelete (RW 0x9C5D6F); true when asked
	bool removeSelectedHero();                 ///< the question's Yes (RW 0x9C5D16): the hero and its file go
	bool choosePower(int row, int column);     ///< OnPowerSelect (0-based) + the update's select
	void chooseNoPower();
	// automation (the device's scripted runs): the name entry's text as typed (false: no live entry); the first available matrix cell, 1-based "row,col" ("" none)
	bool typeName(const std::u16string &name); ///< lane CAH-2 r2: typed through the entry's insert (RW 0x72260B, the 22 limit, RW 0x75E4DF); false if a character was refused
	std::string firstAvailablePower();

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	void registerAll();
	void update();
	void fillHeroList();
	void refreshTexts();
	void refreshClassPage();
	void refreshPowersPage();
	void setImage(const std::string &name, const std::string &image);
	void message(const std::string &titleLabel, const std::string &textLabel);
	void bumpDisplay() { ++m_displayRevision; }
	bool nameIsFree(const std::u16string &name) const;
	std::u16string entryName() const;
	void invoke(const std::string &function, const std::vector<std::string> &args);

	ShellEnvironment &m_env;
	std::unique_ptr<AptMyHero> m_myHero;
	CahPowers m_powers;
	std::vector<CreateAHeroHero> m_classHeroes; ///< the class page's hero per (class, subclass) (+0x18)
	int m_classHero = -1;
	bool m_showClassHero = false;
	char m_page = 0;
	char m_previousPage = 0;
	int m_selectedHero = -1;
	std::uint32_t m_displayRevision = 1;
	bool m_rotateLeft = false, m_rotateRight = false, m_zoomIn = false, m_zoomOut = false;
	int m_pictureFrames = 0;
	std::string m_lastMessage;
	std::unique_ptr<AptMessageBox> m_box;      ///< lane CAH-2: TheMessageBox's calls of this screen (RW 0x81A375 / 0x81A452)
	int m_deleteAnswer = -1;                   ///< the delete question's button, acted on in update (outside the box's own command)
	bool m_exitRequested = false;
	GameWindow *m_heroList = nullptr;
	GameWindow *m_heroStats = nullptr;
	GameWindow *m_nameEntry = nullptr;
	CahPowers::Cell *m_pendingPower = nullptr; ///< + 0x20
	int m_keepPowers = CahPowers::MAX_POWERS;  ///< + 0x28
	bool m_powersDirty = false;                ///< + 0x5C
	bool m_listDirty = true;
	bool m_editingExisting = false;            ///< the Appearance / Powers pages were opened from the Manager (+0x41C == Manager)
	int m_pendingQueryRow = -1, m_pendingQueryCol = -1; ///< the cell CurrentPowerIndex was last set to
	int m_powersScrollUpdates = 0;             ///< updates left that put the matrix at its first row (S-1404)
};
