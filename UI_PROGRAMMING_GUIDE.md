# UI Programming Guide

How to build a user interface on `snd::ui`. Complements PROGRAMMING-GUIDE.md
(which covers the whole library); this file is only the UI layer.

## Model

`snd::ui` is **Dear ImGui** (immediate mode) plus a thin native-window shell
and a set of audio-oriented widgets drawn on top via `ImDrawList`. ImGui is
vendored unmodified. You include `snd/ui.h`, open a `Window`, and each frame
call stock `ImGui::` functions and `snd::ui::` widgets to declare the UI. State
lives in *your* variables, not in the immediate widgets.

SND's retained-mode UI layer is currently exposed in C++ as
`snd::ui::retained` through `snd/ui_retained.h` and retained widget helpers. It
uses a stable tree for layout, focus, events, dirty state, and semantics, but
shares SND's paint/style vocabulary with immediate widgets.

Immediate-mode consequences:

- A widget both draws and reports interaction in the same call. `knob(...)`
  returns `true` on the frame its value changed; `toggle(...)` returns `true`
  the frame it flips.
- You pass the value in and the widget mutates it in place (`float* value`),
  or you read the bool return. There is no callback registration and no
  "get widget by id" — you already hold the state.
- Every widget takes a string `id`/`label`. Identical visible labels need
  distinct ids: use the `"Label##uniqueid"` suffix or `ImGui::PushID`.

## The window and the frame loop

```cpp
#include "snd/ui.h"

snd::ui::Window window;
if (!window.create(800, 600, "My App"))   // decorated native window
    return 1;

while (!window.shouldClose()) {
    if (!window.beginFrame())   // pumps events, starts the ImGui frame
        break;                  // false = the window is gone

    ImGui::Begin("Panel");
    // ... declare widgets ...
    ImGui::End();

    window.endFrame();          // renders the frame + swaps buffers
}
window.destroy();
```

`beginFrame`/`endFrame` bracket exactly one frame. Do all UI between them.
`shouldClose()` reflects the OS close button; override it with
`setShouldClose(bool)` (e.g. hold the window open to show an unsaved-changes
prompt).

## Frameless windows and a custom title bar

`create(w, h, title, /*decorated=*/false)` gives a borderless window; you draw
the title bar yourself and drive the OS with:

- `getPosition(x,y)` / `setPosition(x,y)` / `setSize(w,h)`
- `minimize()`; `toggleMaximize()` / `isZoomed()` — zoom, not fullscreen:
  fill the screen's working area keeping the app-drawn title bar (Mac
  title-bar double-click behaviour), toggling restores the previous frame.
- `toggleFullscreen()` / `isFullscreen()` — native fullscreen (a macOS
  Space; monitor fullscreen elsewhere), no title bar while active: the
  green-glyph behaviour.
- `beginNativeDrag()` — call once when a mouse press lands on your title-bar
  area; the OS then drags the window (native and smooth on macOS/Windows).

`width()` / `height()` return the current client size. `setTitle(...)` updates
the OS title (also shown in the taskbar/dock).

## Multiple windows

Construct more than one `Window`. Each gets its own ImGui context; GL objects
(textures, fonts) are shared automatically across windows, so an image loaded
in one is usable in another. Give every window its own frame loop, or pump them
in sequence each iteration:

```cpp
for (auto& w : windows) {
    if (!w->beginFrame()) { /* mark for removal */ continue; }
    /* declare that window's UI */
    w->endFrame();
}
```

`beginFrame` makes that window's ImGui context current for the duration of the
frame, so widget state does not leak between windows.

## File drops

`takeDroppedFiles()` returns the paths dropped onto the window since the last
call, then clears them. Poll it each frame.

## Theming: the palette

Every `snd::ui` widget reads a shared `Palette`. One `setPalette(p)` re-skins
them all — knobs, LEDs, meters, faders, headers.

```cpp
snd::ui::Palette p = snd::ui::palette();  // start from the current one
p.accent = IM_COL32(120, 200, 255, 255);
snd::ui::setPalette(p);
```

Palette fields: `accent` / `accentDim` (active elements), `text` / `textDim`,
`frame` / `frameBright` (control bodies / borders), `ledOff`, and the meter
zone colours `meterLow` / `meterMid` / `meterHot`. Colours are `ImU32`
(`IM_COL32(r,g,b,a)`). Stock `ImGui::` widgets follow ImGui's own style, not the
palette — set `ImGui::GetStyle()` colours if you want them to match.

For a look outside the palette's reach (a bespoke skin), draw directly with
`ImGui::GetWindowDrawList()` and your own colours, as the widgets do internally.

## Icon fonts and glyphs

SND embeds two icon fonts; `<snd/icons.h>` provides named glyph constants.

- **Material Icons** (`ICON_MD_*`, Apache-2.0) is merged into the default
  font, so the glyphs work inline anywhere text does:
  `ImGui::TextUnformatted(ICON_MD_FOLDER " Open")`, button labels, menus.
- **Lucide** (`ICON_LC_*`, ISC) is a second font: draw with
  `iconFontLucide()` (e.g. pass it to the tactile icon button, or push it
  around `ImGui::Text`).

`iconFontMaterial()` returns the default font (Material merged in);
`loadFonts()` is internal — `Window::create` calls it per context. Both
accessors return the font for the *current* window's context.

The tactile icon button is the standard home for a lone glyph:

```cpp
snd::ui::iconButton("cfg", ICON_MD_SETTINGS);                  // Material
snd::ui::iconButton("brush", ICON_LC_BRUSH, {0, 0},
                    snd::ui::iconFontLucide());                // Lucide
```

