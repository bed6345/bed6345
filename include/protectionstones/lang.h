// ProtectionStones for Bedrock — Lang
//
// Tiny message catalogue loaded from a JSON file (default: Thai). Messages use
// fmt-style positional placeholders ("{}", "{0}") so server owners can re-word
// or translate everything without touching the code. Missing keys fall back to
// a built-in default, and if a placeholder is malformed the raw text is used so
// a typo in the config can never crash the server.

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <fmt/args.h>
#include <fmt/format.h>

namespace endstone {
class Logger;
}

namespace ps {

class Lang {
public:
    /// Loads messages from `file`. If the file does not exist it is created from
    /// the built-in defaults so admins have something to edit.
    void load(const std::filesystem::path &file, endstone::Logger &logger);

    /// Returns the raw template for a key, or the key itself if unknown.
    [[nodiscard]] const std::string &raw(const std::string &key) const;

    /// Formats a message with positional arguments.
    template <typename... Args>
    [[nodiscard]] std::string get(const std::string &key, Args &&...args) const
    {
        const std::string &tpl = raw(key);
        try {
            fmt::dynamic_format_arg_store<fmt::format_context> store;
            (store.push_back(std::forward<Args>(args)), ...);
            return fmt::vformat(tpl, store);
        }
        catch (const std::exception &) {
            return tpl;  // malformed placeholder — return template verbatim
        }
    }

private:
    static const std::unordered_map<std::string, std::string> &defaults();

    std::unordered_map<std::string, std::string> messages_;
};

}  // namespace ps
