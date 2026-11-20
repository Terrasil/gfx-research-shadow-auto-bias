#pragma once

#include <filesystem>
#include <string_view>

namespace lab {

std::filesystem::path executable_directory(const char* invoked);
std::filesystem::path create_run_directory(const std::filesystem::path& parent, std::string_view name);

inline std::filesystem::path default_results_directory(const char* invoked) {
    return executable_directory(invoked) / "results";
}

}  // namespace lab
