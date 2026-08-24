#include "fabric/core/log.hpp"

#include <chrono>
#include <ostream>

namespace fabric::core {
namespace {

void write_escaped(std::ostream& output, const std::string_view value) {
    constexpr char hexadecimal[] = "0123456789abcdef";
    output.put('"');
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) {
                output << "\\u00" << hexadecimal[character >> 4U]
                       << hexadecimal[character & 0x0fU];
            } else {
                output.put(static_cast<char>(character));
            }
        }
    }
    output.put('"');
}

} // namespace

std::string_view to_string(const LogLevel level) noexcept {
    switch (level) {
    case LogLevel::debug: return "debug";
    case LogLevel::info: return "info";
    case LogLevel::warning: return "warning";
    case LogLevel::error: return "error";
    }
    return "unknown";
}

JsonLineLogger::JsonLineLogger(std::ostream& output) noexcept : output_(output) {}

void JsonLineLogger::write(const LogLevel level, const std::string_view category,
                           const std::string_view message,
                           const std::initializer_list<LogField> fields) {
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::scoped_lock lock(mutex_);

    output_ << "{\"timestampMs\":" << timestamp << ",\"level\":";
    write_escaped(output_, to_string(level));
    output_ << ",\"category\":";
    write_escaped(output_, category);
    output_ << ",\"message\":";
    write_escaped(output_, message);
    output_ << ",\"fields\":{";

    bool first = true;
    for (const auto& field : fields) {
        if (!first) {
            output_.put(',');
        }
        first = false;
        write_escaped(output_, field.key);
        output_.put(':');
        write_escaped(output_, field.value);
    }
    output_ << "}}\n";
    output_.flush();
}

} // namespace fabric::core
