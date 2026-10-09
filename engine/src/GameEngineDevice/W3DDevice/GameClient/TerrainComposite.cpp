// OpenBFME. GPL-3.0.
// See TerrainComposite.h. Presentation data only: nothing here feeds the simulation.

#include "GameEngineDevice/W3DDevice/GameClient/TerrainComposite.h"

#include <algorithm>
#include <string>

float TerrainComposite::interpolateCorner(const float c[4], bool flip, float lx, float ly)
{
	// c[0..3] = SW, SE, NE, NW. The expressions are the barycentric interpolation over the triangle that
	// contains (lx, ly); on the shared diagonal both triangles give the same value.
	if (!flip)
	{
		// diagonal SW-NE (y == x)
		if (ly <= lx)
		{
			return c[0] + lx * (c[1] - c[0]) + ly * (c[2] - c[1]); // (SW, SE, NE)
		}
		return c[0] + ly * (c[3] - c[0]) + lx * (c[2] - c[3]); // (SW, NE, NW)
	}
	// diagonal SE-NW (x + y == 1)
	if (lx + ly <= 1.0f)
	{
		return c[0] + lx * (c[1] - c[0]) + ly * (c[3] - c[0]); // (SW, SE, NW)
	}
	return c[2] + (1.0f - ly) * (c[1] - c[2]) + (1.0f - lx) * (c[3] - c[2]); // (SE, NE, NW)
}

bool TerrainComposite::build(const std::vector<TerrainChunk> &chunks, std::vector<Chunk> &out, Records &records, Stats &stats, std::string *error)
{
	out.clear();
	records = Records();
	stats = Stats();
	std::vector<float> recs; // RECORD_TEXELS * 4 floats per record
	static const float kLocal[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };

	auto fail = [&](const std::string &m) {
		if (error)
		{
			*error = "terrain composite: " + m;
		}
		return false;
	};

	for (const TerrainChunk &tc : chunks)
	{
		const TerrainLayerMesh &L0 = tc.layer[0];
		const TerrainLayerMesh *Ln[2] = { &tc.layer[1], &tc.layer[2] };
		size_t next[2] = { 0, 0 };
		out.emplace_back();
		Chunk &oc = out.back();
		const size_t cells = L0.cellId.size();
		oc.position = L0.position;
		oc.normal = L0.normal;
		oc.uv = L0.uv;
		oc.wrap = L0.wrap;
		oc.index = L0.index;
		oc.color.resize(L0.color.size());
		for (size_t i = 0; i < L0.color.size(); i += 4)
		{
			oc.color[i] = L0.color[i];
			oc.color[i + 1] = L0.color[i + 1];
			oc.color[i + 2] = L0.color[i + 2];
			oc.color[i + 3] = 255;
		}
		oc.local.resize(L0.vertexCount() * 2);
		oc.record.assign(L0.vertexCount(), -1.0f);
		if (L0.cellFlip.size() != cells || L0.vertexCount() != cells * 4 || L0.index.size() != cells * 6)
		{
			return fail("base layer arrays are not whole cells");
		}
		for (size_t i = 0; i < cells; ++i)
		{
			for (size_t k = 0; k < 4; ++k)
			{
				oc.local[(i * 4 + k) * 2] = kLocal[k * 2];
				oc.local[(i * 4 + k) * 2 + 1] = kLocal[k * 2 + 1];
			}
			++stats.cells;
			bool has[2] = { false, false };
			size_t at[2] = { 0, 0 };
			for (int l = 0; l < 2; ++l)
			{
				const TerrainLayerMesh &L = *Ln[l];
				if (L.cellFlip.size() != L.cellId.size() || L.vertexCount() != L.cellId.size() * 4)
				{
					return fail("layer arrays are not whole cells");
				}
				if (next[l] < L.cellId.size())
				{
					if (L.cellId[next[l]] == L0.cellId[i])
					{
						has[l] = true;
						at[l] = next[l]++;
					}
					else if (L.cellId[next[l]] < L0.cellId[i])
					{
						return fail("layer " + std::to_string(l + 1) + " holds a cell the base layer does not (cell id " + std::to_string(L.cellId[next[l]]) + ")");
					}
				}
			}
			if (!has[0] && !has[1])
			{
				continue;
			}
			const float recordIndex = (float)stats.recordCells;
			++stats.recordCells;
			for (size_t k = 0; k < 4; ++k)
			{
				oc.record[i * 4 + k] = recordIndex;
			}
			const size_t base = recs.size();
			recs.resize(base + (size_t)RECORD_TEXELS * 4, 0.0f);
			for (int l = 0; l < 2; ++l)
			{
				if (!has[l])
				{
					continue;
				}
				const TerrainLayerMesh &L = *Ln[l];
				float *t = &recs[base + (size_t)l * 16];
				for (size_t k = 0; k < 4; ++k)
				{
					t[k * 2] = L.uv[(at[l] * 4 + k) * 2];
					t[k * 2 + 1] = L.uv[(at[l] * 4 + k) * 2 + 1];
					t[8 + k] = (float)L.color[(at[l] * 4 + k) * 4 + 3] / 255.0f;
				}
				// the wrap rect is the same on all four vertices of the cell
				for (int j = 0; j < 4; ++j)
				{
					t[12 + j] = L.wrap[at[l] * 16 + (size_t)j];
				}
				recs[base + 8 * 4 + (size_t)l] = L.cellFlip[at[l]] ? 1.0f : 0.0f;
				recs[base + 8 * 4 + 2 + (size_t)l] = 1.0f;
			}
			stats.layer1Cells += has[0];
			stats.layer2Cells += has[1];
			if (has[1] && Ln[1]->cellFlip[at[1]] != L0.cellFlip[i])
			{
				++stats.layer2FlipDiffers;
			}
		}
		if (next[0] != Ln[0]->cellId.size() || next[1] != Ln[1]->cellId.size())
		{
			return fail("a blend layer holds cells the base layer does not");
		}
	}

	records.count = stats.recordCells;
	records.width = RECORD_TEXELS * RECORDS_PER_ROW;
	records.height = (int)std::max<size_t>(1, (stats.recordCells + RECORDS_PER_ROW - 1) / RECORDS_PER_ROW);
	records.texels.assign((size_t)records.width * (size_t)records.height * 4, 0.0f);
	for (size_t r = 0; r < stats.recordCells; ++r)
	{
		const size_t row = r / RECORDS_PER_ROW, col = r % RECORDS_PER_ROW;
		for (size_t k = 0; k < (size_t)RECORD_TEXELS; ++k)
		{
			float *dst = &records.texels[((row * (size_t)records.width) + col * RECORD_TEXELS + k) * 4];
			const float *src = &recs[r * RECORD_TEXELS * 4 + k * 4];
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = src[3];
		}
	}
	return true;
}
