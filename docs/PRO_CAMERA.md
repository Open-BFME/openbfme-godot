# The pro camera

A camera with a real lens, for anything that wants to show the game like a film camera would: cutscene and replay tools,
a photo mode, trailers, screenshot scripts, a map viewer. It is an OpenBFME addition, not part of the original game: the
original camera has a field of view and nothing else. It is client state only. The simulation never reads it, so it is
safe in any game, multiplayer included.

It is a camera engine, not a feature: no keys, no menus, no HUD. It reads no input and owns no node. Whatever uses it
decides how it moves and when, and puts it onto a Godot `Camera3D`. It comes in layers (lane LENS-1), and each works
without the ones above it:

| Layer | File | What it is |
|---|---|---|
| lens maths | [`godot/scripts/pro_lens.gd`](../godot/scripts/pro_lens.gd) | static functions: sensor crops, focal length and field of view, the blur circle, the sharp range |
| effect | [`godot/scripts/pro_lens_effect.gd`](../godot/scripts/pro_lens_effect.gd) | the depth of field, exposure and log output, a `CompositorEffect` for any `Camera3D`, its compute shader inside |
| camera | [`godot/scripts/pro_camera.gd`](../godot/scripts/pro_camera.gd) | a pose and a lens, and `apply(camera)` to put a `Camera3D` there with the effect on it |
| glide | [`godot/scripts/pro_camera_glide.gd`](../godot/scripts/pro_camera_glide.gd) | an optional helper that eases the camera towards targets |

## Using it

```gdscript
const ProCamera := preload("res://scripts/pro_camera.gd")

var cam := ProCamera.new()
var camera := Camera3D.new()

func _ready() -> void:
	add_child(camera)
	camera.current = true
	# Godot world coordinates; sage() turns a map point (x east, y north, z up) into them
	cam.look_at(ProCamera.sage(Vector3(1660, 2890, 172)), ProCamera.sage(Vector3(1648, 2965, 170)))
	cam.focal = 105.0       # mm on a full-frame sensor
	cam.aperture = 1.4      # f/1.4: shallow depth of field around the focus
	cam.ev = 0.5            # half a stop brighter

func _process(delta: float) -> void:
	cam.move_view(20.0 * delta, 0.0, 0.0)   # a slow push in, 20 units a second
	cam.apply(camera)                       # pose, field of view and the effect, every frame
```