`MenuItem::icon` takes the same glyph strings; `MenuOptions::iconFont`
switches the font a menu draws them with.

## SVG and image assets

`<snd/ui.h>` includes a small SVG pipeline (nanosvg) for logos and vector
art that must stay crisp at any size or DPI:

- `rasterizeSvg(svgText, heightPx, tint = 0)` → `SvgBitmap{rgba, w, h}`.
  GL-free and headless-safe (usable in tests). `tint` multiplies every texel:
  recolour a monochrome glyph, or fade one toward a watermark.
- `loadSvgTexture(svgText, heightPx, tint = 0)` → `SvgTexture{id, w, h}`.
  Uploads to a GL texture — call it once a context exists (after
  `Window::create`, or inside the frame loop) and feed `id` to
  `ImGui::Image`. Release with `releaseTexture(id)`.

Width follows the source aspect ratio; a parse failure returns an empty
bitmap / invalid texture id.

PNG decoding is built in (vendored stb_image, PNG-only build):
`decodeImage(bytes, byteCount)` → `SvgBitmap` is GL-free/headless like
`rasterizeSvg`, and `loadImageTexture(bytes, byteCount)` decodes and uploads
in one call. For raster pixels from any other source (a generated bitmap,
another decoder), `loadTextureRGBA(rgba, w, h)` uploads straight-alpha RGBA8
directly. All three return the same texture struct; release with
`releaseTexture`.

## Shared paint and semantics

SND-owned immediate and retained widgets share visuals through `snd/ui_paint.h`.
Those helpers are draw-only: callers still own layout, hit-testing, value
binding, and accessibility nodes.

Use the same control states in both modes: `hovered` previews, `active`
presses/drags, `focused` draws `paint::drawFocusRing`, `disabled` mutes and
rejects actions, and `selected` marks latched state.

Composable gradient primitives live alongside the widget painters:
`paint::drawGradientRect(min, max, top, bottom, rounding)` fills a rounded
rect with a vertical gradient (ImDrawList has no rounded multi-colour rect),
and `paint::drawGradientArc(center, radius, a0, a1, colStart, colEnd,
thickness, segments)` strokes a two-colour arc sampled per segment — the
hardware-style panel bodies and gradient value arcs custom painters need. Shared
paint helpers keep existing `ImDrawList*` overloads and add
`draw::Surface&` overloads where the body is renderer-neutral. The neutral
surface supports per-corner rounded rectangles through masks such as
`draw::kRoundCornersTop` and `draw::kRoundCornersBottom`, so surface-only
painters can match keys, tabs, title strips, and module chrome without falling
back to ImDrawList flags.
`draw::Surface::image(texture, min, max, tint, uvMin, uvMax)` draws decoded
texture quads through the same renderer seam. Use it for menu row thumbnails,
SVG/PNG-backed icons, and custom painter imagery; the caller owns the texture
lifetime and should pass an SND texture id from `loadSvgTexture`,
`loadImageTexture`, or `loadTextureRGBA`.

Widget layers must provide accessibility semantics. Icon-only controls need an
accessible name and action; sliders/knobs/faders need range, value, value text,
and value actions; meters expose read-only value text; decorative icons or LEDs
must be hidden or marked as non-interactive status.

### Custom painters

Include `snd/ui_paint.h` when you want to skin a control body without
forking SND interaction. Four hooks share the contract: `paint::KnobPainter`,
`paint::ButtonPainter`, `paint::XYPadPainter` (custom maps/pucks over the
standard two-axis drag), and `paint::PatternCellPainter` (per-cell bodies —
velocity gradients, spans, chips — while SND keeps the grid frame, playhead
tint, drag-paint interaction, border, and focus). Each draws only from its
args struct. SND still owns hit-testing,
drag/key handling, value mapping, focus rings, semantic names/actions, and
accessibility. Painters must not create ImGui controls, read input, mutate app
or plugin state, touch audio-thread state, or own model data. The state passed
to a body painter has focus suppressed; SND draws the focus ring afterward so a
custom face cannot remove keyboard visibility.

Painter args now carry both `drawList` and `surface`. Under the ImGui backend
both are populated, so existing ImDrawList painters keep working. Prefer
`a.surface` for new draw-only bodies; a painter that uses only
`snd::ui::draw::Surface` can run on a non-ImGui retained renderer later. The
button painter args also carry `fontRef` and `fontSizePx` so surface-only
button faces can call `paint::drawDefaultButton(a)` and keep their label text.
The default knob, button, toggle, LED, meter, fader, XY pad, pattern-grid,
menu, row, graph, focus, and gradient helpers use the surface path when one
is supplied.

Immediate example:

```cpp
snd::ui::button("Generate", {110.0f, 52.0f}, [](const snd::ui::paint::ButtonPaintArgs& a) {
    snd::ui::paint::drawDefaultButton(a); // Or draw your own body with a.surface.
});

snd::ui::knob("Tone", &tone, 0.0f, 1.0f, [](const snd::ui::paint::KnobPaintArgs& a) {
    snd::ui::paint::drawDefaultKnob(a); // Keeps SND's default as a composable base.
});
```

Retained example:

```cpp
auto gain = w::knob("gain", "Gain", binding, &renderer,
                    snd::ui::KnobStyle::Ring, false, 56.0f,
                    [](const snd::ui::paint::KnobPaintArgs& a) {
                        snd::ui::paint::drawDefaultKnob(a);
                    });
```

### The knob modulation ring

