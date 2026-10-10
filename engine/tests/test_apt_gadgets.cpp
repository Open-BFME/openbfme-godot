// OpenBFME tests: the native gadget layer (spec menus-apt.md build step A5): GameWindow, the WND v2 loader, the ListBox, ComboBox,
// TextEntry, CheckBox and HorzSlider gadgets, the draw command contract and the component factory that creates a gadget per
// GameWindowGadgets placeholder.  Synthetic tests use WND text written here (the ZH script grammar); the retail tests load the shipped
// window\apt\*.wnd skins and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.  GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "AptRetail.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindowManagerScript.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"

#include <algorithm>
#include <map>
#include <set>

using namespace apttest;

namespace
{

// ---- WND text ---------------------------------------------------------------------------------------------------------------------------

std::string noImages(const char *key)
{
	std::string s = std::string("  ") + key + " = ";
	for (int i = 0; i < MAX_DRAW_DATA; ++i)
	{
		s += std::string(i ? "                    " : "") + "IMAGE: NoImage, COLOR: 255 255 255 0, BORDERCOLOR: 255 255 255 0" + (i + 1 < MAX_DRAW_DATA ? ",\n" : ";\n");
	}
	return s;
}

std::string withImages(const char *key, const std::vector<std::string> &images, const char *color = "255 255 255 255", const char *border = "0 0 0 255")
{
	std::string s = std::string("  ") + key + " = ";
	for (int i = 0; i < MAX_DRAW_DATA; ++i)
	{
		const std::string img = i < (int)images.size() ? images[(std::size_t)i] : "NoImage";
		s += std::string(i ? "                    " : "") + "IMAGE: " + img + ", COLOR: " + color + ", BORDERCOLOR: " + border + (i + 1 < MAX_DRAW_DATA ? ",\n" : ";\n");
	}
	return s;
}

std::string wnd(const std::string &type, const std::string &status, const std::string &style, int w, int h, const std::string &extra, const std::string &header = "AptGadgets")
{
	return "FILE_VERSION = 2;\nSTARTLAYOUTBLOCK\n  LAYOUTINIT = [None];\n  LAYOUTUPDATE = [None];\n  LAYOUTSHUTDOWN = [None];\nENDLAYOUTBLOCK\nWINDOW\n  WINDOWTYPE = " + type
		+ ";\n  SCREENRECT = UPPERLEFT: 0 0,\n               BOTTOMRIGHT: " + std::to_string(w) + " " + std::to_string(h) + ",\n               CREATIONRESOLUTION: 1024 768;\n  NAME = \"T.wnd:\";\n  STATUS = "
		+ status + ";\n  STYLE = " + style + ";\n  SYSTEMCALLBACK = \"Apt:None\";\n  INPUTCALLBACK = \"Apt:None\";\n  TOOLTIPCALLBACK = \"Apt:None\";\n  DRAWCALLBACK = \"Apt:None\";\n  FONT = NAME: \"Albertus MT\", SIZE: 10, BOLD: 0;\n"
		+ (header.empty() ? std::string() : "  HEADERTEMPLATE = \"" + header + "\";\n")
		+ "  TEXTCOLOR = ENABLED:  255 255 255 255, ENABLEDBORDER:  0 0 0 255,\n              DISABLED: 192 192 192 255, DISABLEDBORDER: 64 64 64 255,\n              HILITE:   92 148 47 255, HILITEBORDER:   0 0 0 255;\n"
		+ extra + "END\n";
}

std::string checkboxWnd()
{
	return wnd("CHECKBOX", "ENABLED+IMAGE+BORDER", "CHECKBOX+MOUSETRACK", 34, 34,
		"  TEXT = \"APT:Null\";\n" + withImages("ENABLEDDRAWDATA", { "NoImage", "BoxUncheckedEnabled", "BoxCheckedEnabled" }) + withImages("DISABLEDDRAWDATA", { "NoImage", "BoxUncheckedDisabled", "BoxCheckedDisabled" })
			+ withImages("HILITEDRAWDATA", { "NoImage", "BoxUncheckedHilite", "BoxCheckedHilite" }));
}

std::string listboxWnd(int w, int h, const std::string &listData = "LENGTH: 999,\n                AUTOSCROLL: 0,\n                SCROLLIFATEND: 1,\n                AUTOPURGE: 1,\n                SCROLLBAR: 1,\n                MULTISELECT: 0,\n                COLUMNS: 1,\n                COLUMNSWIDTH: 100,\n                FORCESELECT: 1")
{
	return wnd("SCROLLLISTBOX", "ENABLED", "SCROLLLISTBOX+MOUSETRACK", w, h,
		"  LISTBOXDATA = " + listData + ";\n" + withImages("ENABLEDDRAWDATA", { "ListBg", "SelLeft", "SelRight", "SelCenter", "SelSmall" }) + withImages("DISABLEDDRAWDATA", { "ListBg" }) + withImages("HILITEDRAWDATA", { "ListBg", "SelLeft", "SelRight", "SelCenter", "SelSmall" })
			+ withImages("LISTBOXENABLEDUPBUTTONDRAWDATA", { "UpEnabled" }) + withImages("LISTBOXENABLEDDOWNBUTTONDRAWDATA", { "DownEnabled" }) + noImages("LISTBOXENABLEDSLIDERDRAWDATA")
			+ noImages("LISTBOXDISABLEDUPBUTTONDRAWDATA") + noImages("LISTBOXDISABLEDDOWNBUTTONDRAWDATA") + noImages("LISTBOXDISABLEDSLIDERDRAWDATA") + noImages("LISTBOXHILITEUPBUTTONDRAWDATA")
			+ noImages("LISTBOXHILITEDOWNBUTTONDRAWDATA") + noImages("LISTBOXHILITESLIDERDRAWDATA") + noImages("SLIDERTHUMBENABLEDDRAWDATA") + noImages("SLIDERTHUMBDISABLEDDRAWDATA") + noImages("SLIDERTHUMBHILITEDRAWDATA"));
}

std::string comboWnd(int w, int h, bool editable = false)
{
	return wnd("COMBOBOX", "ENABLED+IMAGE", "COMBOBOX", w, h,
		std::string("  COMBOBOXDATA = ISEDITABLE: ") + (editable ? "1" : "0") + ",\n                MAXCHARS: 16,\n                MAXDISPLAY: 3,\n                ASCIIONLY: 0,\n                LETTERSANDNUMBERS: 0;\n"
			+ withImages("ENABLEDDRAWDATA", { "ComboBg" }) + noImages("DISABLEDDRAWDATA") + noImages("HILITEDRAWDATA")
			+ withImages("COMBOBOXDROPDOWNBUTTONENABLEDDRAWDATA", { "DropEnabled", "DropSelected" }) + noImages("COMBOBOXDROPDOWNBUTTONDISABLEDDRAWDATA") + noImages("COMBOBOXDROPDOWNBUTTONHILITEDRAWDATA")
			+ noImages("COMBOBOXEDITBOXENABLEDDRAWDATA") + noImages("COMBOBOXEDITBOXDISABLEDDRAWDATA") + noImages("COMBOBOXEDITBOXHILITEDRAWDATA")
			+ withImages("COMBOBOXLISTBOXENABLEDDRAWDATA", { "BlackSquare", "SelLeft", "SelRight", "SelCenter", "SelSmall" }) + noImages("COMBOBOXLISTBOXDISABLEDDRAWDATA") + noImages("COMBOBOXLISTBOXHILITEDRAWDATA")
			+ withImages("LISTBOXENABLEDUPBUTTONDRAWDATA", { "UpEnabled" }) + noImages("LISTBOXDISABLEDUPBUTTONDRAWDATA") + noImages("LISTBOXHILITEUPBUTTONDRAWDATA")
			+ withImages("LISTBOXENABLEDDOWNBUTTONDRAWDATA", { "DownEnabled" }) + noImages("LISTBOXDISABLEDDOWNBUTTONDRAWDATA") + noImages("LISTBOXHILITEDOWNBUTTONDRAWDATA")
			+ noImages("LISTBOXENABLEDSLIDERDRAWDATA") + noImages("LISTBOXDISABLEDSLIDERDRAWDATA") + noImages("LISTBOXHILITESLIDERDRAWDATA")
			+ noImages("SLIDERTHUMBENABLEDDRAWDATA") + noImages("SLIDERTHUMBDISABLEDDRAWDATA") + noImages("SLIDERTHUMBHILITEDRAWDATA"));
}

std::string entryWnd(int w, int h, const std::string &entryData = "MAXLEN: 8,\n                  SECRETTEXT: 0,\n                  NUMERICALONLY: 0,\n                  ALPHANUMERICALONLY: 0,\n                  ASCIIONLY: 0")
{
	return wnd("ENTRYFIELD", "ENABLED+IMAGE", "ENTRYFIELD", w, h, "  TEXT = \"Entry\";\n  TEXTENTRYDATA = " + entryData + ";\n" + withImages("ENABLEDDRAWDATA", { "EntLeft", "EntRight", "EntCenter", "EntSmall" }) + noImages("DISABLEDDRAWDATA") + noImages("HILITEDRAWDATA"));
}

std::string sliderWnd(int w, int h)
{
	return wnd("HORZSLIDER", "ENABLED+IMAGE+TABSTOP", "HORZSLIDER+MOUSETRACK", w, h,
		"  SLIDERDATA = MINVALUE: 0,\n               MAXVALUE: 100;\n" + noImages("ENABLEDDRAWDATA") + withImages("DISABLEDDRAWDATA", { "BarFill", "BarEmpty" }) + withImages("HILITEDRAWDATA", { "BarHilite" })
			+ noImages("SLIDERTHUMBENABLEDDRAWDATA") + noImages("SLIDERTHUMBDISABLEDDRAWDATA") + noImages("SLIDERTHUMBHILITEDRAWDATA"));
}

// ---- fixture ----------------------------------------------------------------------------------------------------------------------------

struct Msg
{
	std::uint32_t msg;
	GameWindow *from;
	WindowMsgData d1, d2;
};

struct GadgetFx
{
	GameWindowManager gwm;
	HeaderTemplateManager headers;
	MappedImageCollection images;
	std::vector<Msg> msgs;
	GameWindow *owner = nullptr;
	std::string error;

