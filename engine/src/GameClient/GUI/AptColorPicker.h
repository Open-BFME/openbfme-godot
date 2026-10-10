// OpenBFME. GPL-3.0.
//
// AptColorPicker (lane CAH-2): RotWK's "ColorPicker" render component, the native half of GadgetColorPicker.swf (the Create-a-Hero builder's colour tabs).
// Client code: no simulation state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read from the disassembly, BFME2 0xB40EEE .. is tier A with no decomp source):
//   * the component registry (RW 0x8151D2 .. 0x815206) names "ColorPicker" (RW 0xC4FD0C) with the handler RW 0x814E3A: a clip tagged `_type = "ColorPicker"`
//     is drawn by the object its `_componentId` names (RW 0x813E21), made on its first draw (RW 0x81471C -> RW 0xB55208 / 0xB55146, vtable RW 0xD0A1A8);
//   * init (RW 0xB5570B): `_path` (the clip GadgetColorPicker.swf calls back, + 0x64) and `_imageName` (a mapped image, RW 0x6DA34C: + 0x60, its size
//     + 0x78 / + 0x7C); two externs <componentId> + "Cursor" and <componentId> + "Color" (RW 0xC10A54 / 0xBE39D0, handler RW 0xB5505B):
//       Cursor: set "%f %f" (RW 0xD0A19C) scaled to the screen (+ 0x68 / + 0x6C, flag + 0x74); get the same back;
//       Color:  set "%u" (+ 0x70, flag + 0x75); get "%u";
//   * draw (RW 0xB552BF): the image over the clip's rectangle (RW 0x44CF58); a new cursor reads the image texel at (texture size * UV origin +
//     cursor * image size / rectangle size, truncated) of its A8R8G8B8 texture (another format: nothing), keeps it as the colour and calls the
//     clip's SetColor(colour) (RW 0xB55259 with "SetColor" RW 0xD0A1C0); a new colour finds the image texel nearest to it (RW 0xB5500E: the sum of
//     the squared byte differences of B, G, R, A; the first smallest row by row) and calls SetCursor(x, y) with that texel's place in the rectangle,
//     rounded (RW 0x92BCC2 with "SetCursor" RW 0xD0A1B4).
// OpenBFME DIFFERENCES (stop S-1408): the per-draw work runs in the window manager's update; the cursor is kept in the movie's units, so the screen
// scale (RW 0x4A897D / 0x4A8983) cancels out; the rectangle is the clip's stage bounds.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AptTextureStore;
class AptFileSource;
class MappedImageCollection;
class WindowManager;
struct Image;

class AptColorPickers
{
public:
	AptColorPickers(WindowManager &windows, const MappedImageCollection &images, AptFileSource &source);
	~AptColorPickers();

	// RW 0x8151D2: the "ColorPicker" component name; its instances are followed by update()
	void registerComponent();
	// RW 0xB552BF's work for every live picker: init on the first pass that sees `_componentId`, then the cursor -> colour and colour -> cursor
	void update();

	struct Picker
	{
		std::string instancePath;
		int level = -1;
		bool initialised = false;
		std::string componentId, imageName, path; // `_path`: the clip's target path
		const Image *image = nullptr;
		float cursor[2] = { 0, 0 };
		bool cursorDirty = false;
		std::uint32_t color = 0;
		bool colorDirty = false;
		std::vector<std::string> externs;
	};
	const std::vector<Picker> &pickers() const { return m_pickers; }
	// the texel pick and the reverse search on a raw image (tests): `rgba` rows top first, the image's rectangle at (x0, y0) of a w x h texture
	static bool texelAt(const std::vector<std::uint8_t> &rgba, int w, int h, int x, int y, std::uint32_t &argb);
	static bool nearestTexel(const std::vector<std::uint8_t> &rgba, int w, int h, int x0, int y0, int iw, int ih, std::uint32_t argb, int &bx, int &by);

private:
	bool init(Picker &p);
	void pick(Picker &p, const float rect[4]);
	void place(Picker &p, const float rect[4]);
	bool pixels(const Image &image, const std::vector<std::uint8_t> *&rgba, int &w, int &h);

	WindowManager &m_windows;
	const MappedImageCollection &m_images;
	std::unique_ptr<AptTextureStore> m_textures;
	std::vector<Picker> m_pickers;
	bool m_registered = false;
};
