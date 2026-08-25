#include "fabric/editor/project_session.hpp"
#include "fabric/editor/creation_prompts.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string_view suffix) {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
                ("fabric-editor-test-" + std::to_string(unique) + "-" +
                 std::string(suffix));
        std::filesystem::create_directories(path_);
        std::cerr << "[ fixture  ] created " << path_.string() << '\n'
                  << std::flush;
    }

    ~TemporaryDirectory() {
        std::cerr << "[ fixture  ] cleaning " << path_.string() << '\n'
                  << std::flush;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::cerr << "[ fixture  ] cleaned " << path_.string() << '\n'
                  << std::flush;
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_valid_project(const std::filesystem::path& root) {
    for (const auto* directory : {"assets", "entities", "maps", "scenes", "schemas"}) {
        std::filesystem::create_directory(root / directory);
    }
    const fabric::project::ProjectManifest manifest{
        .schema_version = fabric::project::current_schema_version,
        .id = {.value = "studio-project"},
        .name = "Studio Project",
        .directories = {},
    };
    std::ofstream output(root / "project.json", std::ios::binary);
    output << fabric::project::serialize_manifest(manifest);
}

void write_valid_png(const std::filesystem::path& path) {
    constexpr std::array<std::uint8_t, 68> png{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00,
        0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xff, 0x1f, 0x00,
        0x02, 0xeb, 0x01, 0xf5, 0x69, 0x76, 0x9d, 0x7b, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
}

void write_valid_svg(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    output << R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="2" viewBox="0 0 4 2"><path d="M0 0h4v2H0z" fill="#d9a441"/></svg>)";
}

void session_opens_a_valid_project() {
    const TemporaryDirectory valid{"valid"};
    std::cerr << "[ fixture  ] writing project\n" << std::flush;
    write_valid_project(valid.path());

    fabric::editor::ProjectSession session;
    std::cerr << "[ fixture  ] opening project\n" << std::flush;
    require(session.open(valid.path()), "valid project did not open");
    std::cerr << "[ fixture  ] project opened\n" << std::flush;
    require(session.has_project(), "session did not retain the project");
    require(session.manifest()->name == "Studio Project",
            "session retained the wrong manifest");
    require(session.errors().empty(), "successful open retained errors");
    std::cerr << "[ fixture  ] assertions complete\n" << std::flush;
}

void session_creates_and_opens_a_project() {
    const TemporaryDirectory parent{"create"};
    const auto project_root = parent.path() / "new-project";
    const fabric::project::ProjectManifest manifest{
        .schema_version = fabric::project::current_schema_version,
        .id = {.value = "new-project"},
        .name = "New Project",
        .directories = {},
    };

    fabric::editor::ProjectSession session;
    require(session.create(project_root, manifest),
            "session could not create a project");
    require(session.has_project(), "created project is not active");
    require(session.project_root() == project_root,
            "created project has the wrong active path");
    require(session.manifest()->id.value == "new-project",
            "created project has the wrong active manifest");
}

void failed_open_preserves_the_active_project() {
    const TemporaryDirectory valid{"valid"};
    const TemporaryDirectory invalid{"invalid"};
    write_valid_project(valid.path());

    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "valid project did not open");
    require(!session.open(invalid.path()), "invalid project opened");
    require(session.has_project(), "failed open cleared the active project");
    require(session.project_root() == valid.path(),
            "failed open replaced the active project path");
    require(!session.errors().empty(), "failed open produced no diagnostics");
}

void failed_creation_preserves_the_active_project() {
    const TemporaryDirectory valid{"valid-before-create"};
    const TemporaryDirectory occupied{"occupied"};
    write_valid_project(valid.path());
    {
        std::ofstream sentinel(occupied.path() / "keep.txt", std::ios::binary);
        sentinel << "keep";
    }
    const fabric::project::ProjectManifest requested{
        .schema_version = fabric::project::current_schema_version,
        .id = {.value = "replacement"},
        .name = "Replacement",
        .directories = {},
    };

    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "initial project did not open");
    require(!session.create(occupied.path(), requested),
            "session created a project in an occupied destination");
    require(session.project_root() == valid.path(),
            "failed creation replaced the active project");
    require(session.manifest()->id.value == "studio-project",
            "failed creation replaced the active manifest");
}

