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
AnimationValue interpolate(const AnimationValue& a,const AnimationValue& b,float t){return std::visit([&](const auto& x)->AnimationValue{using T=std::decay_t<decltype(x)>;const auto& y=std::get<T>(b);if constexpr(std::is_same_v<T,float>)return x+(y-x)*t;else if constexpr(std::is_same_v<T,core::Vec2>)return core::Vec2{x.x+(y.x-x.x)*t,x.y+(y.y-x.y)*t};else if constexpr(std::is_same_v<T,core::Color>)return core::Color{x.red+(y.red-x.red)*t,x.green+(y.green-x.green)*t,x.blue+(y.blue-x.blue)*t,x.alpha+(y.alpha-x.alpha)*t};else return x;},a);}
}
std::string_view to_string(const AnimationInterpolation v) noexcept{switch(v){case AnimationInterpolation::step:return "step";case AnimationInterpolation::linear:return "linear";case AnimationInterpolation::cubic:return "cubic";}return "linear";}
std::string_view to_string(const AnimationComposition v) noexcept{switch(v){case AnimationComposition::replace:return "replace";case AnimationComposition::additive:return "additive";}return "replace";}
std::filesystem::path animation_document_path(const ProjectManifest& m,const core::ResourceId& id){return m.directories.assets/"animations"/(id.value+".animation.json");}
ValidationReport validate_animation(const ProjectManifest&,const AnimationClip& a){ValidationReport r;if(a.document.schema_version!=current_animation_schema_version)error(r.errors,ErrorCode::unsupported_schema_version,"schemaVersion","only animation schema version 1 is supported");if(a.document.type!="animation")error(r.errors,ErrorCode::invalid_asset,"type","must be animation");if(!core::ResourceId::is_valid(a.document.id.value))error(r.errors,ErrorCode::invalid_resource_id,"id","must be valid");if(a.document.name.empty())error(r.errors,ErrorCode::invalid_asset,"name","must not be empty");if(!std::isfinite(a.duration)||a.duration<0.0F)error(r.errors,ErrorCode::invalid_asset,"duration","must be finite and non-negative");std::set<std::string> markers;for(const auto& m:a.markers){if(!core::ResourceId::is_valid(m.id)||!markers.insert(m.id).second)error(r.errors,ErrorCode::invalid_resource_id,"markers","marker ids must be valid and unique");if(!std::isfinite(m.time)||m.time<0||m.time>a.duration)error(r.errors,ErrorCode::invalid_asset,"markers.time","must be within duration");}std::set<std::string> bindings;for(const auto& t:a.tracks){if(!core::ResourceId::is_valid(t.binding.node_id)||t.binding.component_id.empty()||t.binding.property_id.empty())error(r.errors,ErrorCode::invalid_asset,"tracks.binding","binding must have stable node, component and property ids");const auto key=t.binding.node_id+"/"+t.binding.component_id+"/"+t.binding.property_id;if(!bindings.insert(key).second)error(r.errors,ErrorCode::duplicate_resource,"tracks.binding","binding is declared more than once");float previous=-1.0F;for(const auto& k:t.keys){if(!std::isfinite(k.time)||k.time<0||k.time>a.duration||k.time<previous)error(r.errors,ErrorCode::invalid_asset,"tracks.keys.time","key times must be sorted within duration");previous=k.time;if(!finite_value(k.value))error(r.errors,ErrorCode::invalid_asset,"tracks.keys.value","value must be finite");if(const auto* ref=std::get_if<ResourceReference>(&k.value);ref&&!core::ResourceId::is_valid(ref->id.value))error(r.errors,ErrorCode::invalid_resource_id,"tracks.keys.value","resource reference id must be valid");}if(t.keys.empty())error(r.errors,ErrorCode::invalid_asset,"tracks.keys","track must contain a key");if(t.interpolation!=AnimationInterpolation::step)for(std::size_t i=1;i<t.keys.size();++i)if(!interpolatable(t.keys[i-1].value,t.keys[i].value))error(r.errors,ErrorCode::invalid_asset,"tracks.interpolation","adjacent values cannot be interpolated");}return r;}
std::vector<ResourceReference> animation_resource_references(const AnimationClip& a){std::vector<ResourceReference> r;for(const auto& t:a.tracks)for(const auto& k:t.keys)if(const auto* ref=std::get_if<ResourceReference>(&k.value))r.push_back(*ref);return r;}
std::string serialize_animation(const AnimationClip& a){Json j={{"schemaVersion",a.document.schema_version},{"type",a.document.type},{"id",a.document.id.value},{"name",a.document.name},{"duration",a.duration},{"loop",a.loop},{"markers",Json::array()},{"tracks",Json::array()}};for(const auto& m:a.markers)j["markers"].push_back({{"id",m.id},{"time",m.time}});for(const auto& t:a.tracks){Json tr={{"binding",{{"node",t.binding.node_id},{"component",t.binding.component_id},{"property",t.binding.property_id}}},{"interpolation",std::string(to_string(t.interpolation))},{"composition",std::string(to_string(t.composition))},{"keys",Json::array()}};for(const auto& k:t.keys)tr["keys"].push_back({{"time",k.time},{"value",value(k.value)}});j["tracks"].push_back(std::move(tr));}return j.dump(2)+"\n";}
AnimationResult parse_animation(const ProjectManifest& m,std::string_view s){AnimationResult r;Json j;try{j=Json::parse(s);}catch(...){error(r.errors,ErrorCode::invalid_json,"animation","cannot parse animation JSON");return r;}if(!j.is_object()){error(r.errors,ErrorCode::invalid_asset,"animation","top-level value must be an object");return r;}AnimationClip a;auto schema=j.find("schemaVersion");if(schema==j.end()||!schema->is_number_unsigned())error(r.errors,ErrorCode::invalid_asset,"schemaVersion","expected unsigned integer");else a.document.schema_version=schema->get<std::uint32_t>();text(j,"type",a.document.type,r.errors);text(j,"id",a.document.id.value,r.errors);text(j,"name",a.document.name,r.errors);number(j,"duration",a.duration,r.errors);auto loop=j.find("loop");if(loop==j.end()||!loop->is_boolean())error(r.errors,ErrorCode::invalid_asset,"loop","expected boolean");else a.loop=loop->get<bool>();auto ms=j.find("markers");if(ms!=j.end()&&ms->is_array())for(const auto& x:*ms){AnimationMarker m;text(x,"id",m.id,r.errors);number(x,"time",m.time,r.errors);a.markers.push_back(std::move(m));}else error(r.errors,ErrorCode::invalid_asset,"markers","expected array");auto ts=j.find("tracks");if(ts!=j.end()&&ts->is_array())for(const auto& x:*ts){AnimationTrack t;auto b=x.find("binding");if(b==x.end()||!b->is_object()){error(r.errors,ErrorCode::invalid_asset,"binding","expected object");}else{text(*b,"node",t.binding.node_id,r.errors);text(*b,"component",t.binding.component_id,r.errors);text(*b,"property",t.binding.property_id,r.errors);}std::string interpolation;text(x,"interpolation",interpolation,r.errors);if(interpolation=="step")t.interpolation=AnimationInterpolation::step;else if(interpolation=="linear")t.interpolation=AnimationInterpolation::linear;else if(interpolation=="cubic")t.interpolation=AnimationInterpolation::cubic;else error(r.errors,ErrorCode::invalid_asset,"interpolation","unsupported interpolation");auto composition=x.find("composition");if(composition!=x.end()){if(!composition->is_string())error(r.errors,ErrorCode::invalid_asset,"composition","expected a string");else if(composition->get<std::string>()=="additive")t.composition=AnimationComposition::additive;else if(composition->get<std::string>()!="replace")error(r.errors,ErrorCode::invalid_asset,"composition","unsupported composition");}auto ks=x.find("keys");if(ks!=x.end()&&ks->is_array())for(const auto& k:*ks){AnimationKey key;number(k,"time",key.time,r.errors);auto v=k.find("value");if(v==k.end()||!v->is_object())error(r.errors,ErrorCode::invalid_asset,"value","expected object");else value_read(*v,key.value,r.errors);t.keys.push_back(std::move(key));}else error(r.errors,ErrorCode::invalid_asset,"keys","expected array");a.tracks.push_back(std::move(t));}else error(r.errors,ErrorCode::invalid_asset,"tracks","expected array");if(!r.errors.empty())return r;auto v=validate_animation(m,a);if(!v.ok()){r.errors=std::move(v.errors);return r;}r.asset=std::move(a);return r;}
AnimationResult load_animation(const std::filesystem::path& root,const ProjectManifest& m,const std::filesystem::path& path){auto s=load_document(root,path,[&](std::string_view v){return parse_validation(m,v);});AnimationResult r;r.errors=std::move(s.errors);if(s.contents)r=parse_animation(m,*s.contents);if(r.ok()&&path!=animation_document_path(m,r.asset->document.id)){r.asset.reset();error(r.errors,ErrorCode::invalid_path,"document","document filename does not match its id");}return r;}
AnimationResult publish_animation(const std::filesystem::path& root,const ProjectManifest& m,const AnimationClip& a){AnimationResult r;auto v=validate_animation(m,a);if(!v.ok()){r.errors=std::move(v.errors);return r;}auto p=animation_document_path(m,a.document.id);auto s=save_document_atomic(root,p,serialize_animation(a),[&](std::string_view x){return parse_validation(m,x);});if(!s.ok()){r.errors=std::move(s.errors);return r;}return load_animation(root,m,p);}
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
        if (track.interpolation == AnimationInterpolation::cubic)
            amount = amount * amount * (3.0F - 2.0F * amount);

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
