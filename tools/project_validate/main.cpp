#include "fabric/core/log.hpp"
#include "fabric/project/manifest.hpp"

#include <iostream>
#include <string_view>

int main(const int argument_count, char** arguments) {
    const bool structured_output = argument_count == 3 &&
                                   std::string_view(arguments[1]) == "--json";
    if (argument_count != 2 && !structured_output) {
        std::cerr << "usage: fabric_project_validate [--json] <project-directory>\n";
        return 64;
    }

    const char* project_path = arguments[structured_output ? 2 : 1];
    const auto report = fabric::project::validate_project(project_path);
    fabric::core::JsonLineLogger logger{std::cerr};
    for (const auto& error : report.errors) {
        if (structured_output) {
            logger.write(fabric::core::LogLevel::error, "project.validation",
                         error.message,
                         {{"code", fabric::project::to_string(error.code)},
                          {"field", error.field}});
        } else {
            std::cerr << fabric::project::to_string(error.code) << " ["
                      << error.field << "]: " << error.message << '\n';
        }
    }
    if (!report.ok()) {
        return 1;
    }

    if (structured_output) {
        fabric::core::JsonLineLogger success_logger{std::cout};
        success_logger.write(fabric::core::LogLevel::info,
                             "project.validation", "project is valid",
                             {{"project", project_path}});
    } else {
        std::cout << "project is valid\n";
    }
    return 0;
}
