// OpenBFME. GPL-3.0.
// See GameClient/ProjectedShadowManager.h (editions: both RotWK 2.01 and BFME2 1.06).

#include "GameClient/ProjectedShadowManager.h"

#include <algorithm>

ProjectedShadowManager *TheProjectedShadowManager = nullptr;

void Shadow::release()
{
	m_active = false;
	if (TheProjectedShadowManager)
	{
		TheProjectedShadowManager->removeShadow(this);
	}
}

void Shadow::setColor(std::uint32_t c)
{
	m_color = c;
}

void Shadow::setOpacity(int o)
{
	m_opacity = std::clamp(o, 0, 255);
}

ProjectedShadowManager::ProjectedShadowManager()
{
	if (!TheProjectedShadowManager)
	{
		TheProjectedShadowManager = this;
	}
}

ProjectedShadowManager::~ProjectedShadowManager()
{
	if (TheProjectedShadowManager == this)
	{
		TheProjectedShadowManager = nullptr;
	}
}

Shadow *ProjectedShadowManager::createEmitter(int type, const Shadow::ShadowTypeInfo *b, const Shadow::ShadowTypeInfo *c)
{
	auto shadow = std::make_unique<Shadow>();
	shadow->m_shadowType = type;
	if (b)
	{
		shadow->m_texture = b->m_first;
		shadow->m_child1.m_sizeX = b->m_floatC;
		shadow->m_child1.m_sizeY = b->m_float10;
	}
	if (c)
	{
		shadow->m_child2.m_sizeX = c->m_floatC;
		shadow->m_child2.m_sizeY = c->m_float10;
	}
	Shadow *ptr = shadow.get();
	m_shadows.push_back(std::move(shadow));
	return ptr;
}

void ProjectedShadowManager::removeShadow(Shadow *s)
{
	for (auto it = m_shadows.begin(); it != m_shadows.end(); ++it)
	{
		if (it->get() == s)
		{
			s->m_active = false;
			m_shadows.erase(it);
			break;
		}
	}
}

void ProjectedShadowManager::prune()
{
	m_shadows.erase(std::remove_if(m_shadows.begin(), m_shadows.end(),
		[](const std::unique_ptr<Shadow> &s) { return !s || !s->m_active; }),
		m_shadows.end());
}

