#include "publication_suite.hpp"

#include "publication_figure.hpp"
#include "run_paths.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace lab {
namespace fs = std::filesystem;

void PublicationSuite::start(PublicationKind kind, const fs::path& heightmaps, const fs::path& publication_root) {
    if (status().state == PublicationState::running) return;
    const bool full = kind == PublicationKind::full;
    const auto output = create_run_directory(publication_root, full ? "full" : "quick");
    const int total = study_case_count(StudyProfile::pilot) + (full ? study_case_count(StudyProfile::production) : 0);
    {
        const std::lock_guard lock(mutex_);
        completed_cases_.clear();
        completed_cases_.reserve(total);
        status_ = {PublicationState::running, output, "Preparing pilot study", 0, total, false, {}};
    }

    try {
        worker_ = std::jthread([this, kind, heightmaps, output](std::stop_token stop) {
            run(kind, heightmaps, output, stop);
        });
    } catch (const std::exception& error) {
        const std::lock_guard lock(mutex_);
        status_.state = PublicationState::failed;
        status_.detail = error.what();
        throw;
    }
}

void PublicationSuite::cancel() {
    const std::lock_guard lock(mutex_);
    if (status_.state == PublicationState::running) {
        status_.stopping = true;
        worker_.request_stop();
    }
}

void PublicationSuite::wait() {
    if (worker_.joinable()) worker_.join();
}

PublicationStatus PublicationSuite::status() const {
    const std::lock_guard lock(mutex_);
    return status_;
}

std::optional<PublicationCase> PublicationSuite::completed_case(int number) const {
    const std::lock_guard lock(mutex_);
    if (number < 1 || number > static_cast<int>(completed_cases_.size())) return std::nullopt;
    return completed_cases_[number - 1];
}

void PublicationSuite::report(StudyProfile profile, const char* name, int offset, const StudyUpdate& update) {
    const std::lock_guard lock(mutex_);
    status_.completed = offset + update.completed;
    status_.detail = std::string(name) + " / " + std::string(update.detail);
    if (update.active) {
        const int number = update.active->boxes ? status_.completed + (update.scores ? 0 : 1) : 0;
        status_.current = PublicationCase{number, profile, *update.active, update.scores};
        if (update.scores) completed_cases_.push_back(*status_.current);
    }
}

void PublicationSuite::run(PublicationKind kind, const fs::path& heightmaps, const fs::path& output,
                           std::stop_token stop) {
    PublicationState result = PublicationState::cancelled;
    std::string description = "Suite stopped. The run folder contains the completed cases.";
    try {
        const bool full = kind == PublicationKind::full;
        const auto run_profile = [&](StudyProfile profile, const char* name, int offset) {
            return run_study(profile, heightmaps, output / name, stop,
                [&](const StudyUpdate& update) { report(profile, name, offset, update); });
        };

        if (run_profile(StudyProfile::pilot, "pilot", 0) && !stop.stop_requested()
                && (!full || run_profile(StudyProfile::production, "production", study_case_count(StudyProfile::pilot)))
                && !stop.stop_requested()) {
            {
                const std::lock_guard lock(mutex_);
                status_.detail = "Exporting comparison PNGs";
            }
            write_publication_figures(output / "pilot", stop);
            if (!stop.stop_requested()) {
                result = PublicationState::complete;
                description = "Publication suite completed.";
            }
        }
    } catch (const std::exception& error) {
        result = PublicationState::failed;
        description = error.what();
    }

    try {
        const auto snapshot = status();
        std::ofstream manifest(output / "suite.txt");
        manifest.exceptions(std::ios::failbit | std::ios::badbit);
        const char* record = result == PublicationState::complete ? "complete"
            : result == PublicationState::cancelled ? "cancelled" : "failed";
        manifest << "suite=" << (kind == PublicationKind::full ? "full" : "quick") << '\n'
                 << "status=" << record << "\ncompleted_cases=" << snapshot.completed
                 << "\nrequested_cases=" << snapshot.total << '\n';
        manifest.close();
    } catch (const std::exception& error) {
        if (result != PublicationState::failed) {
            result = PublicationState::failed;
            description = error.what();
        }
    }
    const std::lock_guard lock(mutex_);
    status_.state = result;
    status_.detail = std::move(description);
    status_.stopping = false;
}

}  // namespace lab
