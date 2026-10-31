#include "doctest.h"
#include "GameClient/AptCanvas.h"

#include <array>

TEST_CASE("shell backdrop: aspect fit follows resolution changes without changing the menu mapping")
{
	struct Resolution
	{
		float width, height, left, top, right, bottom;
	};
	const std::array<Resolution, 8> resolutions = {{
		{ 1024, 768, 0, 0, 1024, 768 },
		{ 1280, 720, 160, 0, 1120, 720 },
		{ 1920, 1080, 240, 0, 1680, 1080 },
		{ 2560, 1440, 320, 0, 2240, 1440 },
		{ 3440, 1440, 760, 0, 2680, 1440 },
		{ 1280, 1024, 0, 32, 1280, 992 },
		{ 768, 1024, 0, 224, 768, 800 },
		{ 1280, 720, 160, 0, 1120, 720 },
	}};
	AptStageMapping menu;
	for (const Resolution &resolution : resolutions)
	{
		CAPTURE(resolution.width);
		CAPTURE(resolution.height);
		menu.windowW = resolution.width;
		menu.windowH = resolution.height;
		const AptStageMapping backdrop = menu.fitImage(1024, 768);
		float left, top, right, bottom;
		backdrop.stageToWindow(0, 0, left, top);
		backdrop.stageToWindow(1024, 768, right, bottom);
		CHECK(left == resolution.left);
		CHECK(top == resolution.top);
		CHECK(right == resolution.right);
		CHECK(bottom == resolution.bottom);
		CHECK(backdrop.scaleX() == backdrop.scaleY());
		CHECK(menu.mode == AptStageMapping::Mode::Stretch);
		float menuRight, menuBottom;
		menu.stageToWindow(1024, 768, menuRight, menuBottom);
		CHECK(menuRight == resolution.width);
		CHECK(menuBottom == resolution.height);
	}
}

TEST_CASE("shell backdrop: fit uses the mapped artwork dimensions independently of the movie stage")
{
	AptStageMapping menu;
	menu.stageW = 800;
	menu.stageH = 600;
	menu.windowW = 1280;
	menu.windowH = 720;
	for (AptStageMapping::Mode mode : { AptStageMapping::Mode::Stretch, AptStageMapping::Mode::Fit })
	{
		menu.mode = mode;
		const AptStageMapping backdrop = menu.fitImage(2048, 1024);
		CHECK(backdrop.stageW == 2048);
		CHECK(backdrop.stageH == 1024);
		CHECK(backdrop.scaleX() == 0.625f);
		CHECK(backdrop.scaleY() == 0.625f);
		CHECK(backdrop.offsetX() == 0);
		CHECK(backdrop.offsetY() == 40);
		CHECK(menu.stageW == 800);
		CHECK(menu.stageH == 600);
		CHECK(menu.mode == mode);
	}
}
