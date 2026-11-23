#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <string_view>
#include <utility>

struct ExperimentProgress {
    std::size_t completed = 0;
    std::size_t total = 0;
    std::string_view scene;
    std::string_view method;
};

// A run pauses between measurement blocks so the application can draw its menu.
class ExperimentRun {
public:
    struct promise_type {
        ExperimentProgress progress;
        std::exception_ptr error;

        ExperimentRun get_return_object() noexcept {
            return ExperimentRun{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        std::suspend_always yield_value(ExperimentProgress update) noexcept {
            progress = update;
            return {};
        }
        void return_void() const noexcept {}
        void unhandled_exception() noexcept { error = std::current_exception(); }
    };

    ExperimentRun() = default;
    ExperimentRun(ExperimentRun&& other) noexcept : state_(std::exchange(other.state_, {})) {}
    ExperimentRun& operator=(ExperimentRun&& other) noexcept {
        if (this != &other) {
            reset();
            state_ = std::exchange(other.state_, {});
        }
        return *this;
    }
    ~ExperimentRun() { reset(); }

    explicit operator bool() const noexcept { return state_ && !state_.done(); }
    const ExperimentProgress& progress() const noexcept { return state_.promise().progress; }

    bool advance() {
        if (!*this) return false;
        state_.resume();
        if (auto error = state_.promise().error) std::rethrow_exception(error);
        return !state_.done();
    }

    void reset() noexcept {
        if (state_) std::exchange(state_, {}).destroy();
    }

private:
    explicit ExperimentRun(std::coroutine_handle<promise_type> state) noexcept : state_(state) {}
    std::coroutine_handle<promise_type> state_{};
};
