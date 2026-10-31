// OpenBFME tests, lane HOVER-1: every on-hover text from the game's own files. The help box text of the command buttons (RW 0x807A81's remaining
// branches), the InGameSimpleHelp layout (RW 0x940159 / 0x93FF0A), the unit help over a hero portrait (RW 0x8EBC61) and the spell book's / hero bar's
// providers (RW 0x930DC4 / 0x92BF34 / 0x92C265). GPL-3.0.
// The retail cases hover with a scripted pointer through the HUD (InGameHud::mouseMove) and compare what the help box holds with the string file's text
// (data/lotr.str of the install) for the button's labels, the numbers taken from the logic's own cost functions; they SKIP loudly without the installs.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "Common/BuildAssistant.h"
#include "GameClient/AptCanvas.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/ControlBarRadialMenu.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHelpBox.h"
#include "GameClient/InGameHeroSelect.h"
#include "GameClient/InGameHud.h"
#include "GameClient/UnitHelpSource.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"

#include <cmath>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
struct MapText : GameTextSource
{
	std::map<std::string, std::u16string> t;
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		auto it = t.find(label);
		if (it == t.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}
};

std::string utf8(const std::u16string &s)
{
	return loadScreenU16ToUtf8(s);
}
} // namespace

TEST_CASE("hover1 simple help: the title loses its '&', the two strings wrap at the width and stack centred (RW 0x94002B / 0x940159 / 0x93FF0A)")
{
	CommandButtonHelp help = CommandButtonHelp::simple(u"Ara&gorn", u"Level: 3 / Maximum Level: 10\nMaximum Health: 2400");
	CHECK(help.isSimple());
	CHECK(help.name() == u"Aragorn");
	CHECK(help.shortcutKey() == 0); // InGameSimpleHelp keeps no key
	HelpBoxSettings settings;
	settings.name = { "Albertus MT", 16, false, 0xFFFFFFFFu };
	settings.cost = { "Albertus MT", 14, false, 0xFFFFCC00u };
	settings.shortcut = { "Albertus MT", 14, false, 0xFFFFCC00u };
	settings.description = { "Albertus MT", 12, false, 0xFFFFCC00u };
	DefaultFontMetrics metrics; // fontHeight = size + 4
	const int height = help.setWidthAndComputeHeight(400, settings, 1.0f, 1.0f, 1.0f, metrics);
	CHECK(height == 20 + 2 * 16); // the title, then two text lines; no shortcut / cost line, no padding line
	std::vector<HelpDrawOp> ops;
	help.render(100.0f, 50.0f, 400.0f, (float)height, ops);
	REQUIRE(ops.size() == 3);
	CHECK(ops[0].text == "Aragorn");
	CHECK(ops[0].font.pointSize == 16);
	CHECK(ops[1].text == "Level: 3 / Maximum Level: 10");
	CHECK(ops[2].text == "Maximum Health: 2400");
	CHECK(ops[1].font.pointSize == 12);
	CHECK(ops[0].y == 50.0f);
	CHECK(ops[1].y == 70.0f);
	CHECK(ops[2].y == 86.0f);
	// centred: floor(left + (width - text width) * 0.5 + 0.5)
	CHECK(ops[0].x == (float)(long)(100.0f + (400.0f - ops[0].w) * 0.5f + 0.5f));
	// an empty text adds nothing
	CommandButtonHelp only = CommandButtonHelp::simple(u"Gandalf", u"");
	CHECK(only.setWidthAndComputeHeight(400, settings, 1.0f, 1.0f, 1.0f, metrics) == 20);
}

TEST_CASE("localized text: command button shortcut templates use typed text")
{
	MapText source;
	CommandButtonHelp::Text text;
	text.name = u"&Hero";
	struct Case { const char16_t *pattern; const char *expected; };
	for (const Case &c : { Case{ u"Shortcut: %s", "Shortcut: H" }, Case{ u"%s", "H" }, Case{ u"%n", "%n" }, Case{ u"%x", "%x" },
		Case{ u"%p", "%p" }, Case{ u"%999999d", "%999999d" }, Case{ u"%", "%" }, Case{ u"%ls/%S/%d", "H//" }, Case{ u"plain", "plain" } })
	{
		source.t["TOOLTIP:Shortcut"] = c.pattern;
		CommandButtonHelp help(text, &source, nullptr, nullptr);
		HelpBoxSettings settings;
		settings.name = { "Test", 16, false, 0xFFFFFFFFu };
		settings.shortcut = { "Test", 14, false, 0xFFFFCC00u };
		DefaultFontMetrics metrics;
		const int height = help.setWidthAndComputeHeight(400, settings, 1.0f, 1.0f, 1.0f, metrics);
		std::vector<HelpDrawOp> ops;
		help.render(0.0f, 0.0f, 400.0f, (float)height, ops);
		bool found = false;
		for (const HelpDrawOp &op : ops)
		{
			found |= op.text == c.expected;
		}
		CHECK(found);
	}
}

TEST_CASE("hover1 format: the retail labels' arguments go in order (TOOLTIP:Rank's two %d, TOOLTIP:VerteranHordeMemberName's two %s)")
{
	CHECK(UnitHelpSource::format(u"Level: %d / Maximum Level: %d", { u"3", u"10" }) == u"Level: 3 / Maximum Level: 10");
	CHECK(UnitHelpSource::format(u"%s of %s", { u"Rohirrim", u"Eored" }) == u"Rohirrim of Eored");
	CHECK(UnitHelpSource::format(u"100%% %d", { u"7" }) == u"100% 7");
}

