// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptMainMenu.h.

#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "Common/PathIO.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/WindowManager.h"

#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/Credits.h"
#include "Common/INIException.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/BfmeGame.h"
#include "GameClient/OptionPreferences.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptActionInterpreter.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptObject.h"

#include "GameClient/GUI/Skirmish/LivingWorldShellInfo.h" // lane WOTR-1

#include <algorithm>
#include <filesystem>

namespace
{
struct CommandRow
{
	const char *name;
	ShellAction action;
};

// Commands that become a ShellRequest (their engine behaviour belongs to other lanes).
const CommandRow kRequestCommands[] = {
	{ "AptMainMenu::LoadGame", ShellAction::LoadGame },
	{ "AptMainMenu::LoadReplay", ShellAction::LoadReplay },
	{ "AptMainMenu::LAN", ShellAction::Lan },
	{ "AptMainMenu::OnlineButtonPressed", ShellAction::Online },
	{ "AptMainMenu::BattleSchool", ShellAction::BattleSchool },
	{ "AptMainMenu::LevelSelect", ShellAction::LevelSelect },
	{ "AptMainMenu::LoadCampaign", ShellAction::LoadCampaign },
	{ "AptMainMenu::ContinueCampaign", ShellAction::ContinueCampaign },
	{ "AptMainMenu::Expansion1Campaign", ShellAction::Expansion1Campaign },
	{ "AptMainMenu::BonusCampaign", ShellAction::BonusCampaign },
	{ "AptMainMenu::OnTutorial", ShellAction::Tutorial },
	{ "AptMainMenu::StopGameMovie", ShellAction::StopGameMovie },
	{ "AptMainMenu::ResetResolution", ShellAction::ResetResolution },
};
} // namespace

const std::vector<std::string> &AptMainMenu::retailNames()
{
	static const std::vector<std::string> names = {
		"AptMainMenu::RenderCredits", "AptMainMenu::OnTutorial", "AptMainMenu::ResetResolution", "AptMainMenu::StopGameMovie",
		"AptMainMenu::BattleSchool", "AptMainMenu::OnlineButtonPressed", "AptMainMenu::LAN", "AptMainMenu::LevelSelect",
		"AptMainMenu::LoadReplay", "AptMainMenu::LoadGame", "AptMainMenu::ExitGame", "AptMainMenu::CreditsExit",
		"AptMainMenu::Credits", "AptMainMenu::CreateAHero", "AptMainMenu::Options", "AptMainMenu::Skirmish",
		"AptMainMenu::LoadCampaign", "AptMainMenu::ContinueCampaign", "AptMainMenu::WarOfTheRing", "AptMainMenu::BonusCampaign",
		"AptMainMenu::Expansion1Campaign", "AptMainMenu::OnInitialized"
	};
	return names;
}

AptMainMenu::AptMainMenu(WindowManager &windows, Shell &shell, ShellServices &services, const ShellEnvironment *environment)
	: AptScreen(windows, shell, "MainMenu.apt", "AptMainMenu"), m_services(services), m_env(environment)
{
	// lane FB7-1: the ctor (RW 0x91C9A9, first instance) binds the mapped image LogoWithShadow to the movie's RenderImage clip `Image` through
	// RW 0x6236F6 (WindowManager, image record by name; RW 0x91CA5C .. 0x91CA98): the game's logo over the front-end background
	windows.setAptImage("Image", "LogoWithShadow");
	registerAll();
	windows.addUpdateListener(this, [this]() { update(); });
}

AptMainMenu::~AptMainMenu()
{
	windows().clearRenderCallback("AptMainMenu::RenderCredits");
}

void AptMainMenu::runInit()
{
	AptScreen::runInit();
	if (m_env && m_env->fileSystem && isBfme1Data(*m_env->fileSystem))
	{
		windows().hideBackground(true);
	}
}

