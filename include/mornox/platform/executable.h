#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mornox {

std::optional<std::filesystem::path> FindExecutableOnPath(const std::string& executable);
std::optional<std::filesystem::path> FindFirstExecutableOnPath(const std::vector<std::string>& executables);

}
