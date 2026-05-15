#pragma once

#include <filesystem>
#include <string>

namespace vanta {

class Uri {
public:
    Uri() = default;
    explicit Uri(std::string value);

    static Uri parse(std::string value);
    static Uri fromLocalPath(const std::filesystem::path& path);

    bool empty() const noexcept;
    const std::string& string() const noexcept;
    std::string scheme() const;
    std::string path() const;
    std::string filename() const;
    std::string extension() const;

    bool operator==(const Uri& other) const noexcept;
    bool operator!=(const Uri& other) const noexcept;
    bool operator<(const Uri& other) const noexcept;

private:
    std::string value_;
};

}
