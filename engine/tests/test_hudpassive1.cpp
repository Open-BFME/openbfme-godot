// OpenBFME synthetic Palantir passive-state tests. These exercise the engine's sync against an in-memory movie and do not claim retail script fidelity.

#include "doctest.h"

#include "AptTestUtil.h"
#include "AptPlayerTestUtil.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/HudContext.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/TacticalView.h"
#include "Libraries/Source/Apt/AptRenderList.h"
#include "LogicTestUtil.h"

#include <algorithm>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

using namespace apttest;

namespace
{

TestMovie palantirMovie()
{
	TestMovie movie;
	movie.addCharacter(0);
	const std::uint32_t shapeRecord = movie.addShape(0, 0, 10, 10, 1);
	const std::uint32_t shapeId = movie.addCharacter(shapeRecord);
	const std::uint32_t staticRecord = movie.addSprite({ { movie.addPlaceItem(placeChar(shapeId, 1, "passiveArt")) } });
	const std::uint32_t staticId = movie.addCharacter(staticRecord);
	std::vector<std::uint32_t> contentItems;
	const char *contentNames[] = { "passiveRing", "activeGlow", "selectedHighlight", "timerArt" };
	for (int i = 0; i < 4; ++i)
	{
		contentItems.push_back(movie.addPlaceItem(placeChar(shapeId, i + 1, contentNames[i])));
	}
	const std::uint32_t contentAction = program(movie, [](Asm &a) {
		a.defineFunction("SetState", { "state" }, [](Asm &) {});
		a.defineFunction("SetAutoAbilityOverlayState", { "state" }, [](Asm &) {});
	});
	contentItems.push_back(movie.addActionItem(contentAction));
	const std::uint32_t contentRecord = movie.addSprite({ contentItems });
	const std::uint32_t contentId = movie.addCharacter(contentRecord);
	movie.addExport("Gizmo", contentId);

	std::vector<std::uint32_t> buttonItems;
	for (int i = 0; i < AptPalantir::kArcPositions; ++i)
	{
		const int position = i + 1;
		const std::uint32_t frameAction = program(movie, [position](Asm &a) {
			a.defineFunction("CreateContent", { "type", "name" }, [position](Asm &f) {
				f.pushByte(4).pushString("content").pushString("Gizmo").pushByte(3).getStringVar("this").pushString("attachMovie").op(APT_OP_EA_CALLMETHODPOP);
				f.getURL("FSCommand:PalantirTest::ContentCreated", std::to_string(position));
				f.getStringVar("extern").pushString("_level1.CommandButtons.Command" + std::to_string(position) + "_ContentName").setStringMember("Gizmo");
			});
			a.defineFunction("DeleteContent", {}, [position](Asm &f) {
				f.pushByte(0).getStringVar("this").pushString("content").op(APT_OP_GETMEMBER).pushString("removeMovieClip").op(APT_OP_EA_CALLMETHODPOP);
				f.getURL("FSCommand:PalantirTest::ContentDeleted", std::to_string(position));
			});
		});
		const std::uint32_t frameRecord = movie.addSprite({ { movie.addPlaceItem(placeChar(staticId, 1, "_static")), movie.addActionItem(frameAction) } });
		const std::uint32_t frameId = movie.addCharacter(frameRecord);
		buttonItems.push_back(movie.addPlaceItem(placeChar(frameId, position, "Command" + std::to_string(position))));
	}
	const std::uint32_t buttonsAction = program(movie, [](Asm &a) {
		a.defineFunction("gotoAndPlay", { "state" }, [](Asm &f) { f.getURL("FSCommand:PalantirTest::FrameStateChanged", ""); });
	});
	buttonItems.push_back(movie.addActionItem(buttonsAction));
	const std::uint32_t buttonsRecord = movie.addSprite({ buttonItems });
	const std::uint32_t buttonsId = movie.addCharacter(buttonsRecord);
	const std::uint32_t rootAction = program(movie, [](Asm &a) {
		for (const char *name : { "SetPalantirFrameState", "SetPlayerButtonsState", "EnablePlayerMagicButton", "SetPlayerFaction", "ShowCommandInterface",
				 "HideCommandInterface", "HideRankInterface", "ShowRankInterface", "ShowRankProgress", "HideRankProgress", "SetRankProgressBar", "SetPlayerMagicProgress" })
		{
			a.defineFunction(name, {}, [](Asm &) {});
		}
		a.getURL("FSCommand:AptPalantir::OnInitialized", "");
		for (int i = 0; i < AptPalantir::kArcPositions; ++i)
		{
			a.getURL("FSCommand:PalantirCommandUI::OnButtonFrameLoaded", "index=" + std::to_string(i) + "&name=CommandButtons.Command" + std::to_string(i + 1));
		}
	});
	const std::uint32_t rootButtons = movie.addPlaceItem(placeChar(buttonsId, 1, "CommandButtons"));
	const std::uint32_t rootScript = movie.addActionItem(rootAction);
	movie.setRootFrames({ { rootButtons, rootScript } });
	return movie;
}

struct PalantirFx
{
	MemorySource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	std::unique_ptr<WindowManager> windows;
	std::unique_ptr<Shell> shell;