TEST_CASE("hover1 root prerequisites: an owned science passes, else one prerequisite group of passing sciences (RW 0x5FF4D7 / 0x5FF323)")
{
	struct Owner : ScienceOwner
	{
		std::set<ScienceType> owned;
		bool hasScience(ScienceType st) const override { return owned.count(st) != 0; }
		int getSciencePurchasePoints() const override { return 0; }
	};
	if (!haveWorld("hover1 root prerequisites"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto ctx = s.world->enterContext();
	REQUIRE(TheScienceStore);
	// a science of the install with a prerequisite group: owning the group passes, owning nothing fails
	ScienceType withPrereq = SCIENCE_INVALID, need = SCIENCE_INVALID;
	for (const ScienceInfo *si : TheScienceStore->sciences())
	{
		if (!si->m_prereqSciences.empty() && !si->m_prereqSciences.front().empty())
		{
			withPrereq = si->m_science;
			need = si->m_prereqSciences.front().front();
			break;
		}
	}
	REQUIRE(withPrereq != SCIENCE_INVALID);
	// nothing owned: the science's first group holds roots only (sciences without prerequisites), so the walk passes through them (RW 0x5FF323: a
	// science with no group passes)
	Owner none;
	bool rootsOnly = true;
	for (ScienceType st : TheScienceStore->findScienceInfo(withPrereq)->m_prereqSciences.front())
	{
		rootsOnly = rootsOnly && TheScienceStore->findScienceInfo(st) && TheScienceStore->findScienceInfo(st)->m_prereqSciences.empty();
	}
	CHECK(TheScienceStore->playerHasRootPrereqsForScience(none, withPrereq) == rootsOnly);
	Owner owns;
	owns.owned.insert(withPrereq);
	CHECK(TheScienceStore->playerHasRootPrereqsForScience(owns, withPrereq));
	Owner group;
	for (ScienceType st : TheScienceStore->findScienceInfo(withPrereq)->m_prereqSciences.front())
	{
		group.owned.insert(st);
	}
	CHECK(TheScienceStore->playerHasRootPrereqsForScience(group, withPrereq));
	(void)need;
}

namespace
{
// a retail game with the HUD, the string file and the mapped images (the help box's icons)
struct HoverRig
{
	Rig rig;
	RecordingShellServices services;
	GameTextTableSource text;
	MappedImageCollection images;
	INIEnvironment imageEnv;
	std::unique_ptr<InGameHud> hud;
	HoverRig(SharedWorld &s, const std::string &faction) : rig(s, "map mp fall back 4p", faction.c_str())
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(text.table.loadRetail(*s.mount->fs, &error), error); // data/lotr.str, or a non-English install's lotr.csf
		imageEnv.fileSystem = s.mount->fs.get();
		images.registerBlocks(imageEnv.blocks);
		INI ini(imageEnv);
		ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, &text };
		hud = std::make_unique<InGameHud>(cfg);
		hud->setMappedImages(&images);
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
	}
	~HoverRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	void select(Object *o)
	{
		hud->input().ui().deselectAll(true);
		hud->input().ui().selectObject(o->getID());
	}
	std::u16string label(const std::string &l) const { return fetchOrMissing(&text, l); }
	// the help the box shows for the pointer at (x, y): away first (the box hides), then on the point until the box shows (TOOLTIPDELAY 1 ms, the
	// 5-update hide); null when it never shows
	const CommandButtonHelp *hoverAt(int x, int y)
	{
		hud->mouseMove(2, 2, 0);
		for (int i = 0; i < 12 && hud->helpBox().state() != 0; ++i)
		{
			hud->update(0.033);
		}
		hud->mouseMove(x, y, 0);
		for (int i = 0; i < 12; ++i)
		{
			hud->update(0.033);
			std::vector<HelpDrawOp> ops; // the device's render of the content clip (it gives the box its sampled width, state 1)
			hud->helpBoxRender(400.0f, 500.0f, 360.0f, 120.0f, ops);
			if (hud->helpBox().isShown() && hud->helpBox().help())
			{
				return hud->helpBox().help();
			}
		}
		return nullptr;
	}
	// every Apt button under the pointer in a 4-pixel grid of the window: its target path -> the first point that hits it
	std::map<std::string, ICoord2D> aptButtons()
	{
		std::map<std::string, ICoord2D> out;
		AptStageMapping m;
		m.windowW = 1024.0f;
		m.windowH = 768.0f;
		for (int y = 2; y < 768; y += 4)
		{
			for (int x = 2; x < 1024; x += 4)
			{
				float sx, sy;
				m.windowToStage((float)x, (float)y, sx, sy);
				if (AptButtonInst *b = hud->apt().input().hitTestButtons(sx, sy))
				{
					out.emplace(b->targetPath(), ICoord2D{ x, y });
				}
			}
		}
		return out;
	}
};

std::u16string stripAmp(const std::u16string &s)
{
	std::u16string out;
	for (char16_t c : s)
	{
		if (c != u'&')
		{
			out += c;
		}
	}
	return out;
}
std::u16string num(long long v)
{
	const std::string s = std::to_string(v);
	return std::u16string(s.begin(), s.end());
}
// the test's own printf for the string file's formats (each %d / %s / %i / %u takes the next argument, %% is a percent sign); independent of the
// engine's formatters
std::u16string fmtN(const std::u16string &f, const std::vector<std::u16string> &args)
{
	std::u16string out;
	size_t next = 0;
	for (size_t i = 0; i < f.size(); ++i)
	{
		if (f[i] == u'%' && i + 1 < f.size())
		{
			const char16_t c = f[++i];
			if (c == u'%')
			{
				out += u'%';
			}
			else if (c == u'd' || c == u's' || c == u'i' || c == u'u')
			{
				out += next < args.size() ? args[next] : std::u16string();
				++next;
			}
			else
			{
				out += u'%';
				out += c;
			}
			continue;
		}
		out += f[i];
	}
	return out;
}
std::u16string fmt1(const std::u16string &f, const std::u16string &v)
{
	return fmtN(f, { v });
}

// what one hovered button showed against the string file (the oracle: the button's labels in data/lotr.str and the logic's own cost functions)
struct Tally
{
	std::map<std::string, int> descriptionKinds; ///< the named retail rule each description matched
	std::map<std::string, int> commands;          ///< hovered buttons by Command
	std::vector<std::string> hovered;             ///< "<object>/<button>"
	std::vector<std::string> noHelp;              ///< the box never showed
};

// the TextLabel / DescriptLabel the original shows for a button on an object, evaluated here from the CommandButton's own data and the object's state
// (not the control bar's output): slot 0, the second slot while a toggle-image button's object has one of its FlagsUsedForToggle weapon conditions, a
// stance toggle's slot of the object's current stance in the button's Stances list (RW InGameToggleStanceCommandButton, StanceToButtonSlot)
size_t labelSlot(const CommandButton &b, Object *obj)
{
	size_t slot = 0;
	if (!obj)
	{
		return slot;
	}
	if (b.hasOption(COMMAND_OPTION_TOGGLE_IMAGE_ON_WEAPONSET) && b.m_buttonImageName.size() > 1)
	{
		const WeaponConditionFlags &f = obj->getWeaponSetFlags();
		for (size_t w = 0; w < f.size() && w < b.m_flagsUsedForToggle.size(); ++w)
		{
			if ((f[w] & b.m_flagsUsedForToggle[w]) != 0)
			{
				slot = 1;
			}
		}
	}
	if (b.m_command == GUI_COMMAND_TOGGLE_STANCE)
	{
		const StancesBehavior *sb = StancesBehavior::of(*obj);
		for (size_t i = 0; sb && i < b.m_stances.size(); ++i)
		{
			if (b.m_stances[i] == (int)sb->getStance())
			{
				slot = i;
				break;
			}
		}
	}
	return slot;
}
std::string pickLabel(const std::vector<std::string> &labels, size_t slot)
{
	return labels.empty() ? std::string() : labels[slot < labels.size() ? slot : 0];
}

void checkHelp(HoverRig &h, const CommandButtonHelp &help, const ControlBarButton &cb, Object *selection, Tally &tally, const std::string &where)
{
	const CommandButton &b = *cb.button;
	INFO(where << " " << b.m_name << " (" << GUICommandName(b.m_command) << ")");
	tally.commands[GUICommandName(b.m_command)]++;
	Player *player = selection ? selection->getControllingPlayer() : h.rig.local;
	// the name: the TextLabel's text without its '&' markers, the key the first alphanumeric after one
	const size_t slot = labelSlot(b, selection);
	const std::string textLabel = pickLabel(b.m_textLabel, slot);
	const std::string descriptLabel = pickLabel(b.m_descriptLabel, slot);
	const std::u16string name = textLabel.empty() ? std::u16string() : h.label(textLabel);
	CHECK(utf8(help.name()) == utf8(stripAmp(name)));
	CHECK(help.name().find(u"MISSING:") == std::u16string::npos);
	// the cost line
	std::u16string cost;
	const ThingTemplate *tt = b.getThingTemplate();
	const UpgradeTemplate *up = TheUpgradeCenter && !b.m_upgradeName.empty() ? TheUpgradeCenter->findUpgrade(b.m_upgradeName) : nullptr;
	const bool upgradeCommand = b.m_command == GUI_COMMAND_PLAYER_UPGRADE || b.m_command == GUI_COMMAND_OBJECT_UPGRADE || b.m_command == GUI_COMMAND_CASTLE_UPGRADE;
	if (tt && (!up || !upgradeCommand) && b.m_command != GUI_COMMAND_PURCHASE_SCIENCE && b.m_command != GUI_COMMAND_HORDE_SET_FORMATION)
	{
		const int c = BuildAssistant::calcCostToBuild(*tt, player, selection, -1);
		const int freeBit = ObjectTemplateInfoBuilder::kindOfIndex("BUILD_FOR_FREE");
		const bool isFree = MaskTest(h.rig.logic().templateInfo(tt->getFinalOverride()).kindOf, (unsigned)freeBit);
		cost = (!isFree && c > 0) ? fmt1(h.label("TOOLTIP:Cost"), num(c)) : h.label("TOOLTIP:CostFree");
		const int cp = BuildAssistant::commandPoints(*tt);
		std::string points; // TOOLTIP:CommandPoints only for a template with CommandPoints
		if (cp > 0)
		{
			points = utf8(fmt1(h.label("TOOLTIP:CommandPoints"), num(cp)));
		}
		CHECK(utf8(help.description()) == points);
	}
	else if (up)
	{
		const int c = up->calcCostToBuild(player, selection);
		cost = c > 0 ? fmt1(h.label("TOOLTIP:Cost"), num(c)) : std::u16string();
	}
	else if (b.m_science.size() == 1 && TheScienceStore)
	{
		const ScienceType st = TheScienceStore->getScienceFromInternalName(b.m_science.front());
		cost = fmt1(h.label("TOOLTIP:ScienceCost"), num(TheScienceStore->getSciencePurchaseCost(st, true)));
	}
	else if (b.m_command == GUI_COMMAND_SPECIAL_POWER && TheSpecialPowerStore)
	{
		const SpecialPowerTemplate *sp = TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName);
		if (sp && sp->getFinalOverride()->m_unitCost != 0)
		{
			cost = fmt1(h.label("TOOLTIP:UnitCost"), num(sp->getFinalOverride()->m_unitCost));
		}
	}
	// the description this button has in this state, by the original's rules (RW 0x807A81) evaluated here from the data and the logic's state: the
	// DescriptLabel, BuildDisabled after it for a template the local player may not build, an upgrade's already-upgraded / conflicting /
	// lacks-a-prerequisite replacement, its queue-full / not-enough-money warning
	const std::u16string base = descriptLabel.empty() ? std::u16string() : h.label(descriptLabel);
	std::string kind = "the DescriptLabel";
	std::u16string description = base;
	bool upgradeOwned = false;
	if (tt && (!up || !upgradeCommand) && b.m_command != GUI_COMMAND_PURCHASE_SCIENCE && b.m_command != GUI_COMMAND_HORDE_SET_FORMATION)
	{
		if (!BuildAssistant::playerAllowedToBuild(*h.rig.local, tt, h.rig.logic()))
		{
			kind = "BuildDisabled appended";
			description = base + (base.empty() ? u"" : u"\n") + h.label("TOOLTIP:BuildDisabled");
		}
	}
	else if (up)
	{
		const bool playerHas = player && player->hasUpgradeComplete(up);
		if (!base.empty() && selection && player && !playerHas && upgradeCommand)
		{
			ProductionUpdateInterface *pu = selection->getProductionUpdate();
			if (pu && pu->getProductionCount() == 20)
			{
				kind = "queue full";
				description = base + u"\n\n" + h.label("TOOLTIP:TooltipCannotPurchaseBecauseQueueFull");
			}
			else if (!UpgradeCenter::canAffordUpgrade(h.rig.local, up, selection))
			{
				kind = "not enough money";
				description = base + u"\n\n" + h.label("TOOLTIP:TooltipNotEnoughMoneyToBuild");
			}
		}
		bool has = playerHas, lacks = false, conflict = false;
		if (!has && selection)
		{
			bool needed = !b.m_neededUpgradeAny;
			for (const std::string &n : b.m_neededUpgrade)
			{
				const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(n);
				const bool r = u && (u->getUpgradeType() == UPGRADE_TYPE_OBJECT ? selection->hasUpgrade(u) : selection->getControllingPlayer()->hasUpgradeComplete(u));
				needed = b.m_neededUpgradeAny ? (needed || r) : (needed && r);
			}
			if ((b.m_options & COMMAND_OPTION_NEED_UPGRADE) && !selection->hasUpgrade(up) && !needed)
			{
				lacks = true;
			}
			else
			{
				has = selection->hasUpgrade(up);
				conflict = !has && b.m_command == GUI_COMMAND_OBJECT_UPGRADE && !selection->affectedByUpgrade(up);
			}
		}
		if (lacks)
		{
			kind = "lacks a prerequisite";
			description = b.m_lacksPrerequisiteLabel.empty() ? h.label("TOOLTIP:LacksPrerequisiteUpgradeDefault") : h.label(b.m_lacksPrerequisiteLabel);
		}
		else if (conflict)
		{
			kind = "conflicting upgrade";
			description = b.m_conflictingLabel.empty() ? h.label("TOOLTIP:HasConflictingUpgradeDefault") : h.label(b.m_conflictingLabel);
		}
		else if (has)
		{
			kind = "already upgraded";
			description = b.m_purchasedLabel.empty() ? h.label("TOOLTIP:AlreadyUpgradedDefault") : h.label(b.m_purchasedLabel);
			upgradeOwned = true;
		}
	}
	CHECK(utf8(help.title()) == (upgradeOwned ? std::string() : utf8(cost))); // RW 0x808386: an owned upgrade shows no cost
	CHECK(utf8(help.longText()) == utf8(description));
	tally.descriptionKinds[kind]++;
	CHECK(help.longText().find(u"MISSING:") == std::u16string::npos);
	tally.hovered.push_back(where + "/" + b.m_name);
}

