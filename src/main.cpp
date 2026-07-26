// pantheon — a verification gate for Claude Code, and nothing else.
//
// Wired to the Stop hook. It reads the transcript of the turn that is trying to finish and
// refuses the stop when the evidence contradicts "done": a check that failed, a stub that
// was introduced, or a substantial code change with no verification behind it at all.
//
// Everything is fail-open. This runs on every attempt to end a turn, and a hook that dies
// must never take the session with it — any error exits 0 in silence.
//
//   (no args)          hook mode: event JSON on stdin, decision JSON on stdout
//   --scan <path>      print the turn digest for a transcript (the bench and diff harness)
//   --selftest         run the assertions (built only with -DPANTHEON_SELFTEST)
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "gate.hpp"
#include "json.hpp"
#include "scan.hpp"
#include "state.hpp"

namespace {

std::string esc(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (const char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned char>(c));
                    o += buf;
                } else {
                    o.push_back(c);
                }
        }
    }
    return o;
}

pan::json::Ptr json_parse_or_null(const std::string& raw) {
    if (raw.find_first_not_of(" \t\r\n") == std::string::npos) return nullptr;
    pan::json::Ptr doc = pan::json::parse(raw);
    if (!doc || doc->kind != pan::json::Value::Kind::Object) return nullptr;
    return doc;
}

std::string str_at(const pan::json::Value* doc, const char* key) {
    if (!doc) return "";
    const pan::json::Value* v = doc->get(key);
    return (v && v->kind == pan::json::Value::Kind::String) ? v->str : std::string();
}

// The digest, as JSON. Field names match the Python dict so the two can be diffed directly.
int print_scan(const std::string& path) {
    const pan::Turn t = pan::scan_turn(path);
    std::string out = "{\"last_user\":\"" + esc(t.last_user) + "\",\"edits\":[";
    for (std::size_t i = 0; i < t.edits.size(); ++i) {
        const auto& e = t.edits[i];
        if (i) out += ",";
        out += "{\"file\":\"" + esc(e.file) + "\",\"added\":" + std::to_string(e.added) +
               ",\"removed\":" + std::to_string(e.removed) + "}";
    }
    out += "],\"tests\":[";
    for (std::size_t i = 0; i < t.tests.size(); ++i) {
        if (i) out += ",";
        out += "{\"command\":\"" + esc(t.tests[i].command) +
               "\",\"failed\":" + (t.tests[i].failed ? "true" : "false") + "}";
    }
    out += "],\"verified\":" + std::string(t.verified ? "true" : "false") + ",\"skills\":[";
    for (std::size_t i = 0; i < t.skills.size(); ++i) {
        if (i) out += ",";
        out += "\"" + esc(t.skills[i]) + "\"";
    }
    out += "],\"out_tokens\":" + std::to_string(t.out_tokens) + ",\"problems\":[";
    const auto problems = pan::gate_check(t);
    for (std::size_t i = 0; i < problems.size(); ++i) {
        if (i) out += ",";
        out += "\"" + esc(problems[i]) + "\"";
    }
    out += "]}";
    std::cout << out << "\n";
    return 0;
}

// Replays the fixture corpus through the real pipeline. Filenames encode the expectation:
// t<N>_* plants one fake-"done" the gate must catch, c<N>_* is a legitimate turn it must
// leave alone. Deterministic, no API calls — this is the only claim about the gate that is
// worth making, so it runs in CI.
int bench_mode(const std::string& dir) {
    std::vector<std::filesystem::path> fixtures;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".jsonl") fixtures.push_back(entry.path());
    }
    if (ec || fixtures.empty()) {
        std::fprintf(stderr, "no fixtures in %s\n", dir.c_str());
        return 2;
    }
    std::sort(fixtures.begin(), fixtures.end());

    int traps = 0, traps_caught = 0, clean = 0, false_positives = 0, failures = 0;
    for (const auto& path : fixtures) {
        const std::string name = path.stem().string();
        const bool expect_trap = !name.empty() && name[0] == 't';
        const auto problems = pan::gate_check(pan::scan_turn(path.string()));
        const bool caught = !problems.empty();
        const bool passed = expect_trap ? caught : !caught;

        if (expect_trap) {
            ++traps;
            traps_caught += caught ? 1 : 0;
        } else {
            ++clean;
            false_positives += caught ? 1 : 0;
        }
        failures += passed ? 0 : 1;
        std::string detail = problems.empty() ? "(no problems)" : problems[0];
        if (detail.size() > 42) detail = detail.substr(0, 39) + "...";
        std::printf("%-34s %-6s %-44s %s\n", name.c_str(), expect_trap ? "trap" : "clean",
                    detail.c_str(), passed ? "PASS" : "FAIL");
    }
    std::printf("\ngate caught %d/%d planted fake-dones, %d/%d false positives on clean turns\n",
                traps_caught, traps, false_positives, clean);
    return failures == 0 ? 0 : 1;
}

int hook_mode() {
    std::ostringstream buf;
    buf << std::cin.rdbuf();
    const std::string raw = buf.str();
    const pan::json::Ptr payload = json_parse_or_null(raw);
    if (!payload) return 0;

    const std::string cwd = str_at(payload.get(), "cwd");
    const std::string session = str_at(payload.get(), "session_id");
    const std::string transcript = str_at(payload.get(), "transcript_path");
    const std::string prompt_id = str_at(payload.get(), "prompt_id");
    const pan::json::Value* sa = payload->get("stop_hook_active");
    const bool stop_active = sa && sa->truthy();

    const pan::Turn turn = pan::scan_turn(transcript);
    const pan::Verdict v =
        pan::run_gate(turn, pan::load_mode(cwd), session, prompt_id, stop_active);

    if (v.kind == pan::Verdict::Kind::Block) {
        std::cout << R"({"decision":"block","reason":")" << esc(v.text) << "\"}\n";
    } else if (v.kind == pan::Verdict::Kind::Notice) {
        std::cout << R"({"systemMessage":")" << esc(v.text) << "\"}\n";
    }
    return 0;
}

}  // namespace

#ifdef PANTHEON_SELFTEST
#include "selftest.inc"
#endif

int main(int argc, char** argv) {
    const std::string_view arg = argc > 1 ? std::string_view(argv[1]) : std::string_view();
#ifdef PANTHEON_SELFTEST
    if (arg == "--selftest") return selftest();
#endif
    if (arg == "--scan") {
        if (argc < 3) {
            std::fprintf(stderr, "usage: pantheon --scan <transcript.jsonl>\n");
            return 2;
        }
        return print_scan(argv[2]);
    }
    if (arg == "--bench") {
        return bench_mode(argc > 2 ? argv[2] : "bench/fixtures");
    }
    try {
        return hook_mode();
    } catch (...) {
        return 0;  // a hook must never break the session
    }
}
