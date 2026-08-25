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
Json constraint_json(const AnimationConstraint& c){Json result={{"id",c.id},{"kind",c.kind==AnimationConstraintKind::copy_transform?"copyTransform":c.kind==AnimationConstraintKind::limits?"limits":"lookAt"},{"targetNode",c.target_node},{"sourceNode",c.source_node},{"order",c.order},{"constrainPosition",c.constrain_position},{"constrainRotation",c.constrain_rotation},{"constrainScale",c.constrain_scale}};if(c.min_position)result["minPosition"]=vec(*c.min_position);if(c.max_position)result["maxPosition"]=vec(*c.max_position);if(c.min_rotation_degrees)result["minRotationDegrees"]=*c.min_rotation_degrees;if(c.max_rotation_degrees)result["maxRotationDegrees"]=*c.max_rotation_degrees;if(c.min_scale)result["minScale"]=vec(*c.min_scale);if(c.max_scale)result["maxScale"]=vec(*c.max_scale);return result;}
bool optional_number(const Json& o,const char* key,std::optional<float>& value,std::vector<Error>& e){auto i=o.find(key);if(i==o.end()||i->is_null())return true;if(!i->is_number()){error(e,ErrorCode::invalid_asset,key,"expected a finite number");return false;}float parsed=i->get<float>();if(!std::isfinite(parsed)){error(e,ErrorCode::invalid_asset,key,"must be finite");return false;}value=parsed;return true;}
bool optional_vec(const Json& o,const char* key,std::optional<core::Vec2>& value,std::vector<Error>& e){auto i=o.find(key);if(i==o.end()||i->is_null())return true;if(!i->is_object()){error(e,ErrorCode::invalid_asset,key,"expected a Vec2");return false;}core::Vec2 parsed{};if(!vec_read(o,key,parsed,e))return false;value=parsed;return true;}
bool constraint_read(const Json& o, AnimationConstraint& c, std::vector<Error>& e){text(o,"id",c.id,e);std::string kind;text(o,"kind",kind,e);if(kind=="copyTransform")c.kind=AnimationConstraintKind::copy_transform;else if(kind=="limits")c.kind=AnimationConstraintKind::limits;else if(kind=="lookAt")c.kind=AnimationConstraintKind::look_at;else error(e,ErrorCode::invalid_asset,"constraints.kind","unsupported constraint kind");text(o,"targetNode",c.target_node,e);text(o,"sourceNode",c.source_node,e);std::int64_t order{};auto i=o.find("order");if(i==o.end()||!i->is_number_integer())error(e,ErrorCode::invalid_asset,"constraints.order","expected an integer");else{order=i->get<std::int64_t>();c.order=static_cast<int>(order);}boolean(o,"constrainPosition",c.constrain_position,e);boolean(o,"constrainRotation",c.constrain_rotation,e);boolean(o,"constrainScale",c.constrain_scale,e);optional_vec(o,"minPosition",c.min_position,e);optional_vec(o,"maxPosition",c.max_position,e);optional_number(o,"minRotationDegrees",c.min_rotation_degrees,e);optional_number(o,"maxRotationDegrees",c.max_rotation_degrees,e);optional_vec(o,"minScale",c.min_scale,e);optional_vec(o,"maxScale",c.max_scale,e);return e.empty();}
Json ik_chain_json(const FabrikChainDefinition& chain){return {{"id",chain.id},{"joints",chain.joints},{"targetNode",chain.target_node},{"maxIterations",chain.max_iterations},{"tolerance",chain.tolerance}};}
bool ik_chain_read(const Json& o,FabrikChainDefinition& chain,std::vector<Error>& e){text(o,"id",chain.id,e);auto joints=o.find("joints");if(joints==o.end()||!joints->is_array()){error(e,ErrorCode::invalid_asset,"ikChains.joints","expected an array");}else for(const auto& value:*joints){if(!value.is_string())error(e,ErrorCode::invalid_asset,"ikChains.joints","expected strings");else chain.joints.push_back(value.get<std::string>());}text(o,"targetNode",chain.target_node,e);auto iterations=o.find("maxIterations");if(iterations==o.end()||!iterations->is_number_unsigned())error(e,ErrorCode::invalid_asset,"ikChains.maxIterations","expected an unsigned integer");else chain.max_iterations=iterations->get<std::size_t>();number(o,"tolerance",chain.tolerance,e);return e.empty();}
Json deformation_mesh_json(const DeformationMesh& mesh){Json result={{"vertices",Json::array()},{"triangles",Json::array()}};for(const auto& vertex:mesh.vertices){Json influences=Json::array();for(const auto& influence:vertex.influences)influences.push_back({{"node",influence.node_id},{"weight",influence.weight}});result["vertices"].push_back({{"restPosition",vec(vertex.rest_position)},{"influences",std::move(influences)}});}for(const auto& triangle:mesh.triangles)result["triangles"].push_back({{"first",triangle.first},{"second",triangle.second},{"third",triangle.third}});return result;}
bool deformation_mesh_read(const Json& o, DeformationMesh& mesh, std::vector<Error>& e){const auto vertices=o.find("vertices");if(vertices==o.end()||!vertices->is_array())error(e,ErrorCode::invalid_asset,"deformationMesh.vertices","expected an array");else for(const auto& item:*vertices){if(!item.is_object()){error(e,ErrorCode::invalid_asset,"deformationMesh.vertices","expected objects");continue;}MeshVertex vertex;vec_read(item,"restPosition",vertex.rest_position,e);const auto influences=item.find("influences");if(influences==item.end()||!influences->is_array())error(e,ErrorCode::invalid_asset,"deformationMesh.influences","expected an array");else for(const auto& value:*influences){if(!value.is_object()){error(e,ErrorCode::invalid_asset,"deformationMesh.influences","expected objects");continue;}MeshInfluence influence;text(value,"node",influence.node_id,e);number(value,"weight",influence.weight,e);vertex.influences.push_back(std::move(influence));}mesh.vertices.push_back(std::move(vertex));}const auto triangles=o.find("triangles");if(triangles==o.end()||!triangles->is_array())error(e,ErrorCode::invalid_asset,"deformationMesh.triangles","expected an array");else for(const auto& item:*triangles){if(!item.is_object()){error(e,ErrorCode::invalid_asset,"deformationMesh.triangles","expected objects");continue;}MeshTriangle triangle;const auto read_index=[&](const char* key,std::size_t& output){auto value=item.find(key);if(value==item.end()||!value->is_number_unsigned()){error(e,ErrorCode::invalid_asset,std::string("deformationMesh.triangles.")+key,"expected an unsigned integer");return;}output=value->get<std::size_t>();};read_index("first",triangle.first);read_index("second",triangle.second);read_index("third",triangle.third);mesh.triangles.push_back(triangle);}return e.empty();}
Json xpbd_json(const XpbdSystem& system){Json result={{"particles",Json::array()},{"distanceConstraints",Json::array()},{"pinConstraints",Json::array()},{"bendingConstraints",Json::array()},{"areaConstraints",Json::array()},{"collisionConstraints",Json::array()}};for(const auto& p:system.particles)result["particles"].push_back({{"position",vec(p.position)},{"inverseMass",p.inverse_mass}});for(const auto& c:system.distance_constraints)result["distanceConstraints"].push_back({{"first",c.first},{"second",c.second},{"restLength",c.rest_length},{"compliance",c.compliance},{"lambda",c.lambda}});for(const auto& c:system.pin_constraints)result["pinConstraints"].push_back({{"particle",c.particle},{"target",vec(c.target)},{"compliance",c.compliance},{"lambda",vec(c.lambda)}});for(const auto& c:system.bending_constraints)result["bendingConstraints"].push_back({{"first",c.first},{"middle",c.middle},{"third",c.third},{"restLength",c.rest_length},{"compliance",c.compliance},{"lambda",c.lambda}});for(const auto& c:system.area_constraints)result["areaConstraints"].push_back({{"first",c.first},{"second",c.second},{"third",c.third},{"restArea",c.rest_area},{"compliance",c.compliance},{"lambda",c.lambda}});for(const auto& c:system.collision_constraints)result["collisionConstraints"].push_back({{"particle",c.particle},{"normal",vec(c.normal)},{"offset",c.offset},{"compliance",c.compliance},{"lambda",c.lambda}});return result;}
bool xpbd_read(const Json& o, XpbdSystem& system, std::vector<Error>& e){
    const auto read_index=[](const Json& item,const char* key,std::size_t& output,std::vector<Error>& errors,const char* field){auto value=item.find(key);if(value==item.end()||!value->is_number_unsigned()){error(errors,ErrorCode::invalid_asset,field,"expected an unsigned integer");return false;}output=value->get<std::size_t>();return true;};
    const auto read_array=[&](const char* key){const auto value=o.find(key);if(value==o.end()||!value->is_array()){error(e,ErrorCode::invalid_asset,std::string("xpbd.")+key,"expected an array");return static_cast<const Json*>(nullptr);}return &*value;};
    if(const auto* values=read_array("particles"))for(const auto& item:*values){if(!item.is_object()){error(e,ErrorCode::invalid_asset,"xpbd.particles","expected objects");continue;}XpbdParticle p;vec_read(item,"position",p.position,e);number(item,"inverseMass",p.inverse_mass,e);system.particles.push_back(p);}
    if(const auto* values=read_array("distanceConstraints"))for(const auto& item:*values){if(!item.is_object()){error(e,ErrorCode::invalid_asset,"xpbd.distanceConstraints","expected objects");continue;}XpbdDistanceConstraint c;read_index(item,"first",c.first,e,"xpbd.distanceConstraints.first");read_index(item,"second",c.second,e,"xpbd.distanceConstraints.second");number(item,"restLength",c.rest_length,e);number(item,"compliance",c.compliance,e);number(item,"lambda",c.lambda,e);system.distance_constraints.push_back(c);}
    if(const auto* values=read_array("pinConstraints"))for(const auto& item:*values){if(!item.is_object()){error(e,ErrorCode::invalid_asset,"xpbd.pinConstraints","expected objects");continue;}XpbdPinConstraint c;read_index(item,"particle",c.particle,e,"xpbd.pinConstraints.particle");vec_read(item,"target",c.target,e);number(item,"compliance",c.compliance,e);vec_read(item,"lambda",c.lambda,e);system.pin_constraints.push_back(c);}
    if(const auto* values=read_array("bendingConstraints"))for(const auto& item:*values){if(!item.is_object()){error(e,ErrorCode::invalid_asset,"xpbd.bendingConstraints","expected objects");continue;}XpbdBendingConstraint c;read_index(item,"first",c.first,e,"xpbd.bendingConstraints.first");read_index(item,"middle",c.middle,e,"xpbd.bendingConstraints.middle");read_index(item,"third",c.third,e,"xpbd.bendingConstraints.third");number(item,"restLength",c.rest_length,e);number(item,"compliance",c.compliance,e);number(item,"lambda",c.lambda,e);system.bending_constraints.push_back(c);}
    if(const auto* values=read_array("areaConstraints"))for(const auto& item:*values){if(!item.is_object()){error(e,ErrorCode::invalid_asset,"xpbd.areaConstraints","expected objects");continue;}XpbdAreaConstraint c;read_index(item,"first",c.first,e,"xpbd.areaConstraints.first");read_index(item,"second",c.second,e,"xpbd.areaConstraints.second");read_index(item,"third",c.third,e,"xpbd.areaConstraints.third");number(item,"restArea",c.rest_area,e);number(item,"compliance",c.compliance,e);number(item,"lambda",c.lambda,e);system.area_constraints.push_back(c);}
    if(const auto* values=read_array("collisionConstraints"))for(const auto& item:*values){if(!item.is_object()){error(e,ErrorCode::invalid_asset,"xpbd.collisionConstraints","expected objects");continue;}XpbdCollisionConstraint c;read_index(item,"particle",c.particle,e,"xpbd.collisionConstraints.particle");vec_read(item,"normal",c.normal,e);number(item,"offset",c.offset,e);number(item,"compliance",c.compliance,e);number(item,"lambda",c.lambda,e);system.collision_constraints.push_back(c);}
    return e.empty();
}
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
    if (a.deformation_mesh) {
        const auto mesh = validate_deformation_mesh(*a.deformation_mesh);
        r.errors.insert(r.errors.end(), mesh.errors.begin(), mesh.errors.end());
        for (const auto& vertex : a.deformation_mesh->vertices)
            for (const auto& influence : vertex.influences)
                if (std::none_of(a.nodes.begin(), a.nodes.end(), [&](const auto& node) {
                        return node.id == influence.node_id;
                    }))
                    error(r.errors, ErrorCode::missing_resource,
                          "deformationMesh.influences", "influence node is missing");
    }
    if (a.xpbd) {
        const auto xpbd = validate_xpbd_system(*a.xpbd, 1.0F / 60.0F, 1);
        r.errors.insert(r.errors.end(), xpbd.errors.begin(), xpbd.errors.end());
        if (a.deformation_mesh &&
            a.deformation_mesh->vertices.size() != a.xpbd->particles.size())
            error(r.errors, ErrorCode::invalid_asset, "xpbd.particles",
                  "XPBD particle count must match deformation mesh vertices");
    }
    std::set<std::string> ik_ids;
    for (const auto& chain : a.ik_chains) {
        if (!core::ResourceId::is_valid(chain.id) || !ik_ids.insert(chain.id).second)
            error(r.errors, ErrorCode::duplicate_resource, "ikChains.id",
                  "IK chain ids must be valid and unique");
        if (chain.joints.size() < 2 || chain.max_iterations == 0 ||
            !std::isfinite(chain.tolerance) || chain.tolerance < 0.0F)
            error(r.errors, ErrorCode::invalid_asset, "ikChains",
                  "IK chains require at least two joints and valid solve settings");
        std::set<std::string> joint_ids;
        for (const auto& joint_id : chain.joints) {
            if (!joint_ids.insert(joint_id).second ||
                std::none_of(a.nodes.begin(), a.nodes.end(), [&](const auto& node) {
                    return node.id == joint_id;
                }))
                error(r.errors, ErrorCode::missing_resource, "ikChains.joints",
                      "IK joint node is missing or duplicated");
        }
        if (std::none_of(a.nodes.begin(), a.nodes.end(), [&](const auto& node) {
                return node.id == chain.target_node;
            }) || joint_ids.contains(chain.target_node))
            error(r.errors, ErrorCode::missing_resource, "ikChains.targetNode",
                  "IK target node must exist outside the joint chain");
    }
    return r;
}
std::vector<ResourceReference> entity_resource_references(const EntityDefinition& a){std::vector<ResourceReference> r;for(const auto& n:a.nodes){if(n.drawable.resource)r.push_back(*n.drawable.resource);if(n.drawable.material)r.push_back(*n.drawable.material);}return r;}
std::string serialize_entity(const EntityDefinition& a){Json j={{"schemaVersion",a.document.schema_version},{"type",a.document.type},{"id",a.document.id.value},{"name",a.document.name},{"nodes",Json::array()},{"constraints",Json::array()},{"deformationMesh",a.deformation_mesh?deformation_mesh_json(*a.deformation_mesh):Json(nullptr)},{"xpbd",a.xpbd?xpbd_json(*a.xpbd):Json(nullptr)},{"ikChains",Json::array()}};for(const auto& n:a.nodes){Json d={{"kind",std::string(to_string(n.drawable.kind))}};if(n.drawable.resource)d["resource"]=ref_json(*n.drawable.resource);if(n.drawable.material)d["material"]=ref_json(*n.drawable.material);j["nodes"].push_back({{"id",n.id},{"name",n.name},{"transform",transform(n.transform)},{"zOrder",n.z_order},{"drawable",d}});if(n.parent)j["nodes"].back()["parent"]=*n.parent;}for(const auto& c:a.constraints)j["constraints"].push_back(constraint_json(c));for(const auto& chain:a.ik_chains)j["ikChains"].push_back(ik_chain_json(chain));return j.dump(2)+"\n";}
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
    const auto dm = j.find("deformationMesh");
    if (dm != j.end() && !dm->is_null()) {
        if (!dm->is_object()) error(r.errors, ErrorCode::invalid_asset, "deformationMesh", "expected an object or null");
        else { DeformationMesh mesh; if (deformation_mesh_read(*dm, mesh, r.errors)) a.deformation_mesh = std::move(mesh); }
    }
    const auto xp = j.find("xpbd");
    if (xp != j.end() && !xp->is_null()) {
        if (!xp->is_object()) error(r.errors, ErrorCode::invalid_asset, "xpbd", "expected an object or null");
        else { XpbdSystem system; if (xpbd_read(*xp, system, r.errors)) a.xpbd = std::move(system); }
    }
    const auto ik = j.find("ikChains");
    if (ik != j.end()) {
        if (!ik->is_array()) error(r.errors, ErrorCode::invalid_asset, "ikChains", "expected an array");
        else for (const auto& value : *ik) {
            FabrikChainDefinition chain;
            if (value.is_object() && ik_chain_read(value, chain, r.errors)) a.ik_chains.push_back(std::move(chain));
            else if (!value.is_object()) error(r.errors, ErrorCode::invalid_asset, "ikChains", "expected objects");
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
