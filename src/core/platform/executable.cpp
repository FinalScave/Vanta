#include "mornox/platform/executable.h"

#include <cstdlib>
#include <string>
#include <utility>

namespace mornox {
namespace {

bool UsableExecutablePath(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error) && !std::filesystem::is_directory(path, error);
}

std::vector<std::filesystem::path> ExecutableNames(const std::string& executable) {
    std::vector<std::filesystem::path> names;
    names.emplace_back(executable);
#if defined(_WIN32)
    if (!std::filesystem::path(executable).has_extension()) {
        names.emplace_back(executable + ".exe");
        names.emplace_back(executable + ".cmd");
        names.emplace_back(executable + ".bat");
        names.emplace_back(executable + ".com");
    }
#endif
    return names;
}

std::vector<std::string> SplitSearchPath(const char* path) {
    std::vector<std::string> entries;
    if (path == nullptr) {
        return entries;
    }
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::string current;
    for (const char character : std::string(path)) {
        if (character == separator) {
            entries.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    entries.push_back(std::move(current));
    return entries;
}

}

std::optional<std::filesystem::path> FindExecutableOnPath(const std::string& executable) {
    if (executable.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path requested(executable);
    if (requested.is_absolute() || requested.has_parent_path()) {
        for (const std::filesystem::path& name : ExecutableNames(executable)) {
            if (UsableExecutablePath(name)) {
                return name;
            }
        }
        return std::nullopt;
    }

    for (const std::string& entry : SplitSearchPath(std::getenv("PATH"))) {
        const std::filesystem::path directory = entry.empty() ? std::filesystem::path(".") : std::filesystem::path(entry);
        for (const std::filesystem::path& name : ExecutableNames(executable)) {
            const std::filesystem::path candidate = directory / name;
            if (UsableExecutablePath(candidate)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> FindFirstExecutableOnPath(const std::vector<std::string>& executables) {
    for (const std::string& executable : executables) {
        if (auto found = FindExecutableOnPath(executable)) {
            return found;
        }
    }
    return std::nullopt;
}

}
