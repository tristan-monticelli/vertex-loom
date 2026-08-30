#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <filesystem>
#include <vector>

namespace fabric::project {

struct ResourceEntry {
    DocumentHeader document;
    std::filesystem::path document_path;
    std::vector<ResourceReference> references;

    friend bool operator==(const ResourceEntry&, const ResourceEntry&) = default;
};

class ResourceRegistry {
public:
    [[nodiscard]] ValidationReport register_resource(ResourceEntry entry);
    [[nodiscard]] const ResourceEntry* resolve(
        const ResourceReference& reference) const noexcept;
    [[nodiscard]] ValidationReport validate() const;
    [[nodiscard]] const std::vector<ResourceEntry>& entries() const noexcept;

private:
    std::vector<ResourceEntry> entries_;
};

} // namespace fabric::project
