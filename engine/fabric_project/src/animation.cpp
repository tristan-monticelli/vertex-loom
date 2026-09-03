#include "fabric/project/animation.hpp"

#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace fabric::project {
namespace {
using Json = nlohmann::json;
void error(std::vector<Error>& e, ErrorCode c, std::string f, std::string m) { e.push_back({c, std::move(f), std::move(m)}); }
Json vec(core::Vec2 v) { return {{"x",v.x},{"y",v.y}}; }
Json color(core::Color v) { return {{"red",v.red},{"green",v.green},{"blue",v.blue},{"alpha",v.alpha}}; }
Json ref(const ResourceReference& v) { return {{"id",v.id.value},{"expectedType",v.expected_type}}; }
Json value(const AnimationValue& v) { return std::visit([](const auto& x)->Json { using T=std::decay_t<decltype(x)>; if constexpr(std::is_same_v<T,float>)return {{"kind","scalar"},{"value",x}}; else if constexpr(std::is_same_v<T,core::Vec2>)return {{"kind","vec2"},{"value",vec(x)}}; else if constexpr(std::is_same_v<T,core::Color>)return {{"kind","color"},{"value",color(x)}}; else if constexpr(std::is_same_v<T,bool>)return {{"kind","bool"},{"value",x}}; else return {{"kind","resource"},{"value",ref(x)}}; },v); }
bool text(const Json& o,const char* k,std::string& v,std::vector<Error>& e){auto i=o.find(k);if(i==o.end()||!i->is_string()){error(e,ErrorCode::invalid_asset,k,"expected a string");return false;}v=i->get<std::string>();return true;}
bool number(const Json& o,const char* k,float& v,std::vector<Error>& e){auto i=o.find(k);if(i==o.end()||(!i->is_number_float()&&!i->is_number_integer())){error(e,ErrorCode::invalid_asset,k,"expected a finite number");return false;}v=i->get<float>();if(!std::isfinite(v))error(e,ErrorCode::invalid_asset,k,"must be finite");return true;}
bool vec_read(const Json& o,core::Vec2& v,std::vector<Error>& e){auto i=o.find("x");auto j=o.find("y");if(i==o.end()||j==o.end()){error(e,ErrorCode::invalid_asset,"value","expected Vec2");return false;}return number(o,"x",v.x,e)&&number(o,"y",v.y,e);}
bool color_read(const Json& o,core::Color& v,std::vector<Error>& e){return number(o,"red",v.red,e)&&number(o,"green",v.green,e)&&number(o,"blue",v.blue,e)&&number(o,"alpha",v.alpha,e);}
bool ref_read(const Json& o,ResourceReference& v,std::vector<Error>& e){return text(o,"id",v.id.value,e)&&text(o,"expectedType",v.expected_type,e);}
bool value_read(const Json& o,AnimationValue& out,std::vector<Error>& e){std::string kind;text(o,"kind",kind,e);auto i=o.find("value");if(i==o.end()){error(e,ErrorCode::invalid_asset,"value","missing animation value");return false;}if(kind=="scalar"){float x{};if(!number(o,"value",x,e))return false;out=x;}else if(kind=="vec2"){core::Vec2 x{};if(!i->is_object()||!vec_read(*i,x,e))return false;out=x;}else if(kind=="color"){core::Color x{};if(!i->is_object()||!color_read(*i,x,e))return false;out=x;}else if(kind=="bool"){if(!i->is_boolean()){error(e,ErrorCode::invalid_asset,"value","expected boolean");return false;}out=i->get<bool>();}else if(kind=="resource"){if(!i->is_object()){error(e,ErrorCode::invalid_asset,"value","expected resource reference");return false;}ResourceReference x;if(!ref_read(*i,x,e))return false;out=std::move(x);}else error(e,ErrorCode::invalid_asset,"kind","unsupported animation value kind");return e.empty();}
ValidationReport parse_validation(const ProjectManifest& m,std::string_view s){auto r=parse_animation(m,s);return {.errors=std::move(r.errors)};}
bool finite_value(const AnimationValue& v){return std::visit([](const auto& x){using T=std::decay_t<decltype(x)>;if constexpr(std::is_same_v<T,float>)return std::isfinite(x);else if constexpr(std::is_same_v<T,core::Vec2>)return std::isfinite(x.x)&&std::isfinite(x.y);else if constexpr(std::is_same_v<T,core::Color>)return std::isfinite(x.red)&&std::isfinite(x.green)&&std::isfinite(x.blue)&&std::isfinite(x.alpha);else return true;},v);}
bool interpolatable(const AnimationValue& a,const AnimationValue& b){return a.index()==b.index()&&!std::holds_alternative<bool>(a)&&!std::holds_alternative<ResourceReference>(a);}
bool valid_tangent(const AnimationValue& tangent,const AnimationValue& value){return interpolatable(tangent,value)&&finite_value(tangent);}
AnimationValue interpolate(const AnimationValue& a,const AnimationValue& b,float t){return std::visit([&](const auto& x)->AnimationValue{using T=std::decay_t<decltype(x)>;const auto& y=std::get<T>(b);if constexpr(std::is_same_v<T,float>)return x+(y-x)*t;else if constexpr(std::is_same_v<T,core::Vec2>)return core::Vec2{x.x+(y.x-x.x)*t,x.y+(y.y-x.y)*t};else if constexpr(std::is_same_v<T,core::Color>)return core::Color{x.red+(y.red-x.red)*t,x.green+(y.green-x.green)*t,x.blue+(y.blue-x.blue)*t,x.alpha+(y.alpha-x.alpha)*t};else return x;},a);}
AnimationValue hermite(const AnimationValue& left,const AnimationValue& right,const AnimationValue& out_tangent,const AnimationValue& in_tangent,float amount,float span){const auto h00=2.0F*amount*amount*amount-3.0F*amount*amount+1.0F;const auto h10=amount*amount*amount-2.0F*amount*amount+amount;const auto h01=-2.0F*amount*amount*amount+3.0F*amount*amount;const auto h11=amount*amount*amount-amount*amount;return std::visit([&](const auto& x)->AnimationValue{using T=std::decay_t<decltype(x)>;const auto& y=std::get<T>(right);const auto& out=std::get<T>(out_tangent);const auto& in=std::get<T>(in_tangent);if constexpr(std::is_same_v<T,float>)return h00*x+h10*span*out+h01*y+h11*span*in;else if constexpr(std::is_same_v<T,core::Vec2>)return core::Vec2{h00*x.x+h10*span*out.x+h01*y.x+h11*span*in.x,h00*x.y+h10*span*out.y+h01*y.y+h11*span*in.y};else if constexpr(std::is_same_v<T,core::Color>)return core::Color{h00*x.red+h10*span*out.red+h01*y.red+h11*span*in.red,h00*x.green+h10*span*out.green+h01*y.green+h11*span*in.green,h00*x.blue+h10*span*out.blue+h01*y.blue+h11*span*in.blue,h00*x.alpha+h10*span*out.alpha+h01*y.alpha+h11*span*in.alpha};else return x;},left);}
float ease(const AnimationEasing easing,const float amount){switch(easing){case AnimationEasing::linear:return amount;case AnimationEasing::ease_in:return amount*amount;case AnimationEasing::ease_out:return 1.0F-(1.0F-amount)*(1.0F-amount);case AnimationEasing::ease_in_out:return amount<0.5F?2.0F*amount*amount:1.0F-std::pow(-2.0F*amount+2.0F,2.0F)/2.0F;}return amount;}
}
std::string_view to_string(const AnimationInterpolation v) noexcept{switch(v){case AnimationInterpolation::step:return "step";case AnimationInterpolation::linear:return "linear";case AnimationInterpolation::cubic:return "cubic";}return "linear";}
std::string_view to_string(const AnimationEasing v) noexcept{switch(v){case AnimationEasing::linear:return "linear";case AnimationEasing::ease_in:return "easeIn";case AnimationEasing::ease_out:return "easeOut";case AnimationEasing::ease_in_out:return "easeInOut";}return "linear";}
std::string_view to_string(const AnimationComposition v) noexcept{switch(v){case AnimationComposition::replace:return "replace";case AnimationComposition::additive:return "additive";}return "replace";}
std::filesystem::path animation_document_path(const ProjectManifest& m,const core::ResourceId& id){return m.directories.assets/"animations"/(id.value+".animation.json");}
ValidationReport validate_animation(const ProjectManifest&, const AnimationClip& a) {
    ValidationReport report;
    if (a.document.schema_version != current_animation_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version,
              "schemaVersion", "only animation schema version 3 is supported");
    if (a.document.type != "animation")
        error(report.errors, ErrorCode::invalid_asset, "type", "must be animation");
    if (!core::ResourceId::is_valid(a.document.id.value))
        error(report.errors, ErrorCode::invalid_resource_id, "id", "must be valid");
    if (a.document.name.empty())
        error(report.errors, ErrorCode::invalid_asset, "name", "must not be empty");
    if (a.preview_entity &&
        (!core::ResourceId::is_valid(a.preview_entity->id.value) ||
         a.preview_entity->expected_type != "entity"))
        error(report.errors, ErrorCode::resource_type_mismatch, "previewEntity",
              "must reference an entity");
    if (!std::isfinite(a.duration) || a.duration < 0.0F)
        error(report.errors, ErrorCode::invalid_asset, "duration",
              "must be finite and non-negative");

    std::set<std::string> markers;
    for (const auto& marker : a.markers) {
        if (!core::ResourceId::is_valid(marker.id) ||
            !markers.insert(marker.id).second)
            error(report.errors, ErrorCode::invalid_resource_id, "markers",
                  "marker ids must be valid and unique");
        if (!std::isfinite(marker.time) || marker.time < 0.0F ||
            marker.time > a.duration)
            error(report.errors, ErrorCode::invalid_asset, "markers.time",
                  "must be within duration");
    }

    std::set<std::string> bindings;
    for (const auto& track : a.tracks) {
        if (!core::ResourceId::is_valid(track.binding.node_id) ||
            track.binding.component_id.empty() ||
            track.binding.property_id.empty())
            error(report.errors, ErrorCode::invalid_asset, "tracks.binding",
                  "binding must have stable node, component and property ids");
        const auto binding = track.binding.node_id + "/" +
            track.binding.component_id + "/" + track.binding.property_id;
        if (!bindings.insert(binding).second)
            error(report.errors, ErrorCode::duplicate_resource, "tracks.binding",
                  "binding is declared more than once");

        float previous = -1.0F;
        for (const auto& key : track.keys) {
            if (!std::isfinite(key.time) || key.time < 0.0F ||
                key.time > a.duration || key.time < previous)
                error(report.errors, ErrorCode::invalid_asset, "tracks.keys.time",
                      "key times must be sorted within duration");
            previous = key.time;
            if (!finite_value(key.value))
                error(report.errors, ErrorCode::invalid_asset, "tracks.keys.value",
                      "value must be finite");
            if (const auto* reference =
                    std::get_if<ResourceReference>(&key.value);
                reference && !core::ResourceId::is_valid(reference->id.value))
                error(report.errors, ErrorCode::invalid_resource_id,
                      "tracks.keys.value", "resource reference id must be valid");
            if (key.in_tangent && !valid_tangent(*key.in_tangent, key.value))
                error(report.errors, ErrorCode::invalid_asset,
                      "tracks.keys.inTangent",
                      "tangent must be finite and match the key value type");
            if (key.out_tangent && !valid_tangent(*key.out_tangent, key.value))
                error(report.errors, ErrorCode::invalid_asset,
                      "tracks.keys.outTangent",
                      "tangent must be finite and match the key value type");
        }
        if (track.keys.empty())
            error(report.errors, ErrorCode::invalid_asset, "tracks.keys",
                  "track must contain a key");
        if (track.interpolation != AnimationInterpolation::step)
            for (std::size_t index = 1; index < track.keys.size(); ++index)
                if (!interpolatable(track.keys[index - 1].value,
                                    track.keys[index].value))
                    error(report.errors, ErrorCode::invalid_asset,
                          "tracks.interpolation",
                          "adjacent values cannot be interpolated");
    }
    return report;
}
std::vector<ResourceReference> animation_resource_references(const AnimationClip& a){std::vector<ResourceReference> r;for(const auto& t:a.tracks)for(const auto& k:t.keys)if(const auto* ref=std::get_if<ResourceReference>(&k.value))r.push_back(*ref);return r;}
std::string serialize_animation(const AnimationClip& animation) {
    Json document = {
        {"schemaVersion", animation.document.schema_version},
        {"type", animation.document.type},
        {"id", animation.document.id.value},
        {"name", animation.document.name},
        {"previewEntity", animation.preview_entity ? ref(*animation.preview_entity)
                                                    : Json(nullptr)},
        {"duration", animation.duration},
        {"loop", animation.loop},
        {"markers", Json::array()},
        {"tracks", Json::array()}};
    for (const auto& marker : animation.markers)
        document["markers"].push_back({{"id", marker.id}, {"time", marker.time}});
    for (const auto& track : animation.tracks) {
        Json serialized_track = {
            {"binding", {{"node", track.binding.node_id},
                         {"component", track.binding.component_id},
                         {"property", track.binding.property_id}}},
            {"interpolation", std::string(to_string(track.interpolation))},
            {"easing", std::string(to_string(track.easing))},
            {"composition", std::string(to_string(track.composition))},
            {"keys", Json::array()}};
        for (const auto& key : track.keys) {
            Json serialized_key = {{"time", key.time}, {"value", value(key.value)}};
            if (key.in_tangent)
                serialized_key["inTangent"] = value(*key.in_tangent);
            if (key.out_tangent)
                serialized_key["outTangent"] = value(*key.out_tangent);
            serialized_track["keys"].push_back(std::move(serialized_key));
        }
        document["tracks"].push_back(std::move(serialized_track));
    }
    return document.dump(2) + "\n";
}
AnimationResult parse_animation(const ProjectManifest& manifest, std::string_view source) {
    AnimationResult result;
    Json document;
    try {
        document = Json::parse(source);
    } catch (...) {
        error(result.errors, ErrorCode::invalid_json, "animation",
              "cannot parse animation JSON");
        return result;
    }
    if (!document.is_object()) {
        error(result.errors, ErrorCode::invalid_asset, "animation",
              "top-level value must be an object");
        return result;
    }

    AnimationClip animation;
    std::uint32_t source_schema = 0;
    const auto schema = document.find("schemaVersion");
    if (schema == document.end() || !schema->is_number_unsigned()) {
        error(result.errors, ErrorCode::invalid_asset, "schemaVersion",
              "expected unsigned integer");
    } else {
        source_schema = schema->get<std::uint32_t>();
        animation.document.schema_version = source_schema;
    }

    text(document, "type", animation.document.type, result.errors);
    text(document, "id", animation.document.id.value, result.errors);
    text(document, "name", animation.document.name, result.errors);

    if (source_schema >= 2) {
        const auto preview = document.find("previewEntity");
        if (preview != document.end() && !preview->is_null()) {
            if (!preview->is_object()) {
                error(result.errors, ErrorCode::invalid_asset, "previewEntity",
                      "expected a resource reference or null");
            } else {
                ResourceReference target;
                if (ref_read(*preview, target, result.errors))
                    animation.preview_entity = std::move(target);
            }
        }
    }

    number(document, "duration", animation.duration, result.errors);
    const auto loop = document.find("loop");
    if (loop == document.end() || !loop->is_boolean())
        error(result.errors, ErrorCode::invalid_asset, "loop",
              "expected boolean");
    else
        animation.loop = loop->get<bool>();

    const auto markers = document.find("markers");
    if (markers != document.end() && markers->is_array()) {
        for (const auto& serialized : *markers) {
            AnimationMarker marker;
            text(serialized, "id", marker.id, result.errors);
            number(serialized, "time", marker.time, result.errors);
            animation.markers.push_back(std::move(marker));
        }
    } else {
        error(result.errors, ErrorCode::invalid_asset, "markers",
              "expected array");
    }

    const auto tracks = document.find("tracks");
    if (tracks != document.end() && tracks->is_array()) {
        for (const auto& serialized : *tracks) {
            AnimationTrack track;
            const auto binding = serialized.find("binding");
            if (binding == serialized.end() || !binding->is_object()) {
                error(result.errors, ErrorCode::invalid_asset, "binding",
                      "expected object");
            } else {
                text(*binding, "node", track.binding.node_id, result.errors);
                text(*binding, "component", track.binding.component_id,
                     result.errors);
                text(*binding, "property", track.binding.property_id,
                     result.errors);
            }

            std::string interpolation;
            text(serialized, "interpolation", interpolation, result.errors);
            if (interpolation == "step")
                track.interpolation = AnimationInterpolation::step;
            else if (interpolation == "linear")
                track.interpolation = AnimationInterpolation::linear;
            else if (interpolation == "cubic")
                track.interpolation = AnimationInterpolation::cubic;
            else
                error(result.errors, ErrorCode::invalid_asset, "interpolation",
                      "unsupported interpolation");

            const auto easing = serialized.find("easing");
            if (easing != serialized.end()) {
                if (!easing->is_string()) {
                    error(result.errors, ErrorCode::invalid_asset, "easing",
                          "expected a string");
                } else if (easing->get<std::string>() == "easeIn") {
                    track.easing = AnimationEasing::ease_in;
                } else if (easing->get<std::string>() == "easeOut") {
                    track.easing = AnimationEasing::ease_out;
                } else if (easing->get<std::string>() == "easeInOut") {
                    track.easing = AnimationEasing::ease_in_out;
                } else if (easing->get<std::string>() != "linear") {
                    error(result.errors, ErrorCode::invalid_asset, "easing",
                          "unsupported easing");
                }
            }

            const auto composition = serialized.find("composition");
            if (composition != serialized.end()) {
                if (!composition->is_string()) {
                    error(result.errors, ErrorCode::invalid_asset, "composition",
                          "expected a string");
                } else if (composition->get<std::string>() == "additive") {
                    track.composition = AnimationComposition::additive;
                } else if (composition->get<std::string>() != "replace") {
                    error(result.errors, ErrorCode::invalid_asset, "composition",
                          "unsupported composition");
                }
            }

            const auto keys = serialized.find("keys");
            if (keys != serialized.end() && keys->is_array()) {
                for (const auto& serialized_key : *keys) {
                    AnimationKey key;
                    number(serialized_key, "time", key.time, result.errors);
                    const auto value_node = serialized_key.find("value");
                    if (value_node == serialized_key.end() ||
                        !value_node->is_object()) {
                        error(result.errors, ErrorCode::invalid_asset, "value",
                              "expected object");
                    } else {
                        value_read(*value_node, key.value, result.errors);
                    }

                    const auto read_tangent = [&](const char* field,
                                                  std::optional<AnimationValue>& target) {
                        const auto tangent = serialized_key.find(field);
                        if (tangent == serialized_key.end()) return;
                        if (!tangent->is_object()) {
                            error(result.errors, ErrorCode::invalid_asset, field,
                                  "expected animation value");
                            return;
                        }
                        AnimationValue value;
                        if (value_read(*tangent, value, result.errors))
                            target = std::move(value);
                    };
                    read_tangent("inTangent", key.in_tangent);
                    read_tangent("outTangent", key.out_tangent);
                    track.keys.push_back(std::move(key));
                }
            } else {
                error(result.errors, ErrorCode::invalid_asset, "keys",
                      "expected array");
            }
            animation.tracks.push_back(std::move(track));
        }
    } else {
        error(result.errors, ErrorCode::invalid_asset, "tracks",
              "expected array");
    }

    if (source_schema < current_animation_schema_version)
        animation.document.schema_version = current_animation_schema_version;
    if (!result.errors.empty()) return result;
    const auto validation = validate_animation(manifest, animation);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.asset = std::move(animation);
    return result;
}
AnimationResult load_animation(const std::filesystem::path& root,const ProjectManifest& m,const std::filesystem::path& path){auto s=load_document(root,path,[&](std::string_view v){return parse_validation(m,v);});AnimationResult r;r.errors=std::move(s.errors);if(s.contents)r=parse_animation(m,*s.contents);if(r.ok()&&path!=animation_document_path(m,r.asset->document.id)){r.asset.reset();error(r.errors,ErrorCode::invalid_path,"document","document filename does not match its id");}return r;}
AnimationResult publish_animation(const std::filesystem::path& root,
                                   const ProjectManifest& manifest,
                                   const AnimationClip& animation) {
    AnimationResult result;
    auto migrated = animation;
    if (migrated.document.schema_version < current_animation_schema_version)
        migrated.document.schema_version = current_animation_schema_version;
    const auto validation = validate_animation(manifest, migrated);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    const auto path = animation_document_path(manifest, migrated.document.id);
    const auto saved = save_document_atomic(
        root, path, serialize_animation(migrated),
        [&](std::string_view contents) { return parse_validation(manifest, contents); });
    if (!saved.ok()) {
        result.errors = std::move(saved.errors);
        return result;
    }
    return load_animation(root, manifest, path);
}
float interpolate_rotation_degrees(const float left, const float right, const float amount) {
    constexpr float full_turn = 360.0F;
    constexpr float half_turn = 180.0F;
    auto delta = std::fmod(right - left + half_turn, full_turn);
    if (delta < 0.0F) delta += full_turn;
    delta -= half_turn;
    return left + delta * amount;
}