// the playable factions of the install (PlayerTemplate PlayableSide, not an observer) with a builder and a fortress, copied: a loaded game rebuilds the
// player templates (MAPRESET-1), so no pointer into the store outlives the next rig
struct Faction
{
	std::string name, builder, fortress, spellBookMp;
	std::vector<std::string> heroes;
};
std::vector<Faction> playableFactions(SharedWorld &s)
{
	std::vector<Faction> out;
	const PlayerTemplateStore &store = s.world->playerTemplates();
	for (int i : store.playableSideIndices())
	{
		const PlayerTemplate *pt = store.getNthPlayerTemplate(i);
		if (pt && !pt->m_startingUnit[0].empty() && !pt->m_startingBuilding.empty())
		{
			out.push_back({ pt->getName(), pt->m_startingUnit[0], pt->m_startingBuilding, pt->m_spellBookMp, pt->m_buildableHeroesMP });
		}
	}
	return out;
}

// the buttons the data gives an object (the inventory rule, independent of the control bar): its template's CommandSet, the slots below the set's
// InitialVisible (RW CommandSet + 0x98; the first range), a button there whose ShowButton is set and that has a place; "<template>/<button>" in slot order
std::vector<std::string> dataButtons(SharedWorld &s, const std::string &templateName)
{
	std::vector<std::string> out;
	const ThingTemplate *tt = s.world->things().findTemplate(templateName);
	REQUIRE_MESSAGE(tt, templateName);
	const CommandSet *cs = TheCommandStore ? TheCommandStore->findCommandSet(CommandButtonHelp::templateLabel(*tt->getFinalOverride(), "CommandSet")) : nullptr;
	if (!cs)
	{
		return out;
	}
	const int n = std::min(cs->m_initialVisible, (int)CommandSet::MAX_BUTTONS);
	for (int i = 0; i < n; ++i)
	{
		const CommandButton *b = cs->getCommandButton(i);
		// on the screen: InPalantir in one of the arc's windows (RW 0x92FF5C, CommandButton + 0x102) or Radial (the side bar / the ring, RW 0x92F082,
		// + 0x101); anything else is a hotkey-only command with no place to hover
		const bool placed = b && ((b->m_inPalantir && i < 6 /* RW 0x930035: windows 0 .. 5 */) || b->m_radial);
		if (b && b->m_showButton && placed)
		{
			out.push_back(templateName + "/" + b->m_name);
		}
	}
	return out;
}

// the structures a faction's builder builds (its CommandSet's DOZER_CONSTRUCT buttons, in slot order) and its fortress
std::vector<std::string> mainStructures(SharedWorld &s, const Faction &pt)
{
	std::vector<std::string> out{ pt.fortress };
	const ThingTemplate *builder = s.world->things().findTemplate(pt.builder);
	REQUIRE(builder);
	const std::string set = CommandButtonHelp::templateLabel(*builder->getFinalOverride(), "CommandSet");
	const CommandSet *cs = TheCommandStore ? TheCommandStore->findCommandSet(set) : nullptr;
	REQUIRE_MESSAGE(cs, pt.name << ": builder command set " << set);
	for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = cs->getCommandButton(i);
		if (b && b->m_command == GUI_COMMAND_DOZER_CONSTRUCT && b->getThingTemplate() &&
		    std::find(out.begin(), out.end(), b->getThingTemplate()->getName()) == out.end())
		{
			out.push_back(b->getThingTemplate()->getName());
		}
	}
	return out;
}
} // namespace

