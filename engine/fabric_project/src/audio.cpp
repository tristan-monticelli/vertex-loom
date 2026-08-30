#include "fabric/project/audio.hpp"
#include "fabric/project/document_storage.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace fabric::project {
namespace { using Json = nlohmann::json;
void fail(std::vector<Error>& e, std::string f, std::string m) { e.push_back({ErrorCode::invalid_asset,std::move(f),std::move(m)}); }
}
std::filesystem::path audio_document_path(const ProjectManifest& m, const core::ResourceId& id) { return m.directories.assets / "audio" / (id.value + ".audio.json"); }
ValidationReport validate_audio(const ProjectManifest&, const AudioDocument& a) {
    ValidationReport r; if (a.document.schema_version != current_audio_schema_version) fail(r.errors,"schemaVersion","unsupported audio schema");
    if (a.document.type != "audio") fail(r.errors,"type","must be audio");
    if (!core::ResourceId::is_valid(a.document.id.value)) fail(r.errors,"id","must be valid");
    std::vector<std::string> ids;
    for (std::size_t i=0;i<a.events.size();++i) { const auto& e=a.events[i]; const auto p="events["+std::to_string(i)+"]";
        if (!core::ResourceId::is_valid(e.id)) fail(r.errors,p+".id","must be valid");
        if (std::ranges::find(ids,e.id)!=ids.end()) fail(r.errors,p+".id","duplicate event id"); ids.push_back(e.id);
        if (e.source.empty()) fail(r.errors,p+".source","must not be empty");
        if (e.volume < 0.0F || e.volume > 1.0F) fail(r.errors,p+".volume","must be in [0,1]");
    } return r;
}
AudioResult parse_audio(const std::string_view text) { AudioResult r; Json j; try { j=Json::parse(text); } catch (...) { fail(r.errors,"audio","invalid JSON"); return r; }
    if (!j.is_object()) { fail(r.errors,"audio","expected object"); return r; } AudioDocument a;
    if (j.contains("schemaVersion")) a.document.schema_version=j["schemaVersion"].get<std::uint32_t>(); else fail(r.errors,"schemaVersion","missing");
    if (j.contains("type")) a.document.type=j["type"].get<std::string>(); else fail(r.errors,"type","missing");
    if (j.contains("id")) a.document.id.value=j["id"].get<std::string>(); else fail(r.errors,"id","missing");
    if (j.contains("name")) a.document.name=j["name"].get<std::string>(); else fail(r.errors,"name","missing");
    if (j.contains("events") && j["events"].is_array()) for (const auto& item:j["events"]) { AudioEvent e; e.id=item.value("id",""); e.source=item.value("source",""); e.volume=item.value("volume",1.0F); e.loop=item.value("loop",false); a.events.push_back(std::move(e)); } else fail(r.errors,"events","expected array");
    const auto v=validate_audio(ProjectManifest{},a); r.errors.insert(r.errors.end(),v.errors.begin(),v.errors.end()); if(r.errors.empty()) r.audio=std::move(a); return r; }
std::string serialize_audio(const AudioDocument& a) { Json j={{"schemaVersion",a.document.schema_version},{"type",a.document.type},{"id",a.document.id.value},{"name",a.document.name},{"events",Json::array()}}; for(const auto& e:a.events) j["events"].push_back({{"id",e.id},{"source",e.source},{"volume",e.volume},{"loop",e.loop}}); return j.dump(2)+"\n"; }
AudioResult load_audio(const std::filesystem::path& root,const ProjectManifest& m,const std::filesystem::path& p) { const auto s=load_document(root,p,[](std::string_view t){auto x=parse_audio(t);return ValidationReport{.errors=x.errors};}); AudioResult r; r.errors=s.errors; if(!s.contents)return r; r=parse_audio(*s.contents); return r; }
AudioResult publish_audio(const std::filesystem::path& root,const ProjectManifest& m,const AudioDocument& a) { AudioResult r; if(const auto v=validate_audio(m,a);!v.ok()){r.errors=v.errors;return r;} const auto p=audio_document_path(m,a.document.id); const auto s=save_document_atomic(root,p,serialize_audio(a),[](std::string_view t){auto x=parse_audio(t);return ValidationReport{.errors=x.errors};}); r.errors=s.errors; if(s.ok())r=load_audio(root,m,p); return r; }
} // namespace fabric::project
