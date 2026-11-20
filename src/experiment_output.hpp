#pragma once

#include <glad/gl.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <string_view>

namespace lab {

inline std::filesystem::path output_path(const std::filesystem::path& root, const char* stem, const char* suffix) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    return root / (std::string(stem) + "-" + std::to_string(us) + suffix);
}

inline std::string path_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}

inline std::ofstream csv_file(const std::filesystem::path& path) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << std::setprecision(9);
    return file;
}

inline std::string gl_csv_string(GLenum field) {
    const auto* value = glGetString(field);
    std::string quoted = "\"";
    if (value) {
        for (const char ch : std::string_view(reinterpret_cast<const char*>(value))) {
            if (ch == '"') quoted += '"';
            quoted += ch;
        }
    }
    return quoted + '"';
}

}  // namespace lab
