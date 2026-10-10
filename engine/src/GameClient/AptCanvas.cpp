// OpenBFME. GPL-3.0.
// See AptCanvas.h for the model and the sources.

#include "GameClient/AptCanvas.h"

#include <algorithm>
#include <cctype>
#include <cmath>

// ---------------------------------------------------------------------------------------------------------------------------------
// stage <-> window
// ---------------------------------------------------------------------------------------------------------------------------------
float AptStageMapping::scaleX() const
{
	if (mode == Mode::Fit)
	{
		return uniformScale();
	}
	return stageW > 0 ? windowW / stageW : 1.0f; // donor: 1.0 when the stage width is 0
}

float AptStageMapping::scaleY() const
{
	if (mode == Mode::Fit)
	{
		return uniformScale();
	}
	return stageH > 0 ? windowH / stageH : 1.0f;
}

float AptStageMapping::uniformScale() const
{
	const float sx = stageW > 0 ? windowW / stageW : 1.0f;
	const float sy = stageH > 0 ? windowH / stageH : 1.0f;
	return std::min(sx, sy);
}

float AptStageMapping::offsetX() const
{
	return mode == Mode::Fit ? (windowW - stageW * uniformScale()) * 0.5f : 0.0f;
}

float AptStageMapping::offsetY() const
{
	return mode == Mode::Fit ? (windowH - stageH * uniformScale()) * 0.5f : 0.0f;
}

void AptStageMapping::stageToWindow(float x, float y, float &wx, float &wy) const
{
	wx = x * scaleX() + offsetX();
	wy = y * scaleY() + offsetY();
}

void AptStageMapping::windowToStage(float wx, float wy, float &x, float &y) const
{
	const float sx = scaleX(), sy = scaleY();
	x = sx != 0 ? (wx - offsetX()) / sx : 0.0f;
	y = sy != 0 ? (wy - offsetY()) / sy : 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// textures
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
std::string lowerAscii(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}
} // namespace

const AptTextureStore::Entry &AptTextureStore::get(const std::string &name)
{
	const std::string key = lowerAscii(name);
	auto it = m_entries.find(key);
	if (it != m_entries.end())
	{
		return it->second;
	}
	Entry entry;
	std::vector<std::uint8_t> bytes;
	std::string error;
	std::string path = "art/textures/" + name;
	bool found = m_source.readFile(path, bytes, &error);
	if (!found && name.size() > 2)
	{
		// the packed textures of the install (the gadget skins' AptComponents_001.tga): art\compiledtextures\<first two letters>\<name> (RotWK Textures1.big layout)
		const std::string compiled = "art/compiledtextures/" + lowerAscii(name.substr(0, 2)) + "/" + name;
		std::string error2;
		if (m_source.readFile(compiled, bytes, &error2))
		{
			path = compiled;
			found = true;
		}
	}
	if (!found)
	{
		entry.error = path + ": " + error;
	}
	else
	{
		decodeEntry(path, bytes, entry);
	}
	return m_entries.emplace(key, std::move(entry)).first->second;
}

const AptTextureStore::Entry &AptTextureStore::getFile(const std::string &path)
{
	std::string slashed = path;
	std::replace(slashed.begin(), slashed.end(), '\\', '/');
	const std::string key = "file:" + lowerAscii(slashed);
	auto it = m_entries.find(key);
	if (it != m_entries.end())
	{
		return it->second;
	}
	Entry entry;
	std::vector<std::uint8_t> bytes;
	std::string error;
	if (!m_source.readFile(slashed, bytes, &error))
	{
		entry.error = slashed + ": " + error;
	}
	else
	{
		decodeEntry(slashed, bytes, entry);
	}
	return m_entries.emplace(key, std::move(entry)).first->second;
}

