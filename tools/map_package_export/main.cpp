#include "fabric/project/map_package.hpp"

#include <iostream>
#include <string_view>

int main(const int argument_count, char** arguments) {
    if (argument_count != 5 ||
        (std::string_view(arguments[1]) != "--map" &&
         std::string_view(arguments[1]) != "--scene")) {
        std::cerr << "usage: fabric_map_package_export (--map <map-id> | "
                     "--scene <scene-id>) "
                     "<project-directory> <package-directory>\n";
        return 64;
    }

    const fabric::core::ResourceId root_id{arguments[2]};
    if (std::string_view(arguments[1]) == "--scene") {
        const auto result = fabric::project::publish_scene_package(
            arguments[3], root_id, arguments[4]);
        for (const auto& error : result.errors)
            std::cerr << fabric::project::to_string(error.code) << " ["
                      << error.field << "]: " << error.message << '\n';
        return result.ok() ? 0 : 1;
    }
    const auto result = fabric::project::publish_map_package(
        arguments[3], root_id, arguments[4]);
    for (const auto& error : result.errors)
        std::cerr << fabric::project::to_string(error.code) << " ["
                  << error.field << "]: " << error.message << '\n';
    return result.ok() ? 0 : 1;
}
