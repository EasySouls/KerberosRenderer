#include "NotificationManager.hpp"

#include "imgui/imgui_internal.h"

namespace Kerberos
{
    void NotificationManager::AddNotification(const std::string& message, Notification::Type type, float duration,
                                              const glm::vec4& bgColor, const glm::vec4& textColor)
    {
        m_Notifications.emplace_back(type, message, duration, m_NextId++, bgColor, textColor);
    }

    void NotificationManager::RenderNotifications()
    {
        if (m_Notifications.empty())
        {
            return;
        }

        float toasterHeight = 0.0f; /// Will be calculated per toaster
        float yOffset = 10.0f;      /// Initial offset from the bottom

        const double currentTime = ImGui::GetTime();

        /// Iterate backwards to safely remove elements
        for (int i = static_cast<int>(m_Notifications.size()) - 1; i >= 0; --i)
        {
            Notification& notif = m_Notifications[i];

            /// Update Alpha for fading in/out
            const float timeSinceStart = static_cast<float>(currentTime - notif.startTime);
            constexpr float fadeTime = 0.5f;

            if (timeSinceStart < fadeTime)
            {
                /// Fade in
                notif.currentAlpha = timeSinceStart / fadeTime;
            }
            else if (currentTime > notif.startTime + notif.duration - fadeTime && !notif.isHovered)
            {
                /// Fade out
                notif.currentAlpha = 1.0f - static_cast<float>((currentTime - (notif.startTime + notif.duration - fadeTime)) / fadeTime);
            }
            else
            {
                notif.currentAlpha = 1.0f; /// Fully visible
            }

            notif.currentAlpha = ImClamp(notif.currentAlpha, 0.0f, 1.0f); /// Clamp between 0 and 1

            /// Set position
            /// They wil be placed in the bottom-right, stacking upwards
            const ImVec2 viewportP = ImGui::GetMainViewport()->Pos;
            const ImVec2 viewportS = ImGui::GetMainViewport()->Size;

            /// Calculate target Y position for the current toaster (stacks from bottom up)
            const float targetY = viewportP.y + viewportS.y - yOffset;

            /// Animate sliding in from the right
            float animatedX = viewportP.x + viewportS.x - ImGui::GetIO().DisplaySize.x * 0.2f;
            if (timeSinceStart < fadeTime)
            {
                constexpr float slideInDistance = 150.0f;
                animatedX = viewportP.x + viewportS.x - (slideInDistance * (1.0f - notif.currentAlpha));
            }
            else
            {
                animatedX = viewportP.x + viewportS.x; /// Fully slid in (adjust for window width later)
            }


            ImGui::SetNextWindowPos(ImVec2(animatedX, targetY), ImGuiCond_Always, ImVec2(1.0f, 1.0f)); /// Align bottom-right
            ImGui::SetNextWindowBgAlpha(notif.currentAlpha * notif.color.w); /// Use the alpha for the background

            constexpr ImGuiWindowFlags windowFlags =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

            std::string windowName = "##Toaster_" + std::to_string(notif.id);
            const ImVec4 bgColor = ImVec4(notif.color.x, notif.color.y, notif.color.z, notif.color.a);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);
            const ImVec4 textColor = ImVec4(notif.textColor.x, notif.textColor.y, notif.textColor.z, notif.textColor.a);
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

            ImGui::SetNextWindowSizeConstraints(ImVec2(500, -1), ImVec2(500, FLT_MAX));

            if (ImGui::Begin(windowName.c_str(), nullptr, windowFlags))
            {
                ImGui::TextWrapped("%s", notif.message.c_str());

                notif.isHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                toasterHeight = ImGui::GetWindowHeight();

                ImGui::End();
            }
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(2);


            /// Remove expired notifications (unless hovered)
            if (!notif.isHovered && currentTime > notif.startTime + notif.duration)
            {
                m_Notifications.erase(m_Notifications.begin() + i);
            }
            else
            {
                /// Adjust yOffset for the next toaster to stack on top
                yOffset += toasterHeight + 10.0f; /// Add a small margin between toasters
            }
        }
    }
}