	GadgetFx()
	{
		headers.addTemplate({ "AptGadgets", "Albertus MT", 14, false });
		for (const char *name : { "BoxUncheckedEnabled", "BoxCheckedEnabled", "BoxUncheckedDisabled", "BoxCheckedDisabled", "BoxUncheckedHilite", "BoxCheckedHilite", "ListBg", "SelLeft", "SelRight", "SelCenter",
				 "SelSmall", "UpEnabled", "DownEnabled", "ComboBg", "DropEnabled", "DropSelected", "BlackSquare", "EntLeft", "EntRight", "EntCenter", "EntSmall", "BarFill", "BarEmpty", "BarHilite" })
		{
			Image img;
			img.name = name;
			img.imageSize.x = 8;
			img.imageSize.y = 16;
			images.addImage(img);
		}
		gwm.setImages(&images);
		WinInstanceData inst;
		inst.m_style = GWS_USER_WINDOW;
		owner = gwm.winCreate(nullptr, WIN_STATUS_HIDDEN | WIN_STATUS_NO_INPUT, 0, 0, 0, 0, [this](GameWindow *w, std::uint32_t msg, WindowMsgData d1, WindowMsgData d2) {
			if (msg != GWM_CREATE)
			{
				msgs.push_back({ msg, w, d1, d2 });
			}
			return MSG_HANDLED;
		}, &inst);
	}
	~GadgetFx()
	{
		gwm.winDestroyAll(); // the owner's system function records into msgs: release the windows while it is alive
		gwm.processDestroyList();
	}
	GameWindow *load(const std::string &text, int x, int y)
	{
		WindowScriptLoader loader(gwm, &headers);
		WindowLayoutInfo info;
		GameWindow *w = loader.createFromScript("test.wnd", text, &info, &error);
		REQUIRE_MESSAGE(w, error);
		w->winSetPosition(x, y);
		w->winSetOwner(owner);
		return w;
	}
	void move(int x, int y)
	{
		ICoord2D p{ x, y };
		gwm.winProcessMouseEvent(GWM_MOUSE_POS, &p, nullptr);
	}
	bool click(int x, int y)
	{
		ICoord2D p{ x, y };
		gwm.winProcessMouseEvent(GWM_MOUSE_POS, &p, nullptr);
		const bool down = gwm.winProcessMouseEvent(GWM_LEFT_DOWN, &p, nullptr) == WIN_INPUT_USED;
		const bool up = gwm.winProcessMouseEvent(GWM_LEFT_UP, &p, nullptr) == WIN_INPUT_USED;
		return down && up;
	}
	std::vector<Msg> take()
	{
		std::vector<Msg> out;
		out.swap(msgs);
		return out;
	}
	static std::size_t count(const std::vector<Msg> &v, std::uint32_t msg)
	{
		std::size_t n = 0;
		for (const Msg &m : v)
		{
			if (m.msg == msg)
			{
				++n;
			}
		}
		return n;
	}
};

UnicodeString u(const char *s)
{
	UnicodeString out;
	for (; *s; ++s)
	{
		out.push_back((char16_t)(unsigned char)*s);
	}
	return out;
}

std::string narrow(const UnicodeString &s)
{
	std::string out;
	for (char16_t c : s)
	{
		out.push_back((char)c);
	}
	return out;
}

} // namespace

// ============================================================================================================================
// GameWindow and the manager
// ============================================================================================================================

TEST_CASE("GameWindow: children go to the head of the parent's list, positions are parent-relative, the hit test walks head first")
{
	GameWindowManager gwm;
	GameWindow *parent = gwm.winCreate(nullptr, WIN_STATUS_ENABLED, 100, 50, 200, 200, nullptr);
	GameWindow *a = gwm.winCreate(parent, WIN_STATUS_ENABLED, 10, 10, 50, 50, nullptr);
	GameWindow *b = gwm.winCreate(parent, WIN_STATUS_ENABLED, 30, 30, 50, 50, nullptr); // overlaps a, created later
	CHECK(parent->winGetChild() == b); // ZH addWindowToParent: the newest child is first
	CHECK(b->winGetNext() == a);
	int x, y;
	a->winGetScreenPosition(&x, &y);
	CHECK(x == 110);
	CHECK(y == 60);
	CHECK(parent->winPointInChild(145, 95) == b); // inside both: the head wins
	CHECK(parent->winPointInChild(115, 65) == a);
	CHECK(parent->winPointInChild(500, 500) == parent);
	b->winHide(true);
	CHECK(parent->winPointInChild(145, 95) == a);
	b->winHide(false);
	b->winEnable(false);
	CHECK(parent->winPointInChild(145, 95) == a); // a disabled child is skipped
	CHECK(parent->winPointInAnyChild(145, 95, true, true) == b);
	// enable recurses
	parent->winEnable(false);
	CHECK_FALSE(a->winGetEnabled());
	// the window tree is reached through winGetWindowFromId
	a->winSetWindowId(77);
	CHECK(gwm.winGetWindowFromId(nullptr, 77) == a);
	CHECK(gwm.winGetWindowFromId(nullptr, 78) == nullptr);
}

TEST_CASE("GameWindow: destroy removes the subtree, clears focus and capture, and frees the windows on the destroy list")
{
	GameWindowManager gwm;
	GameWindow *parent = gwm.winCreate(nullptr, WIN_STATUS_ENABLED, 0, 0, 100, 100, nullptr);
	GameWindow *child = gwm.winCreate(parent, WIN_STATUS_ENABLED, 0, 0, 10, 10, nullptr);
	gwm.winCapture(child);
	CHECK(gwm.winGetCapture() == child);
	CHECK(gwm.windowCount() == 2);
	gwm.winDestroy(parent);
	CHECK(gwm.winGetCapture() == nullptr);
	CHECK(gwm.winGetWindowList() == nullptr);
	CHECK(gwm.windowCount() == 2); // freed when the destroy list is processed
	gwm.processDestroyList();
	CHECK(gwm.windowCount() == 0);
}

TEST_CASE("GameWindow: the status and style name tables are the RotWK game.dat tables (28 status names, 16 style names)")
{
	int n = 0;
	for (const char *const *p = WindowStatusNames(); *p; ++p)
	{
		++n;
	}
	CHECK(n == 29); // ACTIVE .. ON_MOUSE_DOWN (26) + CIRCULAR, AVAIL_FLAG, BLOCK_INPUT
	CHECK(std::string(WindowStatusNames()[3]) == "ENABLED");
	CHECK(std::string(WindowStatusNames()[7]) == "IMAGE");
	CHECK(std::string(WindowStatusNames()[28]) == "WIN_STATUS_BLOCK_INPUT");
	int m = 0;
	for (const char *const *p = WindowStyleNames(); *p; ++p)
	{
		++m;
	}
	CHECK(m == 16);
	CHECK(std::string(WindowStyleNames()[15]) == "COMBOBOX");
	CHECK(WIN_STATUS_ENABLED == (1u << 3));
	CHECK(WIN_STATUS_IMAGE == (1u << 7));
	CHECK(GWS_COMBO_BOX == (1u << 15));
}

// ============================================================================================================================
// The WND loader
// ============================================================================================================================

TEST_CASE("WND loader: a check box script gives the status, style, font from the header template, texts and nine draw data slots per state")
{
	GadgetFx fx;
	GameWindow *w = fx.load(checkboxWnd(), 10, 20);
	CHECK(w->winGetStyle() == (GWS_CHECK_BOX | GWS_MOUSE_TRACK));
	CHECK(BitTest(w->winGetStatus(), WIN_STATUS_ENABLED | WIN_STATUS_IMAGE | WIN_STATUS_BORDER));
	CHECK(w->name() == "T.wnd:");
	int sw, sh;
	w->winGetSize(&sw, &sh);
	CHECK(sw == 34);
	CHECK(sh == 34);
	// HEADERTEMPLATE "AptGadgets" (Albertus MT 14) replaces the FONT line (Albertus MT 10)
	REQUIRE(w->winGetFont());
	CHECK(w->winGetFont()->name == "Albertus MT");
	CHECK(w->winGetFont()->pointSize == 14);
	CHECK(w->winGetEnabledImage(1) == "BoxUncheckedEnabled");
	CHECK(w->winGetEnabledImage(2) == "BoxCheckedEnabled");
	CHECK(w->winGetEnabledImage(0).empty()); // NoImage
	CHECK(w->winGetDisabledImage(2) == "BoxCheckedDisabled");
	CHECK(w->winGetHiliteImage(1) == "BoxUncheckedHilite");
	CHECK(w->winGetEnabledColor(0) == GameMakeColor(255, 255, 255, 255));
	CHECK(w->winGetEnabledBorderColor(0) == GameMakeColor(0, 0, 0, 255));
	CHECK(w->winGetEnabledTextColor() == GameMakeColor(255, 255, 255, 255));
	CHECK(w->winGetDisabledTextColor() == GameMakeColor(192, 192, 192, 255));
	CHECK(w->winGetHiliteTextBorderColor() == GameMakeColor(0, 0, 0, 255));
	// the TEXT label is looked up through the text resolver; unresolved without one it is the label itself
	CHECK(narrow(w->winGetText()) == "APT:Null");
}

TEST_CASE("WND loader: the SCREENRECT is scaled by the display over the creation resolution")
{
	GadgetFx fx;
	fx.gwm.setDisplaySize(1024, 768);
	std::string text = checkboxWnd();
	const std::string from = "CREATIONRESOLUTION: 1024 768;";
	text.replace(text.find(from), from.size(), "CREATIONRESOLUTION: 800 600;");
	GameWindow *w = fx.load(text, 0, 0);
	int sw, sh;
	w->winGetSize(&sw, &sh);
	CHECK(sw == (int)(34 * (1024.0f / 800.0f)));
	CHECK(sh == (int)(34 * (768.0f / 600.0f)));
}