namespace
{
// the main menu's requests that open a screen of their own (the names as the menu's buttons call them), and why a build has no screen for it, in the
// player's words (lane MENUWORK-1: the box says why). Not Credits: MainMenu.apt's CreditsButton plays its own _CreditsMovie clip with an exit button
// (ExitCreditsButton -> ShowMainMenu), the command starts the roll (lane UI-4)
struct UnavailableScreen
{
	const char *action;
	const char *name;
	const char *why;
};
const UnavailableScreen kUnavailableScreens[] = {
	{ "CreateAHero", "My Heroes (Create-a-Hero)", "this build has no screen for it" },
	{ "LoadGame", "Load Game", "this build has no screen for it" },
	{ "Online", "Online", "this build has no screen for it" },
	{ "BattleSchool", "Battle School", "OpenBFME does not have it yet" },
	{ "LevelSelect", "Level Select", "OpenBFME does not have it yet" },
	{ "LoadCampaign", "Load Campaign", "OpenBFME does not have it yet" },
	{ "ContinueCampaign", "Continue Campaign", "this build has no screen for it" },
	{ "GoodCampaign", "BFME1 Good campaign (Living World)", "OpenBFME does not have it yet" },
	{ "EvilCampaign", "BFME1 Evil campaign (Living World)", "OpenBFME does not have it yet" },
	// lane CAMPMENU-1: not Expansion1Campaign / BonusCampaign: the host starts them (godot/scripts/campaign_flow.gd). BFME2's Good / Evil campaigns (BFME2
	// MainMenu.apt's GoodCampaign / EvilCampaign, hidden while godot/scripts/game.gd BFME2_CAMPAIGNS is off) are refused by these names
	{ "BFME2GoodCampaign", "The BFME2 Good campaign", "OpenBFME does not have it yet" },
	{ "BFME2EvilCampaign", "The BFME2 Evil campaign", "OpenBFME does not have it yet" },
	{ "Tutorial", "The War of the Ring tutorial", "OpenBFME does not have it yet" }, // lane BFME2CAMP1: Basic / Advanced start (game.gd)
	{ "Lan", "Network", "this build has no screen for it" },
	{ "LoadReplay", "Replays", "this build has no screen for it" },
};

const UnavailableScreen *unavailableRow(const std::string &action)
{
	for (const UnavailableScreen &row : kUnavailableScreens)
	{
		if (action == row.action)
		{
			return &row;
		}
	}
	return nullptr;
}
} // namespace

const char *const AptMainMenu::kWarOfTheRingStable =
	"War of the Ring is not in the Stable version yet: it is being tested in the Nightly version (OpenBFME Launcher: Settings, Channel).";

std::string AptMainMenu::unavailableScreenName(const std::string &action)
{
	const UnavailableScreen *row = unavailableRow(action);
	return row ? row->name : std::string();
}

std::string AptMainMenu::unavailableText(const std::string &action)
{
	const UnavailableScreen *row = unavailableRow(action);
	return row ? std::string(row->name) + " is not available yet: " + row->why + "." : std::string();
}

std::string AptMainMenu::screenUnavailable(const std::string &action)
{
	const std::string screen = unavailableScreenName(action);
	if (screen.empty())
	{
		return std::string(); // a request that opens no screen (StopGameMovie, ResetResolution ...): nothing is left disabled by it
	}
	const std::string line = "[S-1914] " + screen + " (AptMainMenu " + action + ") is not available in this build yet: the menu's request has no screen; the "
		"player is told and the main menu is usable again";
	windows().note("unported-screen", line);
	showUnavailableBox(unavailableText(action)); // lane MENUWORK-1: the box says why
	return line;
}

void AptMainMenu::showUnavailableBox(const std::string &text)
{
	if (!m_box)
	{
		m_box = std::make_unique<AptMessageBox>(windows(), shell());
	}
	m_boxClosed = false;
	m_box->show(AptMessageBox::TYPE_OK, u"OpenBFME", std::u16string(text.begin(), text.end()), [this](int) { m_boxClosed = true; });
	if (m_box->level() < 0)
	{
		// GuiFX.apt did not load (noted by the box): the menu comes back at once so it is never left disabled
		m_boxClosed = true;
	}
}

