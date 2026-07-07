// Editor-side helpers for snd::plugin::client -- ImGui/snd::ui widgets that
// bind directly to plugin parameters through the UiHost (so the host records
// the edits). Include only from plugin UI code.
#pragma once

#include "snd/plugin_client.h"
#include "snd/ui.h"

namespace snd::plugin::client {

// Knob bound to a parameter. Returns true while the value changes.
inline bool uiKnob(UiHost& ui, const char* label, uint32_t id, float size = 0.0f)
{
    float v = (float)ui.get(id);
    if (snd::ui::knob(label, &v, size)) {
        ui.set(id, v);
        return true;
    }
    return false;
}

// Toggle bound to a parameter (0/1).
inline bool uiToggle(UiHost& ui, const char* label, uint32_t id)
{
    bool on = ui.get(id) >= 0.5;
    if (snd::ui::toggle(label, &on)) {
        ui.set(id, on ? 1.0 : 0.0);
        return true;
    }
    return false;
}

// Fader bound to a parameter.
inline bool uiFader(UiHost& ui, const char* id_, uint32_t id, const ImVec2& size)
{
    float v = (float)ui.get(id);
    if (snd::ui::fader(id_, &v, size)) {
        ui.set(id, v);
        return true;
    }
    return false;
}

} // namespace snd::plugin::client
