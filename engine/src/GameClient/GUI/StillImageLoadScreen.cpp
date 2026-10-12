// OpenBFME. GPL-3.0.
// See GameClient/GUI/StillImageLoadScreen.h.

#include "GameClient/GUI/StillImageLoadScreen.h"

#include "GameClient/GUI/Image.h"

#include <cstdio>

LoadScreenChoice chooseLoadScreen(int gameMode, int screenKind, bool loadingSaveGame, const std::string &picture, const std::function<int(int, int)> &clientRandom)
{
	LoadScreenChoice c;
	auto still = [&](bool pictureAllowed) {
		c.kind = LoadScreenKind::StillImage;
		if (pictureAllowed && !picture.empty())
		{
			c.picture = picture;
			return;
		}
		// RW 0x626362: GetGameClientRandomValue(1, 21) kept in 1 .. 21, "StillImage_Film%02d"
		if (!clientRandom)
		{
			c.error = "a film still (StillImage_Film01 .. 21) needs the client random source, and none was given";
			return;
		}
		int v = clientRandom(1, 21);
		v = v < 1 ? 1 : (v > 21 ? 21 : v);
		char buf[32];
		std::snprintf(buf, sizeof(buf), "StillImage_Film%02d", v);
		c.picture = buf;
	};
	switch (gameMode)
	{
		case 0:
		case 3:
		case 6:
			still(!loadingSaveGame && gameMode != 3);
			break;
		case 1:
		case 2:
		case 5:
			if (screenKind == 0)
			{
				still(!loadingSaveGame);
			}
			else
			{
				c.kind = LoadScreenKind::Players;
			}
			break;
		default:
			c.kind = LoadScreenKind::Other;
			break;
	}
	return c;
}

bool layoutStillImageLoadScreen(const MappedImageCollection &images, const std::string &picture, StillImageLoadScreenLayout &out, std::string *error)
{
	out = StillImageLoadScreenLayout();
	if (!images.findImageByName("LoadScreenForeground"))
	{
		if (error)
		{
			*error = "the mapped image LoadScreenForeground is not in the collection";
		}
		return false;
	}
	out.slots[2] = StillImageSlot{ "LoadScreenForeground", 0.0f, 0.0f, 1.0f, 1.0f, true };
	if (picture.empty())
	{
		if (error)
		{
			*error = "the picture load screen has no picture (getLoadScreen always gives one, RW 0x626A87)";
		}
		return false;
	}
	{
		const Image *img = images.findImageByName(picture);
		if (!img)
		{
			if (error)
			{
				*error = "the load screen picture '" + picture + "' is not a mapped image";
			}
			return false;
		}
		// RW 0x758461: y1 = height * (1 / 768) + 0.1640625 (SSE single precision, as here)
		const float top = 0.1640625f;
		out.slots[0] = StillImageSlot{ picture, 0.0f, top, 1.0f, (float)img->getImageHeight() * 0.0013020833721384406f + top, true };
	}
	return true;
}
