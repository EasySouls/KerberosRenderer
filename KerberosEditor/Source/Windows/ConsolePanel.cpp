#include "ConsolePanel.hpp"

#include "Widgets/EditorIcons.hpp"

#include <imgui/imgui.h>
#include  <spdlog/spdlog.h>

#include <mutex>

import Kerberos;

namespace Kerberos
{
	void ConsolePanel::OnImGuiRender()
	{
		ImGui::Begin("Console");
		EditorIcons::Draw(EditorIcon::ActionClear);
		ImGui::SameLine(0.0f, 4.0f);
		if (ImGui::Button("Clear"))
		{
			Log::GetEditorSink()->Clear();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
		ImGui::Separator();

		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_None);

		{
			std::scoped_lock lock(Log::GetEditorSink()->GetMutex());

			const auto& messages = Log::GetEditorSink()->Messages;
			for (size_t i = 0; i < messages.size(); ++i)
			{
				const auto& [Level, Text] = messages[i];

				ImGui::PushID(static_cast<int>(i));

				ImVec4 color;
				EditorIcon levelIcon = EditorIcon::LogInfo;
				switch (Level)
				{
					case spdlog::level::trace:    color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); levelIcon = EditorIcon::LogTrace; break;
					case spdlog::level::debug:    color = ImVec4(0.0f, 1.0f, 1.0f, 1.0f); levelIcon = EditorIcon::LogDebug; break;
					case spdlog::level::info:     color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); levelIcon = EditorIcon::LogInfo; break;
					case spdlog::level::warn:     color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); levelIcon = EditorIcon::LogWarning; break;
					case spdlog::level::err:      color = ImVec4(1.0f, 0.5f, 0.5f, 1.0f); levelIcon = EditorIcon::LogError; break;
					case spdlog::level::critical: color = ImVec4(1.0f, 0.25f, 0.25f, 1.0f); levelIcon = EditorIcon::LogCritical; break;
					default:                     color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); levelIcon = EditorIcon::LogInfo; break;
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				EditorIcons::Draw(levelIcon, 0.0f, &color);
				ImGui::SameLine(0.0f, 4.0f);
				if (ImGui::Selectable(Text.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) 
				{
					ImGui::SetClipboardText(Text.c_str());
				}
				ImGui::PopStyleColor();

				ImGui::Separator();
				ImGui::PopID();
			}
		}

		if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) 
		{
			ImGui::SetScrollHereY(1.0f);
		}

		ImGui::EndChild();
		ImGui::End();
	}

	void ConsolePanel::OnEvent([[maybe_unused]] Event& event)
	{
	}
}