`KnobMod` adds a modulation overlay to any knob: a thin arc sweeping `depth`
of the normalized range away from the current value, plus a live dot at the
modulated position (`value`; pass `< 0` to hide it). SND draws the overlay
after the body — built-in styles or custom painters — and before the focus
ring, so modulated knobs keep one shared look that a painter cannot remove.
`color = 0` uses a light accent tint.

```cpp
snd::ui::KnobMod mod;
mod.depth = lfoDepth;          // signed; the arc grows from the value angle
mod.value = modulatedPosition; // live 0..1, recomputed per frame
snd::ui::knob("Cutoff", &cutoff, 0.0f, 1.0f, snd::ui::KnobStyle::Ring, mod);
```

Retained knobs take a poll function so a stable tree can show live modulation:

```cpp
auto cutoff = w::knob("cutoff", "Cutoff", binding,
                      [&]() { return snd::ui::KnobMod{lfoDepth(), lfoPos()}; },
                      &renderer);
```

### Randomise windows (ghost fences)

`paint::KnobWindow{lo, hi, locked}` is a ghost-fence overlay pattern: an
independent randomise window per control, value-independent at both ends —
which is why it is not `KnobMod` (one signed depth anchored to the current
value). SND ships the drawing and the pure hit-tests; the caller owns the
overlay mode, drag state, and click-vs-drag lock toggling:

- `paint::drawKnobWindow(dl, topLeft, size, win, pal, accent=0, lockColor=0,
  uiScale=1)` — window arc just outside the knob circle, a grab dot at each
  end, and a padlock at the top-right while locked. Call after the knob body.
- `paint::knobWindowHitEnd(topLeft, size, win, pressPos, uiScale=1)` → `0`
  (lo), `1` (hi), or `-1` (outside the rect grown by 6·uiScale px). Inside,
  the nearest end by knob angle wins.
- `paint::drawComboWindow` / `paint::comboWindowHitEnd` — the same window as
  an index bracket along a combo's bottom edge (3·uiScale px grow, horizontal
  mapping).
- `paint::drawPadlock(dl, mn, mx, color)` — the lock glyph standalone, for
  locked toggles or rows.

The fence dims itself while locked; `lockColor = 0` uses `pal.meterHot`.
`paint::kKnobA0` / `paint::kKnobA1` (the rotary sweep endpoints) are public so
consumer-drawn overlays land exactly on the arc the knob bodies draw.

## Retained widgets

Include `snd/ui_retained_widgets.h` to build retained panels with the shared
SND look. The helper namespace is `snd::ui::retained::widgets`; the renderer is
`snd::ui::retained::PaintRenderer`.

Current helpers cover `row`, `column`, `panel`, `gradientPanel`, `label`,
`sectionHeader`, `badge`, `listItem`, `menuItem`, `popupMenu`,
`dropdownMenu`, `contextMenuRegion`, `button`, `outlineButton`,
`segmented`, `cycleButton`, `ledButton`,
`animatedButton`, `iconButton`, `toggle`, `knob`, `fader`, `meter`, `led`,
`patternGrid`, `xyPad`, `keyboard`, `valueRow`, `dragNumber`, `valueField`,
`envelopeEditor`, `canvas`, and `graphSurface`. Value controls use `ValueBinding`; the caller
still owns plugin parameters, app state, undo, and audio-thread handoff. Knobs,
faders, drag numbers, XY pads, pattern grids, keyboards, menus, and envelope
editors support retained pointer editing through the ImGui input bridge or the
pure retained GL window.
Meters and LEDs can also use `ValueBinding` so a stable retained tree can
display changing audio/UI state without recreating nodes. `xyPad` takes
separate X/Y bindings, while `patternGrid` edits a caller-owned row-major bool
array and can read a dynamic playhead callback.

The normal retained frame call is:

```cpp
snd::ui::retained::drawImGui(tree, renderer, {360.0f, 180.0f});
```

That refreshes `ValueBinding` values with `tree.refreshBoundValues()`, lays out
the tree, reserves the ImGui item rectangle, dispatches current mouse/keyboard
input, and renders through the shared paint layer. If you wire the pieces
manually, call `tree.refreshBoundValues()` before rendering or taking semantic
snapshots whenever caller-owned model values may have changed.

For a retained window with no ImGui context, include `snd/ui_retained_gl.h` and
use `snd::ui::retained::GlWindow`:

```cpp
snd::ui::retained::GlWindow window;
window.create(800, 600, "Retained UI");

while (!window.shouldClose()) {
    if (!window.beginFrame(tree, renderer))
        break;
    window.endFrame();
}
```

`GlWindow::beginFrame` pumps GLFW input into retained events, refreshes
caller-owned bindings, lays out the tree to the current window size, renders
through `PaintRenderer::render(tree, draw::Surface&, FrameContext)`, and
swaps in `endFrame`. This path supports SND-owned retained widgets and
`CanvasSurfaceDraw` painters. Old `CanvasDraw` or custom painters that only
use `ImDrawList*` remain ImGui-backend-only; give new custom retained regions
a `draw::Surface&` painter when they should run everywhere.

Use `canvas` for direct-to-screen animated regions such as waveforms, spectra,
meters, playheads, and other views whose node should own layout, focus,
hit-testing, and semantics while custom code draws inside its bounds.
Canvas-backed controls clip drawing to their node rect by default; set
`VisualStyle::canvasClip = false` only for deliberate bleed effects.
Canvas-backed controls expose focus and names today; add richer custom value
text where practical, and prefer built-in helpers when one exists.
Use `widgets::gradientPanel(...)` only for decorative retained chrome; it is
semantic-hidden and non-interactive.

