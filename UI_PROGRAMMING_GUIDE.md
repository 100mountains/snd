# UI Programming Guide

How to build a user interface on `snd::ui`. Complements PROGRAMMING-GUIDE.md
(which covers the whole library); this file is only the UI layer.

> **Writing for this guide:** before you add a line, ask *"does the new
> programmer coming to this actually want to know this?"* Document what a
> function does and how to use it. Skip the rationale for design decisions, the
> history of how we got here, and justifications for one approach over another —
> that belongs in commit messages, not here.

## Model

`snd::ui` now has two front ends over one shared paint/style vocabulary.

- **aGooey** is the preferred retained UI path for new SND screens. Its C++
  API is still `snd::ui::retained` through `snd/ui_retained.h`,
  `snd/ui_retained_widgets.h`, and the pure retained GL window. Do not rename
  public symbols for the user-facing name without a separate approved API
  decision.
- **Dear ImGui compatibility** remains available for immediate-mode tools,
  plugin editors, and existing hosts. ImGui is vendored unmodified; the
  compatibility widgets still compose with stock `ImGui::` calls.

Both paths use caller-owned state. SND owns widget drawing, input conventions,
focus treatment, semantics, and shared audio-oriented controls; your app owns
plugin parameters, project state, undo, persistence, and audio-thread handoff.

aGooey consequences:

- Build a stable retained tree with caller-chosen node IDs. Rebuild layout as
  needed, but keep the model and values outside the tree unless a widget state
  struct explicitly says otherwise.
- Use `ValueBinding`, callbacks, or small adapters so widgets can read/write
  caller-owned values without owning plugin/DSP state.
- Expose semantics by default: accessible names, roles, focusability, checked
  and disabled state, value text/ranges, and actions where relevant.
- Draw custom/live regions with retained Canvas helpers and
  `draw::Surface` painters when they should run without an ImGui context.

Immediate compatibility consequences:

- A widget both draws and reports interaction in the same call. `knob(...)`
  returns `true` on the frame its value changed; `toggle(...)` returns `true`
  the frame it flips.
- You pass the value in and the widget mutates it in place (`float* value`),
  or you read the bool return. There is no callback registration and no
  "get widget by id" — you already hold the state.
- Every widget takes a string `id`/`label`. Identical visible labels need
  distinct ids: use the `"Label##uniqueid"` suffix or `ImGui::PushID`.

Primitive parity contract:

- A reusable SND primitive needs an immediate API, a retained API, and a shared
  `paint::drawX` path unless it is explicitly documented as mode-specific.
- Input behaviour must be equivalent in both authoring models: pointer,
  keyboard, modifiers, focus-visible treatment, disabled state, and value
  change rules should match.
- Retained widgets must expose accessibility/semantics: role, name, state,
  value range/text, focusability, and semantic actions where relevant.
- Docs, examples, and tests should cover both front ends when a primitive has
  parity.
- Any change to one side of a paired primitive needs a parity review before it
  lands.

## The aGooey window and frame loop

For a new retained/aGooey app, include the retained headers and use
`snd::ui::retained::GlWindow`. Link `snd_ui_retained_gl`; this path has no
ImGui context or immediate-mode compatibility target.

```cpp
#include "snd/ui_retained.h"
#include "snd/ui_retained_gl.h"
#include "snd/ui_retained_widgets.h"

namespace r = snd::ui::retained;

r::Tree tree(buildUiTree());
r::PaintRenderer renderer;

r::GlWindow window;
if (!window.create(800, 600, "My App"))
    return 1;

while (!window.shouldClose()) {
    if (!window.beginFrame(tree, renderer))
        break;

    window.endFrame();
}
window.destroy();
```

`GlWindow::beginFrame` pumps platform input into retained events, refreshes
caller-owned bound values, lays out the tree to the current window size, and
renders through `PaintRenderer::render(tree, draw::Surface&, FrameContext)`.
`endFrame()` presents the rendered frame.

## Dear ImGui compatibility window

Use the immediate `snd::ui::Window` shell when embedding SND widgets in an
existing ImGui tool or maintaining an immediate-mode editor.

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

`beginFrame`/`endFrame` bracket exactly one ImGui frame. Do all immediate UI
between them. `shouldClose()` reflects the OS close button; override it with
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