void session_imports_a_valid_png_persistently() {
    const TemporaryDirectory valid{"png-import"};
    write_valid_project(valid.path());
    const auto source = valid.path() / "source.png";
    write_valid_png(source);

    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "project for import did not open");
    require(session.import_png(source, {.value = "wool-fill"}, "Wool Fill"),
            "valid PNG import failed");
    require(session.imported_texture().has_value(),
            "successful import retained no texture");
    require(session.imported_texture()->image.width == 1,
            "import retained the wrong decoded image");
    require(session.resources().size() == 1,
            "imported texture was not added to the resource index");
    require(session.selected_resource() != nullptr &&
                session.selected_resource()->id.value == "wool-fill",
            "imported texture was not selected");
    require(std::filesystem::is_regular_file(
                valid.path() / "assets/textures/wool-fill.png"),
            "import did not persist the PNG");
    require(std::filesystem::is_regular_file(
                valid.path() / "assets/textures/wool-fill.texture.json"),
            "import did not persist the texture document");
    require(fabric::project::validate_project(valid.path()).ok(),
            "project validator rejected an imported texture");

    fabric::editor::ProjectSession reopened;
    require(reopened.open(valid.path()),
            "project with imported texture could not be reopened");
    require(reopened.resources().size() == 1,
            "reopened project did not index its texture");
    require(reopened.select_resource(
                fabric::editor::StudioResourceKind::texture,
                {.value = "wool-fill"}),
            "indexed texture could not be selected after reopen");
    require(reopened.imported_texture()->image.width == 1,
            "selected texture was not decoded after reopen");
}

void failed_import_writes_no_asset_and_preserves_the_previous_import() {
    const TemporaryDirectory valid{"failed-png-import"};
    write_valid_project(valid.path());
    const auto source = valid.path() / "source.png";
    write_valid_png(source);

    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "project for failed import did not open");
    require(session.import_png(source, {.value = "first"}, "First"),
            "initial valid import failed");
    const auto corrupt = valid.path() / "corrupt.png";
    {
        std::ofstream output(corrupt, std::ios::binary);
        output << "not a png";
    }
    require(!session.import_png(corrupt, {.value = "broken"}, "Broken"),
            "corrupt PNG was imported");
    require(!std::filesystem::exists(
                valid.path() / "assets/textures/broken.texture.json"),
            "failed import published a texture document");
    require(session.imported_texture()->asset.document.id.value == "first",
            "failed import replaced the previous successful import");
    require(!session.import_png(source, {.value = "first"}, "Duplicate"),
            "duplicate texture identifier was accepted");
    require(session.imported_texture()->asset.document.name == "First",
            "duplicate import replaced the existing texture state");
}

void session_imports_a_valid_svg_persistently() {
    const TemporaryDirectory valid{"svg-import"};
    write_valid_project(valid.path());
    const auto source = valid.path() / "source.svg";
    write_valid_svg(source);

    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "project for SVG import did not open");
    require(session.import_svg(source, {.value = "thread-outline"},
                               "Thread Outline"),
            "valid SVG import failed");
    require(session.imported_vector().has_value(),
            "successful import retained no vector");
    require(session.imported_vector()->preview.width == 2048,
            "import retained the wrong SVG preview");
    require(session.resources().size() == 1,
            "imported vector was not added to the resource index");
    require(session.selected_resource() != nullptr &&
                session.selected_resource()->id.value == "thread-outline",
            "imported vector was not selected");
    require(std::filesystem::is_regular_file(
                valid.path() / "assets/vectors/thread-outline.svg"),
            "import did not persist the SVG");
    require(std::filesystem::is_regular_file(
                valid.path() / "assets/vectors/thread-outline.vector.json"),
            "import did not persist the vector document");
    require(fabric::project::validate_project(valid.path()).ok(),
            "project validator rejected an imported vector");
}