Use `widgets::patternGrid(id, name, cells, rows, steps, &renderer, size,
playheadFn, cellPainter)` for step grids. `cells` stays caller-owned, mouse
drag paints, and Alt-drag erases; the optional trailing
`paint::PatternCellPainter` custom-draws cell bodies. Use
`widgets::xyPad(id, name, xBinding, yBinding, &renderer, size, painter)` for
two-axis controls over normalized values; the optional trailing
`paint::XYPadPainter` custom-draws the pad body. Use
`widgets::keyboard(id, name, firstNote, octaves, noteOn, noteOff, lit,
&renderer, size)` for piano surfaces; `noteOn`/`noteOff` fire from retained
pointer transitions and `lit` is an optional caller-owned `bool[128]`.
Use `widgets::envelopeEditor(id, name, points, &renderer, size, tensions)` for
breakpoint envelopes. `points` and optional `tensions` stay caller-owned;
dragging edits points or segment bend, double-clicking empty space adds a
point, and right-click/context-click deletes an interior point.
Use `widgets::valueRow(id, name, binding, &renderer, size)` for compact
read-only numeric/status rows; it keeps the setter out of retained semantics.
Use `widgets::dragNumber(id, name, binding, &renderer, size, dragSpeed)` for a
compact slider-like value row that edits horizontally; Shift-drag is fine
adjustment and keyboard/semantic increments use `ValueBinding::step`.
Use `widgets::valueField(id, name, binding, &renderer, size, style, dragSpeed)`
for numeric fields that should drag horizontally and enter inline text edit on
double-click or Enter/Space while focused. It owns the temporary UI edit
buffer; the caller still owns the bound value. Text editing uses retained
`EventType::TextInput` plus `KeyDown` for Enter, Escape, Backspace, Delete,
Left/Right, Home, and End; non-ImGui hosts should emit the same events.
Use `widgets::led(..., &renderer, radius, onColor)` when a retained LED needs
the same radius or accent override as the immediate LED helper.
Use `widgets::outlineButton(id, name, onActivate, &renderer, size, style,
selected)` for a border-first text action. The default style has no fill or
resting border, shows an accent border on hover, and fills immediately while
pressed. `paint::OutlineButtonStyle` can set fill, border, text, and selected
colours.
Use `widgets::segmented(id, name, labels, binding, &renderer, size)` for a
pill group of mutually exclusive options. The binding holds the selected
index (min/max/step are forced to `0..count-1` step 1, and a default format
reports the selected label); clicking picks a segment and Left/Right arrows
move the selection through the retained Increment/Decrement actions.
Use `widgets::cycleButton(id, name, labels, binding, &renderer, size)` for a
multi-state value button: Enter/Space/click advance through the options with
wrap-around, arrows step without wrapping, and the binding is the option
index as in `segmented`.
Use `widgets::ledButton(id, name, glyph, binding, blink, &renderer, size,
ledColor, font)` for tactile keys with an integrated status LED ring
(record-arm, monitor, latches). The binding is the on/off state as in
`toggle`; `blink` pulses the lit ring for armed/pending states.
Retained faders share the immediate fine-adjust behaviour: Shift switches the
drag from jump-to-position to a relative drag at 10% rate, and Shift on knob
drags is 10× slower.
Use `widgets::animatedButton(id, name, onActivate, &renderer, size)` for
generation/action buttons that need a subtle live sweep. It is Canvas-backed
for pixels, but remains a retained `Role::Button` with normal focus,
activation, and accessible-name expectations.
Use the `widgets::iconButton(..., Icon::Play, ...)` overload for retained
transport/tool buttons that should match the immediate vector icon buttons;
use the string-glyph overload for Material/Lucide tactile icon buttons.
Retained `widgets::button(...)` and `widgets::knob(...)` accept optional custom
painters using the same `paint::ButtonPaintArgs` and `paint::KnobPaintArgs`
contract as immediate mode.

## Semantics, focus, and headless testing

The retained core is the accessibility surface. Every node carries
`Semantics`: a `Role` (`Button`, `Toggle`, `Slider`, `Meter`, `ListItem`,
`Menu`, `Canvas`, ...), an accessible `name`, a `ValueRange` (numeric
value/min/max/step plus display `text`), state bits, and the `Action`s it
supports (`Activate`, `Increment`, `Decrement`, `SetValue`, `OpenMenu`,
`Focus`). The built-in widget helpers fill all of this in; custom nodes must
do the same — `validate()` returns `ValidationIssue`s for empty IDs,
duplicate IDs, and visible interactive nodes without an accessible name, and
belongs in headless tests for any non-trivial tree.

State bits (`SemanticState`): `Focusable`, `Focused`, `FocusVisible`,
`Disabled`, `Pressed`, `Checked`, `Selected`, `Expanded`, `Hidden`.

**Focus-visible.** Keyboard-acquired focus (`Tab` via `focusNext`, or
`Tree::focus(id)`) sets `Focused | FocusVisible` and draws the shared focus
ring; pointer-press focus is semantic focus only — `Focused` without
`FocusVisible`, no ring. The retained renderer maps
`paint::ControlState::focused` from `FocusVisible`, so widgets and custom
painters get the correct ring behaviour without doing anything; don't key
custom ring drawing off `Focused`.

**Two snapshot boundaries.** `nodeSnapshot()` (→ `NodeSnapshot`) is the
renderer/widget boundary: bounds, role, value, interaction state.
`semanticSnapshot()` (→ `SemanticNode`) is the accessibility boundary: what
an assistive technology (or a test) should see — names, value text, actions,
with `Hidden`/decorative nodes filtered. Native
accessibility bridges drive the tree through `semanticNode(id)`,
`performSemanticAction(id, action, value)` (which checks the node actually
exposes the action — prefer it over `performAction` at that boundary),
`value(id)` / `setValue(id, v)` / `incrementValue(id)` / `decrementValue(id)`.

