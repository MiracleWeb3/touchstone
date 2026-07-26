#include "scan.hpp"

#include <algorithm>
#include <unordered_set>

#include "entries.hpp"
#include "json.hpp"
#include "patterns.hpp"

namespace stone {
namespace {

struct BashRun {
    std::string command;
    bool failed = false;
    bool seen_result = false;
};

std::string str_of(const json::Value* v) {
    return (v && v->kind == json::Value::Kind::String) ? v->str : std::string();
}

int line_span(const std::string& s) {
    return s.empty() ? 0 : static_cast<int>(std::count(s.begin(), s.end(), '\n')) + 1;
}

// Truncated to 60 CODEPOINTS, not 60 bytes. This string is the key that collapses repeated
// runs of the same command into one verdict, so a byte cut would both split a multi-byte
// character and — where two long commands share a prefix — merge two distinct checks into
// one. An em dash in a commit message was enough to diverge from the Python.
std::string trim60(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::size_t i = b, chars = 0;
    while (i < e && chars < 60) {
        const auto c = static_cast<unsigned char>(s[i]);
        i += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        ++chars;
    }
    return s.substr(b, std::min(i, e) - b);
}

// tool_result bodies arrive as a bare string or as blocks; both flatten to text.
std::string result_body(const json::Value& part) {
    const json::Value* body = part.get("content");
    if (!body) return "";
    if (body->kind == json::Value::Kind::String) return body->str;
    if (body->kind == json::Value::Kind::Array) {
        std::string out;
        for (const auto& x : body->array) {
            if (!x || x->kind != json::Value::Kind::Object) continue;
            if (const json::Value* t = x->get("text")) {
                if (!out.empty()) out += " ";
                out += t->str;
            }
        }
        return out;
    }
    return "";
}

void read_edit(const std::string& name, const json::Value& input, std::vector<Edit>& edits) {
    std::string fresh, old;
    if (name == "MultiEdit") {
        // One logical change split across several replacements: join them so the churn count
        // reflects the whole edit, not just its last hunk.
        if (const json::Value* eds = input.get("edits"); eds &&
                                                         eds->kind == json::Value::Kind::Array) {
            bool first = true;
            for (const auto& e : eds->array) {
                if (!e || e->kind != json::Value::Kind::Object) continue;
                if (!first) {
                    fresh += "\n";
                    old += "\n";
                }
                first = false;
                fresh += str_of(e->get("new_string"));
                old += str_of(e->get("old_string"));
            }
        }
    } else {
        fresh = str_of(input.get("new_string"));
        if (fresh.empty()) fresh = str_of(input.get("content"));
        if (fresh.empty()) fresh = str_of(input.get("new_source"));
        old = str_of(input.get("old_string"));
    }
    std::string file = str_of(input.get("file_path"));
    if (file.empty()) file = str_of(input.get("notebook_path"));
    edits.push_back(Edit{std::move(file), line_span(fresh), line_span(old), fresh, old});
}

}  // namespace

Turn scan_turn(const std::string& transcript_path) {
    Turn turn;
    const auto entries = turn_entries(transcript_path);

    std::vector<std::pair<std::string, BashRun>> bash;  // keyed by tool_use id, insertion order
    std::unordered_set<std::string> seen_msg_ids;

    auto find_bash = [&bash](const std::string& id) -> BashRun* {
        for (auto& [k, v] : bash) {
            if (k == id) return &v;
        }
        return nullptr;
    };

    for (const auto& obj : entries) {
        if (!obj) continue;
        if (is_real_user(*obj)) {
            if (std::string t = user_text(*obj); !t.empty()) turn.last_user = std::move(t);
        }
        const std::string type = str_of(obj->get("type"));
        const json::Value* msg = obj->get("message");
        const json::Value* content = msg ? msg->get("content") : obj->get("content");

        if (type == "assistant") {
            // Claude Code writes one JSONL line per content block, each carrying the SAME
            // usage — count each message id once or tokens inflate 2-5x.
            const json::Value* usage = msg ? msg->get("usage") : nullptr;
            const std::string mid = msg ? str_of(msg->get("id")) : std::string();
            if (mid.empty() || seen_msg_ids.insert(mid).second) {
                if (usage) {
                    if (const json::Value* ot = usage->get("output_tokens");
                        ot && ot->kind == json::Value::Kind::Number) {
                        turn.out_tokens += static_cast<long long>(ot->number);
                    }
                }
            }
            if (!content || content->kind != json::Value::Kind::Array) continue;
            for (const auto& p : content->array) {
                if (!p || p->kind != json::Value::Kind::Object) continue;
                if (str_of(p->get("type")) != "tool_use") continue;
                const std::string name = str_of(p->get("name"));
                const json::Value* input = p->get("input");
                if (!input || input->kind != json::Value::Kind::Object) continue;
                if (name == "Edit" || name == "Write" || name == "MultiEdit" ||
                    name == "NotebookEdit") {
                    read_edit(name, *input, turn.edits);
                } else if (name == "Bash") {
                    bash.emplace_back(str_of(p->get("id")),
                                      BashRun{str_of(input->get("command")), false, false});
                } else if (name == "Skill") {
                    turn.skills.push_back(str_of(input->get("skill")));
                }
            }
        } else if (type == "user" && content && content->kind == json::Value::Kind::Array) {
            for (const auto& p : content->array) {
                if (!p || p->kind != json::Value::Kind::Object) continue;
                if (str_of(p->get("type")) != "tool_result") continue;
                BashRun* rec = find_bash(str_of(p->get("tool_use_id")));
                if (!rec) continue;
                const json::Value* err = p->get("is_error");
                rec->seen_result = true;
                rec->failed = (err && err->truthy()) || looks_failed(result_body(*p));
            }
        }
    }

    // Final verdict per check command: a later re-run overrides an earlier fail. Tests and
    // build/lint commands are tracked alike — a FAILING build is not verification, it is a
    // failing check.
    std::vector<std::pair<std::string, bool>> final_status;
    for (const auto& [id, rec] : bash) {
        if (!rec.seen_result) continue;
        if (!runs_test(rec.command) && !runs_verify(rec.command)) continue;
        const std::string key = trim60(rec.command);
        auto it = std::find_if(final_status.begin(), final_status.end(),
                               [&key](const auto& kv) { return kv.first == key; });
        if (it == final_status.end()) {
            final_status.emplace_back(key, rec.failed);
        } else {
            it->second = rec.failed;
        }
    }
    for (const auto& [cmd, failed] : final_status) {
        turn.tests.push_back(Check{cmd, failed});
        if (!failed) turn.verified = true;
    }
    return turn;
}

}  // namespace stone
