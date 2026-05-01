/*
 * occt_templot_trace.cpp — diagnostic tracing implementation
 */

#include "occt_templot.h"
#include "occt_templot_trace.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <TCollection_AsciiString.hxx>

namespace {

// -1 = uninitialised (re-read env), 0 = off, 1 = on.
std::atomic<int> g_trace_state{-1};

bool read_env() {
    const char* e = std::getenv("OCCT_TEMPLOT_TRACE");
    if (!e || !*e) return false;
    if (std::strcmp(e, "0") == 0) return false;
    return true;
}

class StderrPrinter : public Message_Printer {
protected:
    void send(const TCollection_AsciiString& theString,
              const Message_Gravity theGravity) const override {
        if (!::ot_trace::enabled()) return;
        const char* tag = "info";
        switch (theGravity) {
            case Message_Trace:   tag = "trace"; break;
            case Message_Info:    tag = "info";  break;
            case Message_Warning: tag = "warn";  break;
            case Message_Alarm:   tag = "alarm"; break;
            case Message_Fail:    tag = "fail";  break;
        }
        std::fprintf(stderr, "[occt:%s] %s\n", tag, theString.ToCString());
        std::fflush(stderr);
    }
};

std::once_flag g_printer_once;

} // namespace

namespace ot_trace {

bool enabled() {
    int v = g_trace_state.load(std::memory_order_relaxed);
    if (v < 0) {
        v = read_env() ? 1 : 0;
        int expected = -1;
        g_trace_state.compare_exchange_strong(expected, v,
                                              std::memory_order_relaxed);
    }
    return v != 0;
}

void set_enabled(bool on) {
    g_trace_state.store(on ? 1 : 0, std::memory_order_relaxed);
    if (on) install_occt_printer();
}

void install_occt_printer() {
    if (!enabled()) return;
    std::call_once(g_printer_once, []() {
        Handle(Message_Printer) printer = new StderrPrinter();
        printer->SetTraceLevel(Message_Trace);
        Message::DefaultMessenger()->AddPrinter(printer);
    });
}

} // namespace ot_trace

extern "C" {

OT_EXPORT void ot_set_trace(bool enable) {
    ::ot_trace::set_enabled(enable);
}

OT_EXPORT bool ot_get_trace(void) {
    return ::ot_trace::enabled();
}

} // extern "C"