**Headless tests.** The core runs without a window, GL, or ImGui: build the
tree, `layout(size)`, `dispatch(Event)`, assert on values and snapshots. This
is how `snd-example --selftest` exercises retained behaviour:

```cpp
r::Tree tree(std::move(root));
tree.layout({320.0f, 120.0f});
tree.focus("gain");

r::Event key;                       // keyboard increments the bound value
key.type = r::EventType::KeyDown;
key.key = r::Key::Up;
tree.dispatch(key);

r::ValueRange v;
tree.value("gain", v);              // assert v.value moved by one step
assert(tree.validate().empty());    // ids + accessible names hold up
```

**Custom controls.** Semantic actions beyond binding/activation need
`setOnAction(...)`. Canvas-backed widgets that draw structured internal parts
must not expose one opaque node: publish virtual children with
`setSemanticChildren(...)` (stable IDs, bounds, roles, values, actions) and
route their actions back with `setOnSemanticAction(...)` — the pattern graph
surfaces use, and the right one for any zoomable/spatial view where real
child layout nodes would fight the viewport transform. Caller-owned models
that a `ValueBinding` can't represent should install `setOnRefresh(...)`;
`refreshBoundValues()` invokes it so semantics stay fresh. Popup content that
floats over surrounding layout uses `Node::setOverlay(true)` — visible and
hittable without consuming row/column space.

**Input bridge details.** `dispatchImGuiInput(tree, origin, mouseCaptured)`
takes an optional flag: pass `true` when another ImGui widget owns the mouse
so the retained tree does not double-handle pointer events (`drawImGui` does
this for you). When wiring render manually instead of `drawImGui`, call
`renderer.prepareOpenPopups(tree)` after layout so anchored popups are moved
before hit-testing/paint, and `renderer.dismissOpenPopupsOutside(...)` on
outside clicks.

**Overlay z-order.** `PaintRenderer::render` paints overlay subtrees (popups,
flyout panels) in a second pass after the whole main tree, so a menu can never
sit under a later sibling. Hosts that composite their own layer between the
tree and its popups (e.g. ImGui-bridged module editors over a background-list
tree) use the split: `renderMain(...)` paints everything except overlays,
`renderOverlays(...)` only them — put the overlays pass on top of your layer
(`ImGui::GetForegroundDrawList()` in the bridged case).

**Splitter.** `widgets::splitter(id, name, binding, horizontal, invert,
renderer, thickness)` is a draggable pane divider: dragging (or arrow keys
while focused) writes the adjacent pane's size through the `ValueBinding`,
clamped to `[min, max]`; `invert` flips direction for far-edge-docked panes.
The splitter is stateless — rebuild or re-layout off the bound value.

**Window canvas colour.** `Window::setClearColor(IM_COL32(...))` sets the GL
clear behind everything the app draws; apps painting their own chrome should
set their canvas colour so padding/gaps don't show the library default.

## Menus

Use SND menu primitives for action lists, dropdown/select controls, and
right-click/context actions. Menu rows share one `MenuItem` model: `id`,
`label`, optional icon glyph, optional texture `image`, `separator`,
`enabled`, `checked`, `rightText`, `danger`, and optional nested `children`.

`MenuOptions` sets `width`, `itemHeight` (0 = defaults), and `iconFont` for
the glyph column. `MenuItem::image` (a `loadSvgTexture`/`loadImageTexture`
id, set by name — it is deliberately the last field) puts a texture icon in
the icon column instead of a glyph; the texture must outlive the menu. The
immediate `dropdownMenu` overload taking a
`paint::OutlineButtonStyle` styles the combo face (square hardware-style wells,
custom fills/borders) — the same passthrough the retained
`widgets::dropdownMenu` offers. `MenuResult` reports `activated`, the row `index`, its
`id`, and — for nested rows — `targetId`, the full path of the activated
child row.

Immediate simple popup:

```cpp
std::vector<snd::ui::MenuItem> items = {
    {"add", "Add module", ICON_MD_ADD},
    {"sep", {}, {}, true},
    {"duplicate", "Duplicate", {}, false, true, true},
    {"delete", "Delete", ICON_MD_DELETE, false, true, false, "Del", true},
};

if (snd::ui::outlineButton("Actions", {92.0f, 28.0f}))
    snd::ui::openPopupMenu("actions.menu");

auto chosen = snd::ui::popupMenu("actions.menu", items);
if (chosen.activated) {
    // chosen.id / chosen.index identify the action.
}
```

Immediate dropdown/select:

```cpp
int selected = 0;
auto picked = snd::ui::dropdownMenu("scale", items[selected].label.c_str(),
                                    items, &selected, {160.0f, 28.0f});
```

Immediate context menus attach to the previous item and open on right-click:

```cpp
snd::ui::patternGrid("steps", cells, rows, steps, {280.0f, 96.0f});
auto action = snd::ui::contextMenu("steps.context", items);
```

