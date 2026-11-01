// OpenBFME. GPL-3.0.
//
// The Godot-independent half of the Apt renderer: the render list of the player (Libraries/Source/Apt/AptRenderList.h) translated
// into canvas operations in window pixels (flat triangle meshes with vertex colours and UVs, resolved text runs, mask brackets),
// the stage-to-window mapping, and the texture store (`art/textures/apt_<movie>_<n>.tga`, menus-apt.md 2.1/2.5).  The Godot device
// (GodotDevice/GodotAptPlayer.cpp) draws the result; everything here is unit-tested headless.
//
// Rendering rules and where they come from (every rule that is not a target or donor fact is listed in AptCanvasList::unverified and
// in docs/STOPS.md S-130..S-139):
//   - stage -> window: the stage is scaled independently in x and y to the display size (donor: Open-BFME-1
//     Rva007833E0AptScaledTransform.cpp setScaledTransform007833E0: scaleX = displayWidth / aptWidth, scaleY = displayHeight /
//     aptHeight, applied to the root transform's matrix and translation; text rectangles are scaled by the same factors and the font
//     size by min(scaleX, scaleY), AptDisplayStringAllocation_ctor.cpp:105-120).  The target (BFME2/RotWK) code was not read: S-130.
//   - a vertex colour is RotWK's (lane UI-2, owner feedback F1; TARGET FACTS): the engine's Apt draw callbacks take the cumulative
//     colour transform (gAptFuncs slot 0xDFD4FC = RW 0x4A934F stores mul a r g b / add a r g b) and every vertex colour of a shape
//     (RW 0x4A999B) and of a text and its drop shadow (RW 0x4A8F95) goes through RW 0x4A8AD5: per channel trunc((c / 255 * mul + add)
//     * 255) on the x87, unclamped, packed as A << 24 | R << 16 | G << 8 | B by OR (AptRetailVertexColour).  So the additive term is in
//     the vertex colour BEFORE the texture modulates it; there is no separate additive pass.  Texture colour times vertex colour and
//     the alpha blend are the D3D draw (inference: the 2D renderer's shader was not read; the `tc` style colour is always
//     255:255:255:255 in the corpus).
//   - the UV of a textured fill is the `tc` matrix applied to the shape-space vertex, in texture pixels (retail data: MainMenu shape
//     44 places image 43 at atlas pixels x 1..436, y 1..69 by `tc:...:1:0:0:1:218:283` over x -217..218, y -282..-214); u = (m11*x +
//     m21*y + tx) / width, v = (m12*x + m22*y + ty) / height.  The order of the four matrix entries is the SWF matrix convention
//     (inference, S-134).  The `N=x y w h` image form uses the same formula on texture `apt_<movie>_<N>.tga` (S-109, S-134).
//   - TGA rows: the image is stored bottom-up (descriptor bit 5 clear, all 114 per-movie textures): rows are flipped so row 0 is the
//     top and v is measured from the top.  Evidence: MainMenu atlas image 41 spans v 1..432 and its pixels sit in the last 432 stored
//     rows (S-134).
//   - a line is a quad of `lineWidth` stage pixels, at least one window pixel (S-135).
//   - a mask (clip layer) is drawn as alpha 1 without texture (S-109, S-135).
//   - text: `$LABEL` resolution and `&dropShadow` follow the donor (GameText.h, S-131); the font follows fontsubstitution.ini (S-132);
//     the box, baseline and wrapping rules are S-133.  The colours of a shape and of a text come from a colour transform whose file
//     bytes are B, G, R, A (Apt/AptFile.cpp parseText and AptCharacterInst.cpp: target facts).

#pragma once

#include "GameClient/FontSubstitution.h"
#include "GameClient/GameText.h"
#include "GameClient/TGAFile.h"
#include "Libraries/Source/Apt/AptLoad.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------------------------
// stage <-> window
// ---------------------------------------------------------------------------------------------------------------------------------
struct AptStageMapping
{
	enum class Mode : std::uint8_t
	{
		Stretch, // donor: independent x / y scale (setScaledTransform007833E0)
		Fit      // uniform scale, centred (letterbox / pillarbox); an option of the viewer, not a retail rule
	};
	float stageW = 1024, stageH = 768;
	float windowW = 1024, windowH = 768;
	Mode mode = Mode::Stretch;