TEST_CASE("WND loader: the list box data is positional like ZH, including its COLUMNSWIDTH quirk; ScrollIfAtEnd is optional")
{
	GadgetFx fx;
	// COLUMNS: 1 followed by COLUMNSWIDTH: 100 and FORCESELECT: 0: ZH reads the COLUMNSWIDTH label as the FORCESELECT label and 100 as its
	// value, so force select is on (the real FORCESELECT line is never read)
	GameWindow *a = fx.load(listboxWnd(160, 100, "LENGTH: 50,\n AUTOSCROLL: 1,\n SCROLLIFATEND: 1,\n AUTOPURGE: 0,\n SCROLLBAR: 1,\n MULTISELECT: 0,\n COLUMNS: 1,\n COLUMNSWIDTH: 100,\n FORCESELECT: 0"), 0, 0);
	ListboxData *la = static_cast<ListboxData *>(a->winGetUserData());
	CHECK(la->listLength == 50);
	CHECK(la->autoScroll);
	CHECK(la->scrollIfAtEnd);
	CHECK_FALSE(la->autoPurge);
	CHECK(la->scrollBar);
	CHECK(la->columns == 1);
	CHECK(la->forceSelect); // the quirk
	// without SCROLLIFATEND the label read after AUTOSCROLL is AUTOPURGE
	GameWindow *b = fx.load(listboxWnd(160, 100, "LENGTH: 20,\n AUTOSCROLL: 0,\n AUTOPURGE: 1,\n SCROLLBAR: 0,\n MULTISELECT: 0,\n COLUMNS: 1,\n COLUMNSWIDTH: 100,\n FORCESELECT: 1"), 0, 0);
	ListboxData *lb = static_cast<ListboxData *>(b->winGetUserData());
	CHECK_FALSE(lb->scrollIfAtEnd);
	CHECK(lb->autoPurge);
	CHECK_FALSE(lb->scrollBar);
	CHECK(GadgetListBoxGetSlider(b) == nullptr);
	// two columns: the widths are percentages
	GameWindow *c = fx.load(listboxWnd(200, 100, "LENGTH: 20,\n AUTOSCROLL: 0,\n SCROLLIFATEND: 0,\n AUTOPURGE: 0,\n SCROLLBAR: 0,\n MULTISELECT: 0,\n COLUMNS: 2,\n COLUMNSWIDTH: 10,\n COLUMNSWIDTH: 90,\n FORCESELECT: 0"), 0, 0);
	ListboxData *lc = static_cast<ListboxData *>(c->winGetUserData());
	CHECK(lc->columns == 2);
	REQUIRE(lc->columnWidthPercentage.size() == 2);
	CHECK(lc->columnWidthPercentage[0] == 10);
	CHECK(lc->columnWidthPercentage[1] == 90);
	CHECK(GadgetListBoxGetColumnWidth(c, 0) == 20);
	CHECK(GadgetListBoxGetColumnWidth(c, 1) == 180);
	CHECK_FALSE(lc->forceSelect); // FORCESELECT: 0 is read here (two columns consume the COLUMNSWIDTH labels)
}

TEST_CASE("WND loader: callback names the lexicon does not know give no function (retail's Apt:None); the gadget keeps its own")
{
	GadgetFx fx;
	WindowScriptLoader loader(fx.gwm, &fx.headers);
	WindowLayoutInfo info;
	std::string error;
	GameWindow *w = loader.createFromScript("t", checkboxWnd(), &info, &error);
	REQUIRE_MESSAGE(w, error);
	CHECK(info.version == 2);
	CHECK(info.initName == "[None]");
	CHECK(info.updateName == "[None]");
	CHECK(info.shutdownName == "[None]");
	CHECK(info.unknownCallbacks.size() == 4);
	CHECK(info.unknownCallbacks[0] == "SYSTEMCALLBACK=Apt:None");
	CHECK(info.windows.size() == 1);
	// with a lexicon entry for the input callback the function is installed
	WindowFunctionLexicon lexicon;
	int called = 0;
	lexicon.input["Apt:None"] = [&](GameWindow *, std::uint32_t, WindowMsgData, WindowMsgData) {
		++called;
		return MSG_HANDLED;
	};
	WindowScriptLoader loader2(fx.gwm, &fx.headers, &lexicon);
	WindowLayoutInfo info2;
	GameWindow *w2 = loader2.createFromScript("t", checkboxWnd(), &info2, &error);
	REQUIRE(w2);
	CHECK(info2.unknownCallbacks.size() == 3);
	fx.gwm.winSendInputMsg(w2, GWM_LEFT_DOWN, 0, 0);
	CHECK(called == 1);
}

TEST_CASE("WND loader: decimal numbers that do not fit an int are errors (no signed overflow): FILE_VERSION and the SCREENRECT values")
{
	GadgetFx fx;
	WindowScriptLoader loader(fx.gwm, &fx.headers);
	std::string error;
	const std::size_t before = fx.gwm.allWindows().size();
	auto withVersion = [&](const std::string &version) {
		std::string text = checkboxWnd();
		text.replace(text.find("2;"), 2, version + ";");
		return text;
	};
	// INT_MAX is a number; the file then fails on what follows the version, not on the number
	CHECK(loader.createFromScript("v.wnd", withVersion("2147483647"), nullptr, &error) != nullptr);
	CHECK(error.find("does not fit") == std::string::npos);
	for (const char *big : { "2147483648", "9999999999999999999999", "10000000000" })
	{
		error.clear();
		CHECK(loader.createFromScript("v.wnd", withVersion(big), nullptr, &error) == nullptr);
		CHECK_MESSAGE(error.find("FILE_VERSION does not fit a 32-bit integer") != std::string::npos, big << " -> " << error);
	}
	// a SCREENRECT value (sscanf("%d") in ZH) far beyond the range
	std::string rect = checkboxWnd();
	rect.replace(rect.find("BOTTOMRIGHT: 34 34"), 18, "BOTTOMRIGHT: 99999999999 34");
	error.clear();
	CHECK(loader.createFromScript("r.wnd", rect, nullptr, &error) == nullptr);
	CHECK(error.find("does not fit a 32-bit integer") != std::string::npos);
	// the most negative int parses; the size computed from it does not fit and is an error too (no signed overflow in the subtraction)
	std::string low = checkboxWnd();
	low.replace(low.find("UPPERLEFT: 0 0"), 14, "UPPERLEFT: -2147483648 0");
	error.clear();
	CHECK(loader.createFromScript("l.wnd", low, nullptr, &error) == nullptr);
	CHECK(error.find("outside the 32-bit range after scaling") != std::string::npos);
	// a scaled coordinate that leaves the range
	std::string huge = checkboxWnd();
	huge.replace(huge.find("BOTTOMRIGHT: 34 34"), 18, "BOTTOMRIGHT: 2147483647 34");
	error.clear();
	CHECK(loader.createFromScript("h.wnd", huge, nullptr, &error) == nullptr);
	CHECK_FALSE(error.empty());
	std::string lower = checkboxWnd();
	lower.replace(lower.find("UPPERLEFT: 0 0"), 14, "UPPERLEFT: -2147483649 0");
	CHECK(loader.createFromScript("l2.wnd", lower, nullptr, &error) == nullptr);
	CHECK(error.find("does not fit a 32-bit integer") != std::string::npos);
	// every failed load left the manager as it was (the two accepted loads each created one window)
	CHECK(fx.gwm.allWindows().size() >= before);
}

TEST_CASE("WND loader: a failed load is transactional: a parent whose CHILD fails is destroyed too, earlier windows stay")
{
	GadgetFx fx;
	WindowScriptLoader loader(fx.gwm, &fx.headers);
	std::string error;
	auto childOf = [](const std::string &w) { return w.substr(w.find("WINDOW\n"), w.rfind("END\n") + 4 - w.find("WINDOW\n")); };
	const std::string draws = withImages("ENABLEDDRAWDATA", { "NoImage", "BoxUncheckedEnabled", "BoxCheckedEnabled" }) + withImages("DISABLEDDRAWDATA", { "NoImage", "BoxUncheckedDisabled", "BoxCheckedDisabled" })
		+ withImages("HILITEDRAWDATA", { "NoImage", "BoxUncheckedHilite", "BoxCheckedHilite" });
	// a window that exists before the failing loads
	GameWindow *keep = loader.createFromScript("keep.wnd", checkboxWnd(), nullptr, &error);
	REQUIRE_MESSAGE(keep, error);
	const std::size_t before = fx.gwm.allWindows().size();
	// 1. a check box parent with an unsupported STATICTEXT child
	const std::string badChild = wnd("STATICTEXT", "ENABLED", "STATICTEXT", 10, 10, "");
	CHECK(loader.createFromScript("p1.wnd", wnd("CHECKBOX", "ENABLED+IMAGE+BORDER", "CHECKBOX+MOUSETRACK", 34, 34, "  TEXT = \"APT:Null\";\n" + draws + "CHILD\n" + childOf(badChild) + "ENDALLCHILDREN\n"), nullptr, &error) == nullptr);
	CHECK(error.find("STATICTEXT") != std::string::npos);
	CHECK(fx.gwm.allWindows().size() == before);
	// 2. a user window with one valid child first and the failing one second: both children and the parent go
	const std::string goodChild = checkboxWnd();
	const std::string twoChildren = wnd("USER", "ENABLED", "USER", 50, 50, "CHILD\n" + childOf(goodChild) + childOf(badChild) + "ENDALLCHILDREN\n");
	error.clear();
	CHECK(loader.createFromScript("p2.wnd", twoChildren, nullptr, &error) == nullptr);
	CHECK_FALSE(error.empty());
	CHECK(fx.gwm.allWindows().size() == before);
	// 3. the same through a nested parent (a failure two levels down)
	const std::string nested = wnd("USER", "ENABLED", "USER", 60, 60, "CHILD\n" + childOf(wnd("USER", "ENABLED", "USER", 30, 30, "CHILD\n" + childOf(goodChild) + childOf(badChild) + "ENDALLCHILDREN\n")) + "ENDALLCHILDREN\n");
	error.clear();
	CHECK(loader.createFromScript("p3.wnd", nested, nullptr, &error) == nullptr);
	CHECK(fx.gwm.allWindows().size() == before);
	// nothing of the failed trees is drawn: the draw list holds the surviving window's commands only (as before the failures)
	GadgetDrawList afterFailures;
	fx.gwm.winRepaint(afterFailures);
	CHECK(afterFailures.commands.size() > 0);
	GameWindow *later = loader.createFromScript("later.wnd", checkboxWnd(), nullptr, &error);
	REQUIRE(later);
	CHECK(later->winGetParent() == nullptr);
	CHECK(fx.gwm.allWindows().size() > before);
}

