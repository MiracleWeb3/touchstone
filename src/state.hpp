// The block budget — the valve that stops the gate becoming an infinite loop.
//
// A Stop hook that blocks re-runs the Stop hook. Without a persisted counter the gate would
// refuse the same turn forever, so every block is spent from a budget and the gate always
// yields once it is empty. If the counter cannot be written, the caller must fail OPEN: an
// unwritable state dir must never wedge a session.
#pragma once

#include <string>

namespace pan {

// Identity of the turn a counter belongs to. Claude Code stamps every user prompt with a
// unique prompt_id — use it. Hashing the prompt TEXT silently collided: type "continue"
// twice inside the expiry window and the second turn inherited the first turn's exhausted
// counter, so the gate yielded without ever firing. Text is the fallback only.
std::string turn_key_for(const std::string& prompt_id, const std::string& last_user);

// Blocks already spent on this turn. Records expire after 2h so a stale counter can never
// mute a later turn.
int blocks_used(const std::string& session, const std::string& turn_key);

// Persists used+1. False means the write failed — the caller must not block.
bool record_block(const std::string& session, const std::string& turn_key, int used);

// Where the counters live. Overridable so the selftest never touches real state.
void set_gate_dir(std::string dir);
std::string gate_dir();

}  // namespace pan