// ---- lane BFME2CAMP1: the other game's two campaign rows in the Solo Play list (an OpenBFME addition, see AptMainMenu.h) ----------------------------
namespace
{
constexpr int kExtraRowDepth = 9100; // ActionScript depths (9100, 9101) above every placed child of SoloPlayNav

// one menu style's addition
struct ListPlan
{
	const char *movie;          // "bfme2" / "rotwk"
	const char *ownFunction;    // the movie's own campaign function that identifies it (and is absent from the other movie)
	const char *otherFunction;  // a function of the other movie (its absence is checked too)
	const char *source;         // the row copied
	const char *insertAbove;    // the copies go right above this row (the rows above it move up)
	const char *names[2];       // the copies from the bottom up
	const char *labels[2];
	int lockedUntilBonus;       // index of the copy disabled until the bonus unlock, -1 none
};

const ListPlan kPlans[] = {
	{ "bfme2", "GoodCampaignButton", "Expansion1CampaignButton", "GoodCampaign", "LoadGame", { "BonusCampaign", "Expansion1Campaign" },
		{ "$BonusCampaign", "$Expansion1Campaign" }, 0 },
	{ "rotwk", "Expansion1CampaignButton", "GoodCampaignButton", "Expansion1Campaign", "Expansion1Campaign", { "GoodCampaign", "EvilCampaign" },
		{ "$GoodCampaign", "$EvilCampaign" }, -1 },
};

bool sameMatrix(const AptMatrix &a, const AptMatrix &b)
{
	return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.tx == b.tx && a.ty == b.ty;
}

// A clip this addition moves: the timeline's matrix (`base`) and what was written over it. When the clip still holds the written matrix the timeline
// has not touched it since and `base` stands; otherwise (a new instance, or a timeline frame that placed it again) its matrix is the new base
AptMatrix unpack(const float (&f)[6])
{
	AptMatrix m;
	m.a = f[0];
	m.b = f[1];
	m.c = f[2];
	m.d = f[3];
	m.tx = f[4];
	m.ty = f[5];
	return m;
}

void pack(const AptMatrix &m, float (&f)[6])
{
	f[0] = m.a;
	f[1] = m.b;
	f[2] = m.c;
	f[3] = m.d;
	f[4] = m.tx;
	f[5] = m.ty;
}

template <class Clip>
AptMatrix baseOf(std::vector<Clip> &moved, AptCharacterInst &inst)
{
	for (Clip &m : moved)
	{
		if (m.inst == &inst && sameMatrix(inst.matrix, unpack(m.written)))
		{
			return unpack(m.base);
		}
	}
	return inst.matrix;
}

template <class Clip>
void write(std::vector<Clip> &moved, AptCharacterInst &inst, const AptMatrix &base, const AptMatrix &to)
{
	inst.matrix = to;
	for (Clip &m : moved)
	{
		if (m.inst == &inst)
		{
			pack(base, m.base);
			pack(to, m.written);
			return;
		}
	}
	Clip c;
	c.inst = &inst;
	pack(base, c.base);
	pack(to, c.written);
	moved.push_back(c);
}

// a row of the list: its button "bttn" (RotWK's locked BonusCampaign row shows only its "bttnDisabled" face)
bool isRow(AptCharacterInst *c)
{
	AptSpriteInst *s = c ? c->asSprite() : nullptr;
	return s && (s->childByName("bttn") != nullptr || s->childByName("bttnDisabled") != nullptr);
}

// a piece of the list's frame: BFME2's NavFrameFive, RotWK's unnamed clips
bool isFramePiece(AptCharacterInst *c)
{
	return c && c->asSprite() && !isRow(c) && (c->instName().empty() || c->instName().rfind("NavFrame", 0) == 0);
}

// the clip's content bounds (its own space) through `m` into the parent's space: y0 (top), y1 (bottom)
bool parentYBounds(const AptCharacterInst &c, const AptMatrix &m, float &y0, float &y1)
{
	float lx0 = 0, ly0 = 0, lx1 = 0, ly1 = 0;
	if (!c.contentBounds(lx0, ly0, lx1, ly1))
	{
		return false;
	}
	bool first = true;
	for (float lx : { lx0, lx1 })
	{
		for (float ly : { ly0, ly1 })
		{
			float px = 0, py = 0;
			m.apply(lx, ly, px, py);
			y0 = first ? py : std::min(y0, py);
			y1 = first ? py : std::max(y1, py);
			first = false;
		}
	}
	return y1 > y0;
}
} // namespace