TEST_CASE("WND loader: malformed and unsupported scripts are errors that name the problem, never half-created windows")
{
	GadgetFx fx;
	WindowScriptLoader loader(fx.gwm, &fx.headers);
	std::string error;
	const std::size_t before = fx.gwm.allWindows().size();
	// a window type the loader does not create
	CHECK(loader.createFromScript("a.wnd", wnd("STATICTEXT", "ENABLED", "STATICTEXT", 10, 10, ""), nullptr, &error) == nullptr);
	CHECK(error.find("STATICTEXT") != std::string::npos);
	CHECK(error.find("a.wnd") != std::string::npos);
	// an undefined header template
	CHECK(loader.createFromScript("b.wnd", checkboxWnd(), nullptr, &error) != nullptr);
	std::string bad = wnd("CHECKBOX", "ENABLED", "CHECKBOX", 10, 10, "", "NoSuchTemplate");
	CHECK(loader.createFromScript("c.wnd", bad, nullptr, &error) == nullptr);
	CHECK(error.find("NoSuchTemplate") != std::string::npos);
	// a value that is not terminated before the end of the file (ZH reads on to the next ';': an unterminated line in the middle swallows the
	// following lines, only the end of the file is the error)
	std::string unterminated = wnd("CHECKBOX", "ENABLED", "CHECKBOX", 10, 10, "");
	unterminated = unterminated.substr(0, unterminated.find("STATUS = ENABLED;") + 16);
	CHECK(loader.createFromScript("d.wnd", unterminated, nullptr, &error) == nullptr);
	CHECK(error.find("not terminated") != std::string::npos);
	// a truncated draw data row
	CHECK(loader.createFromScript("e.wnd", wnd("CHECKBOX", "ENABLED", "CHECKBOX", 10, 10, "  ENABLEDDRAWDATA = IMAGE: NoImage, COLOR: 1 2 3;\n"), nullptr, &error) == nullptr);
	CHECK(error.find("ends early") != std::string::npos);
	// a file with no layout block
	std::string noLayout = checkboxWnd();
	noLayout.replace(noLayout.find("STARTLAYOUTBLOCK"), 16, "XTARTLAYOUTBLOCK");
	CHECK(loader.createFromScript("f.wnd", noLayout, nullptr, &error) == nullptr);
	CHECK(error.find("layout block") != std::string::npos);
	// a file that ends inside a window
	CHECK(loader.createFromScript("g.wnd", checkboxWnd().substr(0, checkboxWnd().size() - 4), nullptr, &error) == nullptr);
	CHECK(error.find("no END") != std::string::npos);
	// nothing but the one good window of b.wnd was left behind
	CHECK(fx.gwm.allWindows().size() == before + 1);
}

// ============================================================================================================================
// Gadgets
// ============================================================================================================================

TEST_CASE("CheckBox: a click toggles the box and tells the owner GBM_SELECTED; the space key toggles too; the images follow the state")
{
	GadgetFx fx;
	GameWindow *w = fx.load(checkboxWnd(), 100, 100);
	CHECK_FALSE(GadgetCheckBoxIsChecked(w));
	CHECK(fx.click(110, 110));
	std::vector<Msg> got = fx.take();
	REQUIRE(GadgetFx::count(got, GBM_SELECTED) == 1);
	CHECK(msgPtr<GameWindow>(got.back().d1) == w);
	CHECK(GadgetCheckBoxIsChecked(w));
	// draw: the checked box image, hilited (the pointer is on it)
	GadgetDrawList list;
	fx.gwm.winRepaint(list);
	bool sawChecked = false;
	for (const GadgetDrawCommand &c : list.commands)
	{
		if (c.kind == GadgetDrawCommand::Kind::Image && c.image == "BoxCheckedHilite")
		{
			sawChecked = true;
			CHECK(c.x0 == 100);
			CHECK(c.y0 == 103);
			CHECK(c.x1 == 100 + (34 - 6));
		}
	}
	CHECK(sawChecked);
	// keyboard: focus, then space down toggles
	fx.gwm.winSetFocus(w);
	CHECK(fx.gwm.winProcessKey(KEY_SPACE, KEY_STATE_DOWN) == WIN_INPUT_USED);
	CHECK_FALSE(GadgetCheckBoxIsChecked(w));
	// a click that starts and ends outside does nothing
	fx.take();
	CHECK_FALSE(fx.click(400, 400));
	CHECK(GadgetFx::count(fx.take(), GBM_SELECTED) == 0);
	// disabled windows are skipped by the mouse router
	w->winEnable(false);
	CHECK_FALSE(fx.click(110, 110));
	GadgetCheckBoxSetChecked(w, true); // the API sends the message even when disabled
	CHECK(GadgetCheckBoxIsChecked(w));
}

TEST_CASE("ListBox: entries, selection by click, force select, the wheel, reset and the row arithmetic of ZH")
{
	GadgetFx fx;
	GameWindow *lb = fx.load(listboxWnd(160, 100, "LENGTH: 999,\n AUTOSCROLL: 0,\n SCROLLIFATEND: 0,\n AUTOPURGE: 1,\n SCROLLBAR: 1,\n MULTISELECT: 0,\n COLUMNS: 1,\n COLUMNSWIDTH: 100,\n FORCESELECT: 1"), 100, 100);
	ListboxData *list = static_cast<ListboxData *>(lb->winGetUserData());
	const int rowH = fx.gwm.winFontHeight(lb->winGetFont()) + 1; // font 14 + 4 = 18, +1 separator
	CHECK(GadgetListBoxGetNumEntries(lb) == 0);
	for (int i = 0; i < 8; ++i)
	{
		const std::string label = "Row" + std::to_string(i);
		CHECK(GadgetListBoxAddEntryText(lb, u(label.c_str()), GameMakeColor(255, 255, 255, 255), -1, 0) == i);
	}
	CHECK(GadgetListBoxGetNumEntries(lb) == 8);
	CHECK(list->totalHeight == 8 * rowH);
	CHECK(list->listData[3].listHeight == 4 * rowH); // cumulative
	CHECK(narrow(GadgetListBoxGetText(lb, 2)) == "Row2");
	// a click on row 1 selects it and tells the owner
	fx.take();
	CHECK(fx.click(110, 100 + rowH + 2));
	std::vector<Msg> got = fx.take();
	REQUIRE(GadgetFx::count(got, GLM_SELECTED) == 1);
	CHECK((std::intptr_t)got.back().d2 == 1);
	int sel = -2;
	GadgetListBoxGetSelected(lb, &sel);
	CHECK(sel == 1);
	// force select (the COLUMNSWIDTH quirk makes it true): clicking the selected row again keeps it
	fx.click(110, 100 + rowH + 2);
	GadgetListBoxGetSelected(lb, &sel);
	CHECK(sel == 1);
	// GLM_SET_SELECTION through the API
	GadgetListBoxSetSelected(lb, 4);
	GadgetListBoxGetSelected(lb, &sel);
	CHECK(sel == 4);
	// a selection outside the list clears it
	GadgetListBoxSetSelected(lb, -1);
	GadgetListBoxGetSelected(lb, &sel);
	CHECK(sel == -1);
	// the wheel scrolls the display by one row
	CHECK(list->displayPos == 0);
	ICoord2D mp{ 110, 120 };
	int wheel = -1;
	fx.gwm.winProcessMouseEvent(GWM_WHEEL_DOWN, &mp, &wheel);
	CHECK(list->displayPos > 0);
	wheel = 1;
	fx.gwm.winProcessMouseEvent(GWM_WHEEL_UP, &mp, &wheel);
	CHECK(list->displayPos == 0);
	// item data
	GadgetListBoxSetItemData(lb, reinterpret_cast<void *>(0x1234), 3, 0);
	CHECK(GadgetListBoxGetItemData(lb, 3, 0) == reinterpret_cast<void *>(0x1234));
	CHECK(GadgetListBoxGetItemData(lb, 99, 0) == nullptr);
	// reset
	GadgetListBoxReset(lb);
	CHECK(GadgetListBoxGetNumEntries(lb) == 0);
	CHECK(list->totalHeight == 0);
	GadgetListBoxGetSelected(lb, &sel);
	CHECK(sel == -1);
}

TEST_CASE("ListBox: a full list with auto purge drops the oldest row; without it the add is refused; the keyboard walks the rows")
{
	GadgetFx fx;
	GameWindow *lb = fx.load(listboxWnd(160, 100, "LENGTH: 3,\n AUTOSCROLL: 0,\n SCROLLIFATEND: 0,\n AUTOPURGE: 1,\n SCROLLBAR: 0,\n MULTISELECT: 0,\n COLUMNS: 1,\n COLUMNSWIDTH: 100,\n FORCESELECT: 1"), 0, 0);
	for (const char *t : { "a", "b", "c", "d" })
	{
		GadgetListBoxAddEntryText(lb, u(t), 0, -1, 0);
	}
	CHECK(GadgetListBoxGetNumEntries(lb) == 3);
	CHECK(narrow(GadgetListBoxGetText(lb, 0)) == "b"); // 'a' was purged
	CHECK(narrow(GadgetListBoxGetText(lb, 2)) == "d");
	GameWindow *noPurge = fx.load(listboxWnd(160, 100, "LENGTH: 2,\n AUTOSCROLL: 0,\n SCROLLIFATEND: 0,\n AUTOPURGE: 0,\n SCROLLBAR: 0,\n MULTISELECT: 0,\n COLUMNS: 1,\n COLUMNSWIDTH: 100,\n FORCESELECT: 1"), 300, 0);
	GadgetListBoxAddEntryText(noPurge, u("x"), 0, -1, 0);
	GadgetListBoxAddEntryText(noPurge, u("y"), 0, -1, 0);
	CHECK(GadgetListBoxAddEntryText(noPurge, u("z"), 0, -1, 0) == -1);
	CHECK(GadgetListBoxGetNumEntries(noPurge) == 2);
	// arrow keys with the focus on the list
	fx.gwm.winSetFocus(lb);
	fx.take();
	CHECK(fx.gwm.winProcessKey(KEY_DOWN, KEY_STATE_DOWN) == WIN_INPUT_USED);
	int sel;
	GadgetListBoxGetSelected(lb, &sel);
	CHECK(sel == 0); // none -> first
	fx.gwm.winProcessKey(KEY_DOWN, KEY_STATE_DOWN);
	GadgetListBoxGetSelected(lb, &sel);
	CHECK(sel == 1);
	fx.gwm.winProcessKey(KEY_UP, KEY_STATE_DOWN);
	GadgetListBoxGetSelected(lb, &sel);
	CHECK(sel == 0);
	CHECK(GadgetFx::count(fx.take(), GLM_SELECTED) == 3);
}

TEST_CASE("ListBox: the scroll bar (up, down, slider) exists, takes its skin from the script and follows the list")
{
	GadgetFx fx;
	GameWindow *lb = fx.load(listboxWnd(160, 100, "LENGTH: 999,\n AUTOSCROLL: 0,\n SCROLLIFATEND: 0,\n AUTOPURGE: 1,\n SCROLLBAR: 1,\n MULTISELECT: 0,\n COLUMNS: 1,\n COLUMNSWIDTH: 100,\n FORCESELECT: 1"), 100, 100);
	GameWindow *up = GadgetListBoxGetUpButton(lb);
	GameWindow *down = GadgetListBoxGetDownButton(lb);
	GameWindow *slider = GadgetListBoxGetSlider(lb);
	REQUIRE(up);
	REQUIRE(down);
	REQUIRE(slider);
	CHECK(up->winGetEnabledImage(0) == "UpEnabled");   // LISTBOXENABLEDUPBUTTONDRAWDATA reached the button
	CHECK(down->winGetEnabledImage(0) == "DownEnabled");
	CHECK(up->winGetOwner() == lb);
	for (int i = 0; i < 20; ++i)
	{
		GadgetListBoxAddEntryText(lb, u("row"), 0, -1, 0);
	}
	ListboxData *list = static_cast<ListboxData *>(lb->winGetUserData());
	const int top = list->displayPos;
	// the down arrow button: GBM_SELECTED reaches the list through its owner and scrolls one row
	fx.gwm.winSendSystemMsg(lb, GBM_SELECTED, msgData(down), 0);
	CHECK(list->displayPos > top);
	fx.gwm.winSendSystemMsg(lb, GBM_SELECTED, msgData(up), 0);
	CHECK(list->displayPos == top);
	// the slider's range follows the content
	SliderData *sd = static_cast<SliderData *>(slider->winGetUserData());
	CHECK(sd->maxVal > 0);
}

