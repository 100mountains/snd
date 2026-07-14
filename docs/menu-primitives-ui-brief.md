# SND Menu Primitives UI Brief

Status: implemented with native retained menu roles, typeahead, and nested
submenu rows; keep this as the compact design contract.

Owner-facing goal: SND should provide three menu primitives across immediate
and retained UI so downstream apps do not invent local popup styling or
accessibility behaviour.

Technical namespace remains `snd::ui` and `snd::ui::retained`. This brief does
not rename the retained UI layer.

## Primitives

### Simple popup menu

Use for compact action lists: add module, preset actions, duplicate/delete,
routing choices, and similar commands.

Required item vocabulary:

- Action item: label, optional icon, activate callback.
- Separator.
- Disabled item.
- Checked item.
- Optional `rightText` for shortcuts or current value.
- `danger` item with a destructive-action cue that does not rely on colour
  alone.
- Optional `children` for nested submenu rows.

Required behaviour:

- Opens from an explicit button/action.
- Up/Down moves the highlighted item.
- Enter/Space activates the highlighted enabled item.
- Escape and click outside close without activation.
- Hover updates highlight.
- Typeahead moves highlight by visible label prefix.
- Rows with children open/close from click, Enter/Space, Right/Left, and expose
  expanded state.
- Disabled items cannot be highlighted by activation and cannot fire.
- Activating an item closes the menu unless the caller explicitly opts out.

### Dropdown/select menu

Use where the current value must remain visible: scale, time signature, preset,
MIDI channel, module type, routing target.

Required behaviour:

- Closed state is a button face showing the current value.
- Popup list shows the selected item with both checkmark and highlight.
- Keyboard behaviour matches the simple popup menu.
- Selecting an enabled item writes through caller-owned state.
- Typeahead matches visible labels.

### Context menu

Use for right-click/secondary-click on canvases, lists, module cards, pattern
cells, envelope points, and similar targets.

Required behaviour:

- Opens at the pointer position.
- A single physical right-click/context gesture must not generate duplicate
  menu opens or duplicate actions.
- Left-click outside, Escape, or choosing an item closes it.
- Retained widgets should expose a semantic fallback action for keyboard or
  accessibility users when the menu contains target-specific actions.
- Context-menu opening must not trigger normal button activation.

## Shared Visual Contract

Menus should read as compact professional tools, not large modal cards.

- Popup panel: dark frame body, 1 px frame-bright border, 3 px corner radius
  unless the caller chooses square chrome for a specific system surface.
- Item row: stable height, compact horizontal padding, no layout shift between
  normal, checked, icon, disabled, and highlighted states.
- Highlight: accent-tinted row background plus a visible left rail or outline
  cue so hover/focus is not colour-only.
- Checked state: checkmark/icon column plus selected semantics.
- Disabled state: dim text and icon; no hover activation.
- Separator: thin muted rule with enough vertical space to scan groups.
- Icon column: fixed width even when an item has no icon.
- Text: label on the left, optional shortcut/value text right-aligned.
- Focus: visible focus/highlight follows keyboard navigation.

Menu rows should use shared paint helpers rather than raw ImGui styling in each
consumer. Immediate and retained menu rows must share the same menu paint
language.

## Immediate API Direction

The immediate API should keep caller-owned state and explicit activation.

Landed shape:

```cpp
std::vector<snd::ui::MenuItem> items = {
{"synth", "Synth", ICON_MD_GRAPHIC_EQ},
{"drums", "Drums", ICON_MD_GRID_VIEW},
{"sep", {}, {}, true},
{"delete", "Delete", ICON_MD_DELETE, false, true, false, "Del", true},
};

if (snd::ui::outlineButton("Add", {72, 28}))
    snd::ui::openPopupMenu("add.module");

auto picked = snd::ui::popupMenu("add.module", items);
if (picked.activated)
    runMenuAction(picked.id);
```

For simple dropdowns:

```cpp
int selected = currentIndex;
auto picked = snd::ui::dropdownMenu("scale", items[selected].label.c_str(),
                                    items, &selected, {120, 28});
```

Context menus attach to the previous immediate item with `snd::ui::contextMenu(...)`.

## Retained API Direction

Retained menu controls should remain caller-owned for model state.

Landed shape:

```cpp
snd::ui::PopupMenuState menuState;
auto menu = rw::popupMenu("add.menu", &menuState, items, onMenuAction, &renderer);

snd::ui::PopupMenuState selectState;
auto scale = rw::dropdownMenu("scale", "Scale", selectState, items,
                              &selected, onSelect, &renderer);

snd::ui::PopupMenuState contextState;
auto canvas = rw::contextMenuRegion("grid", "Pattern grid", size,
                                    contextState, onOpenContext, &renderer);
```

The retained tree already has right/middle/wheel/context event routing. The
menu implementation keeps popup state caller-owned with `PopupMenuState`, uses
`anchorToPosition` + `position` for anchored context popups, lets retained menus
close on Escape or outside click, maps popup panels to `Role::Menu`, menu rows
to `Role::MenuItem`, dropdown faces to `Role::ComboBox`, and uses
`Action::OpenMenu` as the semantic fallback for context menu regions and
submenu rows. The normal retained ImGui bridge positions anchored popups after
layout and before input/render so hit testing, semantics, and pixels agree.
Nested submenu rows use `MenuItem::children` and caller-owned
`PopupMenuState::openSubmenuPath`.
Long retained menu panels and flyouts are height-bounded and scroll their
complete direct item list.
If the dropdown face needs custom square/hover/selected treatment, pass a
`paint::OutlineButtonStyle` to `rw::dropdownMenu(...)`; consumers should not
reach in and restyle the generated `<id>.button` child by ID.
Dropdown popup children are retained overlay nodes: they remain visible,
hittable, and anchored below the button while not consuming ancestor row/column
layout space. Consumers should not pin the dropdown root to hide menu layout
inflation.

## Accessibility Contract

- Menu button/dropdown face: `Role::ComboBox`, accessible name, current value
  text, `Action::Activate`/`Action::OpenMenu`, expanded state while open.
- Popup panel: `Role::Menu`.
- Menu item: `Role::MenuItem`, accessible name, disabled/checked/selected/
  expanded state, activate action, and `Action::OpenMenu` for submenu rows.
- Context menu target: expose `Action::OpenMenu` or an equivalent fallback for
  keyboard users; do not make right-click the only path to destructive or core
  actions.
- Icons are decorative unless they change meaning; labels remain required.

## Acceptance Checklist

- Immediate simple popup, dropdown, and context menu draw with the shared menu
  style and support mouse, keyboard arrows, Enter/Space, Escape, disabled
  items, checked items, separators, icon+label rows, typeahead, nested rows,
  and outside-click close.
- Retained menu button/select/context flows exercise the same visual row paint.
- Retained context menu opens once per right-click/context event, anchors at the
  requested tree-local pointer position, and does not activate the target's
  normal left-click action.
- Retained popup focus enters the open menu, arrow navigation remains scoped to
  menu rows, and Escape/outside click closes without activation.
- Selftests or headless retained tests cover item model validation, selection
  semantics, disabled item blocking, context gesture de-duplication, and
  caller-owned value updates.
- `UI_PROGRAMMING_GUIDE.md` documents all public helpers.
- `site/index.html` should show menu primitives when the public showcase is next
  refreshed.

## Open Integration Decisions

- Whether repeated use calls for a richer `MenuModel` builder around the current
  `std::vector<MenuItem>` data model.
- Whether nested menus should grow a flyout presentation in addition to the
  current retained inline nested-row presentation.
