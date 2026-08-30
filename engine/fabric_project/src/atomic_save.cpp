#include "fabric/project/manifest.hpp"
#include "fabric/project/document_storage.hpp"

namespace fabric::project {

ValidationReport save_manifest_atomic(const std::filesystem::path& project_root,
                                      const ProjectManifest& manifest) {
    return save_document_atomic(
        project_root, "project.json", serialize_manifest(manifest),
        [&manifest](const std::string_view) {
            return validate_manifest(manifest);
        });
}

} // namespace fabric::project
