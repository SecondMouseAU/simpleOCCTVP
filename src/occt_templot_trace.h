/*
 * occt_templot_trace.h — internal diagnostic tracing
 *
 * Off by default. Enable via `OCCT_TEMPLOT_TRACE=1` env var or by calling
 * the public ot_set_trace() from a host language. When enabled:
 *   - OT_TRACE(...) lines are written to stderr with an "[ot] " prefix
 *     and flushed immediately (so a hung call is still visible).
 *   - OCCT's own Message::DefaultMessenger() is wired to stderr too,
 *     surfacing internal warnings/info from ShapeFix, Sewing, etc.
 *
 * This header is INTERNAL — never include from public occt_templot.h.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>

namespace ot_trace {

// Returns true once the env var or ot_set_trace() has activated tracing.
bool enabled();

// Programmatic override. Pass true to force-on, false to force-off.
void set_enabled(bool on);

// Idempotent: registers an OCCT Message_Printer the first time it's called
// while tracing is enabled. No-op once registered or while disabled.
void install_occt_printer();

// RAII wall-clock timer. Emits `[ot] <label>: <ms> ms` on destruction
// when tracing is enabled. Construction always reads the clock once
// (cheap; ~ns) so toggling tracing mid-call still gives a valid duration.
class ScopedTimer {
public:
    explicit ScopedTimer(const char* label)
        : label_(label), start_(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() {
        if (!enabled()) return;
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - start_).count();
        std::fprintf(stderr, "[ot] %s: %.3f ms\n", label_, us / 1000.0);
        std::fflush(stderr);
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
private:
    const char* label_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace ot_trace

// Lightweight format-string trace. Evaluates args only when enabled.
#define OT_TRACE(...)                                                          \
    do {                                                                       \
        if (::ot_trace::enabled()) {                                           \
            std::fprintf(stderr, "[ot] ");                                     \
            std::fprintf(stderr, __VA_ARGS__);                                 \
            std::fputc('\n', stderr);                                          \
            std::fflush(stderr);                                               \
        }                                                                      \
    } while (0)

// Scope-bound wall-clock timer. Place at the top of a function (after
// the NULL guard) to log "<label>: <ms> ms" on every return path,
// including exception unwind. Label must outlive the scope (string
// literal is the safe default).
#define OT_TRACE_CONCAT_INNER_(a, b) a##b
#define OT_TRACE_CONCAT_(a, b) OT_TRACE_CONCAT_INNER_(a, b)
#define OT_TRACE_TIMER(label)                                                  \
    ::ot_trace::ScopedTimer OT_TRACE_CONCAT_(_ot_timer_, __LINE__)(label)