void session_converts_linked_svg_with_undo_and_save() {
    const TemporaryDirectory valid{"svg-conversion"};
    write_valid_project(valid.path());
    const auto source = valid.path() / "source.svg";
    write_valid_svg(source);

    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "project for SVG conversion did not open");
    require(session.import_svg(source, {.value = "native-panel"}, "Native Panel"),
            "SVG import for conversion failed");
    require(session.convert_selected_linked_svg_to_native(),
            "linked SVG conversion failed");
    require(session.created_vector().has_value() &&
                session.created_vector()->source_kind ==
                    fabric::project::VectorSourceKind::native &&
                session.created_vector()->native->nodes.size() == 1U &&
                session.selected_resource()->native,
            "conversion did not activate native authoring");
    require(session.undo(), "SVG conversion could not be undone");
    require(session.imported_vector().has_value() &&
                !session.created_vector().has_value() &&
                !session.selected_resource()->native,
            "undo did not restore the linked SVG state");
    require(session.save(), "undoing SVG conversion could not be saved cleanly");
    require(session.convert_selected_linked_svg_to_native(),
            "linked SVG could not be converted after undo");
    require(session.save(), "converted native SVG could not be saved");

    fabric::editor::ProjectSession reopened;
    require(reopened.open(valid.path()), "converted project could not reopen");
    require(reopened.select_resource(
                fabric::editor::StudioResourceKind::vector,
                {.value = "native-panel"}),
            "converted vector could not be selected after reopen");
    require(reopened.created_vector()->source_kind ==
                fabric::project::VectorSourceKind::native,
            "reopened conversion did not remain native");
}

void failed_svg_import_preserves_the_previous_vector() {
    const TemporaryDirectory valid{"failed-svg-import"};
    write_valid_project(valid.path());
    const auto source = valid.path() / "source.svg";
    write_valid_svg(source);

    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "project for failed SVG import did not open");
    require(session.import_svg(source, {.value = "first-vector"}, "First"),
            "initial valid SVG import failed");
    const auto corrupt = valid.path() / "corrupt.svg";
    {
        std::ofstream output(corrupt, std::ios::binary);
        output << "not an svg";
    }
    require(!session.import_svg(corrupt, {.value = "broken-vector"}, "Broken"),
            "corrupt SVG was imported");
    require(!std::filesystem::exists(
                valid.path() / "assets/vectors/broken-vector.vector.json"),
            "failed SVG import published a vector document");
    require(session.imported_vector()->asset.document.id.value == "first-vector",
            "failed SVG import replaced the previous successful import");
    require(!session.import_svg(source, {.value = "first-vector"}, "Duplicate"),
            "duplicate vector identifier was accepted");
    require(session.imported_vector()->asset.document.name == "First",
            "duplicate SVG import replaced the existing vector state");
}

