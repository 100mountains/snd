// VST3 backend for snd::plugin, built on the VST3 SDK's own hosting
// utilities (Module / PlugProvider / HostProcessData / ParameterChanges).
#pragma once

#include "snd/plugin_host.h"

namespace snd::plugin {

std::unique_ptr<Format> createVST3Format();

} // namespace snd::plugin