TEST_CASE("ComboBox: entries, opening the list by a click, picking an entry sends GCM_SELECTED and shows the text")
{
	GadgetFx fx;
	GameWindow *cb = fx.load(comboWnd(154, 27), 200, 300);
	cb->winSetSize(154, 27);
	for (const char *t : { "Open", "Closed", "Easy AI", "Brutal AI" })
	{
		CHECK(GadgetComboBoxAddEntry(cb, u(t), GameMakeColor(255, 255, 255, 255)) >= 0);
	}
	CHECK(GadgetComboBoxGetLength(cb) == 4);
	GameWindow *list = GadgetComboBoxGetListBox(cb);
	GameWindow *edit = GadgetComboBoxGetEditBox(cb);
	REQUIRE(list);
	REQUIRE(edit);
	CHECK(list->winIsHidden());
	CHECK(BitTest(edit->winGetStatus(), WIN_STATUS_NO_INPUT)); // not editable
	int sel = -2;
	GadgetComboBoxGetSelectedPos(cb, &sel);
	CHECK(sel == -1);
	// a click on the box opens the list: the combo grows to hold it
	fx.take();
	fx.click(220, 310); // ZH's combo box does not take the button down (the click reports "not used"), the release opens the list
	CHECK_FALSE(list->winIsHidden());
	int cw, ch;
	cb->winGetSize(&cw, &ch);
	CHECK(ch > 27);
	CHECK(fx.gwm.winGetLoneWindow() == cb);
	// the list shows MAXDISPLAY (3) rows: with 4 entries the scroll controls are visible
	CHECK_FALSE(GadgetListBoxGetSlider(list)->winIsHidden());
	// pick row 1 ("Closed"): a click inside the open list
	int lx, ly;
	list->winGetScreenPosition(&lx, &ly);
	const int rowH = fx.gwm.winFontHeight(list->winGetFont()) + 1;
	fx.take();
	fx.click(lx + 10, ly + rowH + 2);
	std::vector<Msg> got = fx.take();
	CHECK(GadgetFx::count(got, GCM_SELECTED) == 1);
	CHECK(list->winIsHidden());
	CHECK(narrow(GadgetComboBoxGetText(cb)) == "Closed");
	GadgetComboBoxGetSelectedPos(cb, &sel);
	CHECK(sel == 1);
	cb->winGetSize(&cw, &ch);
	CHECK(ch == 27); // the box shrank back
	// GCM_SET_SELECTION from the engine (a slot set by the lobby) tells the owner too and does not open the list
	GadgetComboBoxSetSelectedPos(cb, 3);
	CHECK(narrow(GadgetComboBoxGetText(cb)) == "Brutal AI");
	CHECK(list->winIsHidden());
	// item data per entry
	GadgetComboBoxSetItemData(cb, 2, reinterpret_cast<void *>(0x22));
	CHECK(GadgetComboBoxGetItemData(cb, 2) == reinterpret_cast<void *>(0x22));
	// reset empties list and text
	GadgetComboBoxReset(cb);
	CHECK(GadgetComboBoxGetLength(cb) == 0);
	CHECK(GadgetComboBoxGetText(cb).empty());
}

namespace
{
// the top of the drawn selection bar of `list` (its first image), -1 when none
int selectionBarTop(GadgetFx &fx, GameWindow *list)
{
	GadgetDrawList draw;
	fx.gwm.winRepaint(draw);
	int lx = 0, ly = 0, lw = 0, lh = 0;
	list->winGetScreenPosition(&lx, &ly);
	list->winGetSize(&lw, &lh);
	for (const GadgetDrawCommand &c : draw.commands)
	{
		if (c.kind == GadgetDrawCommand::Kind::Image && c.image.rfind("Sel", 0) == 0 && c.y0 >= ly && c.y0 < ly + lh)
		{
			return c.y0;
		}
	}
	return -1;
}
} // namespace

// Lane FB7-1 r3 (the owner's report: "the highlight stays on the chosen entry instead of following the mouse"): RotWK's open drop-down list follows the
// pointer (RW 0x72454E sets ListboxData +0x12 / +0x13 and starts +0x30 at the selection; GWM_MOUSE_POS RW 0x727081 / 0x7258E1; the draw RW 0x4A22D6)
TEST_CASE("fb7 ComboBox: the open list's highlight starts on the chosen entry and follows the pointer; the click picks the row under it")
{
	GadgetFx fx;
	GameWindow *cb = fx.load(comboWnd(154, 27), 200, 300);
	cb->winSetSize(154, 27);
	for (const char *t : { "Open", "Closed", "Easy AI", "Brutal AI" })
	{
		GadgetComboBoxAddEntry(cb, u(t), GameMakeColor(255, 255, 255, 255));
	}
	GadgetComboBoxSetSelectedPos(cb, 0);
	GameWindow *list = GadgetComboBoxGetListBox(cb);
	REQUIRE(list);
	ListboxData *data = static_cast<ListboxData *>(list->winGetUserData());
	REQUIRE(data);
	CHECK_FALSE(data->trackHover);
	fx.click(220, 310);
	REQUIRE_FALSE(list->winIsHidden());
	CHECK(data->trackHover);
	CHECK(data->hoverActive);
	CHECK(data->hoverPos == 0); // the chosen entry
	int lx, ly;
	list->winGetScreenPosition(&lx, &ly);
	const int rowH = fx.gwm.winFontHeight(list->winGetFont()) + 1;
	const int barAtChosen = selectionBarTop(fx, list);
	REQUIRE(barAtChosen >= 0);
	// the pointer over row 2: the highlight moves there, the selection does not change
	fx.move(lx + 10, ly + 2 * rowH + 2);
	CHECK(data->hoverPos == 2);
	CHECK(data->selectPos == 0);
	const int barAtRow2 = selectionBarTop(fx, list);
	CHECK(barAtRow2 == barAtChosen + 2 * rowH); // two rows down (a row is the font height + 1)
	// the click there picks it
	fx.click(lx + 10, ly + 2 * rowH + 2);
	CHECK(list->winIsHidden());
	CHECK(narrow(GadgetComboBoxGetText(cb)) == "Easy AI");
}

TEST_CASE("ComboBox: a click outside an open box closes it (the lone window); few entries hide the scroll controls")
{
	GadgetFx fx;
	GameWindow *cb = fx.load(comboWnd(154, 27), 200, 300);
	cb->winSetSize(154, 27);
	GadgetComboBoxAddEntry(cb, u("One"), 0);
	GadgetComboBoxAddEntry(cb, u("Two"), 0);
	GameWindow *list = GadgetComboBoxGetListBox(cb);
	fx.click(220, 310);
	REQUIRE_FALSE(list->winIsHidden());
	CHECK(GadgetListBoxGetSlider(list)->winIsHidden()); // 2 entries <= MAXDISPLAY 3
	CHECK(GadgetListBoxGetUpButton(list)->winIsHidden());
	// a click on another window: the lone window is closed with GGM_CLOSE
	GameWindow *other = fx.load(checkboxWnd(), 600, 600);
	(void)other;
	fx.click(610, 610);
	CHECK(list->winIsHidden());
	CHECK(fx.gwm.winGetLoneWindow() == nullptr);
}

TEST_CASE("TextEntry: typed characters (IME chars) fill the text up to MAXLEN (RotWK RW 0x72260B), backspace removes, numeric only filters, Enter reports EDIT_DONE")
{
	GadgetFx fx;
	GameWindow *e = fx.load(entryWnd(160, 25), 50, 50);
	CHECK(narrow(GadgetTextEntryGetText(e)) == "Entry"); // the TEXT label is the initial text (ZH gogoGadgetTextEntry)
	GadgetTextEntrySetText(e, UnicodeString());
	CHECK(fx.click(60, 60)); // focus
	CHECK(fx.gwm.winGetFocus() == e);
	fx.take();
	for (char c : std::string("abcdefghij"))
	{
		fx.gwm.winProcessChar((char16_t)c);
	}
	// lane CAH-2 r2: RotWK's insert refuses only when the text already holds maxTextLen characters (RW 0x72260B), so MAXLEN 8 holds 8 (ZH: 7)
	CHECK(narrow(GadgetTextEntryGetText(e)) == "abcdefgh");
	CHECK(GadgetFx::count(fx.take(), GEM_UPDATE_TEXT) == 8);
	fx.gwm.winProcessKey(KEY_BACKSPACE, KEY_STATE_DOWN);
	fx.gwm.winProcessKey(KEY_BACKSPACE, KEY_STATE_DOWN);
	CHECK(narrow(GadgetTextEntryGetText(e)) == "abcdef");
	// Enter (a '\r' char) is EDIT_DONE and does not change the text
	fx.take();
	fx.gwm.winProcessChar(u'\r');
	CHECK(GadgetFx::count(fx.take(), GEM_EDIT_DONE) == 1);
	CHECK(narrow(GadgetTextEntryGetText(e)) == "abcdef");
	// numeric only
	GameWindow *n = fx.load(entryWnd(160, 25, "MAXLEN: 8,\n SECRETTEXT: 0,\n NUMERICALONLY: 1,\n ALPHANUMERICALONLY: 0,\n ASCIIONLY: 0"), 300, 50);
	GadgetTextEntrySetText(n, UnicodeString());
	fx.click(310, 60);
	for (char c : std::string("a1b2"))
	{
		fx.gwm.winProcessChar((char16_t)c);
	}
	CHECK(narrow(GadgetTextEntryGetText(n)) == "12");
	// the key path: letters are swallowed (ZH answers MSG_HANDLED for any key it does not list), Ctrl+key is ignored
	CHECK(fx.gwm.winProcessKey(KEY_ESC, KEY_STATE_DOWN) == WIN_INPUT_NOT_USED);
	// losing the focus clears the selected / hilite state
	fx.gwm.winSetFocus(nullptr);
	CHECK_FALSE(BitTest(n->winGetInstanceData()->getState(), WIN_STATE_SELECTED));
}