TEST_CASE("hover1 retail: every command button of every faction's builder, main structures and heroes shows its string file text in the help box")
{
	if (!haveWorld("hover1 command buttons"))
	{
		return;
	}
	SharedWorld &s = shared();
	const std::vector<Faction> factions = playableFactions(s);
	MESSAGE("factions: " << factions.size());
	REQUIRE(factions.size() == 7);
	size_t total = 0;
	for (const Faction &f : factions)
	{
		INFO(f.name);
		HoverRig h(s, f.name);
		auto ctx = h.hud->enterContext();
		h.frames(30);
		REQUIRE(h.hud->palantir());
		REQUIRE(h.hud->helpBoxSettings().loaded);
		Tally tally;
		std::vector<std::string> expected; // "<object>/<button>" of every visible button
		const Coord3D spot = h.rig.freeSpot(2400, 1400, 260.0f);
		auto palantirButtons = [&](Object *o, const std::vector<ControlBarButton> &list, bool arc) {
			const std::map<std::string, ICoord2D> points = h.aptButtons();
			for (const ControlBarButton &cb : list)
			{
				if (!cb.button || cb.state == ButtonState::Hidden)
				{
					continue;
				}
				const std::string where = o->getTemplate()->getName();
				expected.push_back(where + "/" + cb.button->m_name);
				const ICoord2D *at = nullptr;
				for (const auto &pp : points)
				{
					bool inArc = false;
					int slot = -1;
					if (h.hud->palantir()->contentSlotAt(pp.first, inArc, slot) && inArc == arc && slot == cb.slot)
					{
						at = &pp.second;
						break;
					}
				}
				if (!at)
				{
					tally.noHelp.push_back(where + "/" + cb.button->m_name + " (no Apt button under any pointer position)");
					continue;
				}
				const CommandButtonHelp *help = h.hoverAt(at->x, at->y);
				if (!help)
				{
					tally.noHelp.push_back(where + "/" + cb.button->m_name);
					continue;
				}
				checkHelp(h, *help, cb, o, tally, where);
			}
		};
		// the builder: its buttons go to the side bar's frames
		{
			Object *builder = h.rig.make(f.builder, spot.x, spot.y);
			h.rig.lookAt(spot);
			h.select(builder);
			h.frames(30);
			palantirButtons(builder, h.hud->controlBar().sideButtons(), false);
			palantirButtons(builder, h.hud->controlBar().palantirButtons(), true);
			h.rig.logic().destroyObject(builder);
			h.frames(5);
		}
		// the main structures: their buttons ring over them (the radial bubbles)
		for (const std::string &name : mainStructures(s, f))
		{
			Object *o = h.rig.make(name, spot.x, spot.y);
			h.rig.lookAt(spot);
			h.select(o);
			h.frames(30);
			const ControlBarRadialMenu &radial = h.hud->radialMenu();
			palantirButtons(o, h.hud->controlBar().palantirButtons(), true); // the arc's InPalantir buttons (Sell)
			const std::vector<ControlBarRadialMenu::Button> ring = radial.buttons();
			for (const ControlBarRadialMenu::Button &rb : ring)
			{
				if (!rb.source.button)
				{
					continue;
				}
				expected.push_back(name + "/" + rb.source.button->m_name);
				const CommandButtonHelp *help = h.hoverAt(rb.rect.x + rb.rect.w / 2, rb.rect.y + rb.rect.h / 2);
				if (!help)
				{
					tally.noHelp.push_back(name + "/" + rb.source.button->m_name);
					continue;
				}
				checkHelp(h, *help, rb.source, o, tally, name);
			}
			h.rig.logic().destroyObject(o);
			h.frames(5);
		}
		// the heroes the faction recruits (BuildableHeroesMP): their buttons go to the Palantir's arc
		for (const std::string &name : f.heroes)
		{
			Object *o = h.rig.make(name, spot.x, spot.y);
			h.rig.lookAt(spot);
			h.select(o);
			h.frames(30);
			palantirButtons(o, h.hud->controlBar().palantirButtons(), true);
			h.rig.logic().destroyObject(o);
			h.frames(5);
		}
		std::string kinds, commands;
		for (const auto &k : tally.descriptionKinds)
		{
			kinds += k.first + " " + std::to_string(k.second) + ", ";
		}
		for (const auto &c : tally.commands)
		{
			commands += c.first + " " + std::to_string(c.second) + ", ";
		}
		std::set<std::string> data; // the data's inventory of this faction (dataButtons)
		{
			for (const std::string &n : dataButtons(s, f.builder))
			{
				data.insert(n);
			}
			for (const std::string &st : mainStructures(s, f))
			{
				for (const std::string &n : dataButtons(s, st))
				{
					data.insert(n);
				}
			}
			for (const std::string &hn : f.heroes)
			{
				for (const std::string &n : dataButtons(s, hn))
				{
					data.insert(n);
				}
			}
			const std::set<std::string> ui(expected.begin(), expected.end());
			for (const std::string &n : data)
			{
				if (!ui.count(n))
				{
					const CommandButton *b = TheCommandStore->findCommandButton(n.substr(n.find('/') + 1));
					FAIL_CHECK("the data's button " << n << " (" << std::string(b ? GUICommandName(b->m_command) : "?") << ") was not on the screen");
				}
			}
			for (const std::string &n : ui)
			{
				if (!data.count(n))
				{
					FAIL_CHECK("the screen showed " << n << ", which the data does not give the object");
				}
			}
		}
		MESSAGE(f.name << ": " << expected.size() << " buttons, " << tally.hovered.size() << " hovered; descriptions: " << kinds << "commands: " << commands);
		for (const std::string &n : tally.noHelp)
		{
			FAIL_CHECK("no help box over " << n);
		}
		CHECK(tally.hovered.size() == expected.size());
		// the inventory's exact size from the data, and the description each rule gave, pinned (measured 2026-10-14 on RotWK 2.01's data; a fresh skirmish:
		// the upgrades with NEED_UPGRADE lack their NeededUpgrade, nothing is owned, the starting money buys every upgrade)
		// (the distinct buttons of the data, the places they show at: an InPalantir and Radial button is hovered in the arc and in the ring or side bar)
		struct Pin
		{
			size_t buttons, places;
			int lacks;
		};
		const std::map<std::string, Pin> pinned = { { "FactionMen", { 90, 117, 8 } }, { "FactionElves", { 73, 93, 4 } }, { "FactionDwarves", { 60, 88, 7 } },
			{ "FactionIsengard", { 72, 98, 6 } }, { "FactionMordor", { 66, 89, 8 } }, { "FactionWild", { 60, 91, 6 } }, { "FactionAngmar", { 67, 92, 16 } } };
		REQUIRE(pinned.count(f.name) == 1);
		const std::map<std::string, std::pair<size_t, int>> pins = { { f.name, { pinned.at(f.name).places, pinned.at(f.name).lacks } } };
		CHECK(data.size() == pinned.at(f.name).buttons);
		CHECK(tally.hovered.size() == pinned.at(f.name).places);
		CHECK((std::set<std::string>(tally.hovered.begin(), tally.hovered.end()) == data));
		const std::map<std::string, int> pinnedKinds = { { "lacks a prerequisite", pins.at(f.name).second },
			{ "the DescriptLabel", (int)pins.at(f.name).first - pins.at(f.name).second } };
		CHECK((tally.descriptionKinds == pinnedKinds));
		total += tally.hovered.size();
	}
	MESSAGE("hovered buttons, all factions: " << total);
	CHECK(total == 668); // places; 488 distinct (object, button) pairs
}

