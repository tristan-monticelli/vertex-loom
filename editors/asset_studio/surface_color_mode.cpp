#include "surface_color_mode.hpp"

#include <imgui.h>

#include <ranges>

namespace fabric::asset_studio {

bool draw_surface_color_mode(
    project::ShaderSurfaceSettings& shader,
    const char* identifier) {
    using Effect = project::SurfaceEffect;
    using Kind = project::SurfaceEffectKind;
    const bool guided = shader.classification == project::TextureClassification::beam ||
        shader.classification == project::TextureClassification::button_eye;
    if (!guided) return false;
    ImGui::PushID(identifier);
    const bool beam = shader.classification == project::TextureClassification::beam;
    const auto find_effect = [&](const Kind kind) {
        return std::ranges::find(shader.effects, kind, &Effect::kind);
    };
    const auto set_effect = [&](const Kind kind, const core::Color color,
                                const float amount) {
        auto effect = find_effect(kind);
        if (effect == shader.effects.end())
            shader.effects.push_back(Effect{.kind = kind, .color = color,
                                            .amount = amount});
        else {
            effect->enabled = true;
            effect->color = color;
            effect->amount = amount;
        }
    };
    const auto sync_legacy = [&] {
        if (const auto tint = find_effect(Kind::tint); tint != shader.effects.end())
            shader.primary_color = tint->color;
        if (const auto glow = find_effect(Kind::holography);
            glow != shader.effects.end()) {
            shader.effect_color = glow->color;
            shader.holography = glow->amount;
        }
        if (const auto shine = find_effect(Kind::shine);
            shine != shader.effects.end()) shader.shine = shine->amount;
    };
    ImGui::SeparatorText("Quick look");
    ImGui::TextDisabled("Preserve the source or recolor it with your selected color.");
    auto tint = find_effect(Kind::tint);
    const bool source = beam ? shader.profile != project::SurfaceShaderProfile::thread
                             : tint == shader.effects.end() || tint->amount <= 0.0F;
    bool changed = false;
    if (ImGui::BeginCombo("Traitement des couleurs",
                          source ? "Source intacte" : "Recoloration")) {
        if (ImGui::Selectable("Source intacte", source)) {
            set_effect(Kind::tint, {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F);
            shader.profile = project::SurfaceShaderProfile::plastic;
            changed = true;
        }
        if (ImGui::Selectable("Recoloration", !source)) {
            const auto color = tint == shader.effects.end()
                ? core::Color{1.0F, 1.0F, 1.0F, 1.0F} : tint->color;
            set_effect(Kind::tint, color, 1.0F);
            shader.profile = beam ? project::SurfaceShaderProfile::thread
                                  : project::SurfaceShaderProfile::custom;
            changed = true;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Réinitialiser depuis la source")) {
        shader.profile = project::SurfaceShaderProfile::plastic;
        set_effect(Kind::tint, {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F);
        set_effect(Kind::holography, {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F);
        set_effect(Kind::shine, {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F);
        changed = true;
    }
    if (tint = find_effect(Kind::tint); tint != shader.effects.end()) {
        bool quick = ImGui::ColorEdit4("Base color##quick", &tint->color.red);
        quick |= ImGui::SliderFloat("Recolor strength##quick", &tint->amount,
                                    0.0F, 1.0F, "%.2f");
        if (quick) {
            tint->enabled = true;
            shader.profile = tint->amount <= 0.0F
                ? project::SurfaceShaderProfile::plastic
                : beam ? project::SurfaceShaderProfile::thread
                       : project::SurfaceShaderProfile::custom;
            changed = true;
        }
    }
    if (auto glow = find_effect(Kind::holography);
        glow != shader.effects.end()) {
        bool quick = ImGui::ColorEdit4("Glow color##quick", &glow->color.red);
        quick |= ImGui::SliderFloat("Glow strength##quick", &glow->amount,
                                    0.0F, 1.0F, "%.2f");
        if (quick) { glow->enabled = true; changed = true; }
    }
    if (auto shine = find_effect(Kind::shine); shine != shader.effects.end()) {
        if (ImGui::SliderFloat("Highlight##quick", &shine->amount,
                               0.0F, 1.0F, "%.2f")) {
            shine->enabled = true;
            changed = true;
        }
    }
    if (changed) sync_legacy();
    ImGui::PopID();
    return changed;
}

} // namespace fabric::asset_studio
