// OpenBFME. GPL-3.0.
// See GameClient/FloatingText.h.

#include "GameClient/FloatingText.h"

#include "GameClient/CommandButtonHelp.h"
#include "GameClient/DrawableIconUI.h"

bool FloatingTextSettings::load(ArchiveFileSystem &fs, FloatingTextSettings &out, std::string *error)
{
	FloatingTextSettings parsed;
	if (!loadInGameUiUnsigned(&fs, nullptr, "FloatingTextTimeOut", parsed.timeOutMs, error) ||
		!loadInGameUiReal(&fs, nullptr, "FloatingTextMoveUpSpeed", parsed.moveUpSpeed, error) ||
		!loadInGameUiReal(&fs, nullptr, "FloatingTextVanishRate", parsed.vanishRate, error))
	{
		return false;
	}
	out = parsed; // Keep milliseconds and raw speeds here; client-frame conversions happen below (S-283).
	return true;
}

void FloatingTextList::add(const std::string &text, const Coord3D &pos, std::uint32_t color, unsigned frame)
{
	if (!m_enabled) return;
	Record r;
	r.text = text;
	r.color = color;
	r.pos = pos;
	r.frameCount = 0;
	r.addedFrame = frame;
	r.frameTimeOut = m_settings.timeOutMs == 0 ? frame + (unsigned)(kClientFramesPerSecond / 3.0f)
	                                           : frame + (unsigned)(0.03f * (float)m_settings.timeOutMs); // BfmeClientFramesPerMillisecond
	m_records.push_front(std::move(r));
	++m_added;
}

void FloatingTextList::update(unsigned frame)
{
	if (!m_haveLastFrame)
	{
		// InGameUI_updateFloatingText.cpp: the static last frame starts at the first call's frame, which steps nothing
		m_haveLastFrame = true;
		m_lastFrame = frame;
		return;
	}
	if (frame <= m_lastFrame)
	{
		return;
	}
	const unsigned first = frame - m_lastFrame > kMaxStepFrames ? frame - kMaxStepFrames + 1 : m_lastFrame + 1;
	m_lastFrame = frame;
	for (unsigned step = first; step <= frame; ++step)
	{
		// one retail update at client frame `step`
		for (auto it = m_records.begin(); it != m_records.end();)
		{
			Record &r = *it;
			if (step <= r.addedFrame)
			{
				++it;
				continue;
			}
			++r.frameCount;
			if (step > r.frameTimeOut)
			{
				int a = (int)(r.color >> 24);
				const int amount = (int)(m_settings.vanishRate / kClientFramesPerSecond * (float)(step - r.frameTimeOut));
				a = a - amount < 0 ? 0 : a - amount;
				r.color = (r.color & 0xFFFFFFu) | ((std::uint32_t)a << 24);
				if (a <= 0)
				{
					it = m_records.erase(it);
					continue;
				}
			}
			++it;
		}
	}
}

void FloatingTextList::ops(const std::function<bool(const Coord3D &, float &, float &)> &project, const std::function<bool(const Coord3D &)> &visible,
	std::vector<IconUIOp> &out) const
{
	for (const Record &r : m_records)
	{
		float x = 0, y = 0;
		if (!project(r.pos, x, y) || !visible(r.pos))
		{
			continue;
		}
		IconUIOp op;
		op.kind = IconUIOp::TEXT;
		op.x = x; // a TEXT op's x is its centre (the device takes width / 2 off)
		op.y = y - (float)r.frameCount * m_settings.moveUpSpeed / kClientFramesPerSecond;
		op.color = r.color;
		op.dropColor = r.color & 0xFF000000u; // black with the text's alpha
		op.text = r.text;
		op.stageTextSize = kStageTextSize;
		out.push_back(std::move(op));
	}
}

std::vector<std::string> FloatingTextList::acceptanceStops()
{
	return { "[S-283] floating text: ported GUI:AddCash from the income log (InGameUIFloatingText.cpp, InGameUI_updateFloatingText.cpp, "
			 "InGameUI_drawFloatingText.cpp); INFERENCE: FloatingTextTimeOut is milliseconds and FloatingTextMoveUpSpeed / FloatingTextVanishRate are taken "
			 "as the INI writes them (the parsers' conversions were not read), and both divisions use 30 client frames a second (GameEngine + 0x38 not read); "
			 "TARGET FACT: the caption font is InGameUI.ini's DrawableCaptionFont = Albertus MT, DrawableCaptionPointSize = 12, bold No (both editions), drawn "
			 "at 12 stage pixels scaled with the window (the device loads Albertus MT; Godot's fallback font only when no shell is attached)" };
}
