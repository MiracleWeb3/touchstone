#include "entries.hpp"

#include <cstdio>
#include <fstream>
#include <regex>

namespace pan {
namespace {

// Anchored on purpose: these are machine entries only when they ARE the message, not when a
// real user quotes one mid-prompt ("why does <command-name> show up in my transcript?").
const std::regex& machine_user_re() {
    static const std::regex r(
        R"(^\s*(This session is being continued from a previous conversation|)"
        R"(<command-name>|<local-command-|<task-notification|<system-reminder|)"
        R"(\[Request interrupted))");
    return r;
}

const json::Value* content_of(const json::Value& obj) {
    if (const json::Value* msg = obj.get("message")) {
        if (const json::Value* c = msg->get("content")) return c;
    }
    return obj.get("content");
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Reads the last max_bytes of a file (whole file when max_bytes is 0). `whole` reports
// whether the window reached the start, so the caller knows not to widen further.
std::vector<std::string> tail_lines(const std::string& path, std::size_t max_bytes, bool& whole) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    std::vector<std::string> lines;
    whole = true;
    if (!in) return lines;
    const auto size = static_cast<std::size_t>(in.tellg());
    const std::size_t start = (max_bytes == 0 || size <= max_bytes) ? 0 : size - max_bytes;
    whole = (start == 0);
    in.seekg(static_cast<std::streamoff>(start));
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t from = 0;
    if (start != 0) {
        const auto nl = data.find('\n');
        if (nl != std::string::npos) from = nl + 1;  // drop the partial first line
    }
    while (from <= data.size()) {
        const auto nl = data.find('\n', from);
        if (nl == std::string::npos) {
            if (from < data.size()) lines.push_back(data.substr(from));
            break;
        }
        lines.push_back(data.substr(from, nl - from));
        from = nl + 1;
    }
    return lines;
}

}  // namespace

std::string user_text(const json::Value& obj) {
    const json::Value* c = content_of(obj);
    if (!c) return "";
    if (c->kind == json::Value::Kind::String) return trim(c->str);
    if (c->kind == json::Value::Kind::Array) {
        std::string out;
        for (const auto& p : c->array) {
            if (!p || p->kind != json::Value::Kind::Object) continue;
            if (const json::Value* t = p->get("text")) {
                if (!out.empty()) out += " ";
                out += t->str;
            }
        }
        return trim(out);
    }
    return "";
}

bool is_real_user(const json::Value& obj) {
    const json::Value* type = obj.get("type");
    if (!type || type->str != "user") return false;
    if (const json::Value* meta = obj.get("isMeta"); meta && meta->truthy()) return false;

    const json::Value* c = content_of(obj);
    if (!c) return false;
    if (c->kind == json::Value::Kind::String) {
        return !trim(c->str).empty() && !std::regex_search(c->str, machine_user_re());
    }
    if (c->kind == json::Value::Kind::Array) {
        bool has_text = false, has_tool_result = false;
        for (const auto& p : c->array) {
            if (!p || p->kind != json::Value::Kind::Object) continue;
            const json::Value* t = p->get("type");
            if (!t) continue;
            if (t->str == "text") has_text = true;
            if (t->str == "tool_result") has_tool_result = true;
        }
        if (!has_text || has_tool_result) return false;
        const std::string text = user_text(obj);
        return !std::regex_search(text, machine_user_re());
    }
    return false;
}

std::vector<json::Ptr> turn_entries(const std::string& path) {
    std::vector<json::Ptr> result;
    if (path.empty()) return result;
    {
        std::ifstream probe(path);
        if (!probe) return result;
    }
    for (const std::size_t cap : {std::size_t{4'000'000}, std::size_t{32'000'000}, std::size_t{0}}) {
        bool whole = false;
        const auto lines = tail_lines(path, cap, whole);
        std::vector<json::Ptr> entries;
        entries.reserve(lines.size());
        for (const auto& line : lines) {
            if (line.empty()) continue;
            json::Ptr obj = json::parse(line);
            if (obj && obj->kind == json::Value::Kind::Object) entries.push_back(std::move(obj));
        }
        for (std::size_t i = entries.size(); i-- > 0;) {
            if (is_real_user(*entries[i])) {
                result.assign(entries.begin() + static_cast<long>(i), entries.end());
                return result;
            }
        }
        if (whole) return entries;
    }
    return result;
}

}  // namespace pan
