#pragma once

#include <filesystem>
#include <stop_token>

namespace lab {

void write_publication_figures(const std::filesystem::path& pilot, std::stop_token stop);

}  // namespace lab
