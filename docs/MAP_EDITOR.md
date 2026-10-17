# In-game map editor

Start a local skirmish, open the Lua console, then click **Map editor**. Editing
pauses the simulation and captures gameplay input. **Exit editor** or **Esc**
restores the previous pause state. Network games, replays and campaigns are excluded.

- **Delete objects:** click an object, or drag a rectangle to delete objects whose
  origins fall inside it. This includes objects you cannot normally select.
- **Place object:** search the template list, select an item, then click the map.
  A translucent model previews the placement where a default model is available.
  Choose its owner slot and rotation; **R** rotates by 15 degrees.
- **Object brush:** stamp the selected object over a square or circular area. The
  height-step/spacing control sets spacing. Each stroke is capped at 256 objects.
- **Raise / Lower terrain:** click or drag a block or circular brush; the height
  step sets how far each covered sample moves per stroke.
- **Flatten terrain:** brush the ground to the height of the stroke's first point.
- **Paint ground texture:** browse all mounted terrain.ini textures by type and region,
  or search by name, filename, or tags, then paint a square or circular brush.
  Textures absent from the original map are added when painted. The selected texture image and source
  filename appear below the selector. Painting uses the full texture pattern,
  aligned to map coordinates across separate strokes. Painted cells replace existing blends.

Terrain brushes update while dragging, with one undo action per stroke. Object
placement commits on release and appears immediately while the editor is paused.
Right-drag or a two-finger touch drag pans; the wheel zooms. Terrain updates
rebuild the terrain mesh and navigation map, so large maps may briefly pause. Terrain-attached objects follow the new ground. Existing authored cliff and
passability masks are retained; the editor does not reauthor those masks.

**Undo** reverses the last stroke or object action. Deleted objects are restored
as fresh instances of their template and owner, rather than restoring their
runtime health, orders, inventory or purchased upgrades. Delete applies to live
engine objects; baked roads, water and other non-object terrain layers are not
object-delete targets.

**Save edits** writes a versioned JSON overlay under `user://map_edits/`.
**Load edits** applies it to the same map after starting a fresh skirmish. The
original retail `.map` and archives are never modified. These are edit overlays,
not standalone WorldBuilder map exports or saved matches.

**Pick terrain texture** previews the base ground texture under the cursor. Click
to select that texture and switch to paint mode. **Rotate brush grid 45°** rotates
square brush footprints and the object-brush lattice; the map heightfield itself
retains its fixed grid. Fog rendering is temporarily disabled in the editor so
new neutral objects remain visible while the simulation is paused.

## Roads and terrain paths

Choose **Edit roads / paths** to show the map's road centerlines and endpoint
handles. Select a segment on the map or in the list. In **Select / move points**,
drag a handle to move it; coincident endpoints move together to keep the path
connected. The line previews the drag and the textured road rebuilds on release.

**New path** starts a connected chain: click the first point, then click each
additional point. Select an existing endpoint and switch to **Add connected
points** to extend it. **Insert point on segment** splits a clicked segment into
two, giving it another movable point. Choose a road type and **Apply road type**
to change the selected segment, or **Delete selected segment** to remove it.
Road changes support Undo and the same saved JSON overlays as terrain edits.

Roads use the existing straight-strip renderer; curved junction geometry and
bridges are outside this tool. Road paths change the visible ground overlay,
not unit navigation waypoints or passability.

**Select objects / owner** selects an object by clicking, or multiple objects by
dragging a rectangle. Set **Owner player slot** (0 is neutral), then click
**Set selected objects owner**. Ownership changes in place and supports Undo and
saved JSON overlays. The chosen slot must exist in the current match.

For **Place object**, hold **Shift** to show the 50-unit grid and snap position
and rotation (45° steps). Press to anchor an object, drag to set its facing, then
release to place it. A simple click uses the current rotation.

The editor workspace has a top mode/action toolbar, a left tool inspector, and a
right lookup window. Object modes show a searchable object library with an
isolated render-to-texture model preview that automatically centers and fits the
selected model. Terrain modes show the categorized texture library and image
preview in the same area. The toolbar includes persistent grid snapping, brush
orientation, Undo, Save, Load, and Exit controls.