void AptMainMenu::updateExtraRows()
{
	std::vector<MovedClip> &moved = m_extraMoved; // the clips of the open list (cleared when it closes)
	AptSpriteInst *root = level() >= 0 ? windows().apt().level(level()) : nullptr;
	if (!root || (m_env && m_env->fileSystem && isBfme1Data(*m_env->fileSystem)))
	{
		return; // BFME1's menu: its Good / Evil are the Living World's
	}
	const ListPlan *plan = nullptr;
	for (const ListPlan &p : kPlans)
	{
		if (m_extraRows.installed ? m_extraRows.movie == p.movie : false)
		{
			plan = &p;
		}
	}
	if (!m_extraRows.installed)
	{
		for (const ListPlan &p : kPlans)
		{
			AptValue own, other, open;
			if (root->getMember(p.ownFunction, own) && own.isObject() && root->getMember("OpenDifficulty", open) && open.isObject() &&
				!(root->getMember(p.otherFunction, other) && other.isObject()))
			{
				plan = &p;
			}
		}
		if (!plan)
		{
			return;
		}
		for (const char *side : plan->names)
		{
			const std::string name = std::string(side) + "Button"; // the row's click calls _root[<row name> + "Button"]()
			const std::string s = side;
			std::weak_ptr<std::vector<std::string>> pending = m_extraPending;
			AptFunction *fn = windows().apt().vm().newNativeFunction(name, [pending, s](AptCallInfo &) {
				if (const auto queue = pending.lock()) // the menu is still there: OpenDifficulty(side) runs on its next update, outside this call
				{
					queue->push_back(s);
				}
				return AptValue::undefined();
			});
			root->setMember(name, AptValue::object(fn));
		}
		m_extraRows.installed = true;
		m_extraRows.movie = plan->movie;
		moved.clear();
	}
	if (!plan)
	{
		return;
	}
	std::vector<std::string> pending;
	pending.swap(*m_extraPending);
	for (const std::string &side : pending)
	{
		std::string error;
		if (!windows().invokeAS(level(), "OpenDifficulty", { side }, nullptr, &error))
		{
			windows().note("invoke-failed", "MainMenu OpenDifficulty(" + side + ") for the added campaign row: " + error);
		}
	}
	AptCharacterInst *navInst = root->childByName("SoloPlayNav");
	AptSpriteInst *nav = navInst ? navInst->asSprite() : nullptr;
	if (!nav)
	{
		return;
	}
	AptCharacterInst *source = nav->childByName(plan->source);
	AptCharacterInst *below = nav->childByName(plan->insertAbove);
	AptCharacterInst *added[2] = { nav->childByName(plan->names[0]), nav->childByName(plan->names[1]) };
	AptCharacterInst *load = nav->childByName("LoadGame");
	if (!source || !below || !load || !isRow(source) || !isRow(below) || !isRow(load))
	{
		// the list is closed (its rows are gone): the copies go too, the next opening makes new ones
		for (AptCharacterInst *c : added)
		{
			if (c)
			{
				windows().apt().removeClipByTarget(nav, AptValue::object(c));
			}
		}
		moved.clear();
		return;
	}
	const AptMatrix sourceBase = baseOf(moved, *source);
	const AptMatrix belowBase = baseOf(moved, *below);
	const AptMatrix loadBase = baseOf(moved, *load);
	// a row's height upward (negative: -35.5 in both movies): the rows' spacing from Load Game to the next row up
	float step = 0;
	for (AptCharacterInst *c : nav->children())
	{
		if (c != load && c != added[0] && c != added[1] && isRow(c))
		{
			const float d = baseOf(moved, *c).ty - loadBase.ty;
			step = (d < 0 && (step == 0 || d > step)) ? d : step;
		}
	}
	if (step == 0)
	{
		return;
	}
	for (int i = 0; i < 2; ++i)
	{
		if (added[i])
		{
			continue;
		}
		if (!windows().apt().duplicateClip(nav, AptValue::object(source), AptValue::string(plan->names[i]), AptValue::integer(kExtraRowDepth + i), &added[i]) ||
			!added[i])
		{
			windows().note("main-menu-extra-row", std::string("the Solo Play row ") + plan->names[i] + " could not be made (duplicateMovieClip of SoloPlayNav." +
													  plan->source + ")");
			added[i] = nullptr;
			continue;
		}
		added[i]->setMember("buttonName", AptValue::string(plan->labels[i]));
		AptValue closeParent; // RotWK's rows close the list on a click (OnButtonClick reads it); duplicateMovieClip copies no variable
		if (source->getMember("closeParent", closeParent))
		{
			added[i]->setMember("closeParent", closeParent);
		}
		++m_extraRows.rowsAdded;
	}
	// lane STABLEQA-1: a Stable build hides War of the Ring's row (both movies list it above the insertion row); the rows above it move up one place
	// fewer, so they close its gap, and the list grows by one row instead of two
	AptCharacterInst *hidden = warOfTheRingHidden() ? nav->childByName("WarOfTheRing") : nullptr;
	hidden = isRow(hidden) && baseOf(moved, *hidden).ty < belowBase.ty ? hidden : nullptr;
	const float hiddenTy = hidden ? baseOf(moved, *hidden).ty : 0.0f;
	const float grow = (hidden ? 1.0f : 2.0f) * step;
	if (hidden)
	{
		hidden->visible = false;
	}
	// the rows above the insertion row move up two places
	float rowsTop = 0;
	bool haveRowsTop = false;
	for (AptCharacterInst *c : nav->children())
	{
		if (c == added[0] || c == added[1] || c == hidden || !isRow(c))
		{
			continue;
		}
		const AptMatrix base = baseOf(moved, *c);
		float y0 = 0, y1 = 0;
		if (parentYBounds(*c, base, y0, y1))
		{
			rowsTop = haveRowsTop ? std::min(rowsTop, y0) : y0;
			haveRowsTop = true;
		}
		if (base.ty < belowBase.ty)
		{
			AptMatrix to = base;
			to.ty = base.ty + ((hidden && base.ty < hiddenTy) ? 1.0f : 2.0f) * step;
			write(moved, *c, base, to);
		}
	}
	for (int i = 0; i < 2; ++i)
	{
		if (!added[i])
		{
			continue;
		}
		AptMatrix to = sourceBase;
		to.ty = belowBase.ty + (float)(i + 1) * step;
		added[i]->matrix = to;
		added[i]->visible = source->visible;
		added[i]->color = source->color;
	}
	// a copy disabled until the bonus unlock (RotWK: MainMenuUnlockBonusCampaign, RW 0x91BB6B): the row's own "_disable" state
	if (plan->lockedUntilBonus >= 0 && added[plan->lockedUntilBonus] && !(m_env && m_env->bonusCampaignUnlocked))
	{
		AptValue done;
		AptSpriteInst *b = added[plan->lockedUntilBonus]->asSprite();
		const int disabled = b ? b->labelFrame("_disable") : -1;
		if (b && disabled >= 0 && !(b->getMember("openbfmeDisabled", done) && done.isBoolean() && done.asBool()))
		{
			b->gotoFrame(disabled);
			b->playing = true;
			b->setMember("openbfmeDisabled", AptValue::boolean(true));
		}
	}
	// the frame pieces (every other clip with art, not a button and not the list's OpenButton): each stretched about its bottom edge so its part
	// above the rows' top moves up by the two rows (one with War of the Ring hidden); a piece lying mostly above the rows' top moves up instead
	if (!haveRowsTop)
	{
		return;
	}
	for (AptCharacterInst *c : nav->children())
	{
		if (c == added[0] || c == added[1] || !isFramePiece(c))
		{
			continue;
		}
		const AptMatrix base = baseOf(moved, *c);
		float y0 = 0, y1 = 0;
		if (!parentYBounds(*c, base, y0, y1))
		{
			continue;
		}
		AptMatrix to = base;
		if (y1 - rowsTop < (y1 - y0) * 0.5f)
		{
			to.ty = base.ty + grow;
		}
		else
		{
			const float anchor = std::max(rowsTop, y0);
			const float k = (y1 - (anchor + grow)) / (y1 - anchor);
			to.b = base.b * k;
			to.d = base.d * k;
			to.ty = y1 + (base.ty - y1) * k;
		}
		write(moved, *c, base, to);
	}
}

