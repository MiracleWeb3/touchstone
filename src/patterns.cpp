#include "patterns.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_set>

namespace stone {
namespace {

using std::regex;

// ── the evidence vocabulary ──────────────────────────────────────────────────
//
// Flag-form checks live in their own branch with NO leading \b. `--selftest` used to sit
// inside the \b(...)\b group, where it was DEAD: a word boundary can never hold between a
// space and a '-', so `python3 x.py --selftest` never registered. That blinded the gate on
// every stdlib-only project — both to "verification ran" and to "the check failed".
constexpr const char* kRunners =
    R"RX(pytest|py_compile|unittest|jest|vitest|mocha|go test|cargo (test|check)|)RX"
    R"RX(npm (test|run test\w*)|yarn test|pnpm test|bun test|deno test|)RX"
    R"RX(node --check|tsc|make (test|check)|)RX"
    R"RX(rspec|phpunit|mix test|dotnet test|gradle test|mvn test|)RX"
    R"RX((ba)?sh [\w./-]*(test|check)[\w./-]*\.sh)RX";

// A bare `make` IS the build on a C/C++ project — the same evidence `gcc` is. `make clean`
// and `make install` are housekeeping and prove nothing, so they stay excluded or every
// teardown would read as verification.
constexpr const char* kBuilders =
    R"RX(npm run build|yarn build|pnpm build|cargo build|cargo clippy|go build|go vet|)RX"
    R"RX(ruff|eslint|flake8|pylint|mypy|shellcheck|bash -n|gcc|g\+\+|cc|clang|javac|)RX"
    R"RX(cmake|ninja|swift build|make(?!\s+(clean|install|uninstall|distclean)))RX";

// Patterns that START with a non-word char belong here, not in the group above: a leading
// \b can never hold before '-' or '.', which is what killed --selftest.
constexpr const char* kFlags = R"RX(--selftest|--self-test|\./[\w./-]*(test|check)[\w./-]*\.sh)RX";

const regex& test_re() {
    static const regex r(std::string(R"(\b(?:)") + kRunners + R"()\b|(?:)" + kFlags + R"()\b)");
    return r;
}

const regex& verify_re() {
    static const regex r(std::string(R"(\b(?:)") + kRunners + "|" + kBuilders + R"()\b|(?:)" +
                         kFlags + R"()\b)");
    return r;
}

const regex& fail_re() {
    static const regex r(
        R"(\b[1-9]\d* (failed|errors?)\b|FAILED|Traceback \(most recent|AssertionError|)"
        R"(npm ERR!|error TS\d|Exit code [1-9]|✗|\bFAIL\b)");
    return r;
}

// Command segments that merely mention a test tool without running one.
const regex& not_a_test_re() {
    static const regex r(
        R"(^\s*(which|command -v|type|grep|rg|cat|echo|find|ls|man|head|tail|)"
        R"(pip3? install|pipx|npm i(nstall)?|pnpm (add|i)|yarn add|apt(-get)?|brew)\b)");
    return r;
}

const regex& heredoc_re() {
    static const regex r(R"(<<-?\s*['"]?\w+)");
    return r;
}

const regex& splitter_re() {
    static const regex r(R"(&&|\|\||;|\|)");
    return r;
}

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

bool matches(const regex& r, std::string_view s) {
    const std::string t(s);
    return std::regex_search(t, r);
}

std::size_t count_matches(const regex& r, std::string_view s) {
    const std::string t(s);
    return static_cast<std::size_t>(
        std::distance(std::sregex_iterator(t.begin(), t.end(), r), std::sregex_iterator()));
}

bool runs_matching(std::string_view cmd, const regex& r) {
    for (const auto& seg : cmd_segments(cmd)) {
        if (!std::regex_search(seg, not_a_test_re()) && std::regex_search(seg, r)) return true;
    }
    return false;
}

const std::unordered_set<std::string>& code_ext() {
    static const std::unordered_set<std::string> e{
        "py", "js", "jsx", "ts",  "tsx",  "go",  "rs",   "java", "rb",  "php", "c",
        "h",  "cc", "cpp", "hpp", "cs",   "swift", "kt", "scala", "sh", "bash", "zsh",
        "vue", "svelte", "sql"};
    return e;
}

struct StubRule {
    const regex& (*pattern)();
    const char* kind;
};

const regex& todo_re() {
    static const regex r(R"(\bTODO\b|\bFIXME\b|\bXXX\b)");
    return r;
}
const regex& skipped_re() {
    static const regex r(R"(\.skip\(|\.only\(|\bxit\(|\bxdescribe\(|@pytest\.mark\.skip)");
    return r;
}
// No bare 'placeholder' — it false-positives on the HTML/JSX attribute.
const regex& unimpl_re() {
    static const regex r(R"(NotImplementedError|not implemented)", std::regex::icase);
    return r;
}

}  // namespace

std::vector<std::string> cmd_segments(std::string_view cmd) {
    // A heredoc BODY is data, not commands: `python3 - <<'PY' … --selftest … PY` is a script
    // that MENTIONS a check, not a check being run. Judging the body would count arbitrary
    // inline scripts as verification, and a traceback in their output as a failing check —
    // so cut everything from the heredoc on.
    std::string head(cmd);
    std::smatch m;
    if (std::regex_search(head, m, heredoc_re())) head = head.substr(0, m.position(0));

    std::vector<std::string> out;
    const std::sregex_token_iterator end;
    for (std::sregex_token_iterator it(head.begin(), head.end(), splitter_re(), -1); it != end;
         ++it) {
        std::string seg = trim(it->str());
        if (!seg.empty()) out.push_back(std::move(seg));
    }
    return out;
}

bool runs_test(std::string_view cmd) { return runs_matching(cmd, test_re()); }
bool runs_verify(std::string_view cmd) { return runs_matching(cmd, verify_re()); }

bool looks_failed(std::string_view output) {
    const std::size_t kTail = 4000;
    if (output.size() > kTail) output = output.substr(output.size() - kTail);
    return matches(fail_re(), output);
}

bool is_code_file(std::string_view path) {
    std::string p(path);
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.find("/docs/") != std::string::npos || p.find("/node_modules/") != std::string::npos) {
        return false;
    }
    // Harness scratchpad: throwaway probes and one-shot diagnostics, not the user's codebase.
    // Counting them inflates churn, so a turn that only poked at something trips the gate as
    // if it had shipped code.
    if (p.find("/scratchpad/") != std::string::npos ||
        p.find("/tmp/claude-") != std::string::npos) {
        return false;
    }
    const auto dot = p.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = p.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return code_ext().count(ext) > 0;
}

std::vector<std::string> new_stub_kinds(std::string_view fresh, std::string_view old) {
    // Edits are diffed new-vs-old; full-file Writes have no old baseline, so only the STRONG
    // patterns apply there (a pre-existing TODO in a rewritten file is not "introduced", but
    // a fresh NotImplementedError scaffold is).
    static const StubRule kAll[] = {{&todo_re, "TODO/FIXME"},
                                    {&skipped_re, "skipped/only test"},
                                    {&unimpl_re, "unimplemented stub"}};
    const StubRule* begin = old.empty() ? kAll + 1 : kAll;
    const StubRule* end = kAll + 3;

    std::vector<std::string> kinds;
    for (const StubRule* r = begin; r != end; ++r) {
        const regex& rx = r->pattern();
        if (count_matches(rx, fresh) > count_matches(rx, old)) kinds.emplace_back(r->kind);
    }
    return kinds;
}

}  // namespace stone