Construct more than one retained `GlWindow` or immediate `Window`. Immediate
`Window` instances each get their own ImGui context; GL objects (textures,
fonts) are shared automatically across windows, so an image loaded in one is
usable in another. Give every window its own frame loop, or pump them in
sequence each iteration:

```cpp
for (auto& w : windows) {
    if (!w->beginFrame()) { /* mark for removal */ continue; }
    /* declare that window's UI */
    w->endFrame();
}
```

For immediate windows, `beginFrame` makes that window's ImGui context current
for the duration of the frame, so widget state does not leak between windows.

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

For a look outside the palette's reach (a bespoke skin), prefer a retained
Canvas or custom painter that draws through `snd::ui::draw::Surface`. In ImGui
compatibility code, direct `ImGui::GetWindowDrawList()` drawing remains
available.

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
  `Window::create`, or inside the frame loop). In ImGui code, cast `id` to
  `ImTextureID` before feeding it to `ImGui::Image`. Release with
  `releaseTexture(id)`.

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
thickness, segments)` strokes a two-colour arc sampled per segment. For a
smooth multi-colour stroke use `draw::Surface::polylineGradient(points,
count, colors, thickness, closed)` — ONE continuous strip with a colour per
point (gradient wires, conic rims): unlike per-segment lines there are no
joint overlaps, so translucent strokes don't bead and the anti-alias fringe
runs once along the whole strip — the
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
`snd::ui::draw::Surface` runs on the retained GL renderer without ImGui. The
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

## aGooey retained widgets

Include `snd/ui_retained_widgets.h` to build aGooey panels with the shared SND
look. The public C++ namespace remains `snd::ui::retained`; helper widgets live
in `snd::ui::retained::widgets`; the renderer is
`snd::ui::retained::PaintRenderer`.

Current helpers cover `row`, `column`, `panel`, `gradientPanel`, `label`,
`sectionHeader`, `badge`, `listItem`, `menuItem`, `popupMenu`,
`dropdownMenu`, `contextMenuRegion`, `modalDialog`, `alertDialog`,
`confirmDialog`, `button`, `outlineButton`,
`segmented`, `tabBar`, `cycleButton`, `ledButton`,
`animatedButton`, `iconButton`, `toggle`, `checkbox`, `knob`, `fader`, `meter`, `led`,
`patternGrid`, `xyPad`, `keyboard`, `valueRow`, `dragNumber`, `valueField`,
`envelopeEditor`, `canvas`, `textField`, `scrollView`, `splitter`, and
`graphSurface`. Value controls use `ValueBinding`; the caller still owns
plugin parameters, app state, undo, and audio-thread handoff. Knobs,
faders, drag numbers, XY pads, pattern grids, keyboards, menus, and envelope
editors support retained pointer editing through the ImGui input bridge or the
pure retained GL window.

Set `ValueBinding::beginEdit` and `endEdit` when the owner needs one transaction
per continuous gesture. Knobs call them on pointer down/up; toggles and cycle
buttons bracket their atomic change. The callbacks mark transaction boundaries
only and do not move undo ownership into SND.

`GraphSurfaceCallbacks::onDragFinished` marks the end of a module drag so an
owner can coalesce layout movement into one command; `onDrag` remains the live
delta stream.
Meters and LEDs can also use `ValueBinding` so a stable retained tree can
display changing audio/UI state without recreating nodes. `xyPad` takes
separate X/Y bindings, while `patternGrid` edits a caller-owned row-major bool
array and can read a dynamic playhead callback.

When embedding a retained tree inside an ImGui layout, use:

```cpp
snd::ui::retained::drawImGui(tree, renderer, {360.0f, 180.0f});
```

That refreshes `ValueBinding` values with `tree.refreshBoundValues()`, lays out
the tree, reserves the ImGui item rectangle, dispatches current mouse/keyboard
input, and renders through the shared paint layer. If you wire the pieces
manually, call `tree.refreshBoundValues()` before rendering or taking semantic
snapshots whenever caller-owned model values may have changed.

For a pure aGooey retained window with no ImGui context, include
`snd/ui_retained_gl.h` and use `snd::ui::retained::GlWindow`:

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
`widgets::sequencerMatrix(id, name, state, &renderer, size)` for retained
audio-editor matrices. `MatrixGridGeometry` owns the measured column/row/gutter
rects; row policies own continuous bars, gate cells, route cells, and band
spans; overlays add ghost fences, help, playhead, selection, and drag preview
layers. Use
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
Use `widgets::tabBar(id, name, labels, binding, &renderer, size)` for a
page/view tab strip. It uses the same caller-owned selected-index binding as
`segmented`, paints through shared `paint::drawTabBar`, and exposes each tab
as a virtual semantic child with selected state.
Use `widgets::cycleButton(id, name, labels, binding, &renderer, size)` for a
multi-state value button: Enter/Space/click advance through the options with
wrap-around, arrows step without wrapping, and the binding is the option
index as in `segmented`.
Use `widgets::ledButton(id, name, glyph, binding, blink, &renderer, size,
ledColor, font)` for tactile keys with an integrated status LED ring
(record-arm, monitor, latches). The binding is the on/off state as in
`toggle`; `blink` pulses the lit ring for armed/pending states.
Use `widgets::checkbox(id, name, binding, &renderer, size)` for a square
checked/unchecked control. It shares `Role::Toggle` semantics with `toggle`,
uses caller-owned `ValueBinding`, and exposes checked state plus Activate /
SetValue actions.
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
Small parity wrappers should stay boring and named the same as their immediate
counterpart: `widgets::gradientButton`, `widgets::transportButton`,
`widgets::checkbox`, `widgets::knobDb`, `widgets::selectableList`, and
`widgets::fileBrowser` exist so users do not have to rebuild common controls
from lower-level retained pieces. Use `widgets::attachTooltip(node, text)` for
retained hover help; accessible descriptions remain separate in
`Semantics::description`.

## Semantics, focus, and headless testing

The retained core is the accessibility surface. Every node carries
`Semantics`: a `Role` (`Button`, `Toggle`, `Slider`, `Meter`, `ListItem`,
`Menu`, `Canvas`, ...), an accessible `name`, optional `description` and
`tooltip` help text, a `ValueRange` (numeric value/min/max/step plus display
`text`), state bits, and the `Action`s it supports (`Activate`, `Increment`,
`Decrement`, `SetValue`, `OpenMenu`, `Focus`). The built-in widget helpers fill
all of this in; custom nodes must do the same — `validate()` returns
`ValidationIssue`s for empty IDs,
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

**Outline icon button.** `widgets::outlineIconButton(id, name, glyph, ...,
size, style, selected, font, actOnPress)` puts an icon-font glyph (70% of the
button height, centred, never moves) in the outline-button chrome —
border-first, `activeFill` on press, `selectedFill` while latched. Use it
beside `outlineButton` text buttons with the same `OutlineButtonStyle` so a
transport row reads as one family. The tactile `iconButton` remains as the
raised-key alternative; its face is palette-derived and its glyph is pinned.

**Transport button.** `widgets::transportButton(id, name, Icon, onActivate,
&renderer, size, style, selected, actOnPress)` is the building block of a
transport bar: it takes a transport *semantic* (`Icon::Record`, `Play`, `Stop`,
`Loop`, `SkipToStart`, `SkipToEnd`) and renders that meaning as the matching
Lucide house glyph in the same outline chrome as `outlineIconButton` —
`selected` shows the engaged fill (armed/playing/cycling), `actOnPress` fires on
mouse down for no-latency transport feel. Pick buttons by intent
(`transportButton("play", Icon::Play, …)`) rather than wiring glyph constants.
For a solid/filled transport glyph instead of the outline, use the vector
`iconButton(id, Icon, …)`.
Immediate `snd::ui::transportButton(id, Icon, size, selected, actOnPress)`
uses the same outline body and glyph painter; the styled overload takes
`paint::OutlineButtonStyle`.

**Fire on press.** Both `outlineButton` and `outlineIconButton` (and thus
`transportButton`) take a trailing `actOnPress` flag. Default `false` = the
conventional fire-on-release (the click only lands if the pointer is still over
the button on mouse up). `true` = fire on mouse down (and Enter/Space) for a
snappy, latency-free press — the transport feel.

**Label nudge.** `OutlineButtonStyle::labelOffsetY` shifts the centred
label/glyph down by N px for optical centring (e.g. a glyph that reads high in
its box). 0 = geometric centre.

**Text field.** `widgets::textField(id, std::string* text, renderer, size,
placeholder, onCommit)` is a single-line retained editor: caret + selection,
printable keys via the `TextInput` event, Left/Right (Shift extends), Home/
End, Backspace/Delete, Enter fires `onCommit`, Esc reverts to the value at
focus. Click to focus and place the caret. No ImGui `InputText` bridge.

**Scroll view.** `widgets::scrollView(id, gap, padding, renderer)` is a
vertical scroll container: add children and they stack as a column at their
natural height, clipped to the view, with a wheel-driven draggable scrollbar
when they overflow (the wheel bubbles, so it scrolls even when the pointer is
over a child). Give the node a fixed `Length` to size the viewport; it
reports `contentHeight()`/`viewHeight()` and clamps `scrollY`.

**Splitter.** `widgets::splitter(id, name, binding, horizontal, invert,
renderer, thickness)` is a draggable pane divider: dragging (or arrow keys
while focused) writes the adjacent pane's size through the `ValueBinding`,
clamped to `[min, max]`; `invert` flips direction for far-edge-docked panes.
The splitter is stateless — rebuild or re-layout off the bound value.

**Window canvas colour.** `Window::setClearColor(IM_COL32(...))` sets the GL
clear behind everything the app draws; apps painting their own chrome should
set their canvas colour so padding/gaps don't show the library default.

## Menus

`dropdownMenu` also takes a `ValueBinding` (instead of `int*`) so the
control live-follows a value that changes underneath it — a real dropdown
replacement for `cycleButton` on longer enum lists.

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
close. Long retained menus and flyout folders use a bounded scrolling panel;
pass the complete item list instead of truncating it. `widgets::dropdownMenu(...)`
adds a retained combo-box face and popup
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

## Modal Dialogs

Use retained modal helpers for blocking decisions, alerts, destructive
confirmation, and short app-state interruptions. `widgets::modalDialog(...)`
builds a top-level overlay with a scrim, centred dialog panel, title, message,
real retained buttons, focus trap, Escape handling, and inert background input.
`widgets::alertDialog(...)` and `widgets::confirmDialog(...)` are convenience
wrappers over the same primitive.

Add the returned node as a top-level child of the retained root, usually last:

```cpp
snd::ui::retained::ModalDialogState state;
state.open = true;

