#include "fabric/core/log.hpp"
#include "fabric/core/version.hpp"

#include <nlohmann/json.hpp>

#include <sstream>

int main() {
    if (fabric::core::version() != "0.1.0") {
        return 1;
    }

    std::ostringstream output;
    fabric::core::JsonLineLogger logger{
        output, {.session_id = "studio-42", .resource_id = "map-one"}};
    logger.write(fabric::core::LogLevel::warning, "core.test",
                 "line one\n\"line two\"", {{"asset", "wool\\blue"}});

    const auto event = nlohmann::json::parse(output.str(), nullptr, false);
    if (event.is_discarded() || event["level"] != "warning" ||
        event["category"] != "core.test" ||
        event["message"] != "line one\n\"line two\"" ||
        event["context"]["sessionId"] != "studio-42" ||
        event["context"]["resourceId"] != "map-one" ||
        event["fields"]["asset"] != "wool\\blue" ||
        !event["timestampMs"].is_number_integer()) {
        return 1;
    }
    if (fabric::core::make_trace_session_id("studio") ==
        fabric::core::make_trace_session_id("studio")) return 1;
    return 0;
}