void AptTextureStore::decodeEntry(const std::string &path, const std::vector<std::uint8_t> &bytes, Entry &entry)
{
	std::string error;
	{
		TGAImage image;
		// the atlases are up to 1024 wide and 2048 high in the corpus; 4096 is far above any of them
		if (!TGAFile::decode(bytes.data(), bytes.size(), image, &error, 4096))
		{
			entry.error = path + ": " + error;
		}
		else
		{
			entry.ok = true;
			entry.width = image.width;
			entry.height = image.height;
			if (!image.topDownFlag)
			{
				// bottom-up file: flip so that row 0 is the top (S-134)
				const std::size_t row = (std::size_t)image.width * 4;
				for (int y = 0; y < image.height / 2; ++y)
				{
					std::swap_ranges(image.rgba.begin() + (std::size_t)y * row, image.rgba.begin() + (std::size_t)(y + 1) * row, image.rgba.begin() + (std::size_t)(image.height - 1 - y) * row);
				}
			}
			entry.rgba = std::move(image.rgba);
		}
	}
}

void AptTextureStore::releasePixels(const std::string &name)
{
	auto it = m_entries.find(lowerAscii(name));
	if (it != m_entries.end())
	{
		it->second.rgba.clear();
		it->second.rgba.shrink_to_fit();
		it->second.pixelsReleased = true;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// canvas
// ---------------------------------------------------------------------------------------------------------------------------------
std::size_t AptCanvasList::count(AptCanvasOp::Kind kind) const
{
	std::size_t n = 0;
	for (const AptCanvasOp &op : ops)
	{
		if (op.kind == kind)
		{
			++n;
		}
	}
	return n;
}

namespace
{

struct Mapper
{
	const AptStageMapping &m;
	// shape space -> window
	AptMatrix total;
	void apply(float x, float y, float &ox, float &oy) const
	{
		float sx, sy;
		total.apply(x, y, sx, sy);
		m.stageToWindow(sx, sy, ox, oy);
	}
};

void pushVertex(AptCanvasOp &op, const Mapper &map, float x, float y, const float color[4], const AptRenderFill *textured, float texW, float texH)
{
	float wx, wy;
	map.apply(x, y, wx, wy);
	op.positions.push_back(wx);
	op.positions.push_back(wy);
	op.colors.insert(op.colors.end(), color, color + 4);
	if (textured)
	{
		const float *uv = textured->uv;
		op.uvs.push_back((uv[0] * x + uv[2] * y + uv[4]) / texW);
		op.uvs.push_back((uv[1] * x + uv[3] * y + uv[5]) / texH);
	}
}

bool sameState(const AptCanvasOp &a, const AptCanvasOp &b)
{
	return a.kind == AptCanvasOp::Kind::Mesh && b.kind == AptCanvasOp::Kind::Mesh && a.texture == b.texture && a.maskShape == b.maskShape;
}

} // namespace

std::uint32_t AptRetailVertexColour(const float rgba[4], const AptColorTransform &c)
{
	constexpr float kByte = 1.0f / 255.0f; // RW 0xBD1920 (0x3B808081)
	auto channel = [&](int i) -> std::uint32_t {
		float t = rgba[i] * kByte;
		t = t * c.mul[i];
		t = t + c.add[i] * kByte; // retail keeps the additive term as byte / 255 (BFME2 0x00AF7160)
		t = t * 255.0f;           // RW 0xBD88A8
		// _ftol (RW 0xA3CFA4) truncates toward zero; a value outside int range (or NaN) is the integer indefinite 0x80000000
		if (!(t > -2147483648.0f && t < 2147483648.0f))
		{
			return 0x80000000u;
		}
		return (std::uint32_t)(std::int32_t)t;
	};
	// RW 0x4A8AD5's evaluation and packing order: R, then A << 8 ORed in, << 8, G ORed in, << 8, B ORed in
	std::uint32_t packed = channel(0);
	packed |= channel(3) << 8;
	packed <<= 8;
	packed |= channel(1);
	packed <<= 8;
	packed |= channel(2);
	return packed;
}

void AptRetailVertexColour(const float rgba[4], const AptColorTransform &c, float out[4])
{
	const std::uint32_t argb = AptRetailVertexColour(rgba, c);
	out[0] = (float)((argb >> 16) & 0xFFu) / 255.0f;
	out[1] = (float)((argb >> 8) & 0xFFu) / 255.0f;
	out[2] = (float)(argb & 0xFFu) / 255.0f;
	out[3] = (float)(argb >> 24) / 255.0f;
}

void BuildAptCanvas(const AptRenderList &list, const AptCanvasInputs &in, AptCanvasList &out, bool merge)
{
	out.ops.clear();
	out.errors = list.errors;
	out.missingLabels.clear();
	out.fontSubstitutions.clear();
	out.triangles = 0;
	std::set<std::string> unverified(list.unverified.begin(), list.unverified.end());
	unverified.insert("stage-mapping"); // S-130: every canvas is placed by a mapping whose BFME2 / RotWK code was not read
	std::set<std::string> missing, substitutions, textureErrors;
	std::vector<bool> maskShapePhase; // one entry per open mask: still defining the mask shape
	AptCanvasOp *last = nullptr;

	auto push = [&](AptCanvasOp &&op) {
		if (merge && last && sameState(*last, op))
		{
			AptCanvasOp &t = *last;
			t.positions.insert(t.positions.end(), op.positions.begin(), op.positions.end());
			t.colors.insert(t.colors.end(), op.colors.begin(), op.colors.end());
			t.uvs.insert(t.uvs.end(), op.uvs.begin(), op.uvs.end());
			return;
		}
		out.ops.push_back(std::move(op));
		last = &out.ops.back();
	};

	for (const AptRenderCommand &cmd : list.commands)
	{
		switch (cmd.kind)
		{
			case AptRenderCommand::Kind::MaskBegin:
			{
				AptCanvasOp op;
				op.kind = AptCanvasOp::Kind::MaskBegin;
				op.path = cmd.path;
				maskShapePhase.push_back(true);
				out.ops.push_back(op);
				last = nullptr;
				break;
			}
			case AptRenderCommand::Kind::MaskContentBegin:
			{
				AptCanvasOp op;
				op.kind = AptCanvasOp::Kind::MaskContent;
				op.path = cmd.path;
				if (maskShapePhase.empty() || !maskShapePhase.back())
				{
					out.errors.push_back(cmd.path + ": mask content begins without an open mask shape");
				}
				else
				{
					maskShapePhase.back() = false;
				}
				out.ops.push_back(op);
				last = nullptr;
				break;
			}
			case AptRenderCommand::Kind::MaskEnd:
			{
				AptCanvasOp op;
				op.kind = AptCanvasOp::Kind::MaskEnd;
				op.path = cmd.path;
				if (maskShapePhase.empty())
				{
					out.errors.push_back(cmd.path + ": mask end without an open mask");
				}
				else
				{
					maskShapePhase.pop_back();
				}
				out.ops.push_back(op);
				last = nullptr;
				break;
			}
			case AptRenderCommand::Kind::Placeholder:
			{
				if (!maskShapePhase.empty() && maskShapePhase.back())
				{
					break; // only filled shapes define a mask
				}
				AptCanvasOp op;
				op.kind = AptCanvasOp::Kind::Placeholder;
				op.path = cmd.path;
				op.symbolMovie = cmd.symbolMovie;
				op.symbolName = cmd.symbolName;
				op.nativeTag = cmd.nativeTag;
				op.renderObject = cmd.renderObject;
				op.nativeVars = cmd.nativeVars;
				for (int k = 0; k < 4; ++k)
				{
					op.placeholderColor[k] = cmd.color.mul[k]; // lane PLAY-1: the clip's cumulative colour (the device multiplies it into a native image)
				}
				op.matrix = cmd.matrix;
				std::copy(cmd.bounds, cmd.bounds + 4, op.bounds);
				op.scaleX = in.mapping.scaleX();
				op.scaleY = in.mapping.scaleY();
				op.offsetX = in.mapping.offsetX();
				op.offsetY = in.mapping.offsetY();
				out.ops.push_back(op);
				last = nullptr;
				break;
			}
			case AptRenderCommand::Kind::Text:
			{
				if (!maskShapePhase.empty() && maskShapePhase.back())
				{
					break; // only filled shapes define a mask
				}
				if (cmd.fontName.empty())
				{
					break; // the render list reported the font that did not resolve
				}
				const AptTextResolution res = ResolveAptText(cmd.text, *in.text, in.textRecords);
				if (!res.found)
				{
					missing.insert(res.label);
				}
				if (res.text.empty())
				{
					break; // an empty field draws nothing
				}
				AptCanvasOp op;
				op.kind = AptCanvasOp::Kind::Text;
				op.path = cmd.path;
				op.text = res.text;
				op.dropShadow = res.dropShadow;
				op.fontName = cmd.fontName;
				op.fontHeight = cmd.fontHeight;
				op.drawFont = cmd.fontName;
				op.drawSize = cmd.fontHeight;
				if (in.fonts)
				{
					const FontRequestResult r = in.fonts->resolve(cmd.fontName, cmd.fontHeight);
					op.drawFont = r.name;
					op.drawSize = r.size;
					op.bold = r.bold;
					if (r.substituted)
					{
						substitutions.insert(cmd.fontName + " " + std::to_string((int)cmd.fontHeight) + " -> " + r.name + " " + std::to_string((int)r.size));
					}
				}
				// RW 0x4A8F95: the text colour and the drop shadow's opaque black through RW 0x4A8AD5
				const float textBytes[4] = { (float)cmd.textColor[0], (float)cmd.textColor[1], (float)cmd.textColor[2], (float)cmd.textColor[3] };
				const float blackBytes[4] = { 0.0f, 0.0f, 0.0f, 255.0f };
				AptRetailVertexColour(textBytes, cmd.color, op.textColor);
				AptRetailVertexColour(blackBytes, cmd.color, op.shadowColor);
				op.alignment = cmd.alignment;
				op.wordWrap = cmd.wordWrap;
				op.multiline = cmd.multiline;
				op.readOnly = cmd.readOnly;
				op.matrix = cmd.matrix;
				std::copy(cmd.bounds, cmd.bounds + 4, op.bounds);
				op.scaleX = in.mapping.scaleX();
				op.scaleY = in.mapping.scaleY();
				op.offsetX = in.mapping.offsetX();
				op.offsetY = in.mapping.offsetY();
				unverified.insert("text-layout"); // S-133
				unverified.insert("font-size");   // S-132
				out.ops.push_back(op);
				last = nullptr;
				break;
			}
			case AptRenderCommand::Kind::Shape:
			{
				const bool inMaskShape = !maskShapePhase.empty() && maskShapePhase.back();
				Mapper map{ in.mapping, cmd.matrix };
				for (const AptRenderFill &fill : cmd.fills)
				{
					if (!fill.style)
					{
						continue;
					}
					AptCanvasOp op;
					op.kind = AptCanvasOp::Kind::Mesh;
					op.path = cmd.path;
					op.maskShape = inMaskShape;
					float colour[4];
					AptRetailVertexColour(fill.rgba, cmd.color, colour); // RW 0x4A999B -> 0x4A8AD5
					if (inMaskShape)
					{
						colour[0] = colour[1] = colour[2] = colour[3] = 1.0f;
						unverified.insert("mask-alpha"); // S-135
					}
					const AptRenderFill *textured = nullptr;
					float texW = 1, texH = 1;
					if (fill.kind == APT_STYLE_TEXTURED && !inMaskShape)
					{
						if (!fill.imageResolved)
						{
							continue; // reported in the render list errors (an image id with no .dat entry)
						}
						const AptTextureStore::Entry &tex = in.textures->get(fill.textureName);
						if (!tex.ok)
						{
							if (textureErrors.insert(tex.error).second)
							{
								out.errors.push_back("texture " + fill.textureName + ": " + tex.error);
							}
							continue; // nothing is drawn in place of a texture that did not load (no placeholder colour)
						}
						textured = &fill;
						texW = (float)tex.width;
						texH = (float)tex.height;
						op.texture = fill.textureName;
						unverified.insert("uv-matrix-order"); // S-134
						unverified.insert("tga-origin");      // S-134
					}
					const std::vector<float> &tris = fill.style->triangles;
					for (std::size_t i = 0; i + 5 < tris.size(); i += 6)
					{
						for (int v = 0; v < 3; ++v)
						{
							pushVertex(op, map, tris[i + v * 2], tris[i + v * 2 + 1], colour, textured, texW, texH);
						}
					}
					if (fill.kind == APT_STYLE_LINE)
					{
						// each segment is a quad (two triangles) of the line width, at least one window pixel
						const float s = std::sqrt(std::fabs((map.total.a * map.total.d - map.total.b * map.total.c) * in.mapping.scaleX() * in.mapping.scaleY()));
						const float widthWindow = std::max(fill.lineWidth * s, 1.0f);
						const std::vector<float> &lines = fill.style->lines;
						for (std::size_t i = 0; i + 3 < lines.size(); i += 4)
						{
							float ax, ay, bx, by;
							map.apply(lines[i], lines[i + 1], ax, ay);
							map.apply(lines[i + 2], lines[i + 3], bx, by);
							float dx = bx - ax, dy = by - ay;
							const float len = std::sqrt(dx * dx + dy * dy);
							if (len <= 0.0f)
							{
								continue;
							}
							const float nx = -dy / len * widthWindow * 0.5f, ny = dx / len * widthWindow * 0.5f;
							const float corners[4][2] = { { ax + nx, ay + ny }, { bx + nx, by + ny }, { bx - nx, by - ny }, { ax - nx, ay - ny } };
							const int order[6] = { 0, 1, 2, 0, 2, 3 };
							for (int o : order)
							{
								op.positions.push_back(corners[o][0]);
								op.positions.push_back(corners[o][1]);
								op.colors.insert(op.colors.end(), colour, colour + 4);
							}
						}
						unverified.insert("line-width"); // S-135
					}
					if (op.positions.empty())
					{
						continue;
					}
					push(std::move(op));
				}
				break;
			}
		}
	}
	if (!maskShapePhase.empty())
	{
		out.errors.push_back("the render list ends inside " + std::to_string(maskShapePhase.size()) + " open mask(s)");
	}
	for (const AptCanvasOp &op : out.ops)
	{
		out.triangles += op.triangleCount();
	}
	out.unverified.assign(unverified.begin(), unverified.end());
	out.missingLabels.assign(missing.begin(), missing.end());
	out.fontSubstitutions.assign(substitutions.begin(), substitutions.end());
}

AptTextPlacement PlaceAptText(float boxX0, float boxY0, float boxX1, float boxY1, float textWidth, float textHeight, std::uint32_t alignment, bool multiline, bool wordWrap)
{
	// see AptCanvas.h (RW 0x4A8F95)
	AptTextPlacement p;
	const float boxWidth = boxX1 - boxX0, boxHeight = boxY1 - boxY0;
	float w = textWidth;
	if (w > boxWidth && w > 0.0f)
	{
		p.squeezeX = boxWidth / w; // RW 0x4A90B5: the width is clamped to the box
		w = boxWidth;
	}
	float x = boxX0;
	if (alignment == 1)
	{
		x = boxX1 - w;
	}
	else if (alignment == 2)
	{
		x = (boxWidth - w) * 0.5f + boxX0;
	}
	float y = boxY0;
	p.centredVertically = !(multiline && wordWrap);
	if (p.centredVertically)
	{
		y = (boxHeight - textHeight) * 0.5f + boxY0;
	}
	p.x = (float)(int)x;
	p.y = (float)(int)y;
	return p;
}
