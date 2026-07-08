# UI Programming Guide

How to build a user interface on `snd::ui`. Complements PROGRAMMING-GUIDE.md
(which covers the whole library); this file is only the UI layer.

## Model

`snd::ui` is **Dear ImGui** (immediate mode) plus a thin native-window shell
and a set of audio-oriented widgets drawn on top via `ImDrawList`. ImGui is
vendored unmodified. You include `snd/ui.h`, open a `Window`, and each frame
call stock `ImGui::` functions and `snd::ui::` widgets to declare the UI; there
is no retained widget tree. State lives in *your* variables, not in the
widgets.

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
- `minimize()` / `toggleMaximize()`
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

## Widget reference

Values are **normalized 0..1** unless noted. Returns are **`true` on change /
click**. Sizes are `ImVec2`; a size of `0` means a sensible default. State
structs are owned by you and persist across frames (make them `static` or
members).

### Chrome

- `gradientPanel(size, tl, tr, br, bl)` — filled rect with a 4-corner gradient.
- `gradientButton(label, size, top, bottom)` → clicked.
- `iconButton(id, Icon, size, accent, active=false)` → clicked. Vector
  transport/tool icons (`Play, Stop, Record, SkipToStart, SkipToEnd, Loop,
  Waveform, Spectrum, Follow`), crisp at any size. `active` draws lit.
- `badge(text, fill=0)` — small tag ("VST3", "48k"); `fill=0` = translucent
  accent.
- `sectionHeader(text)` — dim uppercase caption with a rule to the right.

### Continuous controls

- `knob(label, float* v, size=0, format="%.2f")` → changing. Rotary 0..1;
  label drawn under it.
- `knobDb(label, float* db, minDb, maxDb, size=0)` → changing. Rotary over a dB
  range; a tick marks 0 dB when in range.
- `fader(id, float* v, size)` → dragging. Vertical fader 0..1.
- `dragNumber(label, float* v, speed, min, max, format="%.2f")` → changing.
  Horizontal-drag number field.

### Switches and indicators

- `toggle(label, bool* on)` → toggled. Animated on/off switch.
- `led(id, bool on, radius=5, clickable=false, onColor=0)` → clicked (only when
  `clickable`). Glows when on; `onColor=0` uses the accent.

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
  alt-drag erases; `playheadStep` highlights a column.
- `envelopeEditor(id, std::vector<EnvPoint>& pts, size, tensions=nullptr)` →
  editing. Breakpoints over 0..1 in both axes; drag to move, double-click empty
  space to add, right-click a point to delete. Endpoints stay pinned to x=0/1.
  Optional `std::vector<float>* tensions` (same length as points): per-segment
  bend -1..1; drag a segment's middle to curve it.
- `xyPad(id, float* x, float* y, size)` → dragging. 2D pad over two 0..1 values.

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
