#include "fabric/editor/map_session.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: release_recovery_smoke source-fixture test-root\n";
        return 64;
    }
    const std::filesystem::path source = argv[1];
    const std::filesystem::path root = argv[2];
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    std::filesystem::copy(source, root,
                          std::filesystem::copy_options::recursive, error);
    if (error) {
        std::cerr << "fixture copy failed: " << error.message() << '\n';
        return 1;
    }

    fabric::editor::MapSession edited;
    if (!edited.open(root, {.value = "textile-head-preview"}) ||
        !edited.declare_event({{.value = "release-recovery-event"}, {}})) {
        std::cerr << "could not create interrupted dirty session\n";
        return 1;
    }
    const auto now = fabric::editor::AutosaveScheduler::Clock::now();
    if (edited.update_autosave(now) != fabric::editor::AutosaveStatus::not_due ||
        edited.update_autosave(now + std::chrono::seconds{31}) !=
            fabric::editor::AutosaveStatus::saved) {
        std::cerr << "autosave was not written\n";
        return 1;
    }

    fabric::editor::MapSession recovered;
    if (!recovered.open(root, {.value = "textile-head-preview"}) ||
        !recovered.has_recovery() || !recovered.accept_recovery(now) ||
        !recovered.map() || recovered.map()->events.empty() ||
        recovered.map()->events.back().id.value != "release-recovery-event" ||
        !recovered.save()) {
        std::cerr << "recovery was not accepted and saved\n";
        return 1;
    }

    fabric::editor::MapSession reloaded;
    if (!reloaded.open(root, {.value = "textile-head-preview"}) ||
        reloaded.has_recovery() || !reloaded.map() ||
        reloaded.map()->events.empty() ||
        reloaded.map()->events.back().id.value != "release-recovery-event") {
        std::cerr << "recovered change did not survive reload\n";
        return 1;
    }
    std::cout << "release recovery smoke passed\n";
    return 0;
}
