# Gooey Renderer Independence: The Draw-Surface Brief

Status: architect-reviewed action plan; implementation HELD until the owner
schedules it against bob2 parity rounds.
Author: Iris, UI Specialist (second thread).
Inspected baseline: working tree, 2026-07-10 (post Round-3 landings).
Reviewers: Calder (Software Architect), Lumen (Project Lead), owner.

## Goal

Gooey (the retained-mode UI) should render, test, and eventually ship with no
required ImGui dependency for SND-owned retained widgets. Dear ImGui remains
the substrate of the immediate-mode layer for the foreseeable future; replacing
that path is not part of this project. ImGui becomes *one backend* of a shared
SND draw surface instead of the only way SND can put pixels on screen.

Calder's standing constraint is preserved, restated one level up: SND keeps
ONE visual vocabulary. Today that vocabulary is "everything draws ImDrawList".
After this work it is "everything draws `snd::ui::draw::Surface`", of which
ImDrawList is the first implementation.

## Current coupling (measured, not estimated)

- `ui_retained.h` / `ui_retained.cpp` (the Gooey core: tree, layout, focus,
  events, semantics, bindings): **zero** ImGui references. Already clean.
- `ui_paint.*`: ~510 references — but shallow ones. All drawing funnels
  through exactly **17 ImDrawList operations** (counted from source):
  AddRectFilled, AddRect, AddRectFilledMultiColor, AddCircleFilled,
  AddCircle, AddLine, AddPolyline, AddTriangleFilled, AddBezierCubic,
  AddConcavePolyFilled, PathArcTo, PathLineTo, PathClear, PathStroke,
  AddText, PushClipRect, PopClipRect. Context reads are tiny: GetFontSize
  (10), GetTime (1), GetStyle (1), and two colour-conversion helpers (pure
  math).
- `ui_retained_widgets.*` (~150 references): the PaintRenderer's render-time
  ImGui reads — GetFont (17), GetIO pointer position (hover granularity),
  GetTime (blink/pulse animation) — plus `valueField`'s inline editor riding
  `ImGui::InputText`, and the two *deliberate* adapters `dispatchImGuiInput`
  and `drawImGui`.
- Types throughout: `ImVec2`, `ImU32`, `ImFont*`, `ImTextureID`.

These counts are an implementation compass, not a frozen contract. Re-run the
count before S1 because UI files are moving quickly.

## Design

### 1. `snd::ui::draw` — types and Surface

```cpp
namespace snd::ui::draw {

using Color = uint32_t;   // exact ImU32/IM_COL32 packed layout -- stage-1
                          // ports are re-plumbs with byte-identical pixels.
struct Vec2 { float x, y; };            // or hoist retained::Vec2 -- naming
struct FontRef { /* opaque handle */ }; // wraps ImFont first; stb atlas later

class Surface {
public:
    // exactly the 17 measured operations, SND-named, e.g.:
    virtual void fillRect(Vec2 mn, Vec2 mx, Color c, float rounding = 0) = 0;
    virtual void strokeRect(...) = 0;
    virtual void fillRectMultiColor(...) = 0;   // 4-corner gradient
    virtual void fillCircle/strokeCircle/line/polyline/fillTriangle(...) = 0;
    virtual void bezierCubic(...) = 0;
    virtual void fillConcavePoly(...) = 0;
    virtual void pathArcTo/pathLineTo/pathClear/pathStroke(...) = 0;
    virtual void text(FontRef, float sizePx, Vec2 pos, Color, const char* begin,
                      const char* end = nullptr) = 0;
    virtual Vec2 measureText(FontRef, float sizePx, const char* b,
                             const char* e = nullptr) = 0;
    virtual void pushClip(Vec2 mn, Vec2 mx, bool intersect) = 0;
    virtual void popClip() = 0;
    // texture quad added when a non-ImGui backend needs SvgTexture/RGBA
    // uploads drawn through the surface (no paint:: op uses one today).
};

// What paint previously read from ImGui context, passed explicitly:
struct FrameContext {
    FontRef font;          // default UI font (Material merged)
    FontRef iconFontLucide;
    float fontSizePx = 0;  // replaces ImGui::GetFontSize()
    double timeSeconds = 0;// replaces ImGui::GetTime() (blink, sweep, pulse)
    Vec2 pointer;          // same coordinate space as Surface draw ops
    bool pointerValid = false;
};

} // namespace snd::ui::draw
```

Virtual-per-op cost is irrelevant at UI draw rates; if profiling ever
disagrees, the interface can become a command-struct without changing
callers.

`Color` is a compatibility type, not a new colour-management policy. A later
colour-space change would be separate visual-design work, not part of renderer
independence.

### 2. Backends

- **ImGuiSurface** (stage 1): wraps `ImDrawList*` + `ImFont*`. Immediate
  widgets construct it over `GetWindowDrawList()`; the retained
  `PaintRenderer` constructs it in `drawImGui`. Pixels byte-identical.
- **Pure Gooey backend** (late stage): an OpenGL surface reusing SND's
  existing GL context handling, with an stb_truetype font atlas over the SAME
  embedded font data (`src/fonts/*.inc` + the base font). Note: ImGui itself
  rasterizes through stb_truetype, so glyph metrics can match closely.
- **Recording/null surface** (cheap, high value): records ops for headless
  paint assertions in `--selftest` — pixel-free visual regression checks
  (op counts, rect coords, colours) with no GL at all.

### 3. Painter-hook compatibility (bob2 must not break)

