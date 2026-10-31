// Lane FOGFX-1: W3DShroud's fog level interpolation (RW 0x472CF2 / 0x472A96), see ShroudFade.h. Client only.
#include "GameClient/ShroudFade.h"

#include <cmath>

void ShroudFade::reset(int w, int h, std::uint8_t level)
{
	m_w = w > 0 ? w : 0;
	m_h = h > 0 ? h : 0;
	const size_t n = (size_t)m_w * (size_t)m_h;
	m_current.assign(n, level);
	m_final.assign(n, level);
	m_inDirty.assign(n, 0);
	m_dirty.clear();
	m_carryMs = 0.0;
}

void ShroudFade::setFinal(int x, int y, std::uint8_t level, std::uint8_t floor)
{
	if (x < 0 || y < 0 || x >= m_w || y >= m_h)
	{
		return; // RW 0x472A96: a cell outside the grid is ignored
	}
	if (level < floor)
	{
		level = floor;
	}
	const size_t i = (size_t)y * (size_t)m_w + (size_t)x;
	m_final[i] = level;
	if (m_current[i] != level && !m_inDirty[i])
	{
		m_inDirty[i] = 1;
		m_dirty.push_back((std::uint32_t)i);
	}
}

bool ShroudFade::step(double ms)
{
	if (m_dirty.empty())
	{
		m_carryMs = 0.0;
		return false;
	}
	m_carryMs += ms > 0.0 ? ms : 0.0;
	const double raw = std::floor(m_carryMs * (double)kLevelsPerMs);
	if (raw < 1.0)
	{
		return false;
	}
	const int stepLevels = raw > 255.0 ? 255 : (int)raw;
	// the milliseconds this step used (all of them once the step is capped at 255)
	m_carryMs = raw > 255.0 ? 0.0 : m_carryMs - raw / (double)kLevelsPerMs;
	bool moved = false;
	size_t keep = 0;
	for (size_t k = 0; k < m_dirty.size(); ++k)
	{
		const std::uint32_t i = m_dirty[k];
		const int cur = m_current[i], fin = m_final[i];
		if (cur != fin)
		{
			const int diff = fin > cur ? fin - cur : cur - fin;
			// RW 0x472CF2: less than a step away lands on the final level, else one step toward it
			m_current[i] = (std::uint8_t)(diff < stepLevels ? fin : (fin > cur ? cur + stepLevels : cur - stepLevels));
			moved = true;
		}
		if (m_current[i] != m_final[i])
		{
			m_dirty[keep++] = i;
		}
		else
		{
			m_inDirty[i] = 0;
		}
	}
	m_dirty.resize(keep);
	return moved;
}

void ShroudFade::snap()
{
	for (std::uint32_t i : m_dirty)
	{
		m_current[i] = m_final[i];
		m_inDirty[i] = 0;
	}
	m_dirty.clear();
	m_carryMs = 0.0;
}