	float scaleX() const;
	float scaleY() const;
	float offsetX() const;
	float offsetY() const;
	float uniformScale() const; // min(scaleX, scaleY): the font scale
	AptStageMapping fitImage(float imageWidth, float imageHeight) const;
	void stageToWindow(float x, float y, float &wx, float &wy) const;
	void windowToStage(float wx, float wy, float &x, float &y) const;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// textures
// ---------------------------------------------------------------------------------------------------------------------------------
class AptTextureStore
{
public:
	struct Entry
	{
		bool ok = false;
		std::string error;
		int width = 0, height = 0;
		std::vector<std::uint8_t> rgba; // top row first; emptied by releasePixels()
		bool pixelsReleased = false;
	};
	explicit AptTextureStore(AptFileSource &source) : m_source(source) {}

	// Load (once) `art/textures/<name>`.  The returned entry is never null; check `ok`.
	const Entry &get(const std::string &name);
	std::string packedTextureStem(const std::string &name) const;
	// lane UI-2: load (once) the TGA at exactly `path` in the mounted archives ('\\' or '/'), e.g. a map's `<map>_pic.tga` (an Image the engine makes
	// from a file, RW 0x975F23). Its pixels stay (a map picture is 128 x 128).
	const Entry &getFile(const std::string &path);
	// The pixels are no longer needed after the device uploaded them.
	void releasePixels(const std::string &name);
	std::size_t loadedCount() const { return m_entries.size(); }

private:
	static void decodeEntry(const std::string &path, const std::vector<std::uint8_t> &bytes, Entry &entry);
	AptFileSource &m_source;
	std::map<std::string, Entry> m_entries; // key: lower-cased name
};

// ---------------------------------------------------------------------------------------------------------------------------------
// canvas operations
// ---------------------------------------------------------------------------------------------------------------------------------
struct AptCanvasOp
{
	enum class Kind : std::uint8_t
	{
		Mesh,        // triangles (and lines as quads) in window pixels
		Text,
		Placeholder, // a native component's rectangle
		MaskBegin,   // the Mesh ops until MaskContent are the mask shape
		MaskContent, // the ops until MaskEnd are clipped by the mask
		MaskEnd
	};
	Kind kind = Kind::Mesh;
	std::string path; // the instance path (diagnostics)
	int level = 0;    // lane HUDREDRAW-1: the Apt level of the command it comes from (ops of two levels are never merged)

	// Mesh: three vertices per triangle, no sharing
	std::string texture;           // empty: untextured
	std::vector<float> positions;  // x y per vertex, window pixels
	std::vector<float> colors;     // r g b a per vertex, 0..1 (AptRetailVertexColour of the fill colour)
	std::vector<float> uvs;        // u v per vertex (textured only), 0..1
	bool maskShape = false;        // part of a mask: drawn with alpha 1, untextured
	std::size_t triangleCount() const { return positions.size() / 6; }

	// Text
	std::string text;              // UTF-8, resolved
	std::string fontName;          // as the movie names it
	float fontHeight = 0;          // as the movie states it (stage pixels)
	std::string drawFont;          // after fontsubstitution.ini
	float drawSize = 0;            // after fontsubstitution.ini, stage pixels (the device multiplies by `scaleXY`)
	int bold = 0;
	float textColor[4] = { 0, 0, 0, 1 };   // AptRetailVertexColour of the text colour, 0..1
	float shadowColor[4] = { 0, 0, 0, 1 }; // of opaque black (RW 0x4A8F95: the drop shadow is 0xFF000000 through the same transform)
	std::uint32_t alignment = 0;   // 0 left, 1 right, 2 centre, 3 justify (SWF)
	bool wordWrap = false, multiline = false, readOnly = false, dropShadow = false;
	AptMatrix matrix;              // stage matrix of the instance
	float bounds[4] = { 0, 0, 0, 0 }; // x0 y0 x1 y1 in the instance's space
	float scaleX = 1, scaleY = 1, offsetX = 0, offsetY = 0; // the stage mapping in force (text and placeholders are placed by the device)

