#include "fabric/project/entity.hpp"

#include "fabric/project/document_storage.hpp"
#include "asset_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace fabric::project {
namespace {
using Json = nlohmann::json;
void error(std::vector<Error>& e, ErrorCode c, std::string f, std::string m) { e.push_back({c, std::move(f), std::move(m)}); }
Json vec(core::Vec2 v) { return {{"x", v.x}, {"y", v.y}}; }
Json transform(core::Transform t) { return {{"position", vec(t.position)}, {"rotationDegrees", t.rotation_degrees}, {"scale", vec(t.scale)}, {"pivot", vec(t.pivot)}}; }
bool text(const Json& o, const char* k, std::string& v, std::vector<Error>& e) { auto i=o.find(k); if(i==o.end()||!i->is_string()){error(e,ErrorCode::invalid_asset,k,"expected a string");return false;}v=i->get<std::string>();return true; }
bool number(const Json& o, const char* k, float& v, std::vector<Error>& e) { auto i=o.find(k); if(i==o.end()||(!i->is_number_float()&&!i->is_number_integer())){error(e,ErrorCode::invalid_asset,k,"expected a finite number");return false;}v=i->get<float>();if(!std::isfinite(v))error(e,ErrorCode::invalid_asset,k,"must be finite");return true; }
bool boolean(const Json& o, const char* k, bool& v, std::vector<Error>& e) { auto i=o.find(k); if(i==o.end()||!i->is_boolean()){error(e,ErrorCode::invalid_asset,k,"expected a boolean");return false;}v=i->get<bool>();return true; }
bool vec_read(const Json& o,const char* k,core::Vec2& v,std::vector<Error>& e){auto i=o.find(k);if(i==o.end()||!i->is_object()){error(e,ErrorCode::invalid_asset,k,"expected a Vec2");return false;}auto x=number(*i,"x",v.x,e);auto y=number(*i,"y",v.y,e);return x&&y;}
bool transform_read(const Json& o,const char* k,core::Transform& t,std::vector<Error>& e){auto i=o.find(k);if(i==o.end()||!i->is_object()){error(e,ErrorCode::invalid_asset,k,"expected a transform");return false;}return vec_read(*i,"position",t.position,e)&&number(*i,"rotationDegrees",t.rotation_degrees,e)&&vec_read(*i,"scale",t.scale,e)&&vec_read(*i,"pivot",t.pivot,e);}
Json ref_json(const ResourceReference& r){return {{"id",r.id.value},{"expectedType",r.expected_type}};}
bool ref_read(const Json& o,const char* k,std::optional<ResourceReference>& out,std::vector<Error>& e){auto i=o.find(k);if(i==o.end()||i->is_null())return true;if(!i->is_object()){error(e,ErrorCode::invalid_asset,k,"expected a resource reference");return false;}ResourceReference r;if(!text(*i,"id",r.id.value,e)||!text(*i,"expectedType",r.expected_type,e))return false;out=std::move(r);return true;}
Json constraint_json(const AnimationConstraint& c){return {{"id",c.id},{"kind",c.kind==AnimationConstraintKind::copy_transform?"copyTransform":c.kind==AnimationConstraintKind::limits?"limits":"lookAt"},{"targetNode",c.target_node},{"sourceNode",c.source_node},{"order",c.order},{"constrainPosition",c.constrain_position},{"constrainRotation",c.constrain_rotation},{"constrainScale",c.constrain_scale}};}
bool constraint_read(const Json& o, AnimationConstraint& c, std::vector<Error>& e){text(o,"id",c.id,e);std::string kind;text(o,"kind",kind,e);if(kind=="copyTransform")c.kind=AnimationConstraintKind::copy_transform;else if(kind=="limits")c.kind=AnimationConstraintKind::limits;else if(kind=="lookAt")c.kind=AnimationConstraintKind::look_at;else error(e,ErrorCode::invalid_asset,"constraints.kind","unsupported constraint kind");text(o,"targetNode",c.target_node,e);text(o,"sourceNode",c.source_node,e);std::int64_t order{};auto i=o.find("order");if(i==o.end()||!i->is_number_integer())error(e,ErrorCode::invalid_asset,"constraints.order","expected an integer");else{order=i->get<std::int64_t>();c.order=static_cast<int>(order);}boolean(o,"constrainPosition",c.constrain_position,e);boolean(o,"constrainRotation",c.constrain_rotation,e);boolean(o,"constrainScale",c.constrain_scale,e);return e.empty();}
ValidationReport parse_validation(const ProjectManifest& m,std::string_view s){auto r=parse_entity(m,s);return {.errors=std::move(r.errors)};}
}
std::string_view to_string(const EntityDrawableKind k) noexcept { switch(k){case EntityDrawableKind::none:return "none";case EntityDrawableKind::vector:return "vector";case EntityDrawableKind::texture:return "texture";}return "none"; }
std::filesystem::path entity_document_path(const ProjectManifest& m,const core::ResourceId& id){return m.directories.entities/(id.value+".entity.json");}
ValidationReport validate_entity(const ProjectManifest&, const EntityDefinition& a) {
    ValidationReport r;
    if (a.document.schema_version != current_entity_schema_version)
        error(r.errors, ErrorCode::unsupported_schema_version, "schemaVersion",
              "only entity schema version 1 is supported");
    if (a.document.type != "entity")
        error(r.errors, ErrorCode::invalid_asset, "type", "must be entity");
    if (!core::ResourceId::is_valid(a.document.id.value))
        error(r.errors, ErrorCode::invalid_resource_id, "id", "must be valid");
    if (a.document.name.empty())
        error(r.errors, ErrorCode::invalid_asset, "name", "must not be empty");
    std::set<std::string> ids;
    for (std::size_t index = 0; index < a.nodes.size(); ++index) {
        const auto& node = a.nodes[index];
        const auto prefix = "nodes[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(node.id) || !ids.insert(node.id).second)
            error(r.errors, ErrorCode::invalid_resource_id, prefix + ".id",
                  "node id must be valid and unique");
        if (node.name.empty())
            error(r.errors, ErrorCode::invalid_asset, prefix + ".name",
                  "must not be empty");
        if (node.parent && (!core::ResourceId::is_valid(*node.parent) ||
                            *node.parent == node.id))
            error(r.errors, ErrorCode::invalid_resource_id, prefix + ".parent",
                  "parent must reference another valid node");
        const auto finite = std::isfinite(node.z_order) &&
            std::isfinite(node.transform.rotation_degrees) &&
            std::isfinite(node.transform.position.x) &&
            std::isfinite(node.transform.position.y) &&
            std::isfinite(node.transform.scale.x) &&
            std::isfinite(node.transform.scale.y) &&
            std::isfinite(node.transform.pivot.x) &&
            std::isfinite(node.transform.pivot.y);
        if (!finite)
            error(r.errors, ErrorCode::invalid_asset, prefix + ".transform",
                  "transform and zOrder must be finite");
        const auto valid_ref = [&](const auto& reference, const char* field,
                                   const char* expected) {
            if (reference &&
                (!core::ResourceId::is_valid(reference->id.value) ||
                 reference->expected_type != expected))
                error(r.errors, ErrorCode::resource_type_mismatch,
                      prefix + "." + field, "reference type is invalid");
        };
        if (node.drawable.kind == EntityDrawableKind::vector) {
            if (!node.drawable.resource)
                error(r.errors, ErrorCode::missing_resource, prefix + ".drawable",
                      "vector drawable requires a resource");
            else valid_ref(node.drawable.resource, "resource", "vector");
        } else if (node.drawable.kind == EntityDrawableKind::texture) {
            if (!node.drawable.resource)
                error(r.errors, ErrorCode::missing_resource, prefix + ".drawable",
                      "texture drawable requires a resource");
            else valid_ref(node.drawable.resource, "resource", "texture");
        } else if (node.drawable.resource) {
            error(r.errors, ErrorCode::invalid_asset, prefix + ".drawable",
                  "none drawable cannot reference a resource");
        }
        valid_ref(node.drawable.material, "material", "material");
    }
    for (const auto& node : a.nodes) {
        if (!node.parent) continue;
        const auto parent = std::find_if(a.nodes.begin(), a.nodes.end(),
            [&](const auto& candidate) { return candidate.id == *node.parent; });
        if (parent == a.nodes.end()) {
            error(r.errors, ErrorCode::missing_resource, "nodes.parent",
                  "parent node is missing");
            continue;
        }
        std::set<std::string> seen;
        const EntityNode* current = &node;
        while (current->parent) {
            if (!seen.insert(current->id).second) {
                error(r.errors, ErrorCode::resource_cycle, "nodes.parent",
                      "parent cycle detected");
                break;
            }
            const auto next = std::find_if(a.nodes.begin(), a.nodes.end(),
                [&](const auto& candidate) { return candidate.id == *current->parent; });
            if (next == a.nodes.end()) break;
            current = &*next;
        }
    }
    const auto constraints = validate_animation_constraints(a.constraints);
    r.errors.insert(r.errors.end(), constraints.errors.begin(), constraints.errors.end());
    for (const auto& constraint : a.constraints) {
        const auto source = std::find_if(a.nodes.begin(), a.nodes.end(),
            [&](const auto& node) { return node.id == constraint.source_node; });
        const auto target = std::find_if(a.nodes.begin(), a.nodes.end(),
            [&](const auto& node) { return node.id == constraint.target_node; });
        if (source == a.nodes.end() || target == a.nodes.end())
            error(r.errors, ErrorCode::missing_resource, "constraints.nodes",
                  "constraint source and target nodes must exist");
    }
    return r;
}
std::vector<ResourceReference> entity_resource_references(const EntityDefinition& a){std::vector<ResourceReference> r;for(const auto& n:a.nodes){if(n.drawable.resource)r.push_back(*n.drawable.resource);if(n.drawable.material)r.push_back(*n.drawable.material);}return r;}
std::string serialize_entity(const EntityDefinition& a){Json j={{"schemaVersion",a.document.schema_version},{"type",a.document.type},{"id",a.document.id.value},{"name",a.document.name},{"nodes",Json::array()},{"constraints",Json::array()}};for(const auto& n:a.nodes){Json d={{"kind",std::string(to_string(n.drawable.kind))}};if(n.drawable.resource)d["resource"]=ref_json(*n.drawable.resource);if(n.drawable.material)d["material"]=ref_json(*n.drawable.material);j["nodes"].push_back({{"id",n.id},{"name",n.name},{"transform",transform(n.transform)},{"zOrder",n.z_order},{"drawable",d}});if(n.parent)j["nodes"].back()["parent"]=*n.parent;}for(const auto& c:a.constraints)j["constraints"].push_back(constraint_json(c));return j.dump(2)+"\n";}
EntityResult parse_entity(const ProjectManifest& m, std::string_view s) {
    EntityResult r;
    Json j;
    try { j = Json::parse(s); }
    catch (...) { error(r.errors, ErrorCode::invalid_json, "entity", "cannot parse entity JSON"); return r; }
    if (!j.is_object()) { error(r.errors, ErrorCode::invalid_asset, "entity", "top-level value must be an object"); return r; }
    EntityDefinition a;
    auto schema = j.find("schemaVersion");
    if (schema == j.end() || !schema->is_number_unsigned()) error(r.errors, ErrorCode::invalid_asset, "schemaVersion", "expected an unsigned integer");
    else a.document.schema_version = schema->get<std::uint32_t>();
    text(j, "type", a.document.type, r.errors);
    text(j, "id", a.document.id.value, r.errors);
    text(j, "name", a.document.name, r.errors);
    const auto ns = j.find("nodes");
    if (ns == j.end() || !ns->is_array()) error(r.errors, ErrorCode::invalid_asset, "nodes", "expected an array");
    else for (const auto& x : *ns) {
        if (!x.is_object()) { error(r.errors, ErrorCode::invalid_asset, "nodes", "expected objects"); continue; }
        EntityNode n;
        text(x, "id", n.id, r.errors); text(x, "name", n.name, r.errors);
        auto p = x.find("parent");
        if (p != x.end()) { if (!p->is_string()) error(r.errors, ErrorCode::invalid_asset, "parent", "expected a string"); else n.parent = p->get<std::string>(); }
        transform_read(x, "transform", n.transform, r.errors); number(x, "zOrder", n.z_order, r.errors);
        auto d = x.find("drawable");
        if (d == x.end() || !d->is_object()) error(r.errors, ErrorCode::invalid_asset, "drawable", "expected an object");
        else {
            std::string k; text(*d, "kind", k, r.errors);
            if (k == "none") n.drawable.kind = EntityDrawableKind::none;
            else if (k == "vector") n.drawable.kind = EntityDrawableKind::vector;
            else if (k == "texture") n.drawable.kind = EntityDrawableKind::texture;
            else error(r.errors, ErrorCode::invalid_asset, "drawable.kind", "unsupported drawable kind");
            ref_read(*d, "resource", n.drawable.resource, r.errors);
            ref_read(*d, "material", n.drawable.material, r.errors);
        }
        a.nodes.push_back(std::move(n));
    }
    const auto cs = j.find("constraints");
    if (cs != j.end()) {
        if (!cs->is_array()) error(r.errors, ErrorCode::invalid_asset, "constraints", "expected an array");
        else for (const auto& x : *cs) {
            AnimationConstraint constraint;
            if (x.is_object() && constraint_read(x, constraint, r.errors)) a.constraints.push_back(std::move(constraint));
            else if (!x.is_object()) error(r.errors, ErrorCode::invalid_asset, "constraints", "expected objects");
        }
    }
    if (!r.errors.empty()) return r;
    auto v = validate_entity(m, a);
    if (!v.ok()) { r.errors = std::move(v.errors); return r; }
    r.entity = std::move(a);
    return r;
}
EntityResult load_entity(const std::filesystem::path& root,const ProjectManifest& m,const std::filesystem::path& path){auto s=load_document(root,path,[&](std::string_view v){return parse_validation(m,v);});EntityResult r;r.errors=std::move(s.errors);if(s.contents)r=parse_entity(m,*s.contents);if(r.ok()&&path!=entity_document_path(m,r.entity->document.id)){r.entity.reset();error(r.errors,ErrorCode::invalid_path,"document","document filename does not match its id");}return r;}
EntityResult publish_entity(const std::filesystem::path& root,const ProjectManifest& m,const EntityDefinition& a){EntityResult r;auto v=validate_entity(m,a);if(!v.ok()){r.errors=std::move(v.errors);return r;}auto p=entity_document_path(m,a.document.id);auto s=save_document_atomic(root,p,serialize_entity(a),[&](std::string_view x){return parse_validation(m,x);});if(!s.ok()){r.errors=std::move(s.errors);return r;}return load_entity(root,m,p);}
}
