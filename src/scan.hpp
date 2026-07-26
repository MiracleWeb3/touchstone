// The digest of one turn: what was edited, what was run, what passed.
//
// This is what makes the gate real instead of honour-system — it reads what actually
// happened rather than asking the agent whether it verified anything.
#pragma once

#include <string>
#include <vector>

namespace pan {

struct Edit {
    std::string file;
    int added = 0;
    int removed = 0;
    std::string fresh;  // new_string / content
    std::string old;    // old_string; empty for a full-file Write
};

struct Check {
    std::string command;
    bool failed = false;
};

struct Turn {
    std::string last_user;
    std::vector<Edit> edits;
    std::vector<Check> tests;   // final verdict per check command
    bool verified = false;      // some check ran AND passed
    std::vector<std::string> skills;
    long long out_tokens = 0;
};

Turn scan_turn(const std::string& transcript_path);

}  // namespace pan
