#include "fabric/runtime/preview_runtime.hpp"

#include <iostream>
#include <string_view>

namespace {

void usage() {
    std::cerr << "usage: game_runtime --project <path> --map <id> "
                 "[--smoke-test [frames]] [--benchmark [frames]]\n";
}

} // namespace

int main(int argc, char** argv) {
    fabric::runtime::PreviewRuntimeOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--project" && index + 1 < argc) {
            options.project_root = argv[++index];
        } else if (argument == "--map" && index + 1 < argc) {
            options.map_id.value = argv[++index];
        } else if (argument == "--smoke-test") {
            options.mode = fabric::runtime::RuntimeMode::smoke_test;
            if (index + 1 < argc && argv[index + 1][0] != '-')
                options.frame_limit = static_cast<std::size_t>(std::stoull(argv[++index]));
        } else if (argument == "--benchmark") {
            options.mode = fabric::runtime::RuntimeMode::benchmark;
            if (index + 1 < argc && argv[index + 1][0] != '-')
                options.frame_limit = static_cast<std::size_t>(std::stoull(argv[++index]));
        } else if (argument == "--help") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    fabric::runtime::PreviewRuntime runtime;
    if (!runtime.load(options) || !runtime.run()) {
        for (const auto& error : runtime.errors()) std::cerr << "error: " << error << '\n';
        return 1;
    }
    if (options.mode != fabric::runtime::RuntimeMode::interactive) {
        std::cout << "frames=" << runtime.stats().frames
                  << " visible=" << runtime.stats().visible_instances
                  << " draw_calls=" << runtime.stats().draw_calls
                  << " triangles=" << runtime.stats().triangles << '\n';
    }
    return 0;
}
