#include "vanta/vfs/uri.h"

namespace vanta {

Uri::Uri(std::string value) : value_(std::move(value)) {}

Uri Uri::parse(std::string value) {
    return Uri(std::move(value));
}

Uri Uri::fromLocalPath(const std::filesystem::path& path) {
    std::string normalized = std::filesystem::absolute(path).lexically_normal().string();
    std::string value = "file://";
    for (char ch : normalized) {
        if (ch == ' ') {
            value += "%20";
        } else {
            value.push_back(ch);
        }
    }
    return Uri(std::move(value));
}

bool Uri::empty() const noexcept {
    return value_.empty();
}

const std::string& Uri::string() const noexcept {
    return value_;
}

std::string Uri::scheme() const {
    const std::size_t separator = value_.find(':');
    return separator == std::string::npos ? "" : value_.substr(0, separator);
}

std::string Uri::path() const {
    const std::string currentScheme = scheme();
    if (currentScheme == "file" && value_.rfind("file://", 0) == 0) {
        std::string result = value_.substr(7);
        std::string decoded;
        for (std::size_t i = 0; i < result.size(); ++i) {
            if (result.compare(i, 3, "%20") == 0) {
                decoded.push_back(' ');
                i += 2;
            } else {
                decoded.push_back(result[i]);
            }
        }
        return decoded;
    }
    const std::size_t separator = value_.find(':');
    return separator == std::string::npos ? value_ : value_.substr(separator + 1);
}

std::string Uri::filename() const {
    return std::filesystem::path(path()).filename().string();
}

std::string Uri::extension() const {
    return std::filesystem::path(path()).extension().string();
}

bool Uri::operator==(const Uri& other) const noexcept {
    return value_ == other.value_;
}

bool Uri::operator!=(const Uri& other) const noexcept {
    return !(*this == other);
}

bool Uri::operator<(const Uri& other) const noexcept {
    return value_ < other.value_;
}

}