TEST_CASE("TextEntry: secret text draws stars; the draw commands carry the text, the image slices and a clip region")
{
	GadgetFx fx;
	GameWindow *e = fx.load(entryWnd(160, 25, "MAXLEN: 16,\n SECRETTEXT: 1,\n NUMERICALONLY: 0,\n ALPHANUMERICALONLY: 0,\n ASCIIONLY: 0"), 50, 50);
	GadgetTextEntrySetText(e, u("pass"));
	GadgetDrawList list;
	fx.gwm.winRepaint(list);
	std::set<std::string> images;
	std::string drawn;
	for (const GadgetDrawCommand &c : list.commands)
	{
		if (c.kind == GadgetDrawCommand::Kind::Image)
		{
			images.insert(c.image);
		}
		if (c.kind == GadgetDrawCommand::Kind::Text)
		{
			drawn = narrow(c.text);
		}
	}
	CHECK(drawn == "****");
	CHECK(images.count("EntLeft") == 1);
	CHECK(images.count("EntRight") == 1);
	CHECK(images.count("EntCenter") + images.count("EntSmall") >= 1);
	CHECK(list.count(GadgetDrawCommand::Kind::ClipBegin) >= 1);
	CHECK(list.count(GadgetDrawCommand::Kind::ClipEnd) >= 1);
}

TEST_CASE("HorzSlider: the position follows GSM_SET_SLIDER, a click moves the thumb, dragging sends GSM_SLIDER_TRACK, the draw is the box meter")
{
	GadgetFx fx;
	GameWindow *s = fx.load(sliderWnd(232, 24), 100, 500);
	CHECK(GadgetSliderGetPosition(s) == 0);
	int mn, mx;
	GadgetSliderGetMinMax(s, &mn, &mx);
	CHECK(mn == 0);
	CHECK(mx == 100);
	GadgetSliderSetPosition(s, 60);
	CHECK(GadgetSliderGetPosition(s) == 60);
	GadgetSliderSetPosition(s, 500); // out of range: ignored (ZH breaks)
	CHECK(GadgetSliderGetPosition(s) == 60);
	GameWindow *thumb = GadgetSliderGetThumb(s);
	REQUIRE(thumb);
	int tx, ty;
	thumb->winGetPosition(&tx, &ty);
	const SliderData *sd = static_cast<SliderData *>(s->winGetUserData());
	CHECK(tx == (int)((60 - 0) * sd->numTicks));
	CHECK(ty == HORIZONTAL_SLIDER_THUMB_Y); // lane FB7-1: RotWK keeps the thumb at y 0
	// dragging the thumb with the mouse
	fx.take();
	ICoord2D p{ 100 + 20, 505 };
	fx.gwm.winProcessMouseEvent(GWM_MOUSE_POS, &p, nullptr);
	fx.gwm.winProcessMouseEvent(GWM_LEFT_DOWN, &p, nullptr);
	ICoord2D q{ 100 + 120, 505 };
	ICoord2D delta{ 100, 0 };
	fx.gwm.winProcessMouseEvent(GWM_LEFT_DRAG, &q, &delta);
	fx.gwm.winProcessMouseEvent(GWM_LEFT_UP, &q, nullptr);
	std::vector<Msg> got = fx.take();
	CHECK(GadgetFx::count(got, GSM_SLIDER_TRACK) >= 1);
	// the draw: filled boxes then empty boxes (disabled image 0 / 1), RotWK W3DGadgetHorizontalSliderImageDraw (RW 0x4A1130)
	GadgetSliderSetPosition(s, 50);
	GadgetDrawList list;
	fx.gwm.winRepaint(list);
	std::size_t fill = 0, empty = 0;
	for (const GadgetDrawCommand &c : list.commands)
	{
		if (c.kind == GadgetDrawCommand::Kind::Image && c.image == "BarFill")
		{
			++fill;
		}
		if (c.kind == GadgetDrawCommand::Kind::Image && c.image == "BarEmpty")
		{
			++empty;
		}
	}
	CHECK(fill > 0);
	CHECK(empty > 0);
	CHECK(fill + empty > 10);
}

TEST_CASE("draw contract: an image the collection does not know is reported in the list, not drawn as a placeholder; clipping pairs up")
{
	GadgetFx fx;
	GameWindow *lb = fx.load(listboxWnd(160, 100), 10, 10);
	GadgetListBoxAddEntryText(lb, u("row"), GameMakeColor(1, 2, 3, 255), -1, 0);
	GadgetListBoxSetSelected(lb, 0);
	// remove one image from the skin: the selection bar cannot be drawn
	GadgetFx fx2;
	fx2.images = MappedImageCollection();
	for (const char *name : { "SelLeft", "SelRight", "SelCenter" }) // SelSmall is missing
	{
		Image img;
		img.name = name;
		img.imageSize.x = 8;
		img.imageSize.y = 16;
		fx2.images.addImage(img);
	}
	GameWindow *lb2 = fx2.load(listboxWnd(160, 100), 10, 10);
	GadgetListBoxAddEntryText(lb2, u("row"), 0, -1, 0);
	GadgetListBoxSetSelected(lb2, 0);
	GadgetDrawList list;
	fx2.gwm.winRepaint(list);
	CHECK(std::find(list.unresolvedImages.begin(), list.unresolvedImages.end(), "SelSmall") != list.unresolvedImages.end());
	// the first fixture draws a selected row text
	GadgetDrawList ok;
	fx.gwm.winRepaint(ok);
	std::size_t texts = ok.count(GadgetDrawCommand::Kind::Text);
	CHECK(texts == 1);
	CHECK(ok.count(GadgetDrawCommand::Kind::ClipBegin) == ok.count(GadgetDrawCommand::Kind::ClipEnd));
}

// ============================================================================================================================
// The component layer: Apt placeholders become gadgets
// ============================================================================================================================

namespace
{

struct LayerFx
{
	MemorySource source;
	RecordingShellServices services;
	GadgetSkinData skins;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::vector<std::string> initNames;

	LayerFx()
	{
		for (const char *name : { "ListBg", "SelLeft", "SelRight", "SelCenter", "SelSmall", "UpEnabled", "DownEnabled", "ComboBg", "DropEnabled", "DropSelected", "BlackSquare", "EntLeft", "EntRight", "EntCenter", "EntSmall" })
		{
			Image img;
			img.name = name;
			img.imageSize.x = 8;
			img.imageSize.y = 16;
			skins.images.addImage(img);
		}
		HeaderTemplate h;
		h.name = "AptGadgets";
		h.fontName = "Albertus MT";
		h.point = 14;
		skins.headers.addTemplate(h);
		source.files["window/apt/listbox.wnd"] = bytes(listboxWnd(160, 100));
		source.files["window/apt/combobox.wnd"] = bytes(comboWnd(120, 24));
		source.files["window/apt/textentry.wnd"] = bytes(entryWnd(160, 25));
		TestMovie level0;
		level0.setRootFrames({ {} });
		source.add("AptLevel0", level0);
		wm = std::make_unique<WindowManager>(source, services);
		layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
		layer->registerComponents();
		wm->init();
		wm->registerScreenRef("T::InitGadgets", [this](const std::string &name, GameWindow *window) {
			initNames.push_back(name);
			CHECK(window != nullptr);
		});
	}
	static std::vector<std::uint8_t> bytes(const std::string &s) { return std::vector<std::uint8_t>(s.begin(), s.end()); }
	void tick(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	}
};

// Lib exports the three gadget symbols as sprites with one rectangle of the given size; User places them.
struct GadgetMovies
{
	TestMovie lib, user;
	std::uint32_t listId = 0, comboId = 0, entryId = 0;
	GadgetMovies()
	{
		lib.addCharacter(0);
		auto symbol = [&](const char *name, float w, float h) {
			std::uint32_t shape = lib.addShape(0, 0, w, h, 1);
			std::uint32_t shapeId = lib.addCharacter(shape);
			std::uint32_t sprite = lib.addSprite({ { lib.addPlaceItem(placeChar(shapeId, 1)) } });
			std::uint32_t id = lib.addCharacter(sprite);
			lib.addExport(name, id);
		};
		symbol("ListBox", 160, 100);
		symbol("ComboBox", 154, 27);
		symbol("TextEntry", 160, 25);
		for (int i = 0; i < 4; ++i)
		{
			user.addCharacter(0); // slots 0..3: the imports fill 1..3
		}
		user.addImport("GameWindowGadgets", "ListBox", 1);
		user.addImport("GameWindowGadgets", "ComboBox", 2);
		user.addImport("GameWindowGadgets", "TextEntry", 3);
	}
};

// The retail placeholder protocol [S-172]: the placeholder's clip script sets `_type`, `_Init` and `_Load` (a Construct clip event).
void withGadgetVars(TestMovie &m, TestMovie::Place &p, const std::string &type, const std::string &load, const std::string &init)
{
	const std::uint32_t code = program(m, [&](Asm &a) {
		a.pushString("_type").setStringVar(type);
		a.pushString("_Init").setStringVar(init);
		a.pushString("_Load").setStringVar(load);
	});
	p.flags |= APT_PLACE_HASCLIPACTION;
	p.events.push_back({ APT_CLIP_CONSTRUCT, 0, code });
}

} // namespace

