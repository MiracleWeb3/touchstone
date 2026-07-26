// Compiled in only under -DPANTHEON_SELFTEST, so the hook that runs on every Stop carries no
// test code. Every assertion here is ported from the Python it replaces — including the ones
// that exist because of a specific incident. Deleting one because it looks redundant is how
// the bug it caught comes back.
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "entries.hpp"
#include "patterns.hpp"

namespace {

int checks = 0;

void ok(bool cond, const char* what) {
    ++checks;
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

bool any_with(const std::vector<std::string>& v, const char* needle) {
    for (const auto& s : v) {
        if (has(s, needle)) return true;
    }
    return false;
}

std::string repeat_nl(int n) { return std::string(static_cast<std::size_t>(n), '\n'); }

// Unique per PROCESS, not just per call: a name built from the tag alone is the same on the
// next run, so the block counters written by the previous run are still sitting there and
// the ladder assertions inherit an exhausted budget. The suite failed on its second run and
// passed on its first, which is the worst way for a test to be wrong.
std::string temp_dir(const char* tag) {
    static int counter = 0;
    static const auto pid = static_cast<long>(::getpid());
    auto p = std::filesystem::temp_directory_path() /
             ("pantheon-" + std::string(tag) + "-" + std::to_string(pid) + "-" +
              std::to_string(++counter));
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p);
    return p.string();
}

pan::Edit mk_edit(std::string file, int added, int removed, std::string fresh, std::string old) {
    return pan::Edit{std::move(file), added, removed, std::move(fresh), std::move(old)};
}

#include "selftestscan.inc"

int selftest() {
    using pan::Mode;
    using pan::Turn;
    using pan::Verdict;

    // ── gate rules ───────────────────────────────────────────────────────────
    Turn failing_turn;
    failing_turn.last_user = "x";
    failing_turn.edits = {mk_edit("a.py", 3, 0, "x=1", "")};
    failing_turn.tests = {{"pytest", true}};
    failing_turn.verified = true;
    ok(any_with(pan::gate_check(failing_turn), "failing"), "failing tests on a code change block");

    Turn big_unverified;
    big_unverified.edits = {mk_edit("a.py", 40, 0, repeat_nl(39), "")};
    ok(any_with(pan::gate_check(big_unverified), "no verification"), "big unverified change blocks");

    Turn small_unverified;
    small_unverified.edits = {mk_edit("a.py", 3, 0, "x=1", "")};
    ok(pan::gate_check(small_unverified).empty(), "a small change is fine");

    // A large unverified DELETION is churn too — added≈0 must not hide it.
    Turn deletion;
    deletion.edits = {mk_edit("a.py", 1, 40, "x", repeat_nl(39))};
    ok(any_with(pan::gate_check(deletion), "no verification"), "a big deletion is churn too");

    // A stub introduced via an Edit is caught even when everything passed.
    Turn stubbed;
    stubbed.edits = {mk_edit("a.py", 2, 1, "x = 1  # TODO later", "x = 1")};
    stubbed.tests = {{"pytest", false}};
    stubbed.verified = true;
    ok(any_with(pan::gate_check(stubbed), "stubs"), "an introduced stub blocks a passing turn");

    Turn docs_only;
    docs_only.edits = {mk_edit("README.md", 99, 0, "TODO: docs", "")};
    ok(pan::gate_check(docs_only).empty(), "docs-only turns never gate");

    // ── modes ────────────────────────────────────────────────────────────────
    Turn warn_turn = stubbed;
    warn_turn.last_user = "q";
    const Verdict warned = pan::run_gate(warn_turn, Mode::Warn, "s", "", false);
    ok(warned.kind == Verdict::Kind::Notice && has(warned.text, "warn-only"), "warn mode warns");
    ok(pan::run_gate(stubbed, Mode::Off, "s", "", false).kind == Verdict::Kind::Allow,
       "off mode is silent");
    Turn nothing_happened;
    ok(pan::run_gate(nothing_happened, Mode::Block, "s", "", false).kind == Verdict::Kind::Allow,
       "a turn that changed nothing is not gated");

    // ── the block budget ─────────────────────────────────────────────────────
    pan::set_gate_dir(temp_dir("gs"));
    pan::record_block("sessA", "t1", 0);
    pan::record_block("sessB", "t9", 0);
    pan::record_block("sessA", "t1", 1);
    ok(pan::blocks_used("sessA", "t1") == 2, "counters accumulate per session");
    ok(pan::blocks_used("sessB", "t9") == 1, "sessions do not share a counter");
    ok(pan::blocks_used("sessA", "t2") == 0, "a new turn resets the counter");
    ok(pan::blocks_used("sessZ", "t1") == 0, "an unknown session starts clean");

    // An expired record must not mute a later turn.
    {
        const std::string path = pan::gate_dir() + "/" + "x.json";
        std::ofstream(path) << R"({"turn_key":"t1","blocks":2,"ts":1})";
        ok(pan::blocks_used("nobody", "t1") == 0, "a stale counter expires");
    }

    // Soft problems block ONCE then yield; hard evidence blocks twice.
    pan::set_gate_dir(temp_dir("gl"));
    Turn soft = big_unverified;
    soft.last_user = "soft-turn";
    const Verdict s1 = pan::run_gate(soft, Mode::Block, "sSoft", "", false);
    ok(s1.kind == Verdict::Kind::Block && has(s1.text, "Final gate pass"),
       "a soft problem blocks once and says so");
    ok(has(pan::run_gate(soft, Mode::Block, "sSoft", "", false).text, "yielded"),
       "the soft nudge yields on the second pass");

    Turn hard = failing_turn;
    hard.last_user = "hard-turn";
    const Verdict h1 = pan::run_gate(hard, Mode::Block, "sHard", "", false);
    ok(h1.kind == Verdict::Kind::Block && !has(h1.text, "Final gate pass"),
       "hard evidence gets two blocks");
    const Verdict h2 = pan::run_gate(hard, Mode::Block, "sHard", "", false);
    ok(h2.kind == Verdict::Kind::Block && has(h2.text, "Final gate pass"), "the second is final");
    ok(has(pan::run_gate(hard, Mode::Block, "sHard", "", false).text, "yielded"),
       "then it always yields — no infinite loop");

    // REGRESSION: two turns whose prompt TEXT is identical ("continue" typed twice) must each
    // get a full budget. Keying on the text exhausted the counter on turn 1, and the gate
    // silently never fired again.
    pan::set_gate_dir(temp_dir("gk"));
    Turn same = failing_turn;
    same.last_user = "continue";
    ok(pan::run_gate(same, Mode::Block, "sK", "p1", false).kind == Verdict::Kind::Block, "p1 #1");
    ok(pan::run_gate(same, Mode::Block, "sK", "p1", false).kind == Verdict::Kind::Block, "p1 #2");
    ok(has(pan::run_gate(same, Mode::Block, "sK", "p1", false).text, "yielded"), "p1 exhausted");
    ok(pan::run_gate(same, Mode::Block, "sK", "p2", false).kind == Verdict::Kind::Block,
       "a new prompt_id gets its own budget");
    ok(pan::turn_key_for("p1", "continue") != pan::turn_key_for("p2", "continue"),
       "prompt_id decides turn identity");
    ok(pan::turn_key_for("", "continue") == pan::turn_key_for("", "continue"),
       "the text fallback is stable");

    // stop_hook_active is a floor: a wiped counter still cannot run away.
    pan::set_gate_dir(temp_dir("gsa"));
    ok(pan::run_gate(same, Mode::Block, "sA", "p9", true).kind == Verdict::Kind::Block,
       "one block already spent still leaves one");
    pan::set_gate_dir(temp_dir("gsa2"));
    Turn soft2 = big_unverified;
    soft2.last_user = "continue";
    ok(has(pan::run_gate(soft2, Mode::Block, "sB", "p9", true).text, "yielded"),
       "a lost counter plus stop_hook_active still yields");

    // An unwritable counter dir fails OPEN rather than wedging the session.
    {
        const std::string plain = temp_dir("gw") + "/plainfile";
        std::ofstream(plain) << "x";
        pan::set_gate_dir(plain + "/sub");  // cannot be created: parent is a file
        Turn wedge = big_unverified;
        wedge.last_user = "q9";
        ok(has(pan::run_gate(wedge, Mode::Block, "sWedge", "", false).text, "fail-open"),
           "an unwritable counter never blocks");
    }

    ok(pan::mode_from_name("off") == Mode::Off && pan::mode_from_name("warn") == Mode::Warn &&
           pan::mode_from_name("nonsense") == Mode::Block,
       "mode names parse, unknown means block");

    selftest_scan();
    std::printf("ok (%d checks) — gate rules, modes, block ladder, transcript scan, patterns\n",
                checks);
    return 0;
}

}  // namespace