	// Placeholder
	std::string symbolMovie, symbolName;
	bool nativeTag = false;    // a clip tagged by its own script (`_type`), not an exported symbol
	std::string renderObject;  // its `_RenderObj`
	std::vector<std::pair<std::string, std::string>> nativeVars; // lane HUD-1: `_imageMap`, `_mode`, `_timerId` of the tagged clip
	float placeholderColor[4] = { 1, 1, 1, 1 }; // lane PLAY-1: the clip's cumulative colour multiply (r g b a)
};

// RotWK RW 0x4A8AD5 (the Apt vertex colour): `rgba` are the colour's bytes (r g b a, 0..255), `c` the cumulative colour transform
// (mul 0..1, add in 0..255 units as the place object stores it; retail holds add as byte * (1 / 255.0f)).  Per channel, in float
// precision as the game's 24-bit x87 mode computes it: t = c * (1 / 255.0f) (constant RW 0xBD1920); t *= mul; t += add; t *= 255
// (RW 0xBD88A8); truncated toward zero (_ftol, RW 0xA3CFA4); the four integers are ORed into A << 24 | R << 16 | G << 8 | B without a
// clamp, so a channel outside 0..255 spills into the higher ones exactly as retail's.  Returns that 0xAARRGGBB.
std::uint32_t AptRetailVertexColour(const float rgba[4], const AptColorTransform &c);
// The same as r g b a floats 0..1 (byte / 255).
void AptRetailVertexColour(const float rgba[4], const AptColorTransform &c, float out[4]);

class AptCanvasList
{
public:
	std::vector<AptCanvasOp> ops;
	std::vector<std::string> unverified;    // render-list facts plus the canvas rules above, sorted, no repeats
	std::vector<std::string> errors;        // render-list errors, textures that did not load, unbalanced masks
	std::vector<std::string> missingLabels; // `$LABEL`s with no table entry (shown as the label, never as nothing)
	std::vector<std::string> fontSubstitutions; // "Albertus MT 14 -> Omnia LT Std 20" ...
	std::size_t triangles = 0;
	std::size_t count(AptCanvasOp::Kind kind) const;
};

// lane HUDREDRAW-1: the device keeps the canvas items of the ops that draw the same as last frame and draws again only the ones that changed (a movie that
// loops a state, the spell store's `_active` buttons or the Palantir's pulsing powers button, no longer redraws the whole HUD).  The unit is a segment: one
// op, or a mask bracket with everything it clips (MaskBegin up to its MaskEnd; a mask left open runs to the list's end, as the device nests the ops after it
// under the mask).  Each segment is drawn under a canvas item of its own, in list order.
struct AptCanvasSegment
{
	int firstLevel = 0, lastLevel = 0; // the levels of its first and last op
	std::size_t begin = 0, end = 0;    // [begin, end) in the list's ops
};
std::vector<AptCanvasSegment> AptCanvasSegments(const AptCanvasList &list);
// Which segments of `now` can keep what is on screen for `drawn`. The two lists are matched level by level (by each segment's first level): the same levels
// in the same order, else `full` (everything drawn again). In a level with as many segments as before, segment by segment: `old[j]` is the drawn segment it
// replaces and `same[j]` whether its ops draw the same (kept); a level whose segment count changed has all its segments drawn anew (`old[j]` = -1).
struct AptCanvasRedrawPlan
{
	bool full = false;
	std::vector<std::ptrdiff_t> old;
	std::vector<bool> same;
};
AptCanvasRedrawPlan AptCanvasPlanRedraw(const AptCanvasList &now, const std::vector<AptCanvasSegment> &nowSegments, const AptCanvasList &drawn,
	const std::vector<AptCanvasSegment> &drawnSegments);
// Whether two ops draw the same: every field the device or its native hook reads, compared bit for bit (floats too: the same bits draw the same)
bool AptCanvasSameOp(const AptCanvasOp &x, const AptCanvasOp &y);
// Whether a's ops [aBegin, aEnd) draw the same as b's ops [bBegin, bEnd)
bool AptCanvasSameOps(const AptCanvasList &a, std::size_t aBegin, std::size_t aEnd, const AptCanvasList &b, std::size_t bBegin, std::size_t bEnd);

struct AptImageReplacement
{
	std::string texture;
	float width = 0, height = 0;
};
using AptImageReplacements = std::map<std::pair<std::string, int>, AptImageReplacement>;

struct AptCanvasInputs
{
	AptStageMapping mapping;
	AptTextureStore *textures = nullptr;           // required
	const GameTextTable *text = nullptr;           // required
	const FontSubstitution *fonts = nullptr;       // null: no substitution
	const AptTextRecordLookup *textRecords = nullptr; // the engine's Apt text records (shell mode); null: only the string table
	// lane PLATFORMICON-1: the icons beside movie text fields (GUI/TextBadges.h): a field with a badge that asks to `reserve` stage pixels at its left gets a
	// box that much narrower, so the string (centred, or squeezed when too wide) never runs under the icon. Null: none
	const std::map<std::string, struct TextBadge> *textBadges = nullptr;
	const AptImageReplacements *imageReplacements = nullptr; // lower-case source movie + image id; presentation only
};

// Lane HUD-3: where an edit-text field's string goes inside its box, as RotWK's Apt display string draw does it (RW 0x4A8F95; the allocation's ctor
// RW 0x4AA369 sets the flags). TARGET FACTS (RotWK game.dat, caveat S-001):
// - the box is the field's bounds through the instance matrix (RW 0x4A8FAE .. 0x4A9021); the measured string is scaled by the matrix's ratio of box to
//   bounds (RW 0x4A9055 .. 0x4A908D) and, when it is wider than the box, squeezed to the box width (RW 0x4A90B5 .. 0x4A90D7: only the width shrinks);
// - horizontally (RW 0x4A9148 .. 0x4A917E): alignment 1 puts the string against the right edge, 2 centres it, every other value (0, 3) leaves it at the left;
// - vertically (RW 0x4A9181 .. 0x4A91A4): a field that is not both multiline and word-wrapping (flag +0x2E = !src+0x28 || !src+0x24, RW 0x4AA3D2 .. 0x4AA3E7)
//   is centred: top + (box height - string height) * 0.5; a wrapping multiline field starts at the top;
// - the position is truncated to whole pixels (cvttss2si, RW 0x4A91AD / 0x4A91B8 / 0x4A91C3).
// INFERENCE (stop S-763): the box and the string are both measured in window pixels (retail mixes the local box height with the unscaled string height,
// equal under a uniform scale). The string height is the GDI char height and the baseline sits in it as RotWK's font library rasterises (lane UI-6,
// GameClient/GdiFontMetrics.h, stop S-3700): the device's fonts carry those metrics (AptMenuPlayer cellFont).
struct AptTextPlacement
{
	float x = 0, y = 0;   ///< the string's top left, window pixels (truncated)
	float squeezeX = 1;   ///< horizontal scale of the string (< 1 when it was wider than the box)
	bool centredVertically = false;
};
// box: the window rectangle of the field's two corners (x0, y0) and (x1, y1) through the instance matrix and the stage mapping (axis-aligned, as retail)
AptTextPlacement PlaceAptText(float boxX0, float boxY0, float boxX1, float boxY1, float textWidth, float textHeight, std::uint32_t alignment, bool multiline, bool wordWrap);

// Lane MENUFIX-1: how much an edit-text instance's stage matrix grows its font: the length of the matrix's transformed vertical unit vector
// (sqrt(c^2 + d^2): the field's vertical scale, rotation-invariant). TARGET FACT (donor, BFME1 AptDisplayStringAllocation_ctor.cpp:105-110 for RW
// 0x4AA369): the display string's font size is the source's size times min(screen scale x, y); the box comes from the field's corners through the
// matrix. INFERENCE (S-132): which matrix factor RotWK folds into the source's size was not read; the vertical scale is the one that keeps the movies'
// designs: LoadScreen.apt widens each row's 47-unit, 14-pixel TheString field with `_width = stringWidth` (160 for PlayerName, 70 for ArmyName, a
// horizontal stretch only), so the names keep the 14-pixel font of the other columns in a wider box. The earlier sqrt(|det|) grew the player names
// to 1.84 times (26 pixels at 1024 x 768), over the next row. Equal to sqrt(|det|) for every uniformly scaled field.
float AptTextFontScale(const AptMatrix &matrix);

// Translate a render list.  `merge` joins consecutive meshes of equal state into one op (fewer device calls, same pixels).
void BuildAptCanvas(const AptRenderList &list, const AptCanvasInputs &in, AptCanvasList &out, bool merge = true);