TEST_CASE("component layer: a placeholder instance becomes a gadget at its stage bounds, InitGadgets gets its name, the window follows the clip")
{
	GadgetMovies m;
	// MapList at (30, 40); a row clip "~3" at (500, 200) holding a ComboBox called Player at (10, 5); an entry at depth 4
	TestMovie::Place list = placeChar(1, 1, "MapList");
	list.flags |= APT_PLACE_HASMATRIX;
	list.translation[0] = 30;
	list.translation[1] = 40;
	withGadgetVars(m.user, list, "ListBox", "Apt/ListBox.wnd", "T::InitGadgets");
	TestMovie::Place player = placeChar(2, 1, "Player");
	player.flags |= APT_PLACE_HASMATRIX;
	player.translation[0] = 10;
	player.translation[1] = 5;
	withGadgetVars(m.user, player, "ComboBox", "Apt/ComboBox.wnd", "T::InitGadgets");
	std::uint32_t rowSprite = m.user.addSprite({ { m.user.addPlaceItem(player) } });
	std::uint32_t rowId = m.user.addCharacter(rowSprite);
	TestMovie::Place row = placeChar(rowId, 2, "~3");
	row.flags |= APT_PLACE_HASMATRIX;
	row.translation[0] = 500;
	row.translation[1] = 200;
	m.user.setRootFrames({ { m.user.addPlaceItem(list), m.user.addPlaceItem(row) } });
	LayerFx fx;
	fx.source.add("GameWindowGadgets", m.lib);
	fx.source.add("User", m.user);
	fx.wm->loadAptWindow("Apt\\", "User.apt", true, 0, -1);
	fx.tick(2);
	REQUIRE(fx.wm->errors().empty());
	REQUIRE(fx.layer->errors().empty());
	GameWindow *lw = fx.layer->windowOf("_level1.MapList");
	GameWindow *cw = fx.layer->windowOf("_level1.~3.Player");
	REQUIRE(lw);
	REQUIRE(cw);
	CHECK(BitTest(lw->winGetStyle(), GWS_SCROLL_LISTBOX));
	CHECK(BitTest(cw->winGetStyle(), GWS_COMBO_BOX));
	int x, y, w, h;
	lw->winGetScreenPosition(&x, &y);
	lw->winGetSize(&w, &h);
	CHECK(x == 30);
	CHECK(y == 40);
	CHECK(w == 160);
	CHECK(h == 100);
	cw->winGetScreenPosition(&x, &y);
	cw->winGetSize(&w, &h);
	CHECK(x == 510);
	CHECK(y == 205);
	CHECK(w == 154);
	CHECK(h == 27);
	CHECK(lw->aptInstanceName() == "MapList");
	CHECK(cw->aptInstanceName() == "3/Player"); // [S-170]
	// the InitGadgets screen reference saw both, in creation order
	CHECK(fx.initNames == std::vector<std::string>{ "MapList", "3/Player" });
	// the child windows (arrows, slider, drop-down button ...) exist inside; the layer's window count is the whole tree
	CHECK(fx.layer->gadgets().allWindows().size() > 8);
	// the window follows the placeholder
	AptCharacterInst *inst = fx.wm->apt().resolvePath(fx.wm->apt().level(1), "MapList");
	REQUIRE(inst);
	inst->matrix.tx = 100;
	inst->matrix.ty = 120;
	fx.tick(1);
	lw->winGetScreenPosition(&x, &y);
	CHECK(x == 100);
	CHECK(y == 120);
	// invisible placeholder: the gadget hides, and shows again
	inst->visible = false;
	fx.tick(1);
	CHECK(lw->winIsHidden());
	inst->visible = true;
	fx.tick(1);
	CHECK_FALSE(lw->winIsHidden());
	// draw commands: the gadget skins reach the list; a hidden gadget draws nothing
	GadgetDrawList dl;
	fx.layer->buildDrawList(dl);
	CHECK(dl.commands.size() > 0);
	// unloading the movie destroys the instances: the records and the native windows go
	fx.wm->unloadAptWindow(1);
	CHECK(fx.wm->components().empty());
	fx.tick(1); // processDestroyList
	CHECK(fx.layer->gadgets().allWindows().empty());
}

TEST_CASE("component layer: mouse and keyboard reach the gadgets through the window manager; the Apt player sees the events too")
{
	GadgetMovies m;
	TestMovie::Place combo = placeChar(2, 1, "Player");
	combo.flags |= APT_PLACE_HASMATRIX;
	combo.translation[0] = 200;
	combo.translation[1] = 300;
	TestMovie::Place entry = placeChar(3, 2, "Name");
	entry.flags |= APT_PLACE_HASMATRIX;
	entry.translation[0] = 400;
	entry.translation[1] = 300;
	m.user.setRootFrames({ { m.user.addPlaceItem(combo), m.user.addPlaceItem(entry) } });
	LayerFx fx;
	fx.source.add("GameWindowGadgets", m.lib);
	fx.source.add("User", m.user);
	fx.wm->loadAptWindow("Apt\\", "User.apt", true, 0, -1);
	fx.tick(2);
	GameWindow *cw = fx.layer->windowOf("_level1.Player");
	GameWindow *ew = fx.layer->windowOf("_level1.Name");
	REQUIRE(cw);
	REQUIRE(ew);
	GadgetComboBoxAddEntry(cw, u("Open"), 0);
	GadgetComboBoxAddEntry(cw, u("Closed"), 0);
	// the open combo box closes when a release lands where no gadget takes it
	fx.wm->postMouseMove(220, 310);
	fx.wm->postMouseButton(true);
	fx.wm->postMouseButton(false);
	GameWindow *list = GadgetComboBoxGetListBox(cw);
	CHECK_FALSE(list->winIsHidden());
	fx.wm->postMouseMove(900, 700);
	fx.wm->postMouseButton(true);
	CHECK_FALSE(fx.wm->postMouseButton(false));
	CHECK(list->winIsHidden());
	// the entry: click, type, backspace
	GadgetTextEntrySetText(ew, UnicodeString());
	fx.wm->postMouseMove(410, 310);
	CHECK(fx.wm->postMouseButton(true));
	CHECK(fx.wm->postMouseButton(false));
	CHECK(fx.layer->gadgets().winGetFocus() == ew);
	CHECK(fx.wm->postTextInput(u'h'));
	CHECK(fx.wm->postTextInput(u'i'));
	CHECK(narrow(GadgetTextEntryGetText(ew)) == "hi");
	CHECK(fx.wm->postGadgetKey(KEY_BACKSPACE, true));
	CHECK(narrow(GadgetTextEntryGetText(ew)) == "h");
	// a click on empty stage: no gadget, so not used
	fx.wm->postMouseMove(900, 700);
	CHECK_FALSE(fx.wm->postMouseButton(true));
	fx.wm->postMouseButton(false);
	// the Apt input saw the moves (its mouse position is the last one)
	fx.tick(1);
	float mx, my;
	fx.wm->apt().mousePosition(mx, my);
	CHECK(mx == doctest::Approx(900.0f));
}

TEST_CASE("component layer: a skin that cannot be read or parsed is an error in the layer's report, the placeholder stays inert")
{
	GadgetMovies m;
	TestMovie::Place list = placeChar(1, 1, "MapList");
	m.user.setRootFrames({ { m.user.addPlaceItem(list) } });
	LayerFx fx;
	fx.source.files.erase("window/apt/listbox.wnd"); // the skin is missing
	fx.source.add("GameWindowGadgets", m.lib);
	fx.source.add("User", m.user);
	fx.wm->loadAptWindow("Apt\\", "User.apt", true, 0, -1);
	fx.tick(2);
	REQUIRE(fx.layer->errors().size() == 1);
	CHECK(fx.layer->errors()[0].find("window/apt/listbox.wnd") != std::string::npos);
	CHECK(fx.layer->windowOf("_level1.MapList") == nullptr);
	CHECK(fx.initNames.empty()); // no window, no InitGadgets call
	CHECK(fx.wm->components().size() == 1); // the record exists, with no window
}

// ============================================================================================================================
// Retail: the shipped skins
// ============================================================================================================================

TEST_CASE("retail: the gadget skin data loads (mapped images, header templates) and the images the APT skins name are there")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	GadgetSkinData skins;
	std::string error;
	REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
	CHECK(skins.images.size() > 3000);
	const Image *hilite = skins.images.findImageByName("AptListBoxHiliteSelectedItem");
	REQUIRE(hilite);
	CHECK(hilite->getImageHeight() > 0);
	CHECK(skins.images.findImageByName("aptlistboxhiliteselecteditem") == hilite); // names are case-insensitive
	CHECK(skins.images.findImageByName("AptCheckboxCheckedEnabled") != nullptr);
	CHECK(skins.images.findImageByName("AptVSliderUpButtonEnabled") != nullptr);
	CHECK(skins.images.findImageByName("AptHSliderOnBar") != nullptr);
	CHECK(skins.images.findImageByName("NoSuchImage") == nullptr);
	const HeaderTemplate *ht = skins.headers.findHeaderTemplate("AptGadgets");
	REQUIRE(ht);
	CHECK(ht->fontName == "Albertus MT");
	CHECK(ht->point == 14);
	CHECK_FALSE(ht->bold);
	CHECK(skins.headers.findHeaderTemplate("AptGadgetsTiny") != nullptr);
	CHECK(skins.headers.duplicates().empty());
}

TEST_CASE("retail: window/apt/*.wnd - the five gadget skins load, with the retail geometry, and every WND of the directory is accounted for")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	GadgetSkinData skins;
	std::string error;
	REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
	struct Row
	{
		const char *file;
		std::uint32_t style;
		const char *font; // the font of the top window
		int point;
		std::size_t unknownCallbacks; // names (of all windows of the file) the FunctionLexicon would have to know
	};
	AptArchiveFileSource source(mount.fs);
	const Row rows[] = {
		{ "checkbox", GWS_CHECK_BOX, "Albertus MT", 14, 4 },
		{ "combobox", GWS_COMBO_BOX, "Albertus MT", 14, 4 },
		{ "horzslider", GWS_HORZ_SLIDER, "Albertus MT", 14, 4 },
		{ "listbox", GWS_SCROLL_LISTBOX, "Albertus MT", 14, 4 },
		{ "textentry", GWS_ENTRY_FIELD, "Albertus MT", 14, 4 },
		{ "listboxmessenger", GWS_SCROLL_LISTBOX, "Albertus MT", 12, 4 }, // HEADERTEMPLATE AptGadgetsTiny
		// BFME's own gadget: a USER window with child windows and callbacks named GadgetImageComboBoxSystem / Input and
		// W3DGadgetImageComboBoxDraw (not ported, S-177); winCreate leaves a USER window with ZH's default font
		{ "imagecombobox", GWS_USER_WINDOW, "Times New Roman", 14, 13 },
	};
	for (const Row &r : rows)
	{
		GameWindowManager gwm;
		gwm.setImages(&skins.images);
		std::vector<std::uint8_t> bytes;
		REQUIRE_MESSAGE(source.readFile(std::string("window/apt/") + r.file + ".wnd", bytes, &error), error);
		WindowScriptLoader loader(gwm, &skins.headers);
		WindowLayoutInfo info;
		GameWindow *w = loader.createFromScript(r.file, std::string(bytes.begin(), bytes.end()), &info, &error);
		INFO(std::string(r.file) << ": " << error);
		REQUIRE(w);
		CHECK(BitTest(w->winGetStyle(), r.style));
		CHECK(info.version == 2);
		CHECK(info.initName == "[None]");
		REQUIRE(w->winGetFont());
		CHECK(w->winGetFont()->name == r.font);
		CHECK(w->winGetFont()->pointSize == r.point);
		CHECK(info.unknownCallbacks.size() == r.unknownCallbacks);
	}
	// the other two skins load as well: readybutton.wnd is a push button, mpmapwindow.wnd a user window with children
	for (const char *file : { "mpmapwindow", "readybutton" })
	{
		GameWindowManager gwm;
		gwm.setImages(&skins.images);
		std::vector<std::uint8_t> bytes;
		REQUIRE_MESSAGE(source.readFile(std::string("window/apt/") + file + ".wnd", bytes, &error), error);
		WindowScriptLoader loader(gwm, &skins.headers);
		WindowLayoutInfo info;
		GameWindow *w = loader.createFromScript(file, std::string(bytes.begin(), bytes.end()), &info, &error);
		INFO(std::string(file) << ": " << error);
		REQUIRE(w);
		CHECK(BitTest(w->winGetStyle(), std::string(file) == "readybutton" ? GWS_PUSH_BUTTON : GWS_USER_WINDOW));
	}
	// every .wnd of the directory is one of the nine above
	FilenameList list;
	mount.fs.getFileListInDirectory("window\\apt\\", "window\\apt\\", "*.wnd", list, false);
	std::set<std::string> have;
	for (const std::string &f : list)
	{
		have.insert(f.substr(f.find_last_of("\\/") + 1));
	}
	CHECK(have == std::set<std::string>{ "checkbox.wnd", "combobox.wnd", "horzslider.wnd", "imagecombobox.wnd", "listbox.wnd", "listboxmessenger.wnd", "mpmapwindow.wnd", "readybutton.wnd", "textentry.wnd" });
}

