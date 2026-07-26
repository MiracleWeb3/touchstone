#include "state.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "json.hpp"

namespace stone {
namespace {

namespace fs = std::filesystem;

constexpr double kExpirySeconds = 7200;  // 2h

std::string* override_dir() {
    static std::string dir;
    return &dir;
}

// FNV-1a. The Python used MD5, but nothing here is a security boundary — these hashes only
// name a file and compare two turns for equality. A stable 64-bit hash does both, without
// carrying a digest implementation for no reason.
std::string short_hash(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%012llx", static_cast<unsigned long long>(h & 0xFFFFFFFFFFFFULL));
    return buf;
}

std::string state_path(const std::string& session) {
    return gate_dir() + "/" + short_hash(session.empty() ? "no-session" : session) + ".json";
}

double now_seconds() { return static_cast<double>(std::time(nullptr)); }

}  // namespace

void set_gate_dir(std::string dir) { *override_dir() = std::move(dir); }

std::string gate_dir() {
    if (!override_dir()->empty()) return *override_dir();
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.claude/touchstone/gate";
}

std::string turn_key_for(const std::string& prompt_id, const std::string& last_user) {
    if (!prompt_id.empty()) return short_hash(prompt_id);
    return short_hash(last_user.empty() ? "?" : last_user.substr(0, 2000));
}

int blocks_used(const std::string& session, const std::string& turn_key) {
    const json::Ptr rec = json::parse_file(state_path(session));
    if (!rec || rec->kind != json::Value::Kind::Object) return 0;
    const json::Value* key = rec->get("turn_key");
    const json::Value* ts = rec->get("ts");
    const json::Value* blocks = rec->get("blocks");
    if (!key || key->str != turn_key) return 0;
    if (!ts || now_seconds() - ts->number >= kExpirySeconds) return 0;
    return blocks ? static_cast<int>(blocks->number) : 0;
}

bool record_block(const std::string& session, const std::string& turn_key, int used) {
    const std::string path = state_path(session);
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    // tmp + rename, so a killed hook can never leave a half-written counter. A truncated
    // file parses as "no record", which would silently reset the budget — the one failure
    // this valve cannot afford.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << R"({"turn_key":")" << turn_key << R"(","blocks":)" << (used + 1) << R"(,"ts":)"
            << static_cast<long long>(now_seconds()) << "}";
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
    return true;
}

}  // namespace stone
