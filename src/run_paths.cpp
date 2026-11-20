#include "run_paths.hpp"

#include <chrono>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#endif

namespace lab {
namespace fs = std::filesystem;

fs::path executable_directory(const char* invoked) {
#ifdef _WIN32
    std::array<wchar_t, 32768> module{};
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length > 0 && length < module.size())
        return fs::path(std::wstring_view(module.data(), length)).parent_path();
#elif defined(__linux__)
    std::error_code error;
    const auto module = fs::read_symlink("/proc/self/exe", error);
    if (!error && module.is_absolute()) return module.parent_path();
#endif
    return fs::absolute(fs::path(invoked)).parent_path();
}

fs::path create_run_directory(const fs::path& parent, std::string_view name) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    const auto stem = std::string(name) + "-" + std::to_string(micros);
    fs::create_directories(parent);
    auto path = parent / stem;
    for (unsigned retry = 1; !fs::create_directory(path); ++retry)
        path = parent / (stem + "-" + std::to_string(retry));
    return path;
}

}  // namespace lab
