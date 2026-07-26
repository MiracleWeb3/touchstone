#include "gate.hpp"

#include <algorithm>

#include "patterns.hpp"
#include "state.hpp"

namespace stone {
namespace {

constexpr int kMaxBlocksPerTurn = 2;
constexpr int kChurnFloor = 15;
constexpr std::size_t kMaxListed = 3;
constexpr std::size_t kMaxStubsListed = 5;

std::string basename_of(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string join(const std::vector<std::string>& parts, const char* sep, std::size_t limit) {
    std::string out;
    for (std::size_t i = 0; i < parts.size() && i < limit; ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

}  // namespace

std::vector<std::string> introduced_stubs(const std::vector<Edit>& edits) {
    std::vector<std::string> found;
    for (const auto& e : edits) {
        if (!is_code_file(e.file)) continue;
        for (const auto& kind : new_stub_kinds(e.fresh, e.old)) {
            found.push_back(basename_of(e.file) + ": " + kind);
        }
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

std::vector<std::string> gate_check(const Turn& turn) {
    std::vector<std::string> problems;

    std::vector<const Edit*> code_edits;
    for (const auto& e : turn.edits) {
        if (is_code_file(e.file)) code_edits.push_back(&e);
    }
    if (!code_edits.empty()) {
        std::vector<std::string> failing;
        for (const auto& t : turn.tests) {
            if (t.failed) failing.push_back(t.command);
        }
        int added = 0, removed = 0;
        for (const Edit* e : code_edits) {
            added += e->added;
            removed += e->removed;
        }
        // A 40-line deletion is as unverified as a 40-line addition.
        const int churn = std::max(added, removed);
        if (!failing.empty()) {
            problems.push_back("verification is failing (" + join(failing, "; ", kMaxListed) + ")");
        } else if (churn >= kChurnFloor && !turn.verified) {
            problems.push_back(std::to_string(code_edits.size()) + " code file(s) changed (~" +
                               std::to_string(churn) +
                               " lines) but no verification passed (tests/build/lint/selftest)");
        }
    }
    if (const auto stubs = introduced_stubs(turn.edits); !stubs.empty()) {
        problems.push_back("stubs introduced: " + join(stubs, ", ", kMaxStubsListed));
    }
    return problems;
}

Verdict run_gate(const Turn& turn, Mode mode, const std::string& session,
                 const std::string& prompt_id, bool stop_active) {
    if (mode == Mode::Off || (turn.edits.empty() && turn.tests.empty())) return {};
    const auto problems = gate_check(turn);
    if (problems.empty()) return {};
    const std::string summary = join(problems, "; ", problems.size());

    if (mode == Mode::Warn) {
        return {Verdict::Kind::Notice, "⚠ touchstone gate (warn-only): " + summary};
    }

    // Hard evidence (failing checks, stubs) earns two blocks; the softer "no verification
    // ran" heuristic gets one nudge, then yields — a repo with no test harness must not lose
    // two turns to a demand it cannot satisfy.
    const bool hard = std::any_of(problems.begin(), problems.end(), [](const std::string& p) {
        return starts_with(p, "verification is failing") || starts_with(p, "stubs introduced");
    });
    const int limit = hard ? kMaxBlocksPerTurn : 1;

    const std::string turn_key = turn_key_for(prompt_id, turn.last_user);
    int used = blocks_used(session, turn_key);
    // Claude Code sets stop_hook_active once a Stop hook has already blocked this turn. Trust
    // it as a FLOOR, not a verdict: if our own counter was lost (state wiped mid-turn) it
    // still proves one block was spent, so the ladder can never run away — while a live
    // counter keeps the two-block budget others give up.
    if (stop_active) used = std::max(used, 1);

    if (used >= limit) {
        return {Verdict::Kind::Notice, "⚠ touchstone gate yielded after " + std::to_string(used) +
                                           " block(s) — still open: " + summary};
    }
    if (!record_block(session, turn_key, used)) {
        return {Verdict::Kind::Notice,
                "⚠ touchstone gate (fail-open, counter unwritable): " + summary};
    }

    std::string reason =
        "touchstone verification gate — do not finish yet: " + summary +
        ". Before stopping again: make the failing tests pass, remove the introduced stubs "
        "(TODO/FIXME/.skip/.only/NotImplementedError), or run a real verification "
        "(tests / build / lint / selftest) on the code you changed and report the result. "
        "If the user explicitly told you to skip verification, say so in one line and stop.";
    if (used + 1 >= limit) reason += " (Final gate pass — the next stop will be allowed.)";
    return {Verdict::Kind::Block, reason};
}

}  // namespace stone
