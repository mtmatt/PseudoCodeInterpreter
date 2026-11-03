#include "profile.h"

#include <algorithm>

namespace {
thread_local ProfileSession* active_session = nullptr;
}  // namespace

ProfileSession::ProfileSession(ProfileStats& stats, uint64_t max_steps, uint64_t max_depth)
    : stats_(stats), previous_(active_session), max_steps_(max_steps), max_depth_(max_depth) {
    stats_ = {};
    active_session = this;
}

ProfileSession::~ProfileSession() noexcept { active_session = previous_; }

void ProfileSession::check_limits() const {
    if (deferred_error_) std::rethrow_exception(deferred_error_);
    if (!limit_error_.empty()) throw ProfileLimitExceeded(limit_error_);
}

void ProfileSession::exceed(const std::string& message) {
    limit_error_ = message;
    throw ProfileLimitExceeded(limit_error_);
}

void ProfileSession::record_step(bool loop) {
    check_limits();
    // Subtraction avoids overflow even when the caller selects UINT64_MAX.
    if (stats_.loop_iterations >= max_steps_ ||
        stats_.ast_evaluations >= max_steps_ - stats_.loop_iterations) {
        exceed("Profile step limit exceeded (AST evaluations + loop iterations; limit " +
               std::to_string(max_steps_) + ")");
    }
    if (loop)
        ++stats_.loop_iterations;
    else
        ++stats_.ast_evaluations;
}

void ProfileSession::enter_call() {
    check_limits();
    if (depth_ >= max_depth_) {
        exceed("Profile call depth limit exceeded (" + std::to_string(max_depth_) + ")");
    }
    ++stats_.algorithm_calls;
    ++depth_;
    stats_.max_call_depth = std::max(stats_.max_call_depth, depth_);
}

bool profile_active() { return active_session != nullptr; }

void profile_record_node() {
    if (active_session) active_session->record_step(false);
}

void profile_record_loop() {
    if (active_session) active_session->record_step(true);
}

void profile_defer_exception(std::exception_ptr error) noexcept {
    if (active_session && !active_session->deferred_error_) {
        active_session->deferred_error_ = error;
    }
}

ProfileCall::ProfileCall() : session_(active_session) {
    if (session_) session_->enter_call();
}

ProfileCall::~ProfileCall() noexcept {
    if (session_) --session_->depth_;
}
