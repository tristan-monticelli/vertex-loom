#pragma once

#include <initializer_list>
#include <iosfwd>
#include <mutex>
#include <string>
#include <string_view>

namespace fabric::core {

enum class LogLevel {
    debug,
    info,
    warning,
    error,
};

struct LogField {
    std::string_view key;
    std::string_view value;
};

struct TraceContext {
    std::string session_id;
    std::string resource_id;
};

[[nodiscard]] std::string make_trace_session_id(
    std::string_view prefix = "session");

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

class JsonLineLogger {
public:
    explicit JsonLineLogger(std::ostream& output,
                            TraceContext context = {}) noexcept;

    void write(LogLevel level, std::string_view category,
               std::string_view message,
               std::initializer_list<LogField> fields = {});

private:
    std::ostream& output_;
    TraceContext context_;
    std::mutex mutex_;
};

} // namespace fabric::core