TEST_CASE("retail: the shipped check box, slider and list box skins carry their draw data into the gadget (images and sub-gadget skins)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	GadgetSkinData skins;
	std::string error;
	REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
	AptArchiveFileSource source(mount.fs);
	GameWindowManager gwm;
	gwm.setImages(&skins.images);
	WindowScriptLoader loader(gwm, &skins.headers);
	auto load = [&](const char *file) {
		std::vector<std::uint8_t> bytes;
		REQUIRE_MESSAGE(source.readFile(std::string("window/apt/") + file + ".wnd", bytes, &error), error);
		GameWindow *w = loader.createFromScript(file, std::string(bytes.begin(), bytes.end()), nullptr, &error);
		REQUIRE_MESSAGE(w, error);
		return w;
	};
	GameWindow *cb = load("checkbox");
	CHECK(cb->winGetEnabledImage(1) == "AptCheckboxUncheckedEnabled");
	CHECK(cb->winGetEnabledImage(2) == "AptCheckboxCheckedEnabled");
	CHECK(cb->winGetHiliteImage(2) == "AptCheckboxCheckedHighlight");
	CHECK(cb->winGetDisabledImage(1) == "AptCheckboxUncheckedDisabled");
	GameWindow *lb = load("listbox");
	GameWindow *up = GadgetListBoxGetUpButton(lb);
	REQUIRE(up);
	CHECK(up->winGetEnabledImage(0) == "AptVSliderUpButtonEnabled");
	CHECK(up->winGetHiliteImage(0) == "AptVSliderUpButtonHilite");
	GameWindow *slider = GadgetListBoxGetSlider(lb);
	REQUIRE(slider);
	CHECK(slider->winGetChild()->winGetEnabledImage(0) == "AptScrollBarThumbEnabledTop");
	CHECK(lb->winGetEnabledImage(3) == "AptListBoxHiliteSelectedItem"); // the centre slot: the skin has no left / right / small pieces
	ListboxData *ld = static_cast<ListboxData *>(lb->winGetUserData());
	CHECK(ld->listLength == 999);
	CHECK(ld->scrollIfAtEnd);
	CHECK(ld->autoPurge);
	CHECK(ld->forceSelect);
	GameWindow *hs = load("horzslider");
	CHECK(hs->winGetDisabledImage(0) == "AptHSliderOnBar");
	CHECK(hs->winGetHiliteImage(0) == "AptHSliderOverOutline");
	int mn, mx;
	GadgetSliderGetMinMax(hs, &mn, &mx);
	CHECK(mn == 0);
	CHECK(mx == 100);
	GameWindow *combo = load("combobox");
	GameWindow *drop = GadgetComboBoxGetDropDownButton(combo);
	REQUIRE(drop);
	CHECK(drop->winGetEnabledImage(0) == "AptVSliderDownButtonEnabled");
	GameWindow *clist = GadgetComboBoxGetListBox(combo);
	REQUIRE(clist);
	CHECK(GadgetListBoxGetUpButton(clist)->winGetEnabledImage(0) == "AptVSliderUpButtonEnabled");
	ComboBoxData *cd = static_cast<ComboBoxData *>(combo->winGetUserData());
	CHECK_FALSE(cd->isEditable);
	CHECK(cd->maxChars == 16);
	CHECK(cd->maxDisplay == 10);
	// the images these skins draw are all in the collection: drawing them reports no unresolved name
	GadgetDrawList dl;
	hs->winSetPosition(10, 10);
	gwm.winRepaint(dl);
	CHECK(dl.unresolvedImages.empty());
}

// ============================================================================================================================
// Retail: the Skirmish lobby creates its gadgets
// ============================================================================================================================

namespace
{
struct RetailGadgetShell
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;
	std::vector<std::string> initNames;

	explicit RetailGadgetShell(AptRetail &mount) : source(mount.fs)
	{
		std::string error;
		REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
		registerAptScreenFactories(factories);
		wm = std::make_unique<WindowManager>(source, services);
		layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
		layer->registerComponents();
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->init();
	}
	~RetailGadgetShell()
	{
		shell.reset();
		layer.reset();
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	}
};
} // namespace

TEST_CASE("retail Skirmish: the lobby places 48 slot combo boxes (Player, PlayerTemplate, Team, Color, Handicap, Hero for 8 slots), the map info list and the profile popup gadgets, each a gadget at its placeholder")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	RetailGadgetShell fx(mount);
	std::map<std::string, std::uint32_t> styles;
	fx.shell->push("Skirmish.apt");
	fx.tick(40);
	REQUIRE(fx.shell->errors().empty());
	CHECK(fx.layer->errors().empty());
	// AptSkirmish owns the screen references the placeholders name (`_Init`); the gadgets it was told about are the component records that named one
	// (lane WINCRASH-1: of the clips placed now; a removed placeholder's record stays, detached, until the level is unloaded)
	for (const WindowManager::ComponentRecord &r : fx.wm->components())
	{
		if (r.window && r.instance && !r.init.empty())
		{
			fx.initNames.push_back(r.instanceName);
			styles[r.instanceName] = r.window->winGetStyle();
		}
	}
	std::map<std::string, int> counts;
	for (const std::string &n : fx.initNames)
	{
		const std::size_t slash = n.find('/');
		counts[slash == std::string::npos ? n : n.substr(slash + 1)]++;
	}
	// 8 slots x (Player, PlayerTemplate, Team, Color, Handicap, Hero), the map list (the host variant: MpGameSetup::HostMode reads "1" and _global.InGame is set), the map info list and the two profile gadgets of the popup
	// lane UI-2: plus the map window CurrentMap, a component by its `_type` (RW 0x814ED0's registry) though no export names it
	CHECK(counts == std::map<std::string, int>{ { "Color", 8 }, { "CurrentMap", 1 }, { "Handicap", 8 }, { "Hero", 8 }, { "MapInfo", 1 }, { "MapList", 1 }, { "Player", 8 },
		{ "PlayerTemplate", 8 }, { "Skirmish::CreatePersonaEntry", 1 }, { "Team", 8 } });
	CHECK(fx.initNames.size() == 52); // the change / delete pages' SelectProfile list exists only while that page is shown (test_apt_skirmish.cpp)
	for (int slot = 0; slot < 8; ++slot)
	{
		for (const char *leaf : { "Player", "PlayerTemplate", "Team", "Color", "Handicap", "Hero" })
		{
			const std::string name = std::to_string(slot) + "/" + leaf;
			CHECK_MESSAGE(std::find(fx.initNames.begin(), fx.initNames.end(), name) != fx.initNames.end(), name);
		}
	}
	// the profile popup's gadgets (the spec's A5 test): CreatePersonaEntry is a text entry
	REQUIRE(styles.count("Skirmish::CreatePersonaEntry"));
	CHECK(BitTest(styles["Skirmish::CreatePersonaEntry"], GWS_ENTRY_FIELD));
	CHECK(BitTest(styles["0/Player"], GWS_COMBO_BOX));
	CHECK(BitTest(styles["MapInfo"], GWS_SCROLL_LISTBOX));
	// the movie's own load request: the OpenPlay lobby page
	REQUIRE_FALSE(fx.wm->movieLoads().empty());
	CHECK(fx.wm->movieLoads()[0].movie == "SkirmishOpenPlay");
	CHECK(fx.wm->movieLoads()[0].target == "_level1.OpenPlay");
}

TEST_CASE("TextEntry: RotWK's character validator (RW 0x75E4DF) refuses the Thai blocks always and applies the flag bits in retail order")
{
	for (char16_t c : { (char16_t)0x0E01, (char16_t)0x0E3A, (char16_t)0x0E3F, (char16_t)0x0E5B })
	{
		CHECK_FALSE(GadgetTextEntryValidateCharacter(c, 0));
	}
	CHECK(GadgetTextEntryValidateCharacter((char16_t)0x0E3B, 0)); // between the two blocks
	CHECK(GadgetTextEntryValidateCharacter((char16_t)0x0E5C, 0));
	CHECK(GadgetTextEntryValidateCharacter(u'\u00E9', 0));
	CHECK_FALSE(GadgetTextEntryValidateCharacter(u' ', 0x01));
	CHECK(GadgetTextEntryValidateCharacter(u' ', 0x81)); // 0x80 allows the space before 0x01 refuses it
	CHECK_FALSE(GadgetTextEntryValidateCharacter(u'%', 0x02));
	CHECK_FALSE(GadgetTextEntryValidateCharacter(u'\\', 0x04));
	CHECK_FALSE(GadgetTextEntryValidateCharacter(u'!', 0x04)); // below 0x22
	CHECK(GadgetTextEntryValidateCharacter(u'"', 0x04));
	CHECK_FALSE(GadgetTextEntryValidateCharacter(u'"', 0x08));
	CHECK_FALSE(GadgetTextEntryValidateCharacter(u'|', 0x08));
	CHECK_FALSE(GadgetTextEntryValidateCharacter(u'\u00E9', 0x10));
	CHECK_FALSE(GadgetTextEntryValidateCharacter(u'a', 0x20));
	CHECK(GadgetTextEntryValidateCharacter(u'7', 0x20));
	CHECK_FALSE(GadgetTextEntryValidateCharacter(u'-', 0x40));
	CHECK(GadgetTextEntryValidateCharacter(u'Q', 0x40));
	CHECK(GadgetTextEntryValidateCharacter(u'Q', 0x100)); // only the low byte is read
}
