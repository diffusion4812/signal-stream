#pragma once

#include "service-bus.h"
#include "SDL3/SDL_log.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#define COMPILED_TRACE_LEVEL SPDLOG_LEVEL_TRACE

class Logger {
public:
    struct Event {
        enum class Severity : int {
            Trace = SPDLOG_LEVEL_TRACE,
            Debug = SPDLOG_LEVEL_DEBUG,
            Info = SPDLOG_LEVEL_INFO,
            Warning = SPDLOG_LEVEL_WARN,
            Error = SPDLOG_LEVEL_ERROR,
            Critical = SPDLOG_LEVEL_CRITICAL,
            Off = SPDLOG_LEVEL_OFF
        };
        Severity severity;
        std::string message;
    };
    Logger(ServiceBus& bus) :
        bus_(bus) {

        log_ = spdlog::basic_logger_st("signal-stream logger", "logs/signal-stream.log");

        log_->set_level((spdlog::level::level_enum)COMPILED_TRACE_LEVEL);
        log_->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");

        token_ = bus_.Subscribe<Event>([&](const Event& ev) {
            log_->log((spdlog::level::level_enum)ev.severity, ev.message);
            });

        bus_.Publish<Event>(Event{ Event::Severity::Info, "Application logging service started" });
    }

private:
    ServiceBus& bus_;
    SubscriptionToken token_;
    std::shared_ptr<spdlog::logger> log_;
};

#if COMPILED_TRACE_LEVEL <= SPDLOG_LEVEL_TRACE
#define TRACE_FUNCTION_SCOPE(bus) \
        FunctionTraceScope traceScope(bus, __FUNCTION__)
#else
#define TRACE_FUNCTION_SCOPE(bus) ((void)0)
#endif

class FunctionTraceScope {
public:
    FunctionTraceScope(ServiceBus& bus, const std::string& function) :
        bus_(bus),
        function_(function) {
        bus_.Publish<Logger::Event>(Logger::Event{ (Logger::Event::Severity)COMPILED_TRACE_LEVEL, std::string(">> Entered ") + function_ });
    }

    ~FunctionTraceScope() {
        bus_.Publish<Logger::Event>(Logger::Event{ (Logger::Event::Severity)COMPILED_TRACE_LEVEL, std::string("<< Left ") + function_ });
    }

private:
    ServiceBus& bus_;
    std::string function_;
};
