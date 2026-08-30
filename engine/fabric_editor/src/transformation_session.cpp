#include "fabric/editor/transformation_session.hpp"

#include "fabric/project/document_storage.hpp"
#include "fabric/project/entity.hpp"

#include <memory>
#include <utility>

namespace fabric::editor {
namespace {
class SnapshotCommand final : public Command {
public:
    SnapshotCommand(project::EntityTransformation& target,
                    project::EntityTransformation before,
                    project::EntityTransformation after)
        : target_(target), before_(std::move(before)), after_(std::move(after)) {}
    bool execute() override { target_ = after_; return true; }
    bool undo() override { target_ = before_; return true; }
private:
    project::EntityTransformation& target_;
    project::EntityTransformation before_;
    project::EntityTransformation after_;
};

project::ValidationReport validate_with_entities(
    const std::filesystem::path& root,
    const project::ProjectManifest& manifest,
    const project::EntityTransformation& value) {
    auto report = project::validate_entity_transformation(manifest, value);
    for (const auto& [field, reference] : {
             std::pair{"sourceEntity", value.source_entity},
             std::pair{"destinationEntity", value.destination_entity}}) {
        if (!project::load_entity(root, manifest,
                project::entity_document_path(manifest, reference.id)).ok())
            report.errors.push_back({project::ErrorCode::missing_resource, field,
                                     "referenced entity is missing or invalid"});
    }
    return report;
}
} // namespace

project::ValidationReport TransformationSession::validate(
    const project::EntityTransformation& value) const {
    if (!manifest_) return {{project::Error{project::ErrorCode::invalid_manifest,
                                             "project", "no project is open"}}};
    return validate_with_entities(root_, *manifest_, value);
}

bool TransformationSession::create(
    const std::filesystem::path& root,
    const project::EntityTransformation& value) {
    auto manifest = project::load_manifest(root);
    if (!manifest.ok()) { errors_ = std::move(manifest.errors); return false; }
    if (dirty() && !save()) return false;
    const auto validation = validate_with_entities(
        root, *manifest.manifest, value);
    if (!validation.ok()) { errors_ = validation.errors; return false; }
    root_ = root;
    manifest_ = std::move(*manifest.manifest);
    path_ = project::entity_transformation_document_path(*manifest_,
                                                          value.document.id);
    std::error_code filesystem_error;
    if (std::filesystem::exists(root_ / path_, filesystem_error) || filesystem_error) {
        errors_ = {{project::ErrorCode::asset_already_exists, "document",
                    "transformation already exists or cannot be inspected"}};
        return false;
    }
    transformation_ = value;
    recovery_.reset(); commands_.clear(); autosave_.reset(); errors_.clear();
    return save();
}

bool TransformationSession::open(const std::filesystem::path& root,
                                 const core::ResourceId& id) {
    auto manifest = project::load_manifest(root);
    if (!manifest.ok()) { errors_ = std::move(manifest.errors); return false; }
    const auto path = project::entity_transformation_document_path(
        *manifest.manifest, id);
    auto loaded = project::load_entity_transformation(root, *manifest.manifest, path);
    if (!loaded.ok()) { errors_ = std::move(loaded.errors); return false; }
    if (dirty() && !save()) return false;
    root_ = root; path_ = path; manifest_ = std::move(*manifest.manifest);
    transformation_ = std::move(*loaded.asset); recovery_.reset();
    commands_.clear(); autosave_.reset(); errors_.clear();
    const auto candidate = project::inspect_recovery(
        root_, path_, [this](std::string_view contents) {
            const auto parsed = project::parse_entity_transformation(
                *manifest_, contents);
            return project::ValidationReport{.errors = parsed.errors};
        });
    if (candidate.candidate) {
        auto parsed = project::parse_entity_transformation(
            *manifest_, candidate.candidate->contents);
        if (parsed.ok()) recovery_ = std::move(*parsed.asset);
    } else if (!candidate.errors.empty()) errors_ = candidate.errors;
    return true;
}

bool TransformationSession::save() {
    if (!transformation_ || !manifest_) return false;
    const auto validation = validate(*transformation_);
    if (!validation.ok()) { errors_ = validation.errors; return false; }
    auto saved = project::publish_entity_transformation(
        root_, *manifest_, *transformation_);
    if (!saved.ok()) { errors_ = std::move(saved.errors); return false; }
    commands_.mark_clean(); autosave_.reset(); errors_.clear(); return true;
}

AutosaveStatus TransformationSession::update_autosave(
    const AutosaveScheduler::Clock::time_point now) {
    if (!transformation_ || !manifest_) return AutosaveStatus::not_due;
    if (dirty() && !autosave_.pending()) autosave_.mark_changed(now);
    if (!autosave_.due(now)) return AutosaveStatus::not_due;
    const auto validation = validate(*transformation_);
    if (!validation.ok()) { errors_ = validation.errors; return AutosaveStatus::failed; }
    const auto report = project::save_autosave_atomic(
        root_, path_, project::serialize_entity_transformation(*transformation_),
        [this](std::string_view contents) {
            const auto parsed = project::parse_entity_transformation(*manifest_, contents);
            return project::ValidationReport{.errors = parsed.errors};
        });
    if (!report.ok()) { errors_ = report.errors; return AutosaveStatus::failed; }
    autosave_.mark_saved(); errors_.clear(); return AutosaveStatus::saved;
}

bool TransformationSession::accept_recovery(
    const AutosaveScheduler::Clock::time_point now) {
    if (!recovery_) return false;
    transformation_ = std::move(recovery_); recovery_.reset(); commands_.clear();
    commands_.mark_dirty(); autosave_.mark_changed(now); errors_.clear(); return true;
}

void TransformationSession::decline_recovery() noexcept {
    recovery_.reset(); errors_.clear();
}

bool TransformationSession::commit(project::EntityTransformation next) {
    if (!transformation_) return false;
    const auto validation = validate(next);
    if (!validation.ok()) { errors_ = validation.errors; return false; }
    if (!commands_.execute(std::make_unique<SnapshotCommand>(
            *transformation_, *transformation_, std::move(next)))) return false;
    errors_.clear(); return true;
}

bool TransformationSession::set_source(core::ResourceId id) {
    if (!transformation_) return false;
    auto next = *transformation_;
    next.source_entity = {std::move(id), "entity"};
    return commit(std::move(next));
}

bool TransformationSession::set_destination(core::ResourceId id) {
    if (!transformation_) return false;
    auto next = *transformation_;
    next.destination_entity = {std::move(id), "entity"};
    return commit(std::move(next));
}

bool TransformationSession::set_policy(project::EntityTransferPolicy policy) {
    if (!transformation_) return false;
    auto next = *transformation_; next.policy = std::move(policy);
    return commit(std::move(next));
}

bool TransformationSession::undo() { return commands_.undo(); }
bool TransformationSession::redo() { return commands_.redo(); }

} // namespace fabric::editor
