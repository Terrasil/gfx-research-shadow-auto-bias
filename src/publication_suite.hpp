#pragma once

#include "benchmark.hpp"

#include <mutex>
#include <thread>

namespace lab {

enum class PublicationKind { quick, full };
enum class PublicationState { idle, running, complete, cancelled, failed };

struct PublicationCase {
    int number;
    StudyProfile profile;
    StudyCase setup;
    std::optional<StudyScores> scores;
};

struct PublicationStatus {
    PublicationState state = PublicationState::idle;
    std::filesystem::path output;
    std::string detail;
    int completed = 0, total = 0;
    bool stopping = false;
    std::optional<PublicationCase> current;
};

class PublicationSuite {
public:
    void start(PublicationKind kind, const std::filesystem::path& heightmaps,
               const std::filesystem::path& publication_root);
    void cancel();
    void wait();
    PublicationStatus status() const;
    std::optional<PublicationCase> completed_case(int number) const;

private:
    void run(PublicationKind kind, const std::filesystem::path& heightmaps,
             const std::filesystem::path& output, std::stop_token stop);
    void report(StudyProfile profile, const char* name, int offset, const StudyUpdate& update);

    mutable std::mutex mutex_;
    PublicationStatus status_;
    std::vector<PublicationCase> completed_cases_;
    std::jthread worker_;
};

}  // namespace lab