TEST_CASE("hover1 retail: every spell book power of every faction shows its string file text in the help box (RW 0x930DC4)")
{
	if (!haveWorld("hover1 spell book"))
	{
		return;
	}
	SharedWorld &s = shared();
	const std::vector<Faction> factions = playableFactions(s);
	REQUIRE(factions.size() == 7);
	size_t total = 0;
	for (const Faction &f : factions)
	{
		INFO(f.name);
		HoverRig h(s, f.name);
		auto ctx = h.hud->enterContext();
		// the spell book wants a finished COMMANDCENTER (test_spell_book_hud.cpp's setup): the faction's fortress
		const ThingTemplate *fortress = s.world->things().findTemplate(f.fortress);
		REQUIRE(fortress);
		Object *c = h.rig.logic().newObject(fortress, h.rig.local->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(c);
		Coord3D at{ 600.0f, 600.0f, 0.0f };
		c->setPosition(&at);
		testutil::completeHorde(c); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		c->friend_onBuildComplete();
		h.frames(40);
		REQUIRE(h.hud->palantir());
		REQUIRE(h.hud->palantir()->spellBookShown());
		// every power owned: its RequiredSciences granted
		for (const InGameSpellBookModel::Button &b : h.hud->spellBar().buttons())
		{
			REQUIRE(b.power);
			for (ScienceType st : b.power->getRequiredSciences())
			{
				h.rig.local->science().grantScience(st);
			}
		}
		h.frames(40);
		const std::vector<InGameSpellBookModel::Button> powers = h.hud->spellBar().buttons();
		const std::map<std::string, ICoord2D> points = h.aptButtons();
		const std::string book = h.hud->palantir()->spellBookTarget();
		Tally tally;
		for (const InGameSpellBookModel::Button &power : powers)
		{
			int slot = -1;
			for (int i = 0; i < AptPalantir::kSpellSlots; ++i)
			{
				slot = h.hud->palantir()->spellSlotButton(i) == power.index ? i : slot;
			}
			if (slot < 0)
			{
				tally.noHelp.push_back(power.button->m_name + " (in no spell book slot)");
				continue;
			}
			const std::string clip = book + ".Spell" + std::to_string(slot + 1);
			const ICoord2D *p = nullptr;
			for (const auto &pp : points)
			{
				if (pp.first.compare(0, clip.size() + 1, clip + ".") == 0)
				{
					p = &pp.second;
					break;
				}
			}
			if (!p)
			{
				tally.noHelp.push_back(power.button->m_name + " (no Apt button under " + clip + ")");
				continue;
			}
			const CommandButtonHelp *help = h.hoverAt(p->x, p->y);
			if (!help)
			{
				tally.noHelp.push_back(power.button->m_name);
				continue;
			}
			ControlBarButton cb;
			cb.button = power.button;
			cb.textLabel = power.button->m_textLabel.empty() ? std::string() : power.button->m_textLabel.front();
			cb.descriptLabel = power.button->m_descriptLabel.empty() ? std::string() : power.button->m_descriptLabel.front();
			checkHelp(h, *help, cb, nullptr, tally, "SpellBook");
			CHECK(h.hud->helpBox().provider().bar == 3);
		}
		for (const std::string &n : tally.noHelp)
		{
			FAIL_CHECK("no help box over the spell book's " << n);
		}
		MESSAGE(f.name << ": " << powers.size() << " spell book powers, " << tally.hovered.size() << " hovered");
		// the data's spell book: every button of the PlayerTemplate SpellBookMp object's CommandSet that names a SpecialPower, by name (12 each in RotWK 2.01)
		std::set<std::string> data;
		{
			const ThingTemplate *book = s.world->things().findTemplate(f.spellBookMp);
			REQUIRE_MESSAGE(book, f.spellBookMp);
			const CommandSet *cs = TheCommandStore->findCommandSet(CommandButtonHelp::templateLabel(*book->getFinalOverride(), "CommandSet"));
			REQUIRE(cs);
			for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *b = cs->getCommandButton(i);
				if (b && !b->m_specialPowerName.empty())
				{
					data.insert("SpellBook/" + b->m_name);
				}
			}
		}
		CHECK(data.size() == 12);
		CHECK((std::set<std::string>(tally.hovered.begin(), tally.hovered.end()) == data));
		CHECK(tally.hovered.size() == powers.size());
		total += tally.hovered.size();
	}
	MESSAGE("hovered spell book powers, all factions: " << total);
	CHECK(total == 84);
}

TEST_CASE("hover1 retail: the hero bar's portraits show the unit help (RW 0x8EBC61), its builder and select-all buttons their NonCommand help (RW 0x92BF34 / 0x92C265)")
{
	if (!haveWorld("hover1 hero bar"))
	{
		return;
	}
	SharedWorld &s = shared();
	const std::vector<Faction> factions = playableFactions(s);
	REQUIRE(factions.size() == 7);
	for (const Faction &f : factions)
	{
		INFO(f.name);
		HoverRig h(s, f.name);
		auto ctx = h.hud->enterContext();
		h.frames(30);
		InGameHeroSelect *bar = h.hud->heroSelect();
		REQUIRE(bar);
		const Coord3D spot = h.rig.freeSpot(2400, 1400, 260.0f);
		std::vector<Object *> heroes;
		for (const std::string &name : f.heroes)
		{
			heroes.push_back(h.rig.make(name, spot.x, spot.y));
		}
		Object *builder = h.rig.make(f.builder, spot.x + 60.0f, spot.y);
		(void)builder;
		h.rig.lookAt(spot);
		h.frames(60);
		REQUIRE(bar->attached());
		const std::map<std::string, ICoord2D> points = h.aptButtons();
		auto pointUnder = [&points](const std::string &clip) -> const ICoord2D * {
			for (const auto &pp : points)
			{
				if (pp.first == clip || pp.first.compare(0, clip.size() + 1, clip + ".") == 0)
				{
					return &pp.second;
				}
			}
			return nullptr;
		};
		int portraits = 0, builders = 0;
		for (int i = 0; i < InGameHeroSelect::kSlots; ++i)
		{
			const InGameHeroSelect::Slot &slot = bar->slot(i);
			if (slot.hero == INVALID_ID && !slot.builder)
			{
				continue;
			}
			const ICoord2D *p = pointUnder(bar->commandPrefix() + ".Hero" + std::to_string(i + 1));
			INFO("Hero" << (i + 1));
			REQUIRE(p);
			const CommandButtonHelp *help = h.hoverAt(p->x, p->y);
			REQUIRE(help);
			if (slot.builder)
			{
				++builders;
				const CommandButton *nb = TheCommandStore->findCommandButton("NonCommand_SelectNearestBuilder");
				REQUIRE(nb);
				CHECK(h.hud->helpBox().provider().button == nb);
				CHECK(utf8(help->name()) == utf8(stripAmp(h.label(nb->m_textLabel.front()))));
				CHECK(utf8(help->longText()) == utf8(nb->m_descriptLabel.empty() ? std::u16string() : h.label(nb->m_descriptLabel.front())));
				continue;
			}
			++portraits;
			const Object *hero = h.rig.logic().findObjectByID(slot.hero);
			REQUIRE(hero);
			CHECK(help->isSimple());
			CHECK(h.hud->helpBox().provider().bar == 4);
			const ThingTemplate *tt = hero->getTemplate()->getFinalOverride();
			// the title: DisplayName's text
			CHECK(utf8(help->name()) == utf8(stripAmp(h.label(CommandButtonHelp::templateLabel(*tt, "DisplayName")))));
			// the lines: the logic's own numbers in the string file's formats (the rank line needs a maximum rank above 1)
			std::u16string lines;
			auto add = [&lines](const std::u16string &l) {
				if (!lines.empty())
				{
					lines += u'\n';
				}
				lines += l;
			};
			const ExperienceTracker *xp = hero->getExperienceTracker();
			REQUIRE(xp);
			const ExperienceLevelTemplate *level = TheExperienceLevelSystem->findLevel(xp->getLevelName());
			const int cahBit = ObjectTemplateInfoBuilder::kindOfIndex("CREATE_A_HERO");
			const bool createAHero = MaskTest(h.rig.logic().templateInfo(tt).kindOf, (unsigned)cahBit); // RW 0x8EBE3B: rank 1, no health / damage lines
			int rank = createAHero ? 1 : (level ? level->m_rank : 0), top = 0;
			if (level)
			{
				const ExperienceLevelTemplate *last = level;
				while (const ExperienceLevelTemplate *n = TheExperienceLevelSystem->nextLevel(xp->levelTargetName(), last->m_name, true))
				{
					last = n;
				}
				top = last->m_rank;
			}
			if (rank > 0 && top > 1)
			{
				add(fmtN(h.label("TOOLTIP:Rank"), { num(rank), num(top) }));
			}
			if (!createAHero)
			{
				add(fmt1(h.label("TOOLTIP:MaxHealth"), num((int)hero->getBodyModule()->getMaxHealth())));
			}
			const FieldValue *melee = tt->findField("DisplayMeleeDamage");
			const FieldValue *ranged = tt->findField("DisplayRangedDamage");
			const long long m = melee && std::get_if<long long>(melee) ? *std::get_if<long long>(melee) : -1; // the template constructor's -1
			const long long r = ranged && std::get_if<long long>(ranged) ? *std::get_if<long long>(ranged) : -1;
			float sum = 0.0f, product = 1.0f;
			const bool haveSum = hero->attributeModifierSum(2, nullptr, sum), haveProduct = hero->attributeModifierProduct(3, nullptr, true, product);
			MESSAGE(tt->getName() << ": melee " << m << " ranged " << r << ", modifiers " << haveSum << " " << sum << " / " << haveProduct << " " << product);
			if (!haveSum && !haveProduct) // a fresh hero: the display damages as written
			{
				if (m >= 0 && !createAHero)
				{
					add(fmt1(h.label("TOOLTIP:MeleeDamage"), num(m)));
				}
				if (r >= 0 && !createAHero)
				{
					add(fmt1(h.label("TOOLTIP:RangedDamage"), num(r)));
				}
				for (unsigned bit = 0; bit < UpgradeMaskType::BITS; ++bit) // the upgrades the hero has, by their Tooltip label
				{
					const UpgradeTemplate *u = hero->getUpgradeMask().test(bit) ? TheUpgradeCenter->findUpgradeByMaskBit((int)bit) : nullptr;
					if (u && !u->m_tooltip.empty())
					{
						add(h.label(u->m_tooltip));
					}
				}
				const std::string description = CommandButtonHelp::templateLabel(*tt, "Description");
				if (!description.empty())
				{
					add(h.label(description));
				}
				CHECK(utf8(help->longText()) == utf8(lines));
			}
			CHECK(help->longText().find(u"MISSING:") == std::u16string::npos);
			CHECK(help->name().find(u"MISSING:") == std::u16string::npos);
		}
		MESSAGE(f.name << ": " << portraits << " portraits, " << builders << " builder slots hovered");
		CHECK(portraits == (int)heroes.size());
		CHECK(builders == 1);
		// the select-all button (shown with more than one hero)
		if (bar->selectAllShown())
		{
			const ICoord2D *p = pointUnder(bar->commandPrefix() + ".SelectAllHeroesBttn");
			REQUIRE(p);
			const CommandButtonHelp *help = h.hoverAt(p->x, p->y);
			REQUIRE(help);
			const CommandButton *sa = TheCommandStore->findCommandButton("NonCommand_SelectAllHeroes");
			REQUIRE(sa);
			CHECK(utf8(help->name()) == utf8(stripAmp(h.label(sa->m_textLabel.front()))));
		}
	}
}

