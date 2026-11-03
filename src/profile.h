#ifndef PROFILE_H
#define PROFILE_H

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>

// Counts source interpreter work; native array/string operations are opaque.
struct ProfileStats {
    uint64_t ast_evaluations{0};
    uint64_t loop_iterations{0};
    uint64_t algorithm_calls{0};
    uint64_t max_call_depth{0};
};

class ProfileLimitExceeded : public std::runtime_error {
   public:
    explicit ProfileLimitExceeded(const std::string& message) : std::runtime_error(message) {}
};

class ProfileSession {
   public:
    // Each session starts with zero counts. A step is an AST evaluation or loop iteration.
    explicit ProfileSession(ProfileStats& stats, uint64_t max_steps = 1000000,
                            uint64_t max_depth = 256);
    ~ProfileSession() noexcept;
    ProfileSession(const ProfileSession&) = delete;
    ProfileSession& operator=(const ProfileSession&) = delete;

    // Call after execution, including an empty program: a user destructor may have
    // failed while running from a noexcept C++ scope cleanup.
    void check_limits() const;

   private:
    friend void profile_record_node();
    friend void profile_record_loop();
    friend void profile_defer_exception(std::exception_ptr error) noexcept;
    friend class ProfileCall;
    void record_step(bool loop);
    void enter_call();
    [[noreturn]] void exceed(const std::string& message);

    ProfileStats& stats_;
    ProfileSession* previous_;
    uint64_t max_steps_;
    uint64_t max_depth_;
    uint64_t depth_{0};
    std::string limit_error_;
    std::exception_ptr deferred_error_;
};

bool profile_active();
void profile_record_node();
void profile_record_loop();
// Retain a cleanup failure without throwing through a C++ destructor.
void profile_defer_exception(std::exception_ptr error) noexcept;

// Counts user algorithm and struct method entries, including constructors. Native
// builtins are excluded. The guard also bounds recursive operator/method calls.
class ProfileCall {
   public:
    ProfileCall();
    ~ProfileCall() noexcept;
    ProfileCall(const ProfileCall&) = delete;
    ProfileCall& operator=(const ProfileCall&) = delete;

   private:
    ProfileSession* session_;
};

#endif