root.addChild(widgets::confirmDialog(
    "delete.confirm", "Delete module?",
    "This removes the module and its cables.", state,
    [&] { deleteSelectedModule(); },
    [&] { keepModule(); },
    &renderer));
```

The caller owns `ModalDialogState::open`. While the modal node is visible,
`Node::setModal(true)` makes the rest of the tree inert: pointer hit tests stay
inside the modal subtree, Tab wraps inside the dialog controls, and semantic
snapshots expose the dialog instead of background controls. Set
`ModalDialogOptions::escapePolicy` to `Ignore`, `Close`, `Cancel`, or
`Primary`; confirm dialogs default Escape to Cancel, alerts default Escape to
the OK/primary action. `closeOnScrimClick` is off by default so destructive or
high-cost decisions cannot disappear from an accidental outside click.

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

Fifteen graph skins ship as presets (`paint::GraphSkin`): five hardware-style
bases (`TechSquare`, `ClassicRounded`, `Blueprint`, `Console`, `Studio`) and
the gradient family, each a full theme — its own slab/header/text tints, pin
triple, wire gradient and weight, rim sweep, and spin character: `Neo`
(Turbo neon), `Rainbow` (six-stop hue wheel), `Ember` (fire), `Redline`
(black slab, red highlight sweep, razor wires), `Glacier` (ice), `Acid`
(toxic green, fat wires), `Vapor` (vaporwave pastels, round pins), `Gold`
(brass on black), `Ghost` (pure greyscale), and `Ultraviolet` (UV to
magenta).
`paint::graphSkinStyle(skin)` returns the full node/pin/wire
`GraphSurfaceStyle` and `paint::graphSkinName(skin)` its menu label; the
backdrop is an independent choice, so pick a `Backdrop` mode and pair it with
`paint::graphBackdropFill(mode)` / `graphBackdropName(mode)`. Building a
skin/background picker is a loop over `paint::kGraphSkinCount` and the modes —
consumers should not re-transcribe colour tables.

The gradient effects are plain `GraphSurfaceStyle` fields any skin can opt
into, all inert at 0: `rimA..rimF` + `rimSpinSeconds` (conic border sweep
through up to six stops, replaces `border`/`selectedBorder`; selection spins
it — the spin reads the `timeSeconds` argument on `drawModuleBox`, which the
graph surface feeds from the render clock) and
`wireGradientStart/wireGradientEnd` (per-segment cable lerp). `glowA/glowB`
(soft halos behind the node body) exist but no preset uses them — the
filled-stack approximation stays until a real blur pass replaces it.
`headerStripe = false` drops the 3px header accent stripe for clean-slab
skins; when on, the stripe starts below the corner arc so it never juts past
rounded corners.

Viewport navigation: plain mouse wheel scrolls the canvas (shift for
horizontal), cmd+wheel zooms at the cursor, cmd/alt+left-drag or middle-drag
pans, and cmd+arrow keys step-scroll while the surface is focused.

Square pins whose node-local rect sits flush against the node's left or right
edge render as sockets: the fill runs to the edge over the node border and the
outline skips that side, so a wire reads as plugging into the box. Pins that
straddle the edge keep the classic full outline.

Hovering a pin shows a connector tooltip with that port's `GraphPort::label`
(same look as `snd::ui::tooltip`, after a short delay) — `graphSurface` owns
the timing and draws it; `GraphSurfaceStyle::portTooltips` (default on) opts
out. `paint::drawTooltip(...)` has `draw::Surface&` and `ImDrawList*` overloads;
the surface overload is the primary reusable helper.

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

`canvas(...)` frames its region with a 1px panel border by default; pass the
trailing `panelBorder=false` for a borderless canvas (a title strip or
wordmark that paints its own chrome). `canvasClip` (in `VisualStyle`) is on by
default — set it false when the draw should be allowed to overhang the node.

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
  Retained: `widgets::gradientButton(id, name, onActivate, renderer, size,
  top, bottom)`.
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
- `tabBar(id, labels, count, int* selected, size={})` → changed. Tab strip for
  switching pages/views. Click a tab, or use Left/Right/Home/End when focused;
  `size=0` gives equal-width tabs sized to the widest label.
- `cycleButton(id, labels, count, int* index, size={})` → changed. Multi-state
  value button: click cycles the options in place, right-click steps back;
  pips mark the position. Sized to the widest option so it stays stable.
- `iconButton(id, Icon, size, accent, active=false)` → clicked. Vector
  transport/tool icons (`Play, Stop, Record, SkipToStart, SkipToEnd, Loop,
  Waveform, Spectrum, Follow`), crisp at any size. `active` draws lit.
- `transportButton(id, Icon, size={36,18}, selected=false, actOnPress=false)`
  → fired. Outline-chrome transport button matching retained
  `widgets::transportButton`; the styled overload takes
  `paint::OutlineButtonStyle`.
- `iconButton(id, glyph, size={}, font=nullptr, toggled=false, face=0)` →
  clicked. Tactile hardware key for a Material/Lucide glyph from
  `<snd/icons.h>`; raised light-grey face that presses into a soft off-white
  inset. `toggled` holds the inset look; `font=nullptr` = the default
  (Material) font; `face` themes the key colour.
- `badge(text, fill=0)` — small tag ("VST3", "48k"); `fill=0` = translucent
  accent.
- `sectionHeader(text)` — dim uppercase caption with a rule to the right.
- `tooltip(text, maxWidth=400)` — JUCE-style tooltip for the previous item:
  standard hover delay, wraps at `maxWidth` with auto height, palette-drawn.
  The SND replacement for calling `ImGui::SetTooltip` directly.
  Retained: `widgets::attachTooltip(node, text)` stores
  `Semantics::tooltip`; retained renderers draw it after the same delay.

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
- `widgets::modalDialog(...)`, `widgets::alertDialog(...)`,
  `widgets::confirmDialog(...)` -> retained overlay nodes. They draw a scrim,
  trap Tab focus, consume background input, expose `Role::Dialog` /
  `Role::Alert`, and close according to `ModalEscapePolicy`.

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
  Retained: `widgets::knobDb(id, name, binding, minDb, maxDb, renderer,
  diameter, painter)` uses the same caller-owned binding model and default dB
  display text.
- `fader(id, float* v, size)` → dragging. Vertical fader 0..1; Shift switches
  from jump-to-position to relative fine drag.
- `dragNumber(label, float* v, speed, min, max, format="%.2f")` → changing.
  Horizontal-drag number field using the shared value-row paint.

### Switches and indicators

- `toggle(label, bool* on)` → toggled. Animated on/off switch.
- `checkbox(label, bool* checked)` → toggled. Square checkbox with optional
  label, using the same shared checked-state paint as retained
  `widgets::checkbox`.
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
- `MatrixGridGeometry` + `widgets::sequencerMatrix(...)` → sequencer/table
  editors. The geometry returns stable cell, row, gutter, and playhead rects
  for both paint and hit-test. `ContinuousBarRow` interpolates drag edits across
  skipped columns; `GateCellRow` and `RouteCellRow` drag-paint from the first
  clicked value; `BandRow` draws span cells. `GhostFenceOverlay`,
  `HelpOverlay`, `PlayheadOverlay`, `SelectionOverlay`, and
  `DragPreviewOverlay` are reusable overlay data, not hidden widget state.
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
  Retained: `widgets::selectableList(id, name, items, selected, renderer,
  size, onSelect)` exposes `Role::ListItem` children, keeps selection in the
  caller-owned `int*`, and refreshes selected state through
  `refreshBoundValues()`. Rebuild the retained subtree when item text/order
  changes.
- `fileBrowser(id, FileBrowserState& st, size, std::string* outPath,
  extensions=nullptr)` → a file was picked (path written to `outPath`).
  `FileBrowserState{dir, selected}`: `dir=""` starts in HOME. `extensions` is a
  comma list ("wav,flac"); `nullptr` shows everything.
  Retained: `widgets::fileBrowser(id, name, state, outPath, renderer, size,
  extensions, onChange)` uses the same `FileBrowserState`, filtering, and caller-owned
  output path. Directory content is enumerated when the subtree is built, so
  rebuild after changing `state.dir` or when the filesystem changes.

### Added primitives

Each control/display below also has a retained `widgets::X` and a shared
`paint::drawX`, per the primitive parity contract.

Continuous controls
- `rangeSlider(id, lo, hi, min, max, size)` — dual-handle min/max slider.

Meters
- `progressBar(id, progress, size)` — fill bar; a negative progress animates an
  indeterminate sweep.

Musical / audio (canvas-backed displays)
- `waveformView(id, samples, count, size, playhead)` — min/max peak envelope.
- `spectrumView(id, mags, bins, size)` — magnitude bars.
- `timelineRuler(id, startBeat, endBeat, beatsPerBar, size, playhead)` — bar/beat ruler.
- `automationLane(id, points, size)` — draggable breakpoint curve (drag to move,
  click to add, double/right-click to remove).

Controls
- `colorPicker(id, h, s, v, size)` — HSV square + hue bar.

Inspector
- `propertyRow(label, size, labelWidth, alt)` — a label + value-column row; group
  rows under `sectionHeader`. Retained: `widgets::propertyRow(id, name, valueNode)`.

Lists
- `table(id, model, size, selectedRow)` — columnar table (sticky header, selection,
  scroll, up/down). `selectableList` covers single-column lists; `fileBrowser`
  lists directories.

Overlays / mode-specific systems (not strict immediate+retained widgets; still
share a `paint::drawX` where there's a visual)
- Toasts — `pushToast` / `toasts` (immediate) + `widgets::toastOverlay` (retained),
  both via `paint::drawToast`.
- `popover(id, topLeft, size, beak…)` + retained `widgets::popover` — floating panel.
- Drag-and-drop — `DragPayload` + `beginDrag`/`dropMatches`/`endDrag` + `dragGhost`
  (retained `widgets::dragGhostOverlay`), `paint::drawDragGhost`.
- `commandPalette(id, items, count, query, size)` + retained
  `widgets::commandPalette` — searchable command list.

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
- **Stock ImGui is fair game in compatibility code.** Use `ImGui::Button`,
  `Combo`, `SliderFloat`, `BeginChild`, tables, etc. for immediate tools that
  are already inside an ImGui frame; `snd::ui` widgets compose with them there.
- **Custom drawing.** In aGooey, prefer Canvas helpers and `draw::Surface`
  painters. In ImGui compatibility code, `ImGui::GetWindowDrawList()` plus
  `InvisibleButton` remains the local pattern for bespoke immediate controls.

## Plugin editors

Current plugin editors built with the client SDK draw in `Processor::drawUi()`
through the Dear ImGui compatibility path. The wrapper owns the window, GL
context, and input routing; `drawUi` only declares widgets, exactly as a panel
between `Begin`/`End` does here. New retained hosts should prefer aGooey
widgets and `draw::Surface` painters when the host exposes that path.
