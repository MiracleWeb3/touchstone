// A small JSON reader — enough to walk a Claude Code transcript.
//
// No library, on purpose: this runs on every Stop, and metal's rule is that a dependency
// you have not read is a runtime you cannot predict. Everything here is read-only; nothing
// writes JSON, because the one object this hook emits is two fixed fields.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pan::json {

struct Value;
using Ptr = std::shared_ptr<Value>;

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<Ptr> array;
    std::vector<std::pair<std::string, Ptr>> object;

    // Object lookup. Returns nullptr when absent or when this is not an object, so callers
    // can chain without checking kind at every step.
    const Value* get(std::string_view key) const;
    bool truthy() const { return kind == Kind::Bool ? boolean : kind != Kind::Null; }
};

// Parses a whole document. Returns nullptr on any malformed input — a broken settings file
// must degrade to "no overrides", never to a crash in a hook.
Ptr parse(std::string_view text);

// Reads and parses a file. nullptr if unreadable.
Ptr parse_file(const std::string& path);

}  // namespace pan::json
