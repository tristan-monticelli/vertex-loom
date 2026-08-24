#include "fabric/core/log.hpp"
#include "fabric/core/version.hpp"

#include <nlohmann/json.hpp>

#include <sstream>

int main() {
    if (fabric::core::version() != "0.1.0") {
        return 1;
    }

    std::ostringstream output;
    fabric::core::JsonLineLogger logger{output};
    logger.write(fabric::core::LogLevel::warning, "core.test",
                 "line one\n\"line two\"", {{"asset", "wool\\blue"}});

    const auto event = nlohmann::json::parse(output.str(), nullptr, false);
    if (event.is_discarded() || event["level"] != "warning" ||
        event["category"] != "core.test" ||
        event["message"] != "line one\n\"line two\"" ||
        event["fields"]["asset"] != "wool\\blue" ||
        !event["timestampMs"].is_number_integer()) {
        return 1;
    }
    return 0;
}