namespace
{
// a ModifierList's DAMAGE_ADD sum and DAMAGE_MULT product as data\ini\attributemodifier.ini writes them ("DAMAGE_MULT 150%", "DAMAGE_ADD <macro>"), read
// here from the file and the INI macro table, not through the engine's modifier store
void modifierValues(SharedWorld &s, const std::string &list, float &add, float &mult)
{
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(s.mount->fs->readFile("data/ini/attributemodifier.ini", bytes, &error), error);
	std::istringstream in(std::string(bytes.begin(), bytes.end()));
	std::string line;
	bool inList = false;
	while (std::getline(in, line))
	{
		const size_t c = std::min(line.find(';'), line.find("//"));
		std::istringstream t(c == std::string::npos ? line : line.substr(0, c));
		std::vector<std::string> tok;
		for (std::string w; t >> w;)
		{
			tok.push_back(w);
		}
		if (tok.empty())
		{
			continue;
		}
		if (tok[0] == "ModifierList")
		{
			inList = tok.size() > 1 && tok[1] == list;
			continue;
		}
		if (!inList || tok[0] != "Modifier" || tok.size() < 4)
		{
			continue;
		}
		std::string v = tok[3];
		if (const INIMacroTable::Entry *m = s.world->iniEnvironment().macros.find(v.c_str()))
		{
			v = m->value;
		}
		const bool percent = !v.empty() && v.back() == '%';
		const float value = (float)std::atof(v.c_str()) / (percent ? 100.0f : 1.0f);
		if (tok[2] == "DAMAGE_ADD")
		{
			add += value;
		}
		else if (tok[2] == "DAMAGE_MULT")
		{
			mult *= value;
		}
	}
}
} // namespace