`look_at` sets the eye at once and focuses on the target; `place(eye, direction, focus)` does the same from a direction.
`aim(target)` turns the view to a target from where the eye is, keeping the roll; `follow(target, offset, facing)` puts
the eye at an offset from a target and aims at it (see [following a unit](#following-a-unit)). `focus_on(point)` focuses
on a point wherever it is in the picture: the focus is a distance along the view, which `depth_of(point)` gives, not the
straight distance to the point. `move`, `move_view` and `turn` change the pose at once. `apply(camera)` sets the
camera's transform, its horizontal field of view across its viewport (`KEEP_WIDTH`), `near` and `far`, and keeps the
effect on its compositor. Call it every frame the pose or the lens changes. `apply(camera, false)` leaves the compositor
alone; `detach(camera)` takes the effect off.

The camera eases nothing by itself and has no built-in limits beyond what the maths needs: it holds exactly what it is
given, and how a shot moves is the caller's choice. Every setting is a plain property, so a script can change several at
once, every frame. A `Tween` animates them together, here a push in with a focus pull and a zoom:

```gdscript
var shot := create_tween().set_parallel().set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
shot.tween_property(cam, "eye", cam.eye + cam.forward() * 60.0, 4.0)
shot.tween_property(cam, "focal", 135.0, 4.0)
shot.tween_property(cam, "focus", cam.focus - 60.0, 4.0)
shot.tween_property(cam, "aperture", 1.2, 4.0)
```

Controls that should glide can smooth towards a target of their own, frame by frame:

```gdscript
var k := 1.0 - exp(-8.0 * delta)          # about an eighth of a second to settle
cam.eye = cam.eye.lerp(wanted_eye, k)
cam.focus = lerpf(cam.focus, wanted_focus, k)
```

The only limit is in `turn()`: the pitch stops 0.1 degrees short of straight up or down, where a heading would mean
nothing (`pitch` set directly is not limited). `near` and `far` (2 and 60,000) are properties like the rest.

### Easing with the glide

For controls that should glide, `pro_camera_glide.gd` eases the camera towards targets, so a key press or a mouse drag
sets where to go and the camera gets there smoothly:

```gdscript
const ProCameraGlide := preload("res://scripts/pro_camera_glide.gd")

var glide := ProCameraGlide.new(cam)

func _process(delta: float) -> void:
	if Input.is_key_pressed(KEY_W):
		glide.move_view(400.0 * delta, 0.0, 0.0)       # the eye's target moves; the eye follows
	glide.update(delta)
	cam.apply(camera)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		glide.turn(event.relative.x * 0.1, -event.relative.y * 0.1)
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_UP:
		glide.set_target("focal", glide.target("focal") * 1.1)
```

`set_target(property, value)` works for `eye`, `heading`, `pitch`, `roll`, `focal`, `aperture`, `focus` and `ev`;
`move`, `move_view`, `turn`, `follow` and `focus_on` set the targets as the camera's own functions set the camera, so
`glide.focus_on(point)` is a smooth focus pull onto a point. `update(dt)` closes each property on its target by 1 -
e^(-rate dt) of what is left, so it settles in about 3 / `rate` seconds at any frame rate: `rate` (12 per second) holds
for all, `rates` overrides it per property (`{"focus": 4.0}` for a slow focus pull), and 0 jumps. The heading takes the
short way round; the aperture eases between f-numbers but jumps to and from 0, which is no depth of field rather than a
wide-open lens. A property without a target is left alone, so setting the camera directly still works; a target is
dropped once reached, `settle()` jumps to them all and `drop()` lets go of them. `moving()` says whether anything is
still on its way.

A `Tween` interpolates the heading as a plain number, so from 350 to 10 degrees it turns the long way round; tween to
370 instead (the camera takes any heading), or use the glide, which takes the short way.

### Following a unit

`follow(target, offset, facing)` puts the eye `offset` from the target and aims at it. The offset is in the frame of
something facing `facing` (a heading), Godot style: -Z ahead, +X to its right, +Y up. So `Vector3(10, 30, 80)` is 80
behind, 30 above and 10 to the right; with `facing` 0 it is in world axes, for a camera that keeps its own direction
whichever way the unit turns. In a game, read the unit from the `GameWorld` each frame; `sage()` and `sage_heading()`
turn its position and orientation into Godot's terms. Follow its `drawable_position`, where it is drawn now, not its
`x`, `y` and `z`: the logic steps 5 times a second, as the original game's does, and the drawables are interpolated
between its steps, so a camera on the logic's position jumps 5 times a second while the unit glides. Its `angle` is the
logic's too, so a facing taken from it turns in steps; the glide smooths that out, a rigid `follow` does not.

```gdscript
func _process(delta: float) -> void:
	var unit: Dictionary = world.get_object(unit_id)
	var at := ProCamera.sage(unit.drawable_position) + Vector3(0.0, 12.0, 0.0)   # drawn now; its chest, not its feet
	var facing := ProCamera.sage_heading(unit.angle)
	glide.follow(at, Vector3(10.0, 30.0, 80.0), facing)   # over its right shoulder, easing after it
	glide.update(delta)
	cam.aim(at)                                          # the eye trails smoothly; the aim and focus stay on the unit
	cam.apply(camera)
```

`cam.follow(...)` instead of the glide's sticks to the unit rigidly. Leave out the `aim` to let the view trail as well,
which feels more like a camera operator following along. To keep the focus on the unit while the camera looks elsewhere
(a unit walking into a shot from the side), `focus_on(at)` or `glide.focus_on(at)` each frame.

### A path through several points

A `Tween` goes from one value to the next. For a crane or a sweep through several points, interpolate the eye along
them, here a smooth curve through every point (`cubic_interpolate`, a Catmull-Rom spline) with the move easing in and
out over its whole length:

```gdscript
const PATH: Array[Vector3] = [Vector3(1500, 60, -2600), Vector3(1600, 140, -2750), Vector3(1720, 320, -2900)]
const LOOK := Vector3(1650, 20, -3000)
var t := 0.0

func _process(delta: float) -> void:
	t = minf(t + delta / 12.0, 1.0)                  # the whole move in 12 seconds
	var s := smoothstep(0.0, 1.0, t) * (PATH.size() - 1)
	var i := mini(int(s), PATH.size() - 2)
	var eye := PATH[i].cubic_interpolate(PATH[i + 1], PATH[maxi(i - 1, 0)], PATH[mini(i + 2, PATH.size() - 1)], s - i)
	cam.look_at(eye, LOOK)
	cam.focal = lerpf(35.0, 85.0, t)                # zooming in on the way
	cam.apply(camera)
```

An orbit is the same with the eye on a circle: `LOOK + Vector3(sin(a), 0, cos(a)) * radius + Vector3.UP * height`.
Godot's `Curve3D` (or a `Path3D` drawn in the editor) gives the same with handles to shape the curve:
`curve.sample_baked(t * curve.get_baked_length())`.

### The effect on its own

The effect works on any `Camera3D` without the camera layer. It knows nothing of the camera's field of view, so set the
same lens on both for the blur to match the picture:

```gdscript
const Lens := preload("res://scripts/pro_lens.gd")
const LensEffect := preload("res://scripts/pro_lens_effect.gd")

var lens := LensEffect.new()

func _ready() -> void:
	lens.focal = 85.0
	lens.aperture = 2.0
	lens.focus = 300.0                       # world units along the view
	var size := get_viewport().get_visible_rect().size
	lens.image_mm = Lens.image_size(size.x / size.y).x
	camera.keep_aspect = Camera3D.KEEP_WIDTH
	camera.fov = Lens.fov_from_focal(lens.focal, lens.image_mm)
	LensEffect.attach(camera, lens)          # call again each frame on a camera OpenBFME draws, to keep its place
```

For a video at a steady frame rate, whatever the machine renders, add
`--write-movie shot.avi --fixed-fps 30` to the chosen scene's Godot launch command
(desktop Linux/macOS/Windows, Godot 4.7 and a configured project/scene; game scenes also need
the built extension and retail mount variables). The window's size is the movie's.

In a game, use a `Camera3D` of your own and make it current, then make the game's camera current again to give the view
back. The HUD (`InGameHudNode`) writes the game's camera every frame from the tactical camera, so it would undo a pose
put on that one. The game's camera state is never touched, so switching back gives back exactly the view the player had.
Two things stay with the game's camera: the audio microphone and the terrain fog shift. Everything that culls, sorts or
faces the camera (the instancers' pose culling, transparent sorting, streaks, particle billboards) follows the
viewport's current camera, so it follows this one.

## The pose

An eye and a turn, in Godot world coordinates:

- `heading`, degrees: 0 looks along -Z, which is OpenBFME's map north; 90 along +X, east.
- `pitch`, degrees above the horizon: `turn()` clamps it to -89.9 to 89.9; direct assignment is not clamped.
- `roll`, degrees about the view, clockwise as seen from behind the camera (the picture turns the other way).

`forward()` and `right()` give the view's direction and its level right. `sage()` turns a map point (SAGE axes) into
Godot's: (x, z, -y).

## The lens

A full-frame camera, a 36 x 24 mm sensor:

- **The crop.** A picture of aspect 3:2 or wider is 36 mm wide on the sensor, a narrower one 24 mm tall, as a camera
  takes the largest frame of its aspect (`sensor`).
- **Focal length.** `focal` mm gives a picture w mm wide a horizontal field of view of 2 atan(w / 2 f). At 3:2 and
  wider, 39 mm is 50 degrees, 24 mm is 74, 200 mm is 10.
- **A frame inside the window.** `frame` (an aspect; 0 is the whole window) keeps the focal length on that frame's crop,
  and the window shows more around it (`image_size`, `frame_rect`), as a camera's viewfinder shows beyond a crop.
- **Depth of field** is the thin lens. A point at distance D, focused at S with aperture f/N, blurs to a circle of (f /
  N) x f / (S - f) x |D - S| / D mm on the sensor, scaled to pixels by the image's pixels per mm (`blur_pixels`). D and
  S are along the view axis, so what is in focus is a plane, as with a real lens. `aperture` 0 keeps everything sharp.
  `sharp_range` gives the distances between which the blur stays under 1/1500 of the frame's diagonal.
- **Scale.** Distances need one: `units_per_metre` world units to the metre (10 by default, so a soldier about 20 units
  tall is the 2 m he would be). With it, f/1.4 at 85 mm blurs as a real lens would at that size: focused across the map
  (1,000 units and more) little blurs; focused within a few hundred units, the distance does.
- **Exposure.** `ev` multiplies the linear light by 2^ev: brighter above 0, darker below. It cannot bring back
  highlights that were already white.

## The effect

`pro_lens_effect.gd` runs after the transparent pass, on the frame in linear light:

1. Each pixel's blur circle, from its depth by the thin lens.
2. The largest blur in each 16 x 16 tile, then the largest of the tiles each tile can reach (at most `max_blur` of the
   width across, 6 % by default): how far the blur must look around each pixel.
3. The blur: Dennis Gustafsson's single pass ("Bokeh depth of field in a single pass", 2018) over a disc as large as
   that reach. Every sample whose own circle reaches the pixel joins a running average with equal weight, and one that
   does not adds the average so far. So a blurred foreground spreads over a sharp background with a soft outline, while
   a sample behind the pixel counts with at most twice the pixel's own circle, so a sharp foreground keeps its edge.
   Light keeps its energy, so a bright point out of focus spreads into a bright disc. The disc's sample pattern turns
   per pixel, which trades the repeated copies a fixed pattern gives in large blur for fine grain.
4. A 5 x 5 binomial resolve of the gathered colour, guided by depth and blur radius. It fades in between blur radii of
   1 and 3 pixels, leaving focused pixels alone; neighbours on different surfaces or with different blur radii are
   rejected. Colour and depth gathers keep their original nearest sampling; smoothing is confined to the resolve.
   This reduces sampling grain without raising `taps` or accumulating previous frames.
5. The exposure.

On a camera OpenBFME draws, `attach()` keeps the effect right after the gamma composite's decode (the last
`GammaCompositeEffect`, lane RENDER-3), where the colour is linear again; the map's colour grade
(`LookupTablePostEffect`, which keeps itself last) runs after it. OpenBFME puts its own pair onto whichever camera is
current, every frame, so `apply()` checks the order every frame and moves the effect only when it is out of place. On a
plain camera it is the only effect.

`taps` (128) sets the blur's samples per pixel: more is smoother and slower; 256 suits a still. `max_blur` (0.06) caps
every blur circle at that share of the picture's width, and with it how far each pixel looks around: a larger cap lets a
strong blur grow larger and is slower, a smaller one is faster and clips the largest circles. Both are on the camera
too, which hands them to the effect. With depth of field enabled, the resolve adds one compute pass and one full-size
RGBA16F intermediate (8 bytes per internal-resolution pixel); exposure and log output alone skip the gather and resolve
filter. Without an aperture, an exposure, log output or pending distance requests the effect does no work. It also
reads the distance along the view at a pixel, to focus on whatever is drawn there: `ask_distance(pixel)` (a pixel of
the viewport) returns a request number, and once `answered` reaches it, `distance_of(number)` is the answer, along the
view like `focus` (below 0 where nothing is drawn: the sky, or a pixel outside the picture; NAN before it is answered).
Requests may overlap: each is answered with its own pixel, in the order asked, up to 16 a frame, and the latest 256
answers are kept.

It needs the Forward+ renderer: a RenderingDevice, and a 3D colour buffer in RGBA16F, which its shader writes. Without
them (headless, the Compatibility renderer) it draws nothing and says why, once, as a warning and in `problem`; a
distance request is then answered at once, below 0.

What it does not do: transparent things (water, particles, glass) write no depth, so they blur with whatever lies behind
them; there is no motion blur, no lens distortion and no bokeh shape other than a disc.

## Log output: grade later

`log_output` (off by default) writes the picture as **ACEScct**, a log encoding that keeps the picture flat so it can be
colour graded afterwards in an editor (DaVinci Resolve and others read it with their ACEScct input transform):

1. The linear light, after the exposure, from Rec. 709 primaries (sRGB's) into ACES AP1, with the Bradford-adapted
   matrix the ACES project publishes.
2. The ACEScct curve (Academy S-2016-001): linear below 2^-7, (log2 x + 9.72) / 17.52 above.
3. Handed over so that Godot's sRGB encode of the output turns it back into the ACEScct values: the picture saved or
   recorded holds ACEScct, not a picture to look at as it is.

What comes after the effect would change those values, so while log output is on it is made neutral: the map's colour
grade (`LookupTablePostEffect`) is switched off, as it would bake the map's look in, and the environment the camera
draws with gets a linear tone map at exposure 1, no glow and no adjustments (OpenBFME's "enhanced" look has AgX tone
mapping and glow). Turning `log_output` off, `detach()`, or freeing the effect puts them back as they were. OpenBFME
shares that grade and environment among its cameras, so detach before handing the view back to the game. What is drawn
over the 3D picture is not encoded: the HUD and text, and the Brightness setting's full-window gamma pass, which draws
nothing at its default of 1.0 and must stay there for log output.

The picture is 8-bit when saved as PNG or recorded by the movie writer, and OpenBFME's lighting stays within the
original game's range, so log output gives room in the shadows and mid-tones rather than recovering clipped highlights.

## Tests

`godot/tests/pro_camera_test.gd` needs no game files. From the repository root,
with Godot 4.7 available as `godot` on PATH (Linux/macOS POSIX shell, or Windows
PowerShell/cmd with that executable name):

```sh
godot --path godot --script res://tests/pro_camera_test.gd
```

- **Phase 1, anywhere** (also with `--headless`): the lens maths against hand-worked values; the pose (`look_at`, turns,
  roll, moves that change the eye at once, aim, follow with an offset in a turned frame, focus along the view for a
  point off centre) and what `apply()` sets on a `Camera3D`, a steep pitch turning smoothly to the vertical; the glide
  (easing at any frame rate, per-property rates, the heading's short way, the aperture's jump from 0, settling, at map
  coordinates too, follow and focus_on); and `attach()` keeping the effect right after the gamma composite's decode on a
  compositor that holds OpenBFME's own effects; log output switching the map's colour grade off and on again. Headless,
  the effect saying it has no RenderingDevice and answering a distance request at once.
- **Phase 2, with a GPU and a window:** grey stripes 20 units away over the upper half of the picture and 2,000 away
  over the lower half, through an 85 mm lens. Focused near at f/1.4, the near stripes keep their contrast and the far
  ones lose most of it; +1 EV brightens the picture; with log output each pixel is the ACEScct value of the same pixel's
  linear light without it; `ask_distance`, asked for three pixels before the next frame, reads 20 and 2,000 at the
  stripes and below 0 outside the picture, each request with its own pixel; and the
  effect on its own, attached to a plain `Camera3D` without the camera layer, blurs the far stripes as well. When
  headless, phase 2 is reported as skipped.

It prints `LENS <check>: ok` or `FAIL` and `LENS RESULT`, and exits 0 when everything passed, 1 when a check failed, and
77 when phase 1 passed and phase 2 was skipped, as OpenBFME's other GPU tests do.

### Grain regression and local comparison

`godot --path godot --script res://tests/pro_camera_bokeh_test.gd` generates a fine checkerboard, isolated lights and a
focused foreground from code. It checks the default 128-tap result against a dense 2,048-tap reference, focused detail
and silhouette against stopped-down output at matching exposure, and that blur actually changes the defocused
background. After viewport resizing it compares the reused effect with a fresh effect and verifies that blur remains
visible. It exits 77 without a GPU.
The reference uses the same resolve, so this measures sampling stability rather than optical ground truth.

Optional user arguments after `--`: `--out=<directory>` saves PNGs; `--before=<previous pro_lens_effect.gd>` also renders
that local baseline with the same scene and lens at 128 taps. Keep review captures outside tracked source. The baseline
must be a trusted local script. These generated captures contain no game assets.