`KnobPaintArgs` / `ButtonPaintArgs` / `XYPadPaintArgs` / `PatternCellPaintArgs`
currently expose `ImDrawList* drawList`. bob2's murk-parity painters are heavy
ImDrawList users. Contract migration:

- Args gain `draw::Surface* surface` alongside `drawList`.
- Under the ImGui backend `drawList` stays populated — every existing painter
  keeps working unchanged.
- A painter that uses only `surface` is "Gooey-pure"; the pure backend may run
  only those. A drawList-only consumer painter is supported by the ImGui
  backend only; non-ImGui Gooey hosts must either reject that painter clearly,
  fall back to the SND default face, or keep that surface on the ImGui backend.
- `drawList` is deprecated once bob2's painters migrate at their own pace;
  removal is a separate owner decision.

### 4. Input and text editing

- `Event` and `dispatchImGuiInput` already have the right shape: the adapter
  pattern stands. A GLFW-direct adapter is added only with the pure Gooey
  window shell.
- **Gap found while writing this brief**: retained `EventType` has no
  character-input event (`Key` covers Tab/Enter/Space/arrows/Escape only).
  `valueField`'s inline editor is possible today only because it borrows
  `ImGui::InputText`. Stage 3 adds `EventType::TextInput { uint32_t
  codepoint }` plus an SND-owned single-line edit state for `valueField`;
  under ImGui embedding the existing InputText path may remain as the
  adapter's implementation.

### 5. Text stack decision (the one real dependency call)

A pure backend needs font loading + rasterization. Recommendation:
**stb_truetype** — single header, public-domain/MIT dual, consumes the same
embedded TTF data, and is literally what ImGui uses internally, so metrics
stay close. Note: vendoring `stb_truetype.h` alongside `stb_image.h` answers
BOTH this and bob2's open PNG-decode ask in one dependency decision (already
flagged to Calder). No complex shaping/BiDi in scope; that limitation is
documented, matching what ImGui provides today.

## Staged plan (each stage: tree green, pixels identical, bob2 unbroken)

1. **S1 — the seam.** Add `draw::` types + Surface + ImGuiSurface. Port
   `paint::` internals toward `Surface&` (mechanical, ~1,300 lines) while
   preserving existing public ImDrawList helper signatures as forwarding
   wrappers. Painter args carry both `surface` and `drawList`. Immediate
   widgets and PaintRenderer construct ImGuiSurface. Palette stays
   `ImU32`-layout (`draw::Color`).
2. **S2 — context purge.** `FrameContext` threads font/size/time/pointer
   through PaintRenderer::render and paint helpers; delete the render-time
   `ImGui::Get*` reads. `drawImGui` fills FrameContext from ImGui; a pure
   caller fills it from GLFW/clock.
3. **S3 — text input.** `EventType::TextInput`, SND-owned valueField editor.
4. **S4 — pure backend.** GL surface + stb_truetype atlas (dependency
   decision point), Gooey window shell without an ImGui context, GLFW input
   adapter.
5. **S5 — proof.** Retained selftest runs the recording surface headless and
   at least one example runs the pure GL backend end-to-end.

## Guardrails

- One visual vocabulary: all SND drawing goes through `paint::` over
  `Surface`; no second paint library, no per-backend forks of widget looks.
- Immediate mode remains Dear ImGui for this plan. Any future immediate-mode
  renderer change would be a separate owner-approved architecture project.
- `ui_retained.h` / `ui_retained.cpp` stay independent of draw-surface and
  backend headers. The seam belongs in paint, retained widgets, and adapters,
  not in the retained core.
- Existing public `paint::` ImDrawList helpers must not be signature-broken in
  S1. Add `Surface` overloads or private shared implementations first, keep
  compatibility wrappers forwarding to the ImGui surface, and make any public
  API cleanup a separate owner decision.
- The painter-hook contract never breaks mid-parity-round; `drawList`
  removal is a separate approval.
- Realtime rules unchanged: no audio-thread involvement anywhere in this
  work.
- Accessibility semantics are renderer-independent already and must stay so.

## Acceptance criteria

- A retained tree renders through the ImGui backend byte-identical to today.
- The same tree renders through the recording surface with no GL/ImGui in
  the process (selftest-provable).
- bob2 builds unmodified after S1/S2 land.
- No new dependency enters without the explicit stb vendoring approval.

## Non-goals

- Rewriting or wrapping immediate-mode ImGui usage in apps/plugins.
- Replacing GLFW, the GL context strategy, or the window shell for existing
  apps.
- A general scene graph, damage-rect partial redraw, or GPU text shaping.
- Renaming `snd::ui::retained` to Gooey in C++ (separate approved decision,
  per DESIGN.md).

## Open questions for review

1. Naming: `snd::ui::draw` vs `snd::ui::render`; `Surface` vs `Canvas`
   (Canvas collides with the retained node kind — recommend Surface).
2. Hoist one shared `Vec2` (retained already defines one) or keep layer-local
   types with implicit conversion?
3. Does the recording surface land in S1 (testing win sooner, slightly more
   S1 scope) or S5? Recommend S1.
4. stb vendoring: approve `stb_truetype.h` + `stb_image.h` together?

## Sequencing

Implementation is HELD until the owner schedules it. S1 is the large diff and
should land in a quiet window between bob2 parity rounds, coordinated through
Lumen, with bob2 warned before paint-header seam changes even though their
painters keep compiling via the compatibility contract.

— Iris