	PalantirFx()
	{
		TestMovie level0;
		level0.addCharacter(0);
		level0.setRootFrames({ {} });
		source.add("AptLevel0", level0);
		TestMovie palantir = palantirMovie();
		source.add("Palantir", palantir);
		factories.registerFactory("Palantir.apt", [](AptScreenContext &context) -> std::unique_ptr<AptScreen> {
			return std::make_unique<AptPalantir>(context.windows, context.shell);
		});
		windows = std::make_unique<WindowManager>(source, services);
		shell = std::make_unique<Shell>(*windows, factories, services, environment);
		windows->init();
		shell->push("Palantir.apt", true);
	}
	void tick(int count)
	{
		for (int i = 0; i < count; ++i)
		{
			windows->update(33);
			shell->update();
		}
	}
	AptPalantir *palantir() const { return dynamic_cast<AptPalantir *>(shell->top()); }
};

} // namespace

TEST_CASE("hudpassive1 synthetic: populated passive content is deleted and recreated on reset transitions")
{
	logictest::LogicWorld game;
	PinholeView view;
	InGameUI ui;
	MessageStream stream;
	ui.setLogic(game.logic.get());
	HudContext context{ *game.logic, nullptr, view, ui, stream, nullptr, {}, {} };
	ControlBar bar(context);
	PalantirFx fx;
	AptPalantir *p = fx.palantir();
	REQUIRE(p);
	REQUIRE(p->level() == 1);
	int frameStateChanges = 0;
	int contentCreated = 0;
	int contentDeleted = 0;
	fx.windows->registerCommand("PalantirTest::FrameStateChanged", [&](const std::string &) { ++frameStateChanges; });
	fx.windows->registerCommand("PalantirTest::ContentCreated", [&](const std::string &) { ++contentCreated; });
	fx.windows->registerCommand("PalantirTest::ContentDeleted", [&](const std::string &) { ++contentDeleted; });

	// This const_cast is safe because `bar` is non-const; it supplies Palantir's input without claiming ControlBar behavior.
	std::vector<ControlBarButton> &buttons = const_cast<std::vector<ControlBarButton> &>(bar.palantirButtons());
	CommandButton firstCommand;
	firstCommand.m_name = "SyntheticPassive";
	firstCommand.m_options = COMMAND_OPTION_NONPRESSABLE;
	ControlBarButton first;
	first.slot = 0;
	first.button = &firstCommand;
	first.state = ButtonState::Enabled;
	first.image = "test-image";
	first.timer = 0.5f;
	first.position = 0;
	first.autoAbility = true;
	first.productionCount = 2;
	buttons.push_back(first);

	AptPalantir::LocalState state;
	state.context = true;
	state.contextObject = 1;
	state.commandSet = "SyntheticA";
	p->setLocalState(state);
	p->sync(bar);
	CHECK_FALSE(p->initialized());
	CHECK(contentCreated == 0);
	CHECK(contentDeleted == 0);

	fx.tick(2);
	REQUIRE(p->initialized());
	REQUIRE(p->buttonFrames().size() == AptPalantir::kArcPositions);
	buttons.clear();
	p->sync(bar);
	CHECK(p->commandInterfaceShown());
	CHECK(frameStateChanges == 1);
	CHECK(contentCreated == 0);
	CHECK(contentDeleted == 0);

	const std::string content = "_level" + std::to_string(p->level()) + ".CommandButtons.Command1.content";
	const std::string initialized = content + "_OnInitialized";
	auto checkContentClip = [&] {
		AptSpriteInst *level = fx.windows->apt().level(p->level());
		REQUIRE(level);
		AptCharacterInst *frameCharacter = fx.windows->apt().resolvePath(level, "CommandButtons.Command1");
		REQUIRE(frameCharacter);
		AptSpriteInst *frame = frameCharacter->asSprite();
		REQUIRE(frame);
		AptCharacterInst *contentCharacter = fx.windows->apt().resolvePath(frame, "content");
		REQUIRE(contentCharacter);
		AptSpriteInst *contentClip = contentCharacter->asSprite();
		REQUIRE(contentClip);
		for (const char *child : { "passiveRing", "activeGlow", "selectedHighlight", "timerArt" })
		{
			CHECK(fx.windows->apt().resolvePath(contentClip, child) != nullptr);
		}
	};
	auto checkLive = [&] {
		checkContentClip();
		const AptPalantir::NativeImage *image = p->imageFor(content + "_Image");
		REQUIRE(image);
		CHECK(image->image == "test-image");
		CHECK(p->timerFor(content + "_Timer") == 0.5f);
		const std::string *count = fx.windows->aptText("APT:" + content + "_ProductionCount");
		REQUIRE(count);
		CHECK(*count == "2");
		CHECK(p->autoAbilityOverlayShown(0, true));
		AptRenderList render;
		fx.windows->apt().buildRenderList(render);
		for (const char *child : { "passiveRing", "activeGlow", "selectedHighlight", "timerArt" })
		{
			CHECK(std::any_of(render.commands.begin(), render.commands.end(), [&](const AptRenderCommand &command) {
				return command.path.find(content + "." + child) != std::string::npos;
			}));
		}
	};
	auto checkEmpty = [&] {
		AptSpriteInst *level = fx.windows->apt().level(p->level());
		REQUIRE(level);
		AptCharacterInst *frameCharacter = fx.windows->apt().resolvePath(level, "CommandButtons.Command1");
		REQUIRE(frameCharacter);
		AptSpriteInst *frame = frameCharacter->asSprite();
		REQUIRE(frame);
		CHECK(fx.windows->apt().resolvePath(frame, "_static.passiveArt") != nullptr);
		CHECK(fx.windows->apt().resolvePath(frame, "content") == nullptr);
		CHECK(p->imageFor(content + "_Image") == nullptr);
		CHECK(p->timerFor(content + "_Timer") == -1.0f);
		const std::string *count = fx.windows->aptText("APT:" + content + "_ProductionCount");
		CHECK((count == nullptr || *count == " "));
		CHECK_FALSE(p->autoAbilityOverlayShown(0, true));
		AptRenderList render;
		fx.windows->apt().buildRenderList(render);
		CHECK(std::any_of(render.commands.begin(), render.commands.end(), [](const AptRenderCommand &command) {
			return command.path.find("Command1._static.passiveArt") != std::string::npos;
		}));
		CHECK(std::none_of(render.commands.begin(), render.commands.end(), [&](const AptRenderCommand &command) {
			return command.path.find(content) != std::string::npos;
		}));
	};
	auto initializeContent = [&] {
		fx.tick(1);
		CHECK(fx.windows->invokeCallback(initialized, ""));
		p->sync(bar);
		for (const std::string &error : p->callErrors())
		{
			MESSAGE(error);
		}
		CHECK(p->callErrors().empty());
	};

	buttons.push_back(first);
	p->sync(bar);
	CHECK(contentCreated == 1);
	checkContentClip();
	fx.tick(1);
	buttons.clear();
	p->sync(bar);
	CHECK(contentDeleted == 1);
	checkEmpty();
	CHECK(fx.windows->invokeCallback(initialized, ""));
	p->sync(bar);
	checkEmpty();
	CHECK(contentDeleted == 1);

	buttons.push_back(first);
	p->sync(bar);
	CHECK(contentCreated == 2);
	initializeContent();
	checkLive();
	p->sync(bar);
	CHECK(frameStateChanges == 1);
	CHECK(contentCreated == 2);
	CHECK(contentDeleted == 1);

	buttons.clear();
	p->sync(bar);
	CHECK(contentDeleted == 2);
	checkEmpty();
	p->sync(bar);
	CHECK(contentDeleted == 2);
	checkEmpty();

	auto refillAndInitialize = [&] {
		buttons.push_back(first);
		p->sync(bar);
		initializeContent();
		checkLive();
	};
	refillAndInitialize();
	const int beforeContextCreate = contentCreated;
	const int beforeContextDelete = contentDeleted;
	state.contextObject = 2;
	p->setLocalState(state);
	p->sync(bar);
	CHECK(contentDeleted == beforeContextDelete + 1);
	CHECK(contentCreated == beforeContextCreate + 1);
	initializeContent();
	checkLive();
	p->sync(bar);
	CHECK(contentCreated == beforeContextCreate + 1);
	CHECK(contentDeleted == beforeContextDelete + 1);

	const int beforeSetCreate = contentCreated;
	const int beforeSetDelete = contentDeleted;
	state.commandSet = "SyntheticB";
	p->setLocalState(state);
	p->sync(bar);
	CHECK(contentDeleted == beforeSetDelete + 1);
	CHECK(contentCreated == beforeSetCreate + 1);
	initializeContent();
	checkLive();

	CommandButton replacementCommand;
	replacementCommand.m_name = firstCommand.m_name;
	replacementCommand.m_options = firstCommand.m_options;
	ControlBarButton replacement = first;
	replacement.button = &replacementCommand;
	buttons[0] = replacement;
	const int beforePointerCreate = contentCreated;
	const int beforePointerDelete = contentDeleted;
	p->sync(bar);
	CHECK(contentDeleted == beforePointerDelete + 1);
	CHECK(contentCreated == beforePointerCreate + 1);
	initializeContent();
	checkLive();
	p->sync(bar);
	CHECK(contentCreated == beforePointerCreate + 1);
	CHECK(contentDeleted == beforePointerDelete + 1);
	CHECK(p->callErrors().empty());
}
