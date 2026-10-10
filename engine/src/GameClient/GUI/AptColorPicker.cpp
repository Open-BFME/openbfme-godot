// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptColorPicker.h (lane CAH-2).

#include "GameClient/GUI/AptColorPicker.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
const char *const kSymbol = "ColorPicker"; // RW 0xC4FD0C

// "_level3.Main.x" -> level path "Main.x" (the window manager's invokes are relative to a level root)
std::string belowLevel(const std::string &target)
{
	if (target.rfind("_level", 0) != 0)
	{
		return target;
	}
	const std::size_t dot = target.find('.');
	return dot == std::string::npos ? std::string() : target.substr(dot + 1);
}
} // namespace

AptColorPickers::AptColorPickers(WindowManager &windows, const MappedImageCollection &images, AptFileSource &source)
	: m_windows(windows), m_images(images), m_textures(std::make_unique<AptTextureStore>(source))
{
}

AptColorPickers::~AptColorPickers()
{
	for (const Picker &p : m_pickers)
	{
		for (const std::string &e : p.externs)
		{
			m_windows.unregisterProvider(e);
		}
	}
	if (m_registered)
	{
		m_windows.unregisterComponent(kSymbol);
	}
}

void AptColorPickers::registerComponent()
{
	if (m_registered)
	{
		return;
	}
	m_registered = m_windows.registerComponent(kSymbol, [this](AptComponentRequest &r) -> std::shared_ptr<GameWindow> {
		// RW 0x814E3A makes the object on the first draw; here the instance is remembered and made by update() once its script set `_componentId`;
		// a clip placed again at the same path (the page shown again) takes its old record, whose externs stay registered
		for (Picker &old : m_pickers)
		{
			if (old.instancePath == r.instancePath && old.level == r.level)
			{
				old.initialised = false;
				old.cursorDirty = old.colorDirty = false;
				return nullptr;
			}
		}
		Picker p;
		p.instancePath = r.instancePath;
		p.level = r.level;
		m_pickers.push_back(std::move(p));
		return nullptr; // no gadget window: the device draws the clip's image (GodotAptPlayer), this object answers the externs
	});
}

bool AptColorPickers::texelAt(const std::vector<std::uint8_t> &rgba, int w, int h, int x, int y, std::uint32_t &argb)
{
	if (x < 0 || y < 0 || x >= w || y >= h || rgba.size() < (std::size_t)w * (std::size_t)h * 4)
	{
		return false;
	}
	const std::uint8_t *t = rgba.data() + ((std::size_t)y * (std::size_t)w + (std::size_t)x) * 4;
	argb = ((std::uint32_t)t[3] << 24) | ((std::uint32_t)t[0] << 16) | ((std::uint32_t)t[1] << 8) | (std::uint32_t)t[2];
	return true;
}

bool AptColorPickers::nearestTexel(const std::vector<std::uint8_t> &rgba, int w, int h, int x0, int y0, int iw, int ih, std::uint32_t argb, int &bx, int &by)
{
	// RW 0xB555A0 .. 0xB555F9 with RW 0xB5500E: the smallest sum of squared byte differences, the first one (strictly smaller) row by row; the start
	// 0x7FFFFFFF and the place (0, 0)
	std::uint32_t best = 0x7FFFFFFFu;
	bx = 0;
	by = 0;
	const int want[4] = { (int)((argb >> 16) & 255u), (int)((argb >> 8) & 255u), (int)(argb & 255u), (int)(argb >> 24) };
	bool any = false;
	for (int y = 0; y < ih; ++y)
	{
		for (int x = 0; x < iw; ++x)
		{
			const int tx = x0 + x, ty = y0 + y;
			if (tx < 0 || ty < 0 || tx >= w || ty >= h)
			{
				continue;
			}
			const std::uint8_t *t = rgba.data() + ((std::size_t)ty * (std::size_t)w + (std::size_t)tx) * 4;
			std::uint32_t d = 0;
			for (int c = 0; c < 4; ++c)
			{
				const int diff = (int)t[c] - want[c];
				d += (std::uint32_t)(diff * diff);
			}
			if (d < best)
			{
				best = d;
				bx = x;
				by = y;
				any = true;
			}
		}
	}
	return any;
}

bool AptColorPickers::pixels(const Image &image, const std::vector<std::uint8_t> *&rgba, int &w, int &h)
{
	const AptTextureStore::Entry &e = m_textures->get(image.filename);
	if (!e.ok || e.rgba.empty())
	{
		m_windows.note("color-picker", "the texture '" + image.filename + "' of image '" + image.name + "' has no pixels: " + e.error);
		return false;
	}
	rgba = &e.rgba;
	w = e.width;
	h = e.height;
	return true;
}

