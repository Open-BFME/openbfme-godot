// OpenBFME. GPL-3.0.
// See AptButtonInst.h for the citations.

#include "Libraries/Source/Apt/AptButtonInst.h"

#include "Libraries/Source/Apt/Apt.h"

#include <algorithm>
#include <cmath>

AptButtonInst::AptButtonInst(Apt &apt) : AptCharacterInst(apt, Type::Button)
{
	props = AptPropertyMap(4); // 0x00AF85B7: the button base's hash has 4 slots (a sprite's has 8)
}

void AptButtonInst::setup(const AptCharRef &ref)
{
	m_char = ref;
	m_info = ref.character ? ref.character->button.get() : nullptr;
}

void AptButtonInst::rebuild()
{
	// 0x00AE1F00: the previous records are released, then every record whose state mask has the state bit is placed.
	// The binary passes the record index as the placement depth (0x00AE1F64 `push edi`), so the records keep file order.
	for (AptCharacterInst *c : m_children)
	{
		c->destroy(false);
	}
	m_children.clear();
	if (!m_info)
	{
		return;
	}
	const std::uint32_t bit = (std::uint32_t)m_state;
	for (std::size_t i = 0; i < m_info->records.size(); ++i)
	{
		const AptButtonRecord &rec = m_info->records[i];
		if (!(rec.stateMask & bit))
		{
			continue;
		}
		AptCharRef ref;
		std::string error;
		if (!m_apt.resolveCharacter(m_char.file, rec.characterId, ref, &error))
		{
			m_apt.vm().reportError("button record " + std::to_string(i) + ": character " + std::to_string(rec.characterId) + " cannot be resolved: " + error);
			continue;
		}
		AptCharacterInst *child = m_apt.createInstance(ref);
		child->m_depth = (int)i;
		child->m_parent = this;
		child->matrix.a = rec.matrix[0];
		child->matrix.b = rec.matrix[1];
		child->matrix.c = rec.matrix[2];
		child->matrix.d = rec.matrix[3];
		child->matrix.tx = rec.translation[0];
		child->matrix.ty = rec.translation[1];
		for (int k = 0; k < 4; ++k)
		{
			child->color.mul[k] = rec.color[k];
		}
		m_children.push_back(child);
		if (child->type() == Type::Sprite)
		{
			m_apt.noteNewInstance(child);
		}
	}
}

void AptButtonInst::setState(State state)
{
	if (state == m_state)
	{
		return; // 0x00AE1F11
	}
	m_state = state;
	rebuild();
}

void AptButtonInst::advance()
{
	// button arm of AptCIH advance (0x00AE2E90): only the children advance
	std::vector<AptCharacterInst *> snapshot = m_children;
	for (AptCharacterInst *c : snapshot)
	{
		if (!c->defined())
		{
			continue;
		}
		if (AptSpriteInst *s = c->asSprite())
		{
			s->advance();
		}
	}
}

bool AptButtonInst::contentBounds(float &x0, float &y0, float &x1, float &y1) const
{
	if (m_info)
	{
		x0 = m_info->bounds[0];
		y0 = m_info->bounds[1];
		x1 = m_info->bounds[2];
		y1 = m_info->bounds[3];
		return true;
	}
	return false;
}

void AptButtonInst::trace(AptGC &gc)
{
	AptCharacterInst::trace(gc);
	for (AptCharacterInst *c : m_children)
	{
		gc.mark(c);
	}
}

void AptButtonInst::destroy(bool fireUnload)
{
	if (!beginDestroy())
	{
		return;
	}
	for (AptCharacterInst *c : m_children)
	{
		c->destroy(fireUnload);
	}
	m_children.clear();
	AptCharacterInst::destroy(fireUnload);
}

bool AptButtonInst::hitTest(float stageX, float stageY) const
{
	// AptInput 0x00AFA420: every Hit record (state mask 8) places the button's hit mesh (a shape record) or its text bounds
	// (an edit text record) with the record matrix under the button's own matrix; the first record containing the point wins
	if (!m_info)
	{
		return false;
	}
	const AptMatrix global = globalMatrix();
	for (const AptButtonRecord &rec : m_info->records)
	{
		if (!(rec.stateMask & 8))
		{
			continue;
		}
		AptMatrix recM;
		recM.a = rec.matrix[0];
		recM.b = rec.matrix[1];
		recM.c = rec.matrix[2];
		recM.d = rec.matrix[3];
		recM.tx = rec.translation[0];
		recM.ty = rec.translation[1];
		bool ok = false;
		AptMatrix inv = global.concat(recM).inverse(&ok);
		if (!ok)
		{
			continue;
		}
		float lx, ly;
		inv.apply(stageX, stageY, lx, ly);
		AptCharRef ref;
		std::string error;
		if (!m_apt.resolveCharacter(m_char.file, rec.characterId, ref, &error))
		{
			continue;
		}
		if (ref.character->type == APT_CHAR_SHAPE)
		{
			const std::vector<float> &v = m_info->vertices;
			if (m_info->triangles.empty() || v.empty())
			{
				// no mesh in the button: the shape's bounds
				if (lx >= ref.character->bounds[0] && lx <= ref.character->bounds[2] && ly >= ref.character->bounds[1] && ly <= ref.character->bounds[3])
				{
					return true;
				}
				continue;
			}
			for (std::size_t t = 0; t + 2 < m_info->triangles.size(); t += 3)
			{
				std::size_t i0 = m_info->triangles[t], i1 = m_info->triangles[t + 1], i2 = m_info->triangles[t + 2];
				if (i0 * 2 + 1 >= v.size() || i1 * 2 + 1 >= v.size() || i2 * 2 + 1 >= v.size())
				{
					continue;
				}
				float ax = v[i0 * 2], ay = v[i0 * 2 + 1], bx = v[i1 * 2], by = v[i1 * 2 + 1], cx = v[i2 * 2], cy = v[i2 * 2 + 1];
				auto sign = [](float px, float py, float qx, float qy, float rx, float ry) { return (px - rx) * (qy - ry) - (qx - rx) * (py - ry); };
				float d1 = sign(lx, ly, ax, ay, bx, by);
				float d2 = sign(lx, ly, bx, by, cx, cy);
				float d3 = sign(lx, ly, cx, cy, ax, ay);
				bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
				bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
				if (!(neg && pos))
				{
					return true;
				}
			}
		}
		else if (ref.character->type == APT_CHAR_EDITTEXT && ref.character->text)
		{
			const float *b = ref.character->text->bounds;
			if (lx >= b[0] && lx <= b[2] && ly >= b[1] && ly <= b[3])
			{
				return true;
			}
		}
	}
	return false;
}