void AptMainMenu::update()
{
	updateExtraRows(); // lane BFME2CAMP1
	if (m_state == 4 && m_credits)
	{
		// RW 0x91C631: the roll's update each engine frame; the engine is limited to 100 frames a second while it rolls (RW 0x91B6DA), so the port
		// runs one step per 10 ms of the shell's clock (INFERENCE, S-2520)
		m_creditsClockMs += std::max(0, windows().lastElapsedMs());
		int steps = 0;
		while (m_creditsClockMs >= 10 && steps < 25)
		{
			m_creditsClockMs -= 10;
			stepCredits();
			++steps;
		}
		if (steps == 25)
		{
			m_creditsClockMs = 0; // a stall is not replayed
		}
	}
	if (!m_box)
	{
		return;
	}
	m_box->update();
	if (m_boxClosed)
	{
		// the box's Ok: the menu gets OnFocus("1") as after a sub-screen's pop (RW 0x46E170) and its ShowMainMenu reveals the buttons; the box is
		// released here, outside its own button command
		m_boxClosed = false;
		m_box.reset();
		std::string error;
		if (!windows().invokeAS(level(), "OnFocus", { "1" }, nullptr, &error))
		{
			windows().note("invoke-failed", "MainMenu OnFocus(1) after the unavailable-screen box: " + error);
		}
	}
}