EvaluationResult evaluate_animation(const AnimationClip& clip, const float time) {
    EvaluationResult result;
    const auto validation = validate_animation(ProjectManifest{}, clip);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    float evaluated_time = time;
    if (clip.loop && clip.duration > 0.0F)
        evaluated_time = std::fmod(std::max(0.0F, time), clip.duration);
    else
        evaluated_time = std::clamp(time, 0.0F, clip.duration);

    for (const auto& track : clip.tracks) {
        const auto iterator = std::upper_bound(
            track.keys.begin(), track.keys.end(), evaluated_time,
            [](const float value, const AnimationKey& key) { return value < key.time; });
        if (iterator == track.keys.begin()) {
            result.properties.push_back(
                {track.binding, track.keys.front().value, track.composition});
            continue;
        }
        if (iterator == track.keys.end()) {
            result.properties.push_back(
                {track.binding, track.keys.back().value, track.composition});
            continue;
        }

        const auto& right = *iterator;
        const auto& left = *(iterator - 1);
        if (track.interpolation == AnimationInterpolation::step ||
            !interpolatable(left.value, right.value)) {
            result.properties.push_back({track.binding, left.value, track.composition});
            continue;
        }
        const float span = right.time - left.time;
        float amount = span <= 0.0F ? 1.0F : (evaluated_time - left.time) / span;
        amount = ease(track.easing, amount);
        if (track.interpolation == AnimationInterpolation::cubic) {
            if (left.out_tangent && right.in_tangent &&
                valid_tangent(*left.out_tangent, left.value) &&
                valid_tangent(*right.in_tangent, right.value)) {
                result.properties.push_back({
                    track.binding,
                    hermite(left.value, right.value, *left.out_tangent,
                            *right.in_tangent, amount, span),
                    track.composition});
                continue;
            }
            amount = amount * amount * (3.0F - 2.0F * amount);
        }

        AnimationValue value = interpolate(left.value, right.value, amount);
        if (track.binding.component_id == "transform" &&
            track.binding.property_id == "rotationDegrees") {
            const auto* left_angle = std::get_if<float>(&left.value);
            const auto* right_angle = std::get_if<float>(&right.value);
            if (left_angle && right_angle)
                value = interpolate_rotation_degrees(*left_angle, *right_angle, amount);
        }
        result.properties.push_back({track.binding, std::move(value), track.composition});
    }
    return result;
}

