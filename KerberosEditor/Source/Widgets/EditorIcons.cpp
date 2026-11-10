#include "Widgets/EditorIcons.hpp"

#include "Assets/Importers/TextureImporter.hpp"
#include "Core/EnginePaths.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "VulkanContext.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace Kerberos
{
    namespace
    {
        std::array<Ref<Texture2D>, static_cast<size_t>(EditorIcon::Count)> s_Textures;
        std::array<ImTextureID, static_cast<size_t>(EditorIcon::Count)> s_TextureIDs{};
        bool s_Initialized = false;

        [[nodiscard]] size_t Index(const EditorIcon icon)
        {
            return static_cast<size_t>(icon);
        }

        [[nodiscard]] ImVec4 TextTint()
        {
            return ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }
    }

    namespace EditorIcons
    {
        void Initialize()
        {
            if (s_Initialized)
                return;

            const std::filesystem::path iconDirectory = EnginePaths::Assets() / "Editor" / "Icons";

#define KBR_LOAD_ICON(name, file) \
            s_Textures[Index(EditorIcon::name)] = TextureImporter::ImportTexture(iconDirectory / (std::string(file) + ".png"), true);
            KBR_EDITOR_ICON_LIST(KBR_LOAD_ICON)
#undef KBR_LOAD_ICON

            auto& vulkan = VulkanContext::Get();
            for (size_t i = 0; i < s_Textures.size(); ++i)
            {
                if (s_Textures[i])
                    s_TextureIDs[i] = static_cast<ImTextureID>(vulkan.GetImGuiRendererID(s_Textures[i]));
            }

            s_Initialized = true;
        }

        void Shutdown()
        {
            s_Textures.fill(nullptr);
            s_TextureIDs.fill(ImTextureID{});
            s_Initialized = false;
        }

        bool IsInitialized()
        {
            return s_Initialized;
        }

        ImTextureID GetTextureID(const EditorIcon icon)
        {
            const size_t index = Index(icon);
            return index < s_TextureIDs.size() ? s_TextureIDs[index] : ImTextureID{};
        }

        void Draw(const EditorIcon icon, const float size, const ImVec4* tint)
        {
            if (!s_Initialized)
                return;

            const ImTextureID texture = GetTextureID(icon);
            if (!texture)
                return;

            const float drawSize = size > 0.0f ? size : ImGui::GetFontSize() * 1.15f;
            const ImVec4 color = tint ? *tint : TextTint();
            ImGui::ImageWithBg(texture, ImVec2(drawSize, drawSize), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                               ImVec4(0.0f, 0.0f, 0.0f, 0.0f), color);
        }

        bool ImageButton(const char* id, const EditorIcon icon, const float size, const bool active)
        {
            const ImTextureID texture = GetTextureID(icon);
            if (!texture)
                return ImGui::Button(id, ImVec2(size, size));

            const ImVec4 background = active
                ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            return ImGui::ImageButton(id, texture, ImVec2(size, size), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                                      background, TextTint());
        }

        bool Selectable(const EditorIcon icon, const char* label, const bool selected,
                        const ImGuiSelectableFlags flags)
        {
            if (!s_Initialized)
                return ImGui::Selectable(label, selected, flags);

            Draw(icon);
            ImGui::SameLine(0.0f, 4.0f);
            return ImGui::Selectable(label, selected, flags);
        }

        bool MenuItem(const EditorIcon icon, const char* label, const char* shortcut,
                      const bool selected, const bool enabled)
        {
            if (s_Initialized)
            {
                const ImVec4 tint = enabled
                    ? TextTint()
                    : ImVec4(TextTint().x, TextTint().y, TextTint().z, TextTint().w * 0.45f);
                Draw(icon, 0.0f, &tint);
                ImGui::SameLine(0.0f, 6.0f);
            }

            return ImGui::MenuItem(label, shortcut, selected, enabled);
        }

        EditorIcon ForAssetType(const AssetType type)
        {
            switch (type)
            {
            case AssetType::Texture2D: return EditorIcon::AssetTexture2d;
            case AssetType::TextureCube: return EditorIcon::AssetTexturecube;
            case AssetType::Material: return EditorIcon::AssetMaterial;
            case AssetType::Mesh: return EditorIcon::AssetMesh;
            case AssetType::Scene: return EditorIcon::AssetScene;
            case AssetType::Sound: return EditorIcon::AssetSound;
            case AssetType::Prefab: return EditorIcon::AssetPrefab;
            case AssetType::Skeleton: return EditorIcon::AssetSkeleton;
            case AssetType::SkinnedMesh: return EditorIcon::AssetSkinnedmesh;
            case AssetType::Animation: return EditorIcon::AssetAnimation;
            case AssetType::AIStateTree: return EditorIcon::ComponentAiStateMachine;
            default: return EditorIcon::File;
            }
        }
    }
}