void AptMainMenu::stepCredits()
{
	if (!m_credits)
	{
		return;
	}
	m_credits->update();
	if (m_credits->isFinished() && !m_hideCreditsAsked)
	{
		// RW 0x91C646 .. 0x91C66C: the movie's HideCredits (asked each frame in retail; the movie has no such member, so the port asks once and notes it)
		m_hideCreditsAsked = true;
		std::string error;
		if (!windows().invokeAS(level(), "HideCredits", {}, nullptr, &error))
		{
			windows().note("credits", "the roll finished; MainMenu has no HideCredits (" + error + "): the page stays until Exit, as in retail");
		}
	}
}

void AptMainMenu::startCredits()
{
	// RW 0x91B5E9
	m_credits.reset();
	m_hideCreditsAsked = false;
	m_creditsClockMs = 0;
	if (!m_env || !m_env->fileSystem || !m_env->language || !windows().gadgetLayer())
	{
		windows().note("credits", "[S-2520] the credits cannot roll: the shell environment has no archives, language.ini or font metrics");
	}
	else
	{
		auto credits = std::make_unique<CreditsManager>();
		int stageWidth = 1024;
		if (AptSpriteInst *root = windows().apt().level(level()))
		{
			if (root->timelineFile)
			{
				stageWidth = (int)root->timelineFile->width;
			}
		}
		try
		{
			credits->load(*m_env->fileSystem, m_env->gameText, *m_env->language, windows().gadgetLayer()->gadgets().fontMetrics(), stageWidth);
			credits->init();
			m_credits = std::move(credits);
		}
		catch (const INIException &e)
		{
			windows().note("credits", std::string("Data\\INI\\Credits.ini: ") + e.what());
		}
	}
	windows().note("unported-transition", "[S-2520] MainMenuToCreditsScreen: the credits page appears without its window transition");
	m_state = 4;
	request(ShellAction::Credits, std::string()); // the host: the shell music nudged, CreditsMusic, the frame rate
}

void AptMainMenu::exitCredits()
{
	// RW 0x91B6FD (BFME2 decomp AptMainMenu::CreditsExit, 0x915231)
	if (m_credits)
	{
		m_credits->reset();
		m_credits.reset();
	}
	m_state = 0;
	m_creditsClockMs = 0;
	request(ShellAction::CreditsExit, std::string()); // the host: the shell's music back
}

void AptMainMenu::request(ShellAction action, const std::string &argument)
{
	ShellRequest r;
	r.action = action;
	r.argument = argument;
	m_services.request(r);
}

void AptMainMenu::showSkirmish()
{
	// ShowSkirmish.cpp (0x00579440): push Skirmish.apt unless its singleton already exists
	if (!shell().findScreenByFilename("Skirmish.apt"))
	{
		shell().push("Skirmish.apt", false);
		windows().setBackground(1); // lane FB7-1: RotWK's opener shows the front-end background (RW 0x928324 .. 0x928326)
	}
}

bool AptMainMenu::warOfTheRingHidden() const
{
	const ShellEnvironment *environment = m_environment ? m_environment : m_env;
	return environment && !environment->warOfTheRingOffered;
}

