// The verification gate: refuse to call a turn finished when the evidence says otherwise.
//
// It does not check that the tests are any good — only that verification actually happened
// on code that actually changed. That is a narrow claim, and it is the one thing here that
// has caught a real failure: an agent reporting "verified" over a broken build.
#pragma once

#include <string>
#include <vector>

#include "config.hpp"
#include "scan.hpp"

namespace pan {

struct Verdict {
    enum class Kind { Allow, Notice, Block };
    Kind kind = Kind::Allow;
    std::string text;
};

// Stub markers introduced this turn, as "file.py: TODO/FIXME". Code files only, sorted and
// deduplicated.
std::vector<std::string> introduced_stubs(const std::vector<Edit>& edits);

// The problems that justify refusing "done". Pure decision — no clock, no filesystem.
std::vector<std::string> gate_check(const Turn& turn);

// The full decision including the block budget. Touches the counter files.
Verdict run_gate(const Turn& turn, Mode mode, const std::string& session,
                 const std::string& prompt_id, bool stop_active);

}  // namespace pan
