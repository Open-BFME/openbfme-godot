// OpenBFME. GPL-3.0.
//
// The render command list of the Apt player (menus-apt.md 3.2 "Rendering"): what would be drawn this frame, in order,
// produced by the core so the Godot renderer is a thin translation.  Nothing here touches a GPU.
//
// Order: levels ascending, children by depth ascending (the display list order of AptDisplayList), a button's current
// state records in file order.  An instance with `_visible` false (or removed) and its subtree are not emitted.
//
// Commands
//   Shape        the triangles / lines of one shape character (`.ru` geometry), under `matrix` (shape -> stage) and
//                `color` (the cumulative colour transform: out = in * mul + add).  A textured fill carries the resolved
//                texture name and the `.dat` rectangle (menus-apt.md 2.5; the rectangle semantics are unverified, S-109).
//   Text         one edit-text field: bounds, font, size, colour, alignment, flags and the text to draw.  The text is the
//                raw string (a `$LABEL` is resolved by the host's string lookup, spec 2.6); a variable bound to a script
//                variable is read through the parent timeline.
//   Placeholder  an instance of a symbol the host says is a native component (gadget, View3D, BinkMovie, ...), or a clip whose
//                script assigned `_type` (RenderImage, View3D, ...): its bounds and origin; its children are not emitted.
//   MaskBegin / MaskContentBegin / MaskEnd
//                an instance with a clip depth (SWF clip layer): the commands between MaskBegin and MaskContentBegin
//                define the mask shape, those between MaskContentBegin and MaskEnd are clipped by it.  The clipped
//                range is the instances above the mask's depth up to and including `clipDepth`.

#pragma once

#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptFile.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A fill of a shape with its image resolved through the movie's `.dat` image map.
struct AptRenderFill
{
	AptStyleKind kind = APT_STYLE_SOLID;
	float rgba[4] = { 0, 0, 0, 0 };
	float lineWidth = 0;
	std::int32_t imageId = 0;
	float uv[6] = { 1, 0, 0, 1, 0, 0 };
	// Textured: `textureName` is `apt_<movie>_<n>.tga` (menus-apt.md 2.1); `imageIsRect` is the `N=x y w h` form.
	std::string textureName;
	bool imageIsRect = false;
	std::int32_t imageRect[4] = { 0, 0, 0, 0 };
	bool imageResolved = false; // false: the image id has no `.dat` entry (reported in AptRenderList::errors)
	const AptGeometryStyle *style = nullptr; // the triangles / lines (owned by the geometry)
};

struct AptRenderCommand
{
	enum class Kind : std::uint8_t
	{
		Shape,
		Text,
		Placeholder,
		MaskBegin,
		MaskContentBegin,
		MaskEnd
	};

	Kind kind = Kind::Shape;
	std::string path;          // the instance's target path (diagnostics and tests)
	int level = 0;
	AptMatrix matrix;          // local -> stage
	AptColorTransform color;   // cumulative

	// Shape
	std::shared_ptr<const AptGeometry> geometry;
	std::uint32_t shapeCharacterId = 0;
	std::string shapeMovie;
	std::vector<AptRenderFill> fills;

	// Text
	std::string text;
	std::string variable;
	std::string fontName;
	std::uint32_t fontId = 0;
	float fontHeight = 0;
	std::uint8_t textColor[4] = { 0, 0, 0, 0 };
	std::uint32_t alignment = 0;
	bool readOnly = false, multiline = false, wordWrap = false;

	// Text and Placeholder
	float bounds[4] = { 0, 0, 0, 0 }; // x0 y0 x1 y1 in the instance's own space

	// Placeholder: where the symbol came from (the export name in the exporting movie)
	std::string symbolMovie;
	std::string symbolName;   // the export name, or the `_type` tag of a script-tagged clip (nativeTag)
	bool nativeTag = false;   // the clip's script assigned `_type` (engine-render / native clip); symbolMovie is empty
	std::string renderObject; // its `_RenderObj` when assigned
	// lane HUD-1: the other variables the clip's script gave the engine (`_imageMap`, `_mode`, `_timerId`: the RenderImage and TimerOverlay clips of libInGameUI), name -> value
	std::vector<std::pair<std::string, std::string>> nativeVars;

	// MaskBegin
	std::int32_t clipDepth = -1;
};

class AptRenderList
{
public:
	std::vector<AptRenderCommand> commands;
	// Facts the list depends on that were not settled (S-109): "rect-image" (a `N=x y w h` image map line), "clip-layer" (a mask range),
	// "static-text-not-drawn" (S-100).  Sorted, no repeats.  (The additive colour term is a target fact since lane UI-2: AptCanvas.h.)
	std::vector<std::string> unverified;
	std::vector<std::string> errors; // resources the list could not resolve (a missing `.ru`, an image id without a `.dat` entry)

	std::size_t count(AptRenderCommand::Kind kind) const;
	// Commands of one instance (by target path), for tests.
	std::vector<const AptRenderCommand *> forPath(const std::string &path) const;
};
