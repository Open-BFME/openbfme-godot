// OpenBFME. GPL-3.0.
// See AptRenderList.h for the model.

#include "Libraries/Source/Apt/AptRenderList.h"

#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"

#include <algorithm>
#include <set>

std::size_t AptRenderList::count(AptRenderCommand::Kind kind) const
{
	std::size_t n = 0;
	for (const AptRenderCommand &c : commands)
	{
		if (c.kind == kind)
		{
			++n;
		}
	}
	return n;
}

std::vector<const AptRenderCommand *> AptRenderList::forPath(const std::string &path) const
{
	std::vector<const AptRenderCommand *> out;
	for (const AptRenderCommand &c : commands)
	{
		if (c.path == path)
		{
			out.push_back(&c);
		}
	}
	return out;
}

namespace
{

struct Ctx
{
	Apt &apt;
	AptRenderList &out;
	int level;
};

std::string textureName(const std::string &movie, std::uint32_t n)
{
	return "apt_" + movie + "_" + std::to_string(n) + ".tga";
}

} // namespace

void Apt::buildRenderList(AptRenderList &out)
{
	out.commands.clear();
	out.errors.clear();
	out.unverified.clear();
	std::set<std::string> unverified;
	std::function<void(AptCharacterInst *, const AptMatrix &, const AptColorTransform &, int)> emit;
	emit = [&](AptCharacterInst *inst, const AptMatrix &parentM, const AptColorTransform &parentC, int level) {
		if (!inst || !inst->defined() || !inst->visible)
		{
			return;
		}
		const AptMatrix M = parentM.concat(inst->matrix);
		const AptColorTransform C = parentC.concat(inst->color);
		AptRenderCommand base;
		base.path = inst->targetPath();
		base.level = level;
		base.matrix = M;
		base.color = C;

		// A clip the movie's own script tagged for the engine: `<clip>._type = "RenderImage"` (MainMenu's `Image`), `"View3D"` ...  (BFME2
		// 0x9162C4 / RotWK 0x91CA5C register the engine side by instance name; the MainMenu program assigns `_type` at APT offset
		// 0x1316C and `_RenderObj` at 0x132F8.)  Its authored children are placeholder art the engine replaces, so the clip is one bounded
		// Placeholder carrying the tag, never drawn through.  The engine's component-factory lookup by `_type` was not read (S-136).
		if (inst->isSpriteBase() && inst->type() != AptCharacterInst::Type::Movie)
		{
			AptValue tag;
			if (inst->getOwn("_type", tag) && tag.isString())
			{
				AptRenderCommand cmd = base;
				cmd.kind = AptRenderCommand::Kind::Placeholder;
				cmd.symbolName = tag.toString();
				cmd.nativeTag = true;
				AptValue obj;
				if (inst->getOwn("_RenderObj", obj) && obj.isString())
				{
					cmd.renderObject = obj.toString();
				}
				for (const char *name : { "_imageMap", "_mode", "_timerId" })
				{
					AptValue v;
					if (inst->getOwn(name, v) && v.isString())
					{
						cmd.nativeVars.emplace_back(name, v.toString());
					}
				}
				// lane UI-2: a View3D's viewer settings (RW 0x8145DD reads `_KeepAspectRatio` and `_AnimMode` with `_RenderObj`; the frame the clip asks
				// for is `_Frame`, RW 0x813514's "_frame=" command); booleans and numbers as their string form
				for (const char *name : { "_AnimMode", "_KeepAspectRatio", "_Frame" })
				{
					AptValue v;
					if (inst->getOwn(name, v) && (v.isString() || v.isNumber() || v.isBoolean()))
					{
						cmd.nativeVars.emplace_back(name, v.toString());
					}
				}
				float x0, y0, x1, y1;
				if (inst->contentBounds(x0, y0, x1, y1))
				{
					cmd.bounds[0] = x0;
					cmd.bounds[1] = y0;
					cmd.bounds[2] = x1;
					cmd.bounds[3] = y1;
				}
				out.commands.push_back(cmd);
				return;
			}
		}

		// native component placeholder: an exported symbol the host flags
		if (inst->charRef().character && inst->charRef().file && inst->type() != AptCharacterInst::Type::Movie)
		{
			const AptFile &f = *inst->charRef().file;
			for (const AptExport &e : f.exports)
			{
				if (e.characterId == inst->charRef().character->id && m_userHost.isComponentSymbol(f.name, e.name))
				{
					AptRenderCommand cmd = base;
					cmd.kind = AptRenderCommand::Kind::Placeholder;
					cmd.symbolMovie = f.name;
					cmd.symbolName = e.name;
					float x0, y0, x1, y1;
					if (inst->contentBounds(x0, y0, x1, y1))
					{
						cmd.bounds[0] = x0;
						cmd.bounds[1] = y0;
						cmd.bounds[2] = x1;
						cmd.bounds[3] = y1;
					}
					out.commands.push_back(cmd);
					return;
				}
			}
		}

		switch (inst->type())
		{
			case AptCharacterInst::Type::Shape:
			{
				const AptCharacter &ch = *inst->charRef().character;
				const std::string &movie = inst->charRef().file->name;
				AptRenderCommand cmd = base;
				cmd.kind = AptRenderCommand::Kind::Shape;
				cmd.shapeCharacterId = ch.id;
				cmd.shapeMovie = movie;
				auto key = std::make_pair(movie, ch.geometryId);
				auto it = m_geometry.find(key);
				if (it == m_geometry.end())
				{
					CachedGeometry entry;
					auto geom = std::make_shared<AptGeometry>();
					if (m_loader.loadGeometry(movie, ch.geometryId, *geom, &entry.error))
					{
						entry.geometry = geom;
					}
					else if (entry.error.empty())
					{
						entry.error = "the geometry could not be loaded";
					}
					it = m_geometry.emplace(key, entry).first;
				}
				if (!it->second.geometry)
				{
					// the failure is cached with its detail and reported by every build that needs the resource (no silent default)
					out.errors.push_back(movie + " shape " + std::to_string(ch.id) + ": " + it->second.error);
				}
				cmd.geometry = it->second.geometry;
				if (cmd.geometry)
				{
					auto mapIt = m_imageMaps.find(movie);
					if (mapIt == m_imageMaps.end())
					{
						auto map = std::make_shared<AptImageMap>();
						std::string error;
						if (!m_loader.loadImageMap(movie, *map, &error))
						{
							// a movie without textures has no `.dat`: only an error when a fill needs it
							map.reset();
						}
						mapIt = m_imageMaps.emplace(movie, map).first;
					}
					for (const AptGeometryStyle &style : cmd.geometry->styles)
					{
						AptRenderFill fill;
						fill.kind = style.kind;
						std::copy(style.rgba, style.rgba + 4, fill.rgba);
						fill.lineWidth = style.lineWidth;
						fill.imageId = style.imageId;
						std::copy(style.uv, style.uv + 6, fill.uv);
						fill.style = &style;
						if (style.kind == APT_STYLE_TEXTURED)
						{
							const AptImageMapEntry *entry = nullptr;
							if (mapIt->second)
							{
								for (const AptImageMapEntry &e : mapIt->second->entries)
								{
									if ((std::int32_t)e.imageId == style.imageId)
									{
										entry = &e;
										break;
									}
								}
							}
							if (!entry)
							{
								out.errors.push_back(movie + " shape " + std::to_string(ch.id) + ": image " + std::to_string(style.imageId) + " has no .dat entry");
							}
							else
							{
								fill.imageResolved = true;
								fill.imageIsRect = entry->isRect;
								if (entry->isRect)
								{
									unverified.insert("rect-image");
								}
								if (entry->isRect)
								{
									fill.textureName = textureName(movie, entry->imageId);
									std::copy(entry->rect, entry->rect + 4, fill.imageRect);
								}
								else
								{
									fill.textureName = textureName(movie, entry->textureId);
								}
							}
						}
						cmd.fills.push_back(fill);
					}
				}
				out.commands.push_back(cmd);
				break;
			}
			case AptCharacterInst::Type::EditText:
			{
				AptTextInst *t = static_cast<AptTextInst *>(inst);
				AptRenderCommand cmd = base;
				cmd.kind = AptRenderCommand::Kind::Text;
				cmd.text = t->text;
				cmd.variable = t->variable;
				if (!t->variable.empty())
				{
					// a text field bound to a variable reads it through the parent timeline (spec 2.6)
					AptCharacterInst *p = t->parent();
					AptValue v;
					if (p)
					{
						std::string path = t->variable;
						std::size_t dot = path.find_last_of("./");
						AptCharacterInst *holder = p;
						std::string name = path;
						if (dot != std::string::npos)
						{
							holder = resolvePath(p, path.substr(0, dot));
							name = path.substr(dot + 1);
						}
						if (holder && holder->getMember(name, v) && !v.isUndefined())
						{
							cmd.text = v.toString();
						}
					}
				}
				if (const AptTextInfo *info = t->charRef().character->text.get())
				{
					cmd.fontId = info->fontId;
					cmd.fontHeight = info->fontHeight;
					// the instance's current colour (file value until a script assigns `textColor`), not the character's
					cmd.textColor[0] = (std::uint8_t)((t->colorArgb >> 16) & 0xFF);
					cmd.textColor[1] = (std::uint8_t)((t->colorArgb >> 8) & 0xFF);
					cmd.textColor[2] = (std::uint8_t)(t->colorArgb & 0xFF);
					cmd.textColor[3] = (std::uint8_t)(t->colorArgb >> 24);
					cmd.alignment = info->alignment;
					cmd.readOnly = info->readOnly;
					cmd.multiline = info->multiline;
					cmd.wordWrap = info->wordWrap;
					std::copy(info->bounds, info->bounds + 4, cmd.bounds);
					AptCharRef fontRef;
					std::string error;
					const std::string label = t->charRef().file->name + " text " + std::to_string(t->charRef().character->id) + " (" + cmd.path + ")";
					if (!resolveCharacter(t->charRef().file, info->fontId, fontRef, &error))
					{
						out.errors.push_back(label + ": font " + std::to_string(info->fontId) + " does not resolve: " + error);
					}
					else if (fontRef.character->type != APT_CHAR_FONT)
					{
						out.errors.push_back(label + ": font " + std::to_string(info->fontId) + " is a character of type " + std::to_string(fontRef.character->type) + ", not a font");
					}
					else
					{
						cmd.fontName = fontRef.character->fontName;
					}
				}
				out.commands.push_back(cmd);
				break;
			}
			case AptCharacterInst::Type::Sprite:
			case AptCharacterInst::Type::Movie:
			{
				const AptSpriteInst *s = inst->asSprite();
				std::vector<AptCharacterInst *> kids = s->children();
				std::int32_t maskEnd = -1; // the clip depth of the mask in force (-1: none)
				for (AptCharacterInst *k : kids)
				{
					if (maskEnd >= 0 && k->depth() > maskEnd)
					{
						AptRenderCommand end = base;
						end.kind = AptRenderCommand::Kind::MaskEnd;
						out.commands.push_back(end);
						maskEnd = -1;
					}
					if (k->clipDepth >= 0)
					{
						unverified.insert("clip-layer");
						if (maskEnd >= 0)
						{
							AptRenderCommand end = base;
							end.kind = AptRenderCommand::Kind::MaskEnd;
							out.commands.push_back(end);
						}
						AptRenderCommand begin = base;
						begin.kind = AptRenderCommand::Kind::MaskBegin;
						begin.path = k->targetPath();
						begin.clipDepth = k->clipDepth;
						out.commands.push_back(begin);
						emit(k, M, C, level);
						AptRenderCommand content = base;
						content.kind = AptRenderCommand::Kind::MaskContentBegin;
						content.path = k->targetPath();
						out.commands.push_back(content);
						maskEnd = k->clipDepth;
						continue;
					}
					emit(k, M, C, level);
				}
				if (maskEnd >= 0)
				{
					AptRenderCommand end = base;
					end.kind = AptRenderCommand::Kind::MaskEnd;
					out.commands.push_back(end);
				}
				break;
			}
			case AptCharacterInst::Type::Button:
			{
				AptButtonInst *b = inst->asButton();
				std::vector<AptCharacterInst *> kids = b->children();
				for (AptCharacterInst *k : kids)
				{
					emit(k, M, C, level);
				}
				break;
			}
			default:
				if (inst->type() == AptCharacterInst::Type::StaticText || inst->type() == AptCharacterInst::Type::Morph)
				{
					unverified.insert("static-text-not-drawn"); // S-100
				}
				break; // static text, morphs and the unsupported characters draw nothing yet (S-100)
		}
	};
	for (int lvl : loadedLevels())
	{
		emit(m_levels[lvl], AptMatrix(), AptColorTransform(), lvl);
	}
	out.unverified.assign(unverified.begin(), unverified.end());
}
