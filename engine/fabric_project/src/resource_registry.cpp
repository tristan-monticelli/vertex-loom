#include "fabric/project/resource_registry.hpp"

#include "asset_storage.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace fabric::project {
namespace {

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

bool entry_less(const ResourceEntry& left, const ResourceEntry& right) {
    if (left.document.id.value != right.document.id.value) {
        return left.document.id.value < right.document.id.value;
    }
    if (left.document.type != right.document.type) {
        return left.document.type < right.document.type;
    }
    return left.document_path.generic_string() <
        right.document_path.generic_string();
}

} // namespace

ValidationReport ResourceRegistry::register_resource(ResourceEntry entry) {
    ValidationReport report;
    if (entry.document.schema_version == 0) {
        add_error(report.errors, ErrorCode::invalid_asset, "schemaVersion",
                  "resource document version must be greater than zero");
    }
    if (entry.document.type.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "type",
                  "resource document type must not be empty");
    }
    if (!core::ResourceId::is_valid(entry.document.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "resource document identifier is invalid");
    }
    if (entry.document.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "name",
                  "resource document name must not be empty");
    }
    if (!detail::is_portable_relative_path(entry.document_path)) {
        add_error(report.errors, ErrorCode::invalid_path, "document",
                  "resource document path must be project-relative");
    }
    for (const auto& reference : entry.references) {
        if (!core::ResourceId::is_valid(reference.id.value)) {
            add_error(report.errors, ErrorCode::invalid_resource_id,
                      "references", "resource reference identifier is invalid");
        }
        if (reference.expected_type.empty()) {
            add_error(report.errors, ErrorCode::invalid_asset, "references",
                      "resource reference type must not be empty");
        }
    }
    if (report.ok()) {
        entries_.push_back(std::move(entry));
    }
    return report;
}

const ResourceEntry* ResourceRegistry::resolve(
    const ResourceReference& reference) const noexcept {
    const ResourceEntry* match = nullptr;
    for (const auto& entry : entries_) {
        if (entry.document.id != reference.id) {
            continue;
        }
        if (match != nullptr ||
            entry.document.type != reference.expected_type) {
            return nullptr;
        }
        match = &entry;
    }
    return match;
}

ValidationReport ResourceRegistry::validate() const {
    ValidationReport report;
    std::vector<std::size_t> ordered(entries_.size());
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        ordered[index] = index;
    }
    std::ranges::sort(ordered, [this](const std::size_t left,
                                      const std::size_t right) {
        return entry_less(entries_[left], entries_[right]);
    });

    std::map<std::string, std::vector<std::size_t>> by_identifier;
    for (const std::size_t index : ordered) {
        by_identifier[entries_[index].document.id.value].push_back(index);
    }

    std::map<std::string, std::size_t> unique_entries;
    for (const auto& [identifier, indices] : by_identifier) {
        if (indices.size() != 1) {
            add_error(report.errors, ErrorCode::duplicate_resource, "id",
                      "resource identifier is declared more than once: " +
                          identifier);
            continue;
        }
        unique_entries.emplace(identifier, indices.front());
    }

    std::vector<std::vector<std::size_t>> adjacency(entries_.size());
    for (const std::size_t source_index : ordered) {
        auto references = entries_[source_index].references;
        std::ranges::sort(references, [](const ResourceReference& left,
                                         const ResourceReference& right) {
            if (left.id.value != right.id.value) {
                return left.id.value < right.id.value;
            }
            return left.expected_type < right.expected_type;
        });
        for (const auto& reference : references) {
            const auto target = unique_entries.find(reference.id.value);
            if (target == unique_entries.end()) {
                if (!by_identifier.contains(reference.id.value)) {
                    add_error(report.errors, ErrorCode::missing_resource,
                              entries_[source_index].document.id.value,
                              "referenced resource is missing: " +
                                  reference.id.value);
                }
                continue;
            }
            const auto& target_entry = entries_[target->second];
            if (target_entry.document.type != reference.expected_type) {
                add_error(report.errors, ErrorCode::resource_type_mismatch,
                          entries_[source_index].document.id.value,
                          "referenced resource has type " +
                              target_entry.document.type + ", expected " +
                              reference.expected_type + ": " +
                              reference.id.value);
                continue;
            }
            adjacency[source_index].push_back(target->second);
        }
        std::ranges::sort(adjacency[source_index],
                          [this](const std::size_t left,
                                 const std::size_t right) {
            return entries_[left].document.id.value <
                entries_[right].document.id.value;
        });
    }

    std::vector<unsigned char> state(entries_.size(), 0);
    std::set<std::string> reported_cycles;
    for (const std::size_t start : ordered) {
        if (state[start] != 0 ||
            !unique_entries.contains(entries_[start].document.id.value)) {
            continue;
        }
        std::vector<std::pair<std::size_t, std::size_t>> stack;
        state[start] = 1;
        stack.emplace_back(start, 0);
        while (!stack.empty()) {
            auto& [current, edge_index] = stack.back();
            if (edge_index == adjacency[current].size()) {
                state[current] = 2;
                stack.pop_back();
                continue;
            }
            const std::size_t target = adjacency[current][edge_index++];
            if (state[target] == 0) {
                state[target] = 1;
                stack.emplace_back(target, 0);
                continue;
            }
            if (state[target] == 1 &&
                reported_cycles.insert(entries_[target].document.id.value)
                    .second) {
                add_error(report.errors, ErrorCode::resource_cycle,
                          entries_[target].document.id.value,
                          "resource dependency cycle detected");
            }
        }
    }
    return report;
}

const std::vector<ResourceEntry>& ResourceRegistry::entries() const noexcept {
    return entries_;
}

} // namespace fabric::project
