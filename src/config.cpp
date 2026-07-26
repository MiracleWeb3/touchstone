#include "config.hpp"

#include <cstdlib>

#include "json.hpp"

namespace pan {
namespace {

// Only the gate survived the rewrite, so only the gate's column of each preset survives.
Mode preset_mode(const std::string& preset) {
    if (preset == "economy") return Mode::Warn;
    if (preset == "quiet") return Mode::Off;
    return Mode::Block;  // "full"
}

// Returns false when the file says nothing about the gate, so the next layer can answer.
bool mode_from_file(const std::string& path, Mode& out) {
    const json::Ptr doc = json::parse_file(path);
    if (!doc || doc->kind != json::Value::Kind::Object) return false;
    if (const json::Value* g = doc->get("gate");
        g && g->kind == json::Value::Kind::String && !g->str.empty()) {
        out = mode_from_name(g->str);
        return true;
    }
    if (const json::Value* p = doc->get("preset");
        p && p->kind == json::Value::Kind::String && !p->str.empty()) {
        out = preset_mode(p->str);
        return true;
    }
    return false;
}

}  // namespace

Mode mode_from_name(const std::string& name) {
    if (name == "off") return Mode::Off;
    if (name == "warn") return Mode::Warn;
    return Mode::Block;
}

Mode load_mode(const std::string& cwd) {
    Mode mode = Mode::Block;
    if (!cwd.empty() && mode_from_file(cwd + "/.pantheon/config.json", mode)) return mode;
    const char* home = std::getenv("HOME");
    if (home && mode_from_file(std::string(home) + "/.claude/pantheon/config.json", mode)) {
        return mode;
    }
    return mode;
}

}  // namespace pan