void session_creates_and_reopens_input_bindings() {
    const TemporaryDirectory valid{"input-create"};
    write_valid_project(valid.path());
    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "project for input creation did not open");
    fabric::editor::CreateInputPrompt prompt;
    prompt.name = "Game controls";
    prompt.actions = {
        {"move_left", {{fabric::project::InputDevice::keyboard, 74}}},
        {"move_right", {{fabric::project::InputDevice::keyboard, 76}}},
        {"jump", {{fabric::project::InputDevice::gamepad, 1}}}};
    require(session.create_input(prompt), "input document creation failed");
    require(session.selected_input().has_value(), "created input was not selected");
    require(std::filesystem::is_regular_file(
                valid.path() / "assets/input/game-controls.input.json"),
            "input document was not persisted");
    require(session.resources().size() == 1,
            "created input was not added to the resource index");
    require(session.set_selected_input_binding(
                0, 0, {fabric::project::InputDevice::keyboard, 81}),
            "input binding edit failed");
    require(session.selected_input()->actions[0].bindings[0].code == 81,
            "input binding edit was not applied");
    require(session.undo(), "input binding undo failed");
    require(session.selected_input()->actions[0].bindings[0].code == 74,
            "input binding undo did not restore the value");
    require(session.redo(), "input binding redo failed");
    require(session.add_selected_input_binding(
                0, {fabric::project::InputDevice::keyboard, 82}),
            "input binding add failed");
    require(session.selected_input()->actions[0].bindings.size() == 2,
            "input binding add was not applied");
    require(session.remove_selected_input_binding(0, 1),
            "input binding remove failed");
    require(session.selected_input()->actions[0].bindings.size() == 1,
            "input binding remove was not applied");
    require(session.add_selected_input_action(
                {"menu", {{fabric::project::InputDevice::keyboard, 77}}}),
            "input action add failed");
    require(session.selected_input()->actions.size() == 4,
            "input action add was not applied");
    require(session.remove_selected_input_action(3),
            "input action remove failed");
    require(session.selected_input()->actions.size() == 3,
            "input action remove was not applied");
    require(session.undo(), "input action remove undo failed");
    require(session.selected_input()->actions.size() == 4,
            "input action remove undo did not restore the action");
    require(session.redo(), "input action remove redo failed");
    require(session.save(), "edited input binding was not saved");
    fabric::editor::ProjectSession reopened;
    require(reopened.open(valid.path()), "project with input could not reopen");
    require(reopened.select_resource(fabric::editor::StudioResourceKind::input,
                                     {.value = "game-controls"}),
            "input resource could not be selected after reopen");
    require(reopened.selected_input()->actions.size() == 3,
            "reopened input lost actions");
    require(reopened.selected_input()->actions[0].bindings[0].code == 81,
            "reopened input lost its edited binding");
}

} // namespace

int main() {
    std::cerr << "[ RUN      ] session_opens_a_valid_project\n" << std::flush;
    session_opens_a_valid_project();
    std::cerr << "[       OK ] session_opens_a_valid_project\n" << std::flush;
    std::cerr << "[ RUN      ] session_creates_and_opens_a_project\n" << std::flush;
    session_creates_and_opens_a_project();
    std::cerr << "[       OK ] session_creates_and_opens_a_project\n" << std::flush;
    std::cerr << "[ RUN      ] failed_open_preserves_the_active_project\n" << std::flush;
    failed_open_preserves_the_active_project();
    std::cerr << "[       OK ] failed_open_preserves_the_active_project\n" << std::flush;
    std::cerr << "[ RUN      ] failed_creation_preserves_the_active_project\n" << std::flush;
    failed_creation_preserves_the_active_project();
    std::cerr << "[       OK ] failed_creation_preserves_the_active_project\n" << std::flush;
    std::cerr << "[ RUN      ] session_imports_a_valid_png_persistently\n" << std::flush;
    session_imports_a_valid_png_persistently();
    std::cerr << "[       OK ] session_imports_a_valid_png_persistently\n" << std::flush;
    std::cerr << "[ RUN      ] failed_import_writes_no_asset_and_preserves_the_previous_import\n" << std::flush;
    failed_import_writes_no_asset_and_preserves_the_previous_import();
    std::cerr << "[       OK ] failed_import_writes_no_asset_and_preserves_the_previous_import\n" << std::flush;
    std::cerr << "[ RUN      ] session_imports_a_valid_svg_persistently\n" << std::flush;
    session_imports_a_valid_svg_persistently();
    std::cerr << "[       OK ] session_imports_a_valid_svg_persistently\n" << std::flush;
    std::cerr << "[ RUN      ] session_converts_linked_svg_with_undo_and_save\n" << std::flush;
    session_converts_linked_svg_with_undo_and_save();
    std::cerr << "[       OK ] session_converts_linked_svg_with_undo_and_save\n" << std::flush;
    std::cerr << "[ RUN      ] failed_svg_import_preserves_the_previous_vector\n" << std::flush;
    failed_svg_import_preserves_the_previous_vector();
    std::cerr << "[       OK ] failed_svg_import_preserves_the_previous_vector\n" << std::flush;
    std::cerr << "[ RUN      ] session_creates_and_reopens_input_bindings\n" << std::flush;
    session_creates_and_reopens_input_bindings();
    std::cerr << "[       OK ] session_creates_and_reopens_input_bindings\n" << std::flush;
    return 0;
}
