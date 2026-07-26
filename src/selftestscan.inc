// Transcript-scan and pattern assertions. Split from selftest.inc only to stay inside the
// 300-line rule; it is one suite.

namespace {

std::string write_transcript(const std::string& dir, const char* name,
                             const std::vector<std::string>& rows) {
    const std::string path = dir + "/" + name;
    std::ofstream out(path);
    for (const auto& r : rows) out << r << "\n";
    return path;
}

bool has_stub(const std::vector<stone::Edit>& edits, const char* expect) {
    const auto found = stone::introduced_stubs(edits);
    return found.size() == 1 && found[0] == expect;
}

void selftest_scan() {
    const std::string dir = temp_dir("tr");

    std::vector<std::string> rows = {
        R"J({"type":"user","message":{"content":"old turn, ignore me"}})J",
        R"J({"type":"assistant","message":{"content":[],"usage":{"output_tokens":99}}})J",
        R"J({"type":"user","message":{"content":"fix the parser and run tests"}})J",
        // The same message split across two JSONL lines, usage repeated on both.
        R"J({"type":"assistant","message":{"id":"m1","usage":{"output_tokens":50,)J"
        R"J("input_tokens":100000},"content":[{"type":"text","text":"on it"}]}})J",
        R"J({"type":"assistant","message":{"id":"m1","usage":{"output_tokens":50,)J"
        R"J("input_tokens":100000},"content":[)J"
        R"J({"type":"tool_use","id":"t1","name":"Edit","input":{"file_path":"/x/app.py",)J"
        R"J("old_string":"a=1","new_string":"a=2  # TODO handle zero"}},)J"
        R"J({"type":"tool_use","id":"t2","name":"Bash","input":{"command":"pytest -q tests/"}},)J"
        R"J({"type":"tool_use","id":"t3","name":"Skill","input":{"skill":"superpowers:brainstorming"}}]}})J",
        R"J({"type":"user","message":{"content":[{"type":"tool_result","tool_use_id":"t2",)J"
        R"J("is_error":false,"content":[{"type":"text","text":"2 passed, 1 failed"}]}]}})J",
    };
    const std::string p = write_transcript(dir, "t.jsonl", rows);
    const stone::Turn t = stone::scan_turn(p);
    ok(t.last_user.rfind("fix the parser", 0) == 0, "the scan starts at the last real prompt");
    ok(t.out_tokens == 50, "one usage per message id — the previous turn's 99 is excluded");
    ok(t.edits.size() == 1 && t.edits[0].file == "/x/app.py", "the edit is captured");
    ok(t.edits[0].removed == 1, "old_string counts as one replaced line");
    ok(t.tests.size() == 1 && t.tests[0].command == "pytest -q tests/" && t.tests[0].failed,
       "a failing run is recorded as failing");
    ok(!t.verified, "the only check FAILED — that is not verification");
    ok(t.skills.size() == 1 && t.skills[0] == "superpowers:brainstorming", "skill invocations are captured");
    ok(has_stub(t.edits, "app.py: TODO/FIXME"), "the introduced TODO is found");

    // A re-run that passes overrides the earlier fail.
    rows.push_back(R"J({"type":"assistant","message":{"content":[{"type":"tool_use","id":"t4",)J"
                   R"J("name":"Bash","input":{"command":"pytest -q tests/"}}]}})J");
    rows.push_back(R"J({"type":"user","message":{"content":[{"type":"tool_result",)J"
                   R"J("tool_use_id":"t4","content":"3 passed"}]}})J");
    const stone::Turn t2 = stone::scan_turn(write_transcript(dir, "t2.jsonl", rows));
    ok(t2.tests.size() == 1 && !t2.tests[0].failed, "a passing re-run overrides the fail");
    ok(t2.verified, "and restores verification");

    // A failing BUILD is a failing check, not verification.
    const stone::Turn tb = stone::scan_turn(write_transcript(
        dir, "t3.jsonl",
        {R"J({"type":"user","message":{"content":"wire up the export module"}})J",
         R"J({"type":"assistant","message":{"content":[{"type":"tool_use","id":"b9",)J"
         R"J("name":"Bash","input":{"command":"npm run build"}}]}})J",
         R"J({"type":"user","message":{"content":[{"type":"tool_result","tool_use_id":"b9",)J"
         R"J("is_error":true,"content":"error TS2304: Cannot find name"}]}})J"}));
    ok(!tb.verified && tb.tests.size() == 1 && tb.tests[0].failed, "a failing build is not proof");

    ok(stone::scan_turn("/nonexistent/nope.jsonl").edits.empty(), "a missing transcript is silent");

    // ── code files ───────────────────────────────────────────────────────────
    ok(stone::is_code_file("a/b.py") && !stone::is_code_file("a/b.md") && !stone::is_code_file("x"),
       "code extensions only");
    ok(!stone::is_code_file("/tmp/claude-1000/-home-u/s/scratchpad/probe.py"),
       "harness scratchpad probes are not the codebase");
    ok(!stone::is_code_file("/home/u/x/scratchpad/diag.sh"), "nor is any scratchpad");
    ok(stone::is_code_file("/home/u/dev/tool/src/main.py"), "real source is");
    ok(!stone::looks_failed("0 failed, 12 passed"), "zero failures is not a failure");
    ok(stone::looks_failed("3 failed"), "a real failure count is");

    // ── stub diffing ─────────────────────────────────────────────────────────
    // A full-file Write keeps a pre-existing TODO -> not "introduced"...
    ok(stone::introduced_stubs({mk_edit("a.py", 1, 0, "x=1  # TODO old note", "")}).empty(),
       "a Write with no baseline does not flag a TODO");
    // ...but a fresh NotImplementedError scaffold via Write IS caught.
    ok(has_stub({mk_edit("h.py", 2, 0, "def f():\n    raise NotImplementedError", "")},
                "h.py: unimplemented stub"),
       "a Write that scaffolds an unimplemented function is caught");
    // An HTML placeholder attribute is not an unimplemented stub.
    ok(stone::introduced_stubs({mk_edit("L.tsx", 1, 1, R"(<input placeholder="Email">)", "<input>")})
           .empty(),
       "placeholder= is not a stub");

    // ── what counts as running a check ───────────────────────────────────────
    ok(!stone::runs_test("which pytest"), "naming a tool is not running it");
    ok(!stone::runs_test("pip install pytest"), "installing it is not running it");
    ok(!stone::runs_test("grep -rn pytest ."), "grepping for it is not running it");
    ok(stone::runs_test("pytest -q"), "running it is");
    ok(stone::runs_test("pip install -e . && pytest -q"), "a compound command still counts");
    ok(stone::runs_verify("npm install && npm test"), "so does install && test");
    ok(stone::runs_test("find . -name 'test_*' | xargs pytest"), "and a pipeline");

    // REGRESSION: flag-form checks sat inside a \b(...) group where the boundary could never
    // hold before '-', so every --selftest run was invisible — the gate nagged for
    // verification that HAD run, and could not see one fail.
    ok(stone::runs_test("python3 scripts/doctor.py --selftest"), "--selftest is a check");
    ok(stone::runs_verify("python3 lib/store.py --selftest"), "--selftest verifies");
    ok(stone::runs_test("cd lib && python3 store.py --selftest"), "even after a cd");
    ok(!stone::runs_test("grep -rn selftest ."), "mentioning selftest is not running one");
    ok(!stone::runs_test("echo --selftesting"), "the trailing word boundary still guards");

    // A heredoc BODY is data: an inline script that mentions a runner is not a check, and a
    // traceback in its output must not read as a failing check.
    ok(!stone::runs_test("python3 - <<'PY'\nx = 'pytest --selftest'\nPY"), "a heredoc body is data");
    ok(!stone::runs_verify("cat <<EOF\nnpm test\nEOF"), "so is any heredoc");
    ok(stone::runs_test("python3 x.py --selftest && cat <<EOF\nnope\nEOF"),
       "but the command before it still counts");

    // C and shell projects were a blind spot: `make` IS the build and ./test.sh IS the suite.
    ok(stone::runs_verify("make"), "a bare make is the build");
    ok(stone::runs_verify("cd ~/dev/tool && make 2>&1 | tail -5"), "make inside a pipeline counts");
    ok(stone::runs_test("make check"), "make check is a test");
    ok(!stone::runs_verify("make clean"), "teardown proves nothing");
    ok(!stone::runs_verify("make install"), "nor does shipping it");
    ok(stone::runs_test("timeout 300 bash test.sh"), "bash test.sh is a suite");
    ok(stone::runs_test("./test.sh"), "so is ./test.sh");
    ok(stone::runs_test("bash tests/run-checks.sh"), "and a checks script");
    ok(stone::runs_test("bun test"), "bun test is a test");
    ok(stone::runs_verify("bash -n deploy.sh"), "a syntax check verifies");
    ok(!stone::runs_verify("cat test.sh"), "reading is not running");
    ok(!stone::runs_verify("ls scripts/test.sh"), "nor is listing");

    // ── machine-generated 'user' entries are not real prompts ────────────────
    auto user_entry = [](const char* text) {
        return stone::json::parse(std::string(R"({"type":"user","message":{"content":")") + text +
                                R"("}})");
    };
    ok(!stone::is_real_user(*user_entry(
           "This session is being continued from a previous conversation. Summary: ...")),
       "a compact summary is not a prompt");
    ok(!stone::is_real_user(*user_entry("<command-name>/compact</command-name> ran")),
       "a local-command echo is not a prompt");
    ok(!stone::is_real_user(*user_entry("<task-notification>job finished</task-notification>")),
       "a task notification is not a prompt");
    ok(stone::is_real_user(*user_entry("fix the bug please")), "a real prompt is");
    ok(stone::is_real_user(*user_entry(
           "why does <command-name> appear in my transcript? fix the hook")),
       "a user QUOTING a marker mid-prompt still is");
}

}  // namespace
