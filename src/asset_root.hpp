#pragma once

#include "run_paths.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace lab {

inline std::filesystem::path asset_root(const char* executable, const char* resource) {
    namespace fs = std::filesystem;
    const auto executable_dir = executable_directory(executable);
#ifndef GFX_RESEARCH_SOURCE_DIR
    const fs::path project_dir = fs::current_path();
#else
    const fs::path project_dir = GFX_RESEARCH_SOURCE_DIR;
#endif
    for (const auto& root : {project_dir, fs::current_path(), executable_dir}) {
        if (fs::exists(root/resource)) return root;
    }
    throw std::runtime_error(std::string("Cannot locate project resource: ") + resource);
}

}  // namespace lab