Retained helpers mirror the same model. `widgets::popupMenu(...)` builds a
retained menu list with separators, disabled rows, checked rows, icon labels,
optional nested submenu rows, typeahead by visible label prefix, keyboard
Up/Down, Enter/Space activation, Right/Left submenu open/close, and Escape
close. `widgets::dropdownMenu(...)` adds a retained combo-box face and popup
list using caller-owned `PopupMenuState` and selected index. Pass a
`paint::OutlineButtonStyle` to `widgets::dropdownMenu(...)` when the combo-box
face needs custom square/hover/selected treatment; do not restyle the generated
`<id>.button` child by ID from consumer code.
`widgets::contextMenuRegion(...)` opens a menu state from a retained
`ContextMenu` event or semantic `OpenMenu`, not from raw right-button down/up,
so right-clicks do not double-trigger normal retained activation. It also sets
`PopupMenuState::anchorToPosition` and records `PopupMenuState::position`; the
normal retained frame bridge moves the popup subtree to that tree-local anchor
before hit testing and render. Retained popup/menu children are overlay nodes:
they do not inflate row/column layout while open.

## Graph Surfaces

Graph UI starts from retained `widgets::graphSurface(...)`: the caller owns
modules, ports, cables, selection, and DSP/plugin graph state; SND owns shared
paint, viewport transforms, hit-region conventions, focus treatment, and
context-menu routing.

Backdrops (`GraphSurfaceStyle::Backdrop`): `Flat`, `Grid`, `GreenGrid`
(0xff04130b + fixed 38px screen-space grid at 0.09 alpha), `Mosaic` (58px
drifting HSV facets with a diagonal split and 0.40 veil), and `Aurora` /
`AuroraMosaic` (a displaced-mesh field with a 7-stop aurora palette, radial
wave + noise displacement; AuroraMosaic adds the bright wireframe pass). The
animated modes read the render-time clock.

Six house skins ship as presets (`paint::GraphSkin`: `TechSquare`,
`ClassicRounded`, `Blueprint`, `Console`, `Studio`, and `Neo` — a dark neon
look with a conic pink→purple→blue rim that spins while a node is selected,
purple→blue gradient cables, and a dual blue/pink glow).
`paint::graphSkinStyle(skin)` returns the full node/pin/wire
`GraphSurfaceStyle` and `paint::graphSkinName(skin)` its menu label; the
backdrop is an independent choice, so pick a `Backdrop` mode and pair it with
`paint::graphBackdropFill(mode)` / `graphBackdropName(mode)`. Building a
skin/background picker is a loop over `paint::kGraphSkinCount` and the modes —
consumers should not re-transcribe colour tables.

Neo's effects are plain `GraphSurfaceStyle` fields any skin can opt into, all
inert at 0: `rimA/rimB/rimC` + `rimSpinSeconds` (conic border sweep, replaces
`border`/`selectedBorder`; selection spins it — the spin reads the
`timeSeconds` argument on `drawModuleBox`, which the graph surface feeds from
the render clock), `wireGradientStart/wireGradientEnd` (per-segment cable
lerp), and `glowA/glowB` (soft offset halos behind the node body).
`headerStripe = false` drops the 3px header accent stripe for clean-slab
skins; when on, the stripe starts below the corner arc so it never juts past
rounded corners.

Viewport navigation: plain mouse wheel scrolls the canvas (shift for
horizontal), cmd+wheel zooms at the cursor, cmd/alt+left-drag or middle-drag
pans, and cmd+arrow keys step-scroll while the surface is focused.

