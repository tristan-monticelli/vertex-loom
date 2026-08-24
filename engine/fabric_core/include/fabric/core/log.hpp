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

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

class JsonLineLogger {
public:
    explicit JsonLineLogger(std::ostream& output) noexcept;

    void write(LogLevel level, std::string_view category,
               std::string_view message,
               std::initializer_list<LogField> fields = {});

private:
    std::ostream& output_;
    std::mutex mutex_;
};

} // namespace fabric::core