std::vector<AnimationMarkerHit> animation_markers_between(
    const AnimationClip& clip, const float from_time, const float to_time) {
    std::vector<AnimationMarkerHit> hits;
    if (!(to_time > from_time) || clip.markers.empty()) return hits;

    if (!clip.loop || clip.duration <= 0.0F) {
        const auto begin = std::max(0.0F, from_time);
        const auto end = std::min(clip.duration, to_time);
        if (!(end > begin)) return hits;
        for (const auto& marker : clip.markers) {
            if (marker.time > begin && marker.time <= end)
                hits.push_back({marker.id, marker.time, marker.time, 0});
        }
        std::stable_sort(hits.begin(), hits.end(),
                         [](const auto& left, const auto& right) {
                             return left.time < right.time;
                         });
        return hits;
    }

    const auto first_loop = static_cast<std::int64_t>(
        std::floor(from_time / clip.duration));
    const auto last_loop = static_cast<std::int64_t>(
        std::floor(to_time / clip.duration));
    for (std::int64_t loop_index = first_loop; loop_index <= last_loop; ++loop_index) {
        const auto loop_start = static_cast<float>(loop_index) * clip.duration;
        for (const auto& marker : clip.markers) {
            const auto absolute_time = loop_start + marker.time;
            if (absolute_time > from_time && absolute_time <= to_time)
                hits.push_back({marker.id, absolute_time, marker.time, loop_index});
        }
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const auto& left, const auto& right) {
                         return left.time < right.time;
                     });
    return hits;
}
}