Square pins whose node-local rect sits flush against the node's left or right
edge render as SOCKETS: the fill runs to the edge over the node border and
the outline skips that side, so a wire reads as plugging into the box. Pins
that straddle the edge (murk's) keep the classic full outline.

Double-click on a module (`NodeBody`/`NodeTitle`/`NodePart`) fires
`GraphSurfaceCallbacks::onNodeDoubleClicked(hit)`, typically to open an
editor or inspector. Double-click on empty canvas fires
`onBackgroundDoubleClick(graphPos)` or, when unset, runs
`fitGraphViewport(state, nodes, surfaceSize)` with bounds expanded 160x120,
zoom clamped 0.45..1.25, and centred; call the same helper directly for a
"fit" action. `onContextMenu(hit, graphPos, surfacePos)` carries the pointer
in surface-local coordinates so a consumer popup opens exactly under the
cursor; the bound `PopupMenuState` is anchored there automatically. Use
`GraphSurfaceState`, `GraphNode`, `GraphNodePart`, `GraphPort`, `GraphCable`,
and optional `GraphSurfaceStyle` as the structured
model. Cables are draw-only; module boxes are first-class graph items with stable child parts for readouts,
meters, bypass/options/delete controls, status chips, and ports. Those parts
have stable IDs, hit regions, and virtual semantic children even though the
first renderer draws them in one canvas pass. See
`docs/graph-surface-ui-brief.md`.
Cable creation is modeled as a retained port-drag preview: `canConnect`
validates the current target, `onCablePreview` observes the preview position,
and `onConnect` is called only for a validated drop. Keyboard arrows move the
surface's virtual graph focus across modules, ports, actionable parts, and
cables; Enter/Space activates the focused graph item.

Envelope and curve drawing is shared in `paint::drawEnvelope(...)`.
The immediate and retained envelope editors use the same curve, point, segment,
and focus treatment. `dispatchImGuiInput(...)` populates retained pointer
events with tree-local pointer delta, right/middle buttons, wheel delta, click
count, context-menu intent, and modifier keys for Canvas-backed controls that
need richer gestures.

Minimal retained panel:

```cpp
namespace r = snd::ui::retained;
namespace w = snd::ui::retained::widgets;

struct PanelModel {
    double bypass = 0.0;
    double gain = 0.65;
    double fader = 0.75;
    double level = 0.35;
    double ready = 1.0;
};

r::ValueBinding bind(double& v, double min = 0.0, double max = 1.0)
{
    double* target = &v;
    r::ValueBinding b;
    b.get = [target] { return *target; };
    b.set = [target](double next) { *target = next; };
    b.min = min;
    b.max = max;
    b.step = 0.01;
    return b;
}

r::PaintRenderer renderer;
PanelModel model;
auto root = w::column("panel", 8.0f, r::Insets::all(8.0f));
auto top = w::row("panel.top", 8.0f);
top->addChild(w::button("transport.play", "Play", [](r::Node&) {}, &renderer));
top->addChild(w::outlineButton("transport.arm", "Arm", [](r::Node&) {}, &renderer,
                               {64.0f, 28.0f}));
top->addChild(w::toggle("effect.bypass", "Bypass", bind(model.bypass), &renderer));
top->addChild(w::knob("gain", "Gain", bind(model.gain), &renderer));
top->addChild(w::fader("output.fader", "Output", bind(model.fader), &renderer));
top->addChild(w::meter("output.meter", "Output level", bind(model.level), &renderer));
top->addChild(w::led("device.ready", "Device ready", bind(model.ready), false, &renderer));
root->addChild(std::move(top));
root->addChild(w::canvas("scope", "Waveform", {320.0f, 64.0f},
    [](ImDrawList& dl, const r::Node&, r::Rect bounds,
       const snd::ui::paint::ControlState&) {
        dl.AddLine({bounds.x, bounds.y + bounds.h * 0.5f},
                   {bounds.x + bounds.w, bounds.y + bounds.h * 0.5f},
                   snd::ui::palette().accent, 1.5f);
    },
    &renderer, true));

r::Tree tree(std::move(root));
// Each UI frame:
r::drawImGui(tree, renderer, {360.0f, 160.0f});
```

Use `VisualStyle::CanvasSurfaceDraw` or the `widgets::canvas(...)` overload
taking `draw::Surface&` for new retained custom regions that should render
without ImGui. `PaintRenderer::render(tree, surface, frameContext)` is the
headless/non-ImGui render path; callers provide font refs, font size, time,
and pointer through `draw::FrameContext`. The ImGui retained adapter and
`GlWindow` both capture the same frame context once per render pass. If a
caller omits `FrameContext::fontSizePx`, retained rendering uses SND's 13 px
reference size so headless/recording output still includes text. Old
ImDrawList canvas callbacks stay valid on the ImGui backend.

## Widget reference

Values are **normalized 0..1** unless noted. Returns are **`true` on change /
click**. Sizes are `ImVec2`; a size of `0` means a sensible default. State
structs are owned by you and persist across frames (make them `static` or
members).

### Chrome

- `gradientPanel(size, tl, tr, br, bl)` — filled rect with a 4-corner gradient.
- `gradientButton(label, size, top, bottom)` → clicked.
- `animatedButton(label, size, top=0, bottom=0)` → clicked. Gradient action
  button with a subtle live sweep; `0` colours use the current palette accent.
- `button(label, size, painter)` → clicked. Generic custom-painted button;
  SND still owns activation and focus.
- `outlineButton(label, size)` → clicked. Border-first text action; the
  default style has no fill, shows the accent border on hover, and fills
  immediately while pressed. The overload accepts `paint::OutlineButtonStyle`
  and a selected flag.
- `segmented(id, labels, count, int* selected, size={})` → changed. Pill group
  of mutually exclusive options (mono/stereo, filter slope, A/B). Click a
  segment or use Left/Right when focused; `size=0` gives equal-width segments
  sized to the widest label.
- `cycleButton(id, labels, count, int* index, size={})` → changed. Multi-state
  value button: click cycles the options in place, right-click steps back;
  pips mark the position. Sized to the widest option so it stays stable.
- `iconButton(id, Icon, size, accent, active=false)` → clicked. Vector
  transport/tool icons (`Play, Stop, Record, SkipToStart, SkipToEnd, Loop,
  Waveform, Spectrum, Follow`), crisp at any size. `active` draws lit.
- `iconButton(id, glyph, size={}, font=nullptr, toggled=false, face=0)` →
  clicked. Tactile hardware key for a Material/Lucide glyph from
  `<snd/icons.h>`; raised light-grey face that recesses to a dark inset on
  press. `toggled` holds the inset look; `font=nullptr` = the default
  (Material) font; `face` themes the key colour.
- `badge(text, fill=0)` — small tag ("VST3", "48k"); `fill=0` = translucent
  accent.
- `sectionHeader(text)` — dim uppercase caption with a rule to the right.
- `tooltip(text, maxWidth=400)` — JUCE-style tooltip for the previous item:
  standard hover delay, wraps at `maxWidth` with auto height, palette-drawn.
  The SND replacement for calling `ImGui::SetTooltip` directly.

### Menus

- `openPopupMenu(id)` + `popupMenu(id, items, options={})` -> `MenuResult`.
  Simple popup action list with separators, disabled rows, checked rows,
  icon+label rows, nested `MenuItem::children`, typeahead, Up/Down,
  Enter/Space, Escape, and click-outside close.
- `dropdownMenu(id, currentLabel, items, &selected, size={}, options={})` ->
  `MenuResult`. Combo-box face plus popup list; updates `selected` when
  activated.
- `contextMenu(id, items, options={})` -> `MenuResult`. Right-click menu for the
  previously submitted item; opens at the pointer and returns the selected row.
  `MenuItem::rightText` draws shortcut/value text on the right; `danger` adds a
  destructive-action cue. Retained menus expose `Role::Menu`,
  `Role::MenuItem`, and `Role::ComboBox` semantics.

### Continuous controls

All knob and fader drags support the shared fine-adjust modifier: holding
Shift drags 10× slower.

- `knob(label, float* v, size=0, format="%.2f")` → changing. Rotary 0..1;
  label drawn under it.
- `knob(label, float* v, min, max, painter, size=0, format="%.2f")` →
  changing. Custom-painted knob body with SND-owned drag/focus/value handling.
- `knob(label, float* v, min, max, style|painter, KnobMod, ...)` → changing.
  Same knobs with the modulation-ring overlay (depth arc + live position dot).
- `knobDb(label, float* db, minDb, maxDb, size=0)` → changing. Rotary over a dB
  range; a tick marks 0 dB when in range.
- `fader(id, float* v, size)` → dragging. Vertical fader 0..1; Shift switches
  from jump-to-position to relative fine drag.
- `dragNumber(label, float* v, speed, min, max, format="%.2f")` → changing.
  Horizontal-drag number field using the shared value-row paint.

### Switches and indicators

- `toggle(label, bool* on)` → toggled. Animated on/off switch.
- `led(id, bool on, radius=5, clickable=false, onColor=0)` → clicked (only when
  `clickable`). Glows when on; `onColor=0` uses the accent.
- `ledButton(id, glyph, bool* on, blink=false, size={}, font=nullptr,
  ledColor=0, face=0)` → clicked. Tactile key with an integrated status LED
  ring (record-arm, monitor, transport latches). Latched buttons hold the
  inset face with the ring lit; `blink` pulses the lit ring for armed/pending
  states.

### Meters

- `meter(id, MeterState& st, float level, size, floorDb=-48)`. `level` is
  linear amplitude 0..1 for this frame; `st` (a caller-owned `MeterState`)
  carries decay + peak-hold between frames. dB scale `floorDb..0`; horizontal
  when `size.x > size.y`. Feed `level` from an atomic the audio thread writes.

### Musical widgets

- `keyboard(id, KeyboardState& st, size, firstNote, octaves, noteOn, noteOff,
  lit=nullptr)` → interacted. Click plays (velocity from how far down the key
  the hit lands), drag glissandos. `noteOn(note,vel)` / `noteOff(note)` fire on
  transitions. Pass a `const bool lit[128]` to light externally-held notes
  (e.g. from incoming MIDI).
- `patternGrid(id, bool* cells, rows, steps, size, playheadStep=-1)` → any cell
  changed. `cells` is a row-major `rows*steps` bool array. Drag paints,
  alt-drag erases; `playheadStep` highlights a column. The overload taking a
  `paint::PatternCellPainter` custom-draws each cell body (SND keeps
  backdrop, playhead, border, focus, and the drag interaction).
- `envelopeEditor(id, std::vector<EnvPoint>& pts, size, tensions=nullptr)` →
  editing. Breakpoints over 0..1 in both axes; drag to move, double-click empty
  space to add, right-click a point to delete. Endpoints stay pinned to x=0/1.
  Optional `std::vector<float>* tensions` (same length as points): per-segment
  bend -1..1; drag a segment's middle to curve it.
- `xyPad(id, float* x, float* y, size)` → dragging. 2D pad over two 0..1
  values. The overload taking a `paint::XYPadPainter` custom-draws the body;
  SND keeps the drag and focus ring.

### Graphs

- `widgets::graphSurface(id, name, state, nodes, cables, callbacks, &renderer,
  size, &menuState, style={})` — retained graph surface with pan/zoom, grid,
  typed hit-testing, cable drawing, module chrome, module child-part regions,
  marquee, cable preview/drop callbacks, arrow-key traversal, anchored
  context-menu support, and virtual semantic children for modules, ports, child
  parts, and cables. `GraphSurfaceStyle` skins backdrop, module chrome,
  square/round pins, and wire droop/thickness. `GraphSurfaceState`, nodes,
  cables, and graph mutations stay caller-owned.

### Lists and files

- `selectableList(id, const std::vector<std::string>& items, int* selected,
  size)` → selection changed. Themed scrolling list.
- `fileBrowser(id, FileBrowserState& st, size, std::string* outPath,
  extensions=nullptr)` → a file was picked (path written to `outPath`).
  `FileBrowserState{dir, selected}`: `dir=""` starts in HOME. `extensions` is a
  comma list ("wav,flac"); `nullptr` shows everything.

## Idioms

- **State is yours.** Keep widget values in your app's state (members, or
  `static` for throwaway demos). The widget only reads/writes what you pass.
- **Audio → UI is one-way through atomics.** The audio thread never touches
  ImGui. Publish meter levels / playheads / live values into `std::atomic`
  fields on the audio thread; read them (relaxed) in the frame loop and pass to
  `meter`, playhead lines, etc.
- **UI → audio is a value the audio thread reads.** A knob writes a normalized
  float; the audio thread reads it (an atomic, or a plugin param). Never call
  back into audio from a widget.
- **Stock ImGui is fair game.** Use `ImGui::Button`, `Combo`, `SliderFloat`,
  `BeginChild`, tables, etc. for everything the audio widgets don't cover;
  `snd::ui` widgets compose with them freely on the same frame.
- **Custom drawing.** For anything bespoke, `ImGui::GetWindowDrawList()` plus
  `InvisibleButton` for hit-testing is the pattern the widgets use internally.

## Plugin editors

A plugin built with the client SDK draws its editor in `Processor::drawUi()`
using these same `snd::ui::` / `ImGui::` calls. The wrapper owns the window, GL
context, and input routing; `drawUi` only declares the widgets, exactly as a
panel between `Begin`/`End` does here.