void AptMainMenu::showWarOfTheRing()
{
	// lane WOTR-1.  AptMainMenu::WarOfTheRing (RW 0x91AFE4) sets the transition state 9 / action 8; the menu update (RW 0x91C2FC case 8) runs the shell clean-up
	// RW 0x7792BC (not ported: the clean-up of a game that is not running here) and RW 0x9282E3(1): the strategic flag RW 0xDB6BF0, then Skirmish.apt unless
	// it exists.  The menu's fade-out before the action (state 9) is not modelled: the push waits for the shell's own transition.
	if (warOfTheRingHidden())
	{
		// lane STABLEQA-1: a Stable build hides the row (updateExtraRows); a click that still arrives (a script, a mod's menu) is refused with the box,
		// whose Ok gives the menu back (no request: the host would open the war)
		windows().note("war-of-the-ring-stable", "AptMainMenu::WarOfTheRing: refused, a Stable channel build offers no War of the Ring");
		showUnavailableBox(kWarOfTheRingStable);
		return;
	}
	request(ShellAction::WarOfTheRing, std::string()); // the engine still sees the action (as every main-menu action)
	if (!m_environment || !m_environment->livingWorld)
	{
		windows().note("skirmish-environment-missing", "AptMainMenu::WarOfTheRing: no LivingWorldShellInfo, the strategic setup cannot open");
		return;
	}
	m_environment->livingWorld->strategicRequested = true;
	if (!shell().findScreenByFilename("Skirmish.apt"))
	{
		shell().push("Skirmish.apt", false);
	}
}

