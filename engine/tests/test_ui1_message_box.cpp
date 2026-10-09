// OpenBFME unit tests. GPL-3.0.
// Lane UI-1: RotWK's message box (GameClient/GUI/AptMessageBox.h) on GuiFX.apt's MessageBox clip: the texts reach the records the clip's fields read
// ("$" + the clip's path + "_Title" / "_Text"), Show / Hide run the clip's own functions, and the clip's buttons (FSCommand "<path>_OnButtonOk" ...) reach the
// caller as RW 0x953141 .. 0x953197's codes. SKIP loudly without the retail install.

#include "doctest.h"

#include "AptRetail.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include <string>
#include <vector>

TEST_CASE("ui1 retail: the message box shows on GuiFX.apt's MessageBox clip and its buttons answer (RW 0x953861 / 0x9534DE)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	registerAptScreenFactories(factories);
	WindowManager wm(source, services);
	Shell shell(wm, factories, services, environment);
	wm.init();
	auto tick = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			wm.update(33);
		}
	};
	tick(2);
	AptMessageBox box(wm, shell);
	REQUIRE(box.level() >= 0);
	const std::string path = "_level" + std::to_string(box.level()) + ".MessageBox";
	std::vector<int> answers;
	box.show(AptMessageBox::TYPE_YES_NO, u"Title here", u"Some text", [&](int b) { answers.push_back(b); });
	for (int i = 0; i < 30 && box.calls().empty(); ++i)
	{
		tick(1);
		box.update();
	}
	REQUIRE(!box.calls().empty());
	CHECK(box.calls().front() == "Show YesNo false");
	CHECK(wm.noteCount("invoke-failed") == 0);
	// RW 0xC8171C "APT:_level%u.%s_" + Title / Text: the records the clip's fields read
	REQUIRE(wm.aptText("APT:" + path + "_Title"));
	CHECK(*wm.aptText("APT:" + path + "_Title") == "Title here");
	CHECK(*wm.aptText("APT:" + path + "_Text") == "Some text");
	tick(20);
	// the clip: Show(msgType) kept the type (its `messageType` variable)
	AptCharacterInst *clip = wm.apt().resolvePath(wm.apt().level(box.level()), "MessageBox");
	REQUIRE(clip != nullptr);
	AptValue type;
	REQUIRE(clip->getMember("messageType", type));
	CHECK(type.toString() == "YesNo");
	// the Yes button's command (GuiFX.apt: "FSCommand:" + _parent + "_OnButtonYes")
	wm.fscommand(path + "_OnButtonYes", "");
	CHECK(answers == std::vector<int>{ AptMessageBox::BUTTON_YES });
	CHECK(box.type() == AptMessageBox::TYPE_NONE);
	// a second box over a first: the first is hidden (RW 0x953861 -> 0x953243), a long text is "large"
	box.show(AptMessageBox::TYPE_OK, u"A", u"B");
	box.show(AptMessageBox::TYPE_OK_CANCEL, u"C", std::u16string(300, u'x'));
	tick(2);
	box.update();
	CHECK(box.calls().back() == "Show OkCancel true");
	wm.fscommand(path + "_OnButtonCancel", "");
	CHECK(answers.size() == 1); // the second box had no callback
	box.show(AptMessageBox::TYPE_NON_INTERACTIVE, u"", u"wait");
	box.hide(true);
	CHECK(box.calls().back() == "Hide true");
	CHECK(wm.noteCount("invoke-failed") == 0);
}