bool AptColorPickers::init(Picker &p)
{
	// RW 0xB5570B
	AptSpriteInst *root = m_windows.apt().level(p.level);
	AptCharacterInst *inst = root ? m_windows.apt().resolvePath(root, belowLevel(p.instancePath)) : nullptr;
	if (!inst)
	{
		return false;
	}
	AptValue id, image, path;
	if (!inst->getMember("_componentId", id) || id.toString().empty())
	{
		return false; // the clip's script has not run yet
	}
	p.componentId = id.toString();
	if (inst->getMember("_imageName", image))
	{
		p.imageName = image.toString();
	}
	if (inst->getMember("_path", path))
	{
		p.path = path.toString();
	}
	p.image = m_images.findImageByName(p.imageName);
	if (!p.image)
	{
		m_windows.note("color-picker", p.instancePath + ": no mapped image '" + p.imageName + "' (RW 0x6DA34C answered none: nothing is drawn or picked)");
	}
	p.initialised = true;
	Picker *self = &p;
	const std::size_t index = (std::size_t)(self - m_pickers.data());
	auto picker = [this, index]() -> Picker * { return index < m_pickers.size() ? &m_pickers[index] : nullptr; };
	// RW 0xB5505B case 0: Cursor
	const std::string cursorName = p.componentId + "Cursor";
	if (m_windows.registerProvider(cursorName, [picker](const std::string &, std::string &value, bool setting) {
		    Picker *q = picker();
		    if (!q)
		    {
			    return false;
		    }
		    if (setting)
		    {
			    float x = 0.0f, y = 0.0f;
			    std::sscanf(value.c_str(), "%f %f", &x, &y); // RW 0xD0A19C
			    q->cursor[0] = x;
			    q->cursor[1] = y;
			    q->cursorDirty = true;
			    return true;
		    }
		    char buf[64];
		    std::snprintf(buf, sizeof(buf), "%f %f", (double)q->cursor[0], (double)q->cursor[1]);
		    value = buf;
		    return true;
	    }))
	{
		p.externs.push_back(cursorName);
	}
	// RW 0xB5505B case 1: Color ("%u", RW 0xBD41E8)
	const std::string colorName = p.componentId + "Color";
	if (m_windows.registerProvider(colorName, [picker](const std::string &, std::string &value, bool setting) {
		    Picker *q = picker();
		    if (!q)
		    {
			    return false;
		    }
		    if (setting)
		    {
			    q->color = (std::uint32_t)std::strtoul(value.c_str(), nullptr, 10);
			    q->colorDirty = true;
			    return true;
		    }
		    value = std::to_string(q->color);
		    return true;
	    }))
	{
		p.externs.push_back(colorName);
	}
	return true;
}

void AptColorPickers::pick(Picker &p, const float rect[4])
{
	// RW 0xB55329 .. 0xB5546B
	p.cursorDirty = false;
	const std::vector<std::uint8_t> *rgba = nullptr;
	int w = 0, h = 0;
	if (!p.image || !pixels(*p.image, rgba, w, h))
	{
		return;
	}
	const float rw = rect[2] - rect[0], rh = rect[3] - rect[1];
	const float px = rw != 0.0f ? (float)p.image->imageSize.x / rw * p.cursor[0] : 0.0f;
	const float py = rh != 0.0f ? (float)p.image->imageSize.y / rh * p.cursor[1] : 0.0f;
	const int tx = (int)((float)p.image->textureSize.x * p.image->uvLo[0] + px); // cvttss2si
	const int ty = (int)((float)p.image->textureSize.y * p.image->uvLo[1] + py);
	std::uint32_t argb = 0;
	if (!texelAt(*rgba, w, h, tx, ty, argb))
	{
		return;
	}
	p.color = argb;
	std::string error;
	if (!m_windows.invokeASAtValues(p.level, belowLevel(p.path), "SetColor", { AptValue::string(std::to_string(argb)) }, &error)) // RW 0x622574: "%u"
	{
		m_windows.note("color-picker", p.path + ".SetColor: " + error);
	}
}

void AptColorPickers::place(Picker &p, const float rect[4])
{
	// RW 0xB5548C .. 0xB556D1
	p.colorDirty = false;
	const std::vector<std::uint8_t> *rgba = nullptr;
	int w = 0, h = 0;
	if (!p.image || !pixels(*p.image, rgba, w, h))
	{
		return;
	}
	const int x0 = (int)std::floor((float)p.image->textureSize.x * p.image->uvLo[0] + 0.5f); // RW 0xB55509 .. : floor(size * uv + 0.5)
	const int y0 = (int)std::floor((float)p.image->textureSize.y * p.image->uvLo[1] + 0.5f);
	int bx = 0, by = 0;
	nearestTexel(*rgba, w, h, x0, y0, p.image->imageSize.x, p.image->imageSize.y, p.color, bx, by);
	const float rw = rect[2] - rect[0], rh = rect[3] - rect[1];
	const float cx = p.image->imageSize.x ? (float)bx * rw / (float)p.image->imageSize.x : 0.0f;
	const float cy = p.image->imageSize.y ? (float)by * rh / (float)p.image->imageSize.y : 0.0f;
	p.cursor[0] = std::floor(cx + 0.5f);
	p.cursor[1] = std::floor(cy + 0.5f);
	std::string error;
	if (!m_windows.invokeASAtValues(p.level, belowLevel(p.path), "SetCursor", { AptValue::string(std::to_string((int)p.cursor[0])), AptValue::string(std::to_string((int)p.cursor[1])) }, &error)) // the rounded ints
	{
		m_windows.note("color-picker", p.path + ".SetCursor: " + error);
	}
}

void AptColorPickers::update()
{
	for (std::size_t i = 0; i < m_pickers.size(); ++i)
	{
		Picker &p = m_pickers[i];
		AptSpriteInst *root = m_windows.apt().level(p.level);
		AptCharacterInst *inst = root ? m_windows.apt().resolvePath(root, belowLevel(p.instancePath)) : nullptr;
		if (!inst)
		{
			continue;
		}
		if (!p.initialised && !init(p))
		{
			continue;
		}
		float rect[4] = { 0, 0, 0, 0 };
		if (!WindowManager::stageBounds(*inst, rect))
		{
			continue;
		}
		if (p.cursorDirty)
		{
			pick(m_pickers[i], rect);
		}
		if (m_pickers[i].colorDirty)
		{
			place(m_pickers[i], rect);
		}
	}
}
