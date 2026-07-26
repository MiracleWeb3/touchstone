// What counts as a check, a failure, and a stub.
//
// These predicates are the gate's entire notion of evidence, so they are ported from the
// Python character-for-character rather than rewritten into something tidier. Each one has
// a scar behind it — see patterns.cpp — and a "cleaner" equivalent silently reopens a bug
// that took a real incident to find.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace stone {

// Splits a shell command on && || ; | so `pip install x && pytest -q` is judged per segment.
// Everything from a heredoc marker on is dropped: the body is data, not commands.
std::vector<std::string> cmd_segments(std::string_view cmd);

// True when some segment runs a test (TEST_RE) / any check at all (VERIFY_RE). Segments that
// merely mention a tool — `which pytest`, `grep -rn pytest .` — never count.
bool runs_test(std::string_view cmd);
bool runs_verify(std::string_view cmd);

// True when command output looks like a failure. Searched against the last 4000 bytes.
bool looks_failed(std::string_view output);

// Source file the gate cares about: known code extension, not docs, node_modules, or a
// harness scratchpad.
bool is_code_file(std::string_view path);

// Stub markers newly introduced by one edit. `old` empty means a full-file Write, which has
// no baseline — only the unambiguous patterns apply there. Returns kind labels, e.g.
// "TODO/FIXME".
std::vector<std::string> new_stub_kinds(std::string_view fresh, std::string_view old);

}  // namespace stone
