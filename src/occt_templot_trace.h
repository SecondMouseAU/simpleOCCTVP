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
#include <cstdio>

namespace ot_trace {

// Returns true once the env var or ot_set_trace() has activated tracing.
bool enabled();

// Programmatic override. Pass true to force-on, false to force-off.
void set_enabled(bool on);

// Idempotent: registers an OCCT Message_Printer the first time it's called
// while tracing is enabled. No-op once registered or while disabled.
void install_occt_printer();

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
