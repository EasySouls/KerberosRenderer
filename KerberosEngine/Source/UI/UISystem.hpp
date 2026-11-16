#pragma once

#include "Scene/Components/UIComponents.hpp"
#include "UI/UITypes.hpp"

#include <entt/single_include/entt/entt.hpp>
#include <glm/glm.hpp>

#include <functional>
#include <unordered_set>
#include <vector>

namespace Kerberos {

class Scene;

/// Resolves UI components into layout, runs pointer/keyboard interaction and
/// produces a batched vertex/index list for the renderer. The system is owned
/// by Scene so a runtime scene copy has its own transient hover/click state.
class UISystem
{
public:
    /// Resolves layout and (when interact is true) updates hover/press/click/focus.
    /// Interact is false in Edit/Simulate so authored UI stays visible while editing.
    void Update(Scene& scene, glm::vec2 screenSize, const glm::mat4& viewProjection,
                const UIInputState& input, bool interact);

    /// Rebuilds the draw data from live component values. Called by the renderer
    /// during command recording so script changes are visible in the same frame.
    const UIDrawData& BuildDrawData(Scene& scene, glm::vec2 screenSize,
                                    const glm::mat4& viewProjection,
                                    const std::function<uint32_t(const AssetHandle&)>& resolveTexture);

    /// Clears transient interaction state on every UI element (Play start/stop).
    void ResetTransientState(Scene& scene);

    /// True when a modal canvas is visible or a text field owns the keyboard.
    bool IsInputCaptured() const { return m_InputCaptured; }

    const UIDrawData& GetDrawData() const { return m_DrawData; }

private:
    struct ResolvedElement
    {
        entt::entity Entity = entt::null;
        UIElementComponent* Element = nullptr;
        UICanvasComponent* Canvas = nullptr;
        int CanvasSortOrder = 0;
        /// Painter order inside the canvas / world layer.
        int Order = 0;
        bool WorldSpace = false;
        float WorldDepth = 0.0f;
        UIRect Rect{};
        UIRect ClipRect{};
        bool ClipEnabled = false;
        float Scale = 1.0f;
    };

    void ResolveElements(Scene& scene, glm::vec2 screenSize, const glm::mat4& viewProjection);

    void LayoutEntity(Scene& scene, entt::entity entity, UICanvasComponent* canvas,
                      int canvasSortOrder, float scale, const UIRect& parentRect,
                      const UIRect& parentClip, bool parentClipEnabled, int& order,
                      std::unordered_set<uint32_t>& visited);

    static UIElementComponent* TryGetElement(Scene& scene, entt::entity entity);

    static void AppendElementDrawData(const ResolvedElement& resolved, 
                                      UIDrawData& out,
                                      const std::function<uint32_t(const AssetHandle&)>& resolveTexture);
    
private:
    std::vector<ResolvedElement> m_Elements;
    UIDrawData m_DrawData;

    UIInputState m_PreviousInput{};
    entt::entity m_PressedEntity = entt::null;
    entt::entity m_FocusedEntity = entt::null;
    bool m_InputCaptured = false;
};

}
