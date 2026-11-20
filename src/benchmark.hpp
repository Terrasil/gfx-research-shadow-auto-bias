#pragma once

#include "experiment_data.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>

namespace lab {

enum class StudyProfile { pilot, production };

inline constexpr std::array study_preview_methods{Method::constant, Method::footprint, Method::full};
using StudyScores = std::array<Counts, study_preview_methods.size()>;

struct StudyCase {
    std::string scene;
    int elevation, azimuth, resolution, warp, radius;
    bool boxes;
};

struct StudyUpdate {
    int completed;
    std::string_view detail;
    std::optional<StudyCase> active;
    std::optional<StudyScores> scores;
};

using CaseProgress = std::function<void(const StudyUpdate&)>;

int study_case_count(StudyProfile profile);
bool run_study(StudyProfile profile, const std::filesystem::path& heightmaps,
               const std::filesystem::path& output, std::stop_token stop, const CaseProgress& progress);

}  // namespace lab

int benchmark_main(int argc, char** argv);
