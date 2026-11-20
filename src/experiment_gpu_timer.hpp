#pragma once

#include "experiment_data.hpp"

#include <glad/gl.h>

namespace lab {

class ShadowPassTimer {
public:
    ShadowPassTimer() = default;
    ShadowPassTimer(const ShadowPassTimer&) = delete;
    ShadowPassTimer& operator=(const ShadowPassTimer&) = delete;
    ~ShadowPassTimer() {
        if (queries_[0]) glDeleteQueries(2, queries_.data());
    }

    void begin() {
        if (!queries_[0]) glCreateQueries(GL_TIMESTAMP, 2, queries_.data());
        if (waiting_) {
            GLint available = GL_FALSE;
            glGetQueryObjectiv(queries_[1], GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available) return;
            GLuint64 start = 0, end = 0;
            glGetQueryObjectui64v(queries_[0], GL_QUERY_RESULT, &start);
            glGetQueryObjectui64v(queries_[1], GL_QUERY_RESULT, &end);
            milliseconds_ = end >= start ? static_cast<double>(end - start) / 1e6 : 0.0;
            waiting_ = false;
        }
        glQueryCounter(queries_[0], GL_TIMESTAMP);
        active_ = true;
    }

    void end() {
        if (!active_) return;
        glQueryCounter(queries_[1], GL_TIMESTAMP);
        waiting_ = true;
        active_ = false;
    }

    double milliseconds() const { return milliseconds_; }
    bool has_result() const { return milliseconds_ >= 0.0; }

private:
    std::array<GLuint, 2> queries_{};
    double milliseconds_ = -1.0;
    bool active_ = false, waiting_ = false;
};

struct TimingRow {
    int pair, side, order;
    double milliseconds;
};

class ExperimentGpuTimer {
public:
    ~ExperimentGpuTimer() { stop(); }

    void start(int pairs = 24, int draws = 20) {
        stop();
        rows_.clear();
        rows_.reserve(static_cast<std::size_t>(pairs)*2);
        draws_ = draws;
        pairs_ = pairs;
        waiting_ = false;
        warmed_ = false;
        glCreateQueries(GL_TIME_ELAPSED,1,&query_);
    }

    void stop() {
        if (query_) glDeleteQueries(1,&query_);
        query_ = 0;
    }

    bool running() const { return query_ != 0; }
    int draws() const { return draws_; }
    int active_side() const { return running() && waiting_ ? side_ : -1; }
    int active_pair() const { return pair_ + 1; }
    int pair_count() const { return pairs_; }
    const std::vector<TimingRow>& rows() const { return rows_; }
    float progress() const { return static_cast<float>(rows_.size())/(pairs_*2); }

    template<class Renderer>
    bool step(Renderer& renderer, const std::array<Arm,2>& arms, int radius) {
        if (!running()) return false;
        if (!warmed_) {
            for (const auto& arm : arms) {
                renderer.bind_mask(arm,radius,false);
                for (int i=0;i<40;++i) renderer.draw();
            }
            warmed_ = true;
        }
        if (waiting_) {
            GLint available = GL_FALSE;
            glGetQueryObjectiv(query_,GL_QUERY_RESULT_AVAILABLE,&available);
            if (!available) return false;
            GLuint64 elapsed = 0;
            glGetQueryObjectui64v(query_,GL_QUERY_RESULT,&elapsed);
            rows_.push_back({pair_+1,side_,order_+1,static_cast<double>(elapsed)/(draws_*1e6)});
            waiting_ = false;
        }
        if (rows_.size() == static_cast<std::size_t>(pairs_)*2) {
            stop();
            return true;
        }
        pair_ = static_cast<int>(rows_.size()/2);
        order_ = static_cast<int>(rows_.size()%2);
        side_ = order_^(pair_&1);
        renderer.bind_mask(arms[side_],radius,false);
        glBeginQuery(GL_TIME_ELAPSED,query_);
        for (int i=0;i<draws_;++i) renderer.draw();
        glEndQuery(GL_TIME_ELAPSED);
        glFlush();
        waiting_ = true;
        return false;
    }

private:
    GLuint query_ = 0;
    std::vector<TimingRow> rows_;
    int draws_ = 20, pairs_ = 24, pair_ = 0, side_ = 0, order_ = 0;
    bool waiting_ = false, warmed_ = false;
};

}  // namespace lab
