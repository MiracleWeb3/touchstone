// One knob. The Python carried presets, per-skill opt-outs, custom routes, budgets and team
// packs; all of it fed features that no longer exist. What survives is the only setting the
// gate ever read.
#pragma once

#include <string>

namespace stone {

enum class Mode { Block, Warn, Off };

// Project config (<cwd>/.touchstone/config.json) wins over global
// (~/.claude/touchstone/config.json). Missing or malformed files mean the default: block.
// Accepts {"gate": "block"|"warn"|"off"} and the legacy {"preset": "full"|"economy"|"quiet"}.
Mode load_mode(const std::string& cwd);

Mode mode_from_name(const std::string& name);

}  // namespace stone
