// Finding the current turn inside an append-only transcript.
//
// Claude Code writes one JSONL line per content block and never rewrites the file, so "what
// happened this turn" means: everything after the last genuine user prompt. Getting that
// boundary wrong is the difference between judging this turn's work and judging the whole
// session's.
#pragma once

#include <string>
#include <vector>

#include "json.hpp"

namespace stone {

// A genuine user prompt: user-typed text, not a tool_result carrier, and not the machine
// entries Claude Code writes as role "user" (compact summaries, local-command echoes,
// system reminders). Treating those as prompts truncates the turn scan to nothing.
bool is_real_user(const json::Value& obj);

// Concatenated text of a user entry, whether the content is a bare string or blocks.
std::string user_text(const json::Value& obj);

// Entries from the last real user prompt to EOF. Tail-reads 4 MB, then 32 MB, then the whole
// file, stopping as soon as the boundary is inside the window — turns are almost always
// small, so the common case reads one small tail.
std::vector<json::Ptr> turn_entries(const std::string& path);

}  // namespace stone