TEST_CASE("hover1 retail: a portrait's damage lines with the hero's damage modifiers, computed from the data (RW 0x8EBA26), and the missing-label marker")
{
	if (!haveWorld("hover1 portrait damage"))
	{
		return;
	}
	SharedWorld &s = shared();
	HoverRig h(s, "FactionMen");
	auto ctx = h.hud->enterContext();
	h.frames(30);
	// no RotWK template sets DisplayMeleeDamage / DisplayRangedDamage (the rows exist, every object keeps the -1 of RW 0x73FF33): a map.ini-style override
	// (load type 2, as a mod or a map would) gives a hero both
	{
		const std::string text = "Object RohanTheoden\n  DisplayMeleeDamage = 40\n  DisplayRangedDamage = 25\nEnd\n";
		INI ini(s.world->iniEnvironment());
		ini.loadMemory("hover1-damage.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_CREATE_OVERRIDES);
	}
	const ThingTemplate *theoden = s.world->things().findTemplate("RohanTheoden");
	REQUIRE(theoden);
	REQUIRE(theoden->getFinalOverride()->findField("DisplayMeleeDamage"));
	const Coord3D spot = h.rig.freeSpot(2400, 1400, 260.0f);
	Object *hero = h.rig.make("RohanTheoden", spot.x, spot.y);
	h.rig.lookAt(spot);
	h.frames(40);
	auto portraitHelp = [&]() -> const CommandButtonHelp * {
		const std::map<std::string, ICoord2D> points = h.aptButtons();
		InGameHeroSelect *bar = h.hud->heroSelect();
		for (int i = 0; i < InGameHeroSelect::kSlots; ++i)
		{
			if (bar->slot(i).hero != hero->getID())
			{
				continue;
			}
			const std::string clip = bar->commandPrefix() + ".Hero" + std::to_string(i + 1);
			for (const auto &pp : points)
			{
				if (pp.first.compare(0, clip.size() + 1, clip + ".") == 0)
				{
					return h.hoverAt(pp.second.x, pp.second.y);
				}
			}
		}
		return nullptr;
	};
	auto damageLines = [&](int melee, int ranged) {
		return utf8(fmt1(h.label("TOOLTIP:MeleeDamage"), num(melee)) + u"\n" + fmt1(h.label("TOOLTIP:RangedDamage"), num(ranged)));
	};
	const CommandButtonHelp *plain = portraitHelp();
	REQUIRE(plain);
	CHECK(utf8(plain->longText()).find(damageLines(40, 25)) != std::string::npos); // tolerance: the two damage lines inside the help's lines (rank and health above)
	// the modifiers: GenericBuff (DAMAGE_MULT) and HeroLevelUpDamage1 (DAMAGE_ADD through its macro)
	float add = 0.0f, mult = 1.0f;
	modifierValues(s, "GenericBuff", add, mult);
	modifierValues(s, "HeroLevelUpDamage1", add, mult);
	CHECK(mult == 1.5f);
	REQUIRE(hero->addAttributeModifier("GenericBuff", -1));
	REQUIRE(hero->addAttributeModifier("HeroLevelUpDamage1", -1));
	h.frames(10);
	const int addInt = (int)std::floor((double)(add + 0.5f));
	const int melee = (int)std::floor((double)((float)(40 + addInt) * mult + 0.5f));
	const int ranged = (int)std::floor((double)((float)(25 + addInt) * mult + 0.5f));
	MESSAGE("modifiers: add " << add << ", mult " << mult << " -> melee " << melee << ", ranged " << ranged);
	const CommandButtonHelp *modified = portraitHelp();
	REQUIRE(modified);
	CHECK(utf8(modified->longText()).find(damageLines(melee, ranged)) != std::string::npos); // tolerance: the two damage lines inside the help's lines
	CHECK(melee == 75); // (40 + 10) * 1.5
	CHECK(ranged == 53); // (25 + 10) * 1.5 = 52.5, rounded half up

	// the Palantir's portrait (RW 0x93099D): the selected own hero shows the same unit help; an enemy's object shows none (RW 0x6A8803: not an ally)
	auto portraitPoint = [&](ICoord2D &at) {
		const std::string clip = "_level" + std::to_string(h.hud->palantir()->level()) + ".CommandUI.PortraitBackground";
		for (const auto &pp : h.aptButtons())
		{
			if (pp.first == clip || pp.first.compare(0, clip.size() + 1, clip + ".") == 0)
			{
				at = pp.second;
				return true;
			}
		}
		return false;
	};
	h.select(hero);
	h.frames(20);
	ICoord2D at{};
	REQUIRE(portraitPoint(at));
	const CommandButtonHelp *portrait = h.hoverAt(at.x, at.y);
	REQUIRE(portrait);
	CHECK(portrait->isSimple());
	CHECK(h.hud->helpBox().provider().bar == 4);
	CHECK(utf8(portrait->name()) == utf8(stripAmp(h.label(CommandButtonHelp::templateLabel(*theoden->getFinalOverride(), "DisplayName")))));
	CHECK(utf8(portrait->longText()).find(damageLines(melee, ranged)) != std::string::npos); // tolerance: the damage lines inside the portrait help's lines
	Player *enemy = h.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	Object *theirs = h.rig.make("MordorGothmog", spot.x + 120.0f, spot.y, enemy);
	h.select(theirs);
	h.frames(20);
	REQUIRE(portraitPoint(at));
	CHECK(h.hoverAt(at.x, at.y) == nullptr);

	// a label the string file lacks shows retail's marker, never another text (GameText::fetch: "MISSING: '<label>'")
	MapText empty;
	CommandButton made;
	made.m_name = "Command_Hover1Missing";
	made.m_command = GUI_COMMAND_STOP;
	ControlBarButton cb;
	cb.button = &made;
	cb.textLabel = "CONTROLBAR:Hover1NoSuchLabel";
	cb.descriptLabel = "CONTROLBAR:Hover1NoSuchTooltip";
	const CommandButtonHelp::Text t = CommandButtonHelp::compose(cb, &empty, h.rig.logic(), h.rig.local, h.rig.local, nullptr, "Men");
	CHECK(utf8(t.name) == "MISSING: 'CONTROLBAR:Hover1NoSuchLabel'");
	CHECK(utf8(t.description) == "MISSING: 'CONTROLBAR:Hover1NoSuchTooltip'");
}

TEST_CASE("hover1 retail: controlled help states: recruit / revive, castle unpack, a several-science button, a disabled power, and each upgrade state (RW 0x807A81)")
{
	if (!haveWorld("hover1 controlled help states"))
	{
		return;
	}
	SharedWorld &s = shared();
	HoverRig h(s, "FactionMen");
	auto ctx = h.hud->enterContext();
	h.frames(30);
	GameLogic &logic = h.rig.logic();
	Player *me = h.rig.local;
	auto buttonOf = [](const CommandButton *b) {
		ControlBarButton cb;
		cb.button = b;
		cb.textLabel = b->m_textLabel.empty() ? std::string() : b->m_textLabel.front();
		cb.descriptLabel = b->m_descriptLabel.empty() ? std::string() : b->m_descriptLabel.front();
		return cb;
	};
	auto compose = [&](const ControlBarButton &cb, Object *selection) {
		return CommandButtonHelp::compose(cb, &h.text, logic, selection ? selection->getControllingPlayer() : me, me, selection, "Men");
	};
	auto labelText = [&](const ThingTemplate &tt, const char *field) {
		const std::string l = CommandButtonHelp::templateLabel(tt, field);
		return l.empty() ? std::u16string() : h.label(l);
	};
	Coord3D spot = h.rig.freeSpot(2400, 1400, 260.0f);

	SUBCASE("the HUD reports stop S-4612 exactly (InGameHud::stops)")
	{
		const std::string expected =
			"[S-4612] hover help (HOVER-1): ported the hero bar's help (RW 0x92BF34: the builder slot and the select-all button show their NonCommand button's "
			"help, a hero its UnitHelpSource RW 0x8EBC61 as an InGameSimpleHelp RW 0x9402E7: name, level, maximum health, damages with the modifiers, upgrades, "
			"description), the Palantir portrait's (RW 0x93099D: an own or allied object's unit help), the spell book's (RW 0x930DC4: the slot's command button), "
			"RW 0x807A81's revive / castle unpack / unit cost / science-disabled / multi-science / upgrade conflict and prerequisite branches (the repair costs "
			"and the revive Create-a-Hero name: lane ROTWK-C); not ported: the drawable's script captions, Object + 0x494 names (the veteran horde member name), the disguise "
			"label, the Create-a-Hero names and template, the battles fought (Object + 0x488, S-852); also not ported, named by the inventory of every tool tip "
			"source (tools/hover/hover_inventory.py, sources.csv): the in-game unit tips (TOOLTIP:AddSpecialAbilityNoPower, ClickToRenameThisUnit, DisbandUnit, "
			"DisbandUnitDisabled, DisbandUnitDisabledHero, DisbandUnitDisabledLeader, SupplyWarehouse, YouMayNotRenameAHero) and the War of the Ring HUD's 23 "
			"labels (STRATEGICHUD:*Tooltip, APT:StrategicBattlePromptSwapButton1Tooltip / 2Tooltip, BackButtonTooltip), "
			"and the control bar's 22 other NonCommand_* readout and menu buttons (Resources, ResourceMultiplier, CommandPoints, "
			"CommandPointsLivingWorld, Options, MultiplayerOptions, IdleWorker, Beacon, Communicator, Messenger, BriefingHistory, Objectives, "
			"PlayerStatus, UpDown, EnemyRank, PlanningMode, ObserveNextPlayer, ObservePriorPlayer, GoodPlayerExperience, EvilPlayerExperience, "
			"MaxEvenstarPower, MaxRingPower: readouts.csv); INFERENCE: a hovered hero bar / spell book "
			"button is the Apt player's current button below \"<clip>.Hero<n>\" / \"<clip>.Spell<n>\" (retail's over-button keys \"<clip>/Hero%d/\" / "
			"\"Palantir/<clip>/Spell%d/\")";
		const std::vector<std::string> stops = h.hud->stops();
		CHECK(std::count(stops.begin(), stops.end(), expected) == 1);
	}
	SUBCASE("recruit and revive (RW 0x8083EF): the GenericRecruitHero / GenericReviveHero name, the template's text, the rank, the record's cost")
	{
		Object *fortress = h.rig.make(s.world->playerTemplates().findPlayerTemplate("FactionMen")->m_startingBuilding, spot.x, spot.y);
		const CommandButton *revive = TheCommandStore->findCommandButton("Command_GenericReviveSlot1");
		REQUIRE(revive);
		REQUIRE(revive->m_command == GUI_COMMAND_REVIVE);
		// the first record of a hero that is not the Create-a-Hero (its name is S-4612's)
		int index = -1;
		const int cah = ObjectTemplateInfoBuilder::kindOfIndex("CREATE_A_HERO");
		for (int i = 0; i < (int)me->heroes().size() && index < 0; ++i)
		{
			const ThingTemplate *t = me->heroes().templateAt(logic, i);
			index = t && !MaskTest(logic.templateInfo(t->getFinalOverride()).kindOf, (unsigned)cah) ? i : -1;
		}
		REQUIRE(index >= 0); // tolerance: a skirmish lists the faction's heroes; the first non-Create-a-Hero one is taken
		const ThingTemplate &hero = *me->heroes().templateAt(logic, index)->getFinalOverride();
		ControlBarButton cb = buttonOf(revive);
		cb.reviveIndex = index;
		const std::u16string heroName = CommandButtonHelp::templateLabel(hero, "Hotkey").empty() ? labelText(hero, "DisplayName") : labelText(hero, "Hotkey");
		const FieldValue *buildCost = hero.findField("BuildCost");
		const long long cost = buildCost && std::get_if<long long>(buildCost) ? *std::get_if<long long>(buildCost) : 0;
		const int rank = me->heroes().at(index)->rank;
		for (bool dead : { false, true })
		{
			INFO(std::string(dead ? "revive" : "recruit"));
			me->heroes().at(index)->dead = dead; // the record of a dead hero (a RespawnUpdate's, RW 0x780DD4)
			const CommandButtonHelp::Text t = compose(cb, fortress);
			CHECK(utf8(t.name) == utf8(fmt1(h.label(dead ? "CONTROLBAR:GenericReviveHero" : "CONTROLBAR:GenericRecruitHero"), heroName)));
			const std::u16string own = labelText(hero, dead ? "ReviveText" : "RecruitText");
			const std::u16string text = own.empty() ? h.label(dead ? "CONTROLBAR:ToolTipGenericReviveHero" : "CONTROLBAR:ToolTipGenericRecruitHero") : own;
			CHECK(utf8(t.description) == utf8(text + u"\n" + fmt1(h.label("APT:RankLabel"), num(rank))));
			CHECK(utf8(t.cost) == utf8(fmt1(h.label("TOOLTIP:Cost"), num(cost))));
		}
	}
	SUBCASE("castle unpack (RW 0x80863C): a camp flag of the player's shows its side's unpack cost")
	{
		// a flag the player captured (BACKLOG2's setup: a hero beside it)
		Object *flag = h.rig.make("CampFlag", spot.x, spot.y);
		flag->friend_onBuildComplete();
		h.rig.make("GondorBoromir", spot.x + 40.0f, spot.y);
		h.frames(40);
		MESSAGE("flag owner " << (flag->getControllingPlayer() ? flag->getControllingPlayer()->getSide() : std::string("none")));
		REQUIRE(flag->getControllingPlayer() == me);
		const CastleBehavior *castle = dynamic_cast<const CastleBehavior *>(flag->findModule("CastleBehavior"));
		REQUIRE(castle);
		REQUIRE(castle->canUnpack(false));
		const CastleFactionEntry *entry = castle->data()->entryFor(me->getPlayerTemplate()->m_side); // CastleToUnpackForFaction = Men Camp_Men 2000
		REQUIRE(entry);
		const CommandButton *unpack = TheCommandStore->findCommandButton("Command_UnpackCamp");
		REQUIRE(unpack);
		const CommandButtonHelp::Text t = compose(buttonOf(unpack), flag);
		CHECK(utf8(t.cost) == utf8(fmt1(h.label("TOOLTIP:Cost"), num(entry->cost))));
		CHECK(entry->cost == 2000u);
	}
	SUBCASE("several sciences (RW 0x807B29): PURCHASE_SCIENCE prices the first one the player lacks; any other command shows no science cost")
	{
		std::vector<std::string> names;
		for (const ScienceInfo *si : TheScienceStore->sciences())
		{
			if (names.size() < 3 && si->m_prereqSciences.empty())
			{
				names.push_back(TheScienceStore->getInternalNameForScience(si->m_science));
			}
		}
		REQUIRE(names.size() == 3);
		CommandButton made;
		made.m_name = "Command_Hover1Sciences";
		made.m_command = GUI_COMMAND_PURCHASE_SCIENCE;
		made.m_science = names;
		me->science().grantScience(TheScienceStore->getScienceFromInternalName(names[0]));
		const ScienceType second = TheScienceStore->getScienceFromInternalName(names[1]);
		const CommandButtonHelp::Text t = compose(buttonOf(&made), nullptr);
		CHECK(utf8(t.cost) == utf8(fmt1(h.label("TOOLTIP:ScienceCost"), num(TheScienceStore->getSciencePurchaseCost(second, true)))));
		made.m_command = GUI_COMMAND_SPECIAL_POWER; // locked: the first science, no cost
		CHECK(compose(buttonOf(&made), nullptr).cost.empty());
	}
	SUBCASE("a spell book power (RW 0x807CBA): its owned science disabled or hidden shows TOOLTIP:ScienceDisabled, available shows the DescriptLabel")
	{
		const CommandButton *heal = TheCommandStore->findCommandButton("Command_SpellBookHeal");
		REQUIRE(heal);
		const SpecialPowerTemplate *sp = TheSpecialPowerStore->findSpecialPowerTemplate(heal->m_specialPowerName);
		REQUIRE(sp);
		REQUIRE(!sp->getRequiredSciences().empty());
		const ScienceType st = sp->getRequiredSciences().front();
		me->science().grantScience(st);
		CHECK(utf8(compose(buttonOf(heal), nullptr).description) == utf8(h.label(heal->m_descriptLabel.front())));
		me->setScienceAvailability(st, 1);
		CHECK(utf8(compose(buttonOf(heal), nullptr).description) == utf8(h.label("TOOLTIP:ScienceDisabled")));
		me->setScienceAvailability(st, 2);
		CHECK(utf8(compose(buttonOf(heal), nullptr).description) == utf8(h.label("TOOLTIP:ScienceDisabled")));
		me->setScienceAvailability(st, 0);
		CHECK(utf8(compose(buttonOf(heal), nullptr).description) == utf8(h.label(heal->m_descriptLabel.front())));
	}
	SUBCASE("upgrades (RW 0x808144): owned, lacking a NeededUpgrade, conflicting, not enough money")
	{
		// the buttons, from the faction's main structures' command sets (the data): a PLAYER_UPGRADE with a DescriptLabel, a NEED_UPGRADE upgrade
		// button, an OBJECT_UPGRADE without NEED_UPGRADE
		const Faction men = playableFactions(s).front();
		const CommandButton *playerUp = nullptr, *needs = nullptr, *objectUp = nullptr;
		std::string playerUpOwner, needsOwner, objectUpOwner;
		for (const std::string &st : mainStructures(s, men))
		{
			const ThingTemplate *tt = s.world->things().findTemplate(st);
			const CommandSet *cs = TheCommandStore->findCommandSet(CommandButtonHelp::templateLabel(*tt->getFinalOverride(), "CommandSet"));
			for (int i = 0; cs && i < CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *b = cs->getCommandButton(i);
				if (!b || b->m_upgradeName.empty())
				{
					continue;
				}
				const bool need = (b->m_options & COMMAND_OPTION_NEED_UPGRADE) != 0;
				if (!playerUp && b->m_command == GUI_COMMAND_PLAYER_UPGRADE && !need && !b->m_descriptLabel.empty())
				{
					playerUp = b;
					playerUpOwner = st;
				}
				if (!needs && need && (b->m_command == GUI_COMMAND_PLAYER_UPGRADE || b->m_command == GUI_COMMAND_OBJECT_UPGRADE))
				{
					needs = b;
					needsOwner = st;
				}
				if (!objectUp && b->m_command == GUI_COMMAND_OBJECT_UPGRADE && !need)
				{
					objectUp = b;
					objectUpOwner = st;
				}
			}
		}
		REQUIRE(playerUp);
		REQUIRE(needs);
		REQUIRE(objectUp);
		MESSAGE("upgrade buttons: " << playerUp->m_name << " (" << playerUpOwner << "), " << needs->m_name << " (" << needsOwner << "), " << objectUp->m_name
		                            << " (" << objectUpOwner << ")");
		const std::u16string playerUpBase = h.label(playerUp->m_descriptLabel.front());
		const UpgradeTemplate *pu = TheUpgradeCenter->findUpgrade(playerUp->m_upgradeName);
		Object *owner = h.rig.make(playerUpOwner, spot.x, spot.y);
		// not enough money: the DescriptLabel, "\n\n", TooltipNotEnoughMoneyToBuild
		me->getMoney()->withdraw(me->getMoney()->countMoney(), nullptr, false);
		CHECK(utf8(compose(buttonOf(playerUp), owner).description) == utf8(playerUpBase + u"\n\n" + h.label("TOOLTIP:TooltipNotEnoughMoneyToBuild")));
		// owned: the purchased label (or the default), no cost line
		me->addUpgrade(pu, Player::UPGRADE_STATUS_COMPLETE, true);
		const CommandButtonHelp::Text owned = compose(buttonOf(playerUp), owner);
		CHECK(utf8(owned.description) ==
		      utf8(playerUp->m_purchasedLabel.empty() ? h.label("TOOLTIP:AlreadyUpgradedDefault") : h.label(playerUp->m_purchasedLabel)));
		CHECK(owned.cost.empty());
		// lacks a prerequisite: NEED_UPGRADE with its NeededUpgrade missing
		Object *needsObj = h.rig.make(needsOwner, spot.x + 400.0f, spot.y);
		CHECK(utf8(compose(buttonOf(needs), needsObj).description) ==
		      utf8(needs->m_lacksPrerequisiteLabel.empty() ? h.label("TOOLTIP:LacksPrerequisiteUpgradeDefault") : h.label(needs->m_lacksPrerequisiteLabel)));
		// conflicting: an OBJECT_UPGRADE shown for an object no module of which the upgrade affects (a builder)
		Object *builder = h.rig.make(men.builder, spot.x - 300.0f, spot.y);
		const UpgradeTemplate *ou = TheUpgradeCenter->findUpgrade(objectUp->m_upgradeName);
		REQUIRE_FALSE(builder->affectedByUpgrade(ou));
		CHECK(utf8(compose(buttonOf(objectUp), builder).description) ==
		      utf8(objectUp->m_conflictingLabel.empty() ? h.label("TOOLTIP:HasConflictingUpgradeDefault") : h.label(objectUp->m_conflictingLabel)));
		// the queue-full warning is pinned by test_hud6.cpp's "hud6 retail: the help's upgrade warnings ..."
	}
}