void AptMainMenu::registerAll()
{
	registerCommand("AptMainMenu::OnInitialized", [this](const std::string &) {
		// the retail body sets preference counters (TimesInGame, FlashTutorial) and the logo mapped image; not ported [S-175]
		windows().note("unported-command", "AptMainMenu::OnInitialized: preference bookkeeping");
		// BFME1 (docs/BFME1.md): MainMenu.apt shows its menu bar only when ShowMainMenu() is called; its root stops after the background fade
		// and no frame script calls it. INFERENCE: the engine's OnInitialized does (Open-BFME-1 declares BfmeAptScreenMainMenu::_bfme_onInitialized,
		// AptMainMenuConstructor.cpp:275, without its body); without the call no menu button ever appears
		if (m_env && m_env->fileSystem && isBfme1Data(*m_env->fileSystem))
		{
			std::string err;
			if (level() < 0 || !windows().invokeAS(level(), "ShowMainMenu", {}, nullptr, &err))
				windows().note("bfme1-main-menu", "ShowMainMenu() could not be called: " + err);
		}
	});
	registerCommand("AptMainMenu::Skirmish", [this](const std::string &) {
		if (m_environment && m_environment->livingWorld)
		{
			m_environment->livingWorld->strategicRequested = false; // RW 0x9282E3(0)
		}
		showSkirmish();
	});
	registerCommand("AptMainMenu::WarOfTheRing", [this](const std::string &) { showWarOfTheRing(); }); // lane WOTR-1
	registerCommand("AptMainMenu::GoodCampaign", [this](const std::string &argument) { request(ShellAction::LoadCampaign, "bfme2:good:" + argument); });
	registerCommand("AptMainMenu::EvilCampaign", [this](const std::string &argument) { request(ShellAction::LoadCampaign, "bfme2:evil:" + argument); });
	registerCommand("AptMainMenu::Options", [this](const std::string &argument) {
		// MainMenu calls GameCode('Options', true|false): advanced or basic (spec 1.10). Lane FB7-1: the menu's state 9 opens it with
		// RW 0x91ED91(0, 1, 1, advanced) (RW 0x91C37E .. 0x91C38A): no network, the resolution and the advanced options allowed
		m_optionsAdvanced = argument == "true" || argument == "1";
		AptOptionsScreen::open(shell(), false, true, true, m_optionsAdvanced);
	});
	registerCommand("AptMainMenu::CreateAHero", [this](const std::string &argument) {
		// lane CAH-1, RW 0x91A018: CreateAHero.apt is pushed and the map Maps\CreateAHero\CreateAHero.map is started behind it (mode 7): the request
		// is the device's cue to start that map (the 3D view of the builder)
		if (!shell().findScreenByFilename("CreateAHero.apt"))
		{
			shell().push("CreateAHero.apt", false);
		}
		request(ShellAction::CreateAHero, argument);
	});
	registerCommand("AptMainMenu::Credits", [this](const std::string &) { startCredits(); });
	registerCommand("AptMainMenu::CreditsExit", [this](const std::string &) { exitCredits(); });
	registerCommand("AptMainMenu::ExitGame", [this](const std::string &argument) {
		// AptMainMenuExitGame.cpp: script hook, sound, TheShell->pop(), TheGameEngine->setQuitting(true).  The pop runs on the next
		// WindowManager::update (the shell cannot be torn down from inside a movie script)
		request(ShellAction::ExitGame, argument);
		windows().requestShellPop();
	});
	std::vector<CommandRow> requests(std::begin(kRequestCommands), std::end(kRequestCommands));
	if (m_env && m_env->fileSystem && isBfme1Data(*m_env->fileSystem))
	{
		requests.push_back({ "AptMainMenu::Tutorial", ShellAction::Tutorial }); // community PR #36: BFME1 MainMenu.apt's name (BFME1 data only)
	}
	for (const CommandRow &row : requests)
	{
		const ShellAction action = row.action;
		const std::string name = row.name;
		registerCommand(name, [this, action, name](const std::string &argument) {
			request(action, argument);
			windows().note("unported-command", name + (argument.empty() ? std::string() : "(" + argument + ")") + ": the request is the whole behaviour so far [S-175]");
		});
	}
	// providers: MainMenuLevel and BlinkBattleSchoolOff are not wired [S-175]
	// lane CAMPSAVE-1: RW 0x91BB6B's cases 2 and 1 (the value "0" first, "1" when true; a setting changes nothing): TheGlobalData + 0xAF5 (Options.ini's BCU
	// at start, the campaign's end sets it, RW 0x927F4C) and whether the Save folder holds "00000000.sav" (RW 0x6DDE0F). RotWK's MainMenu.apt reads only
	// the first (its single player page enables BonusCampaign by it); no button of the movie asks the second
	registerProvider("MainMenuUnlockBonusCampaign", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = (m_env && m_env->bonusCampaignUnlocked) ? "1" : "0";
		}
		return true;
	});
	windows().markNumericProvider("MainMenuUnlockBonusCampaign"); // the single player page tests it with Not: "0" must be false (S-1063's inference)
	registerProvider("MainMenuLevel", unwiredProvider("MainMenuLevel"));
	windows().markNumericProvider("MainMenuContinueCampaign");
	registerProvider("MainMenuContinueCampaign", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = (m_env && continueCampaignSaveExists(m_env->saveFolder)) ? "1" : "0";
		}
		return true;
	});
	registerProvider("BlinkBattleSchoolOff", unwiredProvider("BlinkBattleSchoolOff"));
	// BFME1 BfmeAptScreenMainMenuProvider.cpp (0x0051E9B0): selectors
	// 0 (read/write level), 1 (campaign save exists), 3 (FlashTutorial inverted).
	if (m_env && m_env->fileSystem && isBfme1Data(*m_env->fileSystem))
	{
		windows().unregisterProvider("MainMenuLevel");
		windows().unregisterProvider("MainMenuContinueCampaign");
		windows().unregisterProvider("BlinkBattleSchoolOff");
		registerProvider("MainMenuLevel", [this](const std::string &, std::string &value, bool setting) {
			if (setting) m_mainMenuLevel = value; else value = m_mainMenuLevel;
			return true;
		});
		registerProvider("MainMenuContinueCampaign", [](const std::string &, std::string &value, bool setting) {
			if (!setting) value = "0"; // Living World campaign save import is not implemented.
			return true;
		});
		registerProvider("BlinkBattleSchoolOff", [this](const std::string &, std::string &value, bool setting) {
			if (!setting) value = m_env->options && !m_env->options->getYesNo("FlashTutorial", true) ? "1" : "0";
			return true;
		});
	}
	// the credits render callback (registerAptCallback, RW 0x91D301 binds RW 0x91B1B8): the clip is a component with no window; the device asks the
	// render callback below for the text it draws in the clip's place
	registerComponent("AptMainMenu::RenderCredits", [](AptComponentRequest &) -> std::shared_ptr<GameWindow> { return nullptr; });
	windows().setRenderCallback("AptMainMenu::RenderCredits", [this](float x, float y, float w, float h, GadgetDrawList &out) {
		if (m_credits)
		{
			m_credits->draw(x, y, w, h, out); // RW 0x91B1B8: only with a roll ([RW 0xDEBF50] != 0)
		}
	});
}

bool AptMainMenu::continueCampaignSaveExists(const std::string &saveFolder)
{
	// RW 0x91BBBF .. 0x91BBD5: TheGameState RW 0x6DDE0F(L"00000000.sav") -> RW 0x6DD85E (a name without '\\' is taken in the Save folder) -> TheLocalFileSystem
	// RW 0xA14220 (the file exists)
	if (saveFolder.empty())
	{
		return false;
	}
	std::error_code ec;
	return std::filesystem::is_regular_file(PathIO::native(saveFolder) / "00000000.sav", ec);
}